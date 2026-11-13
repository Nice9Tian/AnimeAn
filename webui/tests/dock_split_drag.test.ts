// 拖并排面板之间的分隔条：拖多少像素，分隔条走多少像素。
// 排版和挂分隔条照 player.ts 的 applyViews 做，鼠标事件走 dock.ts 自己的拖动处理，
// 排列用真的设计稿（webui/design），所以这里的 Tool Options、Layers 就是界面上那两块。
import assert from "node:assert/strict";
import fs from "node:fs/promises";
import path from "node:path";
import { test } from "node:test";
import { fileURLToPath } from "node:url";
import {
  computeDockWindowLayout,
  dockResizeSplit,
  syncDockSplittersDom,
  type DockArea,
  type DockPreviewCallbacks,
  type DockSplitterRect,
} from "../vf/src/dock.ts";
import { loadProjectFiles, type ProjectIO } from "../vf/src/loader.ts";
import { Runtime } from "../vf/src/runtime.ts";

const DESIGN = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "../design");
const io: ProjectIO = {
  root: async () => DESIGN,
  readText: async (rel) => fs.readFile(path.join(DESIGN, rel), "utf8").catch(() => null),
  listDir: async (rel) =>
    (await fs.readdir(path.join(DESIGN, rel), { withFileTypes: true }).catch(() => [])).map((e) => ({ name: e.name, isDir: e.isDirectory() })),
};

// 软件刚打开时开着的面板：右边只有 Tool Options 和 Layers，Assets、History、Repulsion Pad 关着。
const AT_START = ["toolsDock", "toolOptionsDock", "layersDock", "timelineDock"];
const RIGHT = ["toolOptionsDock", "layersDock", "assetsDock", "historyDock", "forcePadDock"];

interface Bar {
  rect: DockSplitterRect;
  down?: (e: object) => void;
}

