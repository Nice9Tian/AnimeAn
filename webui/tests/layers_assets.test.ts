import assert from "node:assert/strict";
import { test } from "node:test";

class FakeClassList {
  private set = new Set<string>();
  constructor(initial = "") {
    for (const c of initial.split(/\s+/)) if (c) this.set.add(c);
  }
  add(...tokens: string[]) {
    for (const t of tokens) if (t) this.set.add(t);
  }
  remove(...tokens: string[]) {
    for (const t of tokens) this.set.delete(t);
  }
  toggle(token: string, force?: boolean) {
    const next = force !== undefined ? force : !this.set.has(token);
    if (next) this.set.add(token);
    else this.set.delete(token);
    return next;
  }
  contains(token: string) {
    return this.set.has(token);
  }
}

class FakeElement {
  tagName: string;
  textContent = "";
  innerHTML = "";
  type = "";
  checked = false;
  draggable = false;
  tabIndex = 0;
  clientHeight = 200;
  clientWidth = 200;
  scrollHeight = 200;
  scrollTop = 0;
  style: Record<string, string> = {};
  children: FakeElement[] = [];
  private _className = "";
  classList = new FakeClassList();
  private listeners = new Map<string, Array<(e: unknown) => void>>();

  constructor(tagName = "div") {
    this.tagName = tagName;
  }
  get className() {
    return this._className;
  }
  set className(v: string) {
    this._className = v;
    this.classList = new FakeClassList(v);
  }
  get firstChild(): FakeElement | null {
    return this.children[0] ?? null;
  }
  append(...nodes: FakeElement[]) {
    this.children.push(...nodes);
  }
  replaceChildren(...nodes: FakeElement[]) {
    this.children = [...nodes];
  }
  scrollIntoView() {}
  matches() {
    return false;
  }
  getContext() {
    return {
      font: "",
      measureText: (s: string) => ({ width: String(s).length * 6 }),
    };
  }
  private find(selector: string): FakeElement | null {
    const cls = selector.startsWith(".") ? selector.slice(1) : "";
    for (const child of this.children) {
      if (cls && child.classList.contains(cls)) return child;
      const found = child.find(selector);
      if (found) return found;
    }
    return null;
  }
  querySelector(selector: string): FakeElement {
    return this.find(selector) ?? new FakeElement("div");
  }
  querySelectorAll(selector: string): FakeElement[] {
    const out: FakeElement[] = [];
    const cls = selector.startsWith(".") ? selector.slice(1) : "";
    const walk = (node: FakeElement) => {
      for (const child of node.children) {
        if (cls && child.classList.contains(cls)) out.push(child);
        walk(child);
      }
    };
    walk(this);
    return out;
  }
  addEventListener(type: string, fn: (e: unknown) => void) {
    const list = this.listeners.get(type) ?? [];
    list.push(fn);
    this.listeners.set(type, list);
  }
  removeEventListener() {}
  dispatch(type: string, init: Record<string, unknown> = {}) {
    let defaultPrevented = false;
    let propagationStopped = false;
    const event = {
      type,
      clientX: 0,
      clientY: 0,
      ...init,
      preventDefault() {
        defaultPrevented = true;
      },
      stopPropagation() {
        propagationStopped = true;
      },
      get defaultPrevented() {
        return defaultPrevented;
      },
      get propagationStopped() {
        return propagationStopped;
      },
    };
    for (const fn of this.listeners.get(type) ?? []) fn(event);
    return event;
  }
}

class FakeDataTransfer {
  effectAllowed = "all";
  dropEffect = "none";
  private data = new Map<string, string>();
  get types(): string[] {
    return Array.from(this.data.keys());
  }
  setData(mime: string, value: string) {
    this.data.set(mime, String(value));
  }
  getData(mime: string): string {
    return this.data.get(mime) ?? "";
  }
}

function withFakeDom<T>(fn: () => Promise<T> | T): Promise<T> {
  const prev = {
    document: globalThis.document,
    window: globalThis.window,
    ResizeObserver: globalThis.ResizeObserver,
    MutationObserver: globalThis.MutationObserver,
  };
  Object.assign(globalThis, {
    document: {
      createElement: (tag: string) => new FakeElement(tag),
      documentElement: { dataset: {} },
      body: new FakeElement("body"),
    },
    window: {
      addEventListener() {},
    },
    ResizeObserver: class {
      observe() {}
    },
    MutationObserver: class {
      observe() {}
    },
  });
  return Promise.resolve()
    .then(fn)
    .finally(() => {
      Object.assign(globalThis, prev);
    });
}

async function loadModules() {
  return withFakeDom(async () => {
    const panels = await import("../src/ui/panels.js");
    const panels2 = await import("../src/ui/panels2.js");
    const app = await import("../src/ui/app.js");
    return { panels, panels2, app };
  });
}

