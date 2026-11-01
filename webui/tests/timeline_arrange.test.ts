import assert from "node:assert/strict";
import { test } from "node:test";

// timeline.js measures text at load; nothing here draws.
async function loadTimeline() {
  const had = { document: globalThis.document, window: globalThis.window };
  Object.assign(globalThis, {
    document: { createElement: () => ({ getContext: () => ({}) }) },
    window: { addEventListener() {} },
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

// 底部一个 dock 区，时间轴的工具栏高 34，帧条 88。
async function dockHarness({ neighbour = false, area = "bottom", collapsed = false } = {}) {
  const { watchTimelineDock } = await loadTimeline();
  const size = { current: 200 };
  const areas: Record<string, { size?: number; groups: { tabs: string[] }[] }> = {
    top: { groups: [] },
    bottom: { groups: [] },
    right: { groups: [] },
  };
  areas[area].size = 200;
  areas[area].groups = [{ tabs: ["timelineDock"] }, { tabs: ["pythonDebugDock"] }];
  const resizes: number[] = [];
  const runtime = {
    getDockLayout: () => ({ areas }),
    view: () => ({ visible: neighbour }),
    dockResizeArea(_: string, next: number) {
      size.current = next;
      areas[area].size = next;
      resizes.push(next);
    },
  };
  let value: Record<string, unknown> = { collapsed };
  let fit = () => {};
  let barHeight = 34;
  const store = {
    timeline: {
      bar: {},
      extent: () => ({ height: barHeight + (value.collapsed ? 0 : 88) }),
    },
    get: () => value,
    on: (_: string, fn: () => void) => { fit = fn; fn(); },
  };
  const hadObserver = globalThis.ResizeObserver;
  Object.assign(globalThis, { ResizeObserver: class { observe() {} } });
  try {
    watchTimelineDock(runtime, store);
  } finally {
    Object.assign(globalThis, { ResizeObserver: hadObserver });
  }
  return {
    size,
    resizes,
    report(next: Record<string, unknown>) { value = next; fit(); },
    wrapBar() { barHeight = 68; fit(); },
    drag(next: number) { size.current = next; areas[area].size = next; },
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

test("浮动或不在上下停靠区时不动高度", async () => {
  const floating = await dockHarness();
  floating.report({ collapsed: true, floating: true });
  assert.deepEqual(floating.resizes, []);
  const beside = await dockHarness({ area: "right" });
  beside.report({ collapsed: true });
  assert.deepEqual(beside.resizes, []);
});