// 舞台是窗口去掉菜单栏和状态栏：1920 × 1200 的窗口里是 1920 × 1157。
async function openStage(width: number, height: number, shown: string[] = AT_START) {
  const project = await loadProjectFiles(io, null);
  const rootId = project.scene.root;
  const rootNode = project.scene.nodes[rootId]!;
  const size = { width, height };
  let bars: Bar[] = [];
  let boxes = new Map<string, { x: number; y: number; width: number; height: number; visible: boolean }>();
  const listeners = new Map<string, Set<(e: object) => void>>();
  const win = {
    addEventListener: (type: string, fn: (e: object) => void) => void (listeners.get(type) ?? listeners.set(type, new Set()).get(type)!).add(fn),
    removeEventListener: (type: string, fn: (e: object) => void) => void listeners.get(type)?.delete(fn),
  };
  const frame = {
    ownerDocument: { defaultView: win },
    get offsetWidth() { return size.width; },
    getBoundingClientRect: () => ({ left: 0, top: 0, width: size.width, height: size.height }),
    querySelectorAll: () => bars.map((bar) => ({ remove: () => void (bars = bars.filter((b) => b !== bar)) })),
    append: (el: { bar: Bar }) => void bars.push(el.bar),
  };
  // syncDockSplittersDom 每条分隔条建一个 div：记下它的矩形和按下的处理。
  const pending: Bar[] = [];
  const doc = {
    createElement: () => {
      const bar: Bar = { rect: undefined as unknown as DockSplitterRect };
      pending.push(bar);
      return {
        bar,
        className: "",
        dataset: {},
        style: {},
        classList: { add() {}, remove() {} },
        addEventListener: (type: string, fn: (e: object) => void) => { if (type === "pointerdown") bar.down = fn; },
      };
    },
  };

  let runtime: Runtime;
  const visible = (id: string) => runtime.view(id).visible;
  const callbacks: DockPreviewCallbacks = {
    getMode: () => "preview",
    getScene: () => project.scene,
    getDockLayout: () => runtime.getDockLayout(rootId),
    getWindowSize: () => size,
    getDockTitle: (id) => id,
    onActivateTab() {}, onTabify() {}, onSplit() {}, onMoveToArea() {}, onFloat() {}, onRestore() {}, onClose() {},
    onResizeArea: (area, next) => runtime.dockResizeArea(area, next),
    // player.ts 的接法：屏幕上各组的像素一起交给运行层。
    onResizeSplit: (area, index, next, _commit, shownSizes) => runtime.dockResizeSplit(area, index, next, undefined, shownSizes),
  };
  const relayout = () => {
    const had = { document: globalThis.document, window: globalThis.window };
    Object.assign(globalThis, { document: doc, window: win });
    try {
      const res = computeDockWindowLayout(project.scene, rootId, rootNode, size.width, 0, size.height, true, visible, runtime.getDockLayout(rootId));
      boxes = res.dockBoxes;
      pending.length = 0;
      syncDockSplittersDom(frame as unknown as HTMLElement, res.splitters, callbacks);
      res.splitters.forEach((rect, i) => { pending[i].rect = rect; });
    } finally {
      Object.assign(globalThis, had);
    }
  };
  runtime = new Runtime(project, { onChange: relayout, onLog() {} });
  runtime.start();
  for (const id of rootNode.docks ?? []) runtime.dockSetVisible(id, shown.includes(id));
  relayout();

  const groupOf = (dockId: string) => {
    const layout = runtime.getDockLayout(rootId);
    for (const area of ["left", "right", "top", "bottom"] as DockArea[]) {
      const index = layout.areas[area].groups.findIndex((g) => g.tabs.includes(dockId));
      if (index >= 0) return { area, index };
    }
    throw new Error(`${dockId} 没停靠`);
  };
  const stage = {
    runtime,
    project,
    box(dockId: string) {
      const b = boxes.get(dockId)!;
      return { x: b.x, y: b.y, width: b.width, height: b.height };
    },
    // 所有可见面板的框，用来确认没被拖动的面板原地不动。
    boxes: (ids: string[]) => Object.fromEntries(ids.filter(visible).map((id) => [id, stage.box(id)])),
    // 屏幕上每条并排分隔条：两边各是哪块面板、各占多少像素、这个区一共有多少像素可分。
    splits() {
      const layout = runtime.getDockLayout(rootId);
      const shownDock = (area: DockArea, index: number) => layout.areas[area].groups[index].tabs.find(visible)!;
      return bars.filter((b) => b.rect.kind === "split").map((b) => {
        const { area, shown: sizes = [], groupIndex } = b.rect;
        const at = sizes.findIndex((s) => s.groupIndex === groupIndex);
        const across = b.rect.orientation === "vertical";
        return {
          across,
          before: shownDock(area, sizes[at].groupIndex),
          after: shownDock(area, sizes[at + 1].groupIndex),
          sizes: [sizes[at].size, sizes[at + 1].size],
          all: sizes.map((s) => shownDock(area, s.groupIndex)),
          // 可分的像素：左右并排是舞台的宽，上下并排是中间那一段的高（Tools 独占左边，和它一样高）。
          fits: sizes.reduce((sum, s) => sum + s.size, 0) === (across ? size.width : stage.box("toolsDock").height),
        };
      });
    },
    // dockId 所在的组和它后面那个可见组之间的分隔条。
    splitterAfter(dockId: string) {
      const { area, index } = groupOf(dockId);
      const bar = bars.find((b) => b.rect.kind === "split" && b.rect.area === area && b.rect.groupIndex === index);
      assert.ok(bar, `${dockId} 后面没有分隔条`);
      const { x, y, width: w, height: h } = bar.rect;
      return { x: x + w / 2, y: y + h / 2 };
    },
    // 在 dockId 后面的分隔条上按下，依次把鼠标移到离按下点 (dx, dy) 的地方，返回每一步之后分隔条离起点多远。
    drag(dockId: string, moves: Array<[number, number]>) {
      const { area, index } = groupOf(dockId);
      const bar = bars.find((b) => b.rect.kind === "split" && b.rect.area === area && b.rect.groupIndex === index)!;
      const from = stage.splitterAfter(dockId);
      const had = { document: globalThis.document, window: globalThis.window };
      Object.assign(globalThis, { document: doc, window: win });
      try {
        bar.down!({ button: 0, clientX: from.x, clientY: from.y, stopPropagation() {}, preventDefault() {} });
        const trail: Array<[number, number]> = [];
        for (const [dx, dy] of moves) {
          for (const fn of [...(listeners.get("pointermove") ?? [])]) fn({ clientX: from.x + dx, clientY: from.y + dy });
          const now = stage.splitterAfter(dockId);
          trail.push([now.x - from.x, now.y - from.y]);
        }
        const [dx, dy] = moves[moves.length - 1];
        for (const fn of [...(listeners.get("pointerup") ?? [])]) fn({ clientX: from.x + dx, clientY: from.y + dy });
        assert.equal(listeners.get("pointermove")?.size ?? 0, 0, "松开后不再跟鼠标");
        return trail;
      } finally {
        Object.assign(globalThis, had);
      }
    },
    show(dockId: string, on: boolean) {
      runtime.dockSetVisible(dockId, on);
      relayout();
    },
    resize(nextWidth: number, nextHeight: number) {
      size.width = nextWidth;
      size.height = nextHeight;
      relayout();
    },
  };
  return stage;
}

