import assert from "node:assert/strict";
import test from "node:test";
import type { ImageMeta } from "../src/net.ts";
import { cacheRelayImage, RelayClient, RelayHost, type RelayChannelLike } from "../src/relay.ts";


class FakeChannelEndpoint implements RelayChannelLike {
  peer: FakeChannelEndpoint | null = null;
  bus: FakeBroadcastBus | null = null;
  closed = false;
  onmessage: ((ev: { data: unknown }) => void) | null = null;
  private listeners = new Set<(ev: { data: unknown }) => void>();

  postMessage(message: unknown): void {
    if (this.closed) throw new Error("Channel is closed");
    if (this.bus) {
      this.bus.broadcast(this, message);
      return;
    }
    const target = this.peer;
    if (!target || target.closed) return;
    target.deliver(message);
  }

  deliver(message: unknown): void {
    if (this.closed) return;
    const cloned = structuredClone(message);
    const ev = { data: cloned };
    this.onmessage?.(ev);
    for (const fn of [...this.listeners]) {
      fn(ev);
    }
  }

  addEventListener(type: string, listener: (ev: unknown) => void): void {
    if (type === "message") {
      this.listeners.add(listener as (ev: { data: unknown }) => void);
    }
  }

  removeEventListener(type: string, listener: (ev: unknown) => void): void {
    if (type === "message") {
      this.listeners.delete(listener as (ev: { data: unknown }) => void);
    }
  }

  close(): void {
    this.closed = true;
    this.bus?.remove(this);
  }
}

class FakeBroadcastBus {
  private endpoints = new Set<FakeChannelEndpoint>();

  createEndpoint(): FakeChannelEndpoint {
    const ep = new FakeChannelEndpoint();
    ep.bus = this;
    this.endpoints.add(ep);
    return ep;
  }

  remove(ep: FakeChannelEndpoint): void {
    this.endpoints.delete(ep);
  }

  broadcast(sender: FakeChannelEndpoint, message: unknown): void {
    for (const ep of [...this.endpoints]) {
      if (ep !== sender && !ep.closed) {
        ep.deliver(message);
      }
    }
  }
}

function createFakeChannelPair(): [FakeChannelEndpoint, FakeChannelEndpoint] {
  const a = new FakeChannelEndpoint();
  const b = new FakeChannelEndpoint();
  a.peer = b;
  b.peer = a;
  return [a, b];
}

function createMockBackendClient() {
  const anyStateListeners = new Set<(value: unknown, key: string) => void>();
  const keyStateListeners = new Map<string, Set<(value: unknown, key: string) => void>>();
  const imageListeners = new Set<(meta: ImageMeta, data: Uint8Array) => void>();
  const disconnectListeners = new Set<(ev?: unknown) => void>();
  let callHandler: (method: string, params?: Record<string, unknown>) => Promise<unknown> = async (method, params) => ({
    method,
    params,
  });

  return {
    onState(key: string, fn: (value: unknown, key: string) => void) {
      if (key === "*") {
        anyStateListeners.add(fn);
        return () => anyStateListeners.delete(fn);
      }
      let set = keyStateListeners.get(key);
      if (!set) {
        set = new Set();
        keyStateListeners.set(key, set);
      }
      set.add(fn);
      return () => set?.delete(fn);
    },
    onImage(fn: (meta: ImageMeta, data: Uint8Array) => void) {
      imageListeners.add(fn);
      return () => imageListeners.delete(fn);
    },
    onDisconnect(fn: (ev?: unknown) => void) {
      disconnectListeners.add(fn);
      return () => disconnectListeners.delete(fn);
    },
    call<T = unknown>(method: string, params?: Record<string, unknown>): Promise<T> {
      return callHandler(method, params) as Promise<T>;
    },
    setCallHandler(fn: (method: string, params?: Record<string, unknown>) => Promise<unknown>) {
      callHandler = fn;
    },
    emitState(key: string, value: unknown) {
      const set = keyStateListeners.get(key);
      if (set) {
        for (const fn of set) fn(value, key);
      }
      for (const fn of anyStateListeners) fn(value, key);
    },
    emitImage(meta: ImageMeta, data: Uint8Array) {
      for (const fn of imageListeners) fn(meta, data);
    },
    emitDisconnect(ev?: unknown) {
      for (const fn of disconnectListeners) fn(ev);
    },
  };
}

class FakeElement {
  tagName: string;
  className = "";
  id = "";
  textContent = "";
  title = "";
  type = "";
  value = "";
  checked = false;
  hidden = false;
  inert = false;
  tabIndex = 0;
  clientWidth = 300;
  clientHeight = 220;
  dataset: Record<string, string> = {};
  style: Record<string, string> = {};
  children: FakeElement[] = [];
  parentElement: FakeElement | null = null;
  private listeners = new Map<string, Array<{ fn: (ev: any) => void; capture: boolean }>>();

  constructor(tag = "div") {
    this.tagName = tag.toUpperCase();
  }

  get firstChild(): FakeElement | null {
    return this.children[0] ?? null;
  }

  get childElementCount(): number {
    return this.children.length;
  }

  set innerHTML(html: string) {
    this.children = [];
    if (html.includes("sb-handle")) {
      const a = new FakeElement("div");
      a.className = "sb-btn sb-a";
      const b = new FakeElement("div");
      b.className = "sb-btn sb-b";
      const h = new FakeElement("div");
      h.className = "sb-handle";
      this.append(a, b, h);
    }
  }

  classList = {
    add: (...names: string[]) => {
      const set = new Set(this.className.split(/\s+/).filter(Boolean));
      for (const n of names) set.add(n);
      this.className = [...set].join(" ");
    },
    remove: (...names: string[]) => {
      const set = new Set(this.className.split(/\s+/).filter(Boolean));
      for (const n of names) set.delete(n);
      this.className = [...set].join(" ");
    },
    toggle: (name: string, force?: boolean) => {
      const set = new Set(this.className.split(/\s+/).filter(Boolean));
      const on = force ?? !set.has(name);
      if (on) set.add(name);
      else set.delete(name);
      this.className = [...set].join(" ");
      return on;
    },
    contains: (name: string) => this.className.split(/\s+/).filter(Boolean).includes(name),
  };

  append(...nodes: Array<FakeElement | string>) {
    for (const node of nodes) {
      if (!node) continue;
      const el = typeof node === "string" ? new FakeElement("#text") : node;
      if (typeof node === "string") el.textContent = node;
      if (el.parentElement) {
        const idx = el.parentElement.children.indexOf(el);
        if (idx >= 0) el.parentElement.children.splice(idx, 1);
      }
      el.parentElement = this;
      this.children.push(el);
    }
  }

