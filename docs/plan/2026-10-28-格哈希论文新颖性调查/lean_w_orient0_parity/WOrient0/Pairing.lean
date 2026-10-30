import WOrient0.Parity

/-!
# 朝向配对判据的代数核心（文档第 8 节）

两个解靠得很近时：同向 ⟹ 同一个根被相邻两格各找一次（合并）；反向 ⟹ 跨折缝的两个真根（保留）。
这里形式化的是它背后的两条代数引理，用整数坐标写（论证逐字适用于任何有序域，文档第 8 节说明），
再加一条「守恒律当校验和能发现少一个根」的清单引理。几何部分（格角分类、缠绕数）留在文档里，
弯 100 px 的格角分类数据在 `Bow100Corners.lean` 里用 `decide` 核对。
-/

namespace WOrient0

/-- 平面整向量。 -/
structure Vec where
  x : Int
  y : Int
  deriving Repr, DecidableEq

/-- 叉积 a × b。小格 (i, j) 的旋向就是 sign(r_i × q_j)。 -/
def cross (a b : Vec) : Int := a.x * b.y - a.y * b.x

/-- 二维的三向量恒等式 a·(b×c) + b·(c×a) + c·(a×b) = 0，逐坐标。 -/
theorem triple_identity_x (a b c : Vec) :
    a.x * cross b c + b.x * cross c a + c.x * cross a b = 0 := by
  unfold cross
  simp only [Int.mul_sub, Int.mul_comm, Int.mul_left_comm]
  omega

theorem triple_identity_y (a b c : Vec) :
    a.y * cross b c + b.y * cross c a + c.y * cross a b = 0 := by
  unfold cross
  simp only [Int.mul_sub, Int.mul_comm, Int.mul_left_comm]
  omega

/-- 公共边的消元：两格的像点 r₁·t + q·u 与 r₂·t' + q·u' 相等时，叉乘 q 消去 u、u'，
得 (r₁×q)·t = (r₂×q)·t'。 -/
theorem cross_eliminate (r₁ r₂ q : Vec) (t u t' u' : Int)
    (hx : r₁.x * t + q.x * u = r₂.x * t' + q.x * u')
    (hy : r₁.y * t + q.y * u = r₂.y * t' + q.y * u') :
    cross r₁ q * t = cross r₂ q * t' := by
  unfold cross
  have h1 := congrArg (· * q.y) hx
  have h2 := congrArg (· * q.x) hy
  simp only [Int.mul_sub, Int.mul_add, Int.mul_comm, Int.mul_left_comm] at h1 h2 ⊢
  omega

/-- 引理 A（同向公共边 ⟹ 单射）。相邻两格 L、R 共一条边，像分别是 C + r₁·t + q·u（t ≤ 0，L 在公共边的一侧）
和 C + r₂·t' + q·u'（t' ≥ 0，R 在另一侧）。两格同向（r₁×q 与 r₂×q 同号、都不为零）时，两格的公共像点只能是
公共边上同一个参数点：t = t' = 0 且 u = u'。所以一个正则点在同向相邻两格里各被找到一次，只能是同一个根。 -/
theorem same_sign_shared_edge (r₁ r₂ q : Vec) (hq : q.x ≠ 0 ∨ q.y ≠ 0)
    (hsame : (0 < cross r₁ q ∧ 0 < cross r₂ q) ∨ (cross r₁ q < 0 ∧ cross r₂ q < 0))
    (t u t' u' : Int) (ht : t ≤ 0) (ht' : 0 ≤ t')
    (hx : r₁.x * t + q.x * u = r₂.x * t' + q.x * u')
    (hy : r₁.y * t + q.y * u = r₂.y * t' + q.y * u') :
    t = 0 ∧ t' = 0 ∧ u = u' := by
  have key := cross_eliminate r₁ r₂ q t u t' u' hx hy
  have ht0 : t = 0 ∧ t' = 0 := by
    rcases hsame with ⟨h₁, h₂⟩ | ⟨h₁, h₂⟩
    · have a : cross r₁ q * t ≤ 0 := Int.mul_nonpos_of_nonneg_of_nonpos (Int.le_of_lt h₁) ht
      have b : 0 ≤ cross r₂ q * t' := Int.mul_nonneg (Int.le_of_lt h₂) ht'
      have e1 : cross r₁ q * t = 0 := by omega
      have e2 : cross r₂ q * t' = 0 := by omega
      exact ⟨(Int.mul_eq_zero.mp e1).resolve_left (Int.ne_of_gt h₁),
             (Int.mul_eq_zero.mp e2).resolve_left (Int.ne_of_gt h₂)⟩
    · have a : 0 ≤ cross r₁ q * t := Int.mul_nonneg_of_nonpos_of_nonpos (Int.le_of_lt h₁) ht
      have b : cross r₂ q * t' ≤ 0 := by
        rw [Int.mul_comm]; exact Int.mul_nonpos_of_nonneg_of_nonpos ht' (Int.le_of_lt h₂)
      have e1 : cross r₁ q * t = 0 := by omega
      have e2 : cross r₂ q * t' = 0 := by omega
      exact ⟨(Int.mul_eq_zero.mp e1).resolve_left (Int.ne_of_lt h₁),
             (Int.mul_eq_zero.mp e2).resolve_left (Int.ne_of_lt h₂)⟩
  obtain ⟨rfl, rfl⟩ := ht0
  refine ⟨rfl, rfl, ?_⟩
  simp only [Int.mul_zero, Int.zero_add] at hx hy
  rcases hq with hqx | hqy
  · exact Int.eq_of_mul_eq_mul_left hqx hx
  · exact Int.eq_of_mul_eq_mul_left hqy hy

