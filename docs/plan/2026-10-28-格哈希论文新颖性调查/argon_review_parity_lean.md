# Argon 审查：Lean 命题陈述部分

2026-10-30，Gemini 4 Argon（high），只读模式（`agy-run.ps1 -Sandbox off`），读了文档、Parity.lean、Bow100.lean、说明页第 2～5 节正文及几何代码、路线计数；STATUS: SUCCESS  (turns=1, duration=335.4s, tokens=68839)。审查对象是修改前的版本：文档里的 doc.md 即本目录的 `2026-10-30-w_orient0奇偶校验命题与Lean证明.md`，审查时的行号对应修改前的文本。采纳情况见该文档第 7 节。原文如下，未改动。

---
### 1. [`Rig`] 与 [`Probe`] 模型的忠实度、假设强弱与 `law` 的定位

1. **记号忠实度**：
   - **小格与外延**：[`Cell`] 用 `inner : Bool` 将参数平面划分为面内（`true`）与外延（`false`）。由于 [doc.md] 中的命题 P1～P4 只区分“面内”与“外延”而不区分边格与角格，将边格和角格合并为 `inner = false` 完全忠实于算术推论所需的信息。
   - **旋向**：`orient_pm`（$\text{orient} \in \{+1, -1\}$）、`orient0_pm`（$\text{orient0} \in \{+1, -1\}$）、`w_range`（$w \in \{-1, 0, +1\}$）准确对应 [doc.md] 第 1 节与 [page_sections_2_to_5.txt] 第 3 节。
   - **正则点每格最多一个原像**：由于非退化小格上的映射是仿射双射，正则点不在小格边界上，因此在每个小格内至多有 1 个原像。用谓词 `hit : Cell → Bool` 配合 `R.cells.filter` 计数（假设 `R.cells` 中小格不重复）忠实编码了“每格 0 或 1 个原像”。

2. **过强或过弱的字段假设**：
   - **对全称定理（P1～P3）而言过强的假设**：
     1. `Rig.w_range`（$w \in \{-1, 0, 1\}$）：除 [`Probe.total_odd_iff`]（把“$w$ 为奇数”简化为“$w \neq 0$”）外，其余所有定理（[`Probe.f_sub_b`]、[`Probe.total_parity`]、[`Probe.inner_signed`]、[`Probe.inner_parity`] 等）对任意 $w \in \mathbb{Z}$ 均成立，并不依赖 `w_range`。
     2. `Rig.orient_pm`：要求 `R.cells` 中**所有**小格满足 $\text{orient} = \pm 1$（与 [doc.md]“假定没有退化小格”一致）。但按 [page_sections_2_to_5.txt] 第 4 节，即使轴线存在退化小格（$\text{orient} = 0$，像为线段或射线），只要正则点 $A$ 不落在退化小格的像上（即只要求**被点中的小格**满足 `∀ c ∈ R.cells, P.hit c = true → c.orient = 1 ∨ c.orient = -1`），全部定理依然成立。
   - **对存在性反例（P4 否定）而言过弱的假设**：
     [`Rig`] 没有要求 `(cells.map Cell.id).Nodup`（小格无重复），没有要求 `∃ c ∈ cells, c.inner = true ∧ c.orient = orient0`（`orient0` 确实来自某个面内小格），也没有约束网格结构及外延格与相邻面内格同向。对于全称定理，结构假设越弱定理越强；但对于存在性定理 [`fIn_parity_not_determined`]，假设越弱越容易构造脱离几何的组合实例——这一点由 [Bow100.lean] 的 676 格真实装置实例做了补足。

