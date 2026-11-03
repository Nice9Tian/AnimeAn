// Archived 2026-11-02: Hobby's METAFONT spline, the second smoothing method of the curve tool demo
// (docs/plan/2026-11-02-线条与圆角工具/). The user chose centripetal Catmull-Rom; this stays for reference.
//
// Why it lost: the solve is global, so every new anchor moves the whole line, and at zig-zag turns
// the arcs swing far past the anchors. Checked before archiving: unevenly spaced anchors on a circle
// come back as that circle within 0.6 % (closed) and 0.03 % (open); G1 at every anchor.
//
// Tension 1, curl 1. theta[k] and phi[k] are the angles the curve makes with chords k and k-1 at
// knot k, theta + phi = psi (the turn of the polyline). Returns cubic segments [p0, c1, c2, p1].

const sub = (a, b) => ({ x: a.x - b.x, y: a.y - b.y });
const add = (a, b) => ({ x: a.x + b.x, y: a.y + b.y });
const mul = (a, k) => ({ x: a.x * k, y: a.y * k });
const len = a => Math.hypot(a.x, a.y);
const lerp = (a, b, t) => add(a, mul(sub(b, a), t));
const rot = (a, ang) => ({ x: a.x * Math.cos(ang) - a.y * Math.sin(ang), y: a.x * Math.sin(ang) + a.y * Math.cos(ang) });
const EPS = 1e-6;

function solve(A, b) {
  const n = b.length;
  for (let c = 0; c < n; c++) {
    let p = c;
    for (let r = c + 1; r < n; r++) if (Math.abs(A[r][c]) > Math.abs(A[p][c])) p = r;
    [A[c], A[p]] = [A[p], A[c]]; [b[c], b[p]] = [b[p], b[c]];
    for (let r = c + 1; r < n; r++) {
      const k = A[r][c] / A[c][c];
      if (!k) continue;
      for (let j = c; j < n; j++) A[r][j] -= k * A[c][j];
      b[r] -= k * b[c];
    }
  }
  const x = new Array(n).fill(0);
  for (let r = n - 1; r >= 0; r--) {
    let s = b[r];
    for (let j = r + 1; j < n; j++) s -= A[r][j] * x[j];
    x[r] = s / A[r][r];
  }
  return x;
}

function hobby(P, closed) {
  const n = P.length, count = closed ? n : n - 1;
  if (!closed && n === 2) return [[P[0], lerp(P[0], P[1], 1 / 3), lerp(P[0], P[1], 2 / 3), P[1]]];
  const at = i => P[(i + n) % n];
  const chord = [], d = [];
  for (let i = 0; i < count; i++) { chord[i] = sub(at(i + 1), at(i)); d[i] = Math.max(len(chord[i]), EPS); }
  const turn = i => {
    const a = chord[(i - 1 + count) % count], b = chord[i % count];
    return Math.atan2(a.x * b.y - a.y * b.x, a.x * b.x + a.y * b.y);
  };
  const psi = [];
  for (let k = 0; k < n; k++) psi[k] = (closed || (k > 0 && k < n - 1)) ? turn(k) : 0;
  const size = n, A = [], r = [];
  for (let k = 0; k < size; k++) { A[k] = new Array(size).fill(0); r[k] = 0; }
  const put = (k, j, v) => { A[k][(j + size) % size] += v; };
  for (let k = 0; k < n; k++) {
    if (!closed && k === 0) { put(0, 0, 1); put(0, 1, 1); r[0] = psi[1]; continue; }             // curl at the start
    if (!closed && k === n - 1) { put(k, k - 1, 1); put(k, k, 1); r[k] = 0; continue; }          // curl at the end
    const d0 = d[(k - 1 + count) % count], d1 = d[k % count];
    put(k, k - 1, 1 / d0); put(k, k, 2 / d0 + 2 / d1); put(k, k + 1, 1 / d1);
    r[k] = 2 * psi[k] / d0 + psi[(k + 1) % n] / d1;
  }
  const x = solve(A, r);
  const velocity = (t, f) => {
    const st = Math.sin(t), sf = Math.sin(f), ct = Math.cos(t), cf = Math.cos(f);
    return (2 + Math.SQRT2 * (st - sf / 16) * (sf - st / 16) * (ct - cf))
      / (3 * (1 + 0.5 * (Math.sqrt(5) - 1) * ct + 0.5 * (3 - Math.sqrt(5)) * cf));
  };
  const segs = [];
  for (let i = 0; i < count; i++) {
    const a = at(i), b = at(i + 1);
    const theta = x[i];
    const j = (i + 1) % n;
    const phi = (!closed && j === n - 1) ? -x[j] : psi[j] - x[j];
    const u = mul(chord[i], 1 / d[i]);
    const c1 = add(a, mul(rot(u, -theta), d[i] * velocity(theta, phi)));
    const c2 = sub(b, mul(rot(u, phi), d[i] * velocity(phi, theta)));
    segs.push([a, c1, c2, b]);
  }
  return segs;
}

if (typeof module !== "undefined") module.exports = { hobby };
