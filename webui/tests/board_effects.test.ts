import assert from "node:assert/strict";
import { test } from "node:test";
import {
  BoardEffects,
  FRAME_WAIT_MS,
  effectDrawable,
  effectProgress,
  flatPathLength,
  paintEffect,
  traceFlatPath,
} from "../src/board/effects.ts";

test("扁平路径的长度：直线、闭合、三次曲线", () => {
  assert.equal(flatPathLength([0, 0, 0, 1, 3, 4]), 5);
  assert.equal(flatPathLength([0, 0, 0, 1, 10, 0, 1, 10, 10, 3]), 10 + 10 + Math.hypot(10, 10));
  // A straight cubic is as long as its chord.
  assert.ok(Math.abs(flatPathLength([0, 0, 0, 2, 10, 0, 20, 0, 30, 0]) - 30) < 1e-9);
  // A truncated or unknown op ends the walk instead of reading garbage.
  assert.equal(flatPathLength([0, 0, 0, 1, 3]), 0);
  assert.equal(flatPathLength([0, 0, 0, 9, 1, 1]), 0);
});

test("扁平路径按命令画进 Path2D", () => {
  const calls: string[] = [];
  traceFlatPath(
    {
      moveTo: (x, y) => calls.push(`M${x},${y}`),
      lineTo: (x, y) => calls.push(`L${x},${y}`),
      bezierCurveTo: (...a) => calls.push(`C${a.join(",")}`),
      closePath: () => calls.push("Z"),
    },
    [0, 1, 2, 1, 3, 4, 2, 5, 6, 7, 8, 9, 10, 3],
  );
  assert.deepEqual(calls, ["M1,2", "L3,4", "C5,6,7,8,9,10", "Z"]);
});

test("进度：画线匀速，铺开和淡入两头缓，时长为 0 直接到底", () => {
  assert.equal(effectProgress("radial", 0, 600), 0);
  assert.equal(effectProgress("radial", 600, 600), 1);
  assert.equal(effectProgress("radial", 300, 600), 0.5);
  assert.ok(effectProgress("radial", 150, 600) < 0.25);
  assert.ok(effectProgress("fade", 450, 600) > 0.75);
  assert.equal(effectProgress("stroke", 100, 400), 0.25);
  assert.equal(effectProgress("stroke", 900, 400), 1);
  assert.equal(effectProgress("fade", 0, 0), 1);
});

test("缺几何的效果不画", () => {
  assert.ok(effectDrawable({ kind: "radial", center: [1, 2], radius: 5 }));
  assert.ok(!effectDrawable({ kind: "radial", center: [1, 2], radius: 0 }));
  assert.ok(!effectDrawable({ kind: "radial", radius: 5 }));
  assert.ok(effectDrawable({ kind: "stroke", path: [0, 0, 0, 1, 5, 0], width: 3 }));
  assert.ok(!effectDrawable({ kind: "stroke", path: [0, 0, 0, 1, 0, 0], width: 3 }));
  assert.ok(effectDrawable({ kind: "fade" }));
  assert.ok(!effectDrawable({ kind: "wobble" }));
});

function recordingCtx() {
  const log: string[] = [];
  const ctx: Record<string, unknown> = { log };
  for (const name of ["save", "restore", "clearRect", "clip", "fill", "stroke", "fillRect", "beginPath", "arc", "drawImage", "setLineDash"]) {
    ctx[name] = (...args: unknown[]) => log.push(`${name}(${args.filter((a) => typeof a !== "object").join(",")})`);
  }
  return new Proxy(ctx, {
    set(target, key, value) {
      if (typeof key === "string" && key !== "log") log.push(`${key}=${value}`);
      target[key as string] = value;
      return true;
    },
  }) as unknown as CanvasRenderingContext2D & { log: string[] };
}

