/-!
# w·orient0 与原像个数的奇偶

只用 Lean 4 核心库，不依赖 Mathlib。对应文档
`../2026-10-30-w_orient0奇偶校验命题与Lean证明.md`，记号沿用说明页 `w_orient0_explainer.html`。

## 模型

参数平面被划成有限多个**小格**：画出的面内的小格（`inner = true`），加上外延的边格和角格
（`inner = false`）。每个小格在画布上的像是一个平行四边形（外延的是无限的条带或锥），
旋向 `orient ∈ {+1, −1}`。一个**正则点** A 在每个小格里最多有一个原像，所以 A 的原像清单
就是「哪些小格被点中」（`Probe.hit`）。

**守恒律**（说明页第 4 节，Brouwer 度）：Σ_{被点中的小格} orient = w。它在这里是假设
`Probe.law`，不在 Lean 里证明。Lean 证明的是它的算术推论，以及一个反例。
-/

namespace WOrient0

/-- 一个小格：编号、旋向、是否在画出的面内。 -/
structure Cell where
  id : Nat
  orient : Int
  inner : Bool
  deriving Repr, DecidableEq

/-- 一副轴线：全部小格（含外延）、O 处的旋向 orient0、端向环的缠绕数 w。 -/
structure Rig where
  cells : List Cell
  orient0 : Int
  w : Int
  /-- 每个小格都不退化。 -/
  orient_pm : ∀ c ∈ cells, c.orient = 1 ∨ c.orient = -1
  /-- orient0 是 O 所在小格的旋向。 -/
  orient0_pm : orient0 = 1 ∨ orient0 = -1
  /-- 说明页第 3 节：环的四步每步不到 180°，所以 w 只能是 −1、0、+1。 -/
  w_range : w = -1 ∨ w = 0 ∨ w = 1

/-- 一个正则点 A 的原像清单，连同守恒律。 -/
structure Probe (R : Rig) where
  hit : Cell → Bool
  /-- 守恒律 Σ orient = w（假设）。 -/
  law : ((R.cells.filter hit).map Cell.orient).sum = R.w

variable {R : Rig}

/-- 被点中、且满足 p 的小格个数。 -/
def Probe.count (P : Probe R) (p : Cell → Bool) : Nat :=
  (R.cells.filter (fun c => P.hit c && p c)).length

/-- 正面原像数（含外延）：orient = orient0。 -/
def Probe.f (P : Probe R) : Nat := P.count (fun c => c.orient == R.orient0)
/-- 背面原像数（含外延）：orient = −orient0。 -/
def Probe.b (P : Probe R) : Nat := P.count (fun c => c.orient == -R.orient0)
/-- 面内的正面原像数。 -/
def Probe.fIn (P : Probe R) : Nat := P.count (fun c => c.inner && c.orient == R.orient0)
/-- 面内的背面原像数。 -/
def Probe.bIn (P : Probe R) : Nat := P.count (fun c => c.inner && c.orient == -R.orient0)
/-- 外延上的正面原像数。 -/
def Probe.fExt (P : Probe R) : Nat := P.count (fun c => !c.inner && c.orient == R.orient0)
/-- 外延上的背面原像数。 -/
def Probe.bExt (P : Probe R) : Nat := P.count (fun c => !c.inner && c.orient == -R.orient0)

/-- 原像总数，不看正背。 -/
def Probe.n (P : Probe R) : Nat := P.count (fun _ => true)
/-- 面内原像总数，不看正背。 -/
def Probe.nIn (P : Probe R) : Nat := P.count Cell.inner
/-- 外延原像总数，不看正背。 -/
def Probe.nExt (P : Probe R) : Nat := P.count (fun c => !c.inner)

/-! ## 两个计数引理 -/

/-- 被点中的小格的 orient 之和 = orient0 · (f − b)。对小格表归纳。 -/
theorem sum_orient_eq (o0 : Int) (ho : o0 = 1 ∨ o0 = -1) (hit : Cell → Bool) :
    ∀ l : List Cell, (∀ c ∈ l, c.orient = 1 ∨ c.orient = -1) →
      ((l.filter hit).map Cell.orient).sum
        = o0 * (((l.filter (fun c => hit c && c.orient == o0)).length : Int)
                - ((l.filter (fun c => hit c && c.orient == -o0)).length : Int))
  | [], _ => by simp
  | c :: l, h => by
    have ih := sum_orient_eq o0 ho hit l (fun d hd => h d (List.mem_cons_of_mem c hd))
    have hc : c.orient = 1 ∨ c.orient = -1 := h c (List.mem_cons_self ..)
    rcases ho with rfl | rfl <;> rcases hc with hc | hc <;>
      cases hh : hit c <;> simp [hh, hc] at ih ⊢ <;> omega