const down = (...steps: number[]): Array<[number, number]> => steps.map((dy) => [0, dy]);
const across = (...steps: number[]): Array<[number, number]> => steps.map((dx) => [dx, 0]);

test("Tool Options 和 Layers 之间的分隔条：任何窗口高度下，拖多少像素就走多少像素", async () => {
  // 732 是设计稿的高度；1157 是 1920 × 1200 的窗口；其余是更矮和更高的窗口。
  for (const height of [732, 1157, 857, 600, 2117]) {
    const stage = await openStage(1920, height);
    const options = stage.box("toolOptionsDock"), layers = stage.box("layersDock");
    const others = stage.boxes(["toolsDock", "timelineDock"]);
    const at = `舞台高 ${height}`;

    // 一个像素一个像素地往下拖 10 个像素。
    const steps = [1, 2, 3, 4, 5, 6, 7, 8, 9, 10];
    assert.deepEqual(stage.drag("toolOptionsDock", down(...steps)), down(...steps), `${at}：分隔条要一步一步跟着鼠标`);
    assert.equal(stage.box("toolOptionsDock").height, options.height + 10, at);
    assert.deepEqual(stage.box("layersDock"), { ...layers, y: layers.y + 10, height: layers.height - 10 }, at);

    // 松开再拖：从现在的位置接着走，往上、再越过起点往下。
    assert.deepEqual(stage.drag("toolOptionsDock", down(-5, -10, -25, 40, 37)), down(-5, -10, -25, 40, 37), `${at}：第二次拖动`);
    assert.equal(stage.box("toolOptionsDock").height, options.height + 47, at);
    assert.equal(stage.box("layersDock").height, layers.height - 47, at);
    assert.deepEqual(stage.boxes(["toolsDock", "timelineDock"]), others, `${at}：别的面板不动`);
  }
});

test("拖过头时停在面板的最小高度，鼠标回来就接着跟手", async () => {
  const stage = await openStage(1920, 1157);
  const options = stage.box("toolOptionsDock"), layers = stage.box("layersDock");
  // 往下拖得比 Layers 还高：Layers 留 48，分隔条停在那里；鼠标回到 +30 时分隔条也在 +30。
  const room = layers.height - 48;
  assert.deepEqual(stage.drag("toolOptionsDock", down(room - 1, room, room + 200, 30)), down(room - 1, room, room, 30));
  // 往上同理，Tool Options 留 48。
  const up = options.height + 30 - 48;
  assert.deepEqual(stage.drag("toolOptionsDock", down(-up - 500, -up + 2)), down(-up, -up + 2));
  assert.equal(stage.box("toolOptionsDock").height, 50);
});

