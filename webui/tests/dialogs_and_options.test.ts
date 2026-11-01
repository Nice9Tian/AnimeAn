import assert from "node:assert/strict";
import { test } from "node:test";
import {
  colorButtonColors,
  colorDarker,
  colorLighter,
  colorLightness,
  parseColor,
  toHexRgb,
} from "../src/ui/palette.js";
import { createToolOptions } from "../src/ui/options.js";
import { createCentral, isUnboundedViewport } from "../src/ui/panels.js";

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
  inert = false;
  tabIndex = 0;
  width = 0;
  height = 0;
  clientWidth = 296;
  clientHeight = 200;
  scrollHeight = 200;
  scrollTop = 0;
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
    } else if (_html.includes("pal-chip")) {
      const chip = new FakeElement("div");
      chip.className = "pal-chip";
      const hex = new FakeElement("div");
      hex.className = "pal-hex";
      const rgba = new FakeElement("div");
      rgba.className = "pal-rgba";
      this.append(chip, hex, rgba);
    } else if (_html.includes("groove")) {
      const g = new FakeElement("div");
      g.className = "groove";
      const s = new FakeElement("div");
      s.className = "sub";
      const h = new FakeElement("div");
      h.className = "handle";
      this.append(g, s, h);
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
      if (!part) continue;
      if (part === ":active") continue;
      if (part.startsWith(".")) {
        const classes = part.slice(1).split(".");
        if (classes.every((c) => this.classList.contains(c))) return true;
      } else if (part === '[data-vf-id="pythonDebugDock"]') {
        if (this.dataset.vfId === "pythonDebugDock") return true;
      } else if (part === '[data-python-debug="true"]') {
        if (this.dataset.pythonDebug === "true") return true;
      } else if (part.toUpperCase() === this.tagName) {
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
    const w = (globalThis as any).window;
    if (w && w !== this && typeof w._dispatchCapture === "function") {
      w._dispatchCapture(event, () => immediateStopped);
      if (stopped) return !event.defaultPrevented;
    }
    const list = [...(this.listeners.get(event.type) ?? [])];
    for (const entry of list) {
      entry.fn(event);
      if (immediateStopped) break;
    }
    if (!stopped && w && w !== this && typeof w._dispatchBubble === "function") {
      w._dispatchBubble(event, () => immediateStopped);
    }
    return !event.defaultPrevented;
  }
  click() {
    this.dispatchEvent({ type: "click" });
  }
  focus() {}
  setSelectionRange() {}
  setPointerCapture() {}
  hasPointerCapture() {
    return false;
  }
  getBoundingClientRect() {
    return { left: 0, top: 0, right: this.clientWidth, bottom: this.clientHeight, width: this.clientWidth, height: this.clientHeight };
  }
  getContext() {
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
      createLinearGradient: () => ({ addColorStop() {} }),
      createConicGradient: () => ({ addColorStop() {} }),
    };
  }
}

let cachedUi: {
  messageBox: typeof import("../src/ui/panels2.js").messageBox;
  createChrome: typeof import("../src/ui/app.js").createChrome;
  createStore: typeof import("../src/ui/app.js").createStore;
  defaultSaveName: typeof import("../src/ui/app.js").defaultSaveName;
  isAllowedWhenFrozen: typeof import("../src/ui/app.js").isAllowedWhenFrozen;
  warningTitleForCall: typeof import("../src/ui/app.js").warningTitleForCall;
} | null = null;

