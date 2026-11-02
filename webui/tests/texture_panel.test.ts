import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import { attachToolsSubControlHost } from "../src/ui/tools.js";

class FakeClassList {
  set = new Set<string>();
  owner: FakeElement;
  constructor(owner: FakeElement) {
    this.owner = owner;
  }
  private sync() {
    this.owner._className = [...this.set].join(" ");
  }
  add(...tokens: string[]) {
    for (const t of tokens) if (t) this.set.add(t);
    this.sync();
  }
  remove(...tokens: string[]) {
    for (const t of tokens) this.set.delete(t);
    this.sync();
  }
  toggle(token: string, force?: boolean) {
    const on = force !== undefined ? force : !this.set.has(token);
    if (on) this.set.add(token);
    else this.set.delete(token);
    this.sync();
    return on;
  }
  contains(token: string) {
    return this.set.has(token);
  }
}

class FakeElement {
  tagName: string;
  id = "";
  _className = "";
  classList = new FakeClassList(this);
  style: Record<string, string> = {};
  dataset: Record<string, string> = {};
  children: FakeElement[] = [];
  parentElement: FakeElement | null = null;
  textContent = "";
  title = "";
  type = "";
  value = "";
  checked = false;
  hidden = false;
  inert = false;
  tabIndex = 0;
  width = 0;
  height = 0;
  clientWidth = 300;
  clientHeight = 220;
  rectOverride: { left: number; top: number; width: number; height: number } | null = null;
  listeners = new Map<string, Array<{ fn: (e: any) => void; capture: boolean }>>();

