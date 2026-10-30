// Acceptance check of the orientation-paired dedup in the three explainer pages (document §8.7), run on each page's
// OWN solve(), cut out of the HTML, with the page's own DEDUP:
//   w_orient0_explainer.html   (DEDUP 1e-4, 24 segments)
//   fold_severing_map.html     (DEDUP 1e-6, 24 segments)
//   fold_inverse_explainer.html (DEDUP 1e-6, 20 segments by default; 'all' mode = every crossing)
// For every rig of each page: next to every fold edge on the sheet (two side-by-side cells of opposite orient, vertical
// and horizontal pairs, probed at the edge midpoint), on both
// sides, at distances from 1e-10 to 1e-3 px, and at uniform random probes, the kept rows must satisfy the
// conservation law sum(orient) = w. The same probes are also run through the page's previous rule (distance only,
// last kept row only, restored by text substitution) to show what the change fixes.
// One failure is not the dedup's: EPS bounds the segment PARAMETER, so the acceptance band of a cell edge has a
// different width on the canvas for the two cells beside it, and a point between the two widths, on the side where
// neither cell really covers it, is accepted by one cell only - an odd number of reports next to the edge, one false
// root, the law off by one whatever the dedup does. Such probes are counted apart ("one-sided EPS band"), found from
// the unmerged reports (DEDUP := 0); every other probe must keep the law.
// Run from this folder: node page_dedup_check.js [randomProbes=5000]
'use strict';
const fs = require('fs');
const NEW_LOOP_START = '    const kept = []; for (const r of rows) { let dup = false;';
const OLD_LOOP = '    const kept = []; for (const r of rows) { const p = kept[kept.length - 1]; if (p && Math.abs(r.lh - p.lh) < DEDUP && Math.abs(r.lv - p.lv) < DEDUP) continue; kept.push(r); }';

function cut(file, startMarker, endMarker) {
  const lines = fs.readFileSync(__dirname + '/../' + file, 'utf8').split('\n');
  const a = lines.findIndex(l => l.includes(startMarker));
  const b = lines.findIndex((l, k) => k > a && l.includes(endMarker));
  if (a < 0 || b < 0) throw new Error('markers not found in ' + file);
  const src = lines.slice(a, b).join('\n');
  const k = src.split('\n').findIndex(l => l.startsWith(NEW_LOOP_START));
  if (k < 0) throw new Error('the orientation-paired dedup loop is not in ' + file);
  const oldSrc = src.split('\n').map((l, i) => (i === k ? OLD_LOOP : l)).join('\n');
  const rawSrc = src.replace(/DEDUP = 1e-[46]/, 'DEDUP = 0');
  if (rawSrc === src) throw new Error('could not switch off DEDUP in ' + file);
  return { src, oldSrc, rawSrc };
}

// each page: how to build its rigs and how to get the kept rows of one point
const pages = [
  {
    file: 'w_orient0_explainer.html', cut: cut('w_orient0_explainer.html', 'const EPS = 1e-7, DEDUP = 1e-4, N = 24;', 'drawing helpers'),
    load: src => {
      const { rigs, build, solve } = new Function(src + '\nreturn { rigs, build, solve };')();
      return Object.keys(rigs).map(key => { const [h, v] = rigs[key].make(); return { key, F: build(h, v, 0), rows: (F, A) => solve(F, A).rows }; });
    },
  },
  {
    file: 'fold_severing_map.html', cut: cut('fold_severing_map.html', 'const EPS = 1e-7, DEDUP = 1e-6;', 'const $ = id =>'),
    load: src => {
      const { rigs, build, solve } = new Function(src + '\nreturn { rigs, build, solve };')();
      return Object.keys(rigs).map(key => { const [h, v] = rigs[key].make(); return { key, F: build(h, v), rows: (F, A) => solve(F, A, true).rows }; });
    },
  },
  {
    file: 'fold_inverse_explainer.html', cut: cut('fold_inverse_explainer.html', '// ------------------------------------------------------------ geometry', '// ------------------------------------------------------------ state & ui'),
    load: src => {
      const g = new Function(src + '\nreturn { sinGuide, hookRig, hairpin, buildFrame, solve };')();
      const segs = 20, rigs = { straight: [0, 0], bow30: [30, 30], bow60: [60, 60], bow100: [100, 100] };
      const out = Object.keys(rigs).map(key => ({ key, F: g.buildFrame(g.sinGuide(rigs[key][0], 'h', segs), g.sinGuide(rigs[key][1], 'v', segs)) }));
      out.push({ key: 'hairpin', F: g.buildFrame(...g.hairpin(segs)) }, { key: 'hook', F: g.buildFrame(...g.hookRig()) });
      for (const o of out) o.rows = (F, A) => g.solve(F, A, 'all').rows;
      return out;
    },
  },
];

function mulberry32(a) { return () => { a |= 0; a = a + 0x6D2B79F5 | 0; let t = Math.imul(a ^ a >>> 15, 1 | a); t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t; return ((t ^ t >>> 14) >>> 0) / 4294967296; }; }
const sum = rows => rows.reduce((s, r) => s + r.orient, 0);

