import type { BoardMessage, ImageMeta, InputMessage } from "./net.ts";

export const DEFAULT_RELAY_CHANNEL = "animean.relay";

// The windows of one AnimeAn run talk on channels named after that run. Two
// copies of AnimeAn on the same WebView2 profile share BroadcastChannel names,
// and must not hear each other: one copy's calls would reach both backends, and
// a closing copy's "bye" would cut the other's panels and dialogs off. The main
// page names the run, keeps the name across a reload of its window, and hands it
// to the windows it opens in their URL (`relay=`).
const RUN_PARAM = "relay";
const RUN_KEY = "animean.run";
const RUN_TAG = /^[A-Za-z0-9_-]{1,64}$/;
let currentRun: string | null = null;

export function runTag(): string {
  if (currentRun) return currentRun;
  const fromUrl = typeof location !== "undefined" ? new URLSearchParams(location.search).get(RUN_PARAM) : null;
  if (fromUrl && RUN_TAG.test(fromUrl)) return (currentRun = fromUrl);
  let kept: string | null = null;
  try {
    kept = sessionStorage.getItem(RUN_KEY);
  } catch {
    // No storage (tests, a locked-down page): a fresh name for this load.
  }
  currentRun = kept && RUN_TAG.test(kept)
    ? kept
    : typeof crypto !== "undefined" && typeof crypto.randomUUID === "function"
      ? crypto.randomUUID()
      : `${Date.now().toString(36)}${Math.random().toString(36).slice(2)}`;
  try {
    sessionStorage.setItem(RUN_KEY, currentRun);
  } catch {
    // As above.
  }
  return currentRun;
}

/** The relay channel of this run. */
export function relayChannelName(tag = runTag()): string {
  return `${DEFAULT_RELAY_CHANNEL}.${tag}`;
}

/** `url` (a page of this app) with this run's name, for a window opened from here. */
export function withRunTag(url: string, tag = runTag()): string {
  return `${url}${url.includes("?") ? "&" : "?"}${RUN_PARAM}=${encodeURIComponent(tag)}`;
}

export interface RelayChannelLike {
  postMessage(message: unknown): void;
  close?(): void;
  addEventListener?(type: string, listener: (ev: unknown) => void): void;
  removeEventListener?(type: string, listener: (ev: unknown) => void): void;
  onmessage?: ((ev: { data: unknown }) => void) | null;
}

export interface BackendClientLike {
  onState(key: string, fn: (value: unknown, key: string) => void): () => void;
  onImage(fn: (meta: ImageMeta, data: Uint8Array) => void): () => void;
  onDisconnect?(fn: (ev?: unknown) => void): () => void;
  call<T = unknown>(method: string, params?: Record<string, unknown>): Promise<T>;
}

export type PanelControlAction = "restore" | "close";

export function openRelayChannel(
  channelOrName?: string | RelayChannelLike | { channel?: string | RelayChannelLike; channelName?: string },
): { channel: RelayChannelLike; owned: boolean } {
  if (channelOrName && typeof channelOrName === "object") {
    if ("postMessage" in channelOrName && typeof channelOrName.postMessage === "function") {
      return { channel: channelOrName, owned: false };
    }
    const inner = (channelOrName as { channel?: string | RelayChannelLike; channelName?: string }).channel ??
      (channelOrName as { channelName?: string }).channelName;
    return openRelayChannel(inner);
  }
  // No name: this run's channel, where this run's main page listens.
  const name = typeof channelOrName === "string" && channelOrName ? channelOrName : relayChannelName();
  if (typeof BroadcastChannel !== "undefined") {
    return {
      channel: new BroadcastChannel(name) as unknown as RelayChannelLike,
      owned: true,
    };
  }
  throw new Error(`BroadcastChannel 不可用，请传入自定义通道 (${name})`);
}