3. **把守恒律写成 [`Probe.law`] 字段是否诚实、恰当**：
   - **诚实且恰当**。守恒律 $\sum_{c \in \text{hit}} \text{orient}(c) = w$ 是分片仿射映射的 Brouwer 度性质，本工作的核心问题是检验“从守恒律能推出哪些关于正背面计数与奇偶的算术结论”。将守恒律作为 [`Probe`] 的结构字段（即每个探针实例自带的前提条件），并在代码注释和文档中明确声明“守恒律是假设而非 Lean 证明的定理”，没有滥用 `axiom` 或 `sorry`。此外，将 `law` 放在 [`Probe`] 字段中还带来一个好处：构造反例 [`toyProbe1`]、[`toyProbe2`] 和 [`Bow100.probe1`]、[`Bow100.probe2`] 时，Lean 强制核对了反例满足守恒律。

---

### 2. 逐条对照 [doc.md] 第 6 节定理表

| Lean 定理 | 是否对应数学命题 | 是否平凡或循环 | 审查意见 |
|---|---|---|---|
| [`sum_orient_eq`] | 是 | **非平凡、非循环** | 对列表归纳，把 `map Cell.orient` 的元素和转化为按 `orient == o0` 与 `orient == -o0` 过滤后的长度差乘以 `o0`，是连接 `law` 与计数的核心引理。 |
| [`Probe.count_split`] / [`Probe.f_split`] / [`Probe.b_split`] | 是 | **非循环** | `f`、`fIn`、`fExt` 各自独立定义为 `R.cells.filter` 的长度，并非把 `f` 定义成 `fIn + fExt`，故拆分性质需经 [`length_filter_split`] 归纳证明。 |
| [`Probe.f_sub_b`] | 是 | **非循环** | `law` 假设的是 $\sum \text{orient} = w$，此定理利用 $\text{orient0} = \pm 1 \implies \text{orient0}^2 = 1$ 推出 $f - b = w \cdot \text{orient0}$，完整对应 [page_sections_2_to_5.txt] 第 5 节。 |
| [`Probe.total_parity`]、[`Probe.total_odd_iff`] | 是（P1） | **非循环** | 准确表达 $(f + b) \bmod 2 = (w \cdot \text{orient0}) \bmod 2$ 及奇偶与 $w \neq 0$ 的等价性。 |
| [`Probe.inner_signed`] | 是（P3） | **非循环** | 由 `f_sub_b`、`f_split`、`b_split` 推出 $f_{\text{in}} - b_{\text{in}} = w \cdot \text{orient0} - (f_{\text{ext}} - b_{\text{ext}})$。 |
| [`Probe.inner_parity`] | 是（P2） | **非循环**（但右端写法可改进） | 准确表达 $(f_{\text{in}} + b_{\text{in}}) \bmod 2 = (w \cdot \text{orient0} + (f_{\text{ext}} + b_{\text{ext}})) \bmod 2$。不过右端写的是 `P.fExt + P.bExt`（先按正背过滤再相加），若引入不看旋向的外延计数 `P.nExt := P.count (fun c => !c.inner)` 则更能直接体现“不需要外延原像的正反”。 |
| [`Probe.fIn_recover`]、[`Probe.bIn_recover`] | 形式上对应 P3′，但**存在同义改写问题** | **是 [`Probe.inner_signed`] 的平凡改写** | 详见下方专门分析。 |
| [`fIn_parity_not_determined`] 与 [`Bow100.fIn_parity_not_determined`] | 是（P4 的否定） | **非循环** | 见问题 3。 |

**重点审查 [`Probe.fIn_recover`] 与 [`Probe.bIn_recover`]**：
- 它们的当前类型是：
  ```lean
  2 * (P.fIn : Int) = ((P.fIn + P.bIn : Nat) : Int) + (R.w * R.orient0 - ((P.fExt : Int) - P.bExt))
  2 * (P.bIn : Int) = ((P.fIn + P.bIn : Nat) : Int) - (R.w * R.orient0 - ((P.fExt : Int) - P.bExt))
  ```
