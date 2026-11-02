import assert from "node:assert/strict";
import { test } from "node:test";

function fakeElement(tag = "div") {
  const classes = new Set<string>();
  const listeners = new Map<string, Array<(...args: unknown[]) => void>>();
  const el: Record<string, any> = {
    tag,
    className: "",
    textContent: "",
    innerHTML: "",
    value: "",
    style: {},
    children: [] as any[],
    parentElement: null as any,
    clientWidth: 0,
    clientHeight: 0,
    classList: {
      add(...names: string[]) { for (const n of names) classes.add(n); },
      toggle(name: string, force?: boolean) {
        const on = force ?? !classes.has(name);
        if (on) classes.add(name); else classes.delete(name);
        return on;
      },
      contains(name: string) { return classes.has(name); },
    },
    append(...nodes: any[]) {
      for (const n of nodes) {
        if (n?.parentElement && n.parentElement !== el) {
          n.parentElement.children = n.parentElement.children.filter((c: any) => c !== n);
        }
        if (n && typeof n === "object") n.parentElement = el;
        if (!el.children.includes(n)) el.children.push(n);
      }
    },
    replaceChildren(...nodes: any[]) {
      for (const c of el.children) if (c && typeof c === "object") c.parentElement = null;
      el.children = [];
      el.append(...nodes);
    },
    querySelector: () => fakeElement("div"),
    addEventListener(type: string, fn: (...args: unknown[]) => void) {
      if (!listeners.has(type)) listeners.set(type, []);
      listeners.get(type)!.push(fn);
    },
    click() {
      for (const fn of listeners.get("click") ?? []) fn({});
    },
    matches: () => false,
    getContext: () => ({
      font: "",
      measureText: (t: string) => ({ width: t.length * 6 }),
      createImageData: (w: number, h: number) => ({ data: new Uint8ClampedArray(w * h * 4) }),
      putImageData() {},
    }),
    toDataURL: () => "data:image/png;base64,",
  };
  Object.defineProperty(el, "clientWidth", {
    get: () => ( String(el.className).includes("tl-bar") ? 800 : 0 ),
  });
  return el;
}

// timeline.js measures text at load; nothing here draws.
async function loadTimeline() {
  const had = { document: globalThis.document, window: globalThis.window };
  Object.assign(globalThis, {
    document: { createElement: (tag: string) => fakeElement(tag), documentElement: fakeElement("html") },
    window: { addEventListener() {}, devicePixelRatio: 1 },
  });
  try {
    return await import("../src/ui/timeline.js");
  } finally {
    Object.assign(globalThis, had);
  }
}

function fakeRoot(children: object[]) {
  const root = {
    children,
    moves: 0,
    replaceChildren(...next: object[]) {
      root.children = next;
      root.moves++;
    },
  };
  return root;
}

test("排列没变就不动子元素：按住的按钮不能被摘下，否则这次点击会丢", async () => {
  const { arrange } = await loadTimeline();
  const bar = {}, strip = {};
  const root = fakeRoot([bar, strip]);
  for (let frame = 0; frame < 12; frame++) arrange(root, [bar, strip]);
  assert.equal(root.moves, 0);
});

test("排列真的变了才重排", async () => {
  const { arrange } = await loadTimeline();
  const bar = {}, strip = {}, side = {};
  const root = fakeRoot([bar, strip]);
  arrange(root, [bar]);
  assert.deepEqual(root.children, [bar]);
  arrange(root, [side, bar]);
  assert.deepEqual(root.children, [side, bar]);
  arrange(root, [bar, side]);
  assert.deepEqual(root.children, [bar, side]);
  assert.equal(root.moves, 3);
});