  replaceChildren(...nodes: Array<FakeElement | string>) {
    for (const c of this.children) c.parentElement = null;
    this.children = [];
    this.append(...nodes);
  }

  replaceWith(node: FakeElement) {
    const siblings = this.parentElement?.children;
    if (siblings) siblings[siblings.indexOf(this)] = node;
    node.parentElement = this.parentElement;
    this.parentElement = null;
  }

  remove() {
    if (!this.parentElement) return;
    const idx = this.parentElement.children.indexOf(this);
    if (idx >= 0) this.parentElement.children.splice(idx, 1);
    this.parentElement = null;
  }

  contains(other: FakeElement | null): boolean {
    for (let cur: FakeElement | null = other; cur; cur = cur.parentElement) {
      if (cur === this) return true;
    }
    return false;
  }

  matches(selector: string): boolean {
    return selector
      .split(",")
      .map((s) => s.trim())
      .filter(Boolean)
      .some((part) => {
        if (part === ":active") return false;
        const scoped = part.startsWith(":scope > ") ? part.slice(":scope > ".length).trim() : part;
        if (scoped.startsWith(".")) {
          const classes = scoped.slice(1).split(".");
          return classes.every((c) => this.classList.contains(c));
        }
        if (scoped.startsWith("#")) return this.id === scoped.slice(1);
        if (scoped === '[data-vf-id="toolsDock"]') return this.dataset.vfId === "toolsDock";
        if (scoped === '[data-vf-id="toolsScroll"]') return this.dataset.vfId === "toolsScroll";
        return this.tagName.toLowerCase() === scoped.toLowerCase();
      });
  }

  closest(selector: string): FakeElement | null {
    for (let cur: FakeElement | null = this; cur; cur = cur.parentElement) {
      if (cur.matches(selector)) return cur;
    }
    return null;
  }

  querySelectorAll(selector: string): FakeElement[] {
    if (selector.startsWith(":scope > ")) {
      const rest = selector.slice(":scope > ".length).trim();
      return this.children.filter((c) => c.matches(rest));
    }
    const out: FakeElement[] = [];
    const visit = (node: FakeElement) => {
      for (const child of node.children) {
        if (child.matches(selector)) out.push(child);
        visit(child);
      }
    };
    visit(this);
    return out;
  }

  querySelector(selector: string): FakeElement | null {
    return this.querySelectorAll(selector)[0] ?? null;
  }

  addEventListener(type: string, fn: (ev: any) => void, options?: boolean | { capture?: boolean }) {
    const capture = typeof options === "boolean" ? options : Boolean(options?.capture);
    const list = this.listeners.get(type) ?? [];
    list.push({ fn, capture });
    this.listeners.set(type, list);
  }

  removeEventListener(type: string, fn: (ev: any) => void, options?: boolean | { capture?: boolean }) {
    const capture = typeof options === "boolean" ? options : Boolean(options?.capture);
    const list = this.listeners.get(type) ?? [];
    this.listeners.set(
      type,
      list.filter((item) => item.fn !== fn || item.capture !== capture),
    );
  }

  dispatchEvent(ev: Record<string, any>) {
    const path: FakeElement[] = [];
    for (let cur: FakeElement | null = this; cur; cur = cur.parentElement) {
      path.push(cur);
    }
    let stopped = false;
    let immediateStopped = false;
    const eventObj = {
      ...ev,
      target: ev.target ?? this,
      stopPropagation() {
        stopped = true;
      },
      stopImmediatePropagation() {
        stopped = true;
        immediateStopped = true;
      },
      preventDefault() {},
    };
    for (let i = path.length - 1; i >= 0 && !stopped; i--) {
      const list = path[i].listeners.get(ev.type) ?? [];
      for (const item of [...list]) {
        if (!item.capture) continue;
        item.fn(eventObj);
        if (immediateStopped) break;
      }
    }
    for (let i = 0; i < path.length && !stopped; i++) {
      const list = path[i].listeners.get(ev.type) ?? [];
      for (const item of [...list]) {
        if (item.capture) continue;
        item.fn(eventObj);
        if (immediateStopped) break;
      }
    }
    return !stopped;
  }

  getBoundingClientRect() {
    const left = Number.parseFloat(this.style.left ?? "0") || 0;
    const top = Number.parseFloat(this.style.top ?? "0") || 0;
    const width = Number.parseFloat(this.style.width ?? "") || this.clientWidth;
    const height = Number.parseFloat(this.style.height ?? "") || this.clientHeight;
    return { left, top, right: left + width, bottom: top + height, width, height };
  }

  getContext() {
    return {
      font: "",
      measureText: (t: string) => ({ width: t.length * 6 }),
      putImageData() {},
      clearRect() {},
      fillRect() {},
    };
  }

  toDataURL() {
    return "data:image/png;base64,AAA=";
  }
}

