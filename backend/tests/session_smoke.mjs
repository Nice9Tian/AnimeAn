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
            return;
        }
        const msg = JSON.parse(event.data);
        if (msg.t === "state") {
            if (msg.key === "event") {
                receivedEvents.push(msg.value);
            }
            stateMap.set(msg.key, msg.value);
            notifyStateWaiters();
        } else if (msg.t === "board") {
            ws.send(JSON.stringify({ t: "ack", view: msg.view, frame: msg.frame }));
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

    // 2. Check menus are strictly ["File", "Edit", "Auto Mapping", "Draw", "Theme", "Windows"] in exact order
    const menus = stateMap.get("menus");
    assert(Array.isArray(menus), "menus state should be an array");
    const menuTitles = menus.map((m) => String(m.text ?? "").replace(/&/g, ""));
    const expectedMenus = ["File", "Edit", "Auto Mapping", "Draw", "Theme", "Windows"];
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

    // 9. Two consecutive menu.trigger calls (file.open, file.saveAs) -> receive two event messages
    const eventsBeforeCount = receivedEvents.length;
    const openPromise = callBackend("menu.trigger", { id: "file.open" });
    const saveAsPromise = callBackend("menu.trigger", { id: "file.saveAs" });
    const [openReply, saveAsReply] = await Promise.all([openPromise, saveAsPromise]);
    assert.equal(openReply.ok, true, `menu.trigger file.open failed: ${openReply.error}`);
    assert.equal(saveAsReply.ok, true, `menu.trigger file.saveAs failed: ${saveAsReply.error}`);

    await waitForState(
        () => receivedEvents.length >= eventsBeforeCount + 2,
        "two event messages from consecutive menu.trigger calls"
    );
    const twoEvents = receivedEvents.slice(eventsBeforeCount, eventsBeforeCount + 2);
    assert.equal(twoEvents.length, 2, "Expected two distinct event state messages");
    assert.equal(twoEvents[0].name, "dialog.openProject");
    assert.equal(twoEvents[1].name, "dialog.saveProjectAs");

    // 10. Verify project.saveAs, project.open, file.texture, file.import with explicit paths
    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "animean-smoke-"));
    try {
        const projPath = path.join(tmpDir, "test_proj.anproj");
        const projSaveReply = await callBackend("project.saveAs", { path: projPath });
        assert.equal(projSaveReply.ok, true, `project.saveAs failed: ${projSaveReply.error}`);
        assert(fs.existsSync(projPath), "project.saveAs did not write project file");

        const projOpenReply = await callBackend("project.open", { path: projPath });
        assert.equal(projOpenReply.ok, true, `project.open failed: ${projOpenReply.error}`);

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
    } finally {
        try {
            fs.rmSync(tmpDir, { recursive: true, force: true });
        } catch {}
    }

    ws.close();
    clearTimeout(globalTimeout);
    console.log("SESSION_SMOKE_OK");
    cleanupAndExit(0);
} catch (err) {
    clearTimeout(globalTimeout);
    cleanupAndExit(1, err);
}