function withDom<T>(
  run: (env: {
    body: FakeElement;
    win: any;
    ui: NonNullable<typeof cachedUi>;
  }) => Promise<T> | T,
): Promise<T> {
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
  const winListeners = new Map<string, Array<{ fn: (e: any) => void; capture: boolean }>>();
  const win: any = {
    devicePixelRatio: 1,
    addEventListener(type: string, fn: (e: any) => void, options?: boolean | { capture?: boolean }) {
      const capture = typeof options === "boolean" ? options : Boolean(options?.capture);
      if (!winListeners.has(type)) winListeners.set(type, []);
      winListeners.get(type)!.push({ fn, capture });
    },
    removeEventListener(type: string, fn: (e: any) => void, options?: boolean | { capture?: boolean }) {
      const capture = typeof options === "boolean" ? options : Boolean(options?.capture);
      const list = winListeners.get(type);
      if (!list) return;
      winListeners.set(
        type,
        list.filter((entry) => entry.fn !== fn || entry.capture !== capture),
      );
    },
    _dispatchCapture(event: any, isImmediateStopped: () => boolean) {
      for (const entry of [...(winListeners.get(event.type) ?? [])]) {
        if (!entry.capture) continue;
        entry.fn(event);
        if (isImmediateStopped()) break;
      }
    },
    _dispatchBubble(event: any, isImmediateStopped: () => boolean) {
      for (const entry of [...(winListeners.get(event.type) ?? [])]) {
        if (entry.capture) continue;
        entry.fn(event);
        if (isImmediateStopped()) break;
      }
    },
    dispatchEvent(raw: Record<string, any>) {
      let immediate = false;
      const event = {
        target: body,
        preventDefault() {
          event.defaultPrevented = true;
        },
        stopPropagation() {},
        stopImmediatePropagation() {
          immediate = true;
        },
        defaultPrevented: false,
        ...raw,
      };
      win._dispatchCapture(event, () => immediate);
      if (!immediate) win._dispatchBubble(event, () => immediate);
    },
  };
  const doc: any = {
    documentElement: docEl,
    body,
    activeElement: null,
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
    },
    MutationObserver: class {
      observe() {}
    },
    HTMLInputElement: class {},
    HTMLTextAreaElement: class {},
  });
  return (async () => {
    if (!cachedUi) {
      const panels2 = await import("../src/ui/panels2.js");
      const app = await import("../src/ui/app.js");
      cachedUi = {
        messageBox: panels2.messageBox,
        createChrome: app.createChrome,
        createStore: app.createStore,
        defaultSaveName: app.defaultSaveName,
        isAllowedWhenFrozen: app.isAllowedWhenFrozen,
        warningTitleForCall: app.warningTitleForCall,
      };
    }
    return await run({ body, win, ui: cachedUi });
  })().finally(() => {
    Object.assign(globalThis, prev);
  });
}

await withDom(async () => {});

test("QColor::lightness、lighter、darker 与 colorButtonStyle 与 Qt 一致", () => {
  // qt3.css line 11: QColor(61, 142, 201).lighter(130) === #54b7ff
  const accent = { r: 61, g: 142, b: 201, a: 255 };
  assert.equal(colorLightness(accent), 131);
  assert.equal(toHexRgb(colorLighter(accent, 130)), "#54b7ff");

  // 边界明度 127 vs 128
  assert.equal(colorLightness("#7f7f7f"), 127);
  assert.equal(colorLightness("#808080"), 128);
  assert.equal(colorButtonColors("#7f7f7f").color, "white");
  assert.equal(colorButtonColors("#808080").color, "black");

  // 深色底（lightness < 128）：白字，hover 用 lighter(135)，pressed 用 lighter(165)
  const dark = parseColor("#204060");
  assert.equal(colorLightness(dark), 64);
  const darkBtn = colorButtonColors(dark);
  assert.equal(darkBtn.background, "#204060");
  assert.equal(darkBtn.color, "white");
  assert.equal(darkBtn.hoverBackground, toHexRgb(colorLighter(dark, 135)));
  assert.equal(darkBtn.pressedBackground, toHexRgb(colorLighter(dark, 165)));

  // 浅色底（lightness >= 128）：黑字，hover 用 darker(108)，pressed 用 darker(125)
  const light = parseColor("#d0e0f0");
  assert.equal(colorLightness(light), 224);
  const lightBtn = colorButtonColors(light);
  assert.equal(lightBtn.background, "#d0e0f0");
  assert.equal(lightBtn.color, "black");
  assert.equal(lightBtn.hoverBackground, toHexRgb(colorDarker(light, 108)));
  assert.equal(lightBtn.pressedBackground, toHexRgb(colorDarker(light, 125)));
});

test("messageBox：点击按钮、Enter 选缺省按钮、Esc 取消", async () => {
  await withDom(async ({ body, ui: { messageBox } }) => {
    const p1 = messageBox({ title: "Save Project", text: "Disk full", buttons: ["Retry", "Cancel"] });
    const dialog1 = body.querySelector(".qdialog")!;
    assert.ok(dialog1);
    assert.equal(dialog1.querySelector(".qdialog-caption")?.textContent, "Save Project");
    assert.equal(dialog1.querySelector(".qdialog-caption-text")?.textContent, "Disk full");
    const buttons1 = dialog1.querySelectorAll(".qbutton");
    assert.equal(buttons1.length, 2);
    assert.equal(buttons1[0].classList.contains("qdefault"), true);
    buttons1[1].click();
    assert.equal(await p1, "Cancel");
    assert.equal(body.querySelector(".qdialog"), null);

    const p2 = messageBox({ title: "Open Project", text: "Bad file", buttons: ["OK", "Cancel"] });
    body.querySelector(".qdialog")!.dispatchEvent({ type: "keydown", key: "Enter" });
    assert.equal(await p2, "OK");

    const p3 = messageBox({ title: "Open Project", text: "Bad file", buttons: ["OK"] });
    body.querySelector(".qdialog")!.dispatchEvent({ type: "keydown", key: "Escape" });
    assert.equal(await p3, null);
  });
});