function attachChannelListener(channel: RelayChannelLike, handler: (data: unknown) => void): () => void {
  const listener = (ev: unknown) => {
    const data = ev && typeof ev === "object" && "data" in (ev as Record<string, unknown>)
      ? (ev as { data: unknown }).data
      : ev;
    handler(data);
  };
  if (typeof channel.addEventListener === "function") {
    channel.addEventListener("message", listener);
    return () => {
      channel.removeEventListener?.("message", listener);
    };
  }
  const prev = channel.onmessage;
  channel.onmessage = (ev: { data: unknown }) => {
    prev?.(ev);
    listener(ev);
  };
  return () => {
    if (channel.onmessage !== null) {
      channel.onmessage = prev ?? null;
    }
  };
}

function toUint8Array(raw: unknown): Uint8Array {
  if (raw instanceof Uint8Array) return new Uint8Array(raw);
  if (raw instanceof ArrayBuffer) return new Uint8Array(raw);
  if (ArrayBuffer.isView(raw)) return new Uint8Array(raw.buffer, raw.byteOffset, raw.byteLength);
  if (Array.isArray(raw)) return new Uint8Array(raw);
  return new Uint8Array(0);
}

function parseThumbKey(key: string): { view: string; frame: string; size: string; revision: number } | null {
  const m = /^thumb_([^_]+)_([^_]+)_(\d+x\d+)_(\d+)$/.exec(key);
  if (!m) return null;
  const revision = Number(m[4]);
  if (!Number.isFinite(revision)) return null;
  return { view: m[1], frame: m[2], size: m[3], revision };
}

export function cacheRelayImage(
  images: Map<string, { meta: ImageMeta; data: Uint8Array }>,
  meta: ImageMeta,
  data: Uint8Array,
): void {
  const parsed = parseThumbKey(meta.key);
  if (parsed) {
    for (const existingKey of [...images.keys()]) {
      if (existingKey === meta.key) continue;
      const existingParsed = parseThumbKey(existingKey);
      if (!existingParsed) continue;
      if (existingParsed.view === parsed.view && existingParsed.frame === parsed.frame) {
        if (existingParsed.revision > parsed.revision) {
          return;
        }
        images.delete(existingKey);
      }
    }
  }
  images.set(meta.key, { meta, data });
}

export class RelayHost {
  private client: BackendClientLike;
  private channel: RelayChannelLike;
  private ownedChannel: boolean;
  private detachChannel: () => void;
  private unsubState: () => void;
  private unsubImage: () => void;
  private unsubDisconnect?: () => void;
  private states = new Map<string, unknown>();
  private images = new Map<string, { meta: ImageMeta; data: Uint8Array }>();
  private panelListeners = new Set<(action: PanelControlAction, dockId: string) => void>();
  private isClosed = false;

  constructor(
    client: BackendClientLike,
    channelOrName?: string | RelayChannelLike | { channel?: string | RelayChannelLike; channelName?: string },
  ) {
    this.client = client;
    const { channel, owned } = openRelayChannel(channelOrName);
    this.channel = channel;
    this.ownedChannel = owned;

    this.unsubState = this.client.onState("*", (value: unknown, key: string) => {
      if (key !== "event") {
        this.states.set(key, value);
      }
      // A new revision outdates every thumbnail, frames that no longer exist included.
      if (key === "thumbnails") {
        for (const imageKey of [...this.images.keys()]) {
          if (imageKey.startsWith("thumb_")) this.images.delete(imageKey);
        }
      }
      this.post({ t: "state", key, value });
    });

    this.unsubImage = this.client.onImage((meta: ImageMeta, data: Uint8Array) => {
      if (!meta?.key || (!meta.key.startsWith("cur_") && !meta.key.startsWith("thumb_"))) return;
      const copy = new Uint8Array(data);
      const cleanMeta: ImageMeta = {
        t: "image",
        key: meta.key,
        w: meta.w,
        h: meta.h,
        bytes: meta.bytes ?? copy.byteLength,
      };
      cacheRelayImage(this.images, cleanMeta, copy);
      this.post({ t: "image", meta: cleanMeta, data: copy });
    });

    if (typeof this.client.onDisconnect === "function") {
      this.unsubDisconnect = this.client.onDisconnect(() => {
        this.post({ t: "bye" });
      });
    }

    this.detachChannel = attachChannelListener(this.channel, (raw) => {
      this.handleMessage(raw);
    });

    if (typeof window !== "undefined" && typeof window.addEventListener === "function") {
      window.addEventListener("pagehide", this.handlePageHide);
    }

    this.post({ t: "host-ready" });
  }

