// fable_band: acceptance-band probes on the three explainer pages' OWN solve() (cut out of ../*.html exactly as
// page_dedup_check.js does), with NOTHING excluded. Per rig:
//   fold   every fold edge (vertical + horizontal), midpoint, both sides, 29 distances 1e-10 .. 1e-3 px
//   plain  every non-fold interior edge, midpoint, both sides, 9 distances 1e-12 .. 1e-4 px
//   node   every interior node, 8 directions, 17 distances 1e-12 .. 1e-4 px
//   rand   5000 uniform probes (same box as page_dedup_check)
//   latt   A = hv(arcs) exactly at every interior node and fold/non-fold edge midpoint, +-1 ulp nudges (9 points)
// Counted: law = sum(orient) of the page's kept rows != w; odd = odd number of UNMERGED reports within 1 arc px of
// the probed edge midpoint (fold probes with d <= 3e-4 only); false / lost = reports vs the exact truth (closed
// cells of the rounded node mesh, BigInt, independent of the page code; w_orient0 and fold_inverse only, the
// fold_severing rows carry no cell id); miss = (latt) no kept row within 1e-6 of the arcs.
// usage: node page_band_check.js
'use strict';
const fs = require('fs');
function cut(file, startMarker, endMarker) {
  const lines = fs.readFileSync(__dirname + '/../' + file, 'utf8').split('\n');
  const a = lines.findIndex(l => l.includes(startMarker));
  const b = lines.findIndex((l, k) => k > a && l.includes(endMarker));
  if (a < 0 || b < 0) throw new Error('markers not found in ' + file);
  const src = lines.slice(a, b).join('\n');
  const rawSrc = src.replace(/DEDUP = 1e-[46]/, 'DEDUP = 0');
  return { src, rawSrc };
}
const pages = [
  { file: 'w_orient0_explainer.html', cut: cut('w_orient0_explainer.html', 'const EPS = 1e-7, DEDUP = 1e-4, N = 24;', 'drawing helpers'),
    load: src => { const g = new Function(src + '\nreturn { rigs, build, solve, hv };')(); return Object.keys(g.rigs).map(key => { const [h, v] = g.rigs[key].make(); return { key, F: g.build(h, v, 0), rows: (F, A) => g.solve(F, A).rows.map(r => ({ ...r, cell: r.i + ',' + r.j })), hv: g.hv }; }); } },
  { file: 'fold_severing_map.html', cut: cut('fold_severing_map.html', 'const EPS = 1e-7, DEDUP = 1e-6;', 'const $ = id =>'),
    load: src => { const g = new Function(src + '\nreturn { rigs, build, solve };')(); return Object.keys(g.rigs).map(key => { const [h, v] = g.rigs[key].make(); return { key, F: g.build(h, v), rows: (F, A) => g.solve(F, A, true).rows, hv: null }; }); } },
  { file: 'fold_inverse_explainer.html', cut: cut('fold_inverse_explainer.html', '// ------------------------------------------------------------ geometry', '// ------------------------------------------------------------ state & ui'),
    load: src => {
      const g = new Function(src + '\nreturn { sinGuide, hookRig, hairpin, buildFrame, solve, hv };')();
      const segs = 20, rigs = { straight: [0, 0], bow30: [30, 30], bow60: [60, 60], bow100: [100, 100] };
      const out = Object.keys(rigs).map(key => ({ key, F: g.buildFrame(g.sinGuide(rigs[key][0], 'h', segs), g.sinGuide(rigs[key][1], 'v', segs)) }));
      out.push({ key: 'hairpin', F: g.buildFrame(...g.hairpin(segs)) }, { key: 'hook', F: g.buildFrame(...g.hookRig()) });
      for (const o of out) { o.rows = (F, A) => g.solve(F, A, 'all').rows.map(r => ({ ...r, cell: r.c.i + ',' + r.c.j })); o.hv = g.hv; }
      return out;
    } },
];

