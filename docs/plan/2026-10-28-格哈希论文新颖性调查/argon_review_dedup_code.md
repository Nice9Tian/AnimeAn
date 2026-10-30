# Argon 交叉审核：去重改为朝向配对（chord_intersect.py 与三张说明页）

2026-10-30，Gemini 4 Argon（high），只读模式（`agy-run.ps1 -Sandbox off`），读了 chord_intersect.py 与 t_chord_intersect.py 的改动和全文、三张页面的改动、page_dedup_check.js、两份测试输出、文档 8.1～8.2；STATUS: SUCCESS  (turns=1, duration=271.0s, tokens=120932)。审的是采纳建议之前的版本，行号对应那时的文件；采纳情况见 `2026-10-30-w_orient0奇偶校验命题与Lean证明.md` 第 8.7 节。原文如下，未改动。

---
### 1. 正确性

* **规则实现**：[`_merge_duplicates`] 与三张说明页的新循环（[`diff_pages.txt`]、[`w_orient0_explainer.html`]）均准确实现了新规则。
* **提前终止与全量比较的等价性**：在有限浮点数下**严格等价**。排序后 `rows` 按 `l_h`（主键）非降序排列，`kept` 作为 `rows` 已处理前缀的子序列，其 `l_h` 同样单调非降：对任意 $0 \le k' \le k < \text{len}(\text{kept})$，有 $\text{kept}[k'][0] \le \text{kept}[k][0] \le \text{row}[0]$，故
  $$0 \le \text{row}[0] - \text{kept}[k][0] \le \text{row}[0] - \text{kept}[k'][0]$$
  一旦在某个 $k$ 处 `row[0] - kept[k][0] >= DEDUP`，更早的所有 $k' \le k$ 必然也满足 $|\text{row}[0] - \text{kept}[k'][0]| \ge \text{DEDUP}$。
* **边界情况**：
  * **`l_h` 相等**：`row[0] - kept[k][0] == 0.0 < DEDUP`，循环不会提前停下，会继续往前扫过所有 `l_h` 相等（以及更早但差值 `< DEDUP`）的已保留项，再由 `abs(row[1] - prev[1]) < DEDUP` 和 `orient` 判定，正确。
  * **负零（`-0.0` vs `+0.0`）**：IEEE 754 下 `-0.0 == 0.0`，差值为 `0.0 < DEDUP`。JS 排序比较器 `a.lh - b.lh || a.lv - b.lv` 中，当 `a.lh` 与 `b.lh` 为 `±0` 时，`a.lh - b.lh` 为 `±0`（在 JS 布尔求值中均为 falsy），会自动落入 `|| a.lv - b.lv` 按次键排，不会出错。
  * **外延上的弧长与 `±inf`**：首末段虽然将参数边界设为 `t_lo[0] = -inf, t_hi[-1] = +inf`（[`_bounds`]），但因退化平行格已被 `abs(den) <= 1e-12 * ...` 排除，对任何有限坐标点 `point`，外延上解出的 `t, u` 和弧长 `l_h, l_v` 都是**有限浮点数**，正常使用不会出现 `±inf`。若人工向 `_merge_duplicates` 传入两个 `+inf` 弧长，`(+inf) - (+inf)` 为 `NaN`，`NaN < _DEDUP` 为 `False`，不会合并。
  * **`NaN`**：正常求交不会产生 `NaN`。若调用方传入含 `NaN` 的坐标，需要注意 [`_test`] 和页面 [`solve`] 用的是 `if t < t_lo or t > t_hi: continue`（`NaN` 比较均为 `False`，不会 `continue`），会生成 `NaN` 弧长并破坏排序单调性；但模块入口默认输入为有限画布坐标，这不影响正常路径。

---

### 2. 合并的传递性