async function withFakeDom<T>(fn: (env: { body: FakeElement; win: any }) => Promise<T> | T): Promise<T> {
  const prevDoc = (globalThis as any).document;
  const prevWin = (globalThis as any).window;
  const prevGetComputedStyle = (globalThis as any).getComputedStyle;
  const prevRO = (globalThis as any).ResizeObserver;
  const prevMO = (globalThis as any).MutationObserver;
  const prevHTMLInput = (globalThis as any).HTMLInputElement;
  const prevHTMLTextArea = (globalThis as any).HTMLTextAreaElement;

  const docEl = new FakeElement("html");
  const body = new FakeElement("body");
  docEl.append(body);
  const winListeners = new Map<string, Array<(ev: any) => void>>();
  const win: any = {
    screenX: 120,
    screenY: 80,
    addEventListener(type: string, cb: (ev: any) => void) {
      const list = winListeners.get(type) ?? [];
      list.push(cb);
      winListeners.set(type, list);
    },
    removeEventListener(type: string, cb: (ev: any) => void) {
      const list = winListeners.get(type) ?? [];
      winListeners.set(
        type,
        list.filter((x) => x !== cb),
      );
    },
    dispatchEvent(ev: { type: string; [k: string]: any }) {
      for (const cb of [...(winListeners.get(ev.type) ?? [])]) cb(ev);
    },
  };

  (globalThis as any).document = {
    documentElement: docEl,
    body,
    activeElement: null,
    createElement: (tag: string) => new FakeElement(tag),
    createElementNS: (_ns: string, tag: string) => new FakeElement(tag),
    getElementById: (id: string) => (id === "app" ? body : body.querySelector(`#${id}`)),
    querySelectorAll: (sel: string) => body.querySelectorAll(sel),
    querySelector: (sel: string) => body.querySelector(sel),
  };
  (globalThis as any).window = win;
  (globalThis as any).getComputedStyle = () => ({ getPropertyValue: () => "#000" });
  (globalThis as any).ResizeObserver = class {
    observe() {}
    disconnect() {}
  };
  (globalThis as any).MutationObserver = class {
    cb: () => void;
    constructor(cb: () => void) {
      this.cb = cb;
    }
    observe(target: FakeElement) {
      const origAppend = target.append.bind(target);
      const origReplace = target.replaceChildren.bind(target);
      target.append = (...nodes: any[]) => {
        origAppend(...nodes);
        this.cb();
      };
      target.replaceChildren = (...nodes: any[]) => {
        origReplace(...nodes);
        this.cb();
      };
    }
    disconnect() {}
  };
  (globalThis as any).HTMLInputElement = class {};
  (globalThis as any).HTMLTextAreaElement = class {};

  try {
    return await fn({ body, win });
  } finally {
    (globalThis as any).document = prevDoc;
    (globalThis as any).window = prevWin;
    (globalThis as any).getComputedStyle = prevGetComputedStyle;
    (globalThis as any).ResizeObserver = prevRO;
    (globalThis as any).MutationObserver = prevMO;
    (globalThis as any).HTMLInputElement = prevHTMLInput;
    (globalThis as any).HTMLTextAreaElement = prevHTMLTextArea;
  }
}

test("RelayClient clientId 唯一性：同一假总线上两个 RelayClient 调用 id 相同，回复不串", async () => {
  const bus = new FakeBroadcastBus();
  const backend = createMockBackendClient();
  const host = new RelayHost(backend, bus.createEndpoint());

  const clientA = new RelayClient(bus.createEndpoint());
  const clientB = new RelayClient(bus.createEndpoint());
  await Promise.all([clientA.connect(), clientB.connect()]);

  let resolveA!: (v: unknown) => void;
  let resolveB!: (v: unknown) => void;
  backend.setCallHandler((_method, params) => {
    return new Promise((resolve) => {
      if (params?.caller === "A") resolveA = resolve;
      else resolveB = resolve;
    });
  });

  // 两个客户端各自从 id = 1 开始发起调用
  const promiseA = clientA.call("layer.select", { caller: "A" });
  const promiseB = clientB.call("layer.select", { caller: "B" });
  await Promise.resolve();

  // 先回复 B 再回复 A，验证不会因为 id 都是 1 而串掉
  resolveB({ from: "B", ok: true });
  resolveA({ from: "A", ok: true });

  const [resA, resB] = await Promise.all([promiseA, promiseB]);
  assert.deepEqual(resA, { from: "A", ok: true });
  assert.deepEqual(resB, { from: "B", ok: true });

  clientA.close();
  clientB.close();
  host.close();
});

test("RelayHost 与 RelayClient：状态快照、增量同步、一次性 event 只转发不缓存、thumb_ 按版本淘汰与补发", async () => {
  const bus = new FakeBroadcastBus();
  const backend = createMockBackendClient();
  const host = new RelayHost(backend, bus.createEndpoint());

  // 卫星页启动前后端已经发出的状态、一次性 event 和多版本缩略图
  backend.emitState("theme", { mode: "dark" });
  backend.emitState("layers/main", { rows: [{ name: "L1" }], selected: 0 });
  backend.emitState("event", { name: "dialog.openProject", args: {} });
  backend.emitImage(
    { t: "image", key: "cur_pen", w: 2, h: 2, bytes: 16 },
    new Uint8Array([1, 2, 3, 4]),
  );
  backend.emitImage(
    { t: "image", key: "thumb_main_0_120x90_1", w: 4, h: 3, bytes: 48 },
    new Uint8Array([1, 1, 1, 1]),
  );
  backend.emitImage(
    { t: "image", key: "thumb_main_0_120x90_2", w: 4, h: 3, bytes: 48 },
    new Uint8Array([2, 2, 2, 2]),
  );
  backend.emitImage(
    { t: "image", key: "thumb_main_1_120x90_1", w: 4, h: 3, bytes: 48 },
    new Uint8Array([3, 3, 3, 3]),
  );
  // 非 cur_ / thumb_ 前缀的图片不应被转发或缓存
  backend.emitImage(
    { t: "image", key: "raw_board", w: 10, h: 10, bytes: 400 },
    new Uint8Array([99]),
  );

  const client = new RelayClient(bus.createEndpoint());
  const receivedStates = new Map<string, unknown>();
  const receivedEvents: unknown[] = [];
  const wildcardStates: Array<[string, unknown]> = [];
  const receivedImages: Array<{ meta: ImageMeta; bytes: number[] }> = [];

  client.onState("theme", (v) => receivedStates.set("theme", v));
  client.onState("layers/main", (v) => receivedStates.set("layers/main", v));
  client.onState("event", (v) => receivedEvents.push(v));
  client.onState("*", (v, k) => wildcardStates.push([k, v]));
  client.onImage((meta, data) => {
    receivedImages.push({ meta, bytes: [...data] });
  });
  await client.connect();

  // welcome 快照只包含常规状态，不应重放启动前的旧 event
  assert.deepEqual(receivedStates.get("theme"), { mode: "dark" });
  assert.deepEqual(receivedStates.get("layers/main"), { rows: [{ name: "L1" }], selected: 0 });
  assert.equal(receivedEvents.length, 0);
  assert.equal(wildcardStates.length, 2);

  // thumb_main_0_120x90_1 已被新版本 _2 顶替，welcome 只下发 cur_pen、thumb_main_0_120x90_2、thumb_main_1_120x90_1
  assert.deepEqual(
    receivedImages.map((img) => img.meta.key),
    ["cur_pen", "thumb_main_0_120x90_2", "thumb_main_1_120x90_1"],
  );
  assert.deepEqual(receivedImages[1].bytes, [2, 2, 2, 2]);

  // 连接后在线收到的 event 正常转发，但不会存入客户端状态缓存（新订阅 onState("event") 不会补发）
  backend.emitState("event", { name: "contextMenu", args: { x: 10, y: 20 } });
  assert.equal(receivedEvents.length, 1);
  assert.deepEqual(receivedEvents[0], { name: "contextMenu", args: { x: 10, y: 20 } });
  let lateEventReplayed = 0;
  client.onState("event", () => {
    lateEventReplayed++;
  });
  assert.equal(lateEventReplayed, 0);

  // 再开第二个卫星页时，之前的在线 event 也不会在 welcome 中被重放
  const client2 = new RelayClient(bus.createEndpoint());
  const client2Events: unknown[] = [];
  client2.onState("event", (v) => client2Events.push(v));
  await client2.connect();
  assert.equal(client2Events.length, 0);

  // 广播更新版本的缩略图 thumb_main_0_160x120_3，验证 RelayClient.images 淘汰旧版本并保留 onImage 补发逻辑
  backend.emitImage(
    { t: "image", key: "thumb_main_0_160x120_3", w: 5, h: 4, bytes: 80 },
    new Uint8Array([9, 9, 9, 9]),
  );
  const replayedOnLateSubscribe: string[] = [];
  client.onImage((meta) => {
    replayedOnLateSubscribe.push(meta.key);
  });
  assert.deepEqual(replayedOnLateSubscribe, [
    "cur_pen",
    "thumb_main_1_120x90_1",
    "thumb_main_0_160x120_3",
  ]);

  // 直接验证共用函数 cacheRelayImage 对同 view + frame 旧 revision 的淘汰
  const map = new Map<string, { meta: ImageMeta; data: Uint8Array }>();
  cacheRelayImage(map, { t: "image", key: "thumb_child_2_100x80_5", w: 1, h: 1, bytes: 4 }, new Uint8Array([5]));
  cacheRelayImage(map, { t: "image", key: "thumb_child_2_100x80_6", w: 1, h: 1, bytes: 4 }, new Uint8Array([6]));
  assert.deepEqual([...map.keys()], ["thumb_child_2_100x80_6"]);

  // 缩略图换了版本：之前的全部作废，删掉的帧的图也不留，新开的窗口不再收到
  backend.emitState("thumbnails", { revision: 9 });
  const client3 = new RelayClient(bus.createEndpoint());
  const welcomed: string[] = [];
  client3.onImage((meta) => welcomed.push(meta.key));
  await client3.connect();
  assert.deepEqual(welcomed, ["cur_pen"]);

  client3.close();
  client2.close();
  client.close();
  host.close();
});