  constructor(tag: string) {
    this.tagName = tag.toUpperCase();
  }
  get className() {
    return this._className;
  }
  set className(v: string) {
    this._className = String(v ?? "");
    this.classList.set = new Set(this._className.split(/\s+/).filter(Boolean));
  }
  get firstChild(): FakeElement | null {
    return this.children[0] ?? null;
  }
  get childElementCount(): number {
    return this.children.length;
  }
  get isConnected(): boolean {
    let cur: FakeElement | null = this;
    while (cur.parentElement) cur = cur.parentElement;
    return cur.tagName === "BODY" || cur.tagName === "HTML";
  }
  set innerHTML(_html: string) {
    this.children = [];
    if (_html.includes("sb-handle")) {
      const a = new FakeElement("div");
      a.className = "sb-btn sb-a";
      const b = new FakeElement("div");
      b.className = "sb-btn sb-b";
      const h = new FakeElement("div");
      h.className = "sb-handle";
      this.append(a, b, h);
    }
  }
  append(...nodes: any[]) {
    for (const n of nodes) {
      if (n instanceof FakeElement) {
        n.remove();
        n.parentElement = this;
        this.children.push(n);
      }
    }
  }
  replaceChildren(...nodes: any[]) {
    for (const c of this.children) c.parentElement = null;
    this.children = [];
    this.append(...nodes);
  }
  replaceWith(other: FakeElement) {
    if (!this.parentElement) return;
    const parent = this.parentElement;
    const idx = parent.children.indexOf(this);
    if (idx >= 0) {
      other.remove();
      this.parentElement = null;
      other.parentElement = parent;
      parent.children[idx] = other;
    }
  }
  remove() {
    if (!this.parentElement) return;
    const idx = this.parentElement.children.indexOf(this);
    if (idx >= 0) this.parentElement.children.splice(idx, 1);
    this.parentElement = null;
  }
  contains(other: FakeElement | null): boolean {
    let cur = other;
    while (cur) {
      if (cur === this) return true;
      cur = cur.parentElement;
    }
    return false;
  }
  matches(selector: string): boolean {
    for (const part of selector.split(",").map((s) => s.trim())) {
      if (!part || part === ":active") continue;
      const scoped = part.startsWith(":scope > ") ? part.slice(":scope > ".length).trim() : part;
      if (scoped.startsWith(".")) {
        const classes = scoped.slice(1).split(".");
        if (classes.every((c) => this.classList.contains(c))) return true;
      } else if (scoped === '[data-vf-id="toolsDock"]') {
        if (this.dataset.vfId === "toolsDock") return true;
      } else if (scoped === '[data-vf-id="toolsScroll"]') {
        if (this.dataset.vfId === "toolsScroll") return true;
      } else if (scoped.toUpperCase() === this.tagName) {
        return true;
      }
    }
    return false;
  }
  closest(selector: string): FakeElement | null {
    let cur: FakeElement | null = this;
    while (cur) {
      if (cur.matches(selector)) return cur;
      cur = cur.parentElement;
    }
    return null;
  }
  querySelector(selector: string): FakeElement | null {
    return this.querySelectorAll(selector)[0] ?? null;
  }
  querySelectorAll(selector: string): FakeElement[] {
    if (selector.startsWith(":scope > ")) {
      const rest = selector.slice(":scope > ".length).trim();
      return this.children.filter((c) => c.matches(rest));
    }
    const out: FakeElement[] = [];
    const visit = (el: FakeElement) => {
      for (const c of el.children) {
        if (c.matches(selector)) out.push(c);
        visit(c);
      }
    };
    visit(this);
    return out;
  }
  addEventListener(type: string, fn: (e: any) => void, options?: boolean | { capture?: boolean }) {
    const capture = typeof options === "boolean" ? options : Boolean(options?.capture);
    if (!this.listeners.has(type)) this.listeners.set(type, []);
    this.listeners.get(type)!.push({ fn, capture });
  }
  removeEventListener(type: string, fn: (e: any) => void, options?: boolean | { capture?: boolean }) {
    const capture = typeof options === "boolean" ? options : Boolean(options?.capture);
    const list = this.listeners.get(type);
    if (!list) return;
    this.listeners.set(
      type,
      list.filter((entry) => entry.fn !== fn || entry.capture !== capture),
    );
  }
  dispatchEvent(raw: Record<string, any>) {
    let stopped = false;
    let immediateStopped = false;
    const event: Record<string, any> = {
      type: "",
      button: 0,
      buttons: 1,
      target: this,
      preventDefault() {
        event.defaultPrevented = true;
      },
      stopPropagation() {
        stopped = true;
      },
      stopImmediatePropagation() {
        stopped = true;
        immediateStopped = true;
      },
      defaultPrevented: false,
      ...raw,
    };
    const list = [...(this.listeners.get(event.type) ?? [])];
    for (const entry of list) {
      entry.fn(event);
      if (immediateStopped) break;
    }
    if (!stopped && this.parentElement) {
      let p: FakeElement | null = this.parentElement;
      while (p && !stopped) {
        for (const entry of [...(p.listeners.get(event.type) ?? [])]) {
          if (!entry.capture) entry.fn(event);
          if (immediateStopped) break;
        }
        p = p.parentElement;
      }
    }
    return !event.defaultPrevented;
  }
  click() {
    this.dispatchEvent({ type: "click" });
  }
  focus() {}
  setPointerCapture() {}
  releasePointerCapture() {}
  hasPointerCapture() {
    return false;
  }
  getBoundingClientRect() {
    if (this.rectOverride) {
      const { left, top, width, height } = this.rectOverride;
      return { left, top, right: left + width, bottom: top + height, width, height };
    }
    const left = Number.parseFloat(this.style.left ?? "0") || 0;
    const top = Number.parseFloat(this.style.top ?? "0") || 0;
    const width = Number.parseFloat(this.style.width ?? "") || this.clientWidth;
    const height = Number.parseFloat(this.style.height ?? "") || this.clientHeight;
    return { left, top, right: left + width, bottom: top + height, width, height };
  }
  getContext(kind?: string) {
    if (kind === "webgl2") return null;
    return {
      font: "",
      measureText: (t: string) => ({ width: t.length * 7 }),
      setTransform() {},
      clearRect() {},
      save() {},
      restore() {},
      beginPath() {},
      rect() {},
      clip() {},
      fillRect() {},
      strokeRect() {},
      arc() {},
      fill() {},
      stroke() {},
      fillText() {},
      drawImage() {},
      createLinearGradient: () => ({ addColorStop() {} }),
      createConicGradient: () => ({ addColorStop() {} }),
    };
  }
}