* **新规则的行为**：若三条同向报告按序为 $a, b, c$，$a$ 与 $b$ 距离 $< \text{DEDUP}$、$b$ 与 $c$ 距离 $< \text{DEDUP}$，而 $a$ 与 $c$ 距离 $\ge \text{DEDUP}$：新规则先保留 $a$；扫到 $b$ 时因与 $a$ 近而丢弃 $b$；扫到 $c$ 时只与 `kept` 中的 $a$ 比，因距离 $\ge \text{DEDUP}$ 而**保留 $c$**，最终保留 `[a, c]`。
* **旧规则的行为**：旧规则只和 `kept[-1]` 比，同样是与已保留的最后一条比、不与被丢弃的 $b$ 比。因此在只有同向的 $a, b, c$ 时，旧规则也会丢弃 $b$、保留 $a$ 和 $c$（结果也是 `[a, c]`）。
* **在本问题里会不会出现、要不要处理**：
  * **不会出现，也不应改成传递闭包**。
  * 原因：在 [`_finish`] 和页面 [`solve`] 中，计算弧长前已将内部节点处的越界参数夹紧到 $[0, 1]$。共边两格在跨边轴向上的弧长差为 $0$，在沿边轴向上的弧长差至多约为 $\text{EPS} \cdot \max(|r_i|, |q_j|)$（Python 中 $\sim 4\times 10^{-7}\text{ px} < \text{DEDUP} = 10^{-6}\text{ px}$；页面中 $\sim 1.7\times 10^{-6}\text{ px} \ll \text{DEDUP} = 10^{-4}\text{ px}$）。同一个真根的 2～4 份重复报告全部挤在直径远小于 `DEDUP` 的极小团簇里，两两直接距离均 `< DEDUP`。反之，若改成传递闭包（单链聚类），在尖点或细密折线段附近反而可能把跨度超过 `DEDUP` 的不同真根串联吞并。

---

### 3. 对下游的影响

* **下游各量在什么情况下会变**：
  1. **探针靠近折缝像，使折缝两侧的一对反向真根 $(r_+, r_-)$ 在 $(l_h, l_v)$ 上距离 $< \text{DEDUP}$**：
     * 旧规则会把排序靠后的那个当成前一个的重复而误删；新规则两个都保留。
     * [`Result.count`]（及面内时的 [`Result.count_on_sheet`]）比旧规则多 1。
     * 当 $r_-$ 的 $l_h$ 比 $r_+$ 略小（排序在 $r_+$ 前面）时，旧规则吞掉的是正面根（或与 `orient0` 同向的根）：在 [`chord_intersect.py`] 和 `fold_inverse_explainer.html` 中，旧规则会因此丢失该正面候选，导致 `principal` 变成 `None`（误裁断）或跳到远处另一层的正面根；新规则恢复了正确的 `principal`。在页面统计中，旧规则误删正面根时新规则的 `f`（及 `fs`）加 1，旧规则误删背面根时新规则的 `b` 加 1，`sum` 从偏离 `w` 恢复为 `w`。
  2. **两条同向重复报告 $a_1, a_2$ 之间在 `l_h` 排序上夹了一条反向报告 $x$（且 $|a_2.l_v - x.l_v| \ge \text{DEDUP}$）**：
     * 旧规则只看 `kept[-1]`（即 $x$），会漏掉对 $a_2$ 的去重；新规则越过 $x$ 看到 $a_1$ 并正确合并，计数比旧规则少 1。
  3. **尖点（cusp）或双折缝角极近处（`< DEDUP` 盲区内）**：
     * 旧规则将正反报告全部压成 1 条；新规则按旋向分别合并，保留一正一反共 2 条，`count` 从 1 变为 2，且保证 `orient0` 方向的候选不会因排在反向报告后面而被吞掉。
* **调用方依赖检查**：
  * [`chord_intersect.py`] 顶部已注明正式映射器不调用此模块。
  * 折缝两侧的两个根落在不同小格 $(i, j)$ 与 $(i+1, j)$、旋向相反，本来就是两个不同的原像；没有任何下游逻辑依赖旧规则误删折缝根后的错误计数，相反，守恒律校验与裁断判定都依赖新规则保留它们。

