export interface BoardMessage {
  t: "board";
  view: string;
  frame: number;
  w: number;
  h: number;
  keep: number;
  ops: unknown[][];
}

export interface ImageMeta {
  t: "image";
  key: string;
  w: number;
  h: number;
  bytes: number;
}

export type InputMessage =
  | {
      t?: "input";
      view: string;
      kind: "press" | "move" | "release";
      x: number;
      y: number;
      button: number;
      buttons: number;
      modifiers: number;
      pressure: number;
      time: number;
    }
  | {
      t?: "input";
      view: string;
      kind: "wheel";
      x: number;
      y: number;
      dx: number;
      dy: number;
      modifiers: number;
    }
  | {
      t?: "input";
      view: string;
      kind: "enter";
      x: number;
      y: number;
    }
  | {
      t?: "input";
      view: string;
      kind: "leave";
    }
  | {
      t?: "input";
      view: string;
      kind: "resize";
      w: number;
      h: number;
      dpr: number;
    }
  | {
      t?: "input";
      view: string;
      kind: "focus";
      in: boolean;
    };

export interface WebSocketLike {
  binaryType?: string;
  readyState: number;
  send(data: string | ArrayBuffer | Uint8Array): void;
  close(): void;
  addEventListener(type: string, listener: (ev: unknown) => void): void;
  removeEventListener?(type: string, listener: (ev: unknown) => void): void;
}

export interface BackendClientOptions {
  createWebSocket?: (url: string) => WebSocketLike;
  onDisconnect?: (ev?: unknown) => void;
}

interface PendingCall {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
}

const WS_OPEN = 1;

export class BackendClient {
  private ws: WebSocketLike | null = null;
  private nextCallId = 1;
  private pendingCalls = new Map<number, PendingCall>();
  private stateListeners = new Map<string, Set<(value: unknown, key: string) => void>>();
  private anyStateListeners = new Set<(key: string, value: unknown) => void>();
  private boardListeners = new Map<string, Set<(msg: BoardMessage) => void>>();
  private imageListeners = new Set<(meta: ImageMeta, data: Uint8Array) => void>();
  private disconnectListeners = new Set<(ev?: unknown) => void>();
  private pendingImageMeta: ImageMeta | null = null;
  private createWebSocket: (url: string) => WebSocketLike;

  constructor(options: BackendClientOptions = {}) {
    this.createWebSocket =
      options.createWebSocket ?? ((url: string) => new WebSocket(url) as unknown as WebSocketLike);
    if (options.onDisconnect) {
      this.disconnectListeners.add(options.onDisconnect);
    }
  }

  get connected(): boolean {
    return this.ws !== null && this.ws.readyState === WS_OPEN;
  }

  connect(url: string): Promise<void> {
    if (this.ws) {
      try {
        this.ws.close();
      } catch {
        // 忽略旧连接关闭异常
      }
      this.ws = null;
    }
    this.pendingImageMeta = null;

    return new Promise<void>((resolve, reject) => {
      let opened = false;
      let ws: WebSocketLike;
      try {
        ws = this.createWebSocket(url);
      } catch (e) {
        reject(e instanceof Error ? e : new Error(String(e)));
        return;
      }
      ws.binaryType = "arraybuffer";
      this.ws = ws;

      const onOpen = () => {
        opened = true;
        resolve();
      };

      const onError = (ev: unknown) => {
        if (!opened) {
          reject(new Error(`WebSocket 连接失败: ${url}`));
        } else {
          void ev;
        }
      };

      const onClose = (ev: unknown) => {
        if (this.ws === ws) {
          this.ws = null;
        }
        this.pendingImageMeta = null;
        const err = new Error("WebSocket 已断开");
        for (const [, pending] of this.pendingCalls) {
          pending.reject(err);
        }
        this.pendingCalls.clear();
        if (!opened) {
          reject(err);
          return;
        }
        for (const fn of this.disconnectListeners) {
          fn(ev);
        }
      };

      const onMessage = (ev: unknown) => {
        const data = (ev as { data?: unknown })?.data ?? ev;
        this.handleFrame(data);
      };

      ws.addEventListener("open", onOpen);
      ws.addEventListener("error", onError);
      ws.addEventListener("close", onClose);
      ws.addEventListener("message", onMessage);

      if (ws.readyState === WS_OPEN) {
        onOpen();
      }
    });
  }

  close(): void {
    if (this.ws) {
      const ws = this.ws;
      this.ws = null;
      ws.close();
    }
  }