function createMockStore(initial: Record<string, unknown> = {}) {
  const values = new Map<string, unknown>(Object.entries(initial));
  const listeners = new Map<string, Array<(v: unknown) => void>>();
  const calls: Array<{ method: string; params: Record<string, unknown> }> = [];
  return {
    assetView: undefined as string | undefined,
    calls,
    get: (key: string) => values.get(key),
    set(key: string, value: unknown) {
      values.set(key, value);
      for (const fn of listeners.get(key) ?? []) fn(value);
    },
    on(key: string, fn: (v: unknown) => void) {
      const list = listeners.get(key) ?? [];
      list.push(fn);
      listeners.set(key, list);
      if (values.has(key)) fn(values.get(key));
    },
    call(method: string, params: Record<string, unknown> = {}) {
      calls.push({ method, params });
      return Promise.resolve({});
    },
  };
}

test("点图层组行：递归选中组里第一个成员层，并将该组行记为当前行高亮", async () => {
  const { panels, app } = await loadModules();
  const rows = [
    { group: true, group_id: 10, name: "Outer", depth: 0, collapsed: false },
    { group: true, group_id: 11, name: "EmptySub", depth: 1, collapsed: false },
    { group: true, group_id: 12, name: "Inner", depth: 1, collapsed: false },
    { group: false, layer: 3, name: "Layer 3", depth: 2, visible: true },
    { group: false, layer: 1, name: "Layer 1", depth: 0, visible: true },
  ];
  assert.equal(panels.firstGroupLayer(rows, 0), 3);
  assert.equal(panels.firstGroupLayer(rows, 1), -1);
  assert.equal(panels.firstGroupLayer(rows, 2), 3);

  await withFakeDom(() => {
    const store = createMockStore({
      "layers/main": { rows, selected: 1 },
    });
    const root = new FakeElement("div");
    app.panelFactories(store, {})["QtLayers"](root);
    const tree = root.querySelectorAll(".layer-tree")[0].querySelector(".qframe-view")!;
    assert.equal(tree.children.length, 5);
    assert.equal(tree.children[4].classList.contains("sel"), true);

    // Click Outer group row (index 0): selects first member layer (3) and highlights row 0
    tree.children[0].dispatch("click");
    assert.deepEqual(store.calls.at(-1), {
      method: "layer.select",
      params: { view: "main", layerIndex: 3 },
    });
    assert.equal(tree.children[0].classList.contains("sel"), true);
    assert.equal(tree.children[3].classList.contains("sel"), false);

    // When backend refreshes layers/main with selected: 3, group row 0 stays highlighted as current row
    store.set("layers/main", { rows, selected: 3 });
    assert.equal(tree.children[0].classList.contains("sel"), true);
    assert.equal(tree.children[3].classList.contains("sel"), false);
  });
});

test("Remove Layer：当前行是组时调 layer.deleteGroup，是普通层时调 layer.delete", async () => {
  const { app } = await loadModules();
  const rows = [
    { group: true, group_id: 20, name: "Group A", depth: 0, collapsed: false },
    { group: false, layer: 2, name: "Layer 2", depth: 1, visible: true },
    { group: false, layer: 0, name: "Layer 0", depth: 0, visible: true },
  ];

  await withFakeDom(() => {
    const store = createMockStore({
      "layers/main": { rows, selected: 0 },
    });
    const root = new FakeElement("div");
    app.panelFactories(store, {})["QtLayers"](root);
    const page = root.querySelectorAll(".layer-page")[0];
    const tree = page.querySelector(".layer-tree")!.querySelector(".qframe-view")!;
    const buttons = page.querySelector(".layer-buttons")!.children;
    const removeBtn = buttons[1];

    // Initially current row is Layer 0 -> Remove Layer calls layer.delete
    removeBtn.dispatch("click");
    assert.deepEqual(store.calls.at(-1), {
      method: "layer.delete",
      params: { view: "main", layerIndex: 0 },
    });

    // Click Group A row -> current row is Group A -> Remove Layer calls layer.deleteGroup
    tree.children[0].dispatch("click");
    removeBtn.dispatch("click");
    assert.deepEqual(store.calls.at(-1), {
      method: "layer.deleteGroup",
      params: { view: "main", groupId: 20 },
    });
  });
});

