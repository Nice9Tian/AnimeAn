import { GlBoardRenderer, type GlCanvasLike } from "../src/board/glrenderer.ts";

const W = 400;
const H = 300;
const GW = 41;
const GH = 31;
const RECT: [number, number, number, number] = [0, 0, W, H];

function buildTestField(): Float32Array {
  const data = new Float32Array(GW * GH * 4);
  for (let j = 0; j < GH; j++) {
    const y = (j / (GH - 1)) * RECT[3] + RECT[1];
    const dx = 20 * Math.sin(y / 50);
    const dy = 0;
    const tag = j <= 15 ? 1.0 : 0.0;
    for (let i = 0; i < GW; i++) {
      const idx = (j * GW + i) * 4;
      data[idx] = dx;
      data[idx + 1] = dy;
      data[idx + 2] = tag;
      data[idx + 3] = 0.0;
    }
  }
  return data;
}

function sampleFieldCpu(
  field: Float32Array,
  x: number,
  y: number,
): { dx: number; dy: number; tag: number; insideRect: boolean } {
  const [x0, y0, rw, rh] = RECT;
  const insideRect = x >= x0 && x <= x0 + rw && y >= y0 && y <= y0 + rh;
  const u = ((x - x0) / rw) * (GW - 1);
  const v = ((y - y0) / rh) * (GH - 1);
  const uc = Math.max(0, Math.min(GW - 1, u));
  const vc = Math.max(0, Math.min(GH - 1, v));
  const i0 = Math.max(0, Math.min(GW - 2, Math.floor(uc)));
  const j0 = Math.max(0, Math.min(GH - 2, Math.floor(vc)));
  const i1 = Math.min(i0 + 1, GW - 1);
  const j1 = Math.min(j0 + 1, GH - 1);
  const fu = Math.max(0, Math.min(1, uc - i0));
  const fv = Math.max(0, Math.min(1, vc - j0));

  const read = (ii: number, jj: number): [number, number] => {
    const idx = (jj * GW + ii) * 4;
    return [field[idx], field[idx + 1]];
  };
  const [d00x, d00y] = read(i0, j0);
  const [d10x, d10y] = read(i1, j0);
  const [d01x, d01y] = read(i0, j1);
  const [d11x, d11y] = read(i1, j1);

  const topX = d00x + (d10x - d00x) * fu;
  const botX = d01x + (d11x - d01x) * fu;
  const dx = topX + (botX - topX) * fv;

  const topY = d00y + (d10y - d00y) * fu;
  const botY = d01y + (d11y - d01y) * fu;
  const dy = topY + (botY - topY) * fv;

  const ni = Math.max(0, Math.min(GW - 1, Math.round(u)));
  const nj = Math.max(0, Math.min(GH - 1, Math.round(v)));
  const tag = field[(nj * GW + ni) * 4 + 2];

  return { dx, dy, tag, insideRect };
}

function readCanvasPixelsTopDown(gl: WebGL2RenderingContext, w: number, h: number): Uint8Array {
  gl.bindFramebuffer(gl.FRAMEBUFFER, null);
  const raw = new Uint8Array(w * h * 4);
  gl.readPixels(0, 0, w, h, gl.RGBA, gl.UNSIGNED_BYTE, raw);
  const topDown = new Uint8Array(w * h * 4);
  const rowBytes = w * 4;
  for (let y = 0; y < h; y++) {
    const srcOffset = (h - 1 - y) * rowBytes;
    const dstOffset = y * rowBytes;
    topDown.set(raw.subarray(srcOffset, srcOffset + rowBytes), dstOffset);
  }
  return topDown;
}

