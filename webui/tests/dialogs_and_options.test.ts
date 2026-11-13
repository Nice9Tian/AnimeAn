import assert from "node:assert/strict";
import { test } from "node:test";
import {
  colorButtonColors,
  colorDarker,
  colorLighter,
  colorLightness,
  createPalette,
  parseColor,
  toHexRgb,
} from "../src/ui/palette.js";
import { createToolOptions } from "../src/ui/options.js";
import { askForMenu, createCentral, isUnboundedViewport, trackPress } from "../src/ui/panels.js";

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
  replaceWith(node: FakeElement) {
    const siblings = this.parentElement!.children;
    siblings[siblings.indexOf(this)] = node;
    node.parentElement = this.parentElement;
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
    if (selector.startsWith(":scope > ")) {
      const rest = selector.slice(":scope > ".length);
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
  isAllowedWhenFrozen: typeof import("../src/ui/app.js").isAllowedWhenFrozen;
  renderDialog: typeof import("../src/ui/dialogwindow.js").renderDialog;
  createDialogHost: typeof import("../src/ui/dialoghost.js").createDialogHost;
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
      const dialogWindow = await import("../src/ui/dialogwindow.js");
      const dialogHost = await import("../src/ui/dialoghost.js");
      cachedUi = {
        messageBox: panels2.messageBox,
        createChrome: app.createChrome,
        createStore: app.createStore,
        isAllowedWhenFrozen: app.isAllowedWhenFrozen,
        renderDialog: dialogWindow.renderDialog,
        createDialogHost: dialogHost.createDialogHost,
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

const tick = () => new Promise((r) => setTimeout(r, 0));

function recordingCall() {
  const calls: Array<{ method: string; params: any }> = [];
  const call = (method: string, params: any) => {
    calls.push({ method, params });
    return Promise.resolve({});
  };
  return { calls, call };
}

test("a failed call is only logged: telling the user is Python's", async () => {
  await withDom(async ({ body, ui: { createStore } }) => {
    const errors: unknown[] = [];
    const origError = console.error;
    console.error = (...args: unknown[]) => errors.push(args[0]);
    try {
      const store = createStore({
        onState() {},
        onImage() {},
        call: () => Promise.reject(new Error("Cannot write file")),
      });
      await store.call("file.import", { kind: "opentoonz", paths: ["test.pli"] });
      await store.call("menu.trigger", { id: "file.save" });
      assert.equal(body.querySelector(".qdialog"), null, "the page opens no dialog of its own");
      assert.equal(errors.length, 2);
    } finally {
      console.error = origError;
    }
  });
});

test("a declared message fills its window and answers with the button the user pressed", async () => {
  await withDom(async ({ body, ui: { renderDialog } }) => {
    const { calls, call } = recordingCall();
    const drags: unknown[] = [];
    const root = new FakeElement("div");
    body.append(root);
    const view = renderDialog(root as any, {
      id: "d1", kind: "message", title: "Save Project As",
      text: "a.anproj already exists.\nDo you want to replace it?", buttons: ["Yes", "No"],
    }, { call, onTitleDrag: (e: unknown) => drags.push(e) });
    assert.equal(body.querySelector(".qdialog-shade"), null, "no shade: the frame is the window");
    assert.ok((view.el as any)?.classList.contains("in-window"));
    assert.equal(root.querySelector(".qdialog-caption")?.textContent, "Save Project As");
    const head = root.querySelector(".qdialog-title")!;
    head.dispatchEvent({ type: "pointerdown" });
    assert.equal(drags.length, 1, "the title bar moves the window");
    root.querySelectorAll(".qbutton")[1].click();
    await tick();
    assert.deepEqual(calls, [{ method: "dialog.result", params: { id: "d1", action: "button", value: "No" } }]);
    assert.ok(root.querySelector(".qdialog"), "it stays until its window closes");

    calls.length = 0;
    const second = new FakeElement("div");
    body.append(second);
    renderDialog(second as any, { id: "d2", kind: "message", title: "Open Project", text: "Bad file", buttons: ["OK"] }, { call });
    second.querySelector(".qdialog")!.dispatchEvent({ type: "keydown", key: "Escape" });
    await tick();
    assert.deepEqual(calls, [{ method: "dialog.result", params: { id: "d2", action: "cancel" } }]);
  });
});

test("the canvas size, colour and settings dialogs answer through dialog.result", async () => {
  await withDom(async ({ body, ui: { renderDialog } }) => {
    const { calls, call } = recordingCall();
    const sizeRoot = new FakeElement("div");
    body.append(sizeRoot);
    renderDialog(sizeRoot as any, { id: "d3", kind: "canvasSize", title: "New Project", width: 1920, height: 1080 }, { call });
    const create = sizeRoot.querySelectorAll(".qbutton").find((b) => b.textContent === "Create")!;
    create.click();
    create.click();
    assert.deepEqual(calls, [{ method: "dialog.result", params: { id: "d3", action: "accept", value: { width: 1920, height: 1080 } } }],
      "Create answers once, with the size");

    calls.length = 0;
    const colorRoot = new FakeElement("div");
    body.append(colorRoot);
    renderDialog(colorRoot as any, { id: "d4", kind: "color", title: "New Color", value: "#ff336699", accept: "Add" }, { call });
    assert.equal(colorRoot.querySelector(".qdialog-caption")?.textContent, "New Color");
    assert.equal(colorRoot.querySelectorAll(".pal-tab").length, 3, "the pickers, without the box");
    const buttons = colorRoot.querySelectorAll(".qbutton");
    assert.deepEqual(buttons.map((b) => b.textContent), ["Add", "Cancel"], "the confirm button says what Python asked");
    buttons[0].click();
    buttons[1].click();
    assert.deepEqual(calls, [{ method: "dialog.result", params: { id: "d4", action: "accept", value: "#ff336699" } }]);

    calls.length = 0;
    const settingsRoot = new FakeElement("div");
    body.append(settingsRoot);
    const layout = { controls: [{ type: "color", name: "axis", hook: "axis_color", title: "Axis", value: "#ff102030", row: 1, start_column: 0, end_column: 0 }] };
    const view = renderDialog(settingsRoot as any, { id: "d5", kind: "settings", title: "Line Display Settings", name: "line_settings", layout }, { call });
    settingsRoot.querySelector(".opt-swatch")!.click();
    assert.deepEqual(calls, [{ method: "dialog.request", params: {
      kind: "color", hook: "axis_color", name: "axis", title: "Axis", value: "#ff102030", row: 1, startColumn: 0, endColumn: 0,
      menu: "line_settings" } }], "a swatch asks Python for the colour dialog, and says which window it is in");
    const swatch = settingsRoot.querySelector(".opt-swatch");
    view.update({ layout });
    assert.equal(settingsRoot.querySelector(".opt-swatch"), swatch, "the same layout is not rebuilt");
    view.update({ layout: { controls: [{ ...layout.controls[0], value: "#ff445566" }] } });
    assert.equal(settingsRoot.querySelector(".opt-swatch")?.title, "#ff445566", "a new layout shows the colour Python chose");
    calls.length = 0;
    settingsRoot.querySelectorAll(".qbutton").find((b) => b.textContent === "Close")!.click();
    assert.deepEqual(calls, [{ method: "dialog.result", params: { id: "d5", action: "cancel" } }]);

    calls.length = 0;
    renderDialog(new FakeElement("div") as any, { id: "d6", kind: "nothing" }, { call });
    assert.equal(calls[0]?.params.action, "error", "a kind the page cannot draw is an error, so C++ drops it");
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

test("Windows 菜单开关的每个面板都跟着 windows 状态显示和隐藏", async () => {
  await withDom(async () => {
    const { syncDocks } = await import("../src/ui/app.js");
    const shown = new Map<string, boolean>();
    let onWindows: (value: unknown) => void = () => {};
    syncDocks(
      { dockSetVisible: (dock: string, visible: boolean) => shown.set(dock, visible) },
      { on: (_key: string, fn: (value: unknown) => void) => { onWindows = fn; } },
    );
    onWindows({ tools: { visible: true }, tool_options: { visible: true }, layers: { visible: false }, timeline: { visible: true } });
    assert.equal(shown.get("toolsDock"), true);
    assert.equal(shown.get("toolOptionsDock"), true);
    assert.equal(shown.get("layersDock"), false);
    assert.equal(shown.get("timelineDock"), true);
  });
});

test("Options 里的纹理板行吃掉面板剩下的高度，没有这一行时不留空", async () => {
  await withDom(async ({ body }) => {
    const root = new FakeElement("div");
    body.append(root);
    const panel = createToolOptions(root, {
      onOption() {},
      onSubWindow: () => new FakeElement("div"),
    });
    const grid = root.querySelector(".opt-grid")!;
    panel.build({
      controls: [
        { type: "button", name: "a", title: "A", row: 0, start_column: 0, end_column: 0 },
        { type: "button", name: "b", title: "B", row: 1, start_column: 0, end_column: 0 },
        { type: "subwindow", name: "texture_view", title: "Texture", row: 2, start_column: 0, end_column: 2 },
      ],
    });
    assert.equal(grid.classList.contains("has-subwindow"), true);
    assert.equal(grid.style.gridTemplateRows, "repeat(2, auto) minmax(min-content, 1fr)");

    panel.build({ controls: [{ type: "button", name: "a", title: "A", row: 0, start_column: 0, end_column: 0 }] });
    assert.equal(grid.classList.contains("has-subwindow"), false);
    assert.equal(grid.style.gridTemplateRows, "");
  });
});

test("Options 的 color 控件与带 state.color 的 button 控件", async () => {
  await withDom(async ({ body }) => {
    const root = new FakeElement("div");
    body.append(root);
    const events: any[] = [];
    const asked: any[] = [];
    const panel = createToolOptions(root, {
      onOption: (e: any) => events.push(e),
      onDialog: (request: any) => asked.push(request),
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

    // 点色块不在页面里弹取色窗：请 Python 开（dialogs.py），选好的颜色作为这个控件的选项回来
    swatch.click();
    assert.equal(body.querySelector(".qdialog"), null);
    assert.deepEqual(asked, [{ kind: "color", hook: "ink", name: "ink", title: "Ink", value: "#80112233", row: 0, startColumn: 0, endColumn: 0 }]);
    assert.equal(events.length, 0);

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

test("菜单标题没变就不重建菜单栏：鼠标下换了元素会再次打开菜单，一直刷新", async () => {
  await withDom(async ({ body }) => {
    const { createMenuBar } = await import("../src/ui/panels.js");
    const root = new FakeElement("div");
    body.append(root);
    const bar = createMenuBar(root, { onTrigger() {}, onOpen() {} });
    bar.update([{ id: "file", text: "File", children: [] }, { id: "edit", text: "Edit", children: [] }]);
    const items = root.querySelectorAll(".item");
    assert.equal(items.length, 2);
    bar.update([{ id: "file", text: "File", children: [{ id: "x", text: "X" }] }, { id: "edit", text: "Edit", children: [] }]);
    assert.equal(root.querySelectorAll(".item")[0], items[0]);
    bar.update([{ id: "file", text: "File", children: [] }]);
    assert.equal(root.querySelectorAll(".item").length, 1);
  });
});

test("打开的菜单把占的矩形报给外壳：打开、子菜单展开收起、关闭各报一次，没变不报", async () => {
  await withDom(async ({ body }) => {
    const { onMenuArea, popupMenu } = await import("../src/ui/panels.js");
    const reports: number[][][] = [];
    const settle = () => new Promise((r) => setTimeout(r, 0));
    onMenuArea((rects: number[][]) => reports.push(rects));
    try {
      const picked: string[] = [];
      popupMenu(
        [
          { id: "a", text: "A" },
          { id: "s", text: "Sub", kind: "submenu", children: [{ id: "b", text: "B" }] },
        ],
        10,
        20,
        (item: { id: string }) => picked.push(item.id),
      );
      await settle();
      assert.equal(reports.length, 1, "opening reports once, not once per step");
      assert.equal(reports[0].length, 1);
      assert.deepEqual(reports[0][0], [0, 0, 296, 200], "[x, y, width, height] in client pixels");

      const [rowA, rowSub] = body.querySelectorAll(".qmenu-item");
      rowSub.dispatchEvent({ type: "pointerenter" });
      await settle();
      assert.equal(reports.length, 2);
      assert.equal(reports[1].length, 2, "the submenu is reported with its menu");

      rowSub.dispatchEvent({ type: "pointerenter" });
      await settle();
      assert.equal(reports.length, 2, "the same boxes are not reported again");

      rowA.dispatchEvent({ type: "pointerenter" });
      await settle();
      assert.equal(reports.length, 3);
      assert.equal(reports[2].length, 1, "leaving the submenu row closes it");

      rowA.click();
      await settle();
      assert.deepEqual(picked, ["a"]);
      assert.equal(body.querySelector(".qmenu"), null);
      assert.deepEqual(reports.at(-1), [], "closing reports no boxes");
    } finally {
      onMenuArea(null);
    }
  });
});

test("点开菜单先调 menu.refresh 获取最新勾选状态再显示弹窗", async () => {
  await withDom(async ({ body, ui: { createStore, createChrome } }) => {
    const stateListeners = new Map<string, (v: any) => void>();
    const calls: Array<{ method: string; params: any }> = [];
    const store = createStore({
      onState: (k: string, fn: (v: any) => void) => stateListeners.set(k, fn),
      onImage() {},
      call: async (method: string, params: any) => {
        calls.push({ method, params });
        if (method === "menu.refresh") {
          return {
            menus: [
              {
                id: "script_menu",
                text: "Scripts",
                kind: "submenu",
                children: [{ id: "script:main:m:item1", text: "Toggle Hook", kind: "check", checked: true }],
              },
            ],
          };
        }
        return {};
      },
    });
    const menuEl = new FakeElement("div");
    const statusEl = new FakeElement("div");
    body.append(menuEl, statusEl);
    createChrome(menuEl, statusEl, store);

    stateListeners.get("menus")?.([
      {
        id: "script_menu",
        text: "Scripts",
        kind: "submenu",
        children: [{ id: "script:main:m:item1", text: "Toggle Hook", kind: "check", checked: false }],
      },
    ]);

    const items = menuEl.querySelectorAll(".item");
    assert.ok(items.length >= 1);
    items[0].click();
    await new Promise((r) => setTimeout(r, 0));

    assert.equal(calls[0]?.method, "menu.refresh");
    const popup = body.querySelector(".qmenu")!;
    assert.ok(popup);
    assert.equal(popup.querySelector(".qmenu-mark")?.textContent, "✓");
  });
});

function fakeShell(picked: string[] = []) {
  const log: string[] = [];
  const picks: any[] = [];
  let closeRequested: ((label: string) => void) | null = null;
  return {
    log,
    picks,
    openDialogWindow: async (args: any) => { log.push(`open ${args.label} owner=${args.owner} title=${args.title} url=${args.url}`); },
    closeDialogWindow: async (label: string) => { log.push(`close ${label}`); },
    setWindowEnabled: async (label: string, enabled: boolean) => { log.push(`${enabled ? "enable" : "disable"} ${label}`); },
    setPanelsFocusable: async (focusable: boolean) => { log.push(focusable ? "focusable panels" : "unfocusable panels"); },
    focusModal: async () => { log.push("focus"); },
    pickFiles: async (args: any) => { picks.push(args); return picked; },
    onDialogCloseRequested: (fn: (label: string) => void) => { closeRequested = fn; },
    requestClose: (label: string) => closeRequested?.(label),
  };
}

function fakeStore() {
  const listeners = new Map<string, Array<(v: any) => void>>();
  const calls: Array<{ method: string; params: any }> = [];
  let disconnected: (() => void) | null = null;
  return {
    calls,
    on: (key: string, fn: (v: any) => void) => {
      if (!listeners.has(key)) listeners.set(key, []);
      listeners.get(key)!.push(fn);
    },
    call: (method: string, params: any) => { calls.push({ method, params }); return Promise.resolve({}); },
    emit: (key: string, value: any) => listeners.get(key)?.forEach((fn) => fn(value)),
    onDisconnect: (fn: () => void) => { disconnected = fn; },
    disconnect: () => disconnected?.(),
  };
}

const sizeDialog = { id: "d1", kind: "canvasSize", modal: true, title: "New Project", width: 1280, height: 720 };
const colorOver = { id: "d2", kind: "color", modal: true, title: "Select Color", value: "#ff000000" };

test("the main page opens a window per dialog and locks the others, owner first", async () => {
  await withDom(async ({ ui: { createDialogHost } }) => {
    const shell = fakeShell();
    const store = fakeStore();
    const host = createDialogHost(store, { role: "main", shell, channel: null });
    store.emit("dialogs", [sizeDialog]);
    await host.settled();
    assert.deepEqual(shell.log, [
      "open dialog-d1 owner=main title=New Project url=dialog.html?id=d1&owner=main",
      "unfocusable panels",
      "disable main",
    ]);
    assert.equal(host.isBlocked(), true, "the main page swallows input under a modal dialog");

    shell.log.length = 0;
    store.emit("dialogs", [sizeDialog, colorOver]);
    await host.settled();
    assert.deepEqual(shell.log, [
      "open dialog-d2 owner=dialog-d1 title=Select Color url=dialog.html?id=d2&owner=dialog-d1",
      "disable dialog-d1",
    ], "a dialog over a dialog is owned by it, and the one below is locked");

    shell.log.length = 0;
    store.emit("dialogs", [sizeDialog]);
    await host.settled();
    assert.deepEqual(shell.log, [
      "enable main", "enable dialog-d1", "focusable panels",
      "close dialog-d2",
      "unfocusable panels", "disable main",
    ], "nothing stays locked while a window closes, or Windows hands the focus to another program");

    shell.log.length = 0;
    store.emit("dialogs", []);
    await host.settled();
    assert.deepEqual(shell.log, ["enable main", "focusable panels", "close dialog-d1"]);
    assert.equal(host.isBlocked(), false);
  });
});

test("dialogs that lose the one under them are opened again under the new owner", async () => {
  await withDom(async ({ ui: { createDialogHost } }) => {
    const shell = fakeShell();
    const store = fakeStore();
    const host = createDialogHost(store, { role: "main", shell, channel: null });
    const settingsBelow = { id: "d1", kind: "settings", modal: true, title: "Line Display Settings", name: "line_settings", layout: {} };
    const note = { id: "d3", kind: "message", modal: true, title: "Note", text: "x", buttons: ["OK"] };
    store.emit("dialogs", [settingsBelow, colorOver, note]);
    await host.settled();
    shell.log.length = 0;
    // The settings page failed: C++ dropped it, the two above it stay.
    store.emit("dialogs", [colorOver, note]);
    await host.settled();
    assert.deepEqual(shell.log, [
      "enable main", "enable dialog-d1", "enable dialog-d2", "focusable panels",
      "close dialog-d3", "close dialog-d2",
      "open dialog-d2 owner=main title=Select Color url=dialog.html?id=d2&owner=main",
      "open dialog-d3 owner=dialog-d2 title=Note url=dialog.html?id=d3&owner=dialog-d2",
      "close dialog-d1",
      "unfocusable panels", "disable dialog-d2", "disable main",
    ], "Windows would destroy them with their owner, and main would stay locked with nothing on screen");

    shell.log.length = 0;
    store.emit("dialogs", [colorOver]);
    await host.settled();
    assert.deepEqual(shell.log, [
      "enable main", "enable dialog-d2", "focusable panels",
      "close dialog-d3",
      "unfocusable panels", "disable main",
    ], "the top one going moves nothing");
  });
});

test("a native picker is the shell's, a close from Windows is an answer, and a lost backend unlocks", async () => {
  await withDom(async ({ ui: { createDialogHost } }) => {
    const shell = fakeShell(["C:/shots/s01.anproj"]);
    const store = fakeStore();
    const host = createDialogHost(store, { role: "main", shell, channel: null });
    const picker = {
      id: "d7", kind: "file", modal: true, title: "Save Project As", mode: "save",
      filters: [{ name: "AnimeAn Projects", extensions: ["anproj"] }], multiple: false, defaultName: "s01.anproj",
    };
    store.emit("dialogs", [picker]);
    await host.settled();
    await tick();
    assert.deepEqual(shell.picks, [{ mode: "save", title: "Save Project As", filters: [{ name: "AnimeAn Projects", extensions: ["anproj"] }], multiple: false, defaultName: "s01.anproj" }]);
    assert.deepEqual(store.calls, [{ method: "dialog.result", params: { id: "d7", action: "accept", value: ["C:/shots/s01.anproj"] } }]);
    assert.deepEqual(shell.log, ["unfocusable panels"], "the picker disables the main window itself");

    store.calls.length = 0;
    shell.log.length = 0;
    store.emit("dialogs", [sizeDialog]);
    await host.settled();
    shell.requestClose("dialog-d1");
    assert.deepEqual(store.calls, [{ method: "dialog.result", params: { id: "d1", action: "closed" } }]);
    assert.equal(shell.log.includes("close dialog-d1"), false, "the window waits for the state");

    shell.log.length = 0;
    store.disconnect();
    await host.settled();
    assert.deepEqual(shell.log, ["enable main", "focusable panels", "close dialog-d1"], "no backend: nothing stays open or locked");
    assert.equal(host.isBlocked(), false);
  });
});

test("a dialog window that never shows itself is reported, so C++ drops it", async () => {
  await withDom(async ({ ui: { createDialogHost } }) => {
    const shell = fakeShell();
    const store = fakeStore();
    let onMessage: ((e: any) => void) | null = null;
    const channel = { addEventListener: (_type: string, fn: (e: any) => void) => { onMessage = fn; } };
    const host = createDialogHost(store, { role: "main", shell, channel, showTimeoutMs: 5 });
    store.emit("dialogs", [sizeDialog, { ...colorOver, id: "d9" }]);
    await host.settled();
    onMessage!({ data: { type: "shown", id: "d1" } });
    await new Promise((r) => setTimeout(r, 30));
    assert.deepEqual(store.calls, [{ method: "dialog.result", params: { id: "d9", action: "error", value: "The dialog window did not show." } }]);
  });
});

test("panel and dialog pages swallow input under a modal dialog and bring it forward when pressed", async () => {
  await withDom(async ({ body, win, ui: { createDialogHost } }) => {
    const shell = fakeShell();
    const store = fakeStore();
    const host = createDialogHost(store, { role: "satellite", shell });
    const below = createDialogHost(store, { role: "dialog", ownId: "d1", shell });
    let keys = 0;
    win.addEventListener("keydown", () => keys++);
    store.emit("dialogs", [sizeDialog]);
    assert.equal(host.isBlocked(), true);
    assert.equal(below.isBlocked(), false, "the dialog itself takes input");
    win.dispatchEvent({ type: "keydown", key: "a" });
    assert.equal(keys, 0);
    win.dispatchEvent({ type: "pointerdown" });
    await tick();
    assert.deepEqual(shell.log, ["focus"]);

    store.emit("dialogs", [sizeDialog, colorOver]);
    assert.equal(below.isBlocked(), true, "a modal dialog above it blocks it");
    store.emit("dialogs", [sizeDialog, { ...colorOver, modal: false }]);
    assert.equal(below.isBlocked(), false, "a modeless one does not");
    store.emit("dialogs", []);
    assert.equal(host.isBlocked(), false);
    win.dispatchEvent({ type: "keydown", key: "a" });
    assert.equal(keys, 1);
    assert.equal(body.querySelector(".modal-blocker"), null);
  });
});

test("without the shell the dialogs are drawn over the main page and pickers answer Cancel", async () => {
  await withDom(async ({ body, ui: { createDialogHost } }) => {
    const store = fakeStore();
    const host = createDialogHost(store, { role: "main", shell: null, channel: null });
    store.emit("dialogs", [{ id: "d1", kind: "message", modal: true, title: "Open Project", text: "Bad file", buttons: ["OK"] }]);
    await host.settled();
    assert.equal(body.querySelectorAll(".qdialog-shade").length, 1);
    assert.equal(body.querySelector(".qdialog-caption")?.textContent, "Open Project");
    store.emit("dialogs", [{ id: "d8", kind: "file", modal: true, title: "Open Project", mode: "open", filters: [] }]);
    await host.settled();
    await tick();
    assert.equal(body.querySelector(".qdialog-shade"), null);
    assert.deepEqual(store.calls.at(-1), { method: "dialog.result", params: { id: "d8", action: "cancel", value: [] } });
  });
});

test("Settings comes from the backend's menus, and a choice Python broadcasts is drawn here", async () => {
  await withDom(async ({ body, ui: { createStore, createChrome } }) => {
    const i18n = await import("../src/i18n/index.js");
    const stateListeners = new Map<string, (v: any) => void>();
    const calls: Array<{ method: string; params: any }> = [];
    const store: any = createStore({
      onState: (k: string, fn: (v: any) => void) => stateListeners.set(k, fn),
      onImage() {},
      call: async (method: string, params: any) => { calls.push({ method, params }); return {}; },
    });
    const menuEl = new FakeElement("div");
    const statusEl = new FakeElement("div");
    body.append(menuEl, statusEl);
    createChrome(menuEl, statusEl, store, { shell: null });
    stateListeners.get("menus")?.([
      { id: "file", text: "File", kind: "submenu", children: [] },
      { id: "Settings", text: "Settings", kind: "submenu", children: [] },
    ]);
    assert.deepEqual(menuEl.querySelectorAll(".item").map((item) => item.textContent), ["File", "Settings"],
      "the page adds no menu of its own");
    try {
      stateListeners.get("py/preferences")?.({ renderer: "canvas2d", language: "ja" });
      assert.equal(i18n.language(), "ja", "the language Python broadcast is applied");
    } finally {
      i18n.setLanguage("en");
    }
    stateListeners.get("window")?.({ canvasWidth: 1280, canvasHeight: 720 });
    assert.ok(calls.some((c) => c.method === "tool.option" && c.params.hook === "ui_renderer" && c.params.name === "webgl"),
      "Python hears whether WebGL works");
    assert.equal(body.querySelector(".qdialog"), null, "and no canvas size dialog of the page's own");
  });
});
test("the board moving in or out of its cell leaves the controls the user has touched alone", async () => {
  await withDom(async ({ body }) => {
    const root = new FakeElement("div");
    body.append(root);
    const cells: FakeElement[] = [];
    const panel = createToolOptions(root, {
      onOption() {},
      onSubWindow: () => {
        const cell = new FakeElement("div");
        cells.push(cell);
        return cell;
      },
    });
    const grid = root.querySelector(".opt-grid")!;
    panel.build({
      controls: [
        { type: "check", name: "fine", title: "Fine", value: true, row: 0, start_column: 0, end_column: 0 },
        { type: "subwindow", name: "texture_view", row: 1, start_column: 0, end_column: 0 },
      ],
    });
    const control = grid.children[0];
    panel.refreshSubWindows();
    assert.equal(cells.length, 2);
    assert.equal(grid.children[0], control, "only the cell of the board is replaced");
    assert.equal(grid.children[1], cells[1]);
    assert.equal(cells[1].style.gridRow, "2");
  });
});

test("New Color reaches the draw layer before the box hears of it", async () => {
  await withDom(async () => {
    const events: string[] = [];
    const palette = createPalette((kind: string) => events.push(kind));
    palette.addSwatch("#ff112233");
    assert.deepEqual(events, ["color", "box"]);
    palette.addSwatch("#ff112233");
    assert.deepEqual(events, ["color", "box", "color"], "a swatch already in the box is not added twice");
  });
});

test("the palette's + asks palette_box for New Color on the colour shown, and opens nothing itself", async () => {
  await withDom(async ({ body }) => {
    const root = new FakeElement("div");
    body.append(root);
    const events: any[] = [];
    createToolOptions(root, { onOption: (e: any) => events.push(e), onSubWindow: () => null }).build({
      controls: [{ type: "palette", name: "palette", hook: "color", value: "#ff336699", swatches: ["#ff000000"], row: 0, start_column: 0, end_column: 2 }],
    });
    await tick();
    root.querySelector(".pal-plus")!.click();
    assert.equal(body.querySelector(".qdialog"), null);
    assert.deepEqual(events, [{ hook: "palette_box", name: "palette", type: "palette", value: "new:#ff336699", row: 0, startColumn: 0, endColumn: 2 }]);
  });
});

test("a colour picked in the dialog window's RGB page is what Add sends", async () => {
  await withDom(async ({ body, ui: { renderDialog } }) => {
    const { calls, call } = recordingCall();
    const root = new FakeElement("div");
    body.append(root);
    renderDialog(root as any, { id: "d4", kind: "color", title: "Select Color", value: "#80112233", accept: "OK" }, { call });
    root.querySelectorAll(".pal-tab")[2].click(); // RGB: R, G, B and A rails
    const rgb = root.querySelectorAll(".pal-picker")[2];
    // The fourth rail (y = 3 * 33 + 12 = 111) is alpha: drag it to about half.
    rgb.dispatchEvent({ type: "pointerdown", button: 0, offsetX: 100, offsetY: 111, pointerId: 1 });
    rgb.dispatchEvent({ type: "pointerup", button: 0, offsetX: 100, offsetY: 111, pointerId: 1 });
    assert.equal(calls.length, 0, "nothing goes out before OK");
    root.querySelectorAll(".qbutton").find((b) => b.textContent === "OK")!.click();
    assert.equal(calls.length, 1);
    assert.match(calls[0].params.value, /^#[0-9a-f]{2}112233$/);
    assert.notEqual(calls[0].params.value, "#80112233");
  });
});

test("a press ends when the browser takes the pointer back as well as on release", () => {
  for (const ending of ["pointerup", "pointercancel", "lostpointercapture"]) {
    const listeners = new Map<string, Set<(e?: unknown) => void>>();
    const el = {
      addEventListener(type: string, fn: (e?: unknown) => void) {
        if (!listeners.has(type)) listeners.set(type, new Set());
        listeners.get(type)!.add(fn);
      },
      removeEventListener(type: string, fn: (e?: unknown) => void) {
        listeners.get(type)?.delete(fn);
      },
    };
    let moves = 0;
    const fire = (type: string) => { for (const fn of [...(listeners.get(type) ?? [])]) fn(); };
    trackPress(el, () => { moves += 1; });
    fire("pointermove");
    fire(ending);
    fire("pointermove");
    assert.equal(moves, 1, ending);
    assert.equal([...listeners.values()].reduce((sum, set) => sum + set.size, 0), 0, "nothing stays attached");
  }
});

test("a reply that arrives after a press or a newer menu opens nothing", () => {
  const first = askForMenu();
  assert.equal(first(), true);
  const second = askForMenu();
  assert.equal(first(), false);
  assert.equal(second(), true);
});
