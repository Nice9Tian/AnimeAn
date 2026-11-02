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

  // Hiding a panel (Windows menu, or out into a window of its own) is.
  assert.notEqual(dockShapeKey(a, all), dockShapeKey(a, (id) => id !== "timeline"));
  assert.equal(dockShapeKey(undefined, all), "");
});

test("矩形比较留半个像素的余量", () => {
  assert.ok(sameRect({ x: 0, y: 0, width: 10, height: 10 }, { x: 0.3, y: 0, width: 10.2, height: 10 }));
  assert.ok(!sameRect({ x: 0, y: 0, width: 10, height: 10 }, { x: 0, y: 0, width: 12, height: 10 }));
});

function fakeEl(canvasSizes: Array<[number, number]>) {
  const listeners = new Map<string, () => void>();
  const calls: unknown[] = [];
  const canvases = canvasSizes.map(([w, h]) => ({ clientWidth: w, clientHeight: h, style: { width: "", height: "" } }));
  const el = {
    style: { overflow: "" },
    querySelectorAll: () => canvases,
    animate: (frames: unknown, opts: unknown) => {
      calls.push(frames, opts);
      return {
        addEventListener: (type: string, fn: () => void) => listeners.set(type, fn),
      };
    },
  };
  return { el: el as unknown as HTMLElement, calls, canvases, finish: () => listeners.get("finish")?.(), cancel: () => listeners.get("cancel")?.() };
}

test("滑动时内容按最终尺寸、外框裁住；画板 canvas 钉在最终像素，结束后还原", () => {
  const { el, calls, canvases, finish } = fakeEl([[640, 480], [0, 0]]);
  const anim = glideBox(el, { x: 0, y: 0, width: 400, height: 300 }, { x: 120, y: 0, width: 520, height: 300 });
  assert.ok(anim);
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

test("被新的滑动打断（cancel）也还原", () => {
  const { el, canvases, cancel } = fakeEl([[300, 200]]);
  glideBox(el, { x: 0, y: 0, width: 1, height: 1 }, { x: 5, y: 5, width: 2, height: 2 });
  cancel();
  assert.deepEqual(canvases[0].style, { width: "", height: "" });
});

test("不能动画的环境不滑", () => {
  const el = { style: {}, querySelectorAll: () => [] } as unknown as HTMLElement;
  assert.equal(glideBox(el, { x: 0, y: 0, width: 1, height: 1 }, { x: 1, y: 1, width: 1, height: 1 }), null);
});