---

### 4. 测试是否真的验证了修改

* **[`t_chord_intersect.py`] 的三段测试**：
  * 先用合成数据直接测了 [`_merge_duplicates`] 的各分支（含同向重复间夹异向报告的情形），再在 `bow 100 px` 上实测：(a) 184 条内部竖直折缝边旁两个真根距离 `< _DEDUP` 时新规则 100% 保持 $\sum \text{orient} = w$ 而旧规则 100% 失败；(b) 80 个尖点角盲区内新规则偏差至多为 $\pm 1$（可被守恒律检出）、从不差 2，而旧规则有 140 处差 2；(c) 2000 个随机探针全部满足守恒律。验证充分。
* **[`page_dedup_check.js`] 排除「单边 EPS 接受带」是否公平、会不会藏匿去重本身的错误**：
  * **完全公平，且不可能藏匿去重本身的错误**。
  * 理由：
    1. 排除条件 `near % 2 === 1` 是在 **`rawRig`（`DEDUP = 0`，完全未去重的原始报告）** 上计算的，**根本不经过去重循环**。去重代码写得对或错，完全不会改变 `near` 的奇偶性，因此去重本身的任何 bug 都无法借此分支逃避断言。
    2. 探针取在内部折缝边中点附近（远离格角），局部窗口 $|\ell_h - s_h| < 1, |\ell_v - s_v| < 1$ 内只有共边的两个反旋向小格。在折叠覆盖侧（2 层侧）两格各有 1 个真根（`near == 2`）；在 0 层侧，因参数容差 `EPS = 1e-7` 换算到画布上的接受带宽度正比于 $|r_i \times \hat{q}_j|$，两格宽度不同，落在两宽度之间的探针会被其中一格单独报出 1 个出界假根（`near == 1`）。此时未去重的原始报告集合本身就多出 1 个孤立假根，任何去重规则都不可能凭空删掉一条孤立报告使 $\sum \text{orient} = w$。
    3. 所有的 2 层侧真根探针（`near == 2`）以及 0 层侧双边接受带探针（`near == 2`，一对反向假根抵消）都被完整保留并参与了新旧规则对比。

---

### 5. 性能

* **最坏情况复杂度**：设单点原始报告数为 $m$。排序为 $O(m \log m)$；内层 `while` 循环在所有报告的 `l_h` 都在 `_DEDUP` 内、但 `l_v` 互不接近的最坏情况下比较 $\frac{m(m-1)}{2}$ 次，时间复杂度为 $O(m^2)$。
* **实际影响**：**完全可以忽略**。实际场景中每点原像通常为 $1 \sim 5$ 个（含重复报告 $m \le 10$），且不同原像的 $l_h$ 通常相差数像素以上，内层循环几乎都在 $1 \sim 2$ 步内因 `row[0] - kept[k][0] >= _DEDUP` 终止，耗时在纳秒级。

---

### 6. 注释与文档字符串是否准确

整体准确，未夸大效果（如实说明了尖点与双折缝角的盲区），但有 **3 处细节不够精确或行号过时**：

1. **零层侧双边 EPS 接受带内的反向假根未在代码注释中区分**：
   * [`doc_section_8_1_to_8_2.md`] 推论明确限定了「在 EPS 接受带之外……A 落在折缝像零层一侧的接受带内时，两格会各报一个略出界的假根、旋向相反，这不在推论的范围里」。
   * [`_merge_duplicates`] 的 docstring 写的是 `with OPPOSITE orients the edge is a crease and the reports are two roots (for a point off the crease image)`，页面注释（[`diff_pages.txt`]）也直接说反向近距报告是折缝两侧的两个真根。实际上在折缝像零层侧的双边 EPS 带内，这两条被保留的反向报告是两个假根（虽然旋向一正一负抵消、不破坏 $\sum \text{orient} = w$）。