- **是的，在当前写法下，它们只是 [`Probe.inner_signed`] 两边同加（或被减）`P.fIn + P.bIn` 的恒等改写**。更关键的问题在于：定理名叫 `fIn_recover`（恢复 `fIn`），但**等式右端直接写出了 `P.fIn + P.bIn`，把待恢复的 `P.fIn` 放在了已知项里**。
- 数学上 P3′ 想说的是：若已知不区分正背的面内原像总数 $N_{\text{in}}$（即 `P.nIn := P.count Cell.inner`），即可算出 `P.fIn`。之所以 $N_{\text{in}} = f_{\text{in}} + b_{\text{in}}$，是因为所有小格不退化（`orient = ±1`）且 `orient0 = ±1`，每个面内原像非正即背。因为代码中没有单独定义 `Probe.nIn (P : Probe R) : Nat := P.count Cell.inner`，跳过了 `P.nIn = P.fIn + P.bIn` 这一步，导致 `fIn_recover` 和 `bIn_recover` 在语法上成了左端含 `fIn`、右端也含 `fIn` 的平凡移项（见问题 6 修改建议）。

---

### 3. [`fIn_parity_not_determined`] 作为 P4 的否定是否充分，`toyRig` 与 `Bow100` 各自证明了什么

1. **作为 P4 的否定是否充分**：
   - **充分**。P4 声称“面内正面像数的奇偶 $f_{\text{in}} \bmod 2$ 由 $k = w \cdot \text{orient0}$ 和外延计数 $(f_{\text{ext}}, b_{\text{ext}})$ 决定”。
   - 两个探针 `P₁ P₂ : Probe R` 共用同一个 `R : Rig`，意味着不仅 $w$、$\text{orient0}$ 及 $k = w \cdot \text{orient0}$ 完全相同，连底层的整套轴线小格结构 `R.cells` 也完全相同。在模型中，一个 `Rig` 代表一副固定轴线，一个 `Probe R` 代表该轴线上一个正则点的原像清单；因此同一 `R` 下的两个 `Probe R` 完全足以表达“同一副轴线上的两个正则点”，比仅要求两点的 $(k, f_{\text{ext}}, b_{\text{ext}})$ 相同更强。

2. **[`toyRig`]（5 格）与 [Bow100.lean]（676 格）各自证明了什么**：
   - **[`toyRig`] 证明的是“公理层面的逻辑独立性”**：仅凭 [`Rig`] 和 [`Probe`] 的假设（特别是守恒律 $\sum \text{orient} = w$），在算术上不可能唯一决定 $f_{\text{in}} \bmod 2$，因为面内多出一对正背原像（跨过面内折缝，$+1$ 与 $-1$ 抵消）不改变 $\sum \text{orient}$ 与外延计数，却使 $f_{\text{in}}$ 加 1。
   - **[Bow100.lean] 证明的是“真实几何装置导出数据的非退化核验”**：`toyRig` 只保留了 5 个被点中的小格，无法排除“在真实的 $26 \times 26$ 网格上会不会因为几何约束而不存在这种点”的疑问；[Bow100.lean] 把弯 100 px 装置的全部 676 个小格及 $A_1=(0,80)$、$A_2=(60,80)$ 的原像小格编号放入 Lean，核对了它们在完整小格表上确实满足守恒律且给出 $(1,0,0,2)$ 与 $(2,1,0,2)$ 的计数。

---

### 4. [Bow100.lean] 核对了什么、没核对什么，以及边界划分

1. **已核对的内容（由 `decide +kernel` 在 Lean 内核中验算）**：
   - [`cells`] 列表中每个元素的 `orient` 均属于 $\{1, -1\}$，且 `orient0 = 1`、`w = -1` 满足范围假设；
   - [`hits1`]（`[0, 118, 675]`）与 [`hits2`]（`[0, 145, 349, 422, 675]`）在 [`cells`] 中筛出的小格 `orient` 之和均等于 `-1 = rig.w`（即 [`probe1.law`] 与 [`probe2.law`]）；
   - [`census1`] 与 [`census2`] 的四元组计数分别为 `(1, 0, 0, 2)` 和 `(2, 1, 0, 2)`，从而 [`Bow100.fIn_parity_not_determined`] 成立。

