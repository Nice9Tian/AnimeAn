import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

import {
  buildBackendEnv,
  findRepoRoot,
  locateBackendExecutable,
} from "../src/discovery.mjs";
import { stopDevEnvironment } from "../src/launcher.mjs";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const TOOL_ROOT = path.resolve(__dirname, "..");
const REPO_ROOT = findRepoRoot(TOOL_ROOT);
const SERVER_SCRIPT = path.join(TOOL_ROOT, "mcp_server.mjs");

class McpStdioClient {
  constructor({ framing = "ndjson", args = [] } = {}) {
    this.framing = framing;
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = Buffer.alloc(0);
    this.stderrChunks = [];

    this.proc = spawn(process.execPath, [SERVER_SCRIPT, ...args], {
      cwd: REPO_ROOT,
      stdio: ["pipe", "pipe", "pipe"],
    });

    this.proc.stderr.on("data", (chunk) => {
      this.stderrChunks.push(chunk.toString("utf-8"));
    });

    this.proc.stdout.on("data", (chunk) => {
      this.buffer = Buffer.concat([this.buffer, chunk]);
      this._drainStdout();
    });

    this.proc.once("exit", (code, signal) => {
      for (const [id, pending] of this.pending.entries()) {
        clearTimeout(pending.timer);
        pending.reject(
          new Error(
            `mcp_server exited early (code=${code}, signal=${signal}, pendingId=${id})\nstderr:\n${this.stderrChunks.join("")}`
          )
        );
      }
      this.pending.clear();
    });
  }

  _drainStdout() {
    while (this.buffer.length > 0) {
      if (this.framing === "ndjson") {
        const nl = this.buffer.indexOf(0x0a);
        if (nl === -1) return;
        const line = this.buffer.subarray(0, nl).toString("utf-8").trim();
        this.buffer = this.buffer.subarray(nl + 1);
        if (!line) continue;
        this._dispatchReply(JSON.parse(line));
      } else {
        let sep = this.buffer.indexOf("\r\n\r\n");
        let sepLen = 4;
        if (sep === -1) {
          sep = this.buffer.indexOf("\n\n");
          sepLen = 2;
        }
        if (sep === -1) return;
        const hdr = this.buffer.subarray(0, sep).toString("utf-8");
        const m = hdr.match(/Content-Length:\s*(\d+)/i);
        if (!m) {
          this.buffer = this.buffer.subarray(sep + sepLen);
          continue;
        }
        const len = Number(m[1]);
        const bodyStart = sep + sepLen;
        if (this.buffer.length < bodyStart + len) return;
        const jsonStr = this.buffer.subarray(bodyStart, bodyStart + len).toString("utf-8");
        this.buffer = this.buffer.subarray(bodyStart + len);
        this._dispatchReply(JSON.parse(jsonStr));
      }
    }
  }

  _dispatchReply(msg) {
    const entry = this.pending.get(msg.id);
    if (!entry) return;
    this.pending.delete(msg.id);
    clearTimeout(entry.timer);
    if (msg.error) {
      entry.reject(new Error(`MCP error ${msg.error.code}: ${msg.error.message}`));
    } else {
      entry.resolve(msg.result);
    }
  }

  _writeFrame(obj) {
    const json = JSON.stringify(obj);
    if (this.framing === "ndjson") {
      this.proc.stdin.write(`${json}\n`, "utf-8");
    } else {
      const byteLen = Buffer.byteLength(json, "utf-8");
      this.proc.stdin.write(`Content-Length: ${byteLen}\r\n\r\n${json}`, "utf-8");
    }
  }

  notify(method, params = {}) {
    this._writeFrame({ jsonrpc: "2.0", method, params });
  }

  request(method, params = {}, timeoutMs = 20000) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(
          new Error(
            `Timed out waiting for MCP '${method}' (id=${id})\nstderr:\n${this.stderrChunks.join("")}`
          )
        );
      }, timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this._writeFrame({ jsonrpc: "2.0", id, method, params });
    });
  }

  async callTool(name, args = {}) {
    const res = await this.request("tools/call", { name, arguments: args });
    assert.equal(
      res.isError,
      false,
      `MCP tool '${name}' failed: ${res?.content?.[0]?.text ?? JSON.stringify(res)}`
    );
    const json = JSON.parse(res.content[0].text);
    return { raw: res, json };
  }

  async close() {
    if (this.proc.exitCode === null) {
      this.proc.stdin.end();
      await new Promise((resolve) => {
        const timer = setTimeout(() => {
          try {
            this.proc.kill();
          } catch {}
          resolve();
        }, 2500);
        this.proc.once("exit", () => {
          clearTimeout(timer);
          resolve();
        });
      });
    }
  }
}