test("RelayClient 调用排队与生命周期：welcome 前排队按序发出、主窗口忙时不超时、收到 bye 全部 reject 且后续立即 reject", async () => {
  const bus = new FakeBroadcastBus();
  const backend = createMockBackendClient();

  // 卫星页先创建并发起两个调用，此时主窗口 RelayHost 尚未就绪（尚未收到 welcome）
  const client = new RelayClient(bus.createEndpoint());
  const receivedOrder: string[] = [];
  let releaseSlowCall: (() => void) | null = null;

  backend.setCallHandler((method, params) => {
    receivedOrder.push(`${method}:${params?.step ?? ""}`);
    if (method === "first.op") {
      return Promise.resolve({ step: 1 });
    }
    if (method === "second.op") {
      return Promise.resolve({ step: 2 });
    }
    if (method === "project.open") {
      return Promise.reject(new Error("文件损坏无法打开"));
    }
    if (method === "slow.op") {
      return new Promise((resolve) => {
        releaseSlowCall = () => resolve({ done: true });
      });
    }
    return Promise.resolve({ ok: true });
  });

  const p1 = client.call("first.op", { step: "a" });
  const p2 = client.call("second.op", { step: "b" });

  // 等待超过原先 50ms 判活时间：welcome 前不应判超时，且尚未到达主窗口
  await new Promise((r) => setTimeout(r, 70));
  assert.equal(receivedOrder.length, 0);

  // 主窗口上线并回应 welcome
  const host = new RelayHost(backend, bus.createEndpoint());

  const [r1, r2] = await Promise.all([p1, p2]);
  assert.deepEqual(receivedOrder, ["first.op:a", "second.op:b"]);
  assert.deepEqual(r1, { step: 1 });
  assert.deepEqual(r2, { step: 2 });

  // 错误传递正常
  await assert.rejects(
    () => client.call("project.open", { path: "bad.anproj" }),
    /文件损坏无法打开/,
  );

  // 挂起中的 call 在主窗口说再见（bye）时立即 reject
  let disconnected = 0;
  client.onDisconnect(() => {
    disconnected++;
  });
  const pendingPromise = client.call("slow.op", { step: "slow" });
  await Promise.resolve();
  assert.ok(releaseSlowCall !== null);
  host.close();

  await assert.rejects(() => pendingPromise, /主窗口已断开/);
  assert.equal(disconnected, 1);

  // bye 之后再次调用立即 reject，直到再次收到 welcome
  await assert.rejects(() => client.call("first.op", { step: "after_bye" }), /主窗口/);

  const host2 = new RelayHost(backend, bus.createEndpoint());
  const rAfterReconnect = await client.call("first.op", { step: "reconnected" });
  assert.deepEqual(rAfterReconnect, { step: 1 });

  client.close();
  host2.close();
});