  private handlePageHide = (): void => {
    this.post({ t: "bye" });
  };

  private post(msg: Record<string, unknown>): void {
    if (this.isClosed) return;
    try {
      this.channel.postMessage(msg);
    } catch {
      // 忽略已关闭通道的发送异常
    }
  }

  private handleMessage(raw: unknown): void {
    if (this.isClosed || !raw || typeof raw !== "object") return;
    const msg = raw as Record<string, unknown>;
    const t = msg.t;

    if (t === "hello") {
      const clientId = typeof msg.clientId === "string" ? msg.clientId : undefined;
      const states = Array.from(this.states.entries());
      const images = Array.from(this.images.values()).map((entry) => ({
        meta: entry.meta,
        data: new Uint8Array(entry.data),
      }));
      this.post({
        t: "welcome",
        ...(clientId ? { clientId } : {}),
        states,
        images,
      });
      return;
    }

    if (t === "call") {
      const id = Number(msg.id);
      const clientId = typeof msg.clientId === "string" ? msg.clientId : undefined;
      const method = String(msg.method ?? "");
      const params =
        msg.params && typeof msg.params === "object" && !Array.isArray(msg.params)
          ? (msg.params as Record<string, unknown>)
          : {};
      Promise.resolve()
        .then(() => this.client.call(method, params))
        .then(
          (result) => {
            this.post({
              t: "reply",
              ...(clientId ? { clientId } : {}),
              id,
              ok: true,
              result,
            });
          },
          (err) => {
            const error = err instanceof Error ? err.message : String(err ?? "调用失败");
            this.post({
              t: "reply",
              ...(clientId ? { clientId } : {}),
              id,
              ok: false,
              error,
            });
          },
        );
      return;
    }

    if (t === "restore" || t === "close") {
      const dockId = String(msg.dockId ?? "");
      if (dockId) {
        for (const fn of this.panelListeners) fn(t, dockId);
      }
      return;
    }

    if (t === "panel") {
      const action = msg.action;
      const dockId = String(msg.dockId ?? "");
      if ((action === "restore" || action === "close") && dockId) {
        for (const fn of this.panelListeners) fn(action, dockId);
      }
    }
  }

  onPanelAction(fn: (action: PanelControlAction, dockId: string) => void): () => void {
    this.panelListeners.add(fn);
    return () => {
      this.panelListeners.delete(fn);
    };
  }

  getChannel(): RelayChannelLike {
    return this.channel;
  }

  close(): void {
    if (this.isClosed) return;
    this.post({ t: "bye" });
    this.isClosed = true;
    this.unsubState();
    this.unsubImage();
    this.unsubDisconnect?.();
    this.detachChannel();
    if (typeof window !== "undefined" && typeof window.removeEventListener === "function") {
      window.removeEventListener("pagehide", this.handlePageHide);
    }
    if (this.ownedChannel) {
      try {
        this.channel.close?.();
      } catch {
        // 忽略关闭异常
      }
    }
  }
}

interface PendingRelayCall {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
}

interface QueuedRelayCall {
  id: number;
  method: string;
  params: Record<string, unknown>;
}

let nextClientSeq = 1;

function generateClientId(): string {
  const seq = nextClientSeq++;
  if (typeof crypto !== "undefined" && typeof crypto.randomUUID === "function") {
    return `sat_${crypto.randomUUID()}`;
  }
  return `sat_${Date.now().toString(36)}_${Math.random().toString(36).slice(2)}_${seq}`;
}