function check(rig, oldRig, rawRig, nRandom) {
  const F = rig.F;
  if (F.w === null) return { skipped: 'w undefined' };
  const orient = new Map(); for (const c of F.cells) orient.set(c.i + ',' + c.j, c.orient);
  const res = { foldEdges: 0, probes: 0, band: 0, fail: 0, oldFail: 0, randomFail: 0, randomOldFail: 0 };
  const deltas = []; for (let e = -40; e <= -12; e++) deltas.push(Math.pow(10, e / 4));   // 1e-10 .. 1e-3 px
  // every fold edge on the sheet, probed at its midpoint: vertical (i, j) | (i+1, j) and horizontal (i, j) | (i, j+1)
  const edges = [];
  for (let i = 0; i + 1 < F.nh; i++) for (let j = 0; j < F.nv; j++) edges.push(['v', i, j, orient.get(i + ',' + j), orient.get((i + 1) + ',' + j)]);
  for (let i = 0; i < F.nh; i++) for (let j = 0; j + 1 < F.nv; j++) edges.push(['h', i, j, orient.get(i + ',' + j), orient.get(i + ',' + (j + 1))]);
  for (const [kind, i, j, a, b] of edges) {
    if (!a || !b || a === b) continue;
    res.foldEdges++;
    const P = kind === 'v'
      ? [F.h[i + 1][0] + (F.v[j][0] + F.v[j + 1][0]) / 2 - F.o[0], F.h[i + 1][1] + (F.v[j][1] + F.v[j + 1][1]) / 2 - F.o[1]]
      : [(F.h[i][0] + F.h[i + 1][0]) / 2 + F.v[j + 1][0] - F.o[0], (F.h[i][1] + F.h[i + 1][1]) / 2 + F.v[j + 1][1] - F.o[1]];
    const e = kind === 'v' ? F.q[j] : F.r[i], n = Math.hypot(e[0], e[1]), nx = -e[1] / n, ny = e[0] / n;
    const sh = kind === 'v' ? F.hc[i + 1] - F.ha : (F.hc[i] + F.hc[i + 1]) / 2 - F.ha;   // the edge midpoint in arcs from O
    const sv = kind === 'v' ? (F.vc[j] + F.vc[j + 1]) / 2 - F.va : F.vc[j + 1] - F.va;
    for (const side of [1, -1]) for (const d of deltas) {
      const A = [P[0] + side * d * nx, P[1] + side * d * ny];
      const near = rawRig.rows(rawRig.F, A).filter(x => Math.abs(x.lh - sh) < 1 && Math.abs(x.lv - sv) < 1).length;
      if (near % 2 === 1) { res.band++; continue; }
      res.probes++;
      if (sum(rig.rows(F, A)) !== F.w) res.fail++;
      if (sum(oldRig.rows(oldRig.F, A)) !== F.w) res.oldFail++;
    }
  }
  let x0 = Infinity, x1 = -Infinity, y0 = Infinity, y1 = -Infinity;
  for (const hp of F.h) for (const vp of F.v) { const x = hp[0] + vp[0] - F.o[0], y = hp[1] + vp[1] - F.o[1]; x0 = Math.min(x0, x); x1 = Math.max(x1, x); y0 = Math.min(y0, y); y1 = Math.max(y1, y); }
  const rnd = mulberry32(5), m = 0.1 * Math.max(x1 - x0, y1 - y0);
  for (let k = 0; k < nRandom; k++) {
    const A = [x0 - m + (x1 - x0 + 2 * m) * rnd(), y0 - m + (y1 - y0 + 2 * m) * rnd()];
    if (sum(rig.rows(F, A)) !== F.w) res.randomFail++;
    if (sum(oldRig.rows(oldRig.F, A)) !== F.w) res.randomOldFail++;
  }
  return res;
}

const nRandom = +(process.argv[2] || 5000);
let failures = 0;
for (const page of pages) {
  const rigs = page.load(page.cut.src), oldRigs = page.load(page.cut.oldSrc), rawRigs = page.load(page.cut.rawSrc);
  console.log('\n=== ' + page.file);
  rigs.forEach((rig, k) => {
    const r = check(rig, oldRigs[k], rawRigs[k], nRandom);
    if (r.skipped) { console.log(`${rig.key}: skipped (${r.skipped})`); return; }
    failures += r.fail + r.randomFail;
    console.log(`${rig.key}: w=${rig.F.w}; ${r.foldEdges} fold edges, ${r.probes} probes next to them (+${r.band} on a one-sided EPS band, not counted): law fails ${r.fail} (previous rule ${r.oldFail}); ` +
      `${nRandom} random probes: law fails ${r.randomFail} (previous rule ${r.randomOldFail})`);
  });
}
console.log(failures ? `\nFAILED: ${failures} probes break the conservation law` : '\npage_dedup_check: ok');
process.exit(failures ? 1 : 0);