async function withTextureHarness(
  run: (h: {
    body: FakeElement;
    centralEl: FakeElement;
    optionsEl: FakeElement;
    toolsEl: FakeElement;
    store: any;
    calls: Array<{ method: string; params: any }>;
    inputs: Array<Record<string, any>>;
    emitState: (key: string, value: any) => void;
  }) => Promise<void> | void,
) {
  const prev = {
    document: globalThis.document,
    window: globalThis.window,
    getComputedStyle: globalThis.getComputedStyle,
    ResizeObserver: globalThis.ResizeObserver,
    MutationObserver: globalThis.MutationObserver,
    HTMLInputElement: globalThis.HTMLInputElement,
    HTMLTextAreaElement: globalThis.HTMLTextAreaElement,
  };
  const docEl = new FakeElement("html");
  const body = new FakeElement("body");
  docEl.append(body);
  const win: any = {
    devicePixelRatio: 1,
    addEventListener() {},
    removeEventListener() {},
  };
  const doc: any = {
    documentElement: docEl,
    body,
    activeElement: null,
    getElementById: (id: string) => (id === "app" ? body : null),
    createElement: (tag: string) => new FakeElement(tag),
    createTextNode: (text: string) => {
      const span = new FakeElement("text");
      span.textContent = text;
      return span;
    },
    querySelector: (sel: string) => body.querySelector(sel),
    querySelectorAll: (sel: string) => body.querySelectorAll(sel),
  };
  Object.assign(globalThis, {
    document: doc,
    window: win,
    getComputedStyle: () => ({ getPropertyValue: () => "#000" }),
    ResizeObserver: class {
      observe() {}
      disconnect() {}
    },
    MutationObserver: class {
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
    },
    HTMLInputElement: class {},
    HTMLTextAreaElement: class {},
  });
  try {
    const { createStore, panelFactories } = await import("../src/ui/app.js");
    const stateListeners = new Map<string, (v: any) => void>();
    const calls: Array<{ method: string; params: any }> = [];
    const inputs: Array<Record<string, any>> = [];
    const client: any = {
      onState: (k: string, fn: (v: any) => void) => stateListeners.set(k, fn),
      onBoard() {},
      onImage() {},
      ack() {},
      sendInput: (msg: Record<string, any>) => inputs.push(msg),
      call: async (method: string, params: any = {}) => {
        calls.push({ method, params });
        if (method === "texture.viewButton") {
          return { viewButtons: [{ name: params.name, title: params.name, checkable: true, checked: Boolean(params.on) }] };
        }
        return { ok: true };
      },
    };
    const store: any = createStore(client);
    const factories = panelFactories(store, client);

    const centralEl = new FakeElement("div");
    centralEl.rectOverride = { left: 260, top: 24, width: 700, height: 500 };
    const optionsEl = new FakeElement("div");
    optionsEl.rectOverride = { left: 960, top: 24, width: 280, height: 340 };
    const toolsDock = new FakeElement("div");
    toolsDock.dataset.vfId = "toolsDock";
    const toolsScroll = new FakeElement("div");
    toolsScroll.dataset.vfId = "toolsScroll";
    toolsScroll.rectOverride = { left: 0, top: 24, width: 250, height: 500 };
    toolsDock.append(toolsScroll);
    body.append(toolsDock, centralEl, optionsEl);

    attachToolsSubControlHost(toolsDock, store);
    factories.QtCentral(centralEl);
    factories.QtToolOptions(optionsEl);

    const emitState = (key: string, value: any) => stateListeners.get(key)?.(value);
    emitState("windows", {
      tools: { visible: true },
      tool_options: { visible: true },
      timeline: { visible: true },
    });

    await run({ body, centralEl, optionsEl, toolsEl: toolsScroll, store, calls, inputs, emitState });
  } finally {
    Object.assign(globalThis, prev);
  }
}

