// Corner census for the orientation-pairing criterion (document §8), run on the explainer's own geometry.
//
// Every interior grid corner C = (s_{i+1}, t_{j+1}) has four cells around it: SW=(i,j), SE=(i+1,j), NE=(i+1,j+1),
// NW=(i,j+1). The images of the four half-edges leaving C are −r_i (W), −q_j (S), r_{i+1} (E), q_{j+1} (N); their
// winding W around the origin equals (#front − #back) of the roots next to C. Classes:
//   homeo          all four orient equal                   → at most 1 root near C (duplicates of one point only)
//   fold-straight  two adjacent cells odd                  → 0 or 2 roots, opposite orient
//   fold-corner    one odd cell, W = 0                     → 0 or 2 roots, opposite orient
//   cusp           one odd cell, |W| = 1                   → 1 or 3 roots; the 3 contain two of the SAME orient
//   double         two diagonal cells odd                  → 0, 2 or 4 roots; the 4 are two same-orient pairs
// The sign rule for |W| (odd SW: σ·(q_j×q_{j+1}) > 0, SE: σ·(r_i×r_{i+1}) < 0, NE: σ·(q_j×q_{j+1}) < 0,
// NW: σ·(r_i×r_{i+1}) > 0, σ = orient of the three others) is checked against the atan2 winding at every corner.
//
// Stress probes: for every one-odd corner the probe is put on the bisector of the odd cell's image sector at a
// distance δ from the corner image, δ chosen inside the window where (a) only the cells that really cover the probe
// report a root (δ above the EPS acceptance band) and (b) the roots next to the corner lie within DEDUP of each
// other in (lh, lv) — the blind zone of the pairing rule. Then the raw rows of the page's solve() go through two
// dedup policies and the conservation law Σ orient = w is checked for each:
//   page     merge a row that has an already kept row within DEDUP, whatever the orient (the explainer's rule,
//            but not restricted to consecutive rows)
//   pairing  merge only when that kept row has the same orient
// More probes: the corner image itself at homeo corners (4 duplicates), midpoints of shared edges (2 duplicates on
// a plain edge, a fold pair on a fold edge), and uniform random probes over the bbox.
//
// Run from this folder: node corner_census.js [rig=all] [crossingIndex=0] [randomProbes=20000] [seed=1] [out.json]
'use strict';
const fs = require('fs');
const html = fs.readFileSync(__dirname + '/../w_orient0_explainer.html', 'utf8').split('\n');
const start = html.findIndex(l => l.includes('const EPS = 1e-7, DEDUP = 1e-4, N = 24;'));
const end = html.findIndex((l, k) => k > start && l.includes('drawing helpers'));
if (start < 0 || end < 0) throw new Error('geometry markers not found in w_orient0_explainer.html');
const src0 = html.slice(start, end).join('\n');
// DEDUP := 0 switches the page's own dedup off, so solve() returns the raw rows (sorted by lh, then lv).
const src = src0.replace('DEDUP = 1e-4', 'DEDUP = 0');
if (src === src0) throw new Error('could not switch off DEDUP');
const DEDUP = 1e-4;
const { rigs, build, solve } = new Function(src + '\nreturn { rigs, build, solve };')();

const cross = (a, b) => a[0] * b[1] - a[1] * b[0];
const sgn = x => (x > 0 ? 1 : x < 0 ? -1 : 0);
const neg = a => [-a[0], -a[1]];
const unit = a => { const n = Math.hypot(a[0], a[1]); return [a[0] / n, a[1] / n]; };
const angle = (a, b) => Math.atan2(cross(a, b), a[0] * b[0] + a[1] * b[1]);
const near = (p, q) => Math.abs(p.lh - q.lh) < DEDUP && Math.abs(p.lv - q.lv) < DEDUP;

function dedup(rows, policy) {
  const kept = [];
  for (const r of rows) if (!kept.some(p => near(p, r) && (policy === 'page' || p.orient === r.orient))) kept.push(r);
  return kept;
}
const sum = rows => rows.reduce((s, r) => s + r.orient, 0);

