// Several pages on one backend: they all stay connected, they all show the
// same picture, and what one of them does reaches the others.
//
//   node backend/tests/multi_page.mjs <path-to-AnimeAnBackend.exe>
//
// Each Page below keeps what a real page keeps: the states, the op list of
// each board rebuilt from `keep` + `ops`, and the images it was sent.
import { spawn } from "node:child_process";
import assert from "node:assert/strict";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

const exePath = process.argv[2];
if (!exePath) {
    console.error("Usage: node backend/tests/multi_page.mjs <path-to-AnimeAnBackend.exe>");
    process.exit(1);
}

const token = `pages_${Date.now()}_${Math.random().toString(36).slice(2)}`;
// The backend keeps window settings on disk; a run must not touch the user's.
const ownSettingsDir = process.env.ANIMEAN_SETTINGS_DIR
    ? null
    : fs.mkdtempSync(path.join(os.tmpdir(), "animean-multi-page-"));

const extraPaths = [
    path.win32.normalize("C:/Qt/Tools/mingw1310_64/bin"),
    path.win32.normalize("C:/Users/admin/Documents/AnimeAn/tools/python312"),
    path.dirname(path.resolve(exePath)),
];
const child = spawn(exePath, ["--port", "0", "--token", token], {
    stdio: ["ignore", "pipe", "pipe"],
    env: {
        ...process.env,
        PATH: [...extraPaths, process.env.PATH || process.env.Path || ""].join(";"),
        ...(ownSettingsDir ? { ANIMEAN_SETTINGS_DIR: ownSettingsDir } : {}),
    },
});
let stdoutBuf = "";
let stderrBuf = "";
child.stderr.on("data", (chunk) => {
    stderrBuf += chunk.toString("utf8");
});

function finish(code, err) {
    if (err) {
        console.error(err);
        if (stderrBuf) console.error("Backend stderr:\n" + stderrBuf);
    }
    try {
        child.kill();
    } catch {}
    if (ownSettingsDir) {
        try {
            fs.rmSync(ownSettingsDir, { recursive: true, force: true });
        } catch {}
    }
    process.exit(code);
}

const globalTimeout = setTimeout(() => finish(1, new Error("Timed out running multi_page.mjs")), 120000);

function waitForPort() {
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
        child.once("exit", (code) => reject(new Error(`Backend exited early with code ${code}. stderr: ${stderrBuf}`)));
    });
}

// When the last message from the backend arrived at any page.
let lastTraffic = Date.now();
const openPages = new Set();

class Page {
    constructor(name, port) {
        this.name = name;
        this.url = `ws://127.0.0.1:${port}/?token=${encodeURIComponent(token)}`;
        this.states = new Map();
        this.ops = { main: [], child: [] };
        this.frames = { main: null, child: null };
        this.frameCount = { main: 0, child: 0 };
        this.replies = [];
        this.images = [];
        this.forgets = [];
        this.pendingCalls = new Map();
        this.nextCallId = 1;
        this.autoAck = true;
        this.closed = false;
    }

    open() {
        this.ws = new WebSocket(this.url);
        this.ws.binaryType = "arraybuffer";
        this.ws.addEventListener("close", () => {
            this.closed = true;
            openPages.delete(this);
        });
        this.ws.addEventListener("message", (event) => this.onMessage(event));
        return new Promise((resolve, reject) => {
            this.ws.addEventListener("open", () => {
                openPages.add(this);
                resolve(this);
            }, { once: true });
            this.ws.addEventListener("error", () => reject(new Error(`${this.name}: WebSocket error`)), { once: true });
        });
    }

    onMessage(event) {
        lastTraffic = Date.now();
        if (typeof event.data !== "string") return;
        const msg = JSON.parse(event.data);
        if (msg.t === "state") {
            this.states.set(msg.key, msg.value);
        } else if (msg.t === "board") {
            const list = this.ops[msg.view];
            list.length = Math.min(list.length, msg.keep);
            list.push(...msg.ops);
            this.frames[msg.view] = { frame: msg.frame, w: msg.w, h: msg.h, keep: msg.keep };
            this.frameCount[msg.view] += 1;
            if (this.autoAck) this.ws.send(JSON.stringify({ t: "ack", view: msg.view, frame: msg.frame }));
        } else if (msg.t === "reply") {
            this.replies.push(msg);
            const pending = this.pendingCalls.get(msg.id);
            if (pending) {
                this.pendingCalls.delete(msg.id);
                pending(msg);
            }
        } else if (msg.t === "image") {
            this.images.push(msg);
        } else if (msg.t === "image.forget") {
            this.forgets.push(msg);
        }
    }

