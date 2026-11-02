import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { beforeEach, test } from "node:test";
import { parse } from "yaml";
import { raiseBusiness, syncTools, watchToolsScroll } from "../src/ui/tools.js";

const KEY = "animean.toolGroups";
const saved = new Map<string, string>();
Object.assign(globalThis, {
  localStorage: {
    getItem: (key: string) => saved.get(key) ?? null,
    setItem: (key: string, value: string) => void saved.set(key, value),
  },
});

function harness() {
  const state = new Map<string, unknown>();
  const listeners = new Map<string, (value: unknown) => void>();
  const runtime = {
    setBusinessState(path: string, value: unknown) {
      state.set(path, value);
      return null;
    },
  };
  const store = {
    toolGroups: undefined as unknown,
    on: (key: string, fn: (value: unknown) => void) => void listeners.set(key, fn),
    call: () => {},
  };
  syncTools(runtime, store, ["pen", "mapping_area"]);
  const extraTools = [{ name: "mapping_area", page: "mapping" }];
  return {
    store,
    open: () => [state.get("tools.paintingOpen"), state.get("tools.mappingOpen")],
    tool: (current: string, activeExtraTool = "") => listeners.get("tools")!({ current, activeExtraTool, extraTools }),
    page: (current: string) => listeners.get("windows")!({ tools: { current } }),
    toggle: (group: string) => raiseBusiness(store, "toolGroupToggle", { group }),
  };
}

beforeEach(() => saved.clear());

test("分组默认全开，折叠状态写进本机并在下次启动恢复", () => {
  const first = harness();
  assert.deepEqual(first.open(), [true, true]);
  assert.equal(first.toggle("mapping"), true);
  assert.deepEqual(first.open(), [true, false]);
  assert.deepEqual(JSON.parse(saved.get(KEY)!), { painting: true, mapping: false });
  assert.deepEqual(harness().open(), [true, false]);
});

test("存的内容坏了时回到全开", () => {
  saved.set(KEY, "{oops");
  assert.deepEqual(harness().open(), [true, true]);
});

test("选中的工具换到折叠的组时展开它", () => {
  const h = harness();
  h.tool("pen");
  h.toggle("mapping");
  h.tool("arrow", "mapping_area");
  assert.deepEqual(h.open(), [true, true]);
  h.toggle("painting");
  h.tool("eraser");
  assert.deepEqual(h.open(), [true, true]);
});

test("启动时和工具没变时不展开，用户收起选中工具所在的组后保持收起", () => {
  saved.set(KEY, JSON.stringify({ painting: false, mapping: true }));
  const h = harness();
  h.tool("pen");
  assert.deepEqual(h.open(), [false, true]);
  h.toggle("painting");
  h.toggle("painting");
  h.tool("pen");
  assert.deepEqual(h.open(), [false, true]);
});

test("图层策略切页时展开对应的组，首次收到页面状态不算切页", () => {
  saved.set(KEY, JSON.stringify({ painting: true, mapping: false }));
  const h = harness();
  h.page("painting");
  assert.deepEqual(h.open(), [true, false]);
  h.page("mapping");
  assert.deepEqual(h.open(), [true, true]);
});

class NoObserver {
  observe() {}
}

function scrollHarness(contentHeight: number, viewHeight: number) {
  const state = new Map<string, unknown>();
  let top = 0;
  let wheel: (event: { deltaY: number; preventDefault(): void }) => void = () => {};
  const area = {
    scrollHeight: contentHeight,
    clientHeight: viewHeight,
    get scrollTop() {
      return top;
    },
    set scrollTop(value: number) {
      top = Math.max(0, Math.min(contentHeight - viewHeight, value));
    },
    addEventListener: (_name: string, fn: typeof wheel) => {
      wheel = fn;
    },
  };
  const runtime = {
    setBusinessState(path: string, value: unknown) {
      state.set(path, value);
      return null;
    },
  };
  const store = { toolsScroll: undefined as unknown };
  // The whole test process shares these globals, and board input looks for ResizeObserver.
  const { ResizeObserver: resize, MutationObserver: mutation } = globalThis;
  Object.assign(globalThis, { ResizeObserver: NoObserver, MutationObserver: NoObserver });
  watchToolsScroll({ querySelector: () => area }, runtime, store);
  Object.assign(globalThis, { ResizeObserver: resize, MutationObserver: mutation });
  return {
    arrows: () => [state.get("tools.canScrollUp"), state.get("tools.canScrollDown")],
    press: (step: number) => raiseBusiness(store, "toolsScrollBy", { step }),
    wheel: (deltaY: number) => wheel({ deltaY, preventDefault() {} }),
    top: () => top,
  };
}

test("放得下时两个箭头都不显示", () => {
  assert.deepEqual(scrollHarness(400, 500).arrows(), [false, false]);
});

test("放不下时先只有向下，滚动后出现向上，到底后向下消失", () => {
  const h = scrollHarness(620, 500);
  assert.deepEqual(h.arrows(), [false, true]);
  assert.equal(h.press(1), true);
  assert.equal(h.top(), 50);
  assert.deepEqual(h.arrows(), [true, true]);
  h.press(1);
  h.press(1);
  assert.equal(h.top(), 120);
  assert.deepEqual(h.arrows(), [true, false]);
  h.press(-1);
  assert.deepEqual(h.arrows(), [true, true]);
});

test("滚轮一格走一行，方向跟滚轮", () => {
  const h = scrollHarness(620, 500);
  h.wheel(240);
  assert.equal(h.top(), 50);
  h.wheel(-3);
  assert.equal(h.top(), 0);
  assert.deepEqual(h.arrows(), [false, true]);
});

test("Python 注册的工具都有节点、状态和图标", () => {
  const read = (path: string) => readFileSync(new URL(path, import.meta.url), "utf8");
  // Each entry's page: "painting" or, when omitted, "mapping" (extra_tools.py).
  const tools = [...read("../../pyfile/extra_tools.py").matchAll(/\{[^{}]*"name": "(\w+)"[^{}]*\}/g)]
    .map((m) => ({ name: m[1], page: /"page": "(\w+)"/.exec(m[0])?.[1] ?? "mapping" }));
  const logic = parse(read("../design/logic/app.logic.yaml"));
  const nodes = JSON.parse(read("../design/ui/scene.json")).nodes;
  const glyphs = read("../design/components/QtToolChip/index.js");
  assert.ok(tools.some((t) => t.name === "deform_line" && t.page === "mapping"));
  assert.ok(tools.some((t) => t.name === "curve" && t.page === "painting"));
  for (const { name, page } of tools) {
    const group = page === "painting" ? { armed: "paintingArmed", body: "paintingBody" } : { armed: "mappingArmed", body: "mappingBody" };
    assert.ok(name in logic.state.tools.locked, `tools.locked.${name}`);
    assert.match(logic.conditions[group.armed].expr, new RegExp(`'${name}'`));
    const [id] = Object.entries(logic.objects).find(
      ([, object]: [string, any]) => object.events?.click?.[0]?.emit?.args?.name === `'${name}'`,
    ) ?? [];
    assert.ok(id, `${name} 没有点击逻辑`);
    assert.ok(nodes[group.body].children.includes(id), `${id} 不在 ${page} 组`);
    assert.ok(glyphs.includes(`  ${nodes[id].props.glyph}: `), `${id} 没有图标`);
  }
});