test("panelwindows 与 syncDocks：WINDOW_FOR_DOCK 反推、缺省窗口视为可见、标题栏 mousedown 单击拖动与双击还原", async () => {
  await withFakeDom(async ({ win }) => {
    const app = await import("../src/ui/app.js");
    const { createPanelWindows, setupSatelliteDock, WINDOW_FOR_DOCK } = await import("../src/ui/panelwindows.js");

    // WINDOW_FOR_DOCK 由 DOCK_FOR_WINDOW 反推再加 timelineDock
    const winForDock = WINDOW_FOR_DOCK as Record<string, string>;
    for (const [winName, dockId] of Object.entries(app.DOCK_FOR_WINDOW)) {
      assert.equal(winForDock[dockId], winName);
    }
    assert.equal(winForDock.timelineDock, "timeline");

    const tauriCalls: Array<{ cmd: string; args: Record<string, any> }> = [];
    const tauriListeners = new Map<string, (event: { payload: unknown }) => void>();
    let dragStarted = 0;
    win.__TAURI__ = {
      core: {
        invoke(cmd: string, args: Record<string, any>) {
          tauriCalls.push({ cmd, args });
          // 外壳关窗时回报窗口最后的位置，再打开时回到那里
          return Promise.resolve(cmd === "close_panel_window" ? { x: 900, y: 40, width: 300, height: 420 } : undefined);
        },
      },
      event: {
        listen(name: string, fn: (event: { payload: unknown }) => void) {
          tauriListeners.set(name, fn);
          return Promise.resolve(() => tauriListeners.delete(name));
        },
      },
      window: {
        getCurrentWindow() {
          return {
            startDragging() {
              dragStarted++;
            },
          };
        },
      },
    };

    const visibleMap = new Map<string, boolean>([
      ["assetsDock", true],
      ["layersDock", true],
      ["timelineDock", false],
    ]);
    const dockSetVisibleCalls: string[] = [];
    const floatingList: Array<{ dockId: string; x: number; y: number; width: number; height: number }> = [];
    const runtime: any = {
      project: {
        scene: {
          nodes: {
            assetsDock: { type: "Dock", minWidth: 110, minHeight: 70 },
            layersDock: { type: "Dock" },
            timelineDock: { type: "Dock" },
          },
        },
      },
      getDockLayout: () => ({
        areas: { top: { groups: [] }, bottom: { groups: [] }, left: { groups: [] }, right: { groups: [] } },
        floating: [...floatingList],
      }),
      dockSetVisible(dockId: string, vis: boolean) {
        dockSetVisibleCalls.push(dockId);
        visibleMap.set(dockId, vis);
      },
      dockRestore(dockId: string) {
        const idx = floatingList.findIndex((f) => f.dockId === dockId);
        if (idx >= 0) floatingList.splice(idx, 1);
      },
      view: (dockId: string) => ({ visible: visibleMap.get(dockId) ?? false, enabled: true, props: {} }),
    };

    const listeners = new Map<string, Array<(v: any) => void>>();
    // 窗口表里故意不写 assets，验证没有这个名字就当可见
    const stateValues = new Map<string, any>([
      ["windows", { layers: { visible: true }, timeline: { visible: false } }],
    ]);
    const storeCalls: Array<{ method: string; params: Record<string, any> }> = [];
    const store: any = {
      get: (k: string) => stateValues.get(k),
      on(k: string, fn: (v: any) => void) {
        const arr = listeners.get(k) ?? [];
        arr.push(fn);
        listeners.set(k, arr);
        if (stateValues.has(k)) fn(stateValues.get(k));
      },
      set(k: string, val: any) {
        stateValues.set(k, val);
        for (const fn of listeners.get(k) ?? []) fn(val);
      },
      call(method: string, params: Record<string, any> = {}) {
        storeCalls.push({ method, params });
        return Promise.resolve({});
      },
    };

    const [hostCh, satCh] = createFakeChannelPair();
    const backend = createMockBackendClient();
    const host = new RelayHost(backend, hostCh);
    const satClient = new RelayClient(satCh);

    const externalChangedLog: string[] = [];
    const panelWins = createPanelWindows(runtime, store, {
      host,
      onExternalChanged: (dockId: string) => externalChangedLog.push(dockId),
    });
    dockSetVisibleCalls.length = 0;
    app.syncDocks(runtime, store);

    // syncDocks 没有重复循环：每个停靠窗只调用一次 dockSetVisible
    assert.equal(
      dockSetVisibleCalls.filter((id) => id === "toolsDock").length,
      1,
    );
    assert.equal(
      dockSetVisibleCalls.filter((id) => id === "toolOptionsDock").length,
      1,
    );
    assert.equal(
      dockSetVisibleCalls.filter((id) => id === "layersDock").length,
      1,
    );

    // 1. 拖动中途（commit=false）不开窗口
    floatingList.push({ dockId: "assetsDock", x: 30, y: 40, width: 250, height: 300 });
    panelWins.onDockChanged("assetsDock", false);
    assert.equal(tauriCalls.length, 0);

    // 2. 松手浮动（commit=true）：打开 panel-assetsDock（即使 windows 表里没写 assets 也当可见），触发 onExternalChanged
    panelWins.onDockChanged("assetsDock", true);
    assert.equal(tauriCalls.length, 1);
    assert.deepEqual(tauriCalls[0], {
      cmd: "open_panel_window",
      args: {
        label: "panel-assetsDock",
        url: "index.html?only=assetsDock&satellite=1",
        x: 120 + 30,
        y: 80 + 40,
        width: 250,
        height: 300,
        minWidth: 110,
        minHeight: 70,
      },
    });
    assert.equal(visibleMap.get("assetsDock"), false);
    assert.deepEqual(externalChangedLog, ["assetsDock"]);

    // 3. syncDocks 收到后端 windows 更新时跳过外出的 assetsDock，不把它显示回主页
    store.set("windows", { assets: { visible: true }, layers: { visible: true }, timeline: { visible: false } });
    assert.equal(visibleMap.get("assetsDock"), false);

    // 4. 后端 windows 把 assets 设为不可见时关掉独立窗口；重新可见时再次打开
    store.set("windows", { assets: { visible: false }, layers: { visible: true }, timeline: { visible: false } });
    assert.equal(tauriCalls.at(-1)?.cmd, "close_panel_window");
    assert.equal(tauriCalls.at(-1)?.args.label, "panel-assetsDock");
    // 主窗口主动关闭时外壳也会报 panel-closed，不应再触发 menu.trigger
    tauriListeners.get("panel-closed")?.({ payload: "panel-assetsDock" });
    assert.equal(storeCalls.length, 0);
    await new Promise((r) => setTimeout(r, 0));

    store.set("windows", { assets: { visible: true }, layers: { visible: true }, timeline: { visible: false } });
    assert.equal(tauriCalls.at(-1)?.cmd, "open_panel_window");
    assert.equal(tauriCalls.at(-1)?.args.label, "panel-assetsDock");
    assert.deepEqual(
      [tauriCalls.at(-1)?.args.x, tauriCalls.at(-1)?.args.y, tauriCalls.at(-1)?.args.width, tauriCalls.at(-1)?.args.height],
      [900, 40, 300, 420],
    );
    assert.equal(visibleMap.get("assetsDock"), false);

    // 5. 卫星页标题栏拖动与双击还原：
    //    - pointerdown 捕获阶段只拦截不调 startDragging
    //    - mousedown detail=1 调 startDragging
    //    - mousedown detail>=2 调 onDockFloatRequest（发 restore）且不调 startDragging
    const satRoot = new FakeElement("div");
    const satHeader = new FakeElement("div");
    satHeader.className = "vf-dock-header";
    const satTitle = new FakeElement("span");
    satTitle.className = "dock-title";
    const satBtn = new FakeElement("button");
    satBtn.className = "dock-btn dock-btn-float";
    satHeader.append(satTitle, satBtn);
    satRoot.append(satHeader);

    const satHooks = setupSatelliteDock(satRoot, "assetsDock", satClient);
    // 独立窗口里的面板总是浮动的，按钮要显示恢复停靠
    assert.equal(satBtn.textContent, "↙");
    assert.equal(satBtn.dataset.dockAction, "restore");
    let innerPointerDownReached = 0;
    satHeader.addEventListener("pointerdown", () => {
      innerPointerDownReached++;
    });

    // pointerdown 只拦截不触发拖动
    satTitle.dispatchEvent({ type: "pointerdown", button: 0, detail: 0 });
    assert.equal(dragStarted, 0);
    assert.equal(innerPointerDownReached, 0);

    // mousedown 单击触发 startDragging
    satTitle.dispatchEvent({ type: "mousedown", button: 0, detail: 1 });
    assert.equal(dragStarted, 1);

    // 点击按钮或右键不触发 startDragging 或 restore
    satBtn.dispatchEvent({ type: "mousedown", button: 0, detail: 1 });
    satBtn.dispatchEvent({ type: "mousedown", button: 0, detail: 2 });
    satTitle.dispatchEvent({ type: "mousedown", button: 2, detail: 1 });
    assert.equal(dragStarted, 1);
    assert.equal(floatingList.length, 1);

    // mousedown 双击（detail: 2）触发 onDockFloatRequest -> 主窗口还原面板并通知 onExternalChanged
    satTitle.dispatchEvent({ type: "mousedown", button: 0, detail: 2 });
    assert.equal(dragStarted, 1);
    assert.equal(floatingList.length, 0);
    assert.equal(visibleMap.get("assetsDock"), true);
    assert.equal(tauriCalls.at(-1)?.cmd, "close_panel_window");
    assert.deepEqual(externalChangedLog, ["assetsDock", "assetsDock"]);

    // 6. 再次浮出并由用户按 Alt+F4（外壳报 panel-closed）：主窗口走 onDockClosed 触发 windows.assets
    floatingList.push({ dockId: "assetsDock", x: 50, y: 60, width: 240, height: 280 });
    panelWins.onDockChanged("assetsDock", true);
    assert.equal(tauriCalls.at(-1)?.cmd, "open_panel_window");

    // 主窗口关闭时 Windows 先带走独立窗口，页面会 pagehide，这不算用户关的
    const callsBefore = storeCalls.length;
    win.dispatchEvent({ type: "pagehide" });
    assert.equal(storeCalls.length, callsBefore);

    tauriListeners.get("panel-closed")?.({ payload: "panel-assetsDock" });
    assert.deepEqual(storeCalls.at(-1), {
      method: "menu.trigger",
      params: { id: "windows.assets" },
    });

    satHooks.destroy();
    panelWins.destroy();
    satClient.close();
    host.close();
  });
});