// 底部或左右 dock 区，时间轴的工具栏高 34，帧条 88，竖向窄列宽 132。
async function dockHarness({
  neighbour = false,
  area = "bottom",
  collapsed = false,
  initial = { collapsed } as Record<string, unknown>,
} = {}) {
  const { watchTimelineDock } = await loadTimeline();
  const size = { current: 200 };
  const areas: Record<string, { size?: number; groups: { tabs: string[] }[] }> = {
    top: { groups: [] },
    bottom: { size: 200, groups: [] },
    left: { size: 200, groups: [] },
    right: { size: 200, groups: [] },
  };
  areas[area].size = 200;
  areas[area].groups = [{ tabs: ["timelineDock"] }, { tabs: ["pythonDebugDock"] }];
  const resizes: number[] = [];
  const moves: string[] = [];
  const floats: boolean[] = [];
  const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
  let floating: { dockId: string; lastArea: string }[] = [];
  let curArea = area;
  const moveDock = (nextArea: string) => {
    for (const a of Object.values(areas)) a.groups = a.groups.filter((g) => !g.tabs.includes("timelineDock"));
    floating = [];
    curArea = nextArea;
    areas[nextArea].groups.unshift({ tabs: ["timelineDock"] });
  };
  const runtime = {
    getDockLayout: () => ({ areas, floating }),
    view: () => ({ visible: neighbour }),
    dockResizeArea(target: string, next: number) {
      size.current = next;
      areas[target].size = next;
      resizes.push(next);
    },
    dockMoveToArea(id: string, nextArea: string) {
      if (id === "timelineDock") { moveDock(nextArea); moves.push(nextArea); }
    },
    dockFloat(id: string) {
      if (id !== "timelineDock") return;
      for (const a of Object.values(areas)) a.groups = a.groups.filter((g) => !g.tabs.includes("timelineDock"));
      floating = [{ dockId: "timelineDock", lastArea: curArea }];
      floats.push(true);
    },
    dockRestore(id: string, targetArea?: string) {
      if (id !== "timelineDock") return;
      const dest = targetArea ?? floating[0]?.lastArea ?? curArea;
      moveDock(dest);
      floats.push(false);
    },
  };
  let value: Record<string, unknown> = initial;
  let fit = () => {};
  let barHeight = 34;
  const store = {
    timeline: {
      bar: {},
      extent: () => (value.vertical && !value.floating ? { width: 132 } : { height: barHeight + (value.collapsed ? 0 : 88) }),
    },
    get: () => value,
    call: (method: string, params: Record<string, unknown>) => { calls.push({ method, params }); },
    on: (_: string, fn: () => void) => { fit = fn; fn(); },
  };
  const hadObserver = globalThis.ResizeObserver;
  Object.assign(globalThis, { ResizeObserver: class { observe() {} } });
  let onDockChanged: ((dockId: string) => void) | undefined;
  try {
    onDockChanged = watchTimelineDock(runtime, store);
  } finally {
    Object.assign(globalThis, { ResizeObserver: hadObserver });
  }
  return {
    size,
    resizes,
    moves,
    floats,
    calls,
    area: () => (floating.length ? "floating" : curArea),
    userMoveToArea(nextArea: string) { moveDock(nextArea); onDockChanged?.("timelineDock"); },
    userFloat() { runtime.dockFloat("timelineDock"); floats.pop(); onDockChanged?.("timelineDock"); },
    userRestore() { runtime.dockRestore("timelineDock"); floats.pop(); onDockChanged?.("timelineDock"); },
    report(next: Record<string, unknown>) { value = next; fit(); },
    wrapBar() { barHeight = 68; fit(); },
    drag(next: number) { size.current = next; areas[curArea].size = next; },
  };
}

test("收起时 dock 缩到只剩标题和工具栏，展开时回到收起前的高度", async () => {
  const dock = await dockHarness();
  dock.drag(260);
  dock.report({ collapsed: true });
  assert.equal(dock.size.current, 18 + 34);
  dock.report({ collapsed: false });
  assert.equal(dock.size.current, 260);
});

test("启动时已经是收起状态也要缩，展开回到设计稿的高度", async () => {
  const dock = await dockHarness({ collapsed: true });
  assert.deepEqual(dock.resizes, [52]);
  dock.report({ collapsed: false });
  assert.deepEqual(dock.resizes, [52, 200]);
});

test("模式没变不动高度：播放每帧的上报不能盖掉用户拖的高度", async () => {
  const dock = await dockHarness();
  dock.report({ collapsed: false });
  dock.drag(300);
  dock.report({ collapsed: false });
  assert.equal(dock.size.current, 300);
  dock.report({ collapsed: true });
  dock.drag(90);
  dock.report({ collapsed: true });
  assert.equal(dock.size.current, 90);
});

test("工具栏折成两行时收起的高度跟着变", async () => {
  const dock = await dockHarness();
  dock.report({ collapsed: true });
  dock.wrapBar();
  assert.equal(dock.size.current, 18 + 68);
  dock.report({ collapsed: false });
  assert.equal(dock.size.current, 200);
});

test("同区还有别的面板开着就不缩，否则会把它压扁", async () => {
  const dock = await dockHarness({ neighbour: true });
  dock.report({ collapsed: true });
  dock.report({ collapsed: false });
  assert.deepEqual(dock.resizes, []);
});

test("浮动时不动停靠区尺寸", async () => {
  const floating = await dockHarness();
  floating.report({ collapsed: true, floating: true });
  assert.deepEqual(floating.resizes, []);
});

