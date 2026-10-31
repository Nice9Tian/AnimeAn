import assert from "node:assert/strict";
import fs from "node:fs";
import { describe, it } from "node:test";
import { BoardRenderer, type Canvas2DContextLike, type CanvasLike } from "../src/board/renderer.ts";

interface RecordedCall {
  op: string;
  args: unknown[];
}

function createMockCanvas(): {
  canvas: CanvasLike;
  calls: RecordedCall[];
  ctx: Canvas2DContextLike;
} {
  const calls: RecordedCall[] = [];
  const ctx: Canvas2DContextLike = {
    fillStyle: "#000000",
    strokeStyle: "#000000",
    lineWidth: 1,
    lineCap: "butt",
    lineJoin: "miter",
    miterLimit: 2,
    lineDashOffset: 0,
    globalAlpha: 1,
    globalCompositeOperation: "source-over",
    imageSmoothingEnabled: false,
    save() {
      calls.push({ op: "save", args: [] });
    },
    restore() {
      calls.push({ op: "restore", args: [] });
    },
    setTransform(a, b, c, d, e, f) {
      calls.push({ op: "setTransform", args: [a, b, c, d, e, f] });
    },
    clearRect(x, y, w, h) {
      calls.push({ op: "clearRect", args: [x, y, w, h] });
    },
    fillRect(x, y, w, h) {
      calls.push({ op: "fillRect", args: [x, y, w, h, this.fillStyle] });
    },
    strokeRect(x, y, w, h) {
      calls.push({ op: "strokeRect", args: [x, y, w, h, this.strokeStyle, this.lineWidth] });
    },
    beginPath() {
      calls.push({ op: "beginPath", args: [] });
    },
    moveTo(x, y) {
      calls.push({ op: "moveTo", args: [x, y] });
    },
    lineTo(x, y) {
      calls.push({ op: "lineTo", args: [x, y] });
    },
    bezierCurveTo(cp1x, cp1y, cp2x, cp2y, x, y) {
      calls.push({ op: "bezierCurveTo", args: [cp1x, cp1y, cp2x, cp2y, x, y] });
    },
    rect(x, y, w, h) {
      calls.push({ op: "rect", args: [x, y, w, h] });
    },
    closePath() {
      calls.push({ op: "closePath", args: [] });
    },
    fill(fillRule) {
      calls.push({ op: "fill", args: [fillRule, this.fillStyle] });
    },
    stroke() {
      calls.push({
        op: "stroke",
        args: [this.strokeStyle, this.lineWidth, this.lineCap, this.lineJoin],
      });
    },
    clip(fillRule) {
      calls.push({ op: "clip", args: [fillRule] });
    },
    setLineDash(segments) {
      calls.push({ op: "setLineDash", args: [segments] });
    },
    createLinearGradient(x0, y0, x1, y1) {
      const stops: Array<[number, string]> = [];
      calls.push({ op: "createLinearGradient", args: [x0, y0, x1, y1, stops] });
      return {
        addColorStop(offset: number, color: string) {
          stops.push([offset, color]);
        },
      } as unknown as CanvasGradient;
    },
    createRadialGradient(x0, y0, r0, x1, y1, r1) {
      const stops: Array<[number, string]> = [];
      calls.push({ op: "createRadialGradient", args: [x0, y0, r0, x1, y1, r1, stops] });
      return {
        addColorStop(offset: number, color: string) {
          stops.push([offset, color]);
        },
      } as unknown as CanvasGradient;
    },
    drawImage(image, sx, sy, sw, sh, dx, dy, dw, dh) {
      calls.push({ op: "drawImage", args: [image, sx, sy, sw, sh, dx, dy, dw, dh] });
    },
  };

  const canvas: CanvasLike = {
    width: 0,
    height: 0,
    getContext: () => ctx,
  };

  return { canvas, calls, ctx };
}