// ---- exact truth, independent of the pages ----
const dv = new DataView(new ArrayBuffer(8));
function big(x) { if (x === 0) return 0n; dv.setFloat64(0, x); const hi = dv.getUint32(0), lo = dv.getUint32(4), e = (hi >>> 20) & 0x7ff; let m = (BigInt(hi & 0xfffff) << 32n) | BigInt(lo); if (e) m = (m | (1n << 52n)) << BigInt(e - 1); return (hi >>> 31) ? -m : m; }
function ori(ax, ay, bx, by, cx, cy) { const C = big(cx), D = big(cy), d = (big(ax) - C) * (big(by) - D) - (big(ay) - D) * (big(bx) - C); return d > 0n ? 1 : d < 0n ? -1 : 0; }
function truthCells(F, A) {
  const cx = A[0] + F.o[0], cy = A[1] + F.o[1], h = F.h, v = F.v, L = F.nh - 1, M = F.nv - 1, out = new Set();
  for (let i = 0; i <= L; i++) for (let j = 0; j <= M; j++) {
    const r = F.r[i], q = F.q[j], D = r[0] * q[1] - r[1] * q[0];
    if (Math.abs(D) <= 1e-12 * (Math.hypot(r[0], r[1]) * Math.hypot(q[0], q[1]) + 1e-300)) continue;
    const x00 = h[i][0] + v[j][0], y00 = h[i][1] + v[j][1], dx = cx - x00, dy = cy - y00;
    const t = (dx * q[1] - dy * q[0]) / D, u = (r[0] * dy - r[1] * dx) / D, g = 1e-3;
    if ((i > 0 && t < -g) || (i < L && t > 1 + g) || (j > 0 && u < -g) || (j < M && u > 1 + g)) continue;
    const s = D >= 0 ? 1 : -1;
    const x10 = h[i + 1][0] + v[j][0], y10 = h[i + 1][1] + v[j][1], x01 = h[i][0] + v[j + 1][0], y01 = h[i][1] + v[j + 1][1], x11 = h[i + 1][0] + v[j + 1][0], y11 = h[i + 1][1] + v[j + 1][1];
    if (i > 0 && s * ori(x00, y00, x01, y01, cx, cy) > 0) continue;
    if (i < L && s * ori(x10, y10, x11, y11, cx, cy) < 0) continue;
    if (j > 0 && s * ori(x00, y00, x10, y10, cx, cy) < 0) continue;
    if (j < M && s * ori(x01, y01, x11, y11, cx, cy) > 0) continue;
    out.add(i + ',' + j);
  }
  return out;
}
const sum = rows => rows.reduce((s, r) => s + r.orient, 0);
function mulberry32(a) { return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
function nudge(x, k) { if (k === 0) return x; if (x === 0) return k * 5e-324; const b = new DataView(new ArrayBuffer(8)); b.setFloat64(0, x); let n = b.getBigInt64(0); n += (x >= 0 ? 1n : -1n) * BigInt(k); b.setBigInt64(0, n); return b.getFloat64(0); }

function run(rig, rawRig) {
  const F = rig.F; if (F.w === null) return null;
  const o = new Map(); for (const c of F.cells) if (!c.degenerate) o.set(c.i + ',' + c.j, c.orient);
  const withTruth = rig.rows(F, [0, 0]).length === 0 || rig.rows(F, [0, 0])[0].cell !== undefined;
  const C = () => ({ n: 0, law: 0, odd: 0, fal: 0, lost: 0, miss: 0, rawlaw: 0 });
  const R = { fold: C(), plain: C(), node: C(), rand: C(), lattReg: C(), lattCrit: C() };
  function probe(A, cnt, near, dOdd) {
    cnt.n++;
    const kept = rig.rows(F, A); if (sum(kept) !== F.w) cnt.law++;
    const raw = rawRig.rows(rawRig.F, A); if (sum(raw) !== F.w) cnt.rawlaw++;
    if (near && dOdd) { const k = raw.filter(x => Math.abs(x.lh - near[0]) < 1 && Math.abs(x.lv - near[1]) < 1).length; if (k % 2) cnt.odd++; }
    if (withTruth) { const rep = new Set(raw.map(x => x.cell)), tr = truthCells(F, A); for (const c of rep) if (!tr.has(c)) cnt.fal++; for (const c of tr) if (!rep.has(c)) cnt.lost++; }
    return kept;
  }
  const P = (i, j) => [F.h[i][0] + F.v[j][0] - F.o[0], F.h[i][1] + F.v[j][1] - F.o[1]];
  const dF = []; for (let e = -40; e <= -12; e++) dF.push(Math.pow(10, e / 4));
  const dP = []; for (let e = -12; e <= -4; e++) dP.push(Math.pow(10, e));
  for (const kind of ['v', 'h']) for (let i = 0; i < F.nh - (kind === 'v' ? 1 : 0); i++) for (let j = 0; j < F.nv - (kind === 'h' ? 1 : 0); j++) {
    const a = o.get(i + ',' + j), b = kind === 'v' ? o.get((i + 1) + ',' + j) : o.get(i + ',' + (j + 1)); if (!a || !b) continue;
    const fold = a !== b;
    const Pm = kind === 'v' ? [F.h[i + 1][0] + (F.v[j][0] + F.v[j + 1][0]) / 2 - F.o[0], F.h[i + 1][1] + (F.v[j][1] + F.v[j + 1][1]) / 2 - F.o[1]]
      : [(F.h[i][0] + F.h[i + 1][0]) / 2 + F.v[j + 1][0] - F.o[0], (F.h[i][1] + F.h[i + 1][1]) / 2 + F.v[j + 1][1] - F.o[1]];
    const e = kind === 'v' ? F.q[j] : F.r[i], n = Math.hypot(e[0], e[1]), nx = -e[1] / n, ny = e[0] / n;
    const sh = kind === 'v' ? F.hc[i + 1] - F.ha : (F.hc[i] + F.hc[i + 1]) / 2 - F.ha, sv = kind === 'v' ? (F.vc[j] + F.vc[j + 1]) / 2 - F.va : F.vc[j + 1] - F.va;
    for (const side of [1, -1]) for (const d of (fold ? dF : dP)) probe([Pm[0] + side * d * nx, Pm[1] + side * d * ny], fold ? R.fold : R.plain, [sh, sv], fold && d <= 3e-4);
    if (rig.hv) {      // lattice: the edge midpoint as an exact hv image, +-1 ulp
      const A0 = rig.hv(F, sh, sv);
      for (const kx of [-1, 0, 1]) for (const ky of [-1, 0, 1]) { const Lc = fold ? R.lattCrit : R.lattReg; const kept = probe([nudge(A0[0], kx), nudge(A0[1], ky)], Lc); if (!kept.some(r => Math.abs(r.lh - sh) < 1e-6 && Math.abs(r.lv - sv) < 1e-6)) Lc.miss++; }
    }
  }
  const dN = []; for (let e = -24; e <= -8; e++) dN.push(Math.pow(10, e / 2));
  for (let i = 1; i < F.nh; i++) for (let j = 1; j < F.nv; j++) {
    const C0 = P(i, j);
    for (let k = 0; k < 8; k++) { const a = 2 * Math.PI * (k + 0.37) / 8; for (const d of dN) probe([C0[0] + d * Math.cos(a), C0[1] + d * Math.sin(a)], R.node); }
    if (rig.hv) { const os = new Set([o.get((i - 1) + ',' + (j - 1)), o.get(i + ',' + (j - 1)), o.get(i + ',' + j), o.get((i - 1) + ',' + j)]); const Lc = os.size === 1 && !os.has(undefined) ? R.lattReg : R.lattCrit; const sh = F.hc[i] - F.ha, sv = F.vc[j] - F.va, A0 = rig.hv(F, sh, sv); for (const kx of [-1, 0, 1]) for (const ky of [-1, 0, 1]) { const kept = probe([nudge(A0[0], kx), nudge(A0[1], ky)], Lc); if (!kept.some(r => Math.abs(r.lh - sh) < 1e-6 && Math.abs(r.lv - sv) < 1e-6)) Lc.miss++; } }
  }
  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  for (const hp of F.h) for (const vp of F.v) { const x = hp[0] + vp[0] - F.o[0], y = hp[1] + vp[1] - F.o[1]; x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); }
  const rnd = mulberry32(5), m = 0.1 * Math.max(x1 - x0, y1 - y0);
  for (let k = 0; k < 5000; k++) probe([x0 - m + (x1 - x0 + 2 * m) * rnd(), y0 - m + (y1 - y0 + 2 * m) * rnd()], R.rand);
  return { R, withTruth };
}