test("文件操作失败标题映射与 createStore 弹窗警告", async () => {
  await withDom(async ({ body, ui: { warningTitleForCall, createStore } }) => {
    assert.equal(warningTitleForCall("project.open", { path: "a.anproj" }), "Open Project");
    assert.equal(warningTitleForCall("project.open", { path: "a.textureview", view: "child" }), "Open Texture View");
    assert.equal(warningTitleForCall("project.save", {}), "Save Project");
    assert.equal(warningTitleForCall("project.saveAs", { path: "a.anproj" }), "Save Project");
    assert.equal(warningTitleForCall("menu.trigger", { id: "file.save" }), "Save Project");
    assert.equal(warningTitleForCall("menu.trigger", { id: "file.texture.save" }), "Save Texture View");
    assert.equal(warningTitleForCall("file.texture", { kind: "open" }), "Open Texture View");
    assert.equal(warningTitleForCall("file.texture", { kind: "save" }), "Save Texture View");
    assert.equal(warningTitleForCall("file.texture", { kind: "saveAs" }), "Save Texture View");
    assert.equal(warningTitleForCall("file.texture", { kind: "exportImage" }), "Export Texture View Image");
    assert.equal(warningTitleForCall("file.import", { kind: "raster" }), "Import Raster");
    assert.equal(warningTitleForCall("file.import", { kind: "opentoonz" }), "Import OpenToonz Lines");
    assert.equal(warningTitleForCall("file.import", { kind: "csp" }), "Import Clip Studio Paint");
    assert.equal(warningTitleForCall("layer.add", { view: "main" }), null);
    assert.equal(warningTitleForCall("menu.trigger", { id: "edit.undo" }), null);

    const errors: unknown[] = [];
    const origError = console.error;
    console.error = (...args: unknown[]) => errors.push(args[0]);
    try {
      const store = createStore({
        onState() {},
        onImage() {},
        call: () => Promise.reject(new Error("Cannot write file")),
      });
      await store.call("layer.add", { view: "main" });
      assert.equal(body.querySelector(".qdialog"), null);
      assert.equal(errors.length, 1);

      await store.call("file.import", { kind: "opentoonz", paths: ["test.pli"] });
      const dlg = body.querySelector(".qdialog")!;
      assert.ok(dlg);
      assert.equal(dlg.querySelector(".qdialog-caption")?.textContent, "Import OpenToonz Lines");
      assert.equal(dlg.querySelector(".qdialog-caption-text")?.textContent, "Cannot write file");
      dlg.querySelector(".qbutton")!.click();
    } finally {
      console.error = origError;
    }
  });
});

test("保存框预填文件名规则与 pick_files 参数传递", async () => {
  await withDom(async ({ body, win, ui: { defaultSaveName, createStore, createChrome } }) => {
    assert.equal(defaultSaveName("project", {}), "");
    assert.equal(defaultSaveName("project", { currentFilePath: "C:/work/cut01.anproj" }), "cut01.anproj");
    assert.equal(defaultSaveName("project", { currentFilePath: "C:\\work\\legacy.json" }), "legacy.anproj");

    assert.equal(defaultSaveName("saveAs", {}), "texture.textureview");
    assert.equal(defaultSaveName("saveAs", { childFilePath: "D:/tex/cape.textureview" }), "cape.textureview");
    assert.equal(defaultSaveName("saveAs", { childFilePath: "D:/tex/cape.json" }), "cape.textureview");

    assert.equal(defaultSaveName("exportImage", {}), "texture.png");
    assert.equal(defaultSaveName("exportImage", { childFilePath: "D:/tex/cape.textureview" }), "cape.png");

    const invokes: Array<{ cmd: string; args: Record<string, any> }> = [];
    win.__TAURI__ = {
      core: {
        invoke: async (cmd: string, args: Record<string, any>) => {
          invokes.push({ cmd, args });
          return [];
        },
      },
    };
    const stateListeners = new Map<string, (v: any) => void>();
    const store = createStore({
      onState: (k: string, fn: (v: any) => void) => stateListeners.set(k, fn),
      onImage() {},
      call: async () => ({}),
    });
    const menuEl = new FakeElement("div");
    const statusEl = new FakeElement("div");
    body.append(menuEl, statusEl);
    createChrome(menuEl, statusEl, store);

    stateListeners.get("window")?.({ currentFilePath: "C:/shots/s01.anproj", childFilePath: "" });
    await stateListeners.get("event")?.({ name: "dialog.saveProjectAs" });
    assert.equal(invokes.at(-1)?.args.defaultName, "s01.anproj");

    await stateListeners.get("event")?.({ name: "menu.action", args: { id: "file.texture.saveAs" } });
    assert.equal(invokes.at(-1)?.args.defaultName, "texture.textureview");

    stateListeners.get("window")?.({ currentFilePath: "", childFilePath: "C:/shots/cloth.textureview" });
    await stateListeners.get("event")?.({ name: "menu.action", args: { id: "file.texture.exportImage" } });
    assert.equal(invokes.at(-1)?.args.defaultName, "cloth.png");
  });
});

