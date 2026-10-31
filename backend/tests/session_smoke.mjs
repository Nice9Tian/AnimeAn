import { spawn } from "node:child_process";
import assert from "node:assert/strict";
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
        "freeze",
        "pad",
    ];

    await waitForState(
        (map) => requiredStateKeys.every((k) => map.has(k)),
        `all initial state keys (${requiredStateKeys.join(", ")})`
    );

    // 2. Check menus contain File, Edit, View, Auto Mapping, Draw, Theme, Windows
    const menus = stateMap.get("menus");
    assert(Array.isArray(menus), "menus state should be an array");
    const menuTitles = menus.map((m) => String(m.text ?? "").replace(/&/g, ""));
    const expectedMenus = ["File", "Edit", "View", "Auto Mapping", "Draw", "Theme", "Windows"];
    for (const expected of expectedMenus) {
        assert(
            menuTitles.includes(expected),
            `Expected menus to include "${expected}", got [${menuTitles.join(", ")}]`
        );
    }

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

    ws.close();
    clearTimeout(globalTimeout);
    console.log("SESSION_SMOKE_OK");
    cleanupAndExit(0);
} catch (err) {
    clearTimeout(globalTimeout);
    cleanupAndExit(1, err);
}