async function spawnRealBackend(exePath, token) {
  const env = buildBackendEnv(exePath, REPO_ROOT);
  const child = spawn(exePath, ["--port", "0", "--token", token], {
    cwd: path.dirname(exePath),
    env,
    stdio: ["ignore", "pipe", "pipe"],
  });

  let stdoutBuf = "";
  let stderrBuf = "";
  const port = await new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      reject(new Error(`Timed out starting AnimeAnBackend.exe\nstdout:\n${stdoutBuf}\nstderr:\n${stderrBuf}`));
    }, 15000);

    child.stdout.on("data", (chunk) => {
      stdoutBuf += chunk.toString("utf-8");
      const m = stdoutBuf.match(/ANIMEAN_BACKEND_PORT=(\d+)/);
      if (m) {
        clearTimeout(timer);
        resolve(Number(m[1]));
      }
    });
    child.stderr.on("data", (chunk) => {
      stderrBuf += chunk.toString("utf-8");
    });
    child.once("exit", (code) => {
      clearTimeout(timer);
      reject(new Error(`Backend exited early (${code})\nstdout:\n${stdoutBuf}\nstderr:\n${stderrBuf}`));
    });
  });

  return { child, port, token };
}

class SimulatedChromePage {
  constructor(wsUrl) {
    this.wsUrl = wsUrl;
    this.ws = null;
    this.nextId = 1;
    this.pending = new Map();
    this.boardWaiters = [];
    this.fullOpsMain = [];
    this.latestMainBoard = null;
  }

  async connect() {
    this.ws = new WebSocket(this.wsUrl);
    this.ws.binaryType = "arraybuffer";

    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error("Chrome WebSocket connect timeout")), 8000);
      this.ws.addEventListener(
        "open",
        () => {
          clearTimeout(timer);
          resolve();
        },
        { once: true }
      );
      this.ws.addEventListener(
        "error",
        (err) => {
          clearTimeout(timer);
          reject(new Error(`Chrome WebSocket error: ${err?.message ?? err}`));
        },
        { once: true }
      );
    });

    this.ws.addEventListener("message", (event) => {
      if (typeof event.data !== "string") return;
      const msg = JSON.parse(event.data);
      if (msg.t === "board") {
        this.ws.send(JSON.stringify({ t: "ack", view: msg.view, frame: msg.frame }));
        if (msg.view === "main") {
          this.fullOpsMain.length = Number(msg.keep || 0);
          if (Array.isArray(msg.ops)) {
            this.fullOpsMain.push(...msg.ops);
          }
          this.latestMainBoard = {
            frame: msg.frame,
            w: msg.w,
            h: msg.h,
            totalOps: this.fullOpsMain.length,
          };
          for (let i = this.boardWaiters.length - 1; i >= 0; i -= 1) {
            const w = this.boardWaiters[i];
            if (w.predicate(this.latestMainBoard)) {
              this.boardWaiters.splice(i, 1);
              clearTimeout(w.timer);
              w.resolve(this.latestMainBoard);
            }
          }
        }
      } else if (msg.t === "reply") {
        const p = this.pending.get(msg.id);
        if (p) {
          this.pending.delete(msg.id);
          clearTimeout(p.timer);
          if (msg.ok) p.resolve(msg.result);
          else p.reject(new Error(msg.error || "Backend call failed"));
        }
      }
    });
  }

  waitForMainBoard(predicate, desc, timeoutMs = 8000) {
    if (this.latestMainBoard && predicate(this.latestMainBoard)) {
      return Promise.resolve(this.latestMainBoard);
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = this.boardWaiters.indexOf(entry);
        if (idx >= 0) this.boardWaiters.splice(idx, 1);
        reject(new Error(`Timed out waiting for Chrome main board (${desc})`));
      }, timeoutMs);
      const entry = { predicate, resolve, reject, timer };
      this.boardWaiters.push(entry);
    });
  }

  call(method, params = {}) {
    const id = this.nextId++;
    this.ws.send(JSON.stringify({ t: "call", id, method, params }));
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`Chrome RPC timeout: ${method}`));
      }, 8000);
      this.pending.set(id, { resolve, reject, timer });
    });
  }

  sendResize(view, w, h, dpr = 1) {
    this.ws.send(JSON.stringify({ t: "input", view, kind: "resize", w, h, dpr }));
  }

  drawStroke(view, yBase) {
    const events = [
      { kind: "press", x: 180, y: yBase, button: 1, buttons: 1 },
      { kind: "move", x: 260, y: yBase + 35, button: 0, buttons: 1 },
      { kind: "move", x: 350, y: yBase + 85, button: 0, buttons: 1 },
      { kind: "release", x: 350, y: yBase + 85, button: 1, buttons: 0 },
    ];
    for (const ev of events) {
      this.ws.send(
        JSON.stringify({
          t: "input",
          view,
          kind: ev.kind,
          x: ev.x,
          y: ev.y,
          button: ev.button,
          buttons: ev.buttons,
          modifiers: 0,
        })
      );
    }
  }

  close() {
    if (this.ws) {
      try {
        this.ws.close();
      } catch {}
    }
  }
}