test("Python 冻结界面：状态栏文字切换、拦截非 Python Debug 事件与恢复", async () => {
  await withDom(async ({ body, ui: { createStore, createChrome, isAllowedWhenFrozen } }) => {
    const stateListeners = new Map<string, (v: any) => void>();
    const store: any = createStore({
      onState: (k: string, fn: (v: any) => void) => stateListeners.set(k, fn),
      onImage() {},
      call: async () => ({}),
    });
    const menuEl = new FakeElement("div");
    const statusEl = new FakeElement("div");
    const toolsPanel = new FakeElement("div");
    toolsPanel.className = "qt-panel";
    const debugPanel = new FakeElement("div");
    debugPanel.className = "qt-panel";
    debugPanel.dataset.pythonDebug = "true";
    const debugBtn = new FakeElement("button");
    debugPanel.append(debugBtn);
    store.pythonDebugEl = debugPanel;
    body.append(menuEl, statusEl, toolsPanel, debugPanel);

    createChrome(menuEl, statusEl, store);
    stateListeners.get("window")?.({ status: "Ready" });
    // 初次收到 freeze: false 不应把状态栏改成 Python ready
    stateListeners.get("freeze")?.(false);
    assert.equal(statusEl.querySelector(".status-text")?.textContent, "Ready");

    // 冻结
    stateListeners.get("freeze")?.(true);
    assert.equal(statusEl.querySelector(".status-text")?.textContent, "Python busy...");
    assert.equal(menuEl.inert, true);
    assert.equal(toolsPanel.inert, true);
    assert.equal(debugPanel.inert, false);
    assert.equal(isAllowedWhenFrozen(toolsPanel, debugPanel), false);
    assert.equal(isAllowedWhenFrozen(debugBtn, debugPanel), true);

    // 冻结期间 window 更新不应覆盖 Python busy...
    stateListeners.get("window")?.({ status: "Old status" });
    assert.equal(statusEl.querySelector(".status-text")?.textContent, "Python busy...");

    // 冻结期间画板/面板事件被拦截，Python Debug 事件放行
    let boardDown = 0;
    toolsPanel.addEventListener("pointerdown", () => boardDown++);
    toolsPanel.dispatchEvent({ type: "pointerdown" });
    assert.equal(boardDown, 0);

    let debugClicked = 0;
    debugBtn.addEventListener("click", () => debugClicked++);
    debugBtn.click();
    assert.equal(debugClicked, 1);

    // 解除冻结
    stateListeners.get("freeze")?.(false);
    assert.equal(statusEl.querySelector(".status-text")?.textContent, "Python ready");
    assert.equal(menuEl.inert, false);
    assert.equal(toolsPanel.inert, false);
    toolsPanel.dispatchEvent({ type: "pointerdown" });
    assert.equal(boardDown, 1);
  });
});

