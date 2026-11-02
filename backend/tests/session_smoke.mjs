import { spawn } from "node:child_process";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const exePath = process.argv[2];
if (!exePath) {
    console.error("Usage: node backend/tests/session_smoke.mjs <path-to-AnimeAnBackend.exe>");
    process.exit(1);
}

const token = `smoke_${Date.now()}_${Math.random().toString(36).slice(2)}`;

const extraPaths = [
    path.win32.normalize("C:/Qt/6.9.1/mingw_64/bin"),
    path.win32.normalize("C:/Qt/Tools/mingw1310_64/bin"),
    path.win32.normalize("C:/Users/admin/Documents/AnimeAn/tools/python312"),
    path.dirname(path.resolve(exePath)),
];
const currentPath = process.env.PATH || process.env.Path || "";
const childEnv = {
    ...process.env,
    PATH: [...extraPaths, currentPath].join(";"),
};

const child = spawn(exePath, ["--port", "0", "--token", token], {
    stdio: ["ignore", "pipe", "pipe"],
    env: childEnv,
});

let stdoutBuf = "";
let stderrBuf = "";
child.stderr.on("data", (chunk) => {
    stderrBuf += chunk.toString("utf8");
});

function cleanupAndExit(code, err) {
    if (err) {
        console.error(err);
        if (stderrBuf) {
            console.error("Backend stderr:\n" + stderrBuf);
        }
    }
    try {
        child.kill();
    } catch {}
    process.exit(code);
}

const globalTimeout = setTimeout(() => {
    cleanupAndExit(1, new Error("Timed out running session_smoke.mjs"));
}, 30000);

async function waitForPort() {
    return new Promise((resolve, reject) => {
        const onData = (chunk) => {
            stdoutBuf += chunk.toString("utf8");
            const match = stdoutBuf.match(/ANIMEAN_BACKEND_PORT=(\d+)/);
            if (match) {
                child.stdout.off("data", onData);
                resolve(Number(match[1]));
            }
        };
        child.stdout.on("data", onData);
        child.once("exit", (code) => {
            reject(new Error(`Backend exited early with code ${code}. stderr: ${stderrBuf}`));
        });
    });
}