test("卫星页与主窗口纹理板归属：卫星页不碰纹理板并显示说明；主窗口 Options 外出时纹理板发 parked，Windows ▸ Texture View 让框在主窗口浮出", async () => {
  await withFakeDom(async ({ body, win }) => {
    const app = await import("../src/ui/app.js");
    const { createPanelWindows } = await import("../src/ui/panelwindows.js");

    win.__TAURI__ = {
      core: {
        invoke() {
          return Promise.resolve();
        },
      },
    };

    const bus = new FakeBroadcastBus();
    const backend = createMockBackendClient();
    const host = new RelayHost(backend, bus.createEndpoint());
    const satClient = new RelayClient(bus.createEndpoint());
    await satClient.connect();

    const backendCalls: Array<{ method: string; params: Record<string, any> }> = [];
    backend.setCallHandler(async (method, params = {}) => {
      backendCalls.push({ method, params });
      return {};
    });

    // --- 1. 卫星页（satellite: true）：不发 texture.setHome，不响应纹理事件，Options 显示说明文字且正常显示 Mapping 选项 ---
    const satStore: any = app.createStore(satClient);
    const satPanels = app.panelFactories(satStore, satClient, { satellite: true });
    const satOptionsEl = new FakeElement("div");
    body.append(satOptionsEl);
    satPanels.QtToolOptions(satOptionsEl);
    satStore.texture.announceHome();

    const mappingOptionsLayout = {
      controls: [
        { type: "button", name: "auto_map", title: "Auto Mapping", hook: "auto_map", row: 0, start_column: 0, end_column: 1 },
        { type: "subwindow", name: "texture_view", row: 1, start_column: 0, end_column: 1 },
      ],
    };
    backend.emitState("toolOptions", mappingOptionsLayout);
    backend.emitState("event", { name: "textureSubControl.surface" });
    backend.emitState("event", { name: "textureView.surface" });
    backend.emitState("event", { name: "window.select", args: { name: "paint", page: "texture" } });

    await new Promise((r) => setTimeout(r, 10));
    assert.equal(
      backendCalls.some((c) => c.method === "texture.setHome"),
      false,
      "卫星页绝不能调用 texture.setHome",
    );
    assert.equal(satStore.texture.isEmbedded(), false);
    assert.equal(satStore.texture.isFloating(), false);

    const satTitles = satOptionsEl.querySelectorAll(".opt-title").map((el) => el.textContent);
    assert.ok(satTitles.includes("Texture is in the main window"));
    const satBtnLabels = satOptionsEl.querySelectorAll(".opt-button").map((el) => el.textContent);
    assert.ok(satBtnLabels.includes("Auto Mapping"));

    // --- 2. 主窗口：Options 嵌入纹理框后外出时发 parked，触发 textureSubControl.surface 在主窗口内浮出 ---
    const mainStore: any = app.createStore(backend);
    const mainPanels = app.panelFactories(mainStore, backend, { satellite: false });
    const mainOptionsEl = new FakeElement("div");
    body.append(mainOptionsEl);
    mainPanels.QtToolOptions(mainOptionsEl);

    const floatingList: Array<{ dockId: string; x: number; y: number; width: number; height: number }> = [];
    const runtime: any = {
      project: { scene: { nodes: { toolOptionsDock: { type: "Dock" } } } },
      getDockLayout: () => ({
        areas: { top: { groups: [] }, bottom: { groups: [] }, left: { groups: [] }, right: { groups: [] } },
        floating: [...floatingList],
      }),
      dockSetVisible(_dockId: string, vis: boolean) {
        mainOptionsEl.style.display = vis ? "" : "none";
      },
      dockRestore(dockId: string) {
        const idx = floatingList.findIndex((f) => f.dockId === dockId);
        if (idx >= 0) floatingList.splice(idx, 1);
      },
      view: () => ({ visible: mainOptionsEl.style.display !== "none", enabled: true, props: {} }),
    };

    const panelWins = createPanelWindows(runtime, mainStore, {
      host,
      onExternalChanged: () => mainStore.texture?.updateHome(),
    });
    mainStore.externalDocks = panelWins.externalDocks;

    backend.emitState("windows", { tool_options: { visible: true } });
    backend.emitState("toolOptions", mappingOptionsLayout);
    assert.equal(mainStore.texture.placement, "embedded");
    assert.equal(mainStore.texture.host, "options");
    assert.equal(mainStore.texture.home, "control");
    assert.deepEqual(
      backendCalls.filter((c) => c.method === "texture.setHome").at(-1)?.params,
      { home: "control", subControlVisible: true },
    );

    // Options 面板外出：纹理板跟不过去，框留在主窗口，自己浮出来，不会两边都看不到
    backendCalls.length = 0;
    floatingList.push({ dockId: "toolOptionsDock", x: 100, y: 100, width: 260, height: 320 });
    panelWins.onDockChanged("toolOptionsDock", true);
    assert.equal(mainStore.externalDocks.has("toolOptionsDock"), true);
    assert.equal(mainStore.texture.isFloating(), true);
    assert.equal(mainStore.texture.home, "control");

    // 主窗口里那份 Options 重建（换工具）：框继续浮着，不回到隐藏的面板里
    backend.emitState("toolOptions", mappingOptionsLayout);
    assert.equal(mainStore.texture.isFloating(), true);
    assert.equal(mainStore.texture.home, "control");

    // 角按钮：面板在别的窗口，放回去放不了，只能收起
    mainStore.texture.toggleFloating();
    assert.equal(mainStore.texture.placement, "parked");
    assert.equal(mainStore.texture.home, "parked");

    // Windows ▸ Texture View（textureSubControl.surface）：在主窗口浮出纹理框，不调 menu.trigger
    backendCalls.length = 0;
    backend.emitState("event", { name: "textureSubControl.surface" });
    assert.equal(mainStore.texture.isFloating(), true);
    assert.equal(mainStore.texture.home, "control");
    assert.equal(
      backendCalls.some((c) => c.method === "menu.trigger"),
      false,
    );
    assert.deepEqual(
      backendCalls.filter((c) => c.method === "texture.setHome").at(-1)?.params,
      { home: "control", subControlVisible: true },
    );

    panelWins.destroy();
    satClient.close();
    host.close();
  });
});