test("当前层在折叠组里时，高亮和当前行落在最外层折叠的组行上", async () => {
  const { panels, app } = await loadModules();
  const bothCollapsed = [
    { group: true, group_id: 1, name: "Outer", depth: 0, collapsed: true },
    { group: true, group_id: 2, name: "Inner", depth: 1, collapsed: true },
    { group: false, layer: 4, name: "Layer 4", depth: 2, visible: true },
  ];
  assert.equal(panels.selectedRowIndex(bothCollapsed, 4), 0);

  const innerOnlyCollapsed = [
    { group: true, group_id: 1, name: "Outer", depth: 0, collapsed: false },
    { group: true, group_id: 2, name: "Inner", depth: 1, collapsed: true },
    { group: false, layer: 4, name: "Layer 4", depth: 2, visible: true },
  ];
  assert.equal(panels.selectedRowIndex(innerOnlyCollapsed, 4), 1);

  await withFakeDom(() => {
    const store = createMockStore({
      "layers/main": { rows: bothCollapsed, selected: 4 },
    });
    const root = new FakeElement("div");
    app.panelFactories(store, {})["QtLayers"](root);
    const page = root.querySelectorAll(".layer-page")[0];
    const tree = page.querySelector(".layer-tree")!.querySelector(".qframe-view")!;
    const removeBtn = page.querySelector(".layer-buttons")!.children[1];

    // Everything under the collapsed Outer group is hidden; its row carries the highlight.
    assert.equal(tree.children.length, 1);
    assert.equal(tree.children[0].classList.contains("sel"), true);

    // Since current row is the collapsed Outer group (id 1), Remove Layer deletes that group
    removeBtn.dispatch("click");
    assert.deepEqual(store.calls.at(-1), {
      method: "layer.deleteGroup",
      params: { view: "main", groupId: 1 },
    });
  });
});

test("Assets 拖到 Layers：自定义类型与图层换序分离，新建素材层并拒绝非目标板的页面", async () => {
  const { panels, app } = await loadModules();
  assert.notEqual(panels.ASSET_DRAG_MIME, "text/plain");
  assert.notEqual(panels.LAYER_DRAG_MIME, "text/plain");
  assert.notEqual(panels.ASSET_DRAG_MIME, panels.LAYER_DRAG_MIME);

  const rows = [
    { group: false, layer: 1, name: "Layer 1", depth: 0, visible: true },
    { group: false, layer: 0, name: "Layer 0", depth: 0, visible: true },
  ];

  await withFakeDom(() => {
    const store = createMockStore({
      window: { activeView: "main" },
      "assets/main": { names: ["A.png", "B.png"], selected: 0 },
      "assets/child": { names: ["Tex.png"], selected: 0 },
      "layers/main": { rows, selected: 0 },
      "layers/child": { rows, selected: 0 },
    });
    const assetsRoot = new FakeElement("div");
    const layersRoot = new FakeElement("div");
    const factories = app.panelFactories(store, {});
    factories["QtAssets"](assetsRoot);
    factories["QtLayers"](layersRoot);

    const assetItems = assetsRoot.querySelector(".qframe-view")!.children;
    assert.equal(assetItems.length, 2);

    // Drag asset index 1 from Assets panel
    const assetDt = new FakeDataTransfer();
    assetItems[1].dispatch("dragstart", { dataTransfer: assetDt });
    assert.equal(assetDt.getData(panels.ASSET_DRAG_MIME), "1");
    assert.equal(assetDt.getData("text/plain"), "");

    const [mainPage, childPage] = layersRoot.querySelectorAll(".layer-page");
    const mainTree = mainPage.querySelector(".layer-tree")!.querySelector(".qframe-view")!;
    const childTree = childPage.querySelector(".layer-tree")!.querySelector(".qframe-view")!;

    // Drop on Main Layers (matches Assets target "main") -> calls asset.addLayer, NOT layer.move
    const dropMain = mainTree.children[0].dispatch("drop", { dataTransfer: assetDt });
    assert.equal(dropMain.defaultPrevented, true);
    assert.deepEqual(store.calls, [
      { method: "asset.addLayer", params: { view: "main", assetIndex: 1 } },
    ]);

    // Drop on Child Layers while Assets targets "main" -> rejected
    const overChild = childTree.dispatch("dragover", { dataTransfer: assetDt });
    assert.equal(overChild.defaultPrevented, false);
    const dropChild = childTree.dispatch("drop", { dataTransfer: assetDt });
    assert.equal(dropChild.defaultPrevented, false);
    assert.equal(store.calls.length, 1);

    // Switch activeView to "child", but assetView is still "main" -> Assets still targets "main"
    store.set("window", { activeView: "child", assetView: "main" });
    assert.equal(store.assetView, "main");
    childTree.dispatch("drop", { dataTransfer: assetDt });
    assert.equal(store.calls.length, 1);

    // Window assetView becomes "child" -> Assets now targets "child", drop on Child Layers succeeds
    store.set("window", { activeView: "child", assetView: "child" });
    assert.equal(store.assetView, "child");
    const childAssetDt = new FakeDataTransfer();
    const childAssetItems = assetsRoot.querySelector(".qframe-view")!.children;
    childAssetItems[0].dispatch("dragstart", { dataTransfer: childAssetDt });
    childTree.dispatch("drop", { dataTransfer: childAssetDt });
    assert.deepEqual(store.calls.at(-1), {
      method: "asset.addLayer",
      params: { view: "child", assetIndex: 0 },
    });

    // Layer reorder drag uses LAYER_DRAG_MIME and triggers layer.applyStructure without triggering asset.addLayer
    const layerDt = new FakeDataTransfer();
    mainTree.children[0].dispatch("dragstart", { dataTransfer: layerDt });
    assert.equal(layerDt.getData(panels.LAYER_DRAG_MIME), "0");
    mainTree.children[1].dispatch("drop", { dataTransfer: layerDt });
    assert.deepEqual(store.calls.at(-1), {
      method: "layer.applyStructure",
      params: {
        view: "main",
        rows: [
          { group: false, layer: 0, name: "Layer 0", depth: 0, visible: true },
          { group: false, layer: 1, name: "Layer 1", depth: 0, visible: true },
        ],
        movedLayer: 1,
      },
    });
  });
});

