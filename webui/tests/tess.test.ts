import assert from "node:assert/strict";
import { describe, it } from "node:test";
import {
  applyDash,
  computeScaleFactor,
  computeTrianglesArea,
  evalCubicBezier,
  evalFillFanAtPoint,
  flattenCubicBezier,
  getLastFlattenedSubpaths,
  parsePath,
  tessellateFill,
  tessellateStroke,
  tessellateStrokeView,
} from "../src/board/tess.ts";

function pointToSegmentDist(
  px: number,
  py: number,
  x0: number,
  y0: number,
  x1: number,
  y1: number,
): number {
  const dx = x1 - x0;
  const dy = y1 - y0;
  const lenSq = dx * dx + dy * dy;
  if (lenSq <= 1e-12) return Math.hypot(px - x0, py - y0);
  const t = Math.max(0, Math.min(1, ((px - x0) * dx + (py - y0) * dy) / lenSq));
  return Math.hypot(px - (x0 + t * dx), py - (y0 + t * dy));
}

function minDistanceToPolyline(px: number, py: number, pts: ReadonlyArray<number>): number {
  let best = Infinity;
  for (let i = 0; i < pts.length - 2; i += 2) {
    const d = pointToSegmentDist(px, py, pts[i], pts[i + 1], pts[i + 2], pts[i + 3]);
    if (d < best) best = d;
  }
  return best;
}