2. **文件内引用的行号与常量说明过时**：
   * [`w_orient0_explainer.html` 第 304 行] 页脚写 `pyfile/chord_intersect.py（orient0 在第 183 行）`；插入 `_merge_duplicates` 后，`self.orient0` 现位于 [`chord_intersect.py` 第 221 行]。
   * [`doc_section_8_1_to_8_2.md` 第 7 行] 写 `（页面 DEDUP = 1e-4 px，正式实现 1e-6 px，chord_intersect.py 第 555 行）`：三张页面中只有 `w_orient0_explainer.html` 是 `1e-4`，另两张是 `1e-6`；且 `chord_intersect.py` 的去重已移至 [`_merge_duplicates`]（第 160–195 行，调用处在 [`_finish` 第 589 行]）。
3. **[`t_chord_intersect.py`] 中 [`raw_rows`] 的注释与实现不一致**：
   * 注释写 `"""Every report of the full matrix, as _finish's rows but NOT merged."""`，但实现调用的是 `solver._pairs_grid(p, "all")` 而非全矩阵 `solver._pairs_python(p)`。
   * 注意在 [`prepare_grid`] 和 [`_pairs_grid`] 中，边界格（`i == 0 / last_i` 或 `j == 0 / last_j`）既有 `sheet` 里的夹紧候选，又在 `extensions="all"` 时无条件跑一遍 `_extension_candidates` 里的未夹紧候选。若探针在边界格的**面内部分**有一个原像，`_pairs_grid(p, "all")` 会把**同一个边界格 `(i, j)` 报告两次**，而全矩阵 `_pairs_python(p)` 只报告一次。

---

### 7. 具体修改建议

以下均为非阻塞的改进建议：

1. **[`t_chord_intersect.py` 第 271–273 行]（[`raw_rows`]）**：
   * **改成什么**：将 `solver._pairs_grid(p, "all")` 改为 `solver._pairs_python(p)`（或在 `_pairs_grid` 结果上按 `(i, j)` 去重）。
   * **理由**：与 docstring `"Every report of the full matrix"` 一致，避免 `_pairs_grid(p, "all")` 对边界格的面内命中重复产出同一格 `(i, j)` 的两份报告。
2. **[`page_dedup_check.js` 第 73 行]（及 [`t_chord_intersect.py` 第 305 行]）**：
   * **改成什么**：在 `page_dedup_check.js` 中将 `j` 的循环改为 `for (let j = 0; j < F.nv; j++)`（因为测试点取在竖直边中点 $u = 0.5$，即使 $j = 0$ 或 $F.nv - 1$ 也在段内部、远离 $u \le 0$ 或 $u \ge 1$ 的外延边界），并可选补充水平折缝边 `(i, j) | (i, j + 1)`。
   * **理由**：`hook` 装置的 $V$ 只有 1 段（`F.nv === 1`），原条件 `j = 1; j + 1 < F.nv` 导致 `hook` 的折缝边被跳过（输出 `0 fold edges`）；放开 `j` 后能测到 `hook` 的折缝边，补充水平折缝边还能覆盖 `l_h` 相等、按次键 `l_v` 区分的分支。
3. **[`w_orient0_explainer.html` 第 304 行] 与 [`chord_intersect.py` 第 170 行]**：
   * **改成什么**：将 [`w_orient0_explainer.html` 第 304 行] 的 `（orient0 在第 183 行）` 改为 `（orient0 在第 221 行）`；将 [`chord_intersect.py` 第 170 行] 的 `(for a point off the crease image)` 改为 `(for a point off the crease image and its zero-sheet-side EPS band)`。
   * **理由**：保持行号引用和数学前提与实现及文档 §8.2 推论完全一致。

---

### 结论

**可以合入**。核心去重算法、排序早停条件与下游衔接均正确无误，无必须修改的阻塞问题。