/-- 引理 B（反向公共边 ⟹ 像叠合）的见证：取 t = r₂×q、t' = r₁×q、u = 0、u' = −(r₁×r₂)，
两格的像点 r₁·t + q·u 与 r₂·t' + q·u' 重合。两格反向（r₁×q 与 r₂×q 异号）时 t 与 t' 异号，
即这个公共像点在两格里都严格在公共边之外、各在自己那一侧——两格的像在公共边像的同一侧叠合，
落在那里的正则点有两个旋向相反的原像。实数情形把 (t, u, t', u') 同乘一个与 r₁×q 同号的小实数，就在两格内部（t < 0 < t'）
得到任意靠近公共边的公共像点；这一步要实数，不在这里。 -/
theorem opposite_sign_shared_edge (r₁ r₂ q : Vec) :
    r₁.x * cross r₂ q + q.x * 0 = r₂.x * cross r₁ q + q.x * (-(cross r₁ r₂)) ∧
    r₁.y * cross r₂ q + q.y * 0 = r₂.y * cross r₁ q + q.y * (-(cross r₁ r₂)) := by
  have hx := triple_identity_x r₁ r₂ q
  have hy := triple_identity_y r₁ r₂ q
  -- cross q r₁ = −cross r₁ q
  have cx : cross q r₁ = -cross r₁ q := by unfold cross; rw [Int.mul_comm q.x, Int.mul_comm q.y]; omega
  rw [cx] at hx hy
  simp only [Int.mul_neg, Int.mul_zero] at hx hy ⊢
  constructor <;> omega

/-- 反向时见证的两个参数确实异号（t = r₂×q < 0 < t' = r₁×q 或反之）。 -/
theorem opposite_sign_witness_sides (r₁ r₂ q : Vec) (h : cross r₁ q * cross r₂ q < 0) :
    (cross r₂ q < 0 ∧ 0 < cross r₁ q) ∨ (0 < cross r₂ q ∧ cross r₁ q < 0) := by
  rcases Int.lt_trichotomy (cross r₁ q) 0 with h₁ | h₁ | h₁
  · rcases Int.lt_trichotomy (cross r₂ q) 0 with h₂ | h₂ | h₂
    · exact absurd h (Int.not_lt.mpr (Int.le_of_lt (Int.mul_pos_of_neg_of_neg h₁ h₂)))
    · rw [h₂, Int.mul_zero] at h; exact absurd h (Int.lt_irrefl 0)
    · exact Or.inr ⟨h₂, h₁⟩
  · rw [h₁, Int.zero_mul] at h; exact absurd h (Int.lt_irrefl 0)
  · rcases Int.lt_trichotomy (cross r₂ q) 0 with h₂ | h₂ | h₂
    · exact Or.inl ⟨h₂, h₁⟩
    · rw [h₂, Int.mul_zero] at h; exact absurd h (Int.lt_irrefl 0)
    · exact absurd h (Int.not_lt.mpr (Int.le_of_lt (Int.mul_pos h₁ h₂)))

/-! ## 守恒律当校验和 -/

/-- 去掉清单的第 k 项，和恰好少了那一项。 -/
theorem sum_eraseIdx_add (l : List Int) : ∀ (k : Nat) (hk : k < l.length),
    (l.eraseIdx k).sum + l[k] = l.sum := by
  induction l with
  | nil => intro k hk; simp at hk
  | cons a l ih =>
    intro k hk
    cases k with
    | zero => simp only [List.eraseIdx_cons_zero, List.sum_cons, List.getElem_cons_zero]; omega
    | succ k =>
      have hk' : k < l.length := by simpa using hk
      simp only [List.eraseIdx_cons_succ, List.sum_cons, List.getElem_cons_succ]
      have := ih k hk'
      omega

/-- 从一张 ±1 的原像清单里少算一个根，Σ orient 一定变：守恒律能发现任何单个错误，
特别是朝向配对在尖点楔形里把两个同向真根错合成一个。 -/
theorem sum_eraseIdx_ne (l : List Int) (h : ∀ x ∈ l, x = 1 ∨ x = -1) (k : Nat) (hk : k < l.length) :
    (l.eraseIdx k).sum ≠ l.sum := by
  have hs := sum_eraseIdx_add l k hk
  have hm : l[k] ∈ l := List.getElem_mem hk
  rcases h _ hm with h1 | h1 <;> omega

/-- 反例：少算一正一背（双折缝格角里错合两对）时 Σ orient 不变，守恒律看不见。 -/
example : ([1, -1, 1, -1] : List Int).sum = ([1, -1] : List Int).sum := by decide

end WOrient0