  onDisconnect(fn: (ev?: unknown) => void): () => void {
    this.disconnectListeners.add(fn);
    return () => {
      this.disconnectListeners.delete(fn);
    };
  }

  call<T = unknown>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    if (!this.ws || this.ws.readyState !== WS_OPEN) {
      return Promise.reject(new Error("WebSocket 未连接"));
    }
    const id = this.nextCallId++;
    const payload = JSON.stringify({
      t: "call",
      id,
      method,
      params,
    });
    return new Promise<T>((resolve, reject) => {
      this.pendingCalls.set(id, {
        resolve: resolve as (value: unknown) => void,
        reject,
      });
      try {
        this.ws!.send(payload);
      } catch (e) {
        this.pendingCalls.delete(id);
        reject(e instanceof Error ? e : new Error(String(e)));
      }
    });
  }

  sendInput(msg: InputMessage): void {
    this.sendRaw({ ...msg, t: "input" });
  }

  sendAck(view: string, frame: number): void {
    this.sendRaw({ t: "ack", view, frame });
  }

  sendRaw(msg: Record<string, unknown>): void {
    if (!this.ws || this.ws.readyState !== WS_OPEN) return;
    this.ws.send(JSON.stringify(msg));
  }

  onState(key: string, fn: (value: unknown, key: string) => void): () => void {
    if (key === "*") {
      const wrapper = (k: string, v: unknown) => fn(v, k);
      this.anyStateListeners.add(wrapper);
      return () => {
        this.anyStateListeners.delete(wrapper);
      };
    }
    let set = this.stateListeners.get(key);
    if (!set) {
      set = new Set();
      this.stateListeners.set(key, set);
    }
    set.add(fn);
    return () => {
      set?.delete(fn);
    };
  }

  onBoard(view: string, fn: (msg: BoardMessage) => void): () => void {
    let set = this.boardListeners.get(view);
    if (!set) {
      set = new Set();
      this.boardListeners.set(view, set);
    }
    set.add(fn);
    return () => {
      set?.delete(fn);
    };
  }

  onImage(fn: (meta: ImageMeta, data: Uint8Array) => void): () => void {
    this.imageListeners.add(fn);
    return () => {
      this.imageListeners.delete(fn);
    };
  }

  private handleFrame(data: unknown): void {
    if (typeof data === "string") {
      let msg: Record<string, unknown>;
      try {
        msg = JSON.parse(data) as Record<string, unknown>;
      } catch {
        return;
      }
      const t = msg.t;
      if (t === "image") {
        this.pendingImageMeta = {
          t: "image",
          key: String(msg.key ?? ""),
          w: Number(msg.w ?? 0),
          h: Number(msg.h ?? 0),
          bytes: Number(msg.bytes ?? 0),
        };
        return;
      }
      // 若上一帧是未配对的 image 文本帧，被其它文本帧打断则丢弃挂起头
      this.pendingImageMeta = null;

      if (t === "reply") {
        const id = Number(msg.id);
        const pending = this.pendingCalls.get(id);
        if (!pending) return;
        this.pendingCalls.delete(id);
        if (msg.ok) {
          pending.resolve(msg.result);
        } else {
          pending.reject(new Error(String(msg.error ?? "调用失败")));
        }
        return;
      }

      if (t === "state") {
        const key = String(msg.key ?? "");
        const value = msg.value;
        const listeners = this.stateListeners.get(key);
        if (listeners) {
          for (const fn of listeners) fn(value, key);
        }
        for (const fn of this.anyStateListeners) {
          fn(key, value);
        }
        return;
      }

      if (t === "board") {
        const boardMsg: BoardMessage = {
          t: "board",
          view: String(msg.view ?? "main"),
          frame: Number(msg.frame ?? 0),
          w: Number(msg.w ?? 0),
          h: Number(msg.h ?? 0),
          keep: Number(msg.keep ?? 0),
          ops: Array.isArray(msg.ops) ? (msg.ops as unknown[][]) : [],
        };
        const listeners = this.boardListeners.get(boardMsg.view);
        if (listeners) {
          for (const fn of listeners) fn(boardMsg);
        }
        return;
      }
      return;
    }

    // 二进制帧：与上一条 image 文本帧配对
    const meta = this.pendingImageMeta;
    this.pendingImageMeta = null;
    if (!meta) return;

    let bytes: Uint8Array | null = null;
    if (data instanceof ArrayBuffer) {
      bytes = new Uint8Array(data);
    } else if (ArrayBuffer.isView(data)) {
      bytes = new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
    }
    if (!bytes) return;

    for (const fn of this.imageListeners) {
      fn(meta, bytes);
    }
  }
}