function cornerCensus(F) {
  const out = { homeo: 0, 'fold-straight': 0, 'fold-corner': 0, cusp: 0, double: 0, corners: 0, ruleMismatch: 0, signMismatch: 0, oneOdd: [], doubles: [] };
  for (let i = 0; i + 1 < F.nh; i++) for (let j = 0; j + 1 < F.nv; j++) {
    const o = [F.omap[i][j], F.omap[i + 1][j], F.omap[i + 1][j + 1], F.omap[i][j + 1]]; // SW SE NE NW
    if (o.some(x => !x)) continue;
    out.corners++;
    const dirs = [neg(F.r[i]), neg(F.q[j]), F.r[i + 1], F.q[j + 1]]; // W S E N
    // the signed angle of each cell = angle between its two half-edge images; its sign must be the cell's orient
    let tot = 0;
    for (let k = 0; k < 4; k++) { const a = angle(dirs[k], dirs[(k + 1) % 4]); if (sgn(a) !== o[k]) out.signMismatch++; tot += a; }
    const W = Math.round(tot / (2 * Math.PI));
    const odd = o.map((x, k) => x !== o[(k + 1) % 4] && x !== o[(k + 3) % 4] ? k : -1).filter(k => k >= 0);
    let cls;
    if (o.every(x => x === o[0])) { cls = 'homeo'; if (W !== o[0]) out.ruleMismatch++; }
    else if (odd.length === 1) {
      const k = odd[0], s = -o[k];
      const rr = sgn(cross(F.r[i], F.r[i + 1])), qq = sgn(cross(F.q[j], F.q[j + 1]));
      const predicted = k === 0 ? s * qq > 0 : k === 1 ? s * rr < 0 : k === 2 ? s * qq < 0 : s * rr > 0;
      if (predicted !== (Math.abs(W) === 1)) out.ruleMismatch++;
      cls = Math.abs(W) === 1 ? 'cusp' : 'fold-corner';
      out.oneOdd.push({ i, j, k, cls, odd: ['SW', 'SE', 'NE', 'NW'][k], W, wedgeDeg: +(Math.abs(angle(dirs[k], dirs[(k + 1) % 4])) * 180 / Math.PI).toFixed(3) });
    } else if (odd.length === 4) { cls = 'double'; out.doubles.push({ i, j, o }); if (W !== 0) out.ruleMismatch++; } // +−+−: every cell differs from both neighbours
    else { cls = 'fold-straight'; if (W !== 0) out.ruleMismatch++; } // two adjacent odd cells
    out[cls]++;
  }
  return out;
}

// canvas image of the parameter point (absolute arclengths sh, sv)
function imageOf(F, sh, sv) {
  const pt = (pts, c, arc) => { let i = 0; while (i < pts.length - 2 && c[i + 1] <= arc) i++; const sp = c[i + 1] - c[i]; const a = sp > 0 ? (arc - c[i]) / sp : 0; return [pts[i][0] + (pts[i + 1][0] - pts[i][0]) * a, pts[i][1] + (pts[i + 1][1] - pts[i][1]) * a]; };
  const ph = pt(F.h, F.hc, sh), pv = pt(F.v, F.vc, sv);
  return [ph[0] + pv[0] - F.o[0], ph[1] + pv[1] - F.o[1]];
}

function probeReport(F, A, cornerArc) {
  const raw = solve(F, A).rows;
  const page = dedup(raw, 'page'), pair = dedup(raw, 'pairing');
  const rep = { raw: raw.length, page: page.length, pairing: pair.length, lawPage: sum(page) === F.w, lawPairing: sum(pair) === F.w };
  if (cornerArc) {
    // rows next to the corner (within half a cell in arc px), their orients, and their spread
    const nearRows = raw.filter(r => Math.abs(r.lh + F.ha - cornerArc[0]) < 1 && Math.abs(r.lv + F.va - cornerArc[1]) < 1);
    let spread = 0; for (const a of nearRows) for (const b of nearRows) spread = Math.max(spread, Math.abs(a.lh - b.lh), Math.abs(a.lv - b.lv));
    rep.near = nearRows.length; rep.nearOrients = nearRows.map(r => r.orient); rep.spread = spread;
  }
  return rep;
}

// probe on the bisector of the odd cell's image sector, distance δ from the corner image
function sectorProbe(F, i, j, k, δ) {
  const dirs = [neg(F.r[i]), neg(F.q[j]), F.r[i + 1], F.q[j + 1]];
  const a = unit(dirs[k]), b = unit(dirs[(k + 1) % 4]); const bis = unit([a[0] + b[0], a[1] + b[1]]);
  const C = imageOf(F, F.hc[i + 1], F.vc[j + 1]);
  return [C[0] + δ * bis[0], C[1] + δ * bis[1]];
}