test("铺开：区域做蒙版，减去已铺开的圆，再填上之前的画面", () => {
  const ctx = recordingCtx();
  const clip = {} as Path2D;
  paintEffect(ctx, { kind: "radial", center: [10, 20], radius: 100 }, {} as CanvasImageSource, 0.5, 300, 200, { clip });
  const log = ctx.log.join(" ");
  assert.match(log, /fill\(evenodd\).*stroke\(\).*globalCompositeOperation=destination-out.*arc\(10,20,50,0,/);
  assert.match(log, /globalCompositeOperation=source-in drawImage\(0,0,300,200\) restore\(\)$/);
});

test("沿线画出：已画出的一段用虚线减掉，宽度含边距", () => {
  const ctx = recordingCtx();
  paintEffect(ctx, { kind: "stroke", width: 6, margin: 3 }, {} as CanvasImageSource, 0.25, 300, 200, { path: {} as Path2D, length: 80 });
  const log = ctx.log.join(" ");
  assert.match(log, /lineWidth=12 stroke\(\) globalCompositeOperation=destination-out lineWidth=14 setLineDash\(\) stroke\(\)/);
});

test("淡入：之前的画面按 1 - 进度 的不透明度盖上；到底就清空", () => {
  const ctx = recordingCtx();
  paintEffect(ctx, { kind: "fade" }, {} as CanvasImageSource, 0.25, 300, 200, {});
  assert.match(ctx.log.join(" "), /globalAlpha=0.75 drawImage\(0,0,300,200\)/);
  const done = recordingCtx();
  paintEffect(done, { kind: "fade" }, {} as CanvasImageSource, 1, 300, 200, {});
  assert.ok(!done.log.some((line) => line.startsWith("drawImage")));
});

// Just enough DOM for BoardEffects: canvases whose 2D context records draws.
function fakeDom() {
  const made: Array<{ el: Record<string, unknown>; ctx: CanvasRenderingContext2D & { log: string[] } }> = [];
  const doc = {
    createElement() {
      const ctx = recordingCtx();
      (ctx as unknown as Record<string, unknown>).setTransform = () => ctx.log.push("setTransform");
      const el: Record<string, unknown> = { width: 0, height: 0, getContext: () => ctx, remove() {} };
      made.push({ el, ctx });
      return el;
    },
  };
  (globalThis as Record<string, unknown>).document = doc;
  (globalThis as Record<string, unknown>).Path2D = class {
    moveTo() {}
    lineTo() {}
    bezierCurveTo() {}
    closePath() {}
  };
  return made;
}

function harness() {
  const made = fakeDom();
  let now = 0;
  const frames: Array<() => void> = [];
  const board = {
    width: 600,
    height: 400,
    clientWidth: 300,
    clientHeight: 200,
    getClientRects: () => [1],
    after(node: Record<string, unknown>) {
      node.previousSibling = board;
    },
  };
  const effects = new BoardEffects({
    canvas: () => board as unknown as HTMLCanvasElement,
    now: () => now,
    raf: (fn) => {
      frames.push(() => fn(now));
      return frames.length;
    },
    cancelRaf: () => {},
  });
  const step = (ms: number) => {
    now += ms;
    const run = frames.splice(0);
    for (const fn of run) fn();
  };
  return { made, board, effects, step, setNow: (v: number) => (now = v) };
}

test("刚连上、画板还没画过任何一帧时不播（没有「之前」）", () => {
  const { effects } = harness();
  effects.play({ kind: "fade", duration: 300 });
  assert.equal(effects.active, false);
});

test("效果到达就盖上之前的画面，等下一帧落地才开始计时，播完清空", () => {
  const { made, effects, step } = harness();
  effects.frame();
  effects.play({ kind: "fade", duration: 300 });
  assert.equal(effects.active, true);
  // made[0] is the snapshot, made[1] the overlay.
  const overlay = made[1].ctx.log;
  assert.ok(overlay.includes("globalAlpha=1"), "covered at once, before any animation frame");
  step(1000); // no frame yet: the clock has not started
  assert.equal(effects.active, true);
  effects.frame();
  step(150);
  assert.ok(overlay.includes("globalAlpha=0.5"));
  step(200);
  assert.equal(effects.active, false);
});

test("一直等不到新帧就放弃", () => {
  const { effects, step } = harness();
  effects.frame();
  effects.play({ kind: "fade", duration: 300 });
  step(FRAME_WAIT_MS + 1);
  assert.equal(effects.active, false);
});

test("画板尺寸变了就停（之前的画面对不上了）", () => {
  const { board, effects } = harness();
  effects.frame();
  effects.play({ kind: "radial", center: [5, 5], radius: 20, duration: 300 });
  board.width = 800;
  effects.frame();
  assert.equal(effects.active, false);
});

test("系统要求减少动态效果时不播", () => {
  fakeDom();
  const effects = new BoardEffects({
    canvas: () => ({ width: 10, height: 10, getClientRects: () => [1], after() {} }) as unknown as HTMLCanvasElement,
    reducedMotion: () => true,
    raf: () => 0,
    cancelRaf: () => {},
  });
  effects.frame();
  effects.play({ kind: "fade", duration: 300 });
  assert.equal(effects.active, false);
});
