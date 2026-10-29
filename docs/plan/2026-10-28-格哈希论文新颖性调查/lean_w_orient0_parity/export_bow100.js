// Export the bow100 rig (all 676 cells of §4 of the page, incl. extension cells) and the
// preimage lists of two probes as the Lean data module WOrient0/Bow100.lean.
// Run from this folder: node export_bow100.js WOrient0/Bow100.lean
const fs = require('fs');
// The page's pure geometry (rigs, build, crossings, solve), cut out of the explainer between two markers.
const html = fs.readFileSync(__dirname + '/../w_orient0_explainer.html', 'utf8').split('\n');
const start = html.findIndex(l => l.includes('const EPS = 1e-7, DEDUP = 1e-4, N = 24;'));
const end = html.findIndex((l, k) => k > start && l.includes('drawing helpers'));
if (start < 0 || end < 0) throw new Error('geometry markers not found in w_orient0_explainer.html');
const src = html.slice(start, end).join('\n');
const { rigs, build, solve } = new Function(src + '\nreturn { rigs, build, solve };')();
const [h, v] = rigs.bow100.make(); const F = build(h, v, 0);
const n = F.nh, m = F.nv, L = F.hc[n], M = F.vc[m];
const clamp = (k, hi) => Math.max(0, Math.min(hi, k));
const id = (ii, jj) => (ii + 1) * (m + 2) + (jj + 1);
const cells = [];
for (let ii = -1; ii <= n; ii++) for (let jj = -1; jj <= m; jj++) {
  const o = F.omap[clamp(ii, n - 1)][clamp(jj, m - 1)];
  if (!o) throw new Error('degenerate ' + ii + ',' + jj);
  cells.push({ id: id(ii, jj), orient: o, inner: ii >= 0 && ii < n && jj >= 0 && jj < m });
}
const hitsOf = A => solve(F, A).rows.map(r => {
  const sh = r.lh + F.ha, sv = r.lv + F.va;
  const ii = sh < -1e-9 ? -1 : sh > L + 1e-9 ? n : r.i, jj = sv < -1e-9 ? -1 : sv > M + 1e-9 ? m : r.j;
  return { id: id(ii, jj), ii, jj, orient: r.orient, onSheet: r.onSheet };
});
const A1 = [0, 80], A2 = [60, 80], H1 = hitsOf(A1), H2 = hitsOf(A2);
console.error('cells', cells.length, 'orient0', F.orient0, 'w', F.w, 'O', F.o);
console.error('A1', JSON.stringify(H1));
console.error('A2', JSON.stringify(H2));
const lit = c => `⟨${c.id}, ${c.orient}, ${c.inner}⟩`;
const rows = []; for (let k = 0; k < cells.length; k += 13) rows.push('  ' + cells.slice(k, k + 13).map(lit).join(', '));
const out = `import WOrient0.Parity

/-!
# 弯 100 px 装置的全部小格与两个探针（由 w_orient0_explainer.html 的几何代码导出）

装置：H、V 都是幅 100 px 的正弦折线（24 段），O 取沿 H 的第 1 个交点 (${F.o.map(x => x.toFixed(1)).join(', ')})，
orient0 = ${F.orient0}，w = ${F.w}。小格编号 id = (i + 1)·${m + 2} + (j + 1)，i、j ∈ −1..${n}，
其中 −1 是首端外延、${n} 是末端外延；外延小格的 orient 与贴着的面内小格相同。
探针 A₁ = (${A1.join(', ')}) 点中 ${H1.length} 个小格，A₂ = (${A2.join(', ')}) 点中 ${H2.length} 个。
Lean 核对的是：这些数据满足守恒律 Σ orient = w，以及由此得到的计数。
「这些小格确实是 A₁、A₂ 的原像」是几何事实，由页面代码算出，不在 Lean 里。
-/

namespace WOrient0.Bow100

def cells : List Cell := [
${rows.join(',\n')}
]

def rig : Rig where
  cells := cells
  orient0 := ${F.orient0}
  w := ${F.w}
  orient_pm := by decide +kernel
  orient0_pm := by decide +kernel
  w_range := by decide +kernel

/-- A₁ = (${A1.join(', ')}) 点中的小格编号。 -/
def hits1 : List Nat := [${H1.map(r => r.id).join(', ')}]
/-- A₂ = (${A2.join(', ')}) 点中的小格编号。 -/
def hits2 : List Nat := [${H2.map(r => r.id).join(', ')}]

def probe1 : Probe rig := ⟨fun c => hits1.contains c.id, by decide +kernel⟩
def probe2 : Probe rig := ⟨fun c => hits2.contains c.id, by decide +kernel⟩

theorem census1 : (probe1.fIn, probe1.bIn, probe1.fExt, probe1.bExt) = (1, 0, 0, 2) := by decide +kernel
theorem census2 : (probe2.fIn, probe2.bIn, probe2.fExt, probe2.bExt) = (2, 1, 0, 2) := by decide +kernel

/-- 同一副轴线、同样的外延计数（0 正 2 背），面内正面数 1 与 2 一奇一偶。 -/
theorem fIn_parity_not_determined :
    probe1.fExt = probe2.fExt ∧ probe1.bExt = probe2.bExt ∧ probe1.fIn % 2 ≠ probe2.fIn % 2 := by
  decide +kernel

/-- 含外延的 (f, b, n)：A₁ 是 (1, 2, 3)，A₂ 是 (2, 3, 5)；f − b 都是 −1 = w·orient0。 -/
theorem census1_total : (probe1.f, probe1.b, probe1.n) = (1, 2, 3) := by decide +kernel
theorem census2_total : (probe2.f, probe2.b, probe2.n) = (2, 3, 5) := by decide +kernel
/-- 含外延的正面数 f 也一奇一偶。 -/
theorem f_parity_not_determined : probe1.f % 2 ≠ probe2.f % 2 := by decide +kernel

/-- 面内原像个数的奇偶与命题 P2 一致：1 ≡ −1 + 2，3 ≡ −1 + 2 (mod 2)。 -/
example : (probe1.nIn : Int) % 2 = (rig.w * rig.orient0 + (probe1.nExt : Int)) % 2 :=
  probe1.inner_parity
example : (probe2.nIn : Int) % 2 = (rig.w * rig.orient0 + (probe2.nExt : Int)) % 2 :=
  probe2.inner_parity

/-- 小格表的编码良构：编号恰是 0..675 无重无漏，inner 标志与行列范围一致（i = id / 26，j = id % 26，
面内是 1 ≤ i ≤ 24 且 1 ≤ j ≤ 24）。 -/
theorem cells_grid_wf :
    cells.map Cell.id = List.range 676 ∧
    cells.all (fun c =>
      let i := c.id / 26
      let j := c.id % 26
      c.inner == (0 < i && i < 25 && 0 < j && j < 25)) = true := by
  decide +kernel

end WOrient0.Bow100
`;
fs.writeFileSync(process.argv[2], out, 'utf8');