describe("BoardRenderer", () => {
  it("replays real backend fixture board_ops.json accurately", () => {
    const rawJson = fs.readFileSync(new URL("./fixtures/board_ops.json", import.meta.url), "utf8");
    const ops = JSON.parse(rawJson) as unknown[][];

    const main = createMockCanvas();
    const renderer = new BoardRenderer(main.canvas, {
      view: "main",
      getDpr: () => 1,
    });

    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 567,
      h: 690,
      keep: 0,
      ops,
    });

    const fillRects = main.calls.filter((c) => c.op === "fillRect");
    assert.equal(fillRects.length, 2, "a=0 的画刷不应调用 fillRect，总共应只有 2 次 fillRect");

    // 第一次 fillRect 的 fillStyle 是 rgb(207,208,211)
    assert.equal(fillRects[0].args[4], "rgb(207,208,211)");

    // 白色纸面 fillRect 尺寸是 1280×720
    assert.equal(fillRects[1].args[0], 0);
    assert.equal(fillRects[1].args[1], 0);
    assert.equal(fillRects[1].args[2], 1280);
    assert.equal(fillRects[1].args[3], 720);
    assert.equal(fillRects[1].args[4], "rgb(255,255,255)");

    // 最后一笔 stroke 的 lineWidth 约等于 5，lineCap 是 round，lineJoin 是 round
    const strokes = main.calls.filter((c) => c.op === "stroke");
    assert.ok(strokes.length >= 1, "应至少有一次 stroke 调用");
    const lastStroke = strokes[strokes.length - 1];
    const lineWidth = Number(lastStroke.args[1]);
    assert.ok(Math.abs(lineWidth - 5) < 1e-3, `lineWidth 应约等于 5，实际为 ${lineWidth}`);
    assert.equal(lastStroke.args[2], "round");
    assert.equal(lastStroke.args[3], "round");
  });

  it("splices ops table by keep, replays only suffix using cache canvas, sizes by DPR, and sends ack", () => {
    const main = createMockCanvas();
    const caches: Array<ReturnType<typeof createMockCanvas>> = [];
    const acks: Array<{ view: string; frame: number }> = [];

    const renderer = new BoardRenderer(main.canvas, {
      view: "main",
      getDpr: () => 2,
      createCacheCanvas: (w, h) => {
        const c = createMockCanvas();
        c.canvas.width = w;
        c.canvas.height = h;
        caches.push(c);
        return c.canvas;
      },
      onAck: (view, frame) => {
        acks.push({ view, frame });
      },
    });

    // 预注入一张图片
    renderer.putImage(
      { t: "image", key: "img1", w: 10, h: 10, bytes: 400 },
      new Uint8Array(400),
    );

    // 第 1 帧：keep=0，4 条基础指令（严格遵循规格格式）
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: 500,
      h: 400,
      keep: 0,
      ops: [
        ["T", 1, 0, 0, 1, 0, 0],
        ["P", 0, 0, 0, 0, 0, 0, 0, 2, 0, null],
        ["B", 255, 255, 255, 255],
        ["R", 0, 0, 500, 400],
      ],
    });

    assert.equal(main.canvas.width, 1000);
    assert.equal(main.canvas.height, 800);
    assert.equal(renderer.getOps().length, 4);
    assert.deepEqual(acks, [{ view: "main", frame: 1 }]);
    assert.equal(
      main.calls.filter((c) => c.op === "fillRect").length,
      1,
      "第 1 帧应在主画布绘制背景矩形",
    );

    // 清空主画布调用记录，验证第 2 帧增量重放
    main.calls.length = 0;

    // 第 2 帧：keep=4，追加 3 条新指令（不重放前 4 条中的 fillRect，只贴缓存并执行后缀）
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 2,
      w: 500,
      h: 400,
      keep: 4,
      ops: [
        ["B", 0, 0, 0, 0],
        ["P", 17, 34, 51, 255, 3, 2, 2, 2, 0, [2, 4]],
        ["S", [0, 10, 20, 1, 30, 40, 2, 50, 60, 70, 80, 90, 100, 3], 0],
      ],
    });

    assert.equal(renderer.getOps().length, 7);
    assert.deepEqual(acks, [
      { view: "main", frame: 1 },
      { view: "main", frame: 2 },
    ]);

    // 主画布应当先 drawImage 贴入缓存画布，不再执行第 1 帧的 fillRect(0, 0, 500, 400)
    assert.equal(
      main.calls.filter((c) => c.op === "fillRect").length,
      0,
      "增量帧不应重放已缓存前缀里的 fillRect",
    );
    const cacheBlit = main.calls.find((c) => c.op === "drawImage" && c.args[0] === caches[0].canvas);
    assert.ok(cacheBlit, "增量帧应把缓存画布贴到主画布");

    // 验证 dash 单位按线宽(3)换算：[2*3, 4*3] = [6, 12]
    const dashCall = main.calls.find(
      (c) => c.op === "setLineDash" && Array.isArray(c.args[0]) && (c.args[0] as number[]).length === 2,
    );
    assert.ok(dashCall);
    assert.deepEqual(dashCall.args[0], [6, 12]);

    // 验证 S 指令的扁平路径 moveTo(0), lineTo(1), bezierCurveTo(2), closePath(3), stroke
    assert.ok(main.calls.some((c) => c.op === "moveTo" && c.args[0] === 10 && c.args[1] === 20));
    assert.ok(main.calls.some((c) => c.op === "lineTo" && c.args[0] === 30 && c.args[1] === 40));
    assert.ok(main.calls.some((c) => c.op === "bezierCurveTo" && c.args[4] === 90 && c.args[5] === 100));
    assert.ok(main.calls.some((c) => c.op === "closePath"));
    assert.equal(main.calls.filter((c) => c.op === "stroke").length, 1);

    // 第 3 帧：keep=5（回退掉了第 2 帧的 P 和 S，保留前 5 条），追加 T、O、M、K、G、cosmetic P、S、H、I
    main.calls.length = 0;
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 3,
      w: 500,
      h: 400,
      keep: 5,
      ops: [
        ["T", 2, 0, 0, 2, 10, 20],
        ["O", 0.75],
        ["M", "multiply"],
        ["K", [0, 0, 0, 1, 200, 0, 1, 200, 200, 1, 0, 200, 3], 0],
        ["G", "linear", [0, 0, 100, 100], [[0, 255, 0, 0, 255], [1, 0, 0, 255, 255]]],
        ["P", 0, 255, 0, 255, 2, 0, 0, 2, 1, null], // cosmetic = 1
        ["B", 0, 0, 0, 0],
        ["S", [0, 5, 5, 1, 15, 25], 0],
        ["H", 1],
        ["I", "img1", 0, 0, 10, 10, 30, 40, 20, 20],
      ],
    });

    assert.equal(renderer.getOps().length, 15);
    assert.deepEqual(acks[2], { view: "main", frame: 3 });
    assert.equal(main.ctx.globalAlpha, 0.75);
    assert.equal(main.ctx.globalCompositeOperation, "multiply");
    assert.equal(main.ctx.imageSmoothingEnabled, true);
    assert.ok(main.calls.some((c) => c.op === "clip" && c.args[0] === "evenodd"));
    assert.ok(main.calls.some((c) => c.op === "createLinearGradient"));

    // 验证 cosmetic 画笔：坐标经过 T=[2,0,0,2,10,20] 变换为 (2*5+10=20, 2*5+20=30) -> (2*15+10=40, 2*25+20=70)，线宽保持 2
    assert.ok(main.calls.some((c) => c.op === "moveTo" && c.args[0] === 20 && c.args[1] === 30));
    assert.ok(main.calls.some((c) => c.op === "lineTo" && c.args[0] === 40 && c.args[1] === 70));
    assert.ok(main.calls.some((c) => c.op === "stroke" && c.args[1] === 2));

    // 验证 I 指令绘制缓存图片
    assert.ok(
      main.calls.some(
        (c) => c.op === "drawImage" && c.args[1] === 0 && c.args[2] === 0 && c.args[5] === 30 && c.args[6] === 40,
      ),
    );
  });
});