// the δ window for one one-odd corner: scan δ geometrically; "inside" = expected number of roots next to the corner
// (3 for a cusp, 2 for a fold corner) and all of them within DEDUP; "outside" = expected number but spread > DEDUP
function oneOddStress(F, c) {
  const expected = c.cls === 'cusp' ? 3 : 2, arc = [F.hc[c.i + 1], F.vc[c.j + 1]];
  let inside = null, outside = null, lo = null, hi = null;
  for (let e = -10; e <= -1; e += 0.125) {
    const δ = Math.pow(10, e), rep = probeReport(F, sectorProbe(F, c.i, c.j, c.k, δ), arc);
    if (rep.near !== expected) continue;
    if (rep.spread < DEDUP) { if (lo === null) lo = δ; hi = δ; inside = { δ, ...rep }; }
    else if (!outside) outside = { δ, ...rep };
  }
  return { ...c, window: lo === null ? null : [lo, hi], inside, outside };
}

function stress(F, census) {
  const res = { cusp: [], foldCorner: [], homeoCorner: [], edge: [] };
  for (const c of census.oneOdd) (c.cls === 'cusp' ? res.cusp : res.foldCorner).push(oneOddStress(F, c));
  for (let i = 0; i + 1 < F.nh; i++) for (let j = 0; j + 1 < F.nv; j++) {
    const o = [F.omap[i][j], F.omap[i + 1][j], F.omap[i + 1][j + 1], F.omap[i][j + 1]];
    if (o.some(x => !x) || !o.every(x => x === o[0])) continue;
    res.homeoCorner.push({ i, j, ...probeReport(F, imageOf(F, F.hc[i + 1], F.vc[j + 1]), [F.hc[i + 1], F.vc[j + 1]]) });
  }
  // midpoints of shared edges, both fold and non-fold: vertical edges (i|i+1 in row j) and horizontal edges (j|j+1 in column i).
  // These points lie exactly on an edge image, so they are boundary (non-regular) points: the probes test what the code does there.
  for (let i = 0; i + 1 < F.nh; i++) for (let j = 0; j < F.nv; j++) {
    if (!F.omap[i][j] || !F.omap[i + 1][j]) continue;
    res.edge.push({ i, j, dir: 'v', fold: F.omap[i][j] !== F.omap[i + 1][j], ...probeReport(F, imageOf(F, F.hc[i + 1], (F.vc[j] + F.vc[j + 1]) / 2)) });
  }
  for (let i = 0; i < F.nh; i++) for (let j = 0; j + 1 < F.nv; j++) {
    if (!F.omap[i][j] || !F.omap[i][j + 1]) continue;
    res.edge.push({ i, j, dir: 'h', fold: F.omap[i][j] !== F.omap[i][j + 1], ...probeReport(F, imageOf(F, (F.hc[i] + F.hc[i + 1]) / 2, F.vc[j + 1])) });
  }
  return res;
}

const cnt = (list, f) => list.filter(f).length;
function summarizeOneOdd(list, label) {
  const n = list.length; if (!n) return `${label}: none`;
  const withWin = list.filter(x => x.window);
  const ins = withWin.map(x => x.inside), outs = list.filter(x => x.outside).map(x => x.outside);
  const orientPattern = rows => rows.map(r => r.nearOrients.slice().sort().join('')).filter((v, i, a) => a.indexOf(v) === i).join(' | ');
  return `${label}: ${n} corners, δ window found at ${withWin.length}` +
    (withWin.length ? ` (δ from ${Math.min(...withWin.map(x => x.window[0])).toExponential(1)} to ${Math.max(...withWin.map(x => x.window[1])).toExponential(1)} px)` : '') +
    `\n   inside the window (roots next to the corner within DEDUP): near-root orients ${orientPattern(ins)}; ` +
    `page keeps law ${cnt(ins, x => x.lawPage)}/${ins.length}, pairing keeps law ${cnt(ins, x => x.lawPairing)}/${ins.length}; ` +
    `page kept ${[...new Set(ins.map(x => x.raw - x.page))].sort().join('/')} fewer rows than raw, pairing ${[...new Set(ins.map(x => x.raw - x.pairing))].sort().join('/')} fewer` +
    `\n   outside the window (same roots, spread > DEDUP): ${outs.length} probes; page keeps law ${cnt(outs, x => x.lawPage)}/${outs.length}, pairing ${cnt(outs, x => x.lawPairing)}/${outs.length}; policies differ at ${cnt(outs, x => x.page !== x.pairing)}`;
}
function summarize(list, label) {
  const n = list.length; if (!n) return `${label}: none`;
  return `${label}: ${n} probes; raw rows ${[...new Set(list.map(x => x.raw))].sort((a, b) => a - b).join('/')}` +
    (list[0].near !== undefined ? `, rows next to the corner ${[...new Set(list.map(x => x.near))].sort().join('/')}` : '') +
    `; page keeps law ${cnt(list, x => x.lawPage)}/${n}, pairing keeps law ${cnt(list, x => x.lawPairing)}/${n}; policies differ at ${cnt(list, x => x.page !== x.pairing)}`;
}