export class RelayClient {
  private channel: RelayChannelLike;
  private ownedChannel: boolean;
  private detachChannel: () => void;
  private clientId = generateClientId();
  private nextCallId = 1;
  private pendingCalls = new Map<number, PendingRelayCall>();
  private queuedCalls: QueuedRelayCall[] = [];
  private stateListeners = new Map<string, Set<(value: unknown, key: string) => void>>();
  private anyStateListeners = new Set<(key: string, value: unknown) => void>();
  private imageListeners = new Set<(meta: ImageMeta, data: Uint8Array) => void>();
  private disconnectListeners = new Set<(ev?: unknown) => void>();
  private states = new Map<string, unknown>();
  private images = new Map<string, { meta: ImageMeta; data: Uint8Array }>();
  private welcomed = false;
  private hostOnline = true;
  private isClosed = false;

  constructor(
    channelOrName?: string | RelayChannelLike | { channel?: string | RelayChannelLike; channelName?: string },
  ) {
    const { channel, owned } = openRelayChannel(channelOrName);
    this.channel = channel;
    this.ownedChannel = owned;

    this.detachChannel = attachChannelListener(this.channel, (raw) => {
      this.handleMessage(raw);
    });

    this.post({ t: "hello", clientId: this.clientId });
  }

  get connected(): boolean {
    return !this.isClosed && this.hostOnline;
  }

  connect(_url?: string): Promise<void> {
    if (this.isClosed) {
      return Promise.reject(new Error("RelayClient 已关闭"));
    }
    if (!this.welcomed) {
      this.post({ t: "hello", clientId: this.clientId });
    }
    return Promise.resolve();
  }

  close(): void {
    if (this.isClosed) return;
    this.isClosed = true;
    this.hostOnline = false;
    this.queuedCalls.length = 0;
    this.rejectAllPending(new Error("RelayClient 已关闭"));
    this.detachChannel();
    if (this.ownedChannel) {
      try {
        this.channel.close?.();
      } catch {
        // 忽略关闭异常
      }
    }
  }

  onDisconnect(fn: (ev?: unknown) => void): () => void {
    this.disconnectListeners.add(fn);
    return () => {
      this.disconnectListeners.delete(fn);
    };
  }

  call<T = unknown>(method: string, params: Record<string, unknown> = {}): Promise<T> {
    if (this.isClosed || !this.hostOnline) {
      return Promise.reject(new Error("主窗口未连接"));
    }
    const id = this.nextCallId++;
    return new Promise<T>((resolve, reject) => {
      this.pendingCalls.set(id, {
        resolve: resolve as (value: unknown) => void,
        reject,
      });
      if (!this.welcomed) {
        this.queuedCalls.push({ id, method, params });
        return;
      }
      this.sendCallMessage(id, method, params);
    });
  }

  private sendCallMessage(id: number, method: string, params: Record<string, unknown>): void {
    try {
      this.channel.postMessage({
        t: "call",
        clientId: this.clientId,
        id,
        method,
        params,
      });
    } catch (e) {
      const pending = this.pendingCalls.get(id);
      this.pendingCalls.delete(id);
      pending?.reject(e instanceof Error ? e : new Error(String(e)));
    }
  }

  sendPanelAction(action: PanelControlAction, dockId: string): void {
    this.post({ t: action, dockId });
  }

  sendInput(_msg: InputMessage): void {}

  sendAck(_view: string, _frame: number): void {}

  sendRaw(_msg: Record<string, unknown>): void {}

