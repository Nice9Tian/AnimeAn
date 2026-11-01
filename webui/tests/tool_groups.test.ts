import assert from "node:assert/strict";
import { beforeEach, test } from "node:test";
import { raiseBusiness, syncTools } from "../src/ui/tools.js";

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