    call(method, params = {}) {
        const id = this.nextCallId++;
        this.ws.send(JSON.stringify({ t: "call", id, method, params }));
        return new Promise((resolve, reject) => {
            const timer = setTimeout(() => {
                this.pendingCalls.delete(id);
                reject(new Error(`${this.name}: no reply to ${method} (id=${id})`));
            }, 10000);
            this.pendingCalls.set(id, (msg) => {
                clearTimeout(timer);
                resolve(msg);
            });
        });
    }

    async python(command) {
        const reply = await this.call("python.run", { command });
        assert.equal(reply.ok, true, `${this.name}: python.run failed: ${reply.error}`);
        return String(reply.result?.output ?? "");
    }

    // The backend takes one page's messages in the order they were sent, but
    // nothing orders two pages' messages against each other. Once this comes
    // back, everything this page sent before it has been handled.
    async sync() {
        if (!this.closed) await this.call("test.sync");
    }

    input(view, kind, fields = {}) {
        this.ws.send(JSON.stringify({ t: "input", view, kind, ...fields }));
    }

    resize(view, w, h) {
        this.input(view, "resize", { w, h, dpr: 1 });
    }

    close() {
        return new Promise((resolve) => {
            if (this.closed) return resolve();
            this.ws.addEventListener("close", () => resolve(), { once: true });
            this.ws.close();
        });
    }
}

// Until everything sent so far has been handled and no page has heard from the
// backend for a while (what a message sets off - a state flush, a frame - goes
// out on the turns of the event loop after it).
async function quiet(ms = 200, timeoutMs = 15000) {
    const deadline = Date.now() + timeoutMs;
    await Promise.all([...openPages].map((page) => page.sync()));
    lastTraffic = Date.now();
    while (Date.now() - lastTraffic < ms) {
        if (Date.now() > deadline) throw new Error("The backend never went quiet");
        await new Promise((resolve) => setTimeout(resolve, 25));
    }
}

async function until(predicate, desc, timeoutMs = 10000) {
    const deadline = Date.now() + timeoutMs;
    while (!predicate()) {
        if (Date.now() > deadline) throw new Error(`Timed out waiting for: ${desc}`);
        await new Promise((resolve) => setTimeout(resolve, 20));
    }
}

let clock = 1000;
// One pen stroke: press at the first point, move through the rest, release.
function stroke(page, points, { release = true, view = "main" } = {}) {
    const [first, ...rest] = points;
    page.input(view, "press", { x: first[0], y: first[1], button: 1, buttons: 1, modifiers: 0, time: (clock += 8) });
    for (const [x, y] of rest) {
        page.input(view, "move", { x, y, button: 0, buttons: 1, modifiers: 0, time: (clock += 8) });
    }
    if (release) {
        const last = points[points.length - 1];
        page.input(view, "release", { x: last[0], y: last[1], button: 1, buttons: 0, modifiers: 0, time: (clock += 8) });
    }
}

const STROKES =
    "(lambda s: print(__import__('json').dumps([[round(b['x'], 2), round(b['y'], 2), round(b['width'], 2), round(b['height'], 2)] for b in (x['bounds'] for x in s)])))" +
    "(main_model.cell_to_dict(main_model.current_layer(), main_model.current_frame(), False)['image']['strokes'])";
// The bounds of every stroke on the current cell of the main board.
async function strokes(page) {
    return JSON.parse(await page.python(STROKES));
}

const samePicture = (a, b, view = "main") => JSON.stringify(a.ops[view]) === JSON.stringify(b.ops[view]);
// A page is sent one answer for each call it made, and none of another page's.
function onlyOwnAnswers(page) {
    const ids = page.replies.map((reply) => reply.id);
    assert.equal(new Set(ids).size, ids.length, `${page.name} was sent two answers with the same id`);
    assert.equal(ids.length, page.nextCallId - 1 - page.pendingCalls.size, `${page.name} was sent an answer it did not ask for`);
}
const pass = (text) => console.log(`[PASS] ${text}`);

const REQUIRED_STATES = [
    "window", "theme", "menus", "tools", "toolOptions", "drawColor", "drawSettings", "layers/main",
    "layers/child", "assets/main", "assets/child", "timeline", "history", "windows", "cursor/main",
    "cursor/child", "viewport/main", "viewport/child", "freeze", "pad", "dialogs", "textureSettings",
];