const T = { law: 0, odd: 0, fal: 0, lost: 0, rawlaw: 0, lattRegMiss: 0, lattRegLaw: 0, lattCritMiss: 0 };
for (const page of pages) {
  const rigs = page.load(page.cut.src), raws = page.load(page.cut.rawSrc);
  console.log('\n=== ' + page.file);
  rigs.forEach((rig, k) => {
    const out = run(rig, raws[k]); if (!out) { console.log(rig.key + ': w undefined, skipped'); return; }
    const f = (c, name) => `${name} ${c.n}: law ${c.law}` + (name === 'fold' ? `, odd ${c.odd}` : '') + (out.withTruth ? `, false ${c.fal}, lost ${c.lost}` : '') + (name.startsWith('latt') ? `, root missed ${c.miss}` : '') + `, raw-sum!=w ${c.rawlaw}`;
    console.log(`${rig.key} (w=${rig.F.w}): ` + ['fold', 'plain', 'node', 'rand', 'lattReg', 'lattCrit'].filter(n => out.R[n].n).map(n => f(out.R[n], n)).join(' | '));
    for (const n of ['fold', 'plain', 'node', 'rand']) { T.law += out.R[n].law; T.fal += out.R[n].fal; T.lost += out.R[n].lost; T.rawlaw += out.R[n].rawlaw; } T.odd += out.R.fold.odd; T.lattRegMiss += out.R.lattReg.miss; T.lattRegLaw += out.R.lattReg.law; T.lattCritMiss += out.R.lattCrit.miss;
  });
}
console.log(`\nTOTAL (fold+plain+node+rand): law ${T.law} (unmerged raw: ${T.rawlaw}), odd ${T.odd}, false ${T.fal}, lost ${T.lost}; lattice regular: root missed ${T.lattRegMiss}, law ${T.lattRegLaw}; lattice on creases/cusps: root missed ${T.lattCritMiss}`);