2. **未核对的内容**：
   - **浮点几何部分**：24 段正弦折线的顶点坐标、各小格切向量叉积符号确实生成了 [`cells`]、端向环缠绕数确实为 `w = -1`、以及画布点 $(0,80)$ 和 $(60,80)$ 经仿射求交算出的原像小格确实是 `[0, 118, 675]` 和 `[0, 145, 349, 422, 675]`。
   - **[`Bow100.lean`] 注释中声称的离散网格结构不变量**：注释第 7～8 行写明 `id = (i + 1)·26 + (j + 1)`（$i, j \in -1..24$）、$-1$ 和 $24$ 为外延、外延格的 `orient` 与相邻面内格相同，但文件里并没有用 `decide +kernel` 核对 `cells` 的 `id` 恰好是 `0..675` 无重无漏、`inner` 字段与行列范围一致、以及外延格 `orient` 与邻格一致。

3. **边界划分与表述评价**：
   - 将“浮点几何求交”（由外部 JS/Python 脚本计算）与“离散守恒律及计数推论”（由 Lean 核验）切开的**边界划得完全正确**。
   - [doc.md] 与 [Bow100.lean] 对浮点几何未入 Lean 的边界**说得很清楚**；唯一可补充的是把上述 $26 \times 26$ 网格本身的离散良构性也在 `Bow100.lean` 中用 `decide +kernel` 核对掉（见问题 6）。

---

### 5. `Int` 取模（`Int.emod`）与 `Nat` 取模混用问题

1. **`Int.emod` 对负数给非负余数（`(-1 : Int) % 2 = 1`）是否影响 [`Probe.total_parity`]、[`Probe.inner_parity`] 的含义**：
   - **不仅没有负面影响，反而是这两条定理在 $w \cdot \text{orient0} = -1$ 时成立的关键**。
   - 在 [`Probe.total_parity`] 中，左端 `((P.f + P.b : Nat) : Int)` 是非负整数，模 2 只可能取 `0` 或 `1`；右端 `R.w * R.orient0` 在 `toyRig` 和 `Bow100.rig` 中取 `-1`。若采用向零截断取模（`Int.tmod`，`(-1).tmod 2 = -1`），左端为 `1` 而右端为 `-1`，等式反而不成立。正因为 Lean 4 的 `%` 是欧几里得取模 `Int.emod`，模数 `2 > 0` 时对任意整数（无论正负）偶数为 `0`、奇数为 `1`，满足 `x % 2 = y % 2 ↔ 2 ∣ (x - y) ↔ x ≡ y (mod 2)`，等号才严格等价于整数模 2 同余。

2. **`Nat` 的 `%` 与 `Int` 的 `%` 混用是否有问题**：
   - **没有逻辑问题**。对任意 `n : Nat`，非负整数上的 `Int.emod` 与 `Nat.mod` 完全一致（`((n : Int) % 2) = ↑(n % 2)`）。在含负数项 `R.w * R.orient0` 的等式（[`Probe.total_parity`]、[`Probe.inner_parity`]）中提升到 `Int` 取模，在纯自然数命题（[`Probe.total_odd_iff`]、[`fIn_parity_not_determined`]）中直接用 `Nat` 取模，语义完全等价且无歧义。

---

### 6. 具体修改建议