test(
  "E2E integration: real AnimeAnBackend + simulated Chrome + all 14 MCP tools (NDJSON + Content-Length)",
  { timeout: 60000 },
  async () => {
    const exePath = locateBackendExecutable({ repoRoot: REPO_ROOT });
    assert(exePath, "AnimeAnBackend.exe must be present for integration test");

    const tmpDir = fs.mkdtempSync(path.join(os.tmpdir(), "animean-mcp-e2e-"));
    const tmpLaunchRepo = path.join(tmpDir, "isolated-launch-repo");
    fs.mkdirSync(tmpLaunchRepo, { recursive: true });

    const token = `mcp_e2e_${Date.now()}`;
    const backend = await spawnRealBackend(exePath, token);
    const wsUrl = `ws://127.0.0.1:${backend.port}/?token=${encodeURIComponent(token)}`;

    const mcpNdjson = new McpStdioClient({ framing: "ndjson", args: ["--ws", wsUrl] });
    const mcpContentLen = new McpStdioClient({ framing: "content-length", args: ["--ws", wsUrl] });
    const mcpLaunch = new McpStdioClient({
      framing: "ndjson",
      args: ["--repo-root", tmpLaunchRepo],
    });
    const chrome = new SimulatedChromePage(wsUrl);

    try {
      // 1. MCP handshake & tools/list (all 14 tools)
      const init = await mcpNdjson.request("initialize", {
        protocolVersion: "2025-03-26",
        clientInfo: { name: "e2e-test", version: "1.0" },
      });
      assert.equal(init.serverInfo.name, "animean-backend-debug");
      mcpNdjson.notify("notifications/initialized");

      const list = await mcpNdjson.request("tools/list");
      assert.equal(list.tools.length, 14);

      // 2. Pre-Chrome backend_status: must inspect safely and release m_presenter before Chrome joins
      const preChromeStatus = (await mcpNdjson.callTool("backend_status")).json;
      assert.equal(preChromeStatus.connected, true);
      assert.equal(preChromeStatus.browserSized, false);

      // 3. Simulated Chrome connects, resizes to 1280x800, announces fieldDisplay support
      await chrome.connect();
      chrome.sendResize("main", 1280, 800, 1);
      await chrome.call("board.capabilities", { view: "main", fieldDisplay: true });
      const resizedBoard = await chrome.waitForMainBoard(
        (b) => b.w === 1280 && b.h === 800 && b.totalOps > 0,
        "Chrome 1280x800 presenter takeover"
      );
      assert.equal(resizedBoard.w, 1280);
      assert.equal(resizedBoard.h, 800);

      // 4. Chrome draws stroke #1
      const opsBeforeStroke1 = chrome.latestMainBoard.totalOps;
      const frameBeforeStroke1 = chrome.latestMainBoard.frame;
      chrome.drawStroke("main", 180);
      const afterStroke1 = await chrome.waitForMainBoard(
        (b) => b.frame > frameBeforeStroke1 && b.totalOps > opsBeforeStroke1,
        "stroke #1 board update"
      );
      assert(afterStroke1.totalOps > opsBeforeStroke1);

      // 5. Inspect live Chrome session through MCP tools
      const statusAfter = (await mcpNdjson.callTool("backend_status")).json;
      assert.equal(statusAfter.browserSized, true);
      assert.equal(statusAfter.boards.main.width, 1280);
      assert.equal(statusAfter.boards.main.height, 800);

      const reconnectRes = (await mcpNdjson.callTool("backend_connect", { forceReconnect: true })).json;
      assert.equal(reconnectRes.releasedUntilBrowserConnects, false);

      const liveState = (
        await mcpNdjson.callTool("get_live_state", { keys: ["tools", "layers/main", "history"] })
      ).json;
      assert(liveState.state.tools);
      assert(liveState.state["layers/main"]);
      assert(liveState.state.history);

      const drawSummary = (
        await mcpNdjson.callTool("get_board_drawlist_summary", { view: "main", limit: 10 })
      ).json;
      assert.equal(drawSummary.available, true);
      assert.equal(drawSummary.width, 1280);
      assert.equal(drawSummary.height, 800);
      assert(drawSummary.pathOps > 0);
      assert(Array.isArray(drawSummary.opsSlice) && drawSummary.opsSlice.length > 0);

      const scene = (await mcpNdjson.callTool("inspect_scene", { view: "both" })).json;
      assert.equal(scene.views.main.available, true);
      assert.equal(scene.views.main.fieldDisplaySupported, true);
      assert(scene.views.main.nonEmptyCells.length >= 1);
      const strokeCell = scene.views.main.nonEmptyCells.find((c) => c.strokeCount > 0);
      assert(
        strokeCell,
        `Expected a cell with strokeCount > 0, got: ${JSON.stringify(scene.views.main.nonEmptyCells)}`
      );

      const geom = (
        await mcpNdjson.callTool("inspect_cell_geometry", {
          view: "main",
          frame: strokeCell.frame,
          layer: strokeCell.layer,
          includeCommands: true,
        })
      ).json;
      assert(geom.totalStrokeCount >= 1);
      assert(geom.strokes[0].commands.length >= 1);
      assert(geom.strokes[0].bounds);

      const geomPoly = (
        await mcpNdjson.callTool("inspect_cell_geometry", {
          view: "main",
          frame: strokeCell.frame,
          layer: strokeCell.layer,
          toPoly: true,
        })
      ).json;
      assert(Array.isArray(geomPoly.strokes[0].polylines));

      const automap = (await mcpNdjson.callTool("inspect_automapping", { view: "both" })).json;
      assert.equal(automap.views.main.fieldDisplaySupported, true);
      assert(Object.keys(automap.autoMappingConstants).length > 0);

      // 6. Live hook tracing while Chrome draws stroke #2
      const traceStart = (await mcpNdjson.callTool("trace_hooks", { action: "start" })).json;
      assert.equal(traceStart.active, true);

      const opsBeforeStroke2 = chrome.latestMainBoard.totalOps;
      const frameBeforeStroke2 = chrome.latestMainBoard.frame;
      chrome.drawStroke("main", 320);
      await chrome.waitForMainBoard(
        (b) => b.frame > frameBeforeStroke2 && b.totalOps > opsBeforeStroke2,
        "stroke #2 board update"
      );

      const traceRead = (await mcpNdjson.callTool("trace_hooks", { action: "read" })).json;
      assert(
        traceRead.events.some((ev) => ev.event === "linefinish"),
        `Expected 'linefinish' in trace events, got: ${JSON.stringify(traceRead.events.map((e) => e.event))}`
      );

      const traceStop = (await mcpNdjson.callTool("trace_hooks", { action: "stop" })).json;
      assert.equal(traceStop.active, false);

      // 7. Offscreen PNG render & JSON repro bundle export
      const pngPath = path.join(tmpDir, "frame_main.png");
      const pngTool = await mcpNdjson.callTool("render_frame_png", {
        view: "main",
        frame: 0,
        width: 320,
        height: 180,
        inlineImage: true,
        outputPath: pngPath,
      });
      assert(fs.existsSync(pngPath));
      const pngMagic = fs.readFileSync(pngPath).subarray(0, 4);
      assert.deepEqual(Array.from(pngMagic), [0x89, 0x50, 0x4e, 0x47]);
      assert.equal(pngTool.raw.content.length, 2);
      assert.equal(pngTool.raw.content[1].type, "image");
      assert.equal(pngTool.raw.content[1].mimeType, "image/png");

      const reproPath = path.join(tmpDir, "repro_bundle.json");
      const reproSummary = (
        await mcpNdjson.callTool("export_repro_bundle", {
          label: "e2e_test",
          outputPath: reproPath,
        })
      ).json;
      assert(fs.existsSync(reproPath));
      assert(reproSummary.bytes > 0);
      const bundle = JSON.parse(fs.readFileSync(reproPath, "utf-8"));
      assert.equal(bundle.schemaVersion, 1);
      const bundleStrokeCell = bundle.views.main.nonEmptyCells.find((c) => c.image?.stroke_count >= 2);
      assert(bundleStrokeCell, "Expected repro bundle to capture >= 2 strokes on main board");

      // 8. eval_python & call_backend_rpc (undo/redo)
      const pyEval = (
        await mcpNdjson.callTool("eval_python", {
          command: "print('LAYER_COUNT', getattr(main_model, 'scene', main_model).layer_count())",
        })
      ).json;
      assert.match(pyEval.output, /LAYER_COUNT\s+\d+/);

      const geomBeforeUndo = (
        await mcpNdjson.callTool("inspect_cell_geometry", {
          view: "main",
          frame: strokeCell.frame,
          layer: strokeCell.layer,
        })
      ).json;
      const undoCountTarget = geomBeforeUndo.totalStrokeCount - 1;

      await mcpNdjson.callTool("call_backend_rpc", { method: "history.undo", params: {} });
      const geomAfterUndo = (
        await mcpNdjson.callTool("inspect_cell_geometry", {
          view: "main",
          frame: strokeCell.frame,
          layer: strokeCell.layer,
        })
      ).json;
      assert.equal(geomAfterUndo.totalStrokeCount, undoCountTarget);

      await mcpNdjson.callTool("call_backend_rpc", { method: "history.redo", params: {} });
      const geomAfterRedo = (
        await mcpNdjson.callTool("inspect_cell_geometry", {
          view: "main",
          frame: strokeCell.frame,
          layer: strokeCell.layer,
        })
      ).json;
      assert.equal(geomAfterRedo.totalStrokeCount, geomBeforeUndo.totalStrokeCount);

      // 9. Verify pythonDebug stays concise (no giant JSON bloat from probe calls)
      const debugLogs = (await mcpNdjson.callTool("get_python_debug_logs", { limit: 120 })).json;
      const probeEntries = debugLogs.entries.filter((e) => e.text.includes("[mcp_probe]"));
      assert(probeEntries.length >= 1, "Expected [mcp_probe] summary lines in pythonDebug");
      for (const entry of probeEntries) {
        assert(
          entry.text.length < 350,
          `pythonDebug entry should stay compact, got length ${entry.text.length}`
        );
      }

      // 10. Confirm Chrome kept presenter dimensions & fieldDisplaySupported throughout all MCP calls
      const sceneEnd = (await mcpNdjson.callTool("inspect_scene", { view: "main" })).json;
      assert.equal(sceneEnd.views.main.fieldDisplaySupported, true);
      assert.equal(chrome.latestMainBoard.w, 1280);
      assert.equal(chrome.latestMainBoard.h, 800);

      // 11. Content-Length stdio framing end-to-end check
      const clInit = await mcpContentLen.request("initialize", { protocolVersion: "2024-11-05" });
      assert.equal(clInit.serverInfo.name, "animean-backend-debug");
      mcpContentLen.notify("notifications/initialized");
      const clList = await mcpContentLen.request("tools/list");
      assert.equal(clList.tools.length, 14);
      const clStatus = (await mcpContentLen.callTool("backend_status")).json;
      assert.equal(clStatus.connected, true);
      assert.equal(clStatus.boards.main.width, 1280);

      // 12. Tool #14: backend_launch_dev + session reuse + auto-discovery in an isolated repo dir
      await mcpLaunch.request("initialize", { protocolVersion: "2024-11-05" });
      mcpLaunch.notify("notifications/initialized");
      const launched1 = (
        await mcpLaunch.callTool("backend_launch_dev", {
          port: 0,
          token: `isolated_${Date.now()}`,
          backendExe: exePath,
          startVite: false,
          openBrowser: false,
        })
      ).json;
      assert(launched1.backendPid > 0);
      assert(launched1.backendPort > 0);
      assert.equal(launched1.backendReused, false);

      const launched2 = (
        await mcpLaunch.callTool("backend_launch_dev", {
          port: 0,
          startVite: false,
          openBrowser: false,
        })
      ).json;
      assert.equal(launched2.backendReused, true);
      assert.equal(launched2.backendPid, launched1.backendPid);

      const discoveredStatus = (await mcpLaunch.callTool("backend_status")).json;
      assert.equal(discoveredStatus.connected, true);
      assert.match(discoveredStatus.endpointSource, /session_file$/);
    } finally {
      await stopDevEnvironment({ repoRoot: tmpLaunchRepo }).catch(() => {});
      await Promise.allSettled([mcpNdjson.close(), mcpContentLen.close(), mcpLaunch.close()]);
      chrome.close();
      try {
        backend.child.kill();
      } catch {}
      await new Promise((r) => setTimeout(r, 150));
      fs.rmSync(tmpDir, { recursive: true, force: true, maxRetries: 10, retryDelay: 100 });
    }
  }
);
