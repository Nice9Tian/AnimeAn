// Census of the preimages of probe points along the route of fig. E of the explainer:
// inner front/back, extension front/back, f - b, for one rig and one choice of the crossing O.
// Run from this folder: node route_census_bow100.js [rig=bow100] [crossingIndex=0] [steps=40]
const fs = require('fs');
// The page's pure geometry (rigs, build, crossings, solve), cut out of the explainer between two markers.
const html = fs.readFileSync(__dirname + '/../w_orient0_explainer.html', 'utf8').split('\n');
const start = html.findIndex(l => l.includes('const EPS = 1e-7, DEDUP = 1e-4, N = 24;'));
const end = html.findIndex((l, k) => k > start && l.includes('drawing helpers'));
if (start < 0 || end < 0) throw new Error('geometry markers not found in w_orient0_explainer.html');
const src = html.slice(start, end).join('\n');
const { rigs, build, solve } = new Function(src + '\nreturn { rigs, build, solve };')();
const census = (F, A) => {
  const s = solve(F, A); let fIn = 0, bIn = 0, fExt = 0, bExt = 0;
  for (const r of s.rows) { const fr = r.orient === F.orient0; if (r.onSheet) { if (fr) fIn++; else bIn++; } else { if (fr) fExt++; else bExt++; } }
  return { fIn, bIn, fExt, bExt, f: s.f, b: s.b, sum: s.sum };
};
const key = process.argv[2] || 'bow100', which = +(process.argv[3] || 0), n = +(process.argv[4] || 40);
const [h, v] = rigs[key].make(); const F = build(h, v, which);
console.log(key, 'O=', F.o.map(x => +x.toFixed(2)), 'orient0=', F.orient0, 'w=', F.w, 'w*orient0=', F.w * F.orient0,
  'route', JSON.stringify(rigs[key].path), 'sampled at', n + 1, 'points');
const [p0, p1] = rigs[key].path;
for (let k = 0; k <= n; k++) {
  const A = [p0[0] + (p1[0] - p0[0]) * k / n, p0[1] + (p1[1] - p0[1]) * k / n]; const c = census(F, A);
  console.log(A.map(x => +x.toFixed(1)).join(','), JSON.stringify(c), 'f-b=', c.f - c.b);
}