1. **在 [Parity.lean] 中引入不依赖旋向的原像个数定义 `Probe.n`、`Probe.nIn`、`Probe.nExt`，并改写 [`Probe.fIn_recover`]、[`Probe.bIn_recover`]、[`Probe.inner_parity`]（以及可选改写 [`Probe.total_parity`]、[`Probe.total_odd_iff`]）**
   - **应增加的定义与引理**：
     ```lean
     /-- 原像总数（不区分正背）。 -/
     def Probe.n (P : Probe R) : Nat := P.count (fun _ => true)
     /-- 面内原像总数（不区分正背）。 -/
     def Probe.nIn (P : Probe R) : Nat := P.count Cell.inner
     /-- 外延原像总数（不区分正背）。 -/
     def Probe.nExt (P : Probe R) : Nat := P.count (fun c => !c.inner)

     theorem Probe.n_eq (P : Probe R) : P.n = P.f + P.b := ...
     theorem Probe.nIn_eq (P : Probe R) : P.nIn = P.fIn + P.bIn := ...
     theorem Probe.nExt_eq (P : Probe R) : P.nExt = P.fExt + P.bExt := ...
     ```
   - **应改成的定理陈述**：
     - [`Probe.inner_parity`]：
       ```lean
       theorem Probe.inner_parity (P : Probe R) :
           (P.nIn : Int) % 2 = (R.w * R.orient0 + (P.nExt : Int)) % 2
       ```
     - [`Probe.fIn_recover`] 与 [`Probe.bIn_recover`]：
       ```lean
       theorem Probe.fIn_recover (P : Probe R) :
           2 * (P.fIn : Int)
             = (P.nIn : Int) + (R.w * R.orient0 - ((P.fExt : Int) - P.bExt))

       theorem Probe.bIn_recover (P : Probe R) :
           2 * (P.bIn : Int)
             = (P.nIn : Int) - (R.w * R.orient0 - ((P.fExt : Int) - P.bExt))
       ```
     - （可选）[`Probe.total_parity`] 与 [`Probe.total_odd_iff`] 将 `P.f + P.b` 换为 `P.n`。
   - **理由**：当前 [`Probe.fIn_recover`] 右端直接写 `P.fIn + P.bIn`，导致待恢复的 `P.fIn` 出现在右端，使定理退化为 [`Probe.inner_signed`] 的平凡线性移项；改为只按位置过滤、不看 `orient` 的 `P.nIn` 和 `P.nExt` 后，才能真正体现“由不区分正背的面内原像总数 `nIn` 恢复 `fIn`”以及 P2“外延计数 `nExt` 不需要读取 `orient`”。

2. **（可选）在 [Bow100.lean] 中补一条核对 676 格网格编码良构性的定理**
   - **应增加的陈述**：
     ```lean
     theorem cells_grid_wf :
         cells.map Cell.id = List.range 676 ∧
         cells.all (fun c =>
           let i := c.id / 26
           let j := c.id % 26
           c.inner == (0 < i && i < 25 && 0 < j && j < 25)) = true := by
       decide +kernel
     ```
   - **理由**：把 [Bow100.lean] 注释中声称的“676 个小格编号无重复且内外延按 $26 \times 26$ 首末行列划分”纳入内核验证。

---

### 结论

**需修改（列出必须改的）**——注：主命题 P1、P2、P3（[`Probe.inner_signed`]）和 P4 的否定反例本身在数学和逻辑上已经**可信**；唯一必须修改以消除“同义反复/循环表达”瑕疵的是 P3′ 的恢复定理：

- **必须修改**：在 [Parity.lean] 中独立定义不看旋向的面内/外延/总体原像计数 `Probe.nIn`、`Probe.nExt`、`Probe.n`（通过 `P.count` 定义而非 `fIn + bIn`），证明 `P.nIn = P.fIn + P.bIn` 与 `P.nExt = P.fExt + P.bExt`，并用 `P.nIn`、`P.nExt` 替换 [`Probe.fIn_recover`]、[`Probe.bIn_recover`]、[`Probe.inner_parity`] 右端的 `P.fIn + P.bIn` 与 `P.fExt + P.bExt`，使 `fIn_recover` 不再在等式右端直接出现 `P.fIn`。

