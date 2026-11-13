import { randomUUID } from "node:crypto";
import fs from "node:fs";
import path from "node:path";
import { getToolkitPaths, resolveBackendEndpoint } from "./discovery.mjs";

const MAX_RECENT_EVENTS = 100;
const OP_NAMES = ["T", "P", "B", "G", "O", "M", "K", "R", "S", "I", "H", "F", "f"];

async function toBuffer(data) {
  if (Buffer.isBuffer(data)) return data;
  if (data instanceof ArrayBuffer) return Buffer.from(data);
  if (ArrayBuffer.isView(data)) {
    return Buffer.from(data.buffer, data.byteOffset, data.byteLength);
  }
  if (data && typeof data.arrayBuffer === "function") {
    return Buffer.from(await data.arrayBuffer());
  }
  return Buffer.from(data);
}

/**
 * Passive multi-client WebSocket observer & RPC bridge for AnimeAnBackend.exe.
 *
 * Strictly never sends `input`, `texture.setHome`, or `board.capabilities` so
 * an interactive Chrome tab always keeps presenter ownership (`m_presenter`)
 * and WebGL2 field-display capabilities intact.
 */
export class BackendDebugClient {
  /**
   * @param {{ repoRoot?: string, wsUrl?: string }} [options]
   */
  constructor(options = {}) {
    this.paths = getToolkitPaths(options.repoRoot);
    this.startupWsUrl = options.wsUrl || null;
    this.explicitWsUrl = this.startupWsUrl;
    this.ws = null;
    this.wsUrl = null;
    this.endpointSource = null;
    this.sessionMeta = null;
    this.connectPromise = null;
    this.connectedAt = null;
    this.lastError = null;
    this.traceActive = false;

    this.nextCallId = 1;
    this.pendingCalls = new Map();
    this.stateMap = new Map();
    this.recentEvents = [];
    this.boardFrames = new Map();
    this.imagesMeta = new Map();
    this.fieldsMeta = new Map();
    this.retainedImageKeys = new Set();

    this.pendingBinaryHeader = null;
    this.stateWaiters = [];
    this.boardWaiters = [];
    this.imageWaiters = [];
    this.binaryChain = Promise.resolve();
  }

  isConnected() {
    return Boolean(this.ws && this.ws.readyState === WebSocket.OPEN);
  }

  isBrowserSized() {
    const main = this.boardFrames.get("main");
    const child = this.boardFrames.get("child");
    const mainSized = Boolean(main && main.w > 0 && (main.w !== 800 || main.h !== 600));
    const childSized = Boolean(child && child.w > 0 && (child.w !== 400 || child.h !== 400));
    return mainSized || childSized;
  }

  resetSessionCache() {
    this.nextCallId = 1;
    this.stateMap.clear();
    this.recentEvents.length = 0;
    this.boardFrames.clear();
    this.imagesMeta.clear();
    this.fieldsMeta.clear();
    this.pendingBinaryHeader = null;
  }

