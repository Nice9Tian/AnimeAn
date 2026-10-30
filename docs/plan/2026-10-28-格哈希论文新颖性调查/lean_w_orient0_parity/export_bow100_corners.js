// Export the corner classification data of the bow100 rig (document §8) as the Lean module WOrient0/Bow100Corners.lean:
// the turn signs sign(r_i × r_{i+1}) and sign(q_j × q_{j+1}) of the two guides at their nodes, plus the class counts
// and the list of cusp corners that the sign rule gives, for Lean to re-derive from Bow100.cells with `decide`.
// Run from this folder: node export_bow100_corners.js WOrient0/Bow100Corners.lean
'use strict';
const fs = require('fs');
const html = fs.readFileSync(__dirname + '/../w_orient0_explainer.html', 'utf8').split('\n');
const start = html.findIndex(l => l.includes('const EPS = 1e-7, DEDUP = 1e-4, N = 24;'));
const end = html.findIndex((l, k) => k > start && l.includes('drawing helpers'));
if (start < 0 || end < 0) throw new Error('geometry markers not found in w_orient0_explainer.html');
const src = html.slice(start, end).join('\n');
const { rigs, build } = new Function(src + '\nreturn { rigs, build };')();
const [h, v] = rigs.bow100.make(); const F = build(h, v, 0);
const n = F.nh, m = F.nv;
const cross = (a, b) => a[0] * b[1] - a[1] * b[0];
const sgn = x => (x > 0 ? 1 : x < 0 ? -1 : 0);
const rr = [], qq = [];
for (let i = 0; i + 1 < n; i++) rr.push(sgn(cross(F.r[i], F.r[i + 1])));
for (let j = 0; j + 1 < m; j++) qq.push(sgn(cross(F.q[j], F.q[j + 1])));
// a zero turn (three collinear nodes) is harmless: there the cells on both sides of the node have the same orient, so no
// one-odd pattern consults that entry; checked here and again in Lean (straight_nodes_consistent)
for (let i = 0; i + 1 < n; i++) if (rr[i] === 0) for (let j = 0; j < m; j++) if (F.omap[i][j] !== F.omap[i + 1][j]) throw new Error('straight H node ' + (i + 1) + ' with an orient change');
for (let j = 0; j + 1 < m; j++) if (qq[j] === 0) for (let i = 0; i < n; i++) if (F.omap[i][j] !== F.omap[i][j + 1]) throw new Error('straight V node ' + (j + 1) + ' with an orient change');
console.error('rr', JSON.stringify(rr)); console.error('qq', JSON.stringify(qq));
// the same classification as corner_census.js, by signs only
function classify(i, j) {
  const sw = F.omap[i][j], se = F.omap[i + 1][j], ne = F.omap[i + 1][j + 1], nw = F.omap[i][j + 1], r = rr[i], q = qq[j];
  if (sw === se && se === ne && ne === nw) return 'homeo';
  if (sw === ne && se === nw) return 'double';
  if (se === ne && ne === nw) return se * q > 0 ? 'cusp' : 'foldCorner';
  if (sw === ne && ne === nw) return sw * r < 0 ? 'cusp' : 'foldCorner';
  if (sw === se && se === nw) return sw * q < 0 ? 'cusp' : 'foldCorner';
  if (sw === se && se === ne) return sw * r > 0 ? 'cusp' : 'foldCorner';
  return 'foldStraight';
}
const counts = { homeo: 0, foldStraight: 0, foldCorner: 0, cusp: 0, double: 0 }, cusps = [];
for (let i = 0; i + 1 < n; i++) for (let j = 0; j + 1 < m; j++) { const c = classify(i, j); counts[c]++; if (c === 'cusp') cusps.push([i, j]); }
console.error('counts', JSON.stringify(counts), 'cusps', cusps.length);
const lit = l => '[' + l.join(', ') + ']';
const out = `import WOrient0.Bow100

/-!
# 弯 100 px 装置的格角分类（由 w_orient0_explainer.html 的几何代码导出）

文档第 8 节：面内 ${n}×${m} 个小格有 ${(n - 1) * (m - 1)} 个格角，每个格角由周围四格的旋向符号和两条导引线在该节点的
转向符号分成五类（homeo、折缝直穿、折缝转角、尖点、双折缝）。旋向来自 \`Bow100.cells\`；两条导引线在节点处的转向符号
sign(r_i × r_{i+1})、sign(q_j × q_{j+1}) 由页面代码算出，是这里的数据。Lean 用 \`decide\` 核对：按符号判据分类后各类的个数、
尖点格角的清单、以及**没有双折缝格角**（朝向配对加守恒律的唯一盲区在这副轴线上不出现）。
「符号判据等于缠绕数」是第 8 节的命题，\`corner_census.js\` 在页面七套装置上逐格角核对过，不在 Lean 里。
-/

namespace WOrient0.Bow100

/-- sign(r_i × r_{i+1})，i = 0..${n - 2}：H 在节点 i + 1 处的转向。 -/
def rr : List Int := ${lit(rr)}
/-- sign(q_j × q_{j+1})，j = 0..${m - 2}：V 在节点 j + 1 处的转向。 -/
def qq : List Int := ${lit(qq)}

/-- 面内小格 (i, j) 的旋向，按编号从 cells 里查出（i、j ∈ 0..${n - 1}；查不到给 0，核对里不会发生）。 -/
def orientAt (i j : Nat) : Int :=
  match cells.find? (fun c => c.id == (i + 1) * ${m + 2} + (j + 1)) with
  | some c => c.orient
  | none => 0

inductive CornerClass where
  | homeo | foldStraight | foldCorner | cusp | double
  deriving DecidableEq, Repr

/-- 格角 C = (s_{i+1}, t_{j+1}) 的分类（第 8 节的符号判据）。四格 SW=(i,j)、SE=(i+1,j)、NE=(i+1,j+1)、NW=(i,j+1)。
只有一格与其余三格旋向不同时，σ 为其余三格的旋向：奇格 SW 时 σ·qq_j > 0、SE 时 σ·rr_i < 0、NE 时 σ·qq_j < 0、
NW 时 σ·rr_i > 0 是尖点（|W| = 1），否则是折缝转角（W = 0）。 -/
def classify (i j : Nat) : CornerClass :=
  let sw := orientAt i j
  let se := orientAt (i + 1) j
  let ne := orientAt (i + 1) (j + 1)
  let nw := orientAt i (j + 1)
  let r := rr.getD i 0
  let q := qq.getD j 0
  if sw = se ∧ se = ne ∧ ne = nw then .homeo
  else if sw = ne ∧ se = nw then .double
  else if se = ne ∧ ne = nw then (if se * q > 0 then .cusp else .foldCorner)
  else if sw = ne ∧ ne = nw then (if sw * r < 0 then .cusp else .foldCorner)
  else if sw = se ∧ se = nw then (if sw * q < 0 then .cusp else .foldCorner)
  else if sw = se ∧ se = ne then (if sw * r > 0 then .cusp else .foldCorner)
  else .foldStraight

/-- 全部 ${(n - 1) * (m - 1)} 个格角，(i, j) ∈ 0..${n - 2} × 0..${m - 2}。 -/
def corners : List (Nat × Nat) := (List.range ${n - 1}).flatMap fun i => (List.range ${m - 1}).map fun j => (i, j)
def classes : List CornerClass := corners.map fun ij => classify ij.1 ij.2
def cuspCorners : List (Nat × Nat) := corners.filter fun ij => classify ij.1 ij.2 = .cusp

/-- 五类的个数：homeo、折缝直穿、折缝转角、尖点、双折缝。 -/
theorem class_counts :
    (classes.count .homeo, classes.count .foldStraight, classes.count .foldCorner,
     classes.count .cusp, classes.count .double) = (${counts.homeo}, ${counts.foldStraight}, ${counts.foldCorner}, ${counts.cusp}, ${counts.double}) := by
  decide +kernel

/-- 没有双折缝格角：守恒律唯一看不见的那类盲区在这副轴线上不出现。 -/
theorem no_double_crease : ∀ ij ∈ corners, classify ij.1 ij.2 ≠ .double := by
  decide +kernel

/-- 尖点格角（朝向配对会在格角像周围半径约 DEDUP 的楔形里错合一对同向真根，守恒律能发现）。 -/
theorem cusp_corners_list : cuspCorners = [${cusps.map(c => `(${c[0]}, ${c[1]})`).join(', ')}] := by
  decide +kernel

/-- 导引线在某节点恰好直行（转向符号 0，正弦折线的拐点）时，该节点两侧的小格旋向逐列相同，
所以不会出现需要查那个转向符号的模式；符号判据只在转向非零处被用到。 -/
theorem straight_nodes_consistent :
    (List.range ${n - 1}).all (fun i => rr.getD i 0 != 0 || (List.range ${m}).all (fun j => orientAt i j == orientAt (i + 1) j)) = true ∧
    (List.range ${m - 1}).all (fun j => qq.getD j 0 != 0 || (List.range ${n}).all (fun i => orientAt i j == orientAt i (j + 1))) = true := by
  decide +kernel

end WOrient0.Bow100
`;
fs.writeFileSync(process.argv[2], out, 'utf8');