  onState(key: string, fn: (value: unknown, key: string) => void): () => void {
    if (key === "*") {
      const wrapper = (k: string, v: unknown) => fn(v, k);
      this.anyStateListeners.add(wrapper);
      for (const [k, v] of this.states) {
        fn(v, k);
      }
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
    if (this.states.has(key)) {
      fn(this.states.get(key), key);
    }
    return () => {
      set?.delete(fn);
    };
  }

  onBoard(_view: string, _fn: (msg: BoardMessage) => void): () => void {
    return () => {};
  }

  onImage(fn: (meta: ImageMeta, data: Uint8Array) => void): () => void {
    this.imageListeners.add(fn);
    for (const entry of this.images.values()) {
      fn(entry.meta, new Uint8Array(entry.data));
    }
    return () => {
      this.imageListeners.delete(fn);
    };
  }

  getChannel(): RelayChannelLike {
    return this.channel;
  }

  private post(msg: Record<string, unknown>): void {
    if (this.isClosed) return;
    try {
      this.channel.postMessage(msg);
    } catch {
      // 忽略发送异常
    }
  }

  private emitState(key: string, value: unknown): void {
    if (key !== "event") {
      this.states.set(key, value);
    }
    const listeners = this.stateListeners.get(key);
    if (listeners) {
      for (const fn of listeners) fn(value, key);
    }
    for (const fn of this.anyStateListeners) {
      fn(key, value);
    }
  }

  private emitImage(meta: ImageMeta, data: Uint8Array): void {
    if (!meta?.key || (!meta.key.startsWith("cur_") && !meta.key.startsWith("thumb_"))) return;
    const copy = new Uint8Array(data);
    cacheRelayImage(this.images, meta, copy);
    for (const fn of this.imageListeners) {
      fn(meta, new Uint8Array(copy));
    }
  }

  private rejectAllPending(err: Error): void {
    for (const [, pending] of this.pendingCalls) {
      pending.reject(err);
    }
    this.pendingCalls.clear();
  }

  private handleMessage(raw: unknown): void {
    if (this.isClosed || !raw || typeof raw !== "object") return;
    const msg = raw as Record<string, unknown>;
    const t = msg.t;

    if (t === "host-ready") {
      if (!this.welcomed) {
        this.post({ t: "hello", clientId: this.clientId });
      }
      return;
    }

    if (t === "welcome") {
      if (typeof msg.clientId === "string" && msg.clientId !== this.clientId) return;
      this.hostOnline = true;
      const firstWelcome = !this.welcomed;
      this.welcomed = true;
      if (firstWelcome) {
        if (Array.isArray(msg.states)) {
          for (const pair of msg.states) {
            if (Array.isArray(pair) && pair.length >= 2) {
              this.emitState(String(pair[0]), pair[1]);
            }
          }
        } else if (msg.states && typeof msg.states === "object") {
          for (const [k, v] of Object.entries(msg.states as Record<string, unknown>)) {
            this.emitState(k, v);
          }
        }

        if (Array.isArray(msg.images)) {
          for (const item of msg.images) {
            if (item && typeof item === "object") {
              const entry = item as { meta?: ImageMeta; data?: unknown };
              if (entry.meta) {
                this.emitImage(entry.meta, toUint8Array(entry.data));
              }
            }
          }
        }
      }
      const queued = this.queuedCalls.splice(0);
      for (const item of queued) {
        if (this.pendingCalls.has(item.id)) {
          this.sendCallMessage(item.id, item.method, item.params);
        }
      }
      return;
    }

    if (t === "state") {
      this.hostOnline = true;
      this.emitState(String(msg.key ?? ""), msg.value);
      return;
    }

    if (t === "image") {
      this.hostOnline = true;
      const meta = msg.meta as ImageMeta | undefined;
      if (meta) {
        this.emitImage(meta, toUint8Array(msg.data));
      }
      return;
    }

    if (t === "reply") {
      if (typeof msg.clientId === "string" && msg.clientId !== this.clientId) return;
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

    if (t === "bye") {
      this.hostOnline = false;
      this.welcomed = false;
      this.queuedCalls.length = 0;
      const err = new Error("主窗口已断开");
      this.rejectAllPending(err);
      for (const fn of this.disconnectListeners) {
        fn(msg);
      }
    }
  }
}