test("右边开着更多面板时，只有分隔条两边的两块变，中间关着的面板不碍事", async () => {
  // 五块全开：拖 Layers 和 Assets 之间。
  const all = await openStage(1920, 1157, [...AT_START, "assetsDock", "historyDock", "forcePadDock"]);
  let before = all.boxes(RIGHT);
  assert.deepEqual(all.drag("layersDock", down(3, 17)), down(3, 17));
  assert.deepEqual(all.boxes(RIGHT), {
    ...before,
    layersDock: { ...before.layersDock, height: before.layersDock.height + 17 },
    assetsDock: { ...before.assetsDock, y: before.assetsDock.y + 17, height: before.assetsDock.height - 17 },
  });

  // Assets 关着：Layers 后面的分隔条那一边是 History。
  const gap = await openStage(1920, 900, [...AT_START, "historyDock", "forcePadDock"]);
  before = gap.boxes(RIGHT);
  assert.deepEqual(gap.drag("layersDock", down(-1, -2, -31)), down(-1, -2, -31));
  assert.deepEqual(gap.boxes(RIGHT), {
    ...before,
    layersDock: { ...before.layersDock, height: before.layersDock.height - 31 },
    historyDock: { ...before.historyDock, y: before.historyDock.y - 31, height: before.historyDock.height + 31 },
  });
});

test("底部左右并排的 Timeline 和 Python Debug：任何窗口宽度下也是拖多少走多少", async () => {
  for (const width of [1027, 1920, 1366]) {
    const stage = await openStage(width, 900, [...AT_START, "pythonDebugDock"]);
    const timeline = stage.box("timelineDock"), debug = stage.box("pythonDebugDock");
    assert.deepEqual(stage.drag("timelineDock", across(1, 2, 3, -40, 25)), across(1, 2, 3, -40, 25), `舞台宽 ${width}`);
    assert.equal(stage.box("timelineDock").width, timeline.width + 25);
    assert.deepEqual(stage.box("pythonDebugDock"), { ...debug, x: debug.x + 25, width: debug.width - 25 });
  }
});

test("拖完以后改窗口大小、再打开关着的面板，各块的比例和没拖过时对得上", async () => {
  const stage = await openStage(1920, 1157);
  // 没拖过时打开 Assets，它占多高。
  stage.show("assetsDock", true);
  const assetsBefore = stage.box("assetsDock").height;
  stage.show("assetsDock", false);

  stage.drag("toolOptionsDock", down(60));
  const options = stage.box("toolOptionsDock").height, layers = stage.box("layersDock").height;

  // 窗口变高再变回来：两块按比例变，回来时还是原来的像素。
  stage.resize(1920, 1600);
  const tall = stage.box("toolOptionsDock").height / (stage.box("toolOptionsDock").height + stage.box("layersDock").height);
  assert.ok(Math.abs(tall - options / (options + layers)) < 1 / 1000, "窗口变高后两块的比例不变");
  stage.resize(1920, 1157);
  assert.deepEqual([stage.box("toolOptionsDock").height, stage.box("layersDock").height], [options, layers]);

  // 拖的是 Tool Options 和 Layers，关着的 Assets 的份额没被动过。
  stage.show("assetsDock", true);
  assert.ok(Math.abs(stage.box("assetsDock").height - assetsBefore) <= 1, `Assets 原来 ${assetsBefore}，现在 ${stage.box("assetsDock").height}`);
});

