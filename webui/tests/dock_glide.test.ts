import assert from "node:assert/strict";
import { test } from "node:test";
import { DOCK_GLIDE_MS, dockShapeKey, glideBox, sameRect } from "../src/dock_glide.ts";
import type { DockLayoutSpec } from "../vf/src/dock.ts";

const layout = (): DockLayoutSpec => ({
  areas: {
    left: { size: 120, groups: [{ tabs: ["tools"] }] },
    right: { size: 260, groups: [{ tabs: ["options"], size: 200 }, { tabs: ["layers", "assets"], active: "layers" }] },
    top: { groups: [] },
    bottom: { size: 180, groups: [{ tabs: ["timeline"] }] },
  },
  floating: [],
});

test("停靠形状：只看哪些可见面板在哪，不看尺寸和当前页签", () => {
  const all = () => true;
  const a = layout();
  const b = layout();
  b.areas.left.size = 300;
  b.areas.right.groups[0].size = 90;
  b.areas.right.groups[1].active = "assets";
  assert.equal(dockShapeKey(a, all), dockShapeKey(b, all), "a splitter drag or a tab switch must not glide");

  const docked = layout();
  docked.areas.right.groups[1].tabs = ["assets"];
  docked.areas.left.groups.push({ tabs: ["layers"] });
  assert.notEqual(dockShapeKey(a, all), dockShapeKey(docked, all), "moving Layers to the left is a new shape");

  const floated = layout();
  floated.areas.right.groups[1].tabs = ["assets"];
  floated.floating = [{ dockId: "layers", lastArea: "right", x: 0, y: 0, width: 200, height: 200 }];
  assert.notEqual(dockShapeKey(a, all), dockShapeKey(floated, all));
  const moved = structuredClone(floated);
  moved.floating![0].x = 400;
  assert.equal(dockShapeKey(floated, all), dockShapeKey(moved, all), "dragging a floating panel around is not a new shape");

  // Moving the first of two floating panels re-appends it: still the same shape.
  const two = structuredClone(floated);
  two.floating!.push({ dockId: "tools", lastArea: "left", x: 0, y: 0, width: 100, height: 100 });
  two.areas.left.groups = [];
  const reordered = structuredClone(two);
  reordered.floating!.reverse();
  assert.equal(dockShapeKey(two, all), dockShapeKey(reordered, all));

  // Turning a two-group area from stacked to side by side is a new shape.
  const sideBySide = layout();
  sideBySide.areas.right.splitOrientation = "horizontal";
  assert.notEqual(dockShapeKey(a, all), dockShapeKey(sideBySide, all));
  // An area's default orientation written out is not.
  const explicit = layout();
  explicit.areas.right.splitOrientation = "vertical";
  assert.equal(dockShapeKey(a, all), dockShapeKey(explicit, all));

  // Hiding a panel (Windows menu, or out into a window of its own) is.
  assert.notEqual(dockShapeKey(a, all), dockShapeKey(a, (id) => id !== "timeline"));
  assert.equal(dockShapeKey(undefined, all), "");
});

test("矩形比较留半个像素的余量", () => {
  assert.ok(sameRect({ x: 0, y: 0, width: 10, height: 10 }, { x: 0.3, y: 0, width: 10.2, height: 10 }));
  assert.ok(!sameRect({ x: 0, y: 0, width: 10, height: 10 }, { x: 0, y: 0, width: 12, height: 10 }));
});

function fakeEl(canvasSizes: Array<[number, number]>) {
  // One listener map per animation; finish() / cancel() fire the FIRST one's.
  const animations: Array<Map<string, () => void>> = [];
  const calls: unknown[] = [];
  const canvases = canvasSizes.map(([w, h]) => ({ clientWidth: w, clientHeight: h, style: { width: "", height: "" } }));
  const el = {
    style: { overflow: "" },
    querySelectorAll: () => canvases,
    animate: (frames: unknown, opts: unknown) => {
      calls.push(frames, opts);
      const listeners = new Map<string, () => void>();
      animations.push(listeners);
      return {
        addEventListener: (type: string, fn: () => void) => listeners.set(type, fn),
        // As in a browser: the cancel event comes later, not from cancel().
        cancel: () => calls.push("cancel"),
      };
    },
  };
  return {
    el: el as unknown as HTMLElement,
    calls,
    canvases,
    finish: () => animations[0]?.get("finish")?.(),
    cancel: () => animations[0]?.get("cancel")?.(),
  };
}

test("滑动时内容按最终尺寸、外框裁住；画板 canvas 钉在最终像素，结束后还原", () => {
  const { el, calls, canvases, finish } = fakeEl([[640, 480], [0, 0]]);
  const glide = glideBox(el, { x: 0, y: 0, width: 400, height: 300 }, { x: 120, y: 0, width: 520, height: 300 });
  assert.ok(glide);
  assert.deepEqual(calls[0], [
    { left: "0px", top: "0px", width: "400px", height: "300px" },
    { left: "120px", top: "0px", width: "520px", height: "300px" },
  ]);
  assert.equal((calls[1] as { duration: number }).duration, DOCK_GLIDE_MS);
  assert.equal(el.style.overflow, "hidden");
  assert.deepEqual(canvases[0].style, { width: "640px", height: "480px" });
  assert.deepEqual(canvases[1].style, { width: "", height: "" }, "a hidden canvas has no size to keep");
  finish();
  assert.equal(el.style.overflow, "");
  assert.deepEqual(canvases[0].style, { width: "", height: "" });
});

test("被别处取消（cancel 事件）也还原", () => {
  const { el, canvases, cancel } = fakeEl([[300, 200]]);
  glideBox(el, { x: 0, y: 0, width: 1, height: 1 }, { x: 5, y: 5, width: 2, height: 2 });
  cancel();
  assert.deepEqual(canvases[0].style, { width: "", height: "" });
});

test("stop() 当场还原，不等稍后才来的 cancel 事件；下一轮滑动从原样开始，迟到的事件不再动它", () => {
  const { el, calls, canvases, cancel } = fakeEl([[300, 200]]);
  const first = glideBox(el, { x: 0, y: 0, width: 1, height: 1 }, { x: 5, y: 5, width: 2, height: 2 })!;
  first.stop();
  assert.ok(calls.includes("cancel"));
  assert.equal(el.style.overflow, "");
  assert.deepEqual(canvases[0].style, { width: "", height: "" });
  // The next glide pins again; the first glide's late cancel event must not undo that.
  glideBox(el, { x: 5, y: 5, width: 2, height: 2 }, { x: 9, y: 9, width: 3, height: 3 });
  cancel();
  assert.equal(el.style.overflow, "hidden");
  assert.deepEqual(canvases[0].style, { width: "300px", height: "200px" });
});

test("不能动画的环境不滑", () => {
  const el = { style: {}, querySelectorAll: () => [] } as unknown as HTMLElement;
  assert.equal(glideBox(el, { x: 0, y: 0, width: 1, height: 1 }, { x: 1, y: 1, width: 1, height: 1 }), null);
});
