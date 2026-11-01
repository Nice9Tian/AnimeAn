import { mapLineCap, mapLineJoin } from "./renderer.ts";

export interface Subpath {
  points: number[];
  closed: boolean;
}

export type LineCapInput = CanvasLineCap | number;
export type LineJoinInput = LineJoin | number;
type LineJoin = CanvasLineJoin;
export type FillRuleInput = CanvasFillRule | number;

export interface StrokePenOptions {
  width: number;
  cap?: LineCapInput;
  join?: LineJoinInput;
  miter?: number;
  cosmetic?: boolean;
  dash?: ReadonlyArray<number> | null;
}

export function evalCubicBezier(
  x0: number,
  y0: number,
  c1x: number,
  c1y: number,
  c2x: number,
  c2y: number,
  x3: number,
  y3: number,
  t: number,
): [number, number] {
  const u = 1 - t;
  const uu = u * u;
  const uuu = uu * u;
  const tt = t * t;
  const ttt = tt * t;
  const x = uuu * x0 + 3 * uu * t * c1x + 3 * u * tt * c2x + ttt * x3;
  const y = uuu * y0 + 3 * uu * t * c1y + 3 * u * tt * c2y + ttt * y3;
  return [x, y];
}

export function flattenCubicBezier(
  x0: number,
  y0: number,
  c1x: number,
  c1y: number,
  c2x: number,
  c2y: number,
  x3: number,
  y3: number,
  scale = 1,
  tolerance = 0.1,
  out?: number[],
): number[] {
  const result = out ?? [x0, y0];
  if (result.length === 0) {
    result.push(x0, y0);
  }
  const effScale = Math.max(Number(scale) || 1, 1e-6);
  const effTol = Math.max(Number(tolerance) || 0.1, 1e-5) / effScale;
  const limitSq = 16 * effTol * effTol;

  function subdivide(
    ax: number,
    ay: number,
    bx: number,
    by: number,
    cx: number,
    cy: number,
    dx: number,
    dy: number,
    depth: number,
  ): void {
    const ux = 3 * bx - 2 * ax - dx;
    const uy = 3 * by - 2 * ay - dy;
    if (ux * ux + uy * uy <= limitSq) {
      const vx = 3 * cx - ax - 2 * dx;
      const vy = 3 * cy - ay - 2 * dy;
      if (vx * vx + vy * vy <= limitSq) {
        result.push(dx, dy);
        return;
      }
    }
    if (depth >= 16) {
      result.push(dx, dy);
      return;
    }

    const abx = (ax + bx) * 0.5;
    const aby = (ay + by) * 0.5;
    const bcx = (bx + cx) * 0.5;
    const bcy = (by + cy) * 0.5;
    const cdx = (cx + dx) * 0.5;
    const cdy = (cy + dy) * 0.5;
    const abcx = (abx + bcx) * 0.5;
    const abcy = (aby + bcy) * 0.5;
    const bcdx = (bcx + cdx) * 0.5;
    const bcdy = (bcy + cdy) * 0.5;
    const midx = (abcx + bcdx) * 0.5;
    const midy = (abcy + bcdy) * 0.5;

    subdivide(ax, ay, abx, aby, abcx, abcy, midx, midy, depth + 1);
    subdivide(midx, midy, bcdx, bcdy, cdx, cdy, dx, dy, depth + 1);
  }

  subdivide(x0, y0, c1x, c1y, c2x, c2y, x3, y3, 0);
  return result;
}

export function parsePath(
  path: ReadonlyArray<number>,
  scale = 1,
  tolerance = 0.1,
  transform?: ReadonlyArray<number>,
  maxEdge = 0,
): Subpath[] {
  const subpaths: Subpath[] = [];
  let current: Subpath | null = null;
  let startX = 0;
  let startY = 0;
  let curX = 0;
  let curY = 0;

  const hasT = Boolean(transform && transform.length >= 6);
  const m11 = hasT ? Number(transform![0]) : 1;
  const m12 = hasT ? Number(transform![1]) : 0;
  const m21 = hasT ? Number(transform![2]) : 0;
  const m22 = hasT ? Number(transform![3]) : 1;
  const dx = hasT ? Number(transform![4]) : 0;
  const dy = hasT ? Number(transform![5]) : 0;

  const mapX = (x: number, y: number): number => (hasT ? m11 * x + m21 * y + dx : x);
  const mapY = (x: number, y: number): number => (hasT ? m12 * x + m22 * y + dy : y);

  let i = 0;
  const len = path.length;
  while (i < len) {
    const cmd = path[i++];
    if (cmd === 0) {
      const rawX = Number(path[i++] ?? 0);
      const rawY = Number(path[i++] ?? 0);
      const x = mapX(rawX, rawY);
      const y = mapY(rawX, rawY);
      current = { points: [x, y], closed: false };
      subpaths.push(current);
      startX = x;
      startY = y;
      curX = x;
      curY = y;
    } else if (cmd === 1) {
      const rawX = Number(path[i++] ?? 0);
      const rawY = Number(path[i++] ?? 0);
      const x = mapX(rawX, rawY);
      const y = mapY(rawX, rawY);
      if (!current) {
        current = { points: [x, y], closed: false };
        subpaths.push(current);
        startX = x;
        startY = y;
      } else {
        current.points.push(x, y);
      }
      curX = x;
      curY = y;
    } else if (cmd === 2) {
      const rc1x = Number(path[i++] ?? 0);
      const rc1y = Number(path[i++] ?? 0);
      const rc2x = Number(path[i++] ?? 0);
      const rc2y = Number(path[i++] ?? 0);
      const rex = Number(path[i++] ?? 0);
      const rey = Number(path[i++] ?? 0);
      const c1x = mapX(rc1x, rc1y);
      const c1y = mapY(rc1x, rc1y);
      const c2x = mapX(rc2x, rc2y);
      const c2y = mapY(rc2x, rc2y);
      const ex = mapX(rex, rey);
      const ey = mapY(rex, rey);
      if (!current) {
        current = { points: [curX, curY], closed: false };
        subpaths.push(current);
        startX = curX;
        startY = curY;
      }
      flattenCubicBezier(curX, curY, c1x, c1y, c2x, c2y, ex, ey, scale, tolerance, current.points);
      curX = ex;
      curY = ey;
    } else if (cmd === 3) {
      if (current) {
        current.closed = true;
        curX = startX;
        curY = startY;
        current = null;
      }
    } else {
      break;
    }
  }

  if (maxEdge > 0) {
    for (let s = 0; s < subpaths.length; s++) {
      subpaths[s].points = subdivideSubpathPoints(subpaths[s].points, subpaths[s].closed, maxEdge);
    }
  }

  return subpaths;
}