test("卫星页 createChrome：不弹启动画布对话框，不处理一次性 dialog/menu.action/settings.open 事件，保留主题、freeze 与菜单快捷键", async () => {
  await withFakeDom(async ({ body, win }) => {
    const app = await import("../src/ui/app.js");
    const [hostCh, satCh] = createFakeChannelPair();
    const backend = createMockBackendClient();
    const host = new RelayHost(backend, hostCh);
    const satClient = new RelayClient(satCh);
    await satClient.connect();
    const store = app.createStore(satClient);

    let pickCalled = 0;
    const triggeredMenus: Array<Record<string, unknown>> = [];
    backend.setCallHandler(async (method, params = {}) => {
      if (method === "menu.trigger") triggeredMenus.push(params);
      return {};
    });
    win.__TAURI__ = {
      core: {
        invoke(cmd: string) {
          if (cmd === "pick_files") pickCalled++;
          return Promise.resolve(["C:/fake.png"]);
        },
      },
    };

    const menuEl = new FakeElement("div");
    const statusEl = new FakeElement("div");
    const dockEl = new FakeElement("div");
    dockEl.className = "vf-dock";
    body.append(dockEl);

    app.createChrome(menuEl, statusEl, store, { satellite: true });

    // 后端发来含 canvasWidth/canvasHeight 的 window 状态及一次性事件
    backend.emitState("window", { canvasWidth: 1920, canvasHeight: 1080, title: "Main" });
    backend.emitState("event", { name: "dialog.canvasSize", args: { width: 800, height: 600 } });
    backend.emitState("event", { name: "settings.open", args: { name: "cfg", title: "Cfg", layout: [] } });
    backend.emitState("event", { name: "menu.action", args: { id: "file.import.raster" } });

    assert.equal(body.querySelectorAll(".qdialog-shade").length, 0);
    assert.equal(pickCalled, 0);

    // 主题与 freeze 仍然生效
    backend.emitState("theme", { mode: "dark" });
    assert.equal((globalThis as any).document.documentElement.dataset.theme, "dark");

    backend.emitState("freeze", { frozen: true });
    assert.equal(dockEl.inert, true);
    assert.equal(dockEl.style.pointerEvents, "none");

    backend.emitState("freeze", { frozen: false });
    assert.equal(dockEl.inert, false);
    assert.equal(dockEl.style.pointerEvents, "");

    // 卫星页快捷键（如 Ctrl+Z）经 relay 触发 menu.trigger
    backend.emitState("menus", [
      {
        id: "edit",
        text: "Edit",
        children: [{ id: "edit.undo", text: "Undo", shortcut: "Ctrl+Z", enabled: true }],
      },
    ]);
    let defaultPrevented = false;
    win.dispatchEvent({
      type: "keydown",
      key: "z",
      ctrlKey: true,
      altKey: false,
      shiftKey: false,
      target: dockEl,
      preventDefault() {
        defaultPrevented = true;
      },
    });
    await new Promise((r) => setTimeout(r, 10));
    assert.equal(defaultPrevented, true);
    assert.deepEqual(triggeredMenus.map((m: any) => m.id), ["edit.undo"]);

    // 重做还有一个菜单上不显示的键：Ctrl+Shift+Z
    backend.emitState("menus", [
      {
        id: "edit",
        text: "Edit",
        children: [{ id: "edit.redo", text: "Redo", shortcut: "Ctrl+Y", enabled: true }],
      },
    ]);
    win.dispatchEvent({
      type: "keydown",
      key: "Z",
      ctrlKey: true,
      altKey: false,
      shiftKey: true,
      target: dockEl,
      preventDefault() {},
    });
    await new Promise((r) => setTimeout(r, 10));
    assert.deepEqual(triggeredMenus.map((m: any) => m.id), ["edit.undo", "edit.redo"]);

    satClient.close();
    host.close();
  });
});