describe("tess", () => {
  it("flattens cubic beziers within screen-pixel tolerance across scales", () => {
    const curves: Array<[number, number, number, number, number, number, number, number]> = [
      [0, 0, 40, 120, 80, -120, 120, 0],
      [10, 20, 150, 20, -50, 180, 90, 180],
      [0, 0, 0, 200, 200, 200, 200, 0],
    ];

    for (const [x0, y0, c1x, c1y, c2x, c2y, x3, y3] of curves) {
      for (const scale of [0.5, 1, 2, 4]) {
        const tolerance = 0.1;
        const maxLocalError = tolerance / scale;
        const pts = flattenCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x3, y3, scale, tolerance);
        assert.ok(pts.length >= 4);

        for (let s = 0; s <= 400; s++) {
          const t = s / 400;
          const [bx, by] = evalCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x3, y3, t);
          const dist = minDistanceToPolyline(bx, by, pts);
          assert.ok(
            dist <= maxLocalError + 1e-6,
            `curve error ${dist} exceeded tolerance/scale ${maxLocalError} at t=${t}`,
          );
        }
      }
    }
  });

  it("keeps stroke pre-expansion polyline within screen-pixel tolerance for width 6, width 3, and cosmetic pens", () => {
    const tolerance = 0.1;
    const testCurves: Array<[number, number, number, number, number, number, number, number]> = [
      [30, 220, 120, 180, 120, 330, 40, 280],
      [40, 280, 0, 230, 130, 200, 115, 315],
      [5, 45, 15, 25, 35, 65, 50, 40],
      [0, 0, 40, 120, 80, -120, 120, 0],
    ];

    for (const [x0, y0, c1x, c1y, c2x, c2y, x3, y3] of testCurves) {
      const path = [0, x0, y0, 2, c1x, c1y, c2x, c2y, x3, y3];

      // 1. width = 6, non-cosmetic (e.g. y=180-300 semi-transparent curves)
      for (const scale of [1, 2]) {
        tessellateStrokeView(path, { width: 6, cap: "round", join: "round", miter: 3, cosmetic: false }, scale, tolerance);
        const subpaths = getLastFlattenedSubpaths();
        assert.equal(subpaths.length, 1);
        const pts = subpaths[0].points;
        const maxLocalErr = tolerance / scale;
        for (let s = 0; s <= 400; s++) {
          const t = s / 400;
          const [bx, by] = evalCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x3, y3, t);
          const dist = minDistanceToPolyline(bx, by, pts);
          assert.ok(
            dist <= maxLocalErr + 1e-6,
            `width=6 scale=${scale} polyline error ${dist} > ${maxLocalErr} at t=${t}`,
          );
        }
      }

      // 2. width = 3, non-cosmetic
      for (const scale of [1, 2]) {
        tessellateStrokeView(path, { width: 3, cap: "round", join: "round", miter: 2, cosmetic: false }, scale, tolerance);
        const subpaths = getLastFlattenedSubpaths();
        assert.equal(subpaths.length, 1);
        const pts = subpaths[0].points;
        const maxLocalErr = tolerance / scale;
        for (let s = 0; s <= 400; s++) {
          const t = s / 400;
          const [bx, by] = evalCubicBezier(x0, y0, c1x, c1y, c2x, c2y, x3, y3, t);
          const dist = minDistanceToPolyline(bx, by, pts);
          assert.ok(
            dist <= maxLocalErr + 1e-6,
            `width=3 scale=${scale} polyline error ${dist} > ${maxLocalErr} at t=${t}`,
          );
        }
      }

      // 3. cosmetic = true under scaling transform [2.5, 0, 0, 2.5, 0, 0] (e.g. y=420 cosmetic curve)
      for (const dpr of [1, 2]) {
        const linearTransform: [number, number, number, number, number, number] = [2.5, 0, 0, 2.5, 0, 0];
        const scale = computeScaleFactor(linearTransform, dpr, true);
        tessellateStrokeView(
          path,
          { width: 3, cap: "square", join: "miter", miter: 2, cosmetic: true },
          scale,
          tolerance,
          linearTransform,
        );
        const subpaths = getLastFlattenedSubpaths();
        assert.equal(subpaths.length, 1);
        const pts = subpaths[0].points;
        for (let s = 0; s <= 400; s++) {
          const t = s / 400;
          const [bx, by] = evalCubicBezier(
            x0 * 2.5,
            y0 * 2.5,
            c1x * 2.5,
            c1y * 2.5,
            c2x * 2.5,
            c2y * 2.5,
            x3 * 2.5,
            y3 * 2.5,
            t,
          );
          const screenDist = minDistanceToPolyline(bx, by, pts) * dpr;
          assert.ok(
            screenDist <= tolerance + 1e-6,
            `cosmetic dpr=${dpr} screen polyline error ${screenDist} > ${tolerance} at t=${t}`,
          );
        }
      }
    }
  });

  it("matches analytical triangle areas for butt, square, and round line caps", () => {
    const linePath = [0, 0, 0, 1, 100, 0];
    const width = 4;
    const r = width / 2;

    const buttTris = tessellateStroke(linePath, { width, cap: "butt", join: "miter", miter: 2 });
    assert.ok(Math.abs(computeTrianglesArea(buttTris) - 400) < 1e-4);

    const squareTris = tessellateStroke(linePath, { width, cap: "square", join: "miter", miter: 2 });
    assert.ok(Math.abs(computeTrianglesArea(squareTris) - 416) < 1e-4);

    const roundTris = tessellateStroke(
      linePath,
      { width, cap: "round", join: "miter", miter: 2 },
      1,
      0.002,
    );
    const expectedRound = 400 + Math.PI * r * r;
    assert.ok(
      Math.abs(computeTrianglesArea(roundTris) - expectedRound) < 0.05,
      `expected ~${expectedRound}, got ${computeTrianglesArea(roundTris)}`,
    );

    const bigR = 10;
    const bigRoundTris = tessellateStroke(
      [0, 0, 0, 1, 50, 0],
      {
        width: bigR * 2,
        cap: "round",
        join: "miter",
        miter: 2,
      },
      1,
      0.005,
    );
    const expectedBigRound = 50 * (bigR * 2) + Math.PI * bigR * bigR;
    assert.ok(Math.abs(computeTrianglesArea(bigRoundTris) - expectedBigRound) < 0.5);
  });

  it("matches analytical triangle areas for bevel, miter (with miterLimit), and round joins", () => {
    const cornerPath = [0, 0, 0, 1, 100, 0, 1, 100, 100];
    const width = 8;
    const r = width / 2;
    const baseQuadsArea = 2 * 100 * width;

    const bevelTris = tessellateStroke(cornerPath, {
      width,
      cap: "butt",
      join: "bevel",
      miter: 2,
    });
    const expectedBevel = baseQuadsArea + 0.5 * r * r;
    assert.ok(Math.abs(computeTrianglesArea(bevelTris) - expectedBevel) < 1e-3);

    const miterTris = tessellateStroke(cornerPath, {
      width,
      cap: "butt",
      join: "miter",
      miter: 2,
    });
    const expectedMiter = baseQuadsArea + r * r;
    assert.ok(Math.abs(computeTrianglesArea(miterTris) - expectedMiter) < 1e-3);

    // For 90 deg turn, miterLength/lineWidth = sqrt(2) ~= 1.4142; miterLimit=1.2 must fall back to bevel.
    const clampedMiterTris = tessellateStroke(cornerPath, {
      width,
      cap: "butt",
      join: "miter",
      miter: 1.2,
    });
    assert.ok(Math.abs(computeTrianglesArea(clampedMiterTris) - expectedBevel) < 1e-3);

    const roundJoinTris = tessellateStroke(
      cornerPath,
      {
        width,
        cap: "butt",
        join: "round",
        miter: 2,
      },
      1,
      0.005,
    );
    const expectedRoundJoin = baseQuadsArea + 0.25 * Math.PI * r * r;
    assert.ok(Math.abs(computeTrianglesArea(roundJoinTris) - expectedRoundJoin) < 0.1);

    const closedBox = [0, 0, 0, 1, 100, 0, 1, 100, 100, 1, 0, 100, 3];
    const closedMiter = tessellateStroke(closedBox, {
      width,
      cap: "round",
      join: "miter",
      miter: 2,
    });
    const expectedClosedMiter = 4 * 100 * width + 4 * (r * r);
    assert.ok(Math.abs(computeTrianglesArea(closedMiter) - expectedClosedMiter) < 1e-2);
  });

  it("splits dashed paths by lineWidth-scaled dash pattern and matches segment counts", () => {
    const linePath = [0, 0, 0, 1, 100, 0];
    const width = 2;
    // dash [3, 2] * width(2) => [6, 4], period 10 => 10 dashes over length 100.
    const subpaths = parsePath(linePath);
    const dashed = applyDash(subpaths, [3, 2], width);
    assert.equal(dashed.length, 10);

    const dashedTris = tessellateStroke(linePath, {
      width,
      cap: "butt",
      join: "miter",
      miter: 2,
      dash: [3, 2],
    });
    assert.equal(dashedTris.length / 6, 20);
    assert.ok(Math.abs(computeTrianglesArea(dashedTris) - 10 * 6 * width) < 1e-3);

    // Odd-length dash [5] * width(2) => [10, 10], period 20 => 5 dashes over length 100.
    const oddDashed = applyDash(subpaths, [5], width);
    assert.equal(oddDashed.length, 5);
  });

  it("produces triangle fans supporting both evenodd and nonzero fill rules", () => {
    // Two concentric clockwise squares: outer [0..100], inner [25..75] wound in the SAME direction.
    const sameWindingConcentric = [
      0, 0, 0, 1, 100, 0, 1, 100, 100, 1, 0, 100, 3,
      0, 25, 25, 1, 75, 25, 1, 75, 75, 1, 25, 75, 3,
    ];
    const fan = tessellateFill(sameWindingConcentric, "evenodd");
    assert.equal(fan.length / 6, 4);

    assert.equal(evalFillFanAtPoint(fan, 10, 50, "evenodd"), true);
    assert.equal(evalFillFanAtPoint(fan, 10, 50, "nonzero"), true);
    assert.equal(evalFillFanAtPoint(fan, 50, 50, "evenodd"), false);
    assert.equal(evalFillFanAtPoint(fan, 50, 50, "nonzero"), true);
    assert.equal(evalFillFanAtPoint(fan, 150, 50, "evenodd"), false);
    assert.equal(evalFillFanAtPoint(fan, 150, 50, "nonzero"), false);

    // Opposite winding on inner square: both evenodd and nonzero treat center as a hole.
    const oppositeWindingConcentric = [
      0, 0, 0, 1, 100, 0, 1, 100, 100, 1, 0, 100, 3,
      0, 25, 25, 1, 25, 75, 1, 75, 75, 1, 75, 25, 3,
    ];
    const fanOpp = tessellateFill(oppositeWindingConcentric, "nonzero");
    assert.equal(evalFillFanAtPoint(fanOpp, 50, 50, "evenodd"), false);
    assert.equal(evalFillFanAtPoint(fanOpp, 50, 50, "nonzero"), false);
    assert.equal(evalFillFanAtPoint(fanOpp, 10, 50, "nonzero"), true);
  });

  it("tessellates 3000 strokes x 40 cubic bezier segments well under 400ms, even on a busy machine", () => {
    const w = 1280;
    const h = 720;
    const strokeCount = 3000;
    const segments = 40;
    const paths: number[][] = [];

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
      paths.push(path);
    }

    const pen = { width: 3, cap: "round" as const, join: "round" as const, miter: 2 };
    const t0 = performance.now();
    let totalFloats = 0;
    for (let i = 0; i < paths.length; i++) {
      const view = tessellateStrokeView(paths[i], pen, 1, 0.1);
      totalFloats += view.length;
    }
    const elapsedMs = performance.now() - t0;
    console.log(
      `[tess benchmark] 3000x40 cubic bezier strokes: ${elapsedMs.toFixed(2)} ms, totalVertices=${totalFloats / 2}, totalTriangles=${totalFloats / 6}`,
    );
    assert.ok(totalFloats > 0);
    assert.ok(elapsedMs < 400, `Expected 3000x40 tessellation < 400ms, got ${elapsedMs.toFixed(2)}ms`);
  });
});
