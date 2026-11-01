import { GlBoardRenderer, type GlCanvasLike } from "../src/board/glrenderer.ts";
import { BoardRenderer, type CanvasLike } from "../src/board/renderer.ts";
import type { ImageMeta } from "../src/net.ts";

export interface IncrementalFrameResult {
  frame: number;
  keep: number;
  opsCount: number;
  passRate: number;
  passPercent: number;
  diffPixels: number;
}

export interface WebglCompareResult {
  done: boolean;
  error?: string;
  passRate: number;
  passPercent: number;
  totalPixels: number;
  matchedPixels: number;
  diffPixels: number;
  meanAbsChannelDiff: number;
  canvas2dMs: number;
  webgl2Ms: number;
  webgl2ColdMs: number;
  drawCalls: number;
  speedup: number;
  incremental: IncrementalFrameResult[];
  glErrors: number[];
  benchmark: {
    strokes: number;
    segmentsPerStroke: number;
    canvas2dMs: number;
    webgl2ColdMs: number;
    webgl2CachedMs: number;
    drawCalls: number;
    speedup: number;
  };
}

declare global {
  interface Window {
    __webglCompare?: WebglCompareResult;
  }
}

function createMulberry32(seed: number): () => number {
  let a = seed | 0;
  return () => {
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

function createTestImage(w: number, h: number): { meta: ImageMeta; data: Uint8Array } {
  const data = new Uint8Array(w * h * 4);
  for (let y = 0; y < h; y++) {
    for (let x = 0; x < w; x++) {
      const idx = (y * w + x) * 4;
      const checker = ((x >> 2) + (y >> 2)) & 1;
      data[idx] = checker ? 230 : 40;
      data[idx + 1] = Math.round((x / Math.max(1, w - 1)) * 220);
      data[idx + 2] = Math.round((y / Math.max(1, h - 1)) * 220);
      data[idx + 3] = 220;
    }
  }
  return {
    meta: { t: "image", key: "test_img_1", w, h, bytes: data.byteLength },
    data,
  };
}

function buildComparisonOps(w: number, h: number, seed = 20261030): unknown[][] {
  const rand = createMulberry32(seed);
  const ops: unknown[][] = [];

  // 1. Background rectangle (T, P, B, R)
  ops.push(["T", 1, 0, 0, 1, 0, 0]);
  ops.push(["P", 0, 0, 0, 0, 1, 0, 0, 2, 0, null]);
  ops.push(["B", 245, 246, 248, 255]);
  ops.push(["R", 0, 0, w, h]);

  // 2. Linear & radial gradients (G)
  ops.push([
    "G",
    "linear",
    [20, 20, 260, 160],
    [
      [0, 240, 80, 70, 255],
      [0.5, 250, 200, 60, 255],
      [1, 60, 140, 240, 255],
    ],
  ]);
  ops.push(["P", 30, 30, 40, 255, 3, 0, 0, 2, 0, null]);
  ops.push(["R", 20, 20, 240, 140]);

  ops.push([
    "G",
    "radial",
    [390, 80, 10, 410, 90, 85],
    [
      [0, 255, 255, 180, 255],
      [0.6, 80, 200, 160, 230],
      [1, 20, 60, 120, 255],
    ],
  ]);
  ops.push(["P", 0, 0, 0, 0, 1, 0, 0, 2, 0, null]);
  ops.push(["S", [0, 310, 20, 1, 510, 20, 1, 510, 170, 1, 310, 170, 3], 0]);

  // 3. Fill rules: concentric rings with evenodd (0) vs nonzero (1)
  const makeDoubleRing = (cx: number, cy: number): number[] => [
    0, cx - 55, cy - 55, 1, cx + 55, cy - 55, 1, cx + 55, cy + 55, 1, cx - 55, cy + 55, 3,
    0, cx - 28, cy - 28, 1, cx + 28, cy - 28, 1, cx + 28, cy + 28, 1, cx - 28, cy + 28, 3,
  ];
  ops.push(["B", 70, 130, 210, 220]);
  ops.push(["P", 20, 40, 80, 255, 2, 0, 0, 2, 0, null]);
  ops.push(["S", makeDoubleRing(600, 95), 0]); // evenodd -> hole in middle
  ops.push(["B", 210, 95, 70, 220]);
  ops.push(["S", makeDoubleRing(725, 95), 1]); // nonzero -> solid in middle

  // 4. Semi-transparent self-overlapping cubic bezier strokes & caps/joins/dashes
  const caps = [0, 1, 2];
  const joins = [0, 1, 2];
  for (let i = 0; i < 6; i++) {
    const bx = 30 + i * 125;
    const by = 220;
    const r = Math.floor(40 + rand() * 180);
    const g = Math.floor(40 + rand() * 180);
    const b = Math.floor(40 + rand() * 180);
    const dash = i % 2 === 1 ? [3, 2] : null;
    ops.push(["B", 0, 0, 0, 0]);
    ops.push(["O", 0.65]);
    ops.push(["P", r, g, b, 190, 6, caps[i % 3], joins[(i >> 1) % 3], 3, 0, dash]);
    ops.push([
      "S",
      [
        0, bx, by,
        2, bx + 90, by - 40, bx + 90, by + 110, bx + 10, by + 60,
        2, bx - 30, by + 10, bx + 100, by - 20, bx + 85, by + 95,
      ],
      0,
    ]);
  }
  ops.push(["O", 1]);

  // 5. Clipping K (evenodd & nonzero) under transform T, then reset K=null
  ops.push(["T", 1, 0, 0, 1, 40, 360]);
  ops.push(["K", [0, 0, 0, 1, 180, 0, 1, 180, 140, 1, 0, 140, 3, 0, 40, 30, 1, 140, 30, 1, 140, 110, 1, 40, 110, 3], 0]);
  ops.push(["B", 120, 200, 130, 255]);
  ops.push(["P", 30, 90, 40, 255, 4, 2, 2, 2, 0, null]);
  ops.push(["R", -20, -20, 220, 180]);
  ops.push(["S", [0, -10, 70, 1, 200, 70], 0]);
  ops.push(["K", null, 0]);

  // 6. Image I with nearest (H=0) and smooth (H=1)
  ops.push(["T", 1, 0, 0, 1, 0, 0]);
  ops.push(["H", 0]);
  ops.push(["I", "test_img_1", 0, 0, 16, 16, 260, 360, 96, 96]);
  ops.push(["H", 1]);
  ops.push(["I", "test_img_1", 2, 2, 12, 12, 375, 360, 96, 96]);

  // 7. Cosmetic pen under scaling transform
  ops.push(["T", 2.5, 0, 0, 2.5, 500, 360]);
  ops.push(["B", 240, 220, 255, 255]);
  ops.push(["P", 90, 20, 140, 255, 3, 1, 0, 2, 1, null]); // cosmetic = 1
  ops.push(["R", 0, 0, 45, 35]);
  ops.push(["B", 0, 0, 0, 0]);
  ops.push(["S", [0, 5, 45, 2, 15, 25, 35, 65, 50, 40], 0]);
  ops.push(["T", 1, 0, 0, 1, 0, 0]);

  // 8. Porter-Duff composite modes (M): source-over, destination-over, lighter
  ops.push(["B", 60, 150, 230, 200]);
  ops.push(["P", 0, 0, 0, 0, 1, 0, 0, 2, 0, null]);
  ops.push(["R", 650, 370, 80, 80]);
  ops.push(["M", "lighter"]);
  ops.push(["B", 200, 80, 60, 180]);
  ops.push(["R", 680, 400, 80, 80]);
  ops.push(["M", "source-over"]);

  return ops;
}

function build3000StrokesOps(w: number, h: number, strokeCount = 3000, segments = 40): unknown[][] {
  const ops: unknown[][] = [
    ["T", 1, 0, 0, 1, 0, 0],
    ["P", 0, 0, 0, 0, 1, 0, 0, 2, 0, null],
    ["B", 255, 255, 255, 255],
    ["R", 0, 0, w, h],
    ["B", 0, 0, 0, 0],
    ["P", 20, 20, 30, 255, 3, 2, 2, 2, 0, null],
  ];

  for (let i = 0; i < strokeCount; i++) {
    let x = w * 0.5 + w * 0.45 * Math.sin(i * 0.7);
    let y = h * 0.5 + h * 0.45 * Math.cos(i * 1.3);
    const path: number[] = [0, x, y];
    for (let j = 0; j < segments; j++) {
      const a = (i + j) * 0.37;
      const b = (i - j) * 0.53;
      const x1 = x + 10 * Math.sin(a);
      const y1 = y + 10 * Math.cos(b);
      const x2 = x1 + 10 * Math.cos(a);
      const y2 = y1 + 10 * Math.sin(b);
      const x3 = x2 + 10 * Math.sin(b);
      const y3 = y2 + 10 * Math.cos(a);
      path.push(2, x1, y1, x2, y2, x3, y3);
      x = x3;
      y = y3;
    }
    ops.push(["S", path, 0]);
  }
  return ops;
}

function median(values: ReadonlyArray<number>): number {
  if (values.length === 0) return 0;
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[sorted.length >> 1];
}

function buildIncrementalFrames(
  allOps: unknown[][],
  seed = 20261101,
): Array<{ keep: number; ops: unknown[][] }> {
  const rand = createMulberry32(seed);
  const total = allOps.length;
  const clipStart = allOps.findIndex((op) => op[0] === "K" && Array.isArray(op[1]));
  // Cut inside active clip before the clipped R/S drawing ops so suffix draws under restored clip.
  const clipCut = clipStart >= 0 ? clipStart + 2 + Math.floor(rand() * 2) : Math.floor(total * 0.65);

  const k1 = 0;
  const len1 = 20 + Math.floor(rand() * 5);

  // cachedCount (len1) > keep (k2), suffix lengthens
  const k2 = 12 + Math.floor(rand() * 4);
  const len2 = clipCut + 5 + Math.floor(rand() * 4);

  // cachedCount (k2) === keep (k2), suffix shortens
  const k3 = k2;
  const len3 = clipCut + 1 + Math.floor(rand() * 3);

  // cachedCount (k2) < keep (clipCut, inside active clip), suffix shortens
  const k4 = clipCut;
  const len4 = Math.min(total - 4, clipCut + 12 + Math.floor(rand() * 4));

  // cachedCount (clipCut) === keep (clipCut, inside active clip), suffix lengthens to total
  const k5 = clipCut;
  const len5 = total;

  // cachedCount (clipCut) < keep (k6, past clip reset), suffix shortens to finish scene
  const k6 = Math.min(total - 2, clipCut + 10 + Math.floor(rand() * 4));
  const len6 = total;

  const specs = [
    { keep: k1, end: len1 },
    { keep: k2, end: len2 },
    { keep: k3, end: len3 },
    { keep: k4, end: len4 },
    { keep: k5, end: len5 },
    { keep: k6, end: len6 },
  ];

  return specs.map(({ keep, end }) => ({
    keep,
    ops: allOps.slice(keep, end),
  }));
}

function compareCanvases(
  ctx2d: CanvasRenderingContext2D,
  cgl: HTMLCanvasElement,
  diffCtx: CanvasRenderingContext2D,
  w: number,
  h: number,
  channelTol: number,
  writeDiffImage: boolean,
): {
  passRate: number;
  passPercent: number;
  matchedPixels: number;
  diffPixels: number;
  meanAbsChannelDiff: number;
} {
  const pixels2d = ctx2d.getImageData(0, 0, w, h).data;
  diffCtx.clearRect(0, 0, w, h);
  diffCtx.drawImage(cgl, 0, 0);
  const pixelsGl = diffCtx.getImageData(0, 0, w, h).data;

  const diffImg = writeDiffImage ? diffCtx.createImageData(w, h) : null;
  const totalPixels = w * h;
  let matchedPixels = 0;
  let totalAbsDiff = 0;

  for (let i = 0; i < pixels2d.length; i += 4) {
    const dr = Math.abs(pixels2d[i] - pixelsGl[i]);
    const dg = Math.abs(pixels2d[i + 1] - pixelsGl[i + 1]);
    const db = Math.abs(pixels2d[i + 2] - pixelsGl[i + 2]);
    const da = Math.abs(pixels2d[i + 3] - pixelsGl[i + 3]);
    const maxD = Math.max(dr, dg, db, da);
    totalAbsDiff += dr + dg + db + da;
    if (maxD <= channelTol) {
      matchedPixels++;
      if (diffImg) {
        diffImg.data[i] = 0;
        diffImg.data[i + 1] = 0;
        diffImg.data[i + 2] = 0;
        diffImg.data[i + 3] = 255;
      }
    } else if (diffImg) {
      diffImg.data[i] = 255;
      diffImg.data[i + 1] = Math.max(0, 255 - maxD * 2);
      diffImg.data[i + 2] = 0;
      diffImg.data[i + 3] = 255;
    }
  }
  if (diffImg) {
    diffCtx.putImageData(diffImg, 0, 0);
  }

  const passRate = matchedPixels / totalPixels;
  const passPercent = +(passRate * 100).toFixed(2);
  const meanAbsChannelDiff = +(totalAbsDiff / (totalPixels * 4)).toFixed(3);
  return {
    passRate,
    passPercent,
    matchedPixels,
    diffPixels: totalPixels - matchedPixels,
    meanAbsChannelDiff,
  };
}

const nextTick = (): Promise<void> => new Promise((r) => setTimeout(r, 0));

async function runComparisonAndBench(): Promise<void> {
  const reportEl = document.getElementById("report");
  const c2d = document.getElementById("canvas2d") as HTMLCanvasElement;
  const cgl = document.getElementById("canvasgl") as HTMLCanvasElement;
  const cdiff = document.getElementById("canvasdiff") as HTMLCanvasElement;

  const W = 800;
  const H = 520;
  const testImg = createTestImage(16, 16);
  const glErrors: number[] = [];
  const collectGlErrors = (gl: WebGL2RenderingContext | null): void => {
    if (!gl || typeof gl.getError !== "function") return;
    for (let guard = 0; guard < 32; guard++) {
      const err = gl.getError();
      if (!err) break;
      glErrors.push(err);
    }
  };

  const r2d = new BoardRenderer(c2d as unknown as CanvasLike, { view: "main", getDpr: () => 1 });
  const rgl = new GlBoardRenderer(cgl as unknown as GlCanvasLike, { view: "main", getDpr: () => 1 });
  const glCtx = cgl.getContext("webgl2")!;
  collectGlErrors(glCtx);

  r2d.putImage(testImg.meta, testImg.data);
  rgl.putImage(testImg.meta, testImg.data);
  collectGlErrors(glCtx);

  const compareOps = buildComparisonOps(W, H);
  r2d.renderFrame({ t: "board", view: "main", frame: 1, w: W, h: H, keep: 0, ops: compareOps });
  rgl.renderFrame({ t: "board", view: "main", frame: 1, w: W, h: H, keep: 0, ops: compareOps });
  collectGlErrors(glCtx);

  const ctx2d = c2d.getContext("2d")!;
  cdiff.width = W;
  cdiff.height = H;
  const diffCtx = cdiff.getContext("2d")!;
  const totalPixels = W * H;
  // Tolerance accounts for 8x MSAA vs Canvas2D analytical coverage on subpixel edges.
  const channelTol = 24;

  const fullCmp = compareCanvases(ctx2d, cgl, diffCtx, W, H, channelTol, true);
  const { passRate, passPercent, matchedPixels, meanAbsChannelDiff } = fullCmp;

  // 6-frame incremental comparison (keep=0, cachedCount>keep, cachedCount===keep, cachedCount<keep, active clip cut)
  const incFrames = buildIncrementalFrames(compareOps, 20261101);
  const incremental: IncrementalFrameResult[] = [];
  for (let i = 0; i < incFrames.length; i++) {
    const fSpec = incFrames[i];
    const frameNo = 2 + i;
    r2d.renderFrame({
      t: "board",
      view: "main",
      frame: frameNo,
      w: W,
      h: H,
      keep: fSpec.keep,
      ops: fSpec.ops,
    });
    rgl.renderFrame({
      t: "board",
      view: "main",
      frame: frameNo,
      w: W,
      h: H,
      keep: fSpec.keep,
      ops: fSpec.ops,
    });
    collectGlErrors(glCtx);
    const cmp = compareCanvases(ctx2d, cgl, diffCtx, W, H, channelTol, i === incFrames.length - 1);
    incremental.push({
      frame: i + 1,
      keep: fSpec.keep,
      opsCount: fSpec.ops.length,
      passRate: cmp.passRate,
      passPercent: cmp.passPercent,
      diffPixels: cmp.diffPixels,
    });
  }

  await nextTick();

  // Benchmark: 3000 strokes x 40 cubic Bezier segments full redraw (keep = 0)
  const benchW = 1280;
  const benchH = 720;
  const benchOps = build3000StrokesOps(benchW, benchH, 3000, 40);

  const bench2dTimes: number[] = [];
  for (let f = 0; f < 5; f++) {
    const s = 1 + f * 0.002;
    benchOps[0] = ["T", s, 0, 0, s, f * 2, f];
    const t0 = performance.now();
    r2d.renderFrame({
      t: "board",
      view: "main",
      frame: 10 + f,
      w: benchW,
      h: benchH,
      keep: 0,
      ops: benchOps,
    });
    // Force Canvas 2D GPU synchronization before recording elapsed time.
    ctx2d.getImageData(0, 0, 1, 1);
    bench2dTimes.push(performance.now() - t0);
    await nextTick();
  }

  const syncPx = new Uint8Array(4);

  benchOps[0] = ["T", 1, 0, 0, 1, 0, 0];
  const coldStart = performance.now();
  rgl.renderFrame({
    t: "board",
    view: "main",
    frame: 100,
    w: benchW,
    h: benchH,
    keep: 0,
    ops: benchOps,
  });
  glCtx.readPixels(0, 0, 1, 1, glCtx.RGBA, glCtx.UNSIGNED_BYTE, syncPx);
  const webgl2ColdMs = +(performance.now() - coldStart).toFixed(2);
  const drawCalls = rgl.getLastDrawCalls();
  collectGlErrors(glCtx);

  const benchGlTimes: number[] = [];
  for (let f = 1; f <= 9; f++) {
    const s = 1 + f * 0.002;
    benchOps[0] = ["T", s, 0, 0, s, f * 2, f];
    const t0 = performance.now();
    rgl.renderFrame({
      t: "board",
      view: "main",
      frame: 100 + f,
      w: benchW,
      h: benchH,
      keep: 0,
      ops: benchOps,
    });
    glCtx.readPixels(0, 0, 1, 1, glCtx.RGBA, glCtx.UNSIGNED_BYTE, syncPx);
    benchGlTimes.push(performance.now() - t0);
    collectGlErrors(glCtx);
    await nextTick();
  }

  const canvas2dMs = +median(bench2dTimes).toFixed(2);
  const webgl2CachedMs = +median(benchGlTimes).toFixed(2);
  const speedup = +(canvas2dMs / Math.max(0.01, webgl2CachedMs)).toFixed(2);

  // Restore comparison scene on the visible canvases after benchmark finishes.
  r2d.renderFrame({ t: "board", view: "main", frame: 200, w: W, h: H, keep: 0, ops: compareOps });
  rgl.renderFrame({ t: "board", view: "main", frame: 200, w: W, h: H, keep: 0, ops: compareOps });
  collectGlErrors(glCtx);
  compareCanvases(ctx2d, cgl, diffCtx, W, H, channelTol, true);

  const result: WebglCompareResult = {
    done: true,
    passRate,
    passPercent,
    totalPixels,
    matchedPixels,
    diffPixels: totalPixels - matchedPixels,
    meanAbsChannelDiff,
    canvas2dMs,
    webgl2Ms: webgl2CachedMs,
    webgl2ColdMs,
    drawCalls,
    speedup,
    incremental,
    glErrors,
    benchmark: {
      strokes: 3000,
      segmentsPerStroke: 40,
      canvas2dMs,
      webgl2ColdMs,
      webgl2CachedMs,
      drawCalls,
      speedup,
    },
  };

  window.__webglCompare = result;
  if (reportEl) {
    reportEl.textContent = JSON.stringify(result, null, 2);
  }
}

runComparisonAndBench().catch((err) => {
  const reportEl = document.getElementById("report");
  const msg = err instanceof Error ? err.message : String(err);
  window.__webglCompare = {
    done: true,
    error: msg,
    passRate: 0,
    passPercent: 0,
    totalPixels: 0,
    matchedPixels: 0,
    diffPixels: 0,
    meanAbsChannelDiff: 0,
    canvas2dMs: 0,
    webgl2Ms: 0,
    webgl2ColdMs: 0,
    drawCalls: 0,
    speedup: 0,
    incremental: [],
    glErrors: [],
    benchmark: {
      strokes: 3000,
      segmentsPerStroke: 40,
      canvas2dMs: 0,
      webgl2ColdMs: 0,
      webgl2CachedMs: 0,
      drawCalls: 0,
      speedup: 0,
    },
  };
  if (reportEl) {
    reportEl.textContent = `ERROR: ${msg}`;
  }
});