// 份额只是比例，最小尺寸是屏幕上的事。把份额当像素去夹最小值、去取整，面板就会在别处的布局变动时跳开。
test("把面板拖到最小以后改别处的布局，它留在原地，关着的面板占的比例也不变", async () => {
  for (const height of [1157, 2117]) {
    const stage = await openStage(1920, height);
    const right = () => stage.runtime.getDockLayout().areas.right;
    const ratios = () => {
      const sizes = right().groups.map((g) => g.size!);
      const sum = sizes.reduce((a, b) => a + b, 0);
      return Object.fromEntries(right().groups.map((g, i) => [g.tabs[0], sizes[i] / sum]));
    };
    const near = (a: Record<string, number>, b: Record<string, number>, what: string) => {
      for (const id of Object.keys(b)) assert.ok(Math.abs((a[id] ?? 0) - b[id]) < 1e-9, `${what}：${id} 的比例从 ${b[id]} 变成 ${a[id]}`);
    };
    const at = `舞台高 ${height}`;

    stage.drag("toolOptionsDock", down(-5000));
    assert.equal(stage.box("toolOptionsDock").height, 48, at);
    const layers = stage.box("layersDock").height;
    const before = ratios();

    // 底部停靠区拖高 50 再拖回来：Tool Options 一直是最小高度，回来后一切照旧。
    stage.runtime.dockResizeArea("bottom", 250);
    assert.equal(stage.box("toolOptionsDock").height, 48, `${at}：底部变高后`);
    assert.equal(stage.box("layersDock").height, layers - 50, `${at}：底部变高后`);
    near(ratios(), before, `${at}：底部变高后`);
    stage.runtime.dockResizeArea("bottom", 200);
    assert.deepEqual([stage.box("toolOptionsDock").height, stage.box("layersDock").height], [48, layers], `${at}：底部回到原来的高度`);
    near(ratios(), before, `${at}：底部回到原来的高度`);

    // 关着的 History 浮出去再放回来：开着的两块不动，其余面板之间的比例不变。
    stage.runtime.dockFloat("historyDock");
    assert.deepEqual([stage.box("toolOptionsDock").height, stage.box("layersDock").height], [48, layers], `${at}：History 浮出后`);
    const rest = 1 - before.historyDock;
    near(ratios(), Object.fromEntries(Object.entries(before).filter(([id]) => id !== "historyDock").map(([id, r]) => [id, r / rest])), `${at}：History 浮出后`);
    stage.runtime.dockRestore("historyDock");
    assert.deepEqual([stage.box("toolOptionsDock").height, stage.box("layersDock").height], [48, layers], `${at}：History 放回后`);
  }
});

test("拖到中间位置以后把底部停靠区拖高再拖回来，分隔条回到原来的像素", async () => {
  const stage = await openStage(1920, 1157);
  stage.drag("toolOptionsDock", down(37));
  const was = [stage.box("toolOptionsDock").height, stage.box("layersDock").height];
  assert.deepEqual(was, [504, 453]);
  stage.runtime.dockResizeArea("bottom", 250);
  // 中间少了 50 像素，两块按比例分：504 : 453。
  assert.deepEqual([stage.box("toolOptionsDock").height, stage.box("layersDock").height], [478, 429]);
  stage.runtime.dockResizeArea("bottom", 200);
  assert.deepEqual([stage.box("toolOptionsDock").height, stage.box("layersDock").height], was);
});

test("屏幕上的尺寸和排列对不上时不动布局，不把屏幕像素当份额用", async () => {
  const stage = await openStage(1920, 1157);
  const { scene } = stage.project;
  const layout = stage.runtime.getDockLayout();
  const sizes = (l: typeof layout) => l.areas.right.groups.map((g) => g.size);
  // 按下以后排列变了：第 9 组不存在；分隔条后面没有可见的组；拖动量不是数。
  for (const [shown, size] of [
    [[{ groupIndex: 0, size: 467 }, { groupIndex: 9, size: 490 }], 468],
    [[{ groupIndex: 1, size: 490 }], 468],
    [[{ groupIndex: 0, size: 467 }, { groupIndex: 1, size: 490 }], Number.NaN],
  ] as const) {
    const next = dockResizeSplit(layout, scene, scene.nodes[scene.root], "right", 0, size, [...shown]);
    assert.deepEqual(sizes(next), sizes(layout), JSON.stringify(shown));
  }
});