test("左右停靠区收起时按 kCollapsedColumnWidth 把宽度给回画布，展开时回到收起前的宽度", async () => {
  for (const side of ["right", "left"] as const) {
    const dock = await dockHarness({ area: side });
    dock.drag(260);
    dock.report({ collapsed: true, vertical: true, dockArea: side });
    assert.equal(dock.size.current, 132);
    dock.report({ collapsed: false, vertical: true, dockArea: side });
    assert.equal(dock.size.current, 260);

    const withNeighbour = await dockHarness({ area: side, neighbour: true });
    withNeighbour.report({ collapsed: true, vertical: true, dockArea: side });
    assert.deepEqual(withNeighbour.resizes, []);
  }
});

test("启动时按后端状态恢复左右停靠位置与浮动状态", async () => {
  const leftDock = await dockHarness({
    initial: { dockArea: "left", vertical: true, leftAligned: true, floating: false, collapsed: false },
  });
  assert.equal(leftDock.area(), "left");
  assert.deepEqual(leftDock.moves, ["left"]);

  const floatDock = await dockHarness({
    initial: { dockArea: "right", vertical: true, leftAligned: false, floating: true, collapsed: false },
  });
  assert.equal(floatDock.area(), "floating");
  assert.deepEqual(floatDock.moves, ["right"]);
  assert.deepEqual(floatDock.floats, [true]);
});

test("后端停靠方向与浮动状态变化时移动停靠窗，且用户拖动同步后端不形成状态回环", async () => {
  const dock = await dockHarness({
    initial: { dockArea: "bottom", vertical: false, leftAligned: false, floating: false, collapsed: false },
  });

  // 后端切换到右侧竖向再切回底部横向
  dock.report({ dockArea: "right", vertical: true, leftAligned: false, floating: false, collapsed: false });
  assert.equal(dock.area(), "right");
  assert.deepEqual(dock.moves, ["right"]);

  dock.report({ dockArea: "bottom", vertical: false, leftAligned: false, floating: false, collapsed: false });
  assert.equal(dock.area(), "bottom");
  assert.deepEqual(dock.moves, ["right", "bottom"]);

  // 用户拖到左侧停靠区 -> 通知后端 timeline.setDockArea，后端回推同一状态不再触发 dockMoveToArea
  dock.userMoveToArea("left");
  assert.deepEqual(dock.calls, [{ method: "timeline.setDockArea", params: { dockArea: "left", floating: false } }]);
  dock.report({ dockArea: "left", vertical: true, leftAligned: true, floating: false, collapsed: false });
  assert.deepEqual(dock.moves, ["right", "bottom"]);

  // 用户拖出浮动 -> 通知后端 timeline.setFloating(true)，回推不重入
  dock.userFloat();
  assert.deepEqual(dock.calls.at(-1), { method: "timeline.setFloating", params: { floating: true } });
  dock.report({ dockArea: "left", vertical: true, leftAligned: true, floating: true, collapsed: false });
  assert.deepEqual(dock.floats, []);

  // 用户归位 -> 通知后端 timeline.setFloating(false)，回推不重入
  dock.userRestore();
  assert.deepEqual(dock.calls.at(-1), { method: "timeline.setFloating", params: { floating: false } });
  dock.report({ dockArea: "left", vertical: true, leftAligned: true, floating: false, collapsed: false });
  assert.deepEqual(dock.floats, []);
});