test("timelineDock 浮动与恢复：watchTimelineDock 与 panelwindows 协同不产生循环调用", async () => {
  await withFakeDom(async ({ win }) => {
    const { watchTimelineDock } = await import("../src/ui/timeline.js");
    const { createPanelWindows } = await import("../src/ui/panelwindows.js");

    const tauriCalls: Array<{ cmd: string; args: Record<string, any> }> = [];
    win.__TAURI__ = {
      core: {
        invoke(cmd: string, args: Record<string, any>) {
          tauriCalls.push({ cmd, args });
          return Promise.resolve();
        },
      },
    };

    const floatingList: Array<{ dockId: string; x: number; y: number; width: number; height: number }> = [];
    let dockVisible = true;
    const runtime: any = {
      project: { scene: { nodes: { timelineDock: { type: "Dock" } } } },
      getDockLayout: () => ({
        areas: {
          top: { groups: [] },
          bottom: { size: 120, groups: floatingList.length ? [] : [{ tabs: ["timelineDock"], active: "timelineDock" }] },
          left: { groups: [] },
          right: { groups: [] },
        },
        floating: [...floatingList],
      }),
      dockFloat(dockId: string) {
        if (!floatingList.some((f) => f.dockId === dockId)) {
          floatingList.push({ dockId, x: 100, y: 200, width: 500, height: 160 });
        }
      },
      dockRestore(dockId: string) {
        const idx = floatingList.findIndex((f) => f.dockId === dockId);
        if (idx >= 0) floatingList.splice(idx, 1);
      },
      dockMoveToArea() {},
      dockResizeArea() {},
      dockSetVisible(_dockId: string, vis: boolean) {
        dockVisible = vis;
      },
      view: () => ({ visible: dockVisible, enabled: true, props: {} }),
    };

    const listeners = new Map<string, Array<(v: any) => void>>();
    const stateValues = new Map<string, any>([
      ["windows", { timeline: { visible: true } }],
      ["timeline", { floating: false, collapsed: false, dockArea: "bottom" }],
    ]);
    const calls: Array<{ method: string; params: Record<string, any> }> = [];
    const store: any = {
      timeline: {
        bar: new FakeElement("div"),
        extent: () => ({ height: 108 }),
      },
      get: (k: string) => stateValues.get(k),
      on(k: string, fn: (v: any) => void) {
        const arr = listeners.get(k) ?? [];
        arr.push(fn);
        listeners.set(k, arr);
        if (stateValues.has(k)) fn(stateValues.get(k));
      },
      set(k: string, val: any) {
        stateValues.set(k, val);
        for (const fn of listeners.get(k) ?? []) fn(val);
      },
      call(method: string, params: Record<string, any> = {}) {
        calls.push({ method, params });
        return Promise.resolve({});
      },
    };

    let onTimelineDockChanged: ((dockId: string) => void) | undefined;
    const panelWins = createPanelWindows(runtime, store, {
      onDockRestored: (dockId: string) => onTimelineDockChanged?.(dockId),
    });
    onTimelineDockChanged = watchTimelineDock(runtime, store);

    // 1. 后端 timeline.floating 变为 true：打开 panel-timelineDock，且不反向调 timeline.setFloating
    store.set("timeline", { floating: true, collapsed: false, dockArea: "bottom" });
    assert.equal(tauriCalls.length, 1);
    assert.equal(tauriCalls[0].cmd, "open_panel_window");
    assert.equal(tauriCalls[0].args.label, "panel-timelineDock");
    assert.equal(dockVisible, false);
    assert.equal(calls.length, 0);

    // 再次收到相同的 timeline.floating = true 状态时不会重复开窗或循环调用
    store.set("timeline", { floating: true, collapsed: false, dockArea: "bottom" });
    assert.equal(tauriCalls.length, 1);
    assert.equal(calls.length, 0);

    // 2. 卫星页发来 restore：恢复停靠，关闭独立窗口，只调一次 timeline.setFloating({ floating: false })
    panelWins.handleAction("restore", "timelineDock");
    assert.equal(tauriCalls.length, 2);
    assert.equal(tauriCalls[1].cmd, "close_panel_window");
    assert.equal(dockVisible, true);
    assert.deepEqual(calls, [{ method: "timeline.setFloating", params: { floating: false } }]);

    // 后端回传 timeline.floating = false 时不产生任何额外调用
    store.set("timeline", { floating: false, collapsed: false, dockArea: "bottom" });
    assert.equal(calls.length, 1);
    assert.equal(tauriCalls.length, 2);

    // 3. 用户关掉浮动的 Timeline：timeline 状态先到，这时窗口表还是旧的，不能把刚关的窗口又打开
    store.set("timeline", { floating: true, collapsed: false, dockArea: "bottom" });
    assert.equal(tauriCalls.at(-1)?.cmd, "open_panel_window");
    panelWins.handleAction("closed", "timelineDock");
    const afterClose = tauriCalls.length;
    store.set("timeline", { floating: true, collapsed: false, dockArea: "bottom" });
    assert.equal(tauriCalls.length, afterClose);
    store.set("windows", { timeline: { visible: false } });
    assert.equal(tauriCalls.length, afterClose);

    panelWins.destroy();
  });
});

test("启动时 Timeline 是浮动的：窗口表到了才决定开不开", async () => {
  await withFakeDom(async ({ win }) => {
    const { createPanelWindows } = await import("../src/ui/panelwindows.js");
    const tauriCalls: string[] = [];
    win.__TAURI__ = {
      core: {
        invoke(cmd: string) {
          tauriCalls.push(cmd);
          return Promise.resolve();
        },
      },
    };
    const runtime: any = {
      project: { scene: { nodes: { timelineDock: { type: "Dock" } } } },
      getDockLayout: () => ({ floating: [{ dockId: "timelineDock", x: 1, y: 2, width: 300, height: 120 }] }),
      dockSetVisible() {},
    };
    const listeners = new Map<string, Array<(v: any) => void>>();
    const stateValues = new Map<string, any>();
    const store: any = {
      get: (k: string) => stateValues.get(k),
      on(k: string, fn: (v: any) => void) {
        listeners.set(k, [...(listeners.get(k) ?? []), fn]);
      },
      set(k: string, val: any) {
        stateValues.set(k, val);
        for (const fn of listeners.get(k) ?? []) fn(val);
      },
      call: () => Promise.resolve({}),
    };
    const panelWins = createPanelWindows(runtime, store, {});

    store.set("timeline", { floating: true });
    assert.deepEqual(tauriCalls, [], "还不知道它该不该显示");
    store.set("windows", { timeline: { visible: false } });
    assert.deepEqual(tauriCalls, []);
    store.set("windows", { timeline: { visible: true } });
    assert.deepEqual(tauriCalls, ["open_panel_window"]);

    panelWins.destroy();
  });
});