export function subdivideSubpathPoints(
  points: ReadonlyArray<number>,
  closed: boolean,
  maxEdge: number,
): number[] {
  if (maxEdge <= 0 || points.length < 4) return points.slice();
  const nPts = points.length >> 1;
  const out: number[] = [points[0], points[1]];
  for (let i = 0; i < nPts - 1; i++) {
    const x0 = points[i * 2];
    const y0 = points[i * 2 + 1];
    const x1 = points[(i + 1) * 2];
    const y1 = points[(i + 1) * 2 + 1];
    const dist = Math.hypot(x1 - x0, y1 - y0);
    if (dist > maxEdge) {
      const steps = Math.ceil(dist / maxEdge);
      for (let s = 1; s < steps; s++) {
        const t = s / steps;
        out.push(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
      }
    }
    out.push(x1, y1);
  }
  if (closed && nPts >= 2) {
    const x0 = points[(nPts - 1) * 2];
    const y0 = points[(nPts - 1) * 2 + 1];
    const x1 = points[0];
    const y1 = points[1];
    const dist = Math.hypot(x1 - x0, y1 - y0);
    if (dist > maxEdge) {
      const steps = Math.ceil(dist / maxEdge);
      for (let s = 1; s < steps; s++) {
        const t = s / steps;
        out.push(x0 + (x1 - x0) * t, y0 + (y1 - y0) * t);
      }
    }
  }
  return out;
}

export function applyDash(
  subpaths: ReadonlyArray<Subpath>,
  dash: ReadonlyArray<number>,
  lineWidth: number,
): Subpath[] {
  const effWidth = lineWidth > 0 ? lineWidth : 1;
  const scaled: number[] = [];
  for (let i = 0; i < dash.length; i++) {
    const v = Math.max(0, (Number(dash[i]) || 0) * effWidth);
    scaled.push(v);
  }
  if (scaled.length === 0) return subpaths.slice();
  // Canvas 2D repeats odd-length dash arrays so on/off pairs stay balanced.
  const pattern = scaled.length % 2 === 1 ? scaled.concat(scaled) : scaled;
  let totalDash = 0;
  for (let i = 0; i < pattern.length; i++) {
    totalDash += pattern[i];
  }
  if (totalDash <= 1e-6) return subpaths.slice();

  const out: Subpath[] = [];

  for (const sp of subpaths) {
    const pts = sp.points;
    if (pts.length < 4) continue;

    const coords = pts.slice();
    if (sp.closed) {
      const dx = coords[coords.length - 2] - coords[0];
      const dy = coords[coords.length - 1] - coords[1];
      if (dx * dx + dy * dy > 1e-12) {
        coords.push(coords[0], coords[1]);
      }
    }

    let patIdx = 0;
    let patRem = pattern[0];
    let draw = true;
    let activePoints: number[] | null = null;

    for (let i = 0; i < coords.length - 2; i += 2) {
      let x0 = coords[i];
      let y0 = coords[i + 1];
      const x1 = coords[i + 2];
      const y1 = coords[i + 3];
      const dx = x1 - x0;
      const dy = y1 - y0;
      let segRem = Math.hypot(dx, dy);
      if (segRem <= 1e-9) continue;
      const ux = dx / segRem;
      const uy = dy / segRem;

      while (segRem > 1e-9) {
        while (patRem <= 1e-9) {
          if (draw && activePoints && activePoints.length >= 4) {
            out.push({ points: activePoints, closed: false });
          }
          activePoints = null;
          patIdx = (patIdx + 1) % pattern.length;
          patRem = pattern[patIdx];
          draw = patIdx % 2 === 0;
        }

        const step = Math.min(segRem, patRem);
        const nx = step >= segRem ? x1 : x0 + ux * step;
        const ny = step >= segRem ? y1 : y0 + uy * step;

        if (draw) {
          if (!activePoints) {
            activePoints = [x0, y0, nx, ny];
          } else {
            activePoints.push(nx, ny);
          }
        }

        x0 = nx;
        y0 = ny;
        segRem -= step;
        patRem -= step;
      }
    }

    if (draw && activePoints && activePoints.length >= 4) {
      out.push({ points: activePoints, closed: false });
    }
  }

  return out;
}

// Pre-allocated typed scratch buffers eliminate per-path JS array allocations and .push() overhead.
let flatX = new Float64Array(8192);
let flatY = new Float64Array(8192);
let flatCount = 0;

let subStart = new Int32Array(256);
let subEnd = new Int32Array(256);
let subClosed = new Uint8Array(256);
let subCount = 0;

let outBuf = new Float32Array(131072);
let outPos = 0;

function ensureFlatCap(need: number): void {
  if (flatCount + need <= flatX.length) return;
  let next = flatX.length * 2;
  while (next < flatCount + need) next *= 2;
  const nx = new Float64Array(next);
  const ny = new Float64Array(next);
  nx.set(flatX.subarray(0, flatCount));
  ny.set(flatY.subarray(0, flatCount));
  flatX = nx;
  flatY = ny;
}

function ensureSubCap(need: number): void {
  if (subCount + need <= subStart.length) return;
  const next = subStart.length * 2;
  const ns = new Int32Array(next);
  const ne = new Int32Array(next);
  const nc = new Uint8Array(next);
  ns.set(subStart.subarray(0, subCount));
  ne.set(subEnd.subarray(0, subCount));
  nc.set(subClosed.subarray(0, subCount));
  subStart = ns;
  subEnd = ne;
  subClosed = nc;
}

function ensureOutCap(needFloats: number): void {
  if (outPos + needFloats <= outBuf.length) return;
  let next = outBuf.length * 2;
  while (next < outPos + needFloats) next *= 2;
  const nb = new Float32Array(next);
  nb.set(outBuf.subarray(0, outPos));
  outBuf = nb;
}

function flattenCubicFast(
  ax: number,
  ay: number,
  bx: number,
  by: number,
  cx: number,
  cy: number,
  dx: number,
  dy: number,
  limitSq: number,
  depth: number,
): void {
  const ux = 3 * bx - 2 * ax - dx;
  const uy = 3 * by - 2 * ay - dy;
  if (ux * ux + uy * uy <= limitSq) {
    const vx = 3 * cx - ax - 2 * dx;
    const vy = 3 * cy - ay - 2 * dy;
    if (vx * vx + vy * vy <= limitSq) {
      flatX[flatCount] = dx;
      flatY[flatCount] = dy;
      flatCount++;
      return;
    }
  }
  if (depth >= 16) {
    flatX[flatCount] = dx;
    flatY[flatCount] = dy;
    flatCount++;
    return;
  }

  const abx = (ax + bx) * 0.5;
  const aby = (ay + by) * 0.5;
  const bcx = (bx + cx) * 0.5;
  const bcy = (by + cy) * 0.5;
  const cdx2 = (cx + dx) * 0.5;
  const cdy2 = (cy + dy) * 0.5;
  const abcx = (abx + bcx) * 0.5;
  const abcy = (aby + bcy) * 0.5;
  const bcdx = (bcx + cdx2) * 0.5;
  const bcdy = (bcy + cdy2) * 0.5;
  const midx = (abcx + bcdx) * 0.5;
  const midy = (abcy + bcdy) * 0.5;

  flattenCubicFast(ax, ay, abx, aby, abcx, abcy, midx, midy, limitSq, depth + 1);
  flattenCubicFast(midx, midy, bcdx, bcdy, cdx2, cdy2, dx, dy, limitSq, depth + 1);
}

function parsePathFast(
  path: ReadonlyArray<number>,
  scale: number,
  tolerance: number,
  transform?: ReadonlyArray<number>,
  maxEdge = 0,
): void {
  flatCount = 0;
  subCount = 0;
  let activeSub = -1;
  void maxEdge;
  let startX = 0;
  let startY = 0;
  let curX = 0;
  let curY = 0;

  const effScale = Math.max(Number(scale) || 1, 1e-6);
  const effTol = Math.max(Number(tolerance) || 0.1, 1e-5) / effScale;
  const limitSq = 16 * effTol * effTol;

  const hasT = Boolean(transform && transform.length >= 6);
  const m11 = hasT ? Number(transform![0]) : 1;
  const m12 = hasT ? Number(transform![1]) : 0;
  const m21 = hasT ? Number(transform![2]) : 0;
  const m22 = hasT ? Number(transform![3]) : 1;
  const tx = hasT ? Number(transform![4]) : 0;
  const ty = hasT ? Number(transform![5]) : 0;

  let i = 0;
  const len = path.length;
  while (i < len) {
    const cmd = path[i++];
    if (cmd === 0) {
      const rx = Number(path[i++] ?? 0);
      const ry = Number(path[i++] ?? 0);
      const x = hasT ? m11 * rx + m21 * ry + tx : rx;
      const y = hasT ? m12 * rx + m22 * ry + ty : ry;
      if (activeSub >= 0) {
        subEnd[activeSub] = flatCount;
      }
      ensureSubCap(1);
      ensureFlatCap(1);
      activeSub = subCount++;
      subStart[activeSub] = flatCount;
      subClosed[activeSub] = 0;
      flatX[flatCount] = x;
      flatY[flatCount] = y;
      flatCount++;
      startX = x;
      startY = y;
      curX = x;
      curY = y;
      subEnd[activeSub] = flatCount;
    } else if (cmd === 1) {
      const rx = Number(path[i++] ?? 0);
      const ry = Number(path[i++] ?? 0);
      const x = hasT ? m11 * rx + m21 * ry + tx : rx;
      const y = hasT ? m12 * rx + m22 * ry + ty : ry;
      if (activeSub < 0) {
        ensureSubCap(1);
        activeSub = subCount++;
        subStart[activeSub] = flatCount;
        subClosed[activeSub] = 0;
        startX = x;
        startY = y;
      }
      ensureFlatCap(1);
      flatX[flatCount] = x;
      flatY[flatCount] = y;
      flatCount++;
      curX = x;
      curY = y;
      subEnd[activeSub] = flatCount;
    } else if (cmd === 2) {
      const rc1x = Number(path[i++] ?? 0);
      const rc1y = Number(path[i++] ?? 0);
      const rc2x = Number(path[i++] ?? 0);
      const rc2y = Number(path[i++] ?? 0);
      const rex = Number(path[i++] ?? 0);
      const rey = Number(path[i++] ?? 0);
      const c1x = hasT ? m11 * rc1x + m21 * rc1y + tx : rc1x;
      const c1y = hasT ? m12 * rc1x + m22 * rc1y + ty : rc1y;
      const c2x = hasT ? m11 * rc2x + m21 * rc2y + tx : rc2x;
      const c2y = hasT ? m12 * rc2x + m22 * rc2y + ty : rc2y;
      const ex = hasT ? m11 * rex + m21 * rey + tx : rex;
      const ey = hasT ? m12 * rex + m22 * rey + ty : rey;
      if (activeSub < 0) {
        ensureSubCap(1);
        ensureFlatCap(1);
        activeSub = subCount++;
        subStart[activeSub] = flatCount;
        subClosed[activeSub] = 0;
        flatX[flatCount] = curX;
        flatY[flatCount] = curY;
        flatCount++;
        startX = curX;
        startY = curY;
      }
      ensureFlatCap(65536);
      flattenCubicFast(curX, curY, c1x, c1y, c2x, c2y, ex, ey, limitSq, 0);
      curX = ex;
      curY = ey;
      subEnd[activeSub] = flatCount;
    } else if (cmd === 3) {
      if (activeSub >= 0) {
        subClosed[activeSub] = 1;
        subEnd[activeSub] = flatCount;
        curX = startX;
        curY = startY;
        activeSub = -1;
      }
    } else {
      break;
    }
  }
}

function emitArcBisect(
  cx: number,
  cy: number,
  r: number,
  u0x: number,
  u0y: number,
  u1x: number,
  u1y: number,
  minSumLenSq: number,
  depth: number,
): void {
  const sx = u0x + u1x;
  const sy = u0y + u1y;
  const lenSq = sx * sx + sy * sy;
  if (lenSq >= minSumLenSq || depth >= 6 || lenSq <= 1e-12) {
    ensureOutCap(6);
    outBuf[outPos++] = cx;
    outBuf[outPos++] = cy;
    outBuf[outPos++] = cx + u0x * r;
    outBuf[outPos++] = cy + u0y * r;
    outBuf[outPos++] = cx + u1x * r;
    outBuf[outPos++] = cy + u1y * r;
    return;
  }
  const invLen = 1 / Math.sqrt(lenSq);
  const mx = sx * invLen;
  const my = sy * invLen;
  emitArcBisect(cx, cy, r, u0x, u0y, mx, my, minSumLenSq, depth + 1);
  emitArcBisect(cx, cy, r, mx, my, u1x, u1y, minSumLenSq, depth + 1);
}

function emitRoundCapFast(
  cx: number,
  cy: number,
  r: number,
  nx: number,
  ny: number,
  dx: number,
  dy: number,
  minSumLenSq: number,
): void {
  // Split the 180-degree semicircle into two 90-degree quadrants around tip direction (dx, dy).
  emitArcBisect(cx, cy, r, nx, ny, dx, dy, minSumLenSq, 0);
  emitArcBisect(cx, cy, r, dx, dy, -nx, -ny, minSumLenSq, 0);
}

function emitJoinFast(
  px: number,
  py: number,
  d0x: number,
  d0y: number,
  n0x: number,
  n0y: number,
  d1x: number,
  d1y: number,
  n1x: number,
  n1y: number,
  r: number,
  join: CanvasLineJoin,
  miterLimit: number,
  maxSkipOneMinusDot: number,
  minSumLenSq: number,
): void {
  const cross = d0x * d1y - d0y * d1x;
  const dot = d0x * d1x + d0y * d1y;
  if (dot > 0 && (Math.abs(cross) <= 1e-6 || 1 - dot <= maxSkipOneMinusDot)) {
    return;
  }

  const outSign = cross >= 0 ? -1 : 1;
  const o0x = n0x * outSign;
  const o0y = n0y * outSign;
  const o1x = n1x * outSign;
  const o1y = n1y * outSign;

  // For round joins, 2 + 2*dot == |o0 + o1|^2 == 4*cos^2(theta/2). When >= minSumLenSq,
  // the arc sagitta in screen pixels is <= tolerance, so a single bevel triangle suffices.
  if (join === "round" && 2 + 2 * dot < minSumLenSq) {
    if (dot < -0.999) {
      emitRoundCapFast(px, py, r, o0x, o0y, d0x, d0y, minSumLenSq);
    } else {
      emitArcBisect(px, py, r, o0x, o0y, o1x, o1y, minSumLenSq, 0);
    }
    return;
  }

  const p0x = px + o0x * r;
  const p0y = py + o0y * r;
  const p1x = px + o1x * r;
  const p1y = py + o1y * r;

  if (join === "miter") {
    const denom = 1 + dot;
    const limit = Math.max(1, miterLimit);
    if (denom > 1e-6 && 2 <= limit * limit * denom) {
      const s = r / denom;
      const mx = px + (o0x + o1x) * s;
      const my = py + (o0y + o1y) * s;
      ensureOutCap(12);
      outBuf[outPos++] = px;
      outBuf[outPos++] = py;
      outBuf[outPos++] = p0x;
      outBuf[outPos++] = p0y;
      outBuf[outPos++] = mx;
      outBuf[outPos++] = my;
      outBuf[outPos++] = px;
      outBuf[outPos++] = py;
      outBuf[outPos++] = mx;
      outBuf[outPos++] = my;
      outBuf[outPos++] = p1x;
      outBuf[outPos++] = p1y;
      return;
    }
  }

  ensureOutCap(6);
  outBuf[outPos++] = px;
  outBuf[outPos++] = py;
  outBuf[outPos++] = p0x;
  outBuf[outPos++] = p0y;
  outBuf[outPos++] = p1x;
  outBuf[outPos++] = p1y;
}

function strokeSubpathRange(
  ptStart: number,
  ptEnd: number,
  closed: boolean,
  r: number,
  cap: CanvasLineCap,
  join: CanvasLineJoin,
  miter: number,
  maxSkipOneMinusDot: number,
  minSumLenSq: number,
): void {
  const ptCount = ptEnd - ptStart;
  if (ptCount < 2) return;

  ensureOutCap(ptCount * 24 + 96);

  let hasFirst = false;
  let firstX0 = 0;
  let firstY0 = 0;
  let firstDx = 0;
  let firstDy = 0;
  let firstNx = 0;
  let firstNy = 0;

  let prevX1 = 0;
  let prevY1 = 0;
  let prevDx = 0;
  let prevDy = 0;
  let prevNx = 0;
  let prevNy = 0;

  const isRoundJoin = join === "round";
  const isMiterJoin = join === "miter";
  const miterLim = Math.max(1, miter);
  const miterLimSq = miterLim * miterLim;

  const totalEdges = closed ? ptCount : ptCount - 1;
  for (let e = 0; e < totalEdges; e++) {
    const i0 = ptStart + e;
    const i1 = e + 1 < ptCount ? i0 + 1 : ptStart;
    const x0 = flatX[i0];
    const y0 = flatY[i0];
    const x1 = flatX[i1];
    const y1 = flatY[i1];
    const dx = x1 - x0;
    const dy = y1 - y0;
    const lenSq = dx * dx + dy * dy;
    if (lenSq <= 1e-16) continue;
    const inv = 1 / Math.sqrt(lenSq);
    const ux = dx * inv;
    const uy = dy * inv;
    const nx = -uy;
    const ny = ux;

    const rx = nx * r;
    const ry = ny * r;
    const l0x = x0 + rx;
    const l0y = y0 + ry;
    const r0x = x0 - rx;
    const r0y = y0 - ry;
    const l1x = x1 + rx;
    const l1y = y1 + ry;
    const r1x = x1 - rx;
    const r1y = y1 - ry;

    if (outPos + 24 > outBuf.length) {
      ensureOutCap(24);
    }
    let p = outPos;
    outBuf[p] = l0x;
    outBuf[p + 1] = l0y;
    outBuf[p + 2] = r0x;
    outBuf[p + 3] = r0y;
    outBuf[p + 4] = l1x;
    outBuf[p + 5] = l1y;
    outBuf[p + 6] = r0x;
    outBuf[p + 7] = r0y;
    outBuf[p + 8] = r1x;
    outBuf[p + 9] = r1y;
    outBuf[p + 10] = l1x;
    outBuf[p + 11] = l1y;
    outPos = p + 12;

    if (!hasFirst) {
      hasFirst = true;
      firstX0 = x0;
      firstY0 = y0;
      firstDx = ux;
      firstDy = uy;
      firstNx = nx;
      firstNy = ny;
    } else {
      const cross = prevDx * uy - prevDy * ux;
      const dot = prevDx * ux + prevDy * uy;
      if (!(dot > 0 && (Math.abs(cross) <= 1e-6 || 1 - dot <= maxSkipOneMinusDot))) {
        const outSign = cross >= 0 ? -1 : 1;
        const o0x = prevNx * outSign;
        const o0y = prevNy * outSign;
        const o1x = nx * outSign;
        const o1y = ny * outSign;
        if (isRoundJoin && 2 + 2 * dot < minSumLenSq) {
          if (dot < -0.999) {
            emitRoundCapFast(prevX1, prevY1, r, o0x, o0y, prevDx, prevDy, minSumLenSq);
          } else {
            emitArcBisect(prevX1, prevY1, r, o0x, o0y, o1x, o1y, minSumLenSq, 0);
          }
        } else {
          const p0x = prevX1 + o0x * r;
          const p0y = prevY1 + o0y * r;
          const p1x = prevX1 + o1x * r;
          const p1y = prevY1 + o1y * r;
          const denom = 1 + dot;
          if (isMiterJoin && denom > 1e-6 && 2 <= miterLimSq * denom) {
            const s = r / denom;
            const mx = prevX1 + (o0x + o1x) * s;
            const my = prevY1 + (o0y + o1y) * s;
            p = outPos;
            outBuf[p] = prevX1;
            outBuf[p + 1] = prevY1;
            outBuf[p + 2] = p0x;
            outBuf[p + 3] = p0y;
            outBuf[p + 4] = mx;
            outBuf[p + 5] = my;
            outBuf[p + 6] = prevX1;
            outBuf[p + 7] = prevY1;
            outBuf[p + 8] = mx;
            outBuf[p + 9] = my;
            outBuf[p + 10] = p1x;
            outBuf[p + 11] = p1y;
            outPos = p + 12;
          } else {
            p = outPos;
            outBuf[p] = prevX1;
            outBuf[p + 1] = prevY1;
            outBuf[p + 2] = p0x;
            outBuf[p + 3] = p0y;
            outBuf[p + 4] = p1x;
            outBuf[p + 5] = p1y;
            outPos = p + 6;
          }
        }
      }
    }

    prevX1 = x1;
    prevY1 = y1;
    prevDx = ux;
    prevDy = uy;
    prevNx = nx;
    prevNy = ny;
  }

  if (!hasFirst) return;

  if (closed) {
    emitJoinFast(
      prevX1,
      prevY1,
      prevDx,
      prevDy,
      prevNx,
      prevNy,
      firstDx,
      firstDy,
      firstNx,
      firstNy,
      r,
      join,
      miter,
      maxSkipOneMinusDot,
      minSumLenSq,
    );
  } else if (cap === "square") {
    ensureOutCap(24);
    const sdx = firstDx * r;
    const sdy = firstDy * r;
    const snx = firstNx * r;
    const sny = firstNy * r;
    const sl0x = firstX0 + snx;
    const sl0y = firstY0 + sny;
    const sr0x = firstX0 - snx;
    const sr0y = firstY0 - sny;
    const sl1x = sl0x - sdx;
    const sl1y = sl0y - sdy;
    const sr1x = sr0x - sdx;
    const sr1y = sr0y - sdy;
    outBuf[outPos++] = sl1x;
    outBuf[outPos++] = sl1y;
    outBuf[outPos++] = sr1x;
    outBuf[outPos++] = sr1y;
    outBuf[outPos++] = sl0x;
    outBuf[outPos++] = sl0y;
    outBuf[outPos++] = sr1x;
    outBuf[outPos++] = sr1y;
    outBuf[outPos++] = sr0x;
    outBuf[outPos++] = sr0y;
    outBuf[outPos++] = sl0x;
    outBuf[outPos++] = sl0y;

    const edx = prevDx * r;
    const edy = prevDy * r;
    const enx = prevNx * r;
    const eny = prevNy * r;
    const el0x = prevX1 + enx;
    const el0y = prevY1 + eny;
    const er0x = prevX1 - enx;
    const er0y = prevY1 - eny;
    const el1x = el0x + edx;
    const el1y = el0y + edy;
    const er1x = er0x + edx;
    const er1y = er0y + edy;
    outBuf[outPos++] = el0x;
    outBuf[outPos++] = el0y;
    outBuf[outPos++] = er0x;
    outBuf[outPos++] = er0y;
    outBuf[outPos++] = el1x;
    outBuf[outPos++] = el1y;
    outBuf[outPos++] = er0x;
    outBuf[outPos++] = er0y;
    outBuf[outPos++] = er1x;
    outBuf[outPos++] = er1y;
    outBuf[outPos++] = el1x;
    outBuf[outPos++] = el1y;
  } else if (cap === "round" && r > 0) {
    emitRoundCapFast(firstX0, firstY0, r, firstNx, firstNy, -firstDx, -firstDy, minSumLenSq);
    emitRoundCapFast(prevX1, prevY1, r, -prevNx, -prevNy, prevDx, prevDy, minSumLenSq);
  }
}

function subdivideFlatSubpaths(maxEdge: number): void {
  if (maxEdge <= 0 || subCount <= 0) return;
  const newX: number[] = [];
  const newY: number[] = [];
  for (let s = 0; s < subCount; s++) {
    const start = subStart[s];
    const end = subEnd[s];
    const closed = subClosed[s] !== 0;
    const nPts = end - start;
    subStart[s] = newX.length;
    if (nPts > 0) {
      newX.push(flatX[start]);
      newY.push(flatY[start]);
      for (let i = start; i < end - 1; i++) {
        const x0 = flatX[i];
        const y0 = flatY[i];
        const x1 = flatX[i + 1];
        const y1 = flatY[i + 1];
        const dist = Math.hypot(x1 - x0, y1 - y0);
        if (dist > maxEdge) {
          const steps = Math.ceil(dist / maxEdge);
          for (let k = 1; k < steps; k++) {
            const t = k / steps;
            newX.push(x0 + (x1 - x0) * t);
            newY.push(y0 + (y1 - y0) * t);
          }
        }
        newX.push(x1);
        newY.push(y1);
      }
      if (closed && nPts >= 2) {
        const x0 = flatX[end - 1];
        const y0 = flatY[end - 1];
        const x1 = flatX[start];
        const y1 = flatY[start];
        const dist = Math.hypot(x1 - x0, y1 - y0);
        if (dist > maxEdge) {
          const steps = Math.ceil(dist / maxEdge);
          for (let k = 1; k < steps; k++) {
            const t = k / steps;
            newX.push(x0 + (x1 - x0) * t);
            newY.push(y0 + (y1 - y0) * t);
          }
        }
      }
    }
    subEnd[s] = newX.length;
  }
  flatCount = 0;
  ensureFlatCap(newX.length);
  for (let i = 0; i < newX.length; i++) {
    flatX[i] = newX[i];
    flatY[i] = newY[i];
  }
  flatCount = newX.length;
}

export function getLastFlattenedSubpaths(): Subpath[] {
  const out: Subpath[] = [];
  for (let s = 0; s < subCount; s++) {
    const start = subStart[s];
    const end = subEnd[s];
    const pts: number[] = [];
    for (let i = start; i < end; i++) {
      pts.push(flatX[i], flatY[i]);
    }
    out.push({ points: pts, closed: subClosed[s] !== 0 });
  }
  return out;
}

export function tessellateStrokeView(
  path: ReadonlyArray<number>,
  pen: StrokePenOptions,
  scale = 1,
  tolerance = 0.1,
  transform?: ReadonlyArray<number>,
  maxEdge = 0,
): Float32Array {
  outPos = 0;
  const effWidth = pen.width > 0 ? pen.width : 1;
  const r = effWidth * 0.5;
  const cap = mapLineCap(pen.cap ?? "butt");
  const join = mapLineJoin(pen.join ?? "miter");
  const miter = Number(pen.miter ?? 2);

  const effScale = Math.max(Number(scale) || 1, 1e-6);
  const effTol = Math.max(Number(tolerance) || 0.1, 1e-5) / effScale;
  const cosHalfMax = Math.max(0, 1 - effTol / Math.max(r, effTol));
  const minSumLenSq = 4 * cosHalfMax * cosHalfMax;
  // Outer corner gap is r * sqrt(2 * (1 - dot)); skip join only when that gap is <= effTol.
  const maxSkipOneMinusDot = (effTol * effTol) / Math.max(1e-12, 2 * r * r);

  if (pen.dash && pen.dash.length > 0) {
    const subpaths = applyDash(parsePath(path, scale, tolerance, transform, maxEdge), pen.dash, effWidth);
    if (maxEdge > 0) {
      for (let s = 0; s < subpaths.length; s++) {
        subpaths[s].points = subdivideSubpathPoints(subpaths[s].points, subpaths[s].closed, maxEdge);
      }
    }
    for (let s = 0; s < subpaths.length; s++) {
      const pts = subpaths[s].points;
      const nPts = pts.length >> 1;
      flatCount = 0;
      ensureFlatCap(nPts);
      for (let k = 0; k < nPts; k++) {
        flatX[k] = pts[k * 2];
        flatY[k] = pts[k * 2 + 1];
      }
      flatCount = nPts;
      strokeSubpathRange(0, nPts, subpaths[s].closed, r, cap, join, miter, maxSkipOneMinusDot, minSumLenSq);
    }
    return outBuf.subarray(0, outPos);
  }

  parsePathFast(path, scale, tolerance, transform, maxEdge);
  if (maxEdge > 0) {
    subdivideFlatSubpaths(maxEdge);
  }
  for (let s = 0; s < subCount; s++) {
    strokeSubpathRange(
      subStart[s],
      subEnd[s],
      subClosed[s] !== 0,
      r,
      cap,
      join,
      miter,
      maxSkipOneMinusDot,
      minSumLenSq,
    );
  }
  return outBuf.subarray(0, outPos);
}

export function tessellateStroke(
  path: ReadonlyArray<number>,
  pen: StrokePenOptions,
  scale = 1,
  tolerance = 0.1,
  transform?: ReadonlyArray<number>,
  maxEdge = 0,
): Float32Array {
  return tessellateStrokeView(path, pen, scale, tolerance, transform, maxEdge).slice();
}

export function normalizeFillRule(rule: FillRuleInput = "evenodd"): CanvasFillRule {
  if (rule === 1 || rule === "nonzero") return "nonzero";
  return "evenodd";
}

export function tessellateFillBandsView(
  path: ReadonlyArray<number>,
  rule: FillRuleInput = "evenodd",
  maxEdge = 4,
  scale = 1,
  tolerance = 0.1,
  transform?: ReadonlyArray<number>,
): Float32Array {
  outPos = 0;
  const effMaxEdge = Math.max(0.5, Number(maxEdge) || 4);
  const fillRule = normalizeFillRule(rule);
  parsePathFast(path, scale, tolerance, transform, 0);

  interface BandEdge {
    ax: number;
    ay: number;
    bx: number;
    by: number;
    ymin: number;
    ymax: number;
    dir: number;
  }

  const edges: BandEdge[] = [];
  const rawYs: number[] = [];

  for (let s = 0; s < subCount; s++) {
    const start = subStart[s];
    const end = subEnd[s];
    const nPts = end - start;
    if (nPts < 2) continue;
    for (let i = 0; i < nPts; i++) {
      const i0 = start + i;
      const i1 = i + 1 < nPts ? i0 + 1 : start;
      const ax = flatX[i0];
      const ay = flatY[i0];
      const bx = flatX[i1];
      const by = flatY[i1];
      if (!Number.isFinite(ax) || !Number.isFinite(ay) || !Number.isFinite(bx) || !Number.isFinite(by)) continue;
      if (Math.abs(by - ay) <= 1e-9) continue;
      const ymin = Math.min(ay, by);
      const ymax = Math.max(ay, by);
      const dir = by > ay ? 1 : -1;
      edges.push({ ax, ay, bx, by, ymin, ymax, dir });
      rawYs.push(ymin, ymax);
    }
  }

  if (edges.length < 2) {
    return outBuf.subarray(0, 0);
  }

  const xAt = (e: BandEdge, y: number): number => e.ax + ((y - e.ay) / (e.by - e.ay)) * (e.bx - e.ax);

  if (edges.length <= 1024) {
    for (let i = 0; i < edges.length; i++) {
      const e1 = edges[i];
      for (let j = i + 1; j < edges.length; j++) {
        const e2 = edges[j];
        const yA = Math.max(e1.ymin, e2.ymin);
        const yB = Math.min(e1.ymax, e2.ymax);
        if (yB - yA <= 1e-7) continue;
        const dA = xAt(e1, yA) - xAt(e2, yA);
        const dB = xAt(e1, yB) - xAt(e2, yB);
        if ((dA < -1e-7 && dB > 1e-7) || (dA > 1e-7 && dB < -1e-7)) {
          const yCross = yA + (yB - yA) * (Math.abs(dA) / (Math.abs(dA) + Math.abs(dB)));
          rawYs.push(yCross);
        }
      }
    }
  }

  rawYs.sort((a, b) => a - b);
  const uniqueYs: number[] = [rawYs[0]];
  for (let i = 1; i < rawYs.length; i++) {
    if (rawYs[i] - uniqueYs[uniqueYs.length - 1] > 1e-6) {
      uniqueYs.push(rawYs[i]);
    }
  }
  if (uniqueYs.length < 2) {
    return outBuf.subarray(0, 0);
  }

  const bandYs: number[] = [uniqueYs[0]];
  for (let i = 0; i < uniqueYs.length - 1; i++) {
    const yA = uniqueYs[i];
    const yB = uniqueYs[i + 1];
    const span = yB - yA;
    const steps = Math.max(1, Math.ceil(span / effMaxEdge));
    for (let k = 1; k <= steps; k++) {
      bandYs.push(k === steps ? yB : yA + (span * k) / steps);
    }
  }

  interface ActiveCross {
    xTop: number;
    xBot: number;
    xMid: number;
    dir: number;
  }

  for (let b = 0; b < bandYs.length - 1; b++) {
    const y0 = bandYs[b];
    const y1 = bandYs[b + 1];
    const dy = y1 - y0;
    if (dy <= 1e-7) continue;
    const yMid = 0.5 * (y0 + y1);

    const active: ActiveCross[] = [];
    for (let i = 0; i < edges.length; i++) {
      const e = edges[i];
      if (e.ymin < yMid && e.ymax > yMid) {
        const xTop = xAt(e, y0);
        const xBot = xAt(e, y1);
        active.push({ xTop, xBot, xMid: 0.5 * (xTop + xBot), dir: e.dir });
      }
    }
    if (active.length < 2) continue;

    active.sort((a, c) => a.xMid - c.xMid || a.xTop - c.xTop || a.xBot - c.xBot);

    const intervals: Array<[number, number, number, number]> = [];
    if (fillRule === "evenodd") {
      for (let k = 0; k + 1 < active.length; k += 2) {
        const e0 = active[k];
        const e1 = active[k + 1];
        intervals.push([
          Math.min(e0.xTop, e1.xTop),
          Math.max(e0.xTop, e1.xTop),
          Math.min(e0.xBot, e1.xBot),
          Math.max(e0.xBot, e1.xBot),
        ]);
      }
    } else {
      let winding = 0;
      let startCross: ActiveCross | null = null;
      for (let k = 0; k < active.length; k++) {
        const cur = active[k];
        if (winding === 0) {
          startCross = cur;
        }
        winding += cur.dir;
        if (winding === 0 && startCross !== null) {
          intervals.push([
            Math.min(startCross.xTop, cur.xTop),
            Math.max(startCross.xTop, cur.xTop),
            Math.min(startCross.xBot, cur.xBot),
            Math.max(startCross.xBot, cur.xBot),
          ]);
          startCross = null;
        }
      }
    }

    for (let idx = 0; idx < intervals.length; idx++) {
      const [tL, tR, bL, bR] = intervals[idx];
      if (tR - tL <= 1e-8 && bR - bL <= 1e-8) continue;
      const maxShiftX = Math.max(Math.abs(bL - tL), Math.abs(bR - tR));
      const vSteps = Math.max(1, Math.ceil(Math.max(dy, maxShiftX * 2) / effMaxEdge));
      for (let v = 0; v < vSteps; v++) {
        const v0 = v / vSteps;
        const v1 = (v + 1) / vSteps;
        const sy0 = y0 + dy * v0;
        const sy1 = y0 + dy * v1;
        const stL = tL + (bL - tL) * v0;
        const stR = tR + (bR - tR) * v0;
        const sbL = tL + (bL - tL) * v1;
        const sbR = tR + (bR - tR) * v1;
        const topW = Math.max(0, stR - stL);
        const botW = Math.max(0, sbR - sbL);
        if (topW <= 1e-8 && botW <= 1e-8) continue;
        const subShift = Math.max(Math.abs(sbL - stL), Math.abs(sbR - stR));
        const availW = Math.max(0.25 * effMaxEdge, effMaxEdge - subShift);
        const hSteps = Math.max(1, Math.ceil(Math.max(topW, botW) / availW));
        ensureOutCap(outPos + hSteps * 12);
        for (let h = 0; h < hSteps; h++) {
          const u0 = h / hSteps;
          const u1 = (h + 1) / hSteps;
          const x00 = stL + topW * u0;
          const x10 = stL + topW * u1;
          const x01 = sbL + botW * u0;
          const x11 = sbL + botW * u1;
          if (topW > 1e-8) {
            outBuf[outPos++] = x00;
            outBuf[outPos++] = sy0;
            outBuf[outPos++] = x10;
            outBuf[outPos++] = sy0;
            outBuf[outPos++] = x11;
            outBuf[outPos++] = sy1;
          }
          if (botW > 1e-8) {
            outBuf[outPos++] = x00;
            outBuf[outPos++] = sy0;
            outBuf[outPos++] = x11;
            outBuf[outPos++] = sy1;
            outBuf[outPos++] = x01;
            outBuf[outPos++] = sy1;
          }
        }
      }
    }
  }

  return outBuf.subarray(0, outPos);
}

export function tessellateFillBands(
  path: ReadonlyArray<number>,
  rule: FillRuleInput = "evenodd",
  maxEdge = 4,
  scale = 1,
  tolerance = 0.1,
  transform?: ReadonlyArray<number>,
): Float32Array {
  return tessellateFillBandsView(path, rule, maxEdge, scale, tolerance, transform).slice();
}

export function tessellateFillView(
  path: ReadonlyArray<number>,
  _rule: FillRuleInput = "evenodd",
  scale = 1,
  tolerance = 0.1,
  transform?: ReadonlyArray<number>,
  maxEdge = 0,
): Float32Array {
  if (maxEdge > 0) {
    return tessellateFillBandsView(path, _rule, maxEdge, scale, tolerance, transform);
  }
  outPos = 0;
  parsePathFast(path, scale, tolerance, transform, 0);

  for (let s = 0; s < subCount; s++) {
    const start = subStart[s];
    const end = subEnd[s];
    const nPts = end - start;
    if (nPts < 3) continue;
    ensureOutCap((nPts - 2) * 6);
    const x0 = flatX[start];
    const y0 = flatY[start];
    for (let i = start + 1; i < end - 1; i++) {
      const x1 = flatX[i];
      const y1 = flatY[i];
      const x2 = flatX[i + 1];
      const y2 = flatY[i + 1];
      const cross = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
      if (Math.abs(cross) <= 1e-10) continue;
      outBuf[outPos++] = x0;
      outBuf[outPos++] = y0;
      outBuf[outPos++] = x1;
      outBuf[outPos++] = y1;
      outBuf[outPos++] = x2;
      outBuf[outPos++] = y2;
    }
  }

  return outBuf.subarray(0, outPos);
}

export function tessellateFill(
  path: ReadonlyArray<number>,
  rule: FillRuleInput = "evenodd",
  scale = 1,
  tolerance = 0.1,
  transform?: ReadonlyArray<number>,
  maxEdge = 0,
): Float32Array {
  return tessellateFillView(path, rule, scale, tolerance, transform, maxEdge).slice();
}

export function computeBounds(vertices: Float32Array): [number, number, number, number] {
  if (vertices.length < 2) {
    return [0, 0, 0, 0];
  }
  let minX = Infinity;
  let minY = Infinity;
  let maxX = -Infinity;
  let maxY = -Infinity;
  for (let i = 0; i < vertices.length; i += 2) {
    const x = vertices[i];
    const y = vertices[i + 1];
    if (x < minX) minX = x;
    if (y < minY) minY = y;
    if (x > maxX) maxX = x;
    if (y > maxY) maxY = y;
  }
  return [minX, minY, maxX, maxY];
}

export function computeTrianglesArea(vertices: Float32Array): number {
  let area = 0;
  for (let i = 0; i <= vertices.length - 6; i += 6) {
    const x0 = vertices[i];
    const y0 = vertices[i + 1];
    const x1 = vertices[i + 2];
    const y1 = vertices[i + 3];
    const x2 = vertices[i + 4];
    const y2 = vertices[i + 5];
    area += Math.abs((x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0)) * 0.5;
  }
  return area;
}

export function evalFillFanAtPoint(
  vertices: Float32Array,
  px: number,
  py: number,
  rule: FillRuleInput = "evenodd",
): boolean {
  const fillRule = normalizeFillRule(rule);
  let winding = 0;
  let parity = 0;

  for (let i = 0; i <= vertices.length - 6; i += 6) {
    const x0 = vertices[i];
    const y0 = vertices[i + 1];
    const x1 = vertices[i + 2];
    const y1 = vertices[i + 3];
    const x2 = vertices[i + 4];
    const y2 = vertices[i + 5];

    const c0 = (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
    const c1 = (x2 - x1) * (py - y1) - (y2 - y1) * (px - x1);
    const c2 = (x0 - x2) * (py - y2) - (y0 - y2) * (px - x2);

    if (c0 >= 0 && c1 >= 0 && c2 >= 0 && c0 + c1 + c2 > 0) {
      winding += 1;
      parity ^= 1;
    } else if (c0 <= 0 && c1 <= 0 && c2 <= 0 && c0 + c1 + c2 < 0) {
      winding -= 1;
      parity ^= 1;
    }
  }

  return fillRule === "evenodd" ? parity !== 0 : winding !== 0;
}

const hashF64 = new Float64Array(1);
const hashI32 = new Int32Array(hashF64.buffer);
const pathHashWeakCache = new WeakMap<object, string>();

export function hashNumbers(nums: ReadonlyArray<number>): string {
  const cached = pathHashWeakCache.get(nums as object);
  if (cached !== undefined) return cached;

  let h1 = 0x811c9dc5 | 0;
  let h2 = 0x01000193 | 0;
  const len = nums.length;
  h1 = Math.imul(h1 ^ len, 0x01000193);
  for (let i = 0; i < len; i++) {
    hashF64[0] = nums[i];
    const lo = hashI32[0];
    const hi = hashI32[1];
    h1 = Math.imul(h1 ^ lo, 0x01000193);
    h2 = Math.imul(h2 ^ hi, 0x85ebca6b);
    h1 ^= h2 >>> 13;
  }
  const res = `${h1 >>> 0}_${h2 >>> 0}_${len}`;
  if (typeof nums === "object" && nums !== null) {
    pathHashWeakCache.set(nums as object, res);
  }
  return res;
}

export function computeScaleFactor(
  transform: ReadonlyArray<number>,
  dpr: number,
  cosmetic = false,
): number {
  const effDpr = Math.max(0.1, Number(dpr) || 1);
  if (cosmetic) {
    return effDpr;
  }
  const m11 = Number(transform[0] ?? 1);
  const m12 = Number(transform[1] ?? 0);
  const m21 = Number(transform[2] ?? 0);
  const m22 = Number(transform[3] ?? 1);
  const sx = Math.hypot(m11, m12);
  const sy = Math.hypot(m21, m22);
  return Math.max(1e-4, Math.max(sx, sy) * effDpr);
}

export function computeScaleBucket(
  transform: ReadonlyArray<number>,
  dpr: number,
  cosmetic = false,
): { bucket: number; scale: number } {
  const raw = computeScaleFactor(transform, dpr, cosmetic);
  const bucket = Math.ceil(Math.log2(Math.max(1, raw)));
  return { bucket, scale: Math.pow(2, bucket) };
}