test("中央 Texture 页显示 createTexturePanel：菜单栏（File、Setting、View/脚本菜单）与脚本按钮行", async () => {
  await withTextureHarness(async ({ body, centralEl, store, calls, emitState }) => {
    emitState("textureMenus", [
      {
        id: "file.texture",
        text: "File",
        kind: "submenu",
        children: [{ id: "file.texture.open", text: "Open Texture View...", kind: "action", enabled: true }],
      },
      {
        id: "child.setting",
        text: "Setting",
        kind: "submenu",
        children: [
          { id: "child.setting.changeableTexture", text: "Changeable Texture", kind: "check", checked: true, enabled: true },
        ],
      },
      {
        id: "script:child:0",
        text: "View",
        kind: "submenu",
        children: [{ id: "script:child:0.0", text: "Fit Content", kind: "action", enabled: true }],
      },
    ]);
    emitState("windows", {
      tools: { visible: true },
      tool_options: { visible: true },
      timeline: { visible: true },
      textureViewButtons: [{ name: "uv_grid", title: "UV Grid", checkable: true, checked: false }],
    });

    // Switch central tab to Texture (index 1)
    const tabs = centralEl.querySelectorAll(".tab");
    assert.equal(tabs.length, 2);
    assert.equal(tabs[1].textContent, "Texture");
    tabs[1].click();

    assert.equal(store.texture.home, "central");
    const texPanelEl = centralEl.querySelector(".texture-panel");
    assert.ok(texPanelEl, "TexturePanel 应挂载在中央 Texture 页槽位里");

    const menuItems = texPanelEl!.querySelectorAll(".item");
    assert.deepEqual(
      menuItems.map((m) => m.textContent),
      ["File", "Setting", "View"],
    );

    // Click View menu -> popup item -> triggers menu.trigger
    menuItems[2].click();
    const popupItem = body.querySelector(".qmenu")?.querySelector(".qmenu-item");
    assert.ok(popupItem);
    assert.equal(popupItem!.querySelector(".qmenu-text")?.textContent, "Fit Content");
    popupItem!.click();
    assert.equal(calls.at(-1)?.method, "menu.trigger");
    assert.equal(calls.at(-1)?.params.id, "script:child:0.0");

    // Script button row -> triggers texture.viewButton
    const btn = texPanelEl!.querySelector(".texture-options")?.querySelector(".qbutton");
    assert.ok(btn);
    assert.equal(btn!.textContent, "UV Grid");
    assert.equal(btn!.classList.contains("checked"), false);
    btn!.click();
    await Promise.resolve();
    assert.equal(calls.at(-1)?.method, "texture.viewButton");
    assert.deepEqual(calls.at(-1)?.params, { view: "child", name: "uv_grid", on: true });
    assert.equal(texPanelEl!.querySelector(".texture-options")?.querySelector(".qbutton")?.classList.contains("checked"), true);
  });
});

test("三个家（central、control、parked）搬迁复用同一个 canvas 节点并发 texture.setHome 与 resize", async () => {
  await withTextureHarness(async ({ centralEl, store, calls, inputs, emitState }) => {
    const childCanvas = store.boards.slots.child.canvas;
    const childRenderer = store.boards.slots.child.renderer;
    assert.ok(childCanvas);

    // Initially Drawing tab is active and SubControlFrame is parked -> home is parked
    assert.equal(store.texture.home, "parked");
    assert.ok(calls.some((c) => c.method === "texture.setHome" && c.params.home === "parked"));

    // 1. Switch central to Texture page -> home becomes central
    const tabs = centralEl.querySelectorAll(".tab");
    tabs[1].click();
    assert.equal(store.texture.home, "central");
    assert.equal(calls.filter((c) => c.method === "texture.setHome").at(-1)?.params.home, "central");
    assert.equal(centralEl.contains(childCanvas), true);
    assert.equal(store.boards.slots.child.canvas, childCanvas);
    assert.equal(store.boards.slots.child.renderer, childRenderer);

    // 2. Windows ▸ Texture View (textureSubControl.surface) while Central is on Texture page:
    // frame floats, but Central Texture page keeps priority while visible, and frame shows "Open in the main view"
    emitState("event", { name: "textureSubControl.surface" });
    assert.equal(store.texture.isFloating(), true);
    assert.equal(store.texture.home, "central");
    assert.equal(store.texture.frame.el.querySelector(".subframe-placeholder")?.textContent, "Open in the main view");

    // 3. Switch central back to Drawing page -> floating SubControlFrame claims TexturePanel (home === "control")
    const prevChildResizes = inputs.filter((i) => i.view === "child" && i.kind === "resize").length;
    tabs[0].click();
    assert.equal(store.texture.home, "control");
    assert.equal(calls.filter((c) => c.method === "texture.setHome").at(-1)?.params.home, "control");
    assert.equal(store.texture.frame.el.contains(childCanvas), true);
    assert.equal(centralEl.querySelector(".central-placeholder")?.textContent, "The texture view is open in its own control.");
    assert.equal(store.boards.slots.child.canvas, childCanvas, "搬迁不得重建 child canvas");
    assert.equal(store.boards.slots.child.renderer, childRenderer, "搬迁不得重建渲染器上下文");
    assert.ok(inputs.filter((i) => i.view === "child" && i.kind === "resize").length > prevChildResizes);

    // 4. Corner button parks when floating with no prior host -> home becomes parked
    store.texture.frame.el.querySelector(".subframe-glyph")!.click();
    assert.equal(store.texture.placement, "parked");
    assert.equal(store.texture.home, "parked");
    assert.equal(calls.filter((c) => c.method === "texture.setHome").at(-1)?.params.home, "parked");
    assert.equal(store.boards.slots.child.canvas, childCanvas);
  });
});