try {
    const port = await waitForPort();
    assert(port > 0 && port <= 65535, `Invalid port: ${port}`);

    const stateMap = new Map();
    const receivedEvents = [];
    const stateWaiters = [];
    const boardWaiters = [];
    const pendingCalls = new Map();

    const fullOpsMain = [];
    let latestMainFrame = null;

    const ws = new WebSocket(`ws://127.0.0.1:${port}/?token=${encodeURIComponent(token)}`);
    ws.binaryType = "arraybuffer";
    const receivedFields = [];
    const messageSequence = [];
    let pendingFieldHeader = null;
    let autoAckMain = true;
    let unackedMainFrame = null;

    const notifyStateWaiters = () => {
        for (let i = stateWaiters.length - 1; i >= 0; --i) {
            if (stateWaiters[i].predicate(stateMap)) {
                const w = stateWaiters.splice(i, 1)[0];
                w.resolve();
            }
        }
    };

    const notifyBoardWaiters = () => {
        for (let i = boardWaiters.length - 1; i >= 0; --i) {
            if (boardWaiters[i].predicate(latestMainFrame)) {
                const w = boardWaiters.splice(i, 1)[0];
                w.resolve(latestMainFrame);
            }
        }
    };

    const waitForState = (predicate, desc, timeoutMs = 10000) => {
        if (predicate(stateMap)) {
            return Promise.resolve();
        }
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                reject(new Error(`Timed out waiting for state: ${desc}`));
            }, timeoutMs);
            stateWaiters.push({
                predicate,
                resolve: () => {
                    clearTimeout(timer);
                    resolve();
                },
            });
        });
    };

    const waitForBoard = (predicate, desc, timeoutMs = 10000) => {
        if (latestMainFrame && predicate(latestMainFrame)) {
            return Promise.resolve(latestMainFrame);
        }
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                reject(new Error(`Timed out waiting for board frame: ${desc}`));
            }, timeoutMs);
            boardWaiters.push({
                predicate,
                resolve: (frame) => {
                    clearTimeout(timer);
                    resolve(frame);
                },
            });
        });
    };

    let nextCallId = 1;
    const callBackend = (method, params = {}) => {
        const id = nextCallId++;
        ws.send(JSON.stringify({ t: "call", id, method, params }));
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                pendingCalls.delete(id);
                reject(new Error(`Timed out waiting for reply to ${method} (id=${id})`));
            }, 10000);
            pendingCalls.set(id, {
                resolve: (msg) => {
                    clearTimeout(timer);
                    resolve(msg);
                },
                reject: (err) => {
                    clearTimeout(timer);
                    reject(err);
                },
            });
        });
    };

    await new Promise((resolve, reject) => {
        ws.addEventListener("open", () => resolve(), { once: true });
        ws.addEventListener("error", (err) => reject(new Error(`WebSocket error: ${err?.message ?? err}`)), { once: true });
    });

    ws.addEventListener("message", (event) => {
        if (typeof event.data !== "string") {
            if (pendingFieldHeader && event.data instanceof ArrayBuffer) {
                const floats = Array.from(new Float32Array(event.data));
                receivedFields.push({
                    header: pendingFieldHeader,
                    byteLength: event.data.byteLength,
                    floats,
                });
                messageSequence.push({
                    type: "field_bin",
                    key: pendingFieldHeader.key,
                    version: pendingFieldHeader.version,
                    byteLength: event.data.byteLength,
                });
                pendingFieldHeader = null;
            }
            return;
        }
        const msg = JSON.parse(event.data);
        if (msg.t === "state") {
            if (msg.key === "event") {
                receivedEvents.push(msg.value);
            }
            stateMap.set(msg.key, msg.value);
            notifyStateWaiters();
        } else if (msg.t === "field") {
            pendingFieldHeader = msg;
            messageSequence.push({
                type: "field_hdr",
                key: msg.key,
                version: msg.version,
                w: msg.w,
                h: msg.h,
                bytes: msg.bytes,
            });
        } else if (msg.t === "board") {
            messageSequence.push({ type: "board", view: msg.view, frame: msg.frame });
            if (msg.view === "main" && !autoAckMain) {
                unackedMainFrame = msg.frame;
            } else {
                ws.send(JSON.stringify({ t: "ack", view: msg.view, frame: msg.frame }));
            }
            if (msg.view === "main") {
                fullOpsMain.length = msg.keep;
                if (Array.isArray(msg.ops)) {
                    fullOpsMain.push(...msg.ops);
                }
                latestMainFrame = {
                    frame: msg.frame,
                    w: msg.w,
                    h: msg.h,
                    keep: msg.keep,
                    opsCount: Array.isArray(msg.ops) ? msg.ops.length : 0,
                    totalOps: fullOpsMain.length,
                };
                notifyBoardWaiters();
            }
        } else if (msg.t === "reply") {
            const pending = pendingCalls.get(msg.id);
            if (pending) {
                pendingCalls.delete(msg.id);
                pending.resolve(msg);
            }
        }
    });

    // 1. Check all required state keys arrive
    const requiredStateKeys = [
        "window",
        "theme",
        "menus",
        "tools",
        "toolOptions",
        "drawColor",
        "drawSettings",
        "layers/main",
        "layers/child",
        "assets/main",
        "assets/child",
        "timeline",
        "history",
        "windows",
        "cursor/main",
        "cursor/child",
        "viewport/main",
        "viewport/child",
        "freeze",
        "pad",
        "dialogs",
    ];

    await waitForState(
        (map) => requiredStateKeys.every((k) => map.has(k)),
        `all initial state keys (${requiredStateKeys.join(", ")})`
    );
    assert.equal(receivedEvents.length, 0, "Should not receive empty event on initial connection");

    // Verify viewport/main fields
    const vpMainInitial = stateMap.get("viewport/main");
    assert(vpMainInitial && typeof vpMainInitial === "object", "viewport/main must be an object");
    for (const axis of ["h", "v"]) {
        const bar = vpMainInitial[axis];
        assert(bar && typeof bar === "object", `viewport/main.${axis} must be an object`);
        for (const numField of ["min", "max", "page", "value"]) {
            assert.equal(
                typeof bar[numField],
                "number",
                `viewport/main.${axis}.${numField} must be a number`
            );
        }
        assert.equal(
            typeof bar.enabled,
            "boolean",
            `viewport/main.${axis}.enabled must be a boolean`
        );
    }

    // 2. Check the menu bar in exact order. Menus registered while initalize.py
    // runs count too (Arrow, user-workflow.md step 3); Settings is Python's and
    // sits after Windows.
    const menus = stateMap.get("menus");
    assert(Array.isArray(menus), "menus state should be an array");
    const menuTitles = menus.map((m) => String(m.text ?? "").replace(/&/g, ""));
    const expectedMenus = ["File", "Edit", "Auto Mapping", "Draw", "Arrow", "Theme", "Windows", "Settings"];
    assert.deepEqual(
        menuTitles,
        expectedMenus,
        `Expected top-level menus to be exactly [${expectedMenus.join(", ")}], got [${menuTitles.join(", ")}]`
    );

    // 2b. Check timeline and windows state reflect timeline open/collapse/float/dock settings
    const initialTimeline = stateMap.get("timeline");
    const initialWindows = stateMap.get("windows");
    assert(initialTimeline && typeof initialTimeline === "object", "timeline state must be an object");
    assert.equal(typeof initialTimeline.visible, "boolean", "timeline.visible must be boolean");
    assert.equal(typeof initialTimeline.collapsed, "boolean", "timeline.collapsed must be boolean");
    assert.equal(typeof initialTimeline.floating, "boolean", "timeline.floating must be boolean");
    assert.equal(typeof initialTimeline.vertical, "boolean", "timeline.vertical must be boolean");
    assert.equal(typeof initialTimeline.leftAligned, "boolean", "timeline.leftAligned must be boolean");
    assert(
        initialWindows &&
            initialWindows.timeline &&
            typeof initialWindows.timeline === "object",
        "windows.timeline must be an object"
    );
    assert.equal(initialWindows.timeline.visible, initialTimeline.visible);
    assert.equal(initialWindows.timeline.collapsed, initialTimeline.collapsed);
    assert.equal(initialWindows.timeline.floating, initialTimeline.floating);
    assert.equal(initialWindows.timeline.vertical, initialTimeline.vertical);
    assert.equal(initialWindows.timeline.leftAligned, initialTimeline.leftAligned);

    // 3. Check board frame arrives
    const firstBoardFrame = await waitForBoard(
        (f) => f !== null && f.totalOps > 0,
        "initial main board frame with ops > 0"
    );
    assert(firstBoardFrame.totalOps > 0, "Expected initial board frame to have ops");

    // 4. Call layer.add -> layers/main has one more layer
    const initialLayersMain = stateMap.get("layers/main");
    assert(initialLayersMain && Array.isArray(initialLayersMain.rows), "layers/main.rows must be an array");
    const initialRowCount = initialLayersMain.rows.length;

    const addLayerReply = await callBackend("layer.add", { view: "main" });
    assert.equal(addLayerReply.ok, true, `layer.add failed: ${addLayerReply.error}`);

    await waitForState(
        (map) => {
            const l = map.get("layers/main");
            return l && Array.isArray(l.rows) && l.rows.length === initialRowCount + 1;
        },
        `layers/main.rows.length === ${initialRowCount + 1}`
    );

    // 5. Simulate press, move, release with Pen -> board frame ops increase
    const frameBeforeStroke = latestMainFrame;
    const opsBeforeStroke = frameBeforeStroke.totalOps;
    const frameIdBeforeStroke = frameBeforeStroke.frame;

    ws.send(
        JSON.stringify({
            t: "input",
            view: "main",
            kind: "press",
            x: 300,
            y: 250,
            button: 1,
            buttons: 1,
            modifiers: 0,
        })
    );
    ws.send(
        JSON.stringify({
            t: "input",
            view: "main",
            kind: "move",
            x: 350,
            y: 280,
            button: 0,
            buttons: 1,
            modifiers: 0,
        })
    );
    ws.send(
        JSON.stringify({
            t: "input",
            view: "main",
            kind: "move",
            x: 410,
            y: 320,
            button: 0,
            buttons: 1,
            modifiers: 0,
        })
    );
    ws.send(
        JSON.stringify({
            t: "input",
            view: "main",
            kind: "release",
            x: 410,
            y: 320,
            button: 1,
            buttons: 0,
            modifiers: 0,
        })
    );

    const frameAfterStroke = await waitForBoard(
        (f) => f !== null && f.frame > frameIdBeforeStroke && f.totalOps > opsBeforeStroke,
        `board frame after Pen stroke with totalOps > ${opsBeforeStroke}`
    );
    assert(
        frameAfterStroke.totalOps > opsBeforeStroke,
        `Expected totalOps after stroke (${frameAfterStroke.totalOps}) > before stroke (${opsBeforeStroke})`
    );

    // 6. Call history.undo -> board ops return to pre-stroke count
    const frameIdBeforeUndo = latestMainFrame.frame;
    const undoReply = await callBackend("history.undo", {});
    assert.equal(undoReply.ok, true, `history.undo failed: ${undoReply.error}`);

    const frameAfterUndo = await waitForBoard(
        (f) => f !== null && f.frame > frameIdBeforeUndo && f.totalOps === opsBeforeStroke,
        `board frame after undo with totalOps === ${opsBeforeStroke}`
    );
    assert.equal(
        frameAfterUndo.totalOps,
        opsBeforeStroke,
        `Expected totalOps after undo (${frameAfterUndo.totalOps}) === before stroke (${opsBeforeStroke})`
    );

    // 6a. Undo and redo bring up the board they land on, whichever page is showing.
    // The page says where the texture board lives whenever its tab changes, as app.js does.
    const paintPage = (map) => map.get("windows")?.list?.find((w) => w.name === "paint")?.current;
    const sendStroke = (view, y) => {
        for (const [kind, x, button, buttons] of [
            ["press", 100, 1, 1],
            ["move", 170, 0, 1],
            ["move", 240, 0, 1],
            ["release", 240, 1, 0],
        ]) {
            ws.send(JSON.stringify({ t: "input", view, kind, x, y, button, buttons, modifiers: 0 }));
        }
    };
    const showPage = async (page) => {
        await callBackend("texture.setHome", { home: page === "texture" ? "central" : "parked", subControlVisible: false });
        await callBackend("view.setActive", { view: page === "texture" ? "child" : "main" });
    };
    const showing = (view, index, desc) =>
        waitForState(
            (map) =>
                map.get("window")?.activeView === view &&
                map.get("history")?.view === view &&
                map.get("history")?.currentIndex === index,
            desc
        );
    const eventsSince = (start, name, page) =>
        receivedEvents
            .slice(start)
            .filter((e) => e.name === name && (page === undefined || (e.args?.name === "paint" && e.args?.page === page))).length;
    const pageAfter = async (action, wanted, desc) => {
        const start = receivedEvents.length;
        await callBackend(action, {});
        await showing(wanted.view, wanted.index, desc);
        const toDrawing = eventsSince(start, "window.select", "drawing");
        assert.equal(toDrawing, wanted.page === "drawing" ? 1 : 0, `${desc}: window.select drawing`);
        // Every restore on the texture board surfaces it; the page leaves a board that is up alone.
        if (wanted.page === "texture") {
            assert.equal(eventsSince(start, "textureView.surface"), 1, `${desc}: textureView.surface`);
        }
        if (wanted.page) {
            await callBackend("texture.setHome", { home: wanted.page === "texture" ? "central" : "parked", subControlVisible: false });
        }
    };

    await callBackend("tool.select", { tool: "Pen" });
    await callBackend("view.setActive", { view: "child" });
    await waitForState((map) => map.get("history")?.view === "child", "the texture board's history");
    const childBase = stateMap.get("history").currentIndex;
    await callBackend("view.setActive", { view: "main" });
    await waitForState((map) => map.get("history")?.view === "main", "the main board's history");
    const mainBase = stateMap.get("history").currentIndex;

    await showing("main", mainBase, "starting on the Drawing page");
    sendStroke("main", 120);
    await showing("main", mainBase + 1, "the main stroke is in the history");
    await showPage("texture");
    await showing("child", childBase, "the Texture page is up");
    sendStroke("child", 120);
    await showing("child", childBase + 1, "the texture stroke is in the history");

    await pageAfter("history.undo", { view: "child", index: childBase }, "undo on the same board leaves the page alone");
    await pageAfter("history.undo", { view: "main", index: mainBase, page: "drawing" },
                    "undo landing on the main board brings up the Drawing page");
    await pageAfter("history.redo", { view: "main", index: mainBase + 1 }, "redo on the same board leaves the page alone");
    await pageAfter("history.redo", { view: "child", index: childBase + 1, page: "texture" },
                    "redo landing on the texture board brings up the Texture page");
    await pageAfter("history.undo", { view: "child", index: childBase }, "undo on the texture board again");
    await pageAfter("history.undo", { view: "main", index: mainBase, page: "drawing" }, "and on to the main board");
    await showPage("texture");
    await showing("child", childBase, "back on the Texture page");
    await pageAfter("history.redo", { view: "main", index: mainBase + 1, page: "drawing" },
                    "redo landing on the main board brings up the Drawing page");
    await pageAfter("history.undo", { view: "main", index: mainBase }, "back to where step 6 left the board");

    // 6b. The paint window's page is the board with the focus, or the one that had it last
    const focusBoard = (view, focused) => ws.send(JSON.stringify({ t: "input", view, kind: "focus", focused }));
    const settle = () => new Promise((resolve) => setTimeout(resolve, 300));
    await callBackend("texture.setHome", { home: "control", subControlVisible: true });
    focusBoard("child", true);
    await waitForState((map) => paintPage(map) === "texture" && map.get("window")?.activeView === "child",
                       "focus on the texture board in its own control names the texture page");
    focusBoard("child", false);
    await settle();
    assert.equal(paintPage(stateMap), "texture", "with the focus gone the texture board, which had it last, still counts");
    focusBoard("main", true);
    await waitForState((map) => paintPage(map) === "drawing" && map.get("window")?.activeView === "main",
                       "focus on the main board names the drawing page");
    focusBoard("main", false);
    await settle();
    assert.equal(paintPage(stateMap), "drawing", "with the focus gone the main board, which had it last, still counts");
    await callBackend("texture.setHome", { home: "parked", subControlVisible: false });

    // 7. Call tool.select to switch to Arrow -> tools changes, toolOptions receives Python JSON
    stateMap.delete("toolOptions");
    const toolSelectReply = await callBackend("tool.select", { tool: "Arrow" });
    assert.equal(toolSelectReply.ok, true, `tool.select failed: ${toolSelectReply.error}`);

    await waitForState(
        (map) => {
            const tools = map.get("tools");
            const toolOptions = map.get("toolOptions");
            const isArrow =
                tools &&
                (tools.tool === "arrow" ||
                    tools.tool === "Arrow" ||
                    tools.toolEnum === "Arrow");
            const hasPythonOptions =
                toolOptions &&
                typeof toolOptions === "object" &&
                Array.isArray(toolOptions.controls) &&
                toolOptions.controls.some((c) => c && c.name === "edit_mode");
            return Boolean(isArrow && hasPythonOptions);
        },
        "tools switched to Arrow and toolOptions received Python JSON"
    );

    // 7b. On a scaled screen the pen ring's bitmap has the screen's pixels and its
    // hotspot logical ones, so the state has to carry the ratio between them.
    const sizeBeforeRatio = stateMap.get("viewport/main");
    const sendRatio = (dpr) =>
        ws.send(
            JSON.stringify({
                t: "input",
                view: "main",
                kind: "resize",
                w: sizeBeforeRatio.h.page,
                h: sizeBeforeRatio.v.page,
                dpr,
            })
        );
    sendRatio(1.25);
    await new Promise((resolve) => setTimeout(resolve, 100));
    const penReply = await callBackend("tool.select", { tool: "Pen" });
    assert.equal(penReply.ok, true, `tool.select failed: ${penReply.error}`);
    await waitForState(
        (map) => map.get("cursor/main")?.scale === 1.25,
        "pen cursor declares the screen's ratio"
    );
    const penCursor = stateMap.get("cursor/main");
    assert.equal(penCursor.shape, "bitmap");
    assert(
        Math.abs(penCursor.hx - penCursor.w / penCursor.scale / 2) <= 1,
        `Hotspot should be the logical centre: ${JSON.stringify(penCursor)}`
    );
    sendRatio(1);
    await callBackend("tool.select", { tool: "Arrow" });

    // 8. Call board.scroll -> viewport/main value changes
    const vpBeforeScroll = stateMap.get("viewport/main");
    const oldHValue = vpBeforeScroll.h.value;
    const nextHValue =
        oldHValue + 50 <= vpBeforeScroll.h.max
            ? oldHValue + 50
            : Math.max(vpBeforeScroll.h.min, oldHValue - 50);
    assert.notEqual(nextHValue, oldHValue, "Scrollable range on viewport/main.h must allow a different value");

    const scrollReply = await callBackend("board.scroll", {
        view: "main",
        axis: "h",
        value: nextHValue,
    });
    assert.equal(scrollReply.ok, true, `board.scroll failed: ${scrollReply.error}`);

    await waitForState(
        (map) => {
            const vp = map.get("viewport/main");
            return vp && vp.h && vp.h.value === nextHValue;
        },
        `viewport/main.h.value === ${nextHValue}`
    );

    // 9. Dialogs are Python's (dialogs.py). Startup asks the canvas size, File
    // items put pickers up, answers go back through dialog.result, and C++
    // drops a dialog whose window closed or whose answer Python could not take.
    const openDialogs = () => stateMap.get("dialogs") ?? [];
    const waitDialogs = (predicate, desc) => waitForState((map) => predicate(map.get("dialogs") ?? []), desc);
    const runPython = async (command) => {
        const reply = await callBackend("python.run", { command });
        assert.equal(reply.ok, true, `python.run failed: ${reply.error}`);
        return reply;
    };
    await waitDialogs((list) => list.some((d) => d.kind === "canvasSize"), "the canvas size asked at startup");
    const startupDialog = openDialogs().find((d) => d.kind === "canvasSize");
    assert.equal(startupDialog.modal, true, "the startup canvas size is modal");
    assert.equal(startupDialog.title, "New Project");
    assert.equal(startupDialog.width, stateMap.get("window").canvasWidth, "it asks on the current canvas");
    const cancelReply = await callBackend("dialog.result", { id: startupDialog.id, action: "cancel" });
    assert.equal(cancelReply.ok, true, `dialog.result cancel failed: ${cancelReply.error}`);
    await waitDialogs((list) => list.length === 0, "Cancel closes the startup canvas size");

    const openReply = await callBackend("menu.trigger", { id: "file.open" });
    assert.equal(openReply.ok, true, `menu.trigger file.open failed: ${openReply.error}`);
    await waitDialogs((list) => list.some((d) => d.kind === "file" && d.title === "Open Project"), "File > Open... puts a picker up");
    const openDialog = openDialogs().find((d) => d.title === "Open Project");
    assert.equal(openDialog.mode, "open");
    assert.deepEqual(openDialog.filters, [{ name: "AnimeAn Projects", extensions: ["anproj"] }]);
    await callBackend("dialog.result", { id: openDialog.id, action: "cancel", value: [] });
    await waitDialogs((list) => list.length === 0, "a cancelled picker closes");

    // A window closed from Windows: C++ drops it at once, and Python hears of it.
    await runPython("import dialogs; dialogs.message('Note', 'Closed from Windows')");
    await waitDialogs((list) => list.length === 1 && list[0].kind === "message", "a message dialog");
    const closedId = openDialogs()[0].id;
    await callBackend("dialog.result", { id: closedId, action: "closed" });
    await waitDialogs((list) => list.length === 0, "closing the window drops the dialog");
    const stackAfterClose = await runPython("import dialogs; print(len(dialogs.stack()))");
    assert.equal(String(stackAfterClose.result?.output ?? "").trim(), "0", "Python dropped it too");

    // An answer Python cannot take: C++ drops the dialog, and a stale stack
    // sent later cannot bring it back.
    const saveAsReply = await callBackend("menu.trigger", { id: "file.saveAs" });
    assert.equal(saveAsReply.ok, true, `menu.trigger file.saveAs failed: ${saveAsReply.error}`);
    await waitDialogs((list) => list.some((d) => d.title === "Save Project As"), "File > Save As... puts a picker up");
    const saveAsDialog = openDialogs().find((d) => d.title === "Save Project As");
    await runPython("import dialogs; dialogs._kept_deliver = dialogs._deliver; dialogs._deliver = lambda *a: (_ for _ in ()).throw(RuntimeError('broken answer'))");
    const brokenReply = await callBackend("dialog.result", { id: saveAsDialog.id, action: "accept", value: ["C:/x.anproj"] });
    assert.equal(brokenReply.ok, false, "a failing answer is reported");
    assert.match(String(brokenReply.error), /broken answer/);
    await waitDialogs((list) => list.length === 0, "a failing answer still drops the dialog");
    await runPython("import dialogs; dialogs._publish()");
    await runPython("import dialogs; dialogs._deliver = dialogs._kept_deliver; dialogs._STACK.clear(); dialogs._publish()");
    assert.equal(openDialogs().length, 0, "the stale stack did not bring the dropped dialog back");

    // A script's settings window is opened by Python from the menu item's name.
    const settingsReply = await callBackend("menu.trigger", { id: "settings:draw_settings", title: "Draw Setting" });
    assert.equal(settingsReply.ok, true, `menu.trigger settings: failed: ${settingsReply.error}`);
    await waitDialogs((list) => list.some((d) => d.kind === "settings" && d.name === "draw_settings"), "the Draw settings window");
    const settingsDialog = openDialogs().find((d) => d.kind === "settings");
    assert(Array.isArray(settingsDialog.layout?.controls) && settingsDialog.layout.controls.length > 0, "it carries its layout");
    await callBackend("dialog.result", { id: settingsDialog.id, action: "cancel" });
    await waitDialogs((list) => list.length === 0, "the settings window closes");

    // A colour control asks Python for the colour dialog; the answer reaches the pen.
    const colorRequest = await callBackend("dialog.request", { kind: "color", hook: "color", name: "palette", value: "#ff336699" });
    assert.equal(colorRequest.ok, true, `dialog.request color failed: ${colorRequest.error}`);
    await waitDialogs((list) => list.some((d) => d.kind === "color" && d.value === "#ff336699"), "the colour dialog on the control's colour");
    const colorDialog = openDialogs().find((d) => d.kind === "color");
    await callBackend("dialog.result", { id: colorDialog.id, action: "accept", value: "#ff123456" });
    await waitForState((map) => map.get("drawColor")?.rgba === "#ff123456", "the chosen colour reaches the pen");
    assert.equal(openDialogs().length, 0, "the colour dialog closed");

    // Settings is a Python menu at the end of the bar; a choice is broadcast for the page.
    const menusNow = stateMap.get("menus") ?? [];
    const settingsMenu = menusNow.at(-1);
    assert.equal(settingsMenu?.text, "Settings", "Settings is the last menu");
    assert.equal(menusNow.at(-2)?.text, "Windows", "right after Windows");
    assert.deepEqual(settingsMenu.children.map((c) => c.text), ["Board Renderer", "Language"]);
    const languageReply = await callBackend("menu.trigger", { id: "script:main:Settings:language.ja", checked: true });
    assert.equal(languageReply.ok, true, `Settings > Language failed: ${languageReply.error}`);
    await waitForState((map) => map.get("py/preferences")?.language === "ja", "the language choice is broadcast");
    // What no Python flow takes is reported, not silently answered.
    const unknownFile = await callBackend("menu.trigger", { id: "file.nothing" });
    assert.equal(unknownFile.ok, false, "a File item no flow runs is reported");
    assert.match(String(unknownFile.error), /Nothing handles the menu item file\.nothing/);
    const unknownKind = await callBackend("dialog.request", { kind: "nothing" });
    assert.equal(unknownKind.ok, false, "a request for a kind Python does not open is reported");
    assert.match(String(unknownKind.error), /Nothing opens a dialog for 'nothing'/);
    assert.equal(openDialogs().length, 0, "and nothing opened");

    // 10. Verify project.saveAs, project.open, file.texture, file.import with explicit paths
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "animean-smoke-"));
    try {
        const projPath = path.join(tmpDir, "test_proj.anproj");
        const projSaveReply = await callBackend("project.saveAs", { path: projPath });
        assert.equal(projSaveReply.ok, true, `project.saveAs failed: ${projSaveReply.error}`);
        assert(fs.existsSync(projPath), "project.saveAs did not write project file");

        // Opening from the Texture page brings up the Drawing page, as Qt did.
        await showPage("texture");
        await waitForState((map) => map.get("window")?.activeView === "child", "on the Texture page before project.open");
        const eventsBeforeOpen = receivedEvents.length;
        const projOpenReply = await callBackend("project.open", { path: projPath });
        assert.equal(projOpenReply.ok, true, `project.open failed: ${projOpenReply.error}`);
        await waitForState(
            (map) => paintPage(map) === "drawing" && map.get("window")?.activeView === "main",
            "project.open makes the main board active"
        );
        assert.equal(eventsSince(eventsBeforeOpen, "window.select", "drawing"), 1, "project.open brings up the Drawing page");
        await callBackend("texture.setHome", { home: "parked", subControlVisible: false });

        const texImgPath = path.join(tmpDir, "texture_export.png");
        const texExportReply = await callBackend("file.texture", {
            kind: "exportImage",
            path: texImgPath,
        });
        assert.equal(texExportReply.ok, true, `file.texture exportImage failed: ${texExportReply.error}`);
        assert(fs.existsSync(texImgPath), "file.texture exportImage did not write PNG file");

        const importRasterReply = await callBackend("file.import", {
            kind: "raster",
            paths: [texImgPath],
        });
        assert.equal(importRasterReply.ok, true, `file.import raster failed: ${importRasterReply.error}`);

        const texProjPath = path.join(tmpDir, "texture_view.textureview");
        const texSaveAsReply = await callBackend("file.texture", {
            kind: "saveAs",
            path: texProjPath,
        });
        assert.equal(texSaveAsReply.ok, true, `file.texture saveAs failed: ${texSaveAsReply.error}`);
        assert(fs.existsSync(texProjPath), "file.texture saveAs did not write file");

        const texOpenReply = await callBackend("file.texture", {
            kind: "open",
            path: texProjPath,
        });
        assert.equal(texOpenReply.ok, true, `file.texture open failed: ${texOpenReply.error}`);

        // 11. Verify probe on divergent extension overwrite (confirmDivergentOverwrite)
        const divergentProbe = await callBackend("project.saveAs", {
            path: path.join(tmpDir, "test_proj"),
            probe: true,
        });
        assert.equal(divergentProbe.ok, true);
        assert.equal(divergentProbe.result?.needsConfirmOverwrite, true);
        assert.equal(divergentProbe.result?.fileName, "test_proj.anproj");

        // 12. Verify menu.refresh, view.setActive -> paint window current, window.assetView, timeline.thumbnails, layer.applyStructure
        const refreshReply = await callBackend("menu.refresh", {});
        assert.equal(refreshReply.ok, true);
        assert(Array.isArray(refreshReply.result?.menus), "menu.refresh must return menus array");

        const setChildReply = await callBackend("view.setActive", { view: "child" });
        assert.equal(setChildReply.ok, true);
        await waitForState(
            (map) => {
                const wins = map.get("windows")?.list;
                const paint = Array.isArray(wins) ? wins.find((w) => w.name === "paint") : null;
                return paint?.current === "texture";
            },
            "windows.paint.current synced to texture on view.setActive(child)"
        );
        assert.equal(stateMap.get("window")?.assetView, "main");

        await callBackend("menu.trigger", { id: "child.setting.changeableTexture", checked: true });
        await waitForState(
            (map) => map.get("window")?.assetView === "child",
            "window.assetView updated to child when changeableTexture is enabled"
        );
        await callBackend("menu.trigger", { id: "child.setting.changeableTexture", checked: false });
        await callBackend("view.setActive", { view: "main" });
        await waitForState(
            (map) => {
                const wins = map.get("windows")?.list;
                const paint = Array.isArray(wins) ? wins.find((w) => w.name === "paint") : null;
                return paint?.current === "drawing" && map.get("window")?.assetView === "main";
            },
            "windows.paint.current synced back to drawing on view.setActive(main)"
        );

        const thumbReply = await callBackend("timeline.thumbnails", { frames: [0], width: 83, height: 62 });
        assert.equal(thumbReply.ok, true, `timeline.thumbnails failed: ${thumbReply.error}`);
        assert(typeof thumbReply.result?.thumbnails?.["0"] === "string", "timeline.thumbnails must return key for frame 0");
        assert.equal(thumbReply.result?.revision, stateMap.get("thumbnails")?.revision, "thumbnails state must carry the revision");

        const curRows = stateMap.get("layers/main")?.rows ?? [];
        if (curRows.length >= 2) {
            const reordered = [curRows[1], curRows[0], ...curRows.slice(2)];
            const structReply = await callBackend("layer.applyStructure", {
                view: "main",
                rows: reordered,
                movedLayer: curRows[0].layer ?? -1,
            });
            assert.equal(structReply.ok, true, `layer.applyStructure failed: ${structReply.error}`);
        }

        // 13. A drag must not strip a group's tag: Auto Mapping finds its units by it.
        const firstLayer = (stateMap.get("layers/main")?.rows ?? []).find((row) => !row.group)?.layer ?? 0;
        const madeReply = await callBackend("python.run", {
            command: `(lambda g: (main_model.set_layer_group_tag(g, 'probe'), print(g)))(main_model.create_layer_group('Probe', [${firstLayer}], [], True))`,
        });
        assert.equal(madeReply.ok, true, `python.run failed: ${madeReply.error}`);
        const groupId = Number.parseInt(madeReply.result?.output ?? "", 10);
        assert(groupId > 0, `create_layer_group must print the new group id, got: ${madeReply.result?.output}`);
        await callBackend("layer.add", { view: "main" });
        const tagged = await waitForState(
            (map) => (map.get("layers/main")?.rows ?? []).some((row) => row.group && row.group_id === groupId),
            "the tagged group shows in the layer rows"
        );
        assert(tagged !== false);
        const groupedRows = stateMap.get("layers/main").rows;
        const dragReply = await callBackend("layer.applyStructure", {
            view: "main",
            rows: groupedRows,
            movedLayer: groupedRows.find((row) => !row.group)?.layer ?? -1,
        });
        assert.equal(dragReply.ok, true, `layer.applyStructure failed: ${dragReply.error}`);
        const tagReply = await callBackend("python.run", { command: `print(main_model.layer_group_tag(${groupId}))` });
        assert.equal(tagReply.result?.output?.trim(), "probe", "the group's tag must survive layer.applyStructure");
    } finally {
        try {
            fs.rmSync(tmpDir, { recursive: true, force: true });
        } catch {}
    }

    // 11. Verify board.capabilities, field packets, F/f ops, call_later, and ack gating
    const capCheckBefore = await callBackend("python.run", {
        code: "import animean_python; print('CAP_BEFORE', animean_python.ui.field_display_supported('main'), animean_python.ui.field_display_supported('child'))",
    });
    assert.equal(capCheckBefore.ok, true, `python.run capCheckBefore failed: ${capCheckBefore.error}`);
    assert(
        String(capCheckBefore.result?.output ?? "").includes("CAP_BEFORE False False"),
        `Expected CAP_BEFORE False False, got: ${capCheckBefore.result?.output}`
    );

    const capSetReply = await callBackend("board.capabilities", { view: "main", fieldDisplay: true });
    assert.equal(capSetReply.ok, true, `board.capabilities failed: ${capSetReply.error}`);

    const capCheckAfter = await callBackend("python.run", {
        code: "import animean_python; print('CAP_AFTER', animean_python.ui.field_display_supported('main'), animean_python.ui.field_display_supported('child'))",
    });
    assert.equal(capCheckAfter.ok, true, `python.run capCheckAfter failed: ${capCheckAfter.error}`);
    assert(
        String(capCheckAfter.result?.output ?? "").includes("CAP_AFTER True False"),
        `Expected CAP_AFTER True False, got: ${capCheckAfter.result?.output}`
    );

    const badFieldReply = await callBackend("python.run", {
        code: [
            "import animean_python",
            "try:",
            "    animean_python.ui.set_field('bad_f', 2, 2, b'short')",
            "    print('BAD_FIELD_NO_ERR')",
            "except ValueError:",
            "    print('BAD_FIELD_VALUE_ERR')",
        ].join("\n"),
    });
    assert.equal(badFieldReply.ok, true);
    assert(
        String(badFieldReply.result?.output ?? "").includes("BAD_FIELD_VALUE_ERR"),
        `Expected ValueError on wrong field byte length, got: ${badFieldReply.result?.output}`
    );

    const prevBoardFrame = latestMainFrame ? latestMainFrame.frame : 0;
    messageSequence.length = 0;
    receivedFields.length = 0;

    const fieldSetupReply = await callBackend("python.run", {
        code: [
            "import struct, animean_python",
            "scenes = {s['sceneName']: s['scene'] for s in animean_python.get_scene()}",
            "child_sc = scenes['child_paint_view']",
            "s_keep = animean_python.vectorlogic.make_stroke_object([(5.0, 5.0), (45.0, 25.0)], (20, 120, 220, 255), 4.0, 1, False, False, 0)",
            "s_keep.property = 'keep_prop'",
            "child_sc.add_stroke_object(0, 0, s_keep)",
            "s_skip = animean_python.vectorlogic.make_stroke_object([(10.0, 10.0), (50.0, 50.0)], (220, 20, 20, 255), 4.0, 2, False, False, 0)",
            "s_skip.property = 'skip_prop'",
            "child_sc.add_stroke_object(0, 0, s_skip)",
            "data = struct.pack('<8f', 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0, 8.0)",
            "animean_python.ui.set_field('smoke_field', 2, 1, data)",
            "animean_python.ui.set_field_view('main', {",
            "    'field': 'smoke_field',",
            "    'rect': (0.0, 0.0, 200.0, 100.0),",
            "    'max_edge': 8.0,",
            "    'source_view': 'child',",
            "    'columns': [0],",
            "    'frame': 0,",
            "    'width_scale': 1.5,",
            "    'skip_properties': ['skip_prop'],",
            "    'passes': [",
            "        {'tag': (-0.5, 0.5), 'opacity': 0.75},",
            "        {'tag': (0.5, 1.5), 'opacity': 0.5, 'tint': (80, 120, 160, 200)},",
            "    ],",
            "    'hide_columns': [0],",
            "})",
        ].join("\n"),
    });
    assert.equal(fieldSetupReply.ok, true, `fieldSetupReply failed: ${fieldSetupReply.error}`);

    const boardWithF = await waitForBoard(
        (f) => f && f.frame > prevBoardFrame && fullOpsMain.some((op) => Array.isArray(op) && op[0] === "F"),
        "board frame with F op"
    );

    assert.equal(receivedFields.length, 1, "Should have received 1 field packet");
    assert.equal(receivedFields[0].header.key, "smoke_field");
    assert.equal(receivedFields[0].header.w, 2);
    assert.equal(receivedFields[0].header.h, 1);
    assert.equal(receivedFields[0].header.bytes, 32);
    assert.equal(receivedFields[0].byteLength, 32);
    assert.deepEqual(receivedFields[0].floats, [1, 2, 3, 4, 5, 6, 7, 8]);
    const ver1 = receivedFields[0].header.version;

    const hdrIdx = messageSequence.findIndex((m) => m.type === "field_hdr" && m.key === "smoke_field");
    const binIdx = messageSequence.findIndex((m) => m.type === "field_bin" && m.key === "smoke_field");
    const brdIdx = messageSequence.findIndex((m) => m.type === "board" && m.view === "main" && m.frame === boardWithF.frame);
    assert(hdrIdx >= 0 && binIdx === hdrIdx + 1 && brdIdx > binIdx, "field text+binary frames must arrive before board frame");

    const fStartIdx = fullOpsMain.findIndex((op) => Array.isArray(op) && op[0] === "F");
    const fEndIdx = fullOpsMain.findIndex((op) => Array.isArray(op) && op[0] === "f");
    assert(fStartIdx >= 0 && fEndIdx > fStartIdx, "Full ops must contain F and f in order");
    assert.equal(fullOpsMain[fStartIdx][1], "smoke_field");
    assert.equal(fullOpsMain[fStartIdx][2], ver1);
    assert.deepEqual(fullOpsMain[fStartIdx][8], [
        [-0.5, 0.5, 0.75, 0, 0, 0, 0],
        [0.5, 1.5, 0.5, 80, 120, 160, 200],
    ]);
    const sInsideFieldScope = fullOpsMain.slice(fStartIdx + 1, fEndIdx).filter((op) => Array.isArray(op) && op[0] === "S");
    assert.equal(sInsideFieldScope.length, 1, "Only non-skipped stroke should be drawn inside F..f");

    // Test ack gating and overwriting unsent field versions via call_later
    autoAckMain = false;
    unackedMainFrame = null;
    const frameBeforeAckTest = latestMainFrame.frame;

    const ackTestReply = await callBackend("python.run", {
        code: [
            "import struct, animean_python",
            "d2 = struct.pack('<8f', 10.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)",
            "d3 = struct.pack('<8f', 20.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)",
            "d4 = struct.pack('<8f', 30.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0)",
            "animean_python.ui.set_field('smoke_field', 2, 1, d2)",
            "def _later_updates():",
            "    animean_python.ui.set_field('smoke_field', 2, 1, d3)",
            "    animean_python.ui.set_field('smoke_field', 2, 1, d4)",
            "animean_python.ui.call_later(30, _later_updates)",
        ].join("\n"),
    });
    assert.equal(ackTestReply.ok, true, `ackTestReply failed: ${ackTestReply.error}`);

    await waitForBoard((f) => f && f.frame > frameBeforeAckTest, "unacked board frame with d2");
    assert(unackedMainFrame !== null, "Main board frame should be held unacked");
    assert.equal(receivedFields.length, 2, "Should have received d2 before the unacked board frame");
    assert.equal(receivedFields[1].floats[0], 10);

    // Wait 100ms for call_later(30, ...) to run while main is waiting for ack
    await new Promise((r) => setTimeout(r, 100));
    assert.equal(receivedFields.length, 2, "Must not send field packets while waiting for ack");

    const heldFrame = unackedMainFrame;
    autoAckMain = true;
    unackedMainFrame = null;
    ws.send(JSON.stringify({ t: "ack", view: "main", frame: heldFrame }));

    await waitForBoard((f) => f && f.frame > heldFrame, "board frame after acking held frame");
    assert.equal(receivedFields.length, 3, "Only latest overwritten field (d4) should be sent after ack");
    assert.equal(receivedFields[2].floats[0], 30, "Unsent d3 should have been overwritten by d4");
    assert.equal(receivedFields[2].header.version, ver1 + 3);

    // Clear field view
    const clearFieldViewFrame = latestMainFrame.frame;
    await callBackend("python.run", {
        code: "import animean_python; animean_python.ui.set_field_view('main', None)",
    });
    await waitForBoard((f) => f && f.frame > clearFieldViewFrame, "board frame after clearing field view");
    assert(!fullOpsMain.some((op) => Array.isArray(op) && (op[0] === "F" || op[0] === "f")), "F/f ops must be removed after set_field_view(None)");

    ws.close();
    clearTimeout(globalTimeout);
    console.log("SESSION_SMOKE_OK");
    cleanupAndExit(0);
} catch (err) {
    clearTimeout(globalTimeout);
    cleanupAndExit(1, err);
}