function runVerification(): void {
  const summaryEl = document.getElementById("summary");
  const detailsEl = document.getElementById("details");
  const canvas = document.getElementById("gl-canvas") as HTMLCanvasElement | null;
  if (!canvas || !summaryEl || !detailsEl) return;

  const logs: string[] = [];
  const failures: string[] = [];

  try {
    const renderer = new GlBoardRenderer(canvas as unknown as GlCanvasLike, { getDpr: () => 1 });
    const gl = canvas.getContext("webgl2");
    if (!gl) {
      throw new Error("WebGL2 context unavailable");
    }

    const fieldData = buildTestField();
    renderer.putField({ t: "field", key: "verify_field", w: GW, h: GH, version: 1, bytes: fieldData.byteLength }, fieldData);

    // 1. First F..f scope: long straight vertical line at x=90 (y: 20..280, width=10, red)
    //    - Pass 0 (tag 1..1, top half): opacity 1.0, no tint [1, 1, 1.0, 0, 0, 0, 0]
    //    - Pass 1 (tag 0..0, bottom half): opacity 0.5, tinted toward (0, 240, 120) with a=128 [0, 0, 0.5, 0, 240, 120, 128]
    // 2. Second F..f scope: filled rectangle at x=210..310 (y: 30..270, blue) crossing the tag boundary,
    //    with a single pass [1, 1, 1.0, 0, 0, 0, 0] so ONLY the tag=1 top half appears and the tag=0 half is discarded.
    renderer.renderFrame({
      t: "board",
      view: "main",
      frame: 1,
      w: W,
      h: H,
      keep: 0,
      ops: [
        ["T", 1, 0, 0, 1, 0, 0],
        [
          "F",
          "verify_field",
          1,
          ...RECT,
          4,
          [
            [1, 1, 1.0, 0, 0, 0, 0],
            [0, 0, 0.5, 0, 240, 120, 128],
          ],
        ],
        ["B", 0, 0, 0, 0],
        ["P", 255, 40, 40, 255, 10, 0, 0, 2, 0, null],
        ["S", [0, 90, 20, 1, 90, 280], 0],
        ["f"],
        [
          "F",
          "verify_field",
          1,
          ...RECT,
          4,
          [
            [1, 1, 1.0, 0, 0, 0, 0],
          ],
        ],
        ["P", 0, 0, 0, 0, 1, 0, 0, 2, 0, null],
        ["B", 30, 140, 255, 255],
        ["S", [0, 210, 30, 1, 310, 30, 1, 310, 270, 1, 210, 270, 3], 0],
        ["f"],
      ],
    });

    const pixels = readCanvasPixelsTopDown(gl, W, H);
    const getPixel = (x: number, y: number): [number, number, number, number] => {
      const idx = (y * W + x) * 4;
      return [pixels[idx], pixels[idx + 1], pixels[idx + 2], pixels[idx + 3]];
    };

    const sampleRows = [40, 60, 80, 100, 120, 180, 200, 220, 240, 260];
    for (const y of sampleRows) {
      const cpu = sampleFieldCpu(fieldData, 90, y);
      const expectedLineX = 90 + cpu.dx;
      const expectedRectLeft = 210 + sampleFieldCpu(fieldData, 210, y).dx;
      const expectedRectRight = 310 + sampleFieldCpu(fieldData, 310, y).dx;

      // Expected premultiplied RGBA at center of the line on row y:
      // Base pen is (255, 40, 40), alpha=1.0.
      // For tag=1: opacity=1.0, tintA=0 -> (255, 40, 40, 255).
      // For tag=0: opacity=0.5, tint=(0, 240, 120, 128) -> mix((255,40,40)*0.5, (0,240,120)*0.5, 128/255), alpha=128.
      const lineAlpha = cpu.tag === 1 ? 1.0 : 0.5;
      const tintMix = cpu.tag === 1 ? 0.0 : 128 / 255;
      const expLineR = Math.round(((1 - tintMix) * 255 + tintMix * 0) * lineAlpha);
      const expLineG = Math.round(((1 - tintMix) * 40 + tintMix * 240) * lineAlpha);
      const expLineB = Math.round(((1 - tintMix) * 40 + tintMix * 120) * lineAlpha);
      const expLineA = Math.round(255 * lineAlpha);

      // Measure line center on row y (x in 50..140)
      let lineSumX = 0;
      let lineWeight = 0;
      for (let x = 50; x <= 140; x++) {
        const [, , , a] = getPixel(x, y);
        if (a > 20) {
          lineSumX += x * a;
          lineWeight += a;
        }
      }
      const measuredLineX = lineWeight > 0 ? lineSumX / lineWeight : NaN;
      const lineErr = Math.abs(measuredLineX - expectedLineX);
      const centerPx = getPixel(Math.round(expectedLineX), y);
      const colorErr = Math.max(
        Math.abs(centerPx[0] - expLineR),
        Math.abs(centerPx[1] - expLineG),
        Math.abs(centerPx[2] - expLineB),
        Math.abs(centerPx[3] - expLineA),
      );

      // Measure blue filled rectangle span on row y (x in 170..350)
      let rectMinX = Infinity;
      let rectMaxX = -Infinity;
      let rectNonZeroCount = 0;
      for (let x = 170; x <= 350; x++) {
        const [, , b, a] = getPixel(x, y);
        if (b > 20 || a > 20) {
          rectNonZeroCount++;
          if (x < rectMinX) rectMinX = x;
          if (x > rectMaxX) rectMaxX = x;
        }
      }

      if (cpu.tag === 1) {
        const leftErr = Math.abs(rectMinX - expectedRectLeft);
        const rightErr = Math.abs(rectMaxX - expectedRectRight);
        logs.push(
          `y=${y} (tag=1): lineX=${measuredLineX.toFixed(2)} (exp ${expectedLineX.toFixed(2)}, err=${lineErr.toFixed(2)}), ` +
            `rgba=[${centerPx.join(",")}] (exp [${expLineR},${expLineG},${expLineB},${expLineA}]) | ` +
            `rect=[${rectMinX}, ${rectMaxX}] (exp [${expectedRectLeft.toFixed(1)}, ${expectedRectRight.toFixed(1)}])`,
        );
        if (!Number.isFinite(leftErr) || leftErr > 2.5 || !Number.isFinite(rightErr) || rightErr > 2.5) {
          failures.push(`Row y=${y} (tag=1): rect edge error left=${leftErr.toFixed(2)}, right=${rightErr.toFixed(2)}`);
        }
      } else {
        logs.push(
          `y=${y} (tag=0): lineX=${measuredLineX.toFixed(2)} (exp ${expectedLineX.toFixed(2)}, err=${lineErr.toFixed(2)}), ` +
            `tinted rgba=[${centerPx.join(",")}] (exp [${expLineR},${expLineG},${expLineB},${expLineA}]) | ` +
            `rectPixels=${rectNonZeroCount} (exp 0, discarded by tag)`,
        );
        if (rectNonZeroCount !== 0) {
          failures.push(`Row y=${y} (tag=0): expected filled rect to be discarded, but ${rectNonZeroCount} pixels appeared`);
        }
      }

      if (!Number.isFinite(lineErr) || lineErr > 1.8) {
        failures.push(`Row y=${y}: line X error ${lineErr.toFixed(2)} > 1.8`);
      }
      if (colorErr > 8) {
        failures.push(
          `Row y=${y}: line RGBA [${centerPx.join(",")}] != expected [${expLineR},${expLineG},${expLineB},${expLineA}]`,
        );
      }
    }
  } catch (err) {
    failures.push(`Exception: ${err instanceof Error ? err.message : String(err)}`);
  }

  const passed = failures.length === 0;
  const headline = passed
    ? `PASS: All ${logs.length} field displacement, subdivision, cover-quad, and tag-opacity checks succeeded.`
    : `FAIL: ${failures.join(" | ")}`;

  document.title = headline;
  summaryEl.textContent = headline;
  summaryEl.className = passed ? "pass" : "fail";
  detailsEl.textContent = [...(failures.length ? ["Failures:", ...failures, ""] : []), "Measurements:", ...logs].join("\n");
}

runVerification();