function mulberry32(a) { return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }

function randomProbes(F, n, seed) {
  const rnd = mulberry32(seed), [x0, y0, x1, y1] = F.bbox, m = 0.1 * Math.max(x1 - x0, y1 - y0);
  const r = { n, dupPairs: 0, foldPairsWithinDedup: 0, minFoldPairDist: Infinity, lawPageFail: 0, lawPairingFail: 0, differ: 0 };
  for (let k = 0; k < n; k++) {
    const A = [x0 - m + (x1 - x0 + 2 * m) * rnd(), y0 - m + (y1 - y0 + 2 * m) * rnd()];
    const raw = solve(F, A).rows;
    for (let a = 0; a < raw.length; a++) for (let b = a + 1; b < raw.length; b++) {
      const p = raw[a], q = raw[b], d = Math.max(Math.abs(p.lh - q.lh), Math.abs(p.lv - q.lv));
      if (p.orient === q.orient) { if (d < DEDUP) r.dupPairs++; }
      else { r.minFoldPairDist = Math.min(r.minFoldPairDist, d); if (d < DEDUP) r.foldPairsWithinDedup++; }
    }
    const page = dedup(raw, 'page'), pair = dedup(raw, 'pairing');
    if (sum(page) !== F.w) r.lawPageFail++;
    if (sum(pair) !== F.w) r.lawPairingFail++;
    if (page.length !== pair.length) r.differ++;
  }
  return r;
}

const want = process.argv[2] || 'all', which = +(process.argv[3] || 0), nRand = +(process.argv[4] || 20000), seed = +(process.argv[5] || 1);
const names = want === 'all' ? Object.keys(rigs) : [want];
const report = {};
for (const key of names) {
  const [h, v] = rigs[key].make(); const F = build(h, v, which);
  console.log(`\n=== ${key} (${rigs[key].name}) O=(${F.o.map(x => x.toFixed(1)).join(', ')}) orient0=${F.orient0} w=${F.w} cells ${F.nh}x${F.nv}`);
  const c = cornerCensus(F);
  console.log(`corners ${c.corners}: homeo ${c.homeo}, fold-straight ${c['fold-straight']}, fold-corner ${c['fold-corner']}, cusp ${c.cusp}, double ${c.double}; ` +
    `sign-rule mismatches ${c.ruleMismatch}, orient/angle sign mismatches ${c.signMismatch}`);
  const cusps = c.oneOdd.filter(x => x.cls === 'cusp');
  if (cusps.length) console.log('cusp corners (i, j, odd cell, W, wedge°):', cusps.map(x => `(${x.i},${x.j},${x.odd},${x.W},${x.wedgeDeg})`).join(' '));
  if (c.doubles.length) console.log('double-crease corners:', JSON.stringify(c.doubles));
  const s = stress(F, c);
  console.log(summarizeOneOdd(s.cusp, 'cusp corners'));
  console.log(summarizeOneOdd(s.foldCorner, 'fold corners'));
  console.log(summarize(s.homeoCorner, 'homeo corner-image probes'));
  console.log(summarize(s.edge.filter(e => !e.fold), 'non-fold shared-edge midpoints (vertical + horizontal edges)'));
  console.log(summarize(s.edge.filter(e => e.fold), 'fold shared-edge midpoints (vertical + horizontal edges)'));
  report[key] = { census: { ...c, oneOdd: undefined }, cusps: s.cusp, foldCorners: s.foldCorner, homeoCorner: s.homeoCorner.length, edges: s.edge.length };
  if (F.w !== null && nRand > 0) {
    const r = randomProbes(F, nRand, seed);
    console.log(`random probes ${r.n}: same-orient pairs within DEDUP ${r.dupPairs}, opposite-orient pairs within DEDUP ${r.foldPairsWithinDedup}, ` +
      `closest opposite-orient pair ${r.minFoldPairDist === Infinity ? 'none' : r.minFoldPairDist.toExponential(2)} px; ` +
      `law fails: page ${r.lawPageFail}, pairing ${r.lawPairingFail}; policies differ ${r.differ}`);
    report[key].random = r;
  }
}
if (process.argv[6]) fs.writeFileSync(process.argv[6], JSON.stringify(report, null, 1));