test("图层拖动：区分行上方、行下方、组里面，支持拖组、拖进拖出组、父层带跟随子层", async () => {
  const { panels } = await loadModules();
  const groupRow = { group: true, group_id: 10, name: "Group", depth: 0, collapsed: false };
  const leafRow = { group: false, layer: 2, name: "Leaf", depth: 0, visible: true };
  const fakeEl = { clientHeight: 24, getBoundingClientRect: () => ({ top: 0, height: 24 }) };

  assert.equal(panels.layerDropZone({ offsetY: 2 }, fakeEl, groupRow), "above");
  assert.equal(panels.layerDropZone({ offsetY: 12 }, fakeEl, groupRow), "inside");
  assert.equal(panels.layerDropZone({ offsetY: 22 }, fakeEl, groupRow), "below");
  assert.equal(panels.layerDropZone({ offsetY: 4 }, fakeEl, leafRow), "above");
  assert.equal(panels.layerDropZone({ offsetY: 18 }, fakeEl, leafRow), "below");

  const treeRows = [
    { group: true, group_id: 10, name: "Grp", depth: 0, collapsed: false },
    { group: false, layer: 3, name: "InGrp", depth: 1, visible: true },
    { group: false, layer: 2, name: "Parent", depth: 0, visible: true, parent: true },
    { group: false, layer: 1, name: "└ Child", depth: 1, visible: true, child: true },
    { group: false, layer: 0, name: "Base", depth: 0, visible: true },
  ];

  // 1) Drag Parent (index 2) into Grp (index 0, zone "inside"): Parent + tracked Child move together into Grp
  const intoGrp = panels.applyLayerDrop(treeRows, 2, 0, "inside");
  assert.ok(intoGrp);
  assert.equal(intoGrp.movedLayer, 2);
  assert.deepEqual(
    intoGrp.rows.map((r: any) => [r.group ? `G${r.group_id}` : `L${r.layer}`, r.depth]),
    [
      ["G10", 0],
      ["L3", 1],
      ["L2", 1],
      ["L1", 2],
      ["L0", 0],
    ],
  );

  // 2) Drag Grp (index 0) below Base (index 4, zone "below"): entire group moves below Base
  const moveGrp = panels.applyLayerDrop(treeRows, 0, 4, "below");
  assert.ok(moveGrp);
  assert.equal(moveGrp.movedLayer, -1);
  assert.deepEqual(
    moveGrp.rows.map((r: any) => [r.group ? `G${r.group_id}` : `L${r.layer}`, r.depth]),
    [
      ["L2", 0],
      ["L1", 1],
      ["L0", 0],
      ["G10", 0],
      ["L3", 1],
    ],
  );

  // 3) Drag InGrp (index 1) out of Grp below Base (index 4): Grp becomes empty and is pruned
  const outOfGrp = panels.applyLayerDrop(treeRows, 1, 4, "below");
  assert.ok(outOfGrp);
  assert.equal(outOfGrp.movedLayer, 3);
  assert.deepEqual(
    outOfGrp.rows.map((r: any) => [r.group ? `G${r.group_id}` : `L${r.layer}`, r.depth]),
    [
      ["L2", 0],
      ["L1", 1],
      ["L0", 0],
      ["L3", 0],
    ],
  );

  // 4) Tracked child row (index 3) is not draggable on its own
  assert.equal(panels.applyLayerDrop(treeRows, 3, 0, "above"), null);
});