/-- 按「满足 q / 不满足 q」把一次过滤计数拆成两份。对小格表归纳。 -/
theorem length_filter_split (hit p q : Cell → Bool) :
    ∀ l : List Cell,
      (l.filter (fun c => hit c && p c)).length
        = (l.filter (fun c => hit c && (q c && p c))).length
          + (l.filter (fun c => hit c && (!q c && p c))).length
  | [] => by simp
  | c :: l => by
    have ih := length_filter_split hit p q l
    cases hh : hit c <;> cases hq : q c <;> cases hp : p c <;> simp [hh, hq, hp] <;> omega

/-- 按「在面内 / 在外延」把计数拆成两份。 -/
theorem Probe.count_split (P : Probe R) (p q : Cell → Bool) :
    P.count p = P.count (fun c => q c && p c) + P.count (fun c => !q c && p c) :=
  length_filter_split P.hit p q R.cells

theorem Probe.f_split (P : Probe R) : P.f = P.fIn + P.fExt := P.count_split _ Cell.inner
theorem Probe.b_split (P : Probe R) : P.b = P.bIn + P.bExt := P.count_split _ Cell.inner

/-- 小格都不退化时，被点中的小格非正即背：不看正背的计数 = 正面数 + 背面数。对小格表归纳。 -/
theorem length_filter_split_pm (o0 : Int) (ho : o0 = 1 ∨ o0 = -1) (hit p : Cell → Bool) :
    ∀ l : List Cell, (∀ c ∈ l, c.orient = 1 ∨ c.orient = -1) →
      (l.filter (fun c => hit c && p c)).length
        = (l.filter (fun c => hit c && (p c && c.orient == o0))).length
          + (l.filter (fun c => hit c && (p c && c.orient == -o0))).length
  | [], _ => by simp
  | c :: l, h => by
    have ih := length_filter_split_pm o0 ho hit p l (fun d hd => h d (List.mem_cons_of_mem c hd))
    have hc : c.orient = 1 ∨ c.orient = -1 := h c (List.mem_cons_self ..)
    rcases ho with rfl | rfl <;> rcases hc with hc | hc <;>
      cases hh : hit c <;> cases hp : p c <;> simp [hh, hp, hc] at ih ⊢ <;> omega

theorem Probe.n_eq (P : Probe R) : P.n = P.f + P.b :=
  length_filter_split_pm R.orient0 R.orient0_pm P.hit (fun _ => true) R.cells R.orient_pm
theorem Probe.nIn_eq (P : Probe R) : P.nIn = P.fIn + P.bIn :=
  length_filter_split_pm R.orient0 R.orient0_pm P.hit Cell.inner R.cells R.orient_pm
theorem Probe.nExt_eq (P : Probe R) : P.nExt = P.fExt + P.bExt :=
  length_filter_split_pm R.orient0 R.orient0_pm P.hit (fun c => !c.inner) R.cells R.orient_pm

/-! ## 守恒律的推论 -/

/-- 说明页第 5 节：f − b = w·orient0。 -/
theorem Probe.f_sub_b (P : Probe R) : (P.f : Int) - P.b = R.w * R.orient0 := by
  have h := sum_orient_eq R.orient0 R.orient0_pm P.hit R.cells R.orient_pm
  rw [P.law] at h
  unfold Probe.f Probe.b Probe.count
  rcases R.orient0_pm with ho | ho <;> rw [ho] at h ⊢ <;> simp at h ⊢ <;> omega

/-- 命题 P3：面内的带符号原像数由 w·orient0 和外延上的正背计数决定。 -/
theorem Probe.inner_signed (P : Probe R) :
    (P.fIn : Int) - P.bIn = R.w * R.orient0 - ((P.fExt : Int) - P.bExt) := by
  have h := P.f_sub_b
  have hf := P.f_split
  have hb := P.b_split
  omega

/-- 命题 P1：原像总数（含外延，不看正背）与 w·orient0 同奇偶。 -/
theorem Probe.total_parity (P : Probe R) : (P.n : Int) % 2 = (R.w * R.orient0) % 2 := by
  have h := P.f_sub_b
  have hn := P.n_eq
  omega

/-- 命题 P1′：原像总数是奇数 ⇔ w ≠ 0。 -/
theorem Probe.total_odd_iff (P : Probe R) : P.n % 2 = 1 ↔ R.w ≠ 0 := by
  have h := P.f_sub_b
  have hn := P.n_eq
  rcases R.w_range with hw | hw | hw <;> rcases R.orient0_pm with ho | ho <;>
    simp [hw, ho] at h ⊢ <;> omega