test("无限画布隐藏双滚动条并占满，主画板单轴禁用仍显示滚动条", async () => {
  assert.equal(isUnboundedViewport({ h: { enabled: false }, v: { enabled: false } }), true);
  assert.equal(isUnboundedViewport({ h: { enabled: false }, v: { enabled: true } }), false);
  assert.equal(isUnboundedViewport({ h: { enabled: true }, v: { enabled: true } }), false);

  await withDom(async ({ body }) => {
    const root = new FakeElement("div");
    body.append(root);
    const central = createCentral(root, { onPage() {}, onScroll() {}, onPill() {} });

    // 无限画布：h、v 的 enabled 均为 false -> 两条都隐藏，画板占满
    central.h.set({ min: 0, max: 0, page: 100, value: 0, enabled: false });
    central.v.set({ min: 0, max: 0, page: 100, value: 0, enabled: false });
    assert.equal(central.h.el.style.display, "none");
    assert.equal(central.v.el.style.display, "none");
    assert.equal(central.host.style.right, "0");
    assert.equal(central.host.style.bottom, "0");

    // 主画板只有一条 enabled 为 false：两条仍然显示
    central.h.set({ min: 0, max: 0, page: 100, value: 0, enabled: false });
    central.v.set({ min: -200, max: 200, page: 100, value: 0, enabled: true });
    assert.equal(central.h.el.style.display, "");
    assert.equal(central.v.el.style.display, "");
    assert.equal(central.h.el.classList.contains("disabled"), true);
    assert.equal(central.v.el.classList.contains("disabled"), false);
  });
});

test("Options 的 color 控件与带 state.color 的 button 控件", async () => {
  await withDom(async ({ body }) => {
    const root = new FakeElement("div");
    body.append(root);
    const events: any[] = [];
    const panel = createToolOptions(root, {
      onOption: (e: any) => events.push(e),
      onPalette() {},
      onNewColor() {},
      onSubWindow: () => null,
    });
    panel.build({
      controls: [
        { type: "color", name: "ink", title: "Ink", value: "#80112233", row: 0, start_column: 0, end_column: 0 },
        {
          type: "button",
          name: "tint_btn",
          title: "Tint",
          value: "go",
          state: { color: "#204060" },
          row: 1,
          start_column: 0,
          end_column: 0,
        },
      ],
    });

    // 1. 色块按不透明画，title 显示 #aarrggbb
    const swatch = root.querySelector(".opt-swatch")!;
    assert.ok(swatch);
    assert.equal(swatch.style.background, "#112233");
    assert.equal(swatch.title, "#80112233");

    // 点色块弹取色窗（pickersOnly 只有 Circle / HSV / RGB 三个 tab），点 Cancel 不改不发
    swatch.click();
    let dlg = body.querySelector(".newcolor-dialog")!;
    assert.ok(dlg);
    assert.equal(dlg.querySelectorAll(".pal-tab").length, 3);
    const cancelBtn = dlg.querySelectorAll(".qbutton").find((b) => b.textContent === "Cancel")!;
    cancelBtn.click();
    assert.equal(body.querySelector(".newcolor-dialog"), null);
    assert.equal(events.length, 0);
    assert.equal(swatch.title, "#80112233");

    // 再次打开并在 RGB 页拖动改变颜色和透明度，点 OK 才发
    swatch.click();
    dlg = body.querySelector(".newcolor-dialog")!;
    const tabs = dlg.querySelectorAll(".pal-tab");
    tabs[2].click(); // RGB picker (含 R, G, B, A 四条滑轨)
    const rgbCanvas = dlg.querySelectorAll(".pal-picker")[2];
    // 第 4 行（y = 3 * 33 + 12 = 111）为 Alpha 滑轨，设到约一半
    rgbCanvas.dispatchEvent({ type: "pointerdown", button: 0, offsetX: 100, offsetY: 111, pointerId: 1 });
    rgbCanvas.dispatchEvent({ type: "pointerup", button: 0, offsetX: 100, offsetY: 111, pointerId: 1 });
    assert.equal(events.length, 0); // 确定前不发送
    const okBtn = dlg.querySelectorAll(".qbutton").find((b) => b.textContent === "OK")!;
    okBtn.click();
    assert.equal(events.length, 1);
    assert.equal(events[0].type, "color");
    assert.match(events[0].value, /^#[0-9a-f]{2}112233$/);
    assert.notEqual(events[0].value, "#80112233");
    assert.equal(swatch.style.background, "#112233");
    assert.equal(swatch.title, events[0].value);

    // 2. 带 state.color 的按钮：文字颜色、悬停底色、按下底色
    const btn = root.querySelector(".opt-button")!;
    const expected = colorButtonColors("#204060");
    assert.equal(btn.style.color, "white");
    assert.equal(btn.style.background, expected.background);
    btn.dispatchEvent({ type: "pointerenter" });
    assert.equal(btn.style.background, expected.hoverBackground);
    btn.dispatchEvent({ type: "pointerdown", button: 0 });
    assert.equal(btn.style.background, expected.pressedBackground);
    btn.dispatchEvent({ type: "pointerup", button: 0 });
    assert.equal(btn.style.background, expected.hoverBackground);
    btn.dispatchEvent({ type: "pointerleave" });
    assert.equal(btn.style.background, expected.background);
  });
});