test("活动板拿到键盘焦点（Qt 的 setActivePaintView）：首个状态不抢，对话框开着不抢", async () => {
  await withTextureHarness(async ({ body, store, emitState }) => {
    const doc: any = globalThis.document;
    const focused: string[] = [];
    for (const view of ["main", "child"]) {
      const canvas = store.boards.slots[view].canvas;
      canvas.focus = () => {
        doc.activeElement = canvas;
        focused.push(view);
      };
    }

    // The state a connection starts with is not a change: the startup dialog keeps the keys.
    emitState("window", { activeView: "main" });
    assert.deepEqual(focused, []);

    emitState("window", { activeView: "child" });
    assert.deepEqual(focused, ["child"]);
    emitState("window", { activeView: "child", statusMessage: "same board" });
    assert.deepEqual(focused, ["child"]);

    const shade = new FakeElement("div");
    shade.className = "qdialog-shade";
    body.append(shade);
    doc.activeElement = shade;
    emitState("window", { activeView: "main" });
    assert.deepEqual(focused, ["child"], "a dialog keeps the keys it has");
    shade.remove();
    doc.activeElement = null;

    emitState("window", { activeView: "child" });
    assert.deepEqual(focused, ["child", "child"]);
    // A board that already has the keys is left alone.
    doc.activeElement = store.boards.slots.main.canvas;
    emitState("window", { activeView: "main" });
    assert.deepEqual(focused, ["child", "child"]);
  });
});

test("Mapping 工具 subwindow 自动嵌入 Options、手动浮出时不抢、拖拽嵌进 Tools/Options 与右下角拉大小", async () => {
  await withTextureHarness(async ({ body, optionsEl, toolsEl, store, inputs, emitState }) => {
    const childCanvas = store.boards.slots.child.canvas;

    // 1. Selecting a Mapping tool sends toolOptions with a subwindow control -> auto-embeds into Options
    emitState("toolOptions", {
      controls: [{ type: "subwindow", name: "texture_view", row: 0, start_column: 0, end_column: 1 }],
    });
    assert.equal(store.texture.placement, "embedded");
    assert.equal(store.texture.host, "options");
    assert.equal(store.texture.home, "control");
    assert.equal(optionsEl.contains(store.texture.frame.el), true);
    assert.equal(optionsEl.contains(childCanvas), true);

    // 2. Dragging title bar floats the frame in-page; Options shows "Texture is floating — drag it back"
    const head = store.texture.frame.el.querySelector(".subframe-title")!;
    head.dispatchEvent({ type: "pointerdown", button: 0, clientX: 980, clientY: 40, pointerId: 1 });
    head.dispatchEvent({ type: "pointermove", clientX: 500, clientY: 200, pointerId: 1 });
    head.dispatchEvent({ type: "pointerup", clientX: 500, clientY: 200, pointerId: 1 });
    assert.equal(store.texture.isFloating(), true);
    assert.equal(optionsEl.querySelector(".opt-title")?.textContent, "Texture is floating — drag it back");

    // Re-sending Mapping toolOptions while manually floating must NOT steal the floating frame back
    emitState("toolOptions", {
      controls: [{ type: "subwindow", name: "texture_view", row: 0, start_column: 0, end_column: 1 }],
    });
    assert.equal(store.texture.isFloating(), true);
    assert.equal(optionsEl.querySelector(".opt-title")?.textContent, "Texture is floating — drag it back");

    // 3. Resize floating frame via bottom-right grip -> enforces min 180x140 and resends child resize
    const grip = store.texture.frame.el.querySelector(".subframe-grip")!;
    const beforeResizeCount = inputs.filter((i) => i.view === "child" && i.kind === "resize").length;
    grip.dispatchEvent({ type: "pointerdown", button: 0, clientX: 500, clientY: 200, pointerId: 2 });
    grip.dispatchEvent({ type: "pointermove", clientX: 620, clientY: 290, pointerId: 2 });
    grip.dispatchEvent({ type: "pointerup", clientX: 620, clientY: 290, pointerId: 2 });
    assert.equal(store.texture.frame.el.style.width, "440px");
    assert.equal(store.texture.frame.el.style.height, "350px");
    assert.ok(inputs.filter((i) => i.view === "child" && i.kind === "resize").length > beforeResizeCount);

    // 4. Drag floating frame over Tools panel -> shows drop preview wash and embeds into Tools on drop
    head.dispatchEvent({ type: "pointerdown", button: 0, clientX: 500, clientY: 200, pointerId: 3 });
    head.dispatchEvent({ type: "pointermove", clientX: 120, clientY: 200, pointerId: 3 });
    assert.ok(body.querySelector(".subframe-wash"), "拖到 Tools 面板上应显示蓝雾预览框");
    head.dispatchEvent({ type: "pointerup", clientX: 120, clientY: 200, pointerId: 3 });
    assert.equal(body.querySelector(".subframe-wash"), null);
    assert.equal(store.texture.placement, "embedded");
    assert.equal(store.texture.host, "tools");
    assert.equal(toolsEl.contains(store.texture.frame.el), true);
    assert.equal(toolsEl.contains(childCanvas), true);

    // 5. Corner button floats from Tools, and clicking again re-embeds back into lastHost (Tools)
    const btn = store.texture.frame.el.querySelector(".subframe-glyph")!;
    btn.click();
    assert.equal(store.texture.isFloating(), true);
    btn.click();
    assert.equal(store.texture.placement, "embedded");
    assert.equal(store.texture.host, "tools");
  });
});