/-- 命题 P2：面内原像个数的奇偶 = w·orient0 加外延原像**个数**的奇偶；两边都不看正背。 -/
theorem Probe.inner_parity (P : Probe R) :
    (P.nIn : Int) % 2 = (R.w * R.orient0 + (P.nExt : Int)) % 2 := by
  have h := P.inner_signed
  have h1 := P.nIn_eq
  have h2 := P.nExt_eq
  omega

/-- 命题 P3′：知道面内原像个数 nIn（不看正背）和外延的正背计数后，面内正面数被精确定出：
2·fIn = nIn + (w·orient0 − (fExt − bExt))。 -/
theorem Probe.fIn_recover (P : Probe R) :
    2 * (P.fIn : Int) = (P.nIn : Int) + (R.w * R.orient0 - ((P.fExt : Int) - P.bExt)) := by
  have h := P.inner_signed
  have h1 := P.nIn_eq
  omega

theorem Probe.bIn_recover (P : Probe R) :
    2 * (P.bIn : Int) = (P.nIn : Int) - (R.w * R.orient0 - ((P.fExt : Int) - P.bExt)) := by
  have h := P.inner_signed
  have h1 := P.nIn_eq
  omega

/-- 全局的对应式：2·f = n + w·orient0，知道原像个数（含外延）就定出正面数。 -/
theorem Probe.f_recover (P : Probe R) : 2 * (P.f : Int) = (P.n : Int) + R.w * R.orient0 := by
  have h := P.f_sub_b
  have hn := P.n_eq
  omega

/-- 只看外延就能下的判断：w·orient0 − (fExt − bExt) > 0 时，面内一定有正面原像。 -/
theorem Probe.fIn_pos_of_pos (P : Probe R)
    (h : 0 < R.w * R.orient0 - ((P.fExt : Int) - P.bExt)) : 0 < P.fIn := by
  have h1 := P.inner_signed
  omega

/-! ## 反例：面内正面数的奇偶**不**由 w·orient0 和外延计数决定

弯 100 px 装置（O 取第 1 个交点，orient0 = +1，w = −1）上，探针 A₁ = (0, 80) 的原像是
面内 1 正、外延 2 背；A₂ = (60, 80) 的原像是面内 2 正 1 背、外延 2 背（数字由说明页的
几何代码算出，见文档）。两者 w·orient0、外延计数全同，fIn 却一奇一偶。下面是只保留
相关五个小格的缩影；带全部 676 个小格的版本在 `Bow100.lean`。
-/

def toyRig : Rig where
  cells := [⟨0, 1, true⟩, ⟨1, 1, true⟩, ⟨2, -1, true⟩, ⟨3, -1, false⟩, ⟨4, -1, false⟩]
  orient0 := 1
  w := -1
  orient_pm := by decide
  orient0_pm := by decide
  w_range := by decide

/-- A₁ = (0, 80)：点中小格 0（面内正）、3、4（外延背）。 -/
def toyProbe1 : Probe toyRig := ⟨fun c => c.id == 0 || c.id == 3 || c.id == 4, by decide⟩
/-- A₂ = (60, 80)：五个小格全点中。 -/
def toyProbe2 : Probe toyRig := ⟨fun _ => true, by decide⟩

theorem toy_census :
    (toyProbe1.fIn, toyProbe1.bIn, toyProbe1.fExt, toyProbe1.bExt) = (1, 0, 0, 2) ∧
    (toyProbe2.fIn, toyProbe2.bIn, toyProbe2.fExt, toyProbe2.bExt) = (2, 1, 0, 2) := by decide

/-- 命题 P4（否定形式）：同一副轴线、同样的外延计数，面内正面数可以一奇一偶。 -/
theorem fIn_parity_not_determined :
    ∃ (R : Rig) (P₁ P₂ : Probe R),
      P₁.fExt = P₂.fExt ∧ P₁.bExt = P₂.bExt ∧ P₁.fIn % 2 ≠ P₂.fIn % 2 :=
  ⟨toyRig, toyProbe1, toyProbe2, by decide⟩

/-- 命题 P1b（否定形式）：含外延的正面数 f 的奇偶也不由 w·orient0 决定（同一副轴线上 1 与 2）。 -/
theorem f_parity_not_determined :
    ∃ (R : Rig) (P₁ P₂ : Probe R), P₁.f % 2 ≠ P₂.f % 2 :=
  ⟨toyRig, toyProbe1, toyProbe2, by decide⟩

end WOrient0