try {
    const port = await waitForPort();

    // 1. A second page joins and the first one stays.
    const a = await new Page("A", port).open();
    a.resize("main", 800, 600);
    a.resize("child", 400, 400);
    await until(() => REQUIRED_STATES.every((key) => a.states.has(key)), "A's states");
    await until(() => a.ops.main.length > 0, "A's first main frame");
    await quiet();
    assert.deepEqual([a.frames.main.w, a.frames.main.h], [800, 600], "The only page sets the board's size");

    const b = await new Page("B", port).open();
    b.resize("main", 500, 400);
    b.resize("child", 300, 300);
    await until(() => REQUIRED_STATES.every((key) => b.states.has(key)), "B's states");
    await until(() => b.ops.main.length > 0, "B's first main frame");
    await quiet();
    assert.equal(a.closed, false, "The first page must stay connected when a second one arrives");
    assert.equal(b.frames.main.keep, 0, "A page that has just arrived starts from a whole frame");
    assert(samePicture(a, b), "Both pages must hold the same main op list after the second one joins");
    assert(samePicture(a, b, "child"), "Both pages must hold the same texture op list after the second one joins");
    for (const key of REQUIRED_STATES) {
        assert.deepEqual(b.states.get(key), a.states.get(key), `State ${key} must be the same on both pages`);
    }
    pass("1. A second page joins, the first stays, and both hold the same states and picture");

    // 2. A board has one size: the presenting page's. The other page's waits.
    assert.deepEqual([a.frames.main.w, a.frames.main.h], [800, 600], "A presents: its size stays in force");
    assert.deepEqual([b.frames.main.w, b.frames.main.h], [800, 600], "B is shown A's frame, at A's size");
    pass("2. The page that was first keeps the board at its own size");

    // 3. An answer goes to the page that asked and to no other.
    {
        const rowsBefore = a.states.get("layers/main").rows.length;
        const reply = await a.call("layer.add", { view: "main" });
        assert.equal(reply.ok, true, `layer.add failed: ${reply.error}`);
        await until(() => b.states.get("layers/main").rows.length === rowsBefore + 1, "B's layers/main to follow A's call");
        await quiet();
        onlyOwnAnswers(a);
        onlyOwnAnswers(b);
        assert.equal(a.states.get("layers/main").rows.length, rowsBefore + 1);
        // B's own first call carries the same id A's did; each gets its own answer.
        const [fromA, fromB] = await Promise.all([
            a.python("print('answer for A')"),
            b.python("print('answer for B')"),
        ]);
        assert.equal(fromA.trim(), "answer for A");
        assert.equal(fromB.trim(), "answer for B");
        onlyOwnAnswers(a);
        onlyOwnAnswers(b);
        pass("3. A call is answered to the page that made it; the change it made reaches both");
    }

    // 4. One page draws, the other sees it.
    const before = await strokes(a);
    {
        const opsBefore = a.ops.main.length;
        stroke(a, [[300, 250], [320, 262], [350, 280]]);
        await quiet();
        const now = await strokes(a);
        assert.equal(now.length, before.length + 1, "A's stroke must be on the board");
        assert(a.ops.main.length > opsBefore, "A must be shown its own stroke");
        assert(samePicture(a, b), "B must be shown exactly what A is shown");
        assert.equal(a.frames.main.frame, b.frames.main.frame, "Both pages must be at the same frame");
        pass("4. A stroke drawn on one page is on the other page too");
    }
    const reference = (await strokes(a)).at(-1);

    // 5. Until its gesture is over, the other page does not take the board's size.
    {
        b.input("main", "press", { x: 100, y: 100, button: 1, buttons: 1, modifiers: 0, time: (clock += 8) });
        b.input("main", "move", { x: 140, y: 130, button: 0, buttons: 1, modifiers: 0, time: (clock += 8) });
        await quiet();
        // No python.run while the pen is down: it ends the stroke being drawn,
        // with one page as with several.
        assert.deepEqual([b.frames.main.w, b.frames.main.h], [800, 600], "The board must not change size under a pen that is down");
        b.input("main", "release", { x: 140, y: 130, button: 1, buttons: 0, modifiers: 0, time: (clock += 8) });
        await quiet();
        assert.equal((await strokes(b)).length, before.length + 2, "B's stroke must be on the board");
        assert.deepEqual([b.frames.main.w, b.frames.main.h], [500, 400], "After its gesture B presents: the board takes B's size");
        assert.deepEqual([a.frames.main.w, a.frames.main.h], [500, 400], "A is now shown B's frame, at B's size");
        assert(samePicture(a, b), "Both pages must hold the same picture after B drew");
        pass("5. The page that last finished a gesture presents, and only once the gesture is over");

        a.input("main", "wheel", { x: 400, y: 300, dx: 0, dy: 120, modifiers: 0 });
        await quiet();
        assert.deepEqual([a.frames.main.w, a.frames.main.h], [800, 600], "A wheel turn on A makes A present again");
        a.input("main", "wheel", { x: 400, y: 300, dx: 0, dy: -120, modifiers: 0 });
        await quiet();
        assert(samePicture(a, b), "Both pages must hold the same picture after the zoom");
        pass("5a. A wheel turn counts as a gesture");
    }

    // 6. While one page holds a button down, the other pages' pointers are ignored.
    {
        const count = (await strokes(a)).length;
        stroke(a, [[300, 250], [320, 262]], { release: false });
        await a.sync();
        // B's whole drag, far away, while A is still down.
        stroke(b, [[60, 60], [700, 500], [60, 500]]);
        b.input("main", "move", { x: 650, y: 80, button: 0, buttons: 0, modifiers: 0, time: (clock += 8) });
        await b.sync();
        a.input("main", "move", { x: 350, y: 280, button: 0, buttons: 1, modifiers: 0, time: (clock += 8) });
        a.input("main", "release", { x: 350, y: 280, button: 1, buttons: 0, modifiers: 0, time: (clock += 8) });
        await quiet();
        const now = await strokes(a);
        assert.equal(now.length, count + 1, "Only A's stroke may land while A holds the pointer");
        assert.deepEqual(now.at(-1), reference, "A's stroke must come out as if B's pointer had not been there");
        pass("6. A second pointer cannot get into a stroke another page is drawing");

        // A drag whose press was ignored stays ignored after the other page lets go.
        stroke(a, [[300, 250], [320, 262]], { release: false });
        await a.sync();
        b.input("main", "press", { x: 60, y: 60, button: 1, buttons: 1, modifiers: 0, time: (clock += 8) });
        await b.sync();
        a.input("main", "release", { x: 320, y: 262, button: 1, buttons: 0, modifiers: 0, time: (clock += 8) });
        await a.sync();
        b.input("main", "move", { x: 700, y: 500, button: 0, buttons: 1, modifiers: 0, time: (clock += 8) });
        b.input("main", "release", { x: 700, y: 500, button: 1, buttons: 0, modifiers: 0, time: (clock += 8) });
        await quiet();
        assert.equal((await strokes(a)).length, count + 2, "The rest of a drag whose press was ignored must be ignored too");
        // And its next drag is its own again.
        stroke(b, [[100, 300], [130, 320], [160, 300]]);
        await quiet();
        assert.equal((await strokes(a)).length, count + 3, "B's next stroke must land");
        assert(samePicture(a, b), "Both pages must hold the same picture after the mixed input");
        pass("6a. The rest of an ignored drag is ignored, and the next one counts again");
    }

    // 7. Frames keep coming when one page stops acknowledging them.
    {
        b.autoAck = false;
        const seen = b.frameCount.main;
        for (let i = 0; i < 5; i++) {
            const reply = await a.call("view.setActive", { view: i % 2 ? "main" : "child" });
            assert.equal(reply.ok, true);
            await quiet(120);
        }
        assert(b.frameCount.main > seen + 2, "A page that does not acknowledge must not hold the others' frames back");
        b.autoAck = true;
        await a.call("view.setActive", { view: "main" });
        await quiet();
        assert(samePicture(a, b), "Both pages must hold the same picture");
        pass("7. One page that stops acknowledging frames does not stall the rest");
    }

    // 8. Where the texture board lives: the presenting page's answer is in force.
    {
        stroke(a, [[500, 100], [520, 110]]); // A presents
        await quiet();
        const fromA = await a.call("texture.setHome", { home: "central" });
        assert.equal(fromA.result.home, "central");
        const fromB = await b.call("texture.setHome", { home: "control" });
        assert.equal(fromB.result.home, "central", "A page that does not present cannot move the texture board");
        stroke(b, [[60, 300], [80, 310]]); // B presents, and its home comes with it
        await quiet();
        const again = await a.call("texture.setHome", { home: "central" });
        assert.equal(again.result.home, "control", "The texture board's home follows the presenting page");
        pass("8. The texture board's home follows the presenting page");
    }

    // 9. Field display is on only when every page that reported can draw it.
    {
        const supported = async () =>
            (await a.python("import animean_python; print(animean_python.ui.field_display_supported('main'))")).trim();
        await a.call("board.capabilities", { view: "main", fieldDisplay: true });
        assert.equal(await supported(), "True", "One page that can: supported");
        await b.call("board.capabilities", { view: "main", fieldDisplay: false });
        assert.equal(await supported(), "False", "One page that cannot: not supported");
        await b.call("board.capabilities", { view: "main", fieldDisplay: true });
        assert.equal(await supported(), "True", "Both can: supported");
        pass("9. Field display needs every page that reported");
    }

    // 10. Thumbnails: new ones go to every page, and a page that joins later is
    // sent the ones made before it came - and only that page is.
    {
        const imagesA = a.images.length;
        const imagesB = b.images.length;
        const reply = await a.call("timeline.thumbnails", { frames: [0], width: 40, height: 30, view: "main" });
        assert.equal(reply.ok, true, `timeline.thumbnails failed: ${reply.error}`);
        await quiet();
        const key = reply.result.frames[0].key;
        assert(a.images.slice(imagesA).some((m) => m.key === key), "The page that asked gets the thumbnail");
        assert(b.images.slice(imagesB).some((m) => m.key === key), "The other page gets the thumbnail too");

        const sentA = a.images.filter((m) => m.key === key).length;
        const c = await new Page("C", port).open();
        await until(() => REQUIRED_STATES.every((k) => c.states.has(k)), "C's states");
        await until(() => c.ops.main.length > 0, "C's first main frame");
        await quiet();
        assert(c.images.some((m) => m.key === key), "A page that joins later is sent the thumbnails already made");
        assert.equal(a.images.filter((m) => m.key === key).length, sentA, "The pages already there are not sent them again");
        assert(samePicture(a, c) && samePicture(b, c), "All three pages must hold the same picture");
        assert.equal(a.closed || b.closed, false, "A third page must not cost the first two their connection");
        await c.close();
        await quiet();
        pass("10. Thumbnails reach every page, and a late page gets the earlier ones to itself");
    }

    onlyOwnAnswers(a);
    onlyOwnAnswers(b);

    // 11. A page that goes away with its pen down: the stroke is finished for
    // it, and the pages that remain can draw.
    {
        stroke(a, [[500, 300], [520, 310]]); // A presents again
        await quiet();
        const count = (await strokes(b)).length;
        stroke(a, [[300, 250], [320, 262], [350, 280]], { release: false });
        await quiet();
        await a.close();
        await quiet();
        assert.equal((await strokes(b)).length, count + 1, "The stroke of a page that left must be finished, once");
        stroke(b, [[100, 100], [140, 130]]);
        await quiet();
        assert.equal((await strokes(b)).length, count + 2, "The page that remains must be able to draw");
        assert.deepEqual([b.frames.main.w, b.frames.main.h], [500, 400], "With A gone, the board takes B's size");
        assert.equal(
            (await b.python("import animean_python; print(animean_python.ui.field_display_supported('main'))")).trim(),
            "True",
            "A page that left no longer counts for field display",
        );
        pass("11. A page that leaves mid-stroke has its stroke finished; the rest carry on at their own size");
    }

    // 12. A page comes back alone: as with a single page before.
    {
        await b.close();
        await new Promise((resolve) => setTimeout(resolve, 200));
        const d = await new Page("D", port).open();
        d.resize("main", 640, 480);
        await until(() => REQUIRED_STATES.every((key) => d.states.has(key)), "D's states");
        await until(() => d.frames.main && d.frames.main.w === 640, "the board to take the only page's size");
        await quiet();
        assert.equal(d.forgets.length, 0, "A page that is alone is not told to forget images it never had");
        stroke(d, [[200, 200], [240, 230]]);
        await quiet();
        assert(d.ops.main.length > 0);
        await d.close();
        pass("12. After every page has left, a new one starts as the only page");
    }

    console.log("MULTI_PAGE_OK");
    clearTimeout(globalTimeout);
    finish(0);
} catch (err) {
    clearTimeout(globalTimeout);
    finish(1, err);
}