test("随机的窗口大小、开关面板和拖动：分隔条始终跟着鼠标走到该到的位置，别的面板不动", async () => {
  let seed = 20261113;
  const random = () => (seed = (Math.imul(seed, 1664525) + 1013904223) >>> 0) / 2 ** 32;
  const pick = (n: number) => Math.floor(random() * n);
  const extra = ["assetsDock", "historyDock", "forcePadDock", "pythonDebugDock"];
  const everything = [...AT_START, ...extra];
  const stage = await openStage(1920, 1157);
  // 右边每块面板占这个区全部份额的几分之几，关着的也算。
  const ratios = (without = "") => {
    const groups = stage.runtime.getDockLayout().areas.right.groups.filter((g) => g.tabs[0] !== without);
    const sum = groups.reduce((acc, g) => acc + g.size!, 0);
    return groups.map((g) => [g.tabs[0], g.size! / sum] as const);
  };
  const sameRatios = (now: ReturnType<typeof ratios>, was: ReturnType<typeof ratios>, what: string) => {
    const before = new Map(was);
    assert.equal(now.length, was.length, what);
    for (const [id, ratio] of now) assert.ok(Math.abs(ratio - before.get(id)!) < 1e-9, `${what}：${id} 的比例从 ${before.get(id)} 变成 ${ratio}`);
  };
  let dragged = 0, cramped = 0, elsewhere = 0;
  for (let round = 0; round < 400; round++) {
    if (pick(3) === 0) stage.resize(700 + pick(2400), 380 + pick(1900));
    if (pick(2) === 0) stage.show(extra[pick(extra.length)], pick(2) === 0);
    // 别处的布局变动不改右边各块之间的比例：底部停靠区变高变矮，一块面板浮出去再放回来。
    if (pick(4) === 0) {
      const was = ratios();
      stage.runtime.dockResizeArea("bottom", 120 + pick(260));
      sameRatios(ratios(), was, `第 ${round} 轮：底部停靠区改了高度`);
      elsewhere++;
    }
    if (pick(8) === 0) {
      const id = extra[pick(3)];
      const was = ratios(id);
      stage.runtime.dockFloat(id);
      sameRatios(ratios(), was, `第 ${round} 轮：${id} 浮出`);
      stage.runtime.dockRestore(id);
      sameRatios(ratios(id), was, `第 ${round} 轮：${id} 放回`);
      elsewhere++;
    }
    const splits = stage.splits();
    if (!splits.length) continue;
    const split = splits[pick(splits.length)];
    const least = split.across ? 60 : 48;
    const [first, second] = split.sizes;
    const others = stage.boxes(everything.filter((id) => id !== split.before && id !== split.after));
    // 拖两三下，有时拖过头；每一下都从按下的位置算。
    const moves = Array.from({ length: 2 + pick(2) }, () => pick(5) === 0 ? pick(2000) - 1000 : pick(81) - 40);
    const trail = stage.drag(split.before, moves.map((d): [number, number] => (split.across ? [d, 0] : [0, d])));
    const at = `第 ${round} 轮，${split.before} | ${split.after}，${first} + ${second}，拖 ${moves.join("、")}`;
    const side = split.across ? "width" : "height";
    if (!split.fits) {
      // 窗口小到放不下所有面板的最小尺寸：不要求跟手，只要求不出怪数。
      cramped++;
      for (const id of split.all) assert.ok(stage.box(id)[side] >= least, at);
      continue;
    }
    // 该到的位置：跟着鼠标，但两边各留最小尺寸。
    const reach = (d: number) => Math.max(least, Math.min(first + second - least, first + d)) - first;
    assert.deepEqual(trail.map(([dx, dy]) => (split.across ? dx : dy)), moves.map(reach), at);
    const moved = reach(moves[moves.length - 1]);
    assert.equal(stage.box(split.before)[side], first + moved, at);
    assert.equal(stage.box(split.after)[side], second - moved, at);
    assert.deepEqual(stage.boxes(everything.filter((id) => id !== split.before && id !== split.after)), others, `${at}：别的面板不动`);
    dragged++;
  }
  assert.ok(dragged > 200 && cramped > 0 && elsewhere > 50, `几种情况都要测到：放得下 ${dragged} 次，放不下 ${cramped} 次，别处变动 ${elsewhere} 次`);
});

test("不给屏幕上的尺寸时，dockResizeSplit 照旧按设计稿的像素算", async () => {
  const stage = await openStage(1920, 1157, [...AT_START, "assetsDock", "historyDock", "forcePadDock"]);
  const { scene } = stage.project;
  const next = dockResizeSplit(stage.runtime.getDockLayout(), scene, scene.nodes[scene.root], "right", 0, 150);
  assert.deepEqual(next.areas.right.groups.map((g) => g.size), [150, 108, 84, 84, 106]);
});