test("竖向时播放条放到 bottomChrome，横向和浮动时放回，Orientation/Float/Collapse 按钮切换状态", async () => {
  const { createTimeline } = await loadTimeline();
  const had = {
    document: globalThis.document,
    window: globalThis.window,
    ResizeObserver: globalThis.ResizeObserver,
    MutationObserver: globalThis.MutationObserver,
  };
  const docEl = fakeElement("html");
  Object.assign(globalThis, {
    document: { createElement: (tag: string) => fakeElement(tag), documentElement: docEl, activeElement: null },
    window: { addEventListener() {}, removeEventListener() {}, devicePixelRatio: 1 },
    ResizeObserver: class { observe() {} },
    MutationObserver: class { observe() {} },
  });
  try {
    const root = fakeElement("div");
    const bottomChrome = fakeElement("div");
    const events: Array<[string, boolean]> = [];
    const panel = createTimeline(root, {
      onOrientation: (v: boolean) => events.push(["Orientation", v]),
      onFloat: (f: boolean) => events.push(["Float", f]),
      onCollapse: (c: boolean) => events.push(["Collapse", c]),
    }, { bottomChrome });

    // 横向：播放条在 root 内，bottomChrome 为空
    panel.update({ vertical: false, floating: false, collapsed: false });
    assert.equal(panel.bar.parentElement, root);
    assert.equal(bottomChrome.children.length, 0);

    // 竖向：播放条移入 bottomChrome，root 内为 [side, strip]
    panel.update({ vertical: true, floating: false, collapsed: false });
    assert.equal(panel.bar.parentElement, bottomChrome);
    assert.deepEqual(bottomChrome.children, [panel.bar]);
    assert.equal(root.children.length, 2);
    assert.deepEqual(panel.extent(), { width: 132 });

    // 浮动时总是横条：即使后端传了 vertical: true，播放条也回到 root
    panel.update({ vertical: true, floating: true, collapsed: false });
    assert.equal(panel.bar.parentElement, root);
    assert.equal(root.classList.contains("floating"), true);
    assert.equal("height" in panel.extent(), true);

    // 点击工具栏上的 Close / Collapse / Orientation / Float 按钮（按右端顺序排列）
    panel.update({ vertical: false, floating: false, collapsed: false });
    const cells: any[] = [...panel.bar.children].filter((c: any) => c.className === "tl-cell" && c.style.display !== "none");
    // chrome 顺序为 ["Close", "Collapse", "Orientation", "Float"]，首次渲染在 cells 索引 5..8
    const byRight = [...cells].sort((a: any, b: any) => parseInt(b.style.left, 10) - parseInt(a.style.left, 10));
    const [_closeBtn, collapseBtn, orientBtn, floatBtn] = byRight;
    collapseBtn.click();
    orientBtn.click();
    floatBtn.click();
    assert.deepEqual(events, [
      ["Collapse", true],
      ["Orientation", true],
      ["Float", true],
    ]);
  } finally {
    Object.assign(globalThis, had);
  }
});

test("时间轴只为屏幕上的关键帧要缩略图，保持帧和滚出去的帧不要，窗口变了要重新报尺寸", async () => {
  const { createTimeline } = await loadTimeline();
  const had = {
    document: globalThis.document,
    window: globalThis.window,
    ResizeObserver: globalThis.ResizeObserver,
    MutationObserver: globalThis.MutationObserver,
    Path2D: globalThis.Path2D,
    getComputedStyle: globalThis.getComputedStyle,
  };
  const observers: Array<() => void> = [];
  const sized = () => {
    const el = fakeElement("div");
    const drawing = new Proxy({}, {
      get: (target: any, name: string) => (name in target ? target[name] : () => {}),
      set: (target: any, name: string, value) => { target[name] = value; return true; },
    });
    return Object.assign(el, { getContext: () => drawing });
  };
  Object.assign(globalThis, {
    document: {
      createElement: (tag: string) => {
        const el = sized();
        if (tag === "div") {
          Object.defineProperty(el, "clientWidth", { get: () => view.width, configurable: true });
          Object.defineProperty(el, "clientHeight", { get: () => view.height, configurable: true });
        }
        return el;
      },
      documentElement: fakeElement("html"),
      activeElement: null,
    },
    window: { addEventListener() {}, removeEventListener() {}, devicePixelRatio: 1 },
    ResizeObserver: class {
      fn: () => void;
      constructor(fn: () => void) { this.fn = fn; observers.push(fn); }
      observe() {}
    },
    MutationObserver: class { observe() {} },
    Path2D: class {},
    getComputedStyle: () => ({ getPropertyValue: () => "#000" }),
  });
  const view = { width: 300, height: 114 };
  try {
    const asked: Array<[number[], { width: number; height: number } | null]> = [];
    const panel = createTimeline(fakeElement("div"), {
      onPreview: (frames: number[], size: { width: number; height: number } | null) => asked.push([frames, size]),
    });
    panel.update({
      frames: [
        { index: 0 }, { index: 1, held: true }, { index: 2 },
        ...Array.from({ length: 40 }, (_, i) => ({ index: i + 3 })),
      ],
      current: 0,
    });
    const [frames, size] = asked.at(-1)!;
    assert.equal(frames[0], 0);
    assert.equal(frames.includes(1), false);
    assert.equal(frames.includes(2), true);
    assert.ok(frames.length < 10, `屏幕外的帧不该要：${frames.length}`);
    assert.ok(size && size.width > 0 && size.height > 0);

    view.height = 214;
    for (const fn of observers) fn();
    const grown = asked.at(-1)![1]!;
    assert.ok(grown.height > size!.height, "格子变高后要按新尺寸重新要缩略图");
  } finally {
    Object.assign(globalThis, had);
  }
});