  /**
   * Disconnect cleanly without holding onto backend presenter state.
   * @param {string} [reason]
   */
  disconnect(reason = "Client disconnected") {
    const ws = this.ws;
    this.ws = null;
    this.connectPromise = null;
    this.connectedAt = null;

    for (const [id, pending] of this.pendingCalls.entries()) {
      clearTimeout(pending.timer);
      pending.reject(new Error(`${reason} (pending RPC id=${id})`));
    }
    this.pendingCalls.clear();
    this._rejectWaiters(this.stateWaiters, reason);
    this._rejectWaiters(this.boardWaiters, reason);
    this._rejectWaiters(this.imageWaiters, reason);

    if (ws) {
      try {
        if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
          ws.close();
        }
      } catch {
        // Ignore socket close error
      }
    }
  }

  _rejectWaiters(waiters, reason) {
    while (waiters.length > 0) {
      const waiter = waiters.pop();
      clearTimeout(waiter.timer);
      waiter.reject(new Error(reason));
    }
  }

  /**
   * Release connection after an inspection call if no browser has sized a board yet,
   * so Chrome gets `m_presenter` cleanly when the user opens it.
   */
  releaseIfNoBrowserPresenter() {
    if (!this.traceActive && !this.isBrowserSized() && this.isConnected()) {
      this.disconnect("Released pre-browser observer connection");
    }
  }

  /**
   * Ensure a live WebSocket connection to the resolved (or explicit) backend endpoint.
   * @param {{ wsUrl?: string, forceReconnect?: boolean, timeoutMs?: number }} [options]
   */
  async ensureConnected(options = {}) {
    if (options.wsUrl) {
      this.explicitWsUrl = options.wsUrl;
    }
    const resolved = resolveBackendEndpoint({
      explicitWsUrl: options.wsUrl || this.explicitWsUrl || undefined,
      repoRoot: this.paths.repoRoot,
    });
    this.endpointSource = resolved.source;
    this.sessionMeta = resolved.sessionMeta;

    if (this.isConnected() && !options.forceReconnect && this.wsUrl === resolved.wsUrl) {
      return {
        wsUrl: this.wsUrl,
        source: this.endpointSource,
        sessionMeta: this.sessionMeta,
        reused: true,
      };
    }

    if (this.connectPromise && !options.forceReconnect && this.wsUrl === resolved.wsUrl) {
      return this.connectPromise;
    }

    if (this.ws || options.forceReconnect) {
      this.disconnect("Reconnecting backend debug client");
    }

    this.wsUrl = resolved.wsUrl;
    this.connectPromise = this._connectInternal(resolved, options.timeoutMs ?? 6000);
    try {
      return await this.connectPromise;
    } finally {
      this.connectPromise = null;
    }
  }

  async _connectInternal(resolved, timeoutMs) {
    this.resetSessionCache();
    this.lastError = null;

    const ws = new WebSocket(resolved.wsUrl);
    ws.binaryType = "arraybuffer";
    this.ws = ws;

    await new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        try {
          ws.close();
        } catch {
          // Ignore
        }
        reject(new Error(`Timed out connecting to AnimeAnBackend at ${resolved.wsUrl}`));
      }, timeoutMs);

      const onOpen = () => {
        clearTimeout(timer);
        ws.removeEventListener("error", onError);
        ws.removeEventListener("close", onCloseBeforeOpen);
        resolve();
      };
      const onError = (err) => {
        clearTimeout(timer);
        ws.removeEventListener("open", onOpen);
        ws.removeEventListener("close", onCloseBeforeOpen);
        reject(new Error(`WebSocket connection failed (${resolved.wsUrl}): ${err?.message || "connection refused"}`));
      };
      const onCloseBeforeOpen = (ev) => {
        clearTimeout(timer);
        ws.removeEventListener("open", onOpen);
        ws.removeEventListener("error", onError);
        reject(new Error(`WebSocket closed during handshake (${ev?.code ?? "unknown"} ${ev?.reason || ""})`.trim()));
      };

      ws.addEventListener("open", onOpen, { once: true });
      ws.addEventListener("error", onError, { once: true });
      ws.addEventListener("close", onCloseBeforeOpen, { once: true });

      ws.addEventListener("message", (event) => {
        this.binaryChain = this.binaryChain
          .then(() => this._handleMessage(ws, event.data))
          .catch((err) => {
            this.lastError = err instanceof Error ? err.message : String(err);
          });
      });

      ws.addEventListener("close", () => {
        if (this.ws === ws) {
          this.disconnect("Backend WebSocket closed");
        }
      });

      ws.addEventListener("error", (err) => {
        this.lastError = err?.message || "WebSocket error";
      });
    });

    this.connectedAt = new Date().toISOString();

    // Wait briefly for initial state push and any immediate board frame.
    try {
      await this.waitForState(
        (m) => m.has("window") && m.has("tools") && m.has("history"),
        "initial state snapshot",
        Math.min(3000, timeoutMs)
      );
    } catch {
      // Partial state is still inspectable if backend had non-standard startup.
    }

    if (!this.isBrowserSized()) {
      try {
        await this.waitForBoard((frame) => frame.w > 0 && frame.h > 0, "sized board", 200);
      } catch {
        // Expected when no browser client has connected yet.
      }
    }

    return {
      wsUrl: this.wsUrl,
      source: this.endpointSource,
      sessionMeta: this.sessionMeta,
      reused: false,
    };
  }

  async _handleMessage(ws, raw) {
    if (this.ws !== ws) return;

    if (typeof raw !== "string") {
      const buf = await toBuffer(raw);
      const pending = this.pendingBinaryHeader;
      this.pendingBinaryHeader = null;
      if (!pending) return;

      if (pending.kind === "image") {
        const meta = pending.meta;
        const shouldRetain = !meta.view || this.retainedImageKeys.has(meta.key);
        const entry = {
          key: meta.key,
          w: meta.w,
          h: meta.h,
          bytes: meta.bytes,
          view: meta.view || null,
          hasData: shouldRetain,
          data: shouldRetain ? buf : null,
          updatedAt: Date.now(),
        };
        this.imagesMeta.set(meta.key, entry);
        this._notifyImageWaiters(entry);
      } else if (pending.kind === "field") {
        const meta = pending.meta;
        this.fieldsMeta.set(meta.key, {
          key: meta.key,
          w: meta.w,
          h: meta.h,
          version: meta.version,
          bytes: meta.bytes,
          receivedBytes: buf.length,
          updatedAt: Date.now(),
        });
      }
      return;
    }

    let msg;
    try {
      msg = JSON.parse(raw);
    } catch {
      return;
    }

    if (msg.t === "state") {
      if (msg.key === "event") {
        this.recentEvents.push({
          ts: new Date().toISOString(),
          ...(typeof msg.value === "object" && msg.value !== null ? msg.value : { value: msg.value }),
        });
        if (this.recentEvents.length > MAX_RECENT_EVENTS) {
          this.recentEvents.splice(0, this.recentEvents.length - MAX_RECENT_EVENTS);
        }
      } else {
        this.stateMap.set(msg.key, msg.value);
      }
      this._notifyStateWaiters();
      return;
    }

    if (msg.t === "board") {
      // Immediately ACK so multi-client frame delivery never stalls or hits backpressure limits.
      if (ws.readyState === WebSocket.OPEN) {
        try {
          ws.send(JSON.stringify({ t: "ack", view: msg.view, frame: msg.frame }));
        } catch {
          // Ignore send failure on closing socket
        }
      }
      const prevOps = this.boardFrames.get(msg.view)?.ops || [];
      const keep = Math.max(0, Math.min(prevOps.length, Number(msg.keep || 0)));
      const deltaOps = Array.isArray(msg.ops) ? msg.ops : [];
      const ops = prevOps.slice(0, keep).concat(deltaOps);
      const frameEntry = {
        view: msg.view,
        frame: msg.frame,
        w: msg.w,
        h: msg.h,
        keep,
        deltaCount: deltaOps.length,
        totalOps: ops.length,
        ops,
        updatedAt: Date.now(),
      };
      this.boardFrames.set(msg.view, frameEntry);
      this._notifyBoardWaiters(frameEntry);
      return;
    }

    if (msg.t === "image") {
      this.pendingBinaryHeader = { kind: "image", meta: msg };
      return;
    }

    if (msg.t === "image.forget") {
      this.imagesMeta.delete(msg.key);
      return;
    }

    if (msg.t === "field") {
      this.pendingBinaryHeader = { kind: "field", meta: msg };
      return;
    }

    if (msg.t === "reply") {
      const pending = this.pendingCalls.get(msg.id);
      if (!pending) return;
      this.pendingCalls.delete(msg.id);
      clearTimeout(pending.timer);
      if (msg.ok) {
        pending.resolve(msg.result ?? {});
      } else {
        pending.reject(new Error(msg.error || `Backend RPC failed (id=${msg.id})`));
      }
    }
  }

  _notifyStateWaiters() {
    for (let i = this.stateWaiters.length - 1; i >= 0; i -= 1) {
      const w = this.stateWaiters[i];
      if (w.predicate(this.stateMap)) {
        this.stateWaiters.splice(i, 1);
        clearTimeout(w.timer);
        w.resolve();
      }
    }
  }

  _notifyBoardWaiters(frameEntry) {
    for (let i = this.boardWaiters.length - 1; i >= 0; i -= 1) {
      const w = this.boardWaiters[i];
      if (w.predicate(frameEntry)) {
        this.boardWaiters.splice(i, 1);
        clearTimeout(w.timer);
        w.resolve(frameEntry);
      }
    }
  }

  _notifyImageWaiters(imageEntry) {
    for (let i = this.imageWaiters.length - 1; i >= 0; i -= 1) {
      const w = this.imageWaiters[i];
      if (w.predicate(imageEntry)) {
        this.imageWaiters.splice(i, 1);
        clearTimeout(w.timer);
        w.resolve(imageEntry);
      }
    }
  }

  waitForState(predicate, desc, timeoutMs = 5000) {
    if (predicate(this.stateMap)) {
      return Promise.resolve();
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = this.stateWaiters.indexOf(entry);
        if (idx >= 0) this.stateWaiters.splice(idx, 1);
        reject(new Error(`Timed out waiting for state (${desc})`));
      }, timeoutMs);
      const entry = { predicate, resolve, reject, timer };
      this.stateWaiters.push(entry);
    });
  }

  waitForBoard(predicate, desc, timeoutMs = 5000) {
    for (const frame of this.boardFrames.values()) {
      if (predicate(frame)) return Promise.resolve(frame);
    }
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = this.boardWaiters.indexOf(entry);
        if (idx >= 0) this.boardWaiters.splice(idx, 1);
        reject(new Error(`Timed out waiting for board frame (${desc})`));
      }, timeoutMs);
      const entry = { predicate, resolve, reject, timer };
      this.boardWaiters.push(entry);
    });
  }

  waitForImage(key, timeoutMs = 10000) {
    const existing = this.imagesMeta.get(key);
    if (existing && existing.hasData && existing.data) {
      return Promise.resolve(existing);
    }
    this.retainedImageKeys.add(key);
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        const idx = this.imageWaiters.indexOf(entry);
        if (idx >= 0) this.imageWaiters.splice(idx, 1);
        this.retainedImageKeys.delete(key);
        reject(new Error(`Timed out waiting for image binary packet: ${key}`));
      }, timeoutMs);
      const entry = {
        predicate: (img) => img.key === key && img.hasData && Boolean(img.data),
        resolve: (img) => {
          this.retainedImageKeys.delete(key);
          resolve(img);
        },
        reject: (err) => {
          this.retainedImageKeys.delete(key);
          reject(err);
        },
        timer,
      };
      this.imageWaiters.push(entry);
    });
  }

  /**
   * Call a BackendSession JSON-RPC method over the live WebSocket.
   * @param {string} method
   * @param {Record<string, any>} [params]
   * @param {number} [timeoutMs]
   */
  async call(method, params = {}, timeoutMs = 15000) {
    await this.ensureConnected();
    if (!this.isConnected()) {
      throw new Error("Backend WebSocket is not connected");
    }
    const id = this.nextCallId++;
    const payload = JSON.stringify({ t: "call", id, method, params });

    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pendingCalls.delete(id);
        reject(new Error(`Timed out waiting for backend RPC '${method}' (id=${id})`));
      }, timeoutMs);
      this.pendingCalls.set(id, { resolve, reject, timer });
      try {
        this.ws.send(payload);
      } catch (err) {
        this.pendingCalls.delete(id);
        clearTimeout(timer);
        reject(err instanceof Error ? err : new Error(String(err)));
      }
    });
  }

  /**
   * Invoke an out-of-band inspection op via `_mcp_probe.py` without bloating `pythonDebug`.
   * @param {string} op
   * @param {Record<string, any>} [args]
   * @param {number} [timeoutMs]
   */
  async callProbe(op, args = {}, timeoutMs = 20000) {
    await this.ensureConnected();
    fs.mkdirSync(this.paths.ipcDir, { recursive: true });
    const id = randomUUID();
    const reqPath = path.join(this.paths.ipcDir, `req_${id}.json`);
    const respPath = path.join(this.paths.ipcDir, `resp_${id}.json`);

    fs.writeFileSync(reqPath, JSON.stringify({ op, args }), "utf-8");
    const cmd = `import importlib, _mcp_probe; importlib.reload(_mcp_probe).run_ipc(r'${reqPath}', r'${respPath}')`;

    try {
      const rpcRes = await this.call(
        "python.run",
        {
          scriptDir: this.paths.pythonProbeDir,
          command: cmd,
        },
        timeoutMs
      );
      if (!fs.existsSync(respPath)) {
        throw new Error(
          `Python probe '${op}' produced no response file. Backend python.run output: ${rpcRes?.output ?? "(none)"}`
        );
      }
      const parsed = JSON.parse(fs.readFileSync(respPath, "utf-8"));
      if (!parsed.ok) {
        const tb = parsed.traceback ? `\n${parsed.traceback}` : "";
        throw new Error(`Python probe '${op}' failed: ${parsed.error}${tb}`);
      }
      if (op === "trace_hooks" && parsed.data && typeof parsed.data.active === "boolean") {
        this.traceActive = parsed.data.active;
      }
      return parsed.data;
    } finally {
      for (const file of [reqPath, respPath, `${respPath}.tmp`]) {
        try {
          if (fs.existsSync(file)) {
            fs.unlinkSync(file);
          }
        } catch {
          // Ignore temp cleanup error
        }
      }
    }
  }

  /**
   * Build an op histogram + slice summary for a cached board frame.
   * @param {"main"|"child"} view
   * @param {{ offset?: number, limit?: number }} [options]
   */
  summarizeBoardDrawList(view = "main", options = {}) {
    const board = this.boardFrames.get(view);
    if (!board) {
      return {
        view,
        available: false,
        reason: "No board frame received yet (open Chrome or trigger a redraw if already open).",
      };
    }

    const histogram = {};
    for (const opName of OP_NAMES) histogram[opName] = 0;

    const activeFields = [];
    const strokesOrPathsCount = { pathOps: 0, rectOps: 0, imageOps: 0 };
    for (const op of board.ops) {
      if (!Array.isArray(op) || op.length === 0) continue;
      const code = String(op[0]);
      histogram[code] = (histogram[code] || 0) + 1;
      if (code === "S") strokesOrPathsCount.pathOps += 1;
      if (code === "R") strokesOrPathsCount.rectOps += 1;
      if (code === "I") strokesOrPathsCount.imageOps += 1;
      if (code === "F") {
        activeFields.push({
          key: op[1],
          version: op[2],
          rect: { x: op[3], y: op[4], w: op[5], h: op[6] },
          maxEdge: op[7],
          passes: op[8],
        });
      }
    }

    const offset = Math.max(0, Number(options.offset || 0));
    const limit = Math.max(0, Math.min(500, Number(options.limit || 0)));
    const opsSlice = limit > 0 ? board.ops.slice(offset, offset + limit) : undefined;

    return {
      view,
      available: true,
      frame: board.frame,
      width: board.w,
      height: board.h,
      keepPrefix: board.keep,
      lastDeltaCount: board.deltaCount,
      totalOps: board.totalOps,
      histogram,
      ...strokesOrPathsCount,
      fieldScopesInFrame: activeFields,
      fieldsMeta: Array.from(this.fieldsMeta.values()),
      ...(opsSlice !== undefined ? { opsOffset: offset, opsSlice } : {}),
    };
  }
}