test("后端 textureMenus、textureSubControl.surface 与 texture.setHome 两处视口记忆及自动适配", async (t) => {
  const here = path.dirname(fileURLToPath(import.meta.url));
  const exePath = path.resolve(here, "../../build/use/backend/AnimeAnBackend.exe");
  if (!fs.existsSync(exePath)) {
    t.skip("AnimeAnBackend.exe not built");
    return;
  }
  const token = `tex_${Date.now()}_${Math.random().toString(36).slice(2)}`;
  const extraPaths = [
    path.win32.normalize("C:/Qt/6.9.1/mingw_64/bin"),
    path.win32.normalize("C:/Qt/Tools/mingw1310_64/bin"),
    path.win32.normalize("C:/Users/admin/Documents/AnimeAn/tools/python312"),
    path.dirname(exePath),
  ];
  const child = spawn(exePath, ["--port", "0", "--token", token], {
    stdio: ["ignore", "pipe", "pipe"],
    env: { ...process.env, PATH: [...extraPaths, process.env.PATH || process.env.Path || ""].join(";") },
  });
  let ws: WebSocket | null = null;
  try {
    const port = await new Promise<number>((resolve, reject) => {
      let buf = "";
      child.stdout.on("data", (chunk) => {
        buf += chunk.toString("utf8");
        const m = buf.match(/ANIMEAN_BACKEND_PORT=(\d+)/);
        if (m) resolve(Number(m[1]));
      });
      child.once("exit", (code) => reject(new Error(`Backend exited (${code})`)));
    });
    const state = new Map<string, any>();
    const events: Array<{ name: string; args: any }> = [];
    const pending = new Map<number, { resolve: (v: any) => void; reject: (e: Error) => void }>();
    let childOps: any[] = [];
    let childBoardWaiters: Array<() => void> = [];
    let nextId = 1;
    ws = new WebSocket(`ws://127.0.0.1:${port}/?token=${encodeURIComponent(token)}`);
    await new Promise<void>((resolve, reject) => {
      ws!.addEventListener("open", () => resolve(), { once: true });
      ws!.addEventListener("error", () => reject(new Error("ws error")), { once: true });
    });
    ws.addEventListener("message", (ev) => {
      if (typeof ev.data !== "string") return;
      const msg = JSON.parse(ev.data);
      if (msg.t === "state") {
        state.set(msg.key, msg.value);
        if (msg.key === "event" && msg.value?.name) events.push(msg.value);
      } else if (msg.t === "board") {
        if (msg.view === "child") {
          childOps = childOps.slice(0, msg.keep ?? 0).concat(msg.ops ?? []);
          for (const w of childBoardWaiters.splice(0)) w();
        }
        ws!.send(JSON.stringify({ t: "ack", view: msg.view, frame: msg.frame }));
      } else if (msg.t === "reply") {
        const p = pending.get(msg.id);
        if (p) {
          pending.delete(msg.id);
          if (msg.ok) p.resolve(msg.result);
          else p.reject(new Error(String(msg.error)));
        }
      }
    });
    const call = (method: string, params: Record<string, any> = {}) =>
      new Promise<any>((resolve, reject) => {
        const id = nextId++;
        pending.set(id, { resolve, reject });
        ws!.send(JSON.stringify({ t: "call", id, method, params }));
      });
    const sendInput = (msg: Record<string, any>) => ws!.send(JSON.stringify({ t: "input", ...msg }));
    const getChildTransform = () => {
      const nonIdentity = childOps.find((op) => Array.isArray(op) && op[0] === "T" && (op[1] !== 1 || op[5] !== 0 || op[6] !== 0));
      const fallback = childOps.find((op) => Array.isArray(op) && op[0] === "T");
      return JSON.stringify(nonIdentity ?? fallback ?? null);
    };
    const flushBoard = async () => {
      await call("view.setActive", { view: "child" });
      await new Promise((r) => setTimeout(r, 25));
    };

    await call("project.new", { width: 400, height: 300 });
    const texMenus = state.get("textureMenus");
    assert.ok(Array.isArray(texMenus), "应下发 textureMenus 状态键");
    assert.deepEqual(
      texMenus.map((m: any) => m.text),
      ["File", "Setting", "View"],
    );

    // Windows ▸ Texture View 触发 textureSubControl.surface
    events.length = 0;
    await call("menu.trigger", { id: "windows.texture_view", checked: true });
    assert.ok(events.some((e) => e.name === "textureSubControl.surface"));

    // 在 child 画板设好尺寸并在 central 家画一笔、缩放平移后切到 control，再切回 central 验证视口记忆与自动适配
    sendInput({ view: "child", kind: "resize", w: 300, h: 200, dpr: 1 });
    await call("texture.setHome", { home: "central" });
    await call("view.setActive", { view: "child" });
    await call("tool.select", { tool: "pen" });
    sendInput({ view: "child", kind: "press", x: 40, y: 40, button: 1, buttons: 1, modifiers: 0, pressure: 0.8, time: 10 });
    sendInput({ view: "child", kind: "move", x: 180, y: 120, button: 0, buttons: 1, modifiers: 0, pressure: 0.8, time: 30 });
    sendInput({ view: "child", kind: "release", x: 180, y: 120, button: 1, buttons: 0, modifiers: 0, pressure: 0.0, time: 50 });

    // 在 central 手动滚轮放大
    sendInput({ view: "child", kind: "wheel", x: 150, y: 100, dx: 0, dy: 240, buttons: 0, modifiers: 0, time: 60 });
    await flushBoard();
    const centralTransformBefore = getChildTransform();
    assert.ok(JSON.parse(centralTransformBefore)[1] > 1, "central 滚轮放大后 zoom 应大于 1");

    // 搬到 control：触发 autoFitTextureControlView，变换改变
    const replyControl = await call("texture.setHome", { home: "control" });
    await flushBoard();
    assert.equal(replyControl.home, "control");
    const controlAutoFitTransform = getChildTransform();
    assert.notEqual(controlAutoFitTransform, centralTransformBefore);

    // 在 control 手动缩放后 autoFitSuspended 置 true，resize 不再覆盖手动缩放系数
    sendInput({ view: "child", kind: "wheel", x: 150, y: 100, dx: 0, dy: 240, buttons: 0, modifiers: 0, time: 80 });
    await flushBoard();
    const manualControlScale = JSON.parse(getChildTransform())[1];
    assert.notEqual(manualControlScale, JSON.parse(controlAutoFitTransform)[1]);
    sendInput({ view: "child", kind: "resize", w: 320, h: 220, dpr: 1 });
    await flushBoard();
    assert.equal(JSON.parse(getChildTransform())[1], manualControlScale);

    // 搬回 central：恢复离开 central 时的视口变换
    await call("texture.setHome", { home: "central" });
    await flushBoard();
    assert.equal(getChildTransform(), centralTransformBefore);

    // 再次搬回 control：autoFitSuspended 重置并重新按内容自动适配
    sendInput({ view: "child", kind: "resize", w: 300, h: 200, dpr: 1 });
    await call("texture.setHome", { home: "control" });
    await flushBoard();
    assert.equal(getChildTransform(), controlAutoFitTransform);
  } finally {
    try {
      ws?.close();
    } catch {}
    try {
      child.kill();
    } catch {}
  }
});
