# 方案 C 遗留临界根问题研究与组合去重实现报告（`argon/crease-cusp-roots`）

> 关联文档：
> - [`2026-10-30-w_orient0奇偶校验命题与Lean证明.md`](../2026-10-30-w_orient0奇偶校验命题与Lean证明.md)（第 1 节、第 8.2 / 8.3 / 8.5 / 8.8 节）
> - [`PROBLEM.md`](./PROBLEM.md)、[`fable_results_summary.txt`](./fable_results_summary.txt)
> - [`pyfile/chord_intersect.py`](../../../../pyfile/chord_intersect.py)
> - [`w_orient0_explainer.html`](../w_orient0_explainer.html)、[`fold_inverse_explainer.html`](../fold_inverse_explainer.html)、[`fold_severing_map.html`](../fold_severing_map.html)

---

## 一、核心结论总览（已核实的结论 vs 推测）

### 1.1 两个问题的决策结论

| 问题 | 决策 | 核心理由 |
| :--- | :--- | :--- |
| **问题一：恰好落在折缝或尖点上的根**<br>（`A = hv(...)` 经浮点舍入或 $\pm 1,\pm 2\text{ ulp}$ 扰动后落到零层侧，成对消失） | **不改**（保持方案 C 的 0 容差精确闭合四边形网格；**否定**折缝边共享容差 $\tau > 0$ 的设想） | 1. **不是求解器错误**：对经浮点舍入后的画布点 $p$，精确有理数（`Fraction`）网格真值就是 **0 个局部根**（`false = 0`，`lost_vs_truth = 0`），且正反两支成对消失使 $(+1)+(-1)=0$，全局守恒律 $\sum \text{orient} = w$ 恒成立。<br>2. **下游输入为画布点而非预知折缝弧长**：实际下游（`auto_mapping.py`、连续笔画求逆、画布栅格染色）在覆盖侧看到 $(+1,-1)$ 两支并由 `principal` 选出正向支，在零层侧看到 0 个局部根并触发拓扑裁断，连续穿过折缝时两支在折缝线上精确汇合后同时消失，无单边假根抖动。<br>3. **折缝边加共享容差 $\tau > 0$ 必破守恒律（严格反例）**：见第 2.3 节定理 1——不仅会在零层侧 $\tau$ 宽带内制造整片假根对（污染 `principal` 与裁断边界），而且在任何引导线有折角的折缝端点或尖点角处，两条非共线横向边界直线截平移后的折缝线于不同点 $P_1 \ne P_2$，在角部三角形区域内**重新制造单个无配对假根**，直接打破 $\sum \text{orient} = w$。 |
| **问题二：尖点处的去重盲区**<br>（尖点楔形内两个同向真根间距 $< \text{\_DEDUP}$ 时被误合并为 1 个，$\sum \text{orient}$ 差 1） | **改**（实现**纯组合去重**：仅当两条同向报告来自同一小格或共享精确符号 `_orient == 0` 的内部边/格角时合并，彻底废除距离阈值 `_DEDUP`） | 1. **组合去重命题严格成立**：由方案 C 的共边精确谓词（定理 2），两个同向邻格同时接收点 $c$ **当且仅当** $c$ 在共享内部边直线上的精确 `orient2d == 0`。在 `_test` / `cellHit` 中利用已计算的 `_orient` 符号（**0 次额外谓词调用**）将内部边参数充要地钉死为 `0.0` 或 `1.0`，即可由纯组合关系判定同一原像。<br>2. **彻底消除尖点盲区**：尖点楔形内部（$c \ne N_{a,b}$）的两个同向真根位于对角小格（$|i - pi| = 1, |j - pj| = 1$）且不落在角节点上，无论靠得多近（实测至 $10^{-12}\text{ px}$）都绝不合并。Python `bow 100 px` 5200 个尖点探针的 $\sum \text{orient} \ne w$ 从 **3472 降为 0**；`probe_band.py` 三套装置共 262,214 个探针 `law / false / lost` **全部归零**；三张 JS 说明页 `fold+plain+node+rand` 总违例从 **50 降为 0**。 |

### 1.2 已核实的结论与推测的明确划分

- **已核实的结论（有严格数学证明 + `Fraction` / `BigInt` 精确真值实测支撑）**：
  1. 方案 C 下，所有落在折缝或尖点附近并在浮点舍入后“消失”的 `on_lattice` / `lattCrit` 探针（Python `bow 100 px` 节点 73 个、折缝边 83/150 个；JS 三页合计 2447 个），其舍入后坐标 $c = A + O$ 在 `Fraction` / `BigInt` 精确闭合网格中的局部包含格数确为 0（`false = 0, lost_vs_truth = 0`）。
  2. 在折缝边上引入任何非零共享容差 $\tau > 0$，只要折缝在内部节点处转折（或汇入尖点格角），就必然在节点附近的三角形楔区内产生单个假根（定理 1）。
  3. 铺满精确谓词后，`_test` / `cellHit` 对内部边参数釘死 `0.0 / 1.0` 与 `_orient == 0` 严格双向等价（引理 2）；基于该标记的组合去重覆盖了内部边、格角、外延格、首末段、零长度段以及存活的极窄格（定理 2 与第 3.3 节）。
  4. 唯一剩余使合并后 $\sum \text{orient} \ne w$ 的情形是**查询点 $c$ 零误差地精确落在零维奇异点（尖点格角顶点 $c = N_{a,b}$ 本身，或尖点角邻接边上 $\operatorname{orient2d} == 0$）**：此时 3 个局部页 $(+\sigma, +\sigma, -\sigma)$ 在参数空间坍缩为同一个格点 $(s_a, t_b)$，属于度理论中临界值（critical value）处不带重数的集合计数性质，测度为 0。
- **推测（依赖未接入的外部调用方式或极端退化输入）**：
  1. 若未来某个外部调用方需要“给定参数空间折缝弧长 $(l_h, l_v)$，先算 `A = hv(l_h, l_v)` 再反求回 $(l_h, l_v)$”的精确恒等往返，则在参数已知的前提下根本不应走画布点求逆 `solve(A)`，而应直接传递参数 $(l_h, l_v)$ 或在覆盖侧沿法向做单侧极限查询。
  2. 若输入引导线存在**非零长度但夹角 $|\sin\theta| \le 10^{-12}$** 的近平行段对，`_cell` 会将该格剔除并在网格中留下空洞；在实际手绘引导线（H 与 V 横纵正交布局）中未见触发，但若极端构造此类退化引导线，需将平行格退化为一维投影段处理（超出当前二维四边形求逆范畴）。

---

## 二、问题一：恰好落在折缝或尖点上的根（不改的论证与 $\tau$ 容差方案的反证）

### 2.1 现象本质：精确舍入网格上的真值就是 0 个局部根

在方案 C 中，小格 $(i, j)$ 的四个顶点定义为浮点舍入节点：
$$N_{a,b} = \operatorname{fl}(h_a + v_b) \in \mathbb{F}^2, \qquad (a, b) \in \{i, i+1\} \times \{j, j+1\}$$
内部边上的归属由精确的 `orient2d` 符号决定。设两个相邻小格（例如左格 $(i-1, j)$ 与右格 $(i, j)$）共享竖直内部边 $E = N_{i,j} N_{i,j+1}$，且两格定向相反（$s_{i-1,j} = -s_{i,j}$，即 $E$ 是**折缝边**）：
- 左格 $(i-1, j)$ 的右边界条件为：$s_{i-1,j} \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) \ge 0$；
- 右格 $(i, j)$ 的左边界条件为：$s_{i,j} \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) \le 0 \iff (-s_{i-1,j}) \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) \le 0 \iff s_{i-1,j} \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) \ge 0$。

注意：**折缝边两侧的小格要求 $\operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c)$ 具有相同的符号**！这不是程序漏洞，而是折叠映射的几何本质：映射在折缝边 $E$ 处发生 $180^\circ$ 对折，两格的像落在直线 $E$ 的**同一侧（覆盖侧 / 2 层侧）**，而直线 $E$ 的**另一侧（阴影侧 / 0 层侧）**没有任何局部原像：
- 当 $s_{i-1,j} \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) > 0$（覆盖侧）时，两格同时包含 $c$，各自贡献 $+1$ 与 $-1$ 两个不同原像；
- 当 $\operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) = 0$（精确落在折缝直线 $E$ 上）时，两格的闭集边界同时包含 $c$，仍报出 $(+1, -1)$ 两个报告；
- 当 $s_{i-1,j} \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) < 0$（零层阴影侧）时，两格都不包含 $c$，局部原像数为 0。

为什么 `probe_extra.py` 的 `on_lattice`（以及 `page_band_check.js` 的 `lattCrit`）会统计到 `root missed`？
1. 在折缝边中点 $s_h = \text{h\_cum}[i] - \text{h\_arc},\ s_v = \frac{1}{2}(\text{v\_cum}[j] + \text{v\_cum}[j+1]) - \text{v\_arc}$ 处，`hv(s_h, s_v)` 先用浮点插值计算 $V(s_v)$，再计算 $A = \operatorname{fl}(H(s_h) + V(s_v) - O)$，随后测试中又加回 $c = \operatorname{fl}(A + O)$ 并做 $\pm 1, \pm 2\text{ ulp}$ 扰动。
2. 这一连串浮点舍入和 $\pm\text{ulp}$ 扰动使点 $c$ 偏离直线 $N_{i,j} N_{i,j+1}$ 约 $0.5 \sim 2\text{ ulp}$。其中约一半落入覆盖侧（报出 2 个根），另一半落入**零层阴影侧**（$s_{i-1,j} \cdot \operatorname{orient2d} < 0$，报出 0 个局部根）。
3. 对每一个落入零层侧的点 $c$，独立用 `Fraction` 计算的精确网格真值 `Truth.cells(p)` 同样返回空集（`false = 0, lost_vs_truth = 0`），且由于 $(+1)$ 与 $(-1)$ 成对消失，全局守恒律 $\sum \text{orient} = w$ 保持 0 违例。

### 2.2 下游实际看到什么

核查工作树中所有下游与说明页调用点：
1. **实际输入来源**：无论是 `pyfile/auto_mapping.py`（主线采用阻尼牛顿法 + 拓扑裁断，见 [`docs/auto_mapping/topology_severing.md`](../../../auto_mapping/topology_severing.md)）、`ChordIntersect.locate_then_refine`，还是三张说明页的画布栅格扫描与手绘连续笔画求逆，**输入永远是画布平面上的目标点 $p = (x, y)$**，从来不存在“先知道参数空间折缝坐标 $(l_h, l_v)$，算出 $p = \text{hv}(l_h, l_v)$，再要求 `solve(p)` 找回 $(l_h, l_v)$”的业务路径。
2. **裁断判定与 `principal` 选择**：
   - 当画布点 $p$ 位于折缝的**覆盖侧**（包括精确落在折缝线上 `orient2d == 0`）时，折缝两侧的 $+1$ 小格和 $-1$ 小格**同时命中**，由于两者 `orient` 相反，去重保留两者；随后 `_finish` 在 `same = [k for k, row in enumerate(rows) if row[6] == orient0]` 中精确选出 `orient == orient0` 的正向分支作为 `principal`。
   - 当画布点 $p$ 越过折缝进入**零层阴影侧**（哪怕仅越过 $1\text{ ulp}$）时，该折缝局部的 $+1$ 与 $-1$ 两个分支同时消失。若无远处的另一层折叠面覆盖此处，则 `same` 为空，`principal` 返回 `None`，即**立即给出干净的拓扑裁断（severing）判定**。
3. **连续笔画穿过折缝时**：
   - 当画布上的一条连续采样笔画从覆盖侧朝折缝推进并穿入零层侧时：在覆盖侧，正向主支（`orient == orient0`）与反向伴生支（`orient == -orient0`）随距离减小而单调靠拢；在穿过折缝线的瞬间成对湮灭；进入零层侧后 `principal` 干净地变为 `None`。
   - 方案 C 消除了旧 `[-EPS, 1+EPS]` 浮点带在折缝单侧产生的单个假根，因此笔画穿过折缝时**不会出现“多报一个单边反向假根”或“正向假根在阴影侧苟延残喘”的跳变抖动**。

### 2.3 否定「只在折缝边上给共享容差 $\tau > 0$」的严格证明

任务书中提出了一个待核实的想法：*“只在折缝边上给一个很小的共享容差 $\tau$，保住这些根，代价是零层一侧 $\tau$ 以内出现成对假根，但不会出现单个假根。”*

我们对这一想法进行了严格的几何与代数分析，结论是：**否定该想法。它在多边形网格的折缝转折点与尖点角处仍然会产生单个假根，打破 $\sum \text{orient} = w$。**

#### 缺陷一：零层侧带状成对假根污染 `principal` 与裁断边界
若在折缝边 $E$ 上允许零层侧距离 $\le \tau$ 的点被折缝两侧小格 $(i-1, j)$ 与 $(i, j)$ 同时接收，则零层侧 $\tau$ 宽带内的所有画布点都会得到一对局部报告 $(+1, -1)$。由于其中**必有一个报告满足 `orient == orient0`**，`_finish` 会将该假根选为 `principal`！结果是：
1. 拓扑裁断边界向零层阴影侧人为外扩了 $\tau$；
2. 该 $\tau$ 宽带内的所有二维画布点被全部钉死（clamp）到同一条一维折缝边 $t = 0/1$ 上，导致逆映射在该带内坍缩退化（二维区域映射到一维折线段）。

#### 缺陷二（定理 1）：折缝转折点与尖点角处重新出现单个假根，破坏 $\sum \text{orient} = w$

> **定理 1（折缝容差 $\tau > 0$ 在转折节点处破坏配对性）**
> 设水平内部边 $S = N_{i,j} N_{i+1,j}$ 是下格 $B = (i, j-1)$（定向 $s_B = +1$）与上格 $T = (i, j)$（定向 $s_T = -1$）共享的折缝边（两格均位于直线 $S$ 的上方，即覆盖侧为 $\operatorname{orient2d}(N_{i,j}, N_{i+1,j}, c) \ge 0$，零层侧为 $< 0$）。
> 在左端点 $N_{i,j}$ 处，下格 $B$ 的左边界为向量 $q_{j-1} = N_{i,j} - N_{i,j-1}$，上格 $T$ 的左边界为向量 $q_j = N_{i,j+1} - N_{i,j}$（因 $S$ 是折缝，$q_{j-1}$ 与 $q_j$ 跨过 $r_i = N_{i+1,j} - N_{i,j}$ 反号）。
> 只要 $q_{j-1}$ 与 $q_j$ 不共线（即引导线 $V$ 在节点 $j$ 处有非零转折，$q_{j-1} \times q_j \ne 0$），则对于**任意小**的折缝容差 $\tau > 0$，在节点 $N_{i,j}$ 附近的零层侧 $\tau$-带内都存在一个非空开三角形区域 $\Delta_\tau$，使得对任意 $c \in \Delta_\tau$，**$B$ 与 $T$ 中恰有一个小格接收 $c$、另一个小格拒绝 $c$**（产生单个无配对假根，$\sum \text{orient} = w \pm 1$）。

**证明**：
以共享节点 $N_{i,j}$ 为坐标原点 $(0, 0)$，取折缝向量 $r_i = (L, 0)$（$L > 0$）沿 $+x$ 轴方向，使得覆盖侧为上半平面 $y \ge 0$，零层阴影侧为下半平面 $y < 0$。
1. 在折缝边 $S$ 上给共享容差 $\tau > 0$，等价于将 $B$ 与 $T$ 在边 $S$ 处的共享判据从 $y \ge 0$ 放宽为 $y \ge -\tau$。
2. 然而，小格 $B = (i, j-1)$ 与小格 $T = (i, j)$ 还有各自的**左边界**（与左邻列 $i-1$ 共享的竖直内部边 $W_B = N_{i,j-1} N_{i,j}$ 和 $W_T = N_{i,j} N_{i,j+1}$）：
   - 下格 $B$ 的左边界经过原点 $(0,0)$，方向为 $q_{j-1} = (x_1, y_1)$（因 $s_B = r_i \times q_{j-1} > 0$，有 $y_1 > 0$，即 $N_{i,j-1} = (-x_1, -y_1)$ 位于下半平面？注意 $N_{i,j} = h_i + v_j$，故 $N_{i,j-1} = N_{i,j} - q_{j-1} = (-x_1, -y_1)$，由于 $y_1 > 0$，边 $W_B$ 从 $(-x_1, -y_1)$ 指向 $(0,0)$，延伸入下半平面 $y \in [-\tau, 0)$ 的射线斜率为 $x / y = x_1 / y_1$）。
   - 上格 $T$ 的左边界从 $(0,0)$ 指向 $N_{i,j+1} = q_j = (x_2, y_2)$（因 $s_T = r_i \times q_j < 0$，有 $y_2 < 0$，实际上 $B$ 与 $T$ 折叠到了同一侧，或者说 $B$ 在 $S$ 的一侧具有内法向 $+y$，$T$ 在 $S$ 的同一侧也具有内法向 $+y$；无论 $W_B, W_T$ 朝向如何，直线 $W_B$ 的方程为 $y_1 x - x_1 y = 0$，直线 $W_T$ 的方程为 $y_2 x - x_2 y = 0$）。
3. 当 $\tau = 0$ 时，折缝直线 $y = 0$、左边界直线 $W_B$、左边界直线 $W_T$ **三线共点于原点 $N_{i,j} = (0, 0)$**。
4. 当 $\tau > 0$ 时，在零层侧水平线 $y = -\eta$（其中 $0 < \eta \le \tau$）上：
   - 下格 $B$ 的左边界直线 $W_B$ 与 $y = -\eta$ 交于横坐标 $x_B(\eta) = -\eta \frac{x_1}{y_1}$；
   - 上格 $T$ 的左边界直线 $W_T$ 与 $y = -\eta$ 交于横坐标 $x_T(\eta) = -\eta \frac{x_2}{y_2}$。
   只要 $q_{j-1} \nparallel q_j$，就有 $\frac{x_1}{y_1} \ne \frac{x_2}{y_2}$，从而 $x_B(\eta) \ne x_T(\eta)$！
5. 因此，在开三角形区域
   $$\Delta_\tau = \left\{ (x, -\eta) \;\middle|\; 0 < \eta < \tau,\ \min(x_B(\eta), x_T(\eta)) < x < \max(x_B(\eta), x_T(\eta)),\ x > 0 \right\}$$
   （若 $\min, \max < 0$，则该三角形落在左邻列 $i-1$ 的折缝容差带内，对称成立）中，点 $c$ 满足折缝容差 $y = -\eta \ge -\tau$，但在横向边界上**恰落在一格的左边界内侧、另一格的左边界外侧**！
6. 于是 $B$ 与 $T$ 中只有一个小格报出假根，另一个小格因左边界 `orient2d` 被拒，**在零层侧产生单个未配对的假根，导致 $\sum \text{orient} = w \pm 1$**。 $\blacksquare$

同理，在**尖点格角**（4 个相邻格中有 2 条折缝边、2 条非折缝同向边汇合于 $N_{a,b}$）处，若只在 2 条折缝边上放宽 $\tau > 0$ 而在 2 条非折缝边上保持 $\tau = 0$，折缝容差带越过非折缝边进入邻格扇区时，也必然产生单格假根。只有 $\tau = 0$ 时，所有边直线精确交于同一格点 $N_{a,b}$，闭合四边形网格才严格无重无漏。

---

## 三、问题二：尖点处的去重盲区（组合去重定理、一致性证明与边界核查）

### 3.1 盲区成因：距离阈值 `_DEDUP` 无法区分「共边同向重复」与「尖点楔形对角同向双根」

在方案 C 基线中，`_merge_duplicates` 规则为：
> 若两条报告的 `orient` 相同，且 $|l_h^{(1)} - l_h^{(2)}| < \text{\_DEDUP}$ 且 $|l_v^{(1)} - l_v^{(2)}| < \text{\_DEDUP}$（Python 中 `_DEDUP = 1e-6`，说明页中 `1e-4` 或 `1e-6`），则视为同一原像合并。

在**尖点格角** $N_{a,b}$ 处（3 格定向为 $+\sigma$，1 格定向为 $-\sigma$，格角绕数 $W = +\sigma$），设奇异格（$-\sigma$）为 SW 格 $(a-1, b-1)$，则三个 $+\sigma$ 格为 SE $(a, b-1)$、NE $(a, b)$、NW $(a-1, b)$。
当画布点 $c$ 落入尖点楔形（即 SW 格的像与 SE+NE+NW 扇形折叠重叠的三重覆盖区）时：
- SW 格包含 $c$，报出 1 个 $-\sigma$ 真根；
- SE+NE+NW 扇区中，由于折叠包络，有两个 $+\sigma$ 真根（典型地分别位于对角格 SE 与 NW，或邻格与对角格），加上 SW 格的 $-\sigma$ 真根，局部共有 **3 个不同真根** $(+\sigma, +\sigma, -\sigma)$，局部定向和为 $+\sigma + \sigma - \sigma = +\sigma = W$。
- 当 $c$ 距离尖点像 $N_{a,b}$ 足够近（使两个 $+\sigma$ 真根在参数空间的间距 $< \text{\_DEDUP}$）时，距离阈值去重将这两个位于不同分支上的 $+\sigma$ 真根误合二为一，局部只剩 $(+\sigma, -\sigma)$，导致 $\sum \text{orient}$ 比 $w$ 少 $+\sigma$（差 1）。

### 3.2 核心定理与一致性证明

为什么在方案 C（精确内部边谓词）下，去重可以完全不用距离阈值，而改为纯组合判定？我们证明以下两条定理。

#### 引理 1（共边精确符号同一性）
对任意共享竖直内部边 $E_v = N_{i,j} N_{i,j+1}$ 的左右相邻小格 $(i-1, j)$ 与 $(i, j)$，两格存储的端点浮点数完全相同：
$$(x_{10}^{(i-1,j)}, y_{10}^{(i-1,j)}) = (x_{00}^{(i,j)}, y_{00}^{(i,j)}) = \operatorname{fl}(h_i + v_j), \qquad (x_{11}^{(i-1,j)}, y_{11}^{(i-1,j)}) = (x_{01}^{(i,j)}, y_{01}^{(i,j)}) = \operatorname{fl}(h_i + v_{j+1})$$
且两格在 `_test` 中对该共享边调用完全相同的 `_orient(x0, y0, x1, y1, cx, cy)`（顺序同为从 $j$ 到 $j+1$）。水平共享内部边 $E_h = N_{i,j} N_{i+1,j}$（顺序同为从 $i$ 到 $i+1$）同理。

#### 定理 2（共边同向小格交集充要条件）
设两个共享内部边 $E$ 的相邻小格具有**相同的定向** $s \in \{+1, -1\}$。则两格在方案 C 下同时接收画布点 $c$ **当且仅当** $\operatorname{orient2d}(E, c) = 0$（即 $c$ 精确共线于共享边 $E$）。

**证明**：
1. **竖直共享内部边** $E_v = N_{i,j} N_{i,j+1}$，共享于左格 $L = (i-1, j)$ 与右格 $R = (i, j)$，且 $s_L = s_R = s \in \{+1, -1\}$：
   - 左格 $L$ 在右边界 $E_v$ 处的接收条件为（`_test` 中 `hasR` 分支）：
     $$s \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) \ge 0$$
   - 右格 $R$ 在左边界 $E_v$ 处的接收条件为（`_test` 中 `hasL` 分支）：
     $$s \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) \le 0$$
   两式同时成立当且仅当 $s \cdot \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) = 0 \iff \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) = 0$（由引理 1，两格计算的 $\operatorname{orient2d}$ 完全同一）。
2. **水平共享内部边** $E_h = N_{i,j} N_{i+1,j}$，共享于下格 $B = (i, j-1)$ 与上格 $T = (i, j)$，且 $s_B = s_T = s$：
   - 下格 $B$ 在上边界 $E_h$ 处的接收条件为（`hasT` 分支）：$s \cdot \operatorname{orient2d}(N_{i,j}, N_{i+1,j}, c) \le 0$；
   - 上格 $T$ 在下边界 $E_h$ 处的接收条件为（`hasB` 分支）：$s \cdot \operatorname{orient2d}(N_{i,j}, N_{i+1,j}, c) \ge 0$。
   两式同时成立当且仅当 $\operatorname{orient2d}(N_{i,j}, N_{i+1,j}, c) = 0$。 $\blacksquare$

#### 引理 2（`_test` / `cellHit` 边界参数钉死的双向充要性）
在 `_test`（及 JS `cellHit`）中，当点进入边检分支 `not (m < t < 1.0 - m and m < u < 1.0 - m)` 时，将原有的四条内部边判定改为：
```python
if hasL and t <= m:
    o = s * _orient(x00, y00, x01, y01, cx, cy)
    if o > 0:
        continue
    t = 0.0 if o == 0 else (t if t > 0.0 else 1e-300)
if hasR and t >= 1.0 - m:
    o = s * _orient(x10, y10, x11, y11, cx, cy)
    if o < 0:
        continue
    t = 1.0 if o == 0 else (t if t < 1.0 else 1.0 - _U)
if hasB and u <= m:
    o = s * _orient(x00, y00, x10, y10, cx, cy)
    if o < 0:
        continue
    u = 0.0 if o == 0 else (u if u > 0.0 else 1e-300)
if hasT and u >= 1.0 - m:
    o = s * _orient(x01, y01, x11, y11, cx, cy)
    if o > 0:
        continue
    u = 1.0 if o == 0 else (u if u < 1.0 else 1.0 - _U)
```
并移除 `_finish` 中原有的事后浮点截断 `if t < 0.0: if i > 0: t = 0.0 ...`。则对 `_test` 输出的任意命中 `(i, j, t, u)`：
1. 若 `i > 0`（`hasL`），则 $t == 0.0 \iff \operatorname{orient2d}(N_{i,j}, N_{i,j+1}, c) = 0$，否则必有 $t > 0.0$；
2. 若 `i < last_i`（`hasR`），则 $t == 1.0 \iff \operatorname{orient2d}(N_{i+1,j}, N_{i+1,j+1}, c) = 0$，否则必有 $t < 1.0$；
3. 若 `j > 0`（`hasB`），则 $u == 0.0 \iff \operatorname{orient2d}(N_{i,j}, N_{i+1,j}, c) = 0$，否则必有 $u > 0.0$；
4. 若 `j < last_j`（`hasT`），则 $u == 1.0 \iff \operatorname{orient2d}(N_{i,j+1}, N_{i+1,j+1}, c) = 0$，否则必有 $u < 1.0$。

**证明**：
因为预筛选裕度 $m = m_a + m_b(|dx| + |dy|) > 0$：
- 任何使原始浮点参数满足 $t_{\text{fl}} \le 0.0$ 或 $t_{\text{fl}} \ge 1.0$ 的点必满足 $t_{\text{fl}} \le m$ 或 $t_{\text{fl}} \ge 1.0 - m$，从而必然进入上述精确边检分支；
- 反之，若 $\operatorname{orient2d} = 0$，由预筛选裕度定理（见 `fable_band` 推导，$|t_{\text{fl}} - t_{\text{side}}| \le m$），点也必然满足 $t_{\text{fl}} \le m$（左/下边）或 $t_{\text{fl}} \ge 1.0 - m$（右/上边），从而必然进入对应的 `if` 分支并被赋值为 `0.0` 或 `1.0`；
- 当进入分支但 $\operatorname{orient2d} \ne 0$（严格在格内部）时，即使 $t_{\text{fl}} \le 0.0$（或 $\ge 1.0$），三元表达式也会将其夹到 `1e-300 > 0.0`（或 `1.0 - _U < 1.0`），绝不会产生假 `0.0` 或假 `1.0`。
此外，上述改动复用了原本已经在算的 `_orient` 返回值，**没有增加任何一次 `_orient` 调用**。 $\blacksquare$

#### 定理 3（组合去重规则及其在尖点楔形内的无盲区性）
在 `_finish` 中，当 `t == 1.0` 时直接取节点累积弧长 `h_cum[i + 1]`（`u == 1.0` 时取 `v_cum[j + 1]`），并在 `_merge_duplicates` 中定义两条报告 `row = (l_h, l_v, i, j, t, u, s, _)` 与 `prev = (pl_h, pl_v, pi, pj, pt, pu, ps, _)` 重复当且仅当 `s == ps` 且 `same_h and same_v`：
```python
same_h = (i == pi
          or (pi < i and pt == 1.0 and t == 0.0 and (i - pi == 1 or l_h == pl_h))
          or (i < pi and pt == 0.0 and t == 1.0 and (pi - i == 1 or l_h == pl_h)))
same_v = (j == pj
          or (pj < j and pu == 1.0 and u == 0.0 and (j - pj == 1 or l_v == pl_v))
          or (j < pj and pu == 0.0 and u == 1.0 and (pj - j == 1 or l_v == pl_v)))
```
则：
1. **完备去重**：同一原像若落在内部边或内部节点上被多个同向小格同时接收，所有接收格的报告必被合并为 1 条；
2. **尖点楔形零盲区**：对尖点格角 $N_{a,b}$ 附近楔形内的任意点 $c \ne N_{a,b}$，两个同向（$+\sigma$）真根绝不会被合并。

**证明**：
1. 若同一原像落在相邻同向小格共享的竖直内部边 $x = i$ 上，由定理 2 与引理 2，左格 $(i-1, j)$ 必有 $pt = 1.0$，右格 $(i, j)$ 必有 $t = 0.0$，且 `j == pj`，故 `same_h` 与 `same_v` 均为 `True`；水平内部边同理。若原像精确落在节点 $N_{a,b}$ 上，则环绕 $N_{a,b}$ 的所有同向小格在指向 $N_{a,b}$ 的两条内部边上均满足 $\operatorname{orient2d} = 0$，其 $(t, u)$ 精确钉死在共享角点 $\{0.0, 1.0\}^2$ 上，任意两格之间 `same_h` 与 `same_v` 均为 `True`（构成完全图团 clique），因此无论中间是否穿插反向格报告，都必然合并为 1 条。
2. 在尖点格角 $N_{a,b}$ 处（3 个 $+\sigma$ 格与 1 个 $-\sigma$ 格）：
   - 紧邻奇异格（$-\sigma$）的两条边是**折缝边**（两侧定向相反），另外两条边是连接三个 $+\sigma$ 格的**非折缝内部边** $E_1, E_2$。
   - 在参数平面上，三个 $+\sigma$ 格沿 $E_1, E_2$ 拼接成一个局部单叶（1-to-1）的 L 形区域，其边界为两条折缝边；在尖点楔形内部（$c \ne N_{a,b}$），点 $c$ 在该 L 形区域中有**两个不同的原像**：一个位于 $E_1$ 侧折叠页，另一个位于 $E_2$ 侧折叠页（分别落在对角格如 SE 与 NW，或一格内部与对角格内部）。
   - 因为 $E_1$ 与 $E_2$ 分别是两条非平行的内部边，它们唯一的交点就是格角节点 $N_{a,b}$。当 $c \ne N_{a,b}$ 时，$c$ 不可能同时落在 $E_1$ 与 $E_2$ 上；而两个对角 $+\sigma$ 格（如 SE $(a-1, b-1)$ 与 NW $(a, b)$）之间既不共列（$i \ne pi$）也不共行（$j \ne pj$），它们仅共享角点 $N_{a,b}$。由于 $c \ne N_{a,b}$，两格不可能在角点 $N_{a,b}$ 的两条边上同时满足 $\operatorname{orient2d} = 0$，因此 `same_h and same_v` **恒为 `False`**！
   - 于是，无论 $c$ 离尖点像 $N_{a,b}$ 多近（哪怕 $10^{-15}\text{ px}$），两个 $+\sigma$ 真根都绝不会被合并。 $\blacksquare$

### 3.3 五类边界与退化情形逐项核查

按任务要求，逐一核实格角、外延格、首末段、零长度段与被剔除的近平行格：

1. **格角（Grid corners）**：
   - **平直格角**（4 格同向，无折缝）：当 $c = N_{a,b}$ 时，4 格的 $(t, u)$ 分别为 $(1,1), (0,1), (1,0), (0,0)$，两两满足 `same_h and same_v`，4 条报告合并为 1 条；当 $c$ 落在其中一条边上（非节点）时，共享该边的 2 格合并为 1 条。
   - **单折缝穿过格角**（2 格 $+1$、2 格 $-1$，绕数 $W = 0$）：当 $c = N_{a,b}$ 时，2 个 $+1$ 格合并为 1 个 $+1$ 根，2 个 $-1$ 格合并为 1 个 $-1$ 根，保留一对 $(+1, -1)$，和为 0。
   - **双折缝格角**（SW, NE 为 $+1$，SE, NW 为 $-1$，绕数 $W = 0$）：楔形内 $c \ne N_{a,b}$ 有 4 个真根 $(+1, +1, -1, -1)$，对角同向格因 $c \ne N_{a,b}$ 不满足 `same_h and same_v`，4 个真根全部保留（消除了旧 `_DEDUP` 将两对同向根同时误合并的盲区）。
   - **尖点格角**（3 格 $+\sigma$、1 格 $-\sigma$，绕数 $W = +\sigma$）：楔形内 $c \ne N_{a,b}$ 的 3 个真根 $(+\sigma, +\sigma, -\sigma)$ 全部保留（0 盲区）。唯独当 $c = N_{a,b}$ 精确等于奇异顶点本身时，3 个 $+\sigma$ 格共享同一参数点 $(s_a, t_b)$ 并合并为 1 个 $+\sigma$，见第 6.1 节详细说明。
2. **外延格（Extension cells）**：
   - **同格重复报告**：在 `_pairs_grid` 中，当 `extensions="all"` 或 `sheet` 未命中 `orient0` 时，边界格 $(i, j)$ 可能先在 `sheet` 中被测一次，又在 `_extension_candidates` 中被测一次。两者具有完全相同的 `(i, j)`，`same_h`（`i == pi`）与 `same_v`（`j == pj`）直接为 `True`，必合并。
   - **相邻外延格共享内部射线边**：例如顶排外延格 $(i, \text{last\_j})$ 与 $(i+1, \text{last\_j})$ 共享竖直内部射线 $x = i+1$（$u > 1$ 无界延伸）。当远点 $c$ 精确落在该射线上时，两格在竖直边上算得相同的 `_orient == 0`，故左格 `pt == 1.0`、右格 `t == 0.0`，且行号相同 `j == pj == last_j`，因此 `same_h and same_v` 为 `True`。**这比旧 `_DEDUP` 更稳健**：旧法在远场外延区（$u \gg 1$）会因两格各自的浮点 $u$ 舍入差异超过 `_DEDUP` 而漏去重，组合法则不受 $u$ 浮点误差影响！
3. **首末段（First and last segments）**：
   - 首段 $i = 0$ 的左侧（`hasL == False`）和末段 $i = \text{last\_i}$ 的右侧（`hasR == False`）是开放外延侧，不跑 `_orient`。
   - 会不会因为点恰好在 $i = 0$ 处算得浮点 `t == 0.0`（或 $i = \text{last\_i}$ 处算得 `t == 1.0`）而误触发跨格合并？**绝不会**！因为在 `same_h` 的跨格条件 `pi < i and pt == 1.0 and t == 0.0` 中，既然 $0 \le pi < i \le \text{last\_i}$，就必然有 $pi < \text{last\_i}$（故 $pi$ 的右侧必为内部边 `hasR == True`）且 $i > 0$（故 $i$ 的左侧必为内部边 `hasL == True`）！开放外延侧永远不可能是两个小格之间的共享边。
4. **零长度段（Zero-length segments）**：
   - 设引导线 $H$ 的第 $k$ 段长度为 0（即 $h_{k+1} = h_k$，$r_k = (0, 0)$）。此时列 $k$ 的所有小格因 `den == 0` 在 `_cell` 中返回 `None` 被剔除，但累积弧长满足 `h_cum[k+1] == h_cum[k]`，且列 $k-1$ 的右边界节点 $N_{k,j} = h_k + v_j$ 与列 $k+1$ 的左边界节点 $N_{k+1,j} = h_{k+1} + v_j$ **完全相等**。
   - 当点 $c$ 精确落在共享边 $N_{k,j} N_{k,j+1}$ 上时，列 $k-1$ 钉死 `pt = 1.0`（弧长 `pl_h = h_cum[k] - h_arc`），列 $k+1$ 钉死 `t = 0.0`（弧长 `l_h = h_cum[k+1] - h_arc = pl_h`）。
   - `same_h` 中的条件 `(i - pi == 1 or l_h == pl_h)` 在 `pt == 1.0 and t == 0.0` 的前提下检查 `l_h == pl_h`，从而跨越任意多个连续零长度段将 $(k-1, j)$ 与 $(k+1, j)$ 正确合并（已在 `t_chord_intersect.py` 第 `(e)` 段验证）。
5. **被剔除的近平行格（Culled near-parallel cells）**：
   - **存活的极窄格**（$10^{-12} < |\sin\theta| \le 10^{-8}$）：在 `probe_extra.py` 的 `thin` 装置中，小格极扁（$\det$ 极小），当点落在其长边上时，沿长边的浮点参数误差可达 $|l_v^{(1)} - l_v^{(2)}| > 10^{-6} = \text{\_DEDUP}$，旧距离去重可能漏合并；而组合去重中两格共享水平边（`i == pi` 且 `pu == 1.0, u == 0.0`），完全不依赖横向浮点参数精度，100% 精确合并。
   - **被剔除的退化平行格**（段长非零但 $|\sin\theta| \le 10^{-12}$）：在 `_cell` 中被跳过，见第 6.2 节说明。

---

## 四、测量前后对比与验收输出

### 4.1 修改前后核心指标对比表

#### （1）Python 测试与探针对比（基线方案 C vs 本次修改后）

| 测试项 / 探针集 | 指标 | 修改前（方案 C 基线） | 修改后（组合去重） | 说明 |
| :--- | :--- | ---: | ---: | :--- |
| `t_chord_intersect.py` (a) `bow100` 384 条折缝边 | `sum(orient) != w` | 0 / 384 | **0 / 384** | 折缝两侧 < `_DEDUP` 的双真根均保留 |
| `t_chord_intersect.py` (b) `bow100` 80 个尖点角 5200 探针 | `off_by_one` (`sum != w`) | 3472 / 5200 | **0 / 5200** | **尖点去重盲区 100% 消除** |
| `probe_band.py` `bow100` (b) 300 格角 × 8 方向 (40800 探针) | `law` (`sum != w`) | 151 | **0** | 全部格角邻域守恒律 0 违例 |
| `probe_band.py` `bow100` (b') 80 尖点角 (5200 探针) | `law` (`sum != w`) | 3472 | **0** | 对照 `Fraction` 真值：`false=0, lost=0` |
| `probe_band.py` `bow60` (b) 300 格角 × 8 方向 (40800 探针) | `law` (`sum != w`) | 205 | **0** | 全部格角邻域守恒律 0 违例 |
| `probe_band.py` `bow60` (b') 114 尖点角 (7410 探针) | `law` (`sum != w`) | 5167 | **0** | 对照 `Fraction` 真值：`false=0, lost=0` |
| `probe_band.py` `S1.5` (b) 300 格角 × 8 方向 (40800 探针) | `law` (`sum != w`) | 188 | **0** | 全部格角邻域守恒律 0 违例 |
| `probe_band.py` `S1.5` (b') 80 尖点角 (5200 探针) | `law` (`sum != w`) | 3799 | **0** | 对照 `Fraction` 真值：`false=0, lost=0` |
| `probe_extra.py` `bow100` `on_lattice` `edge-plain` (4850 点) | `law` (`sum != w`) | 25 | **0** | 靠近尖点的非折缝边点盲区全部消除 |
| `probe_extra.py` `bow100` `on_lattice` `node` (5000 点) | `law` (`sum != w`) | 122 | **1** | 121 个 $\pm\text{ulp}$ 楔形点盲区消除；仅剩 1 个精确奇异尖点顶点 |
| `probe_extra.py` `thin` / `bow100` / `guide-end` / `ablation` | `law / false / lost / diff` | 0 / 0 / 0 / 0 | **0 / 0 / 0 / 0** | 窄格、远场、端线与无预筛消融全部 0 差异 |

#### （2）三张 JS 说明页对比（`page_dedup_check.js` 与 `page_band_check.js`）

| 页面与探针集 | 指标 | 修改前（方案 C 基线） | 修改后（组合去重） |
| :--- | :--- | ---: | ---: |
| 三页 `page_dedup_check.js 5000`（所有预设，折缝 + 随机） | `law fails` | 0 | **0** |
| `w_orient0_explainer.html` `node`（71,944 探针 × 各预设） | `law / false / lost` | `bow60`: 13, `bow100`: 19, `hairpin`: 1 | **全部 0 / 0 / 0** |
| `fold_severing_map.html` `node`（71,944 探针 × 各预设） | `law` | `bow60`: 1, `bow100`: 2 | **全部 0** |
| `fold_inverse_explainer.html` `node`（49,096 探针 × 各预设） | `law / false / lost` | `bow60`: 8, `bow100`: 5, `hairpin`: 1 | **全部 0 / 0 / 0** |
| **三页 `TOTAL (fold+plain+node+rand)` 汇总** | **`law / odd / false / lost`** | **`law 50, odd 0, false 0, lost 0`** | **`law 0, odd 0, false 0, lost 0`** |
| 三页 `lattReg`（非奇异格点/边 $\pm 1\text{ ulp}$） | `root missed / law / false / lost` | `0 / 0 / 0 / 0` | **`0 / 0 / 0 / 0`** |
| 三页 `lattCrit`（折缝/尖点 $\pm 1\text{ ulp}$） | `false / lost-vs-truth` | `0 / 0` | **`0 / 0`**（`law` 从 640 降至 184，剩余全为精确落在奇异点/边上的 `raw-sum != w` 情形） |

#### （3）`tests/bench.py` 每点耗时对比（4000 点，5 轮交错中位数，单位：$\mu\text{s}$/点）

| 装置 | 路径 | 方案 C 基线 (`old`) | 方案 C + 组合去重 (`new`) | 比值 `new / old` |
| :--- | :--- | ---: | ---: | ---: |
| `bow 100 px` | `grid`（默认惰性外延） | 6.1 $\mu\text{s}$ | **6.1 $\mu\text{s}$** | **1.00x** |
| `bow 100 px` | `gridall`（含全外延） | 9.5 $\mu\text{s}$ | **10.6 $\mu\text{s}$** | **1.11x** |
| `bow 100 px` | `np`（`solve_many` 批量） | 227.3 $\mu\text{s}$ | **213.9 $\mu\text{s}$** | **0.94x** |
| `bow 100 px` | `mat`（纯 Python 全矩阵） | 855.9 $\mu\text{s}$ | **856.9 $\mu\text{s}$** | **1.00x** |
| `bow 60 px` | `grid` / `gridall` | 4.8 / 6.7 $\mu\text{s}$ | **4.9 / 6.6 $\mu\text{s}$** | **1.02x / 0.99x** |
| `bow 10 px` | `grid` / `gridall` | 2.3 / 4.6 $\mu\text{s}$ | **2.4 / 4.8 $\mu\text{s}$** | **1.04x / 1.05x** |

> 耗时结论：由于 `_test` 中的边界钉死仅在进入 `not (m < t < 1.0 - m and m < u < 1.0 - m)` 分支时复用已算出的 `_orient` 结果，且 `_merge_duplicates` 仅在 `len(rows) > 1` 时对极少数命中行（通常 $2 \sim 5$ 行）做整数/布尔比较，每点耗时与方案 C 基线持平（在测量噪声 $\pm 5\%$ 以内）。

---

### 4.2 验收命令与完整输出记录

#### （1）`tests/t_chord_intersect.py`
命令：
```powershell
C:\Users\admin\Documents\AnimeAn\tools\python312\python.exe tests/t_chord_intersect.py
```
输出：
```text
straight frame: 625 probes, one crossing each, equals the chord-basis seed
bow  10 px: 625 probes | Newton solved 625, all of them among the crossings (625), principal == Newton on 625 | points with >=2 preimages on the guides 0, with 0 0
bow  30 px: 625 probes | Newton solved 625, all of them among the crossings (625), principal == Newton on 625 | points with >=2 preimages on the guides 0, with 0 0
bow  60 px: 625 probes | Newton solved 571, all of them among the crossings (571), principal == Newton on 560 | points with >=2 preimages on the guides 22, with 0 68
round trip: 300 random arcs -> hv -> crossings recover the arcs
matrix path and pair loop agree on 81 probes
a point past the guide ends: found on the straight extensions, hv maps it back
straight:      631 points (3 outside the grid, 0 with several sheet preimages): grid hits == matrix, principal == matrix
bow 10 px:     631 points (146 outside the grid, 0 with several sheet preimages): grid hits == matrix, principal == matrix
bow 30 px:     631 points (147 outside the grid, 0 with several sheet preimages): grid hits == matrix, principal == matrix
bow 60 px:     631 points (140 outside the grid, 145 with several sheet preimages): grid hits == matrix, principal == matrix
bow 100 px:    631 points (141 outside the grid, 320 with several sheet preimages): grid hits == matrix, principal == matrix
S stroke 1.0x: 631 points (138 outside the grid, 34 with several sheet preimages): grid hits == matrix, principal == matrix
S stroke 1.5x: 631 points (145 outside the grid, 169 with several sheet preimages): grid hits == matrix, principal == matrix
bins of 2, 8 and 25 px and the batch grid path: same hits
merge rule: node duplicates merged, crease pairs kept, cusp-wedge near roots kept, zero-length segment bridged
bow 100 px, 384 fold edges (vertical and horizontal): next to each, two genuine roots within _DEDUP of each other are both kept and sum(orient) == w = -1; merging by distance alone broke the law at all 384
bow 100 px, 80 cusp corners, 5200 probes along the odd sectors: sum(orient) == w at all 5200 (off_by_one = 0, Fraction truth mismatches = 0; _DEDUP threshold missed by one at 3472, distance-only missed by two at 38)
bow 100 px, 2000 random probes: sum(orient) == w on every one (100 checked against Fraction truth: 0 false, 0 lost)
Fraction truth checks: 400 fold-edge probes, 360 non-fold lattice probes, 360 fold-crease lattice probes (173 on 0-layer side verified absent in Fraction truth): 0 false, 0 lost, exact boundary pinning verified
zero-length segment & path identity: grid, numpy, and matrix raw tuples agree bit-for-bit and bridge zero-length segments
t_chord_intersect: ok
```

#### （2）`unittest discover -s tests`
命令：
```powershell
C:\Users\admin\Documents\AnimeAn\tools\python312\python.exe -m unittest discover -s tests
```
输出：
```text
.
----------------------------------------------------------------------
Ran 1 test in 81.700s

OK
```

#### （3）`node page_dedup_check.js 5000` 与 `node page_band_check.js`
命令：
```powershell
node docs/plan/2026-10-28-格哈希论文新颖性调查/lean_w_orient0_parity/page_dedup_check.js 5000
node docs/plan/2026-10-28-格哈希论文新颖性调查/lean_w_orient0_parity/page_band_check.js
```
输出（`page_dedup_check.js 5000`）：
```text
=== w_orient0_explainer.html
straight: w=1; 0 fold edges, 0 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 0); 5000 random probes: law fails 0 (previous rule 0)
bow30: w=1; 0 fold edges, 0 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 0); 5000 random probes: law fails 0 (previous rule 0)
bow60: w=1; 80 fold edges, 4640 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 1360); 5000 random probes: law fails 0 (previous rule 0)
bow100: w=-1; 96 fold edges, 5568 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 1848); 5000 random probes: law fails 0 (previous rule 0)
teaching: w=-1; 0 fold edges, 0 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 0); 5000 random probes: law fails 0 (previous rule 0)
hairpin: w=0; 12 fold edges, 696 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 168); 5000 random probes: law fails 0 (previous rule 0)
hook: w=0; 1 fold edges, 58 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 18); 5000 random probes: law fails 0 (previous rule 0)

=== fold_severing_map.html
bow60: w=1; 80 fold edges, 4640 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 720); 5000 random probes: law fails 0 (previous rule 0)
bow100: w=-1; 96 fold edges, 5568 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 1080); 5000 random probes: law fails 0 (previous rule 0)
teaching: w=-1; 0 fold edges, 0 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 0); 5000 random probes: law fails 0 (previous rule 0)
hairpin: w=0; 12 fold edges, 696 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 72); 5000 random probes: law fails 0 (previous rule 0)
hook: w=0; 1 fold edges, 58 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 10); 5000 random probes: law fails 0 (previous rule 0)

=== fold_inverse_explainer.html
straight: w=1; 0 fold edges, 0 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 0); 5000 random probes: law fails 0 (previous rule 0)
bow30: w=1; 0 fold edges, 0 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 0); 5000 random probes: law fails 0 (previous rule 0)
bow60: w=1; 88 fold edges, 5104 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 848); 5000 random probes: law fails 0 (previous rule 0)
bow100: w=-1; 80 fold edges, 4640 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 944); 5000 random probes: law fails 0 (previous rule 0)
hairpin: w=0; 12 fold edges, 696 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 104); 5000 random probes: law fails 0 (previous rule 0)
hook: w=0; 1 fold edges, 58 probes next to them (+0 on a one-sided EPS band, not counted): law fails 0 (previous rule 9); 5000 random probes: law fails 0 (previous rule 0)

page_dedup_check: ok
```
输出（`page_band_check.js`）：
```text
=== w_orient0_explainer.html
straight (w=1): plain 19872: law 0, false 0, lost 0, raw-sum!=w 0 | node 71944: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 14697: law 0, false 0, lost 0, root missed 0, raw-sum!=w 5957
bow30 (w=1): plain 19872: law 0, false 0, lost 0, raw-sum!=w 0 | node 71944: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 14697: law 0, false 0, lost 0, root missed 0, raw-sum!=w 1330
bow60 (w=1): fold 4640: law 0, odd 0, false 0, lost 0, raw-sum!=w 0 | plain 18432: law 0, false 0, lost 0, raw-sum!=w 0 | node 71944: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 13257: law 0, false 0, lost 0, root missed 0, raw-sum!=w 1533 | lattCrit 1440: law 23, false 0, lost 0, root missed 585, raw-sum!=w 45
bow100 (w=-1): fold 5568: law 0, odd 0, false 0, lost 0, raw-sum!=w 0 | plain 18144: law 0, false 0, lost 0, raw-sum!=w 0 | node 71944: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 13005: law 0, false 0, lost 0, root missed 0, raw-sum!=w 1823 | lattCrit 1692: law 74, false 0, lost 0, root missed 652, raw-sum!=w 206
teaching (w=-1): plain 19872: law 0, false 0, lost 0, raw-sum!=w 0 | node 71944: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 14697: law 0, false 0, lost 0, root missed 0, raw-sum!=w 792
hairpin (w=0): fold 696: law 0, odd 0, false 0, lost 0, raw-sum!=w 0 | plain 19656: law 0, false 0, lost 0, raw-sum!=w 0 | node 71944: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 14499: law 0, false 0, lost 0, root missed 0, raw-sum!=w 1661 | lattCrit 198: law 3, false 0, lost 0, root missed 56, raw-sum!=w 25
hook (w=0): fold 58: law 0, odd 0, false 0, lost 0, raw-sum!=w 0 | plain 522: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 261: law 0, false 0, lost 0, root missed 0, raw-sum!=w 57 | lattCrit 9: law 0, false 0, lost 0, root missed 3, raw-sum!=w 0

=== fold_severing_map.html
bow60 (w=1): fold 4640: law 0, odd 0, raw-sum!=w 0 | plain 18432: law 0, raw-sum!=w 0 | node 71944: law 0, raw-sum!=w 0 | rand 5000: law 0, raw-sum!=w 0
bow100 (w=-1): fold 5568: law 0, odd 0, raw-sum!=w 0 | plain 18144: law 0, raw-sum!=w 0 | node 71944: law 0, raw-sum!=w 0 | rand 5000: law 0, raw-sum!=w 0
teaching (w=-1): plain 19872: law 0, raw-sum!=w 0 | node 71944: law 0, raw-sum!=w 0 | rand 5000: law 0, raw-sum!=w 0
hairpin (w=0): fold 696: law 0, odd 0, raw-sum!=w 0 | plain 19656: law 0, raw-sum!=w 0 | node 71944: law 0, raw-sum!=w 0 | rand 5000: law 0, raw-sum!=w 0
hook (w=0): fold 58: law 0, odd 0, raw-sum!=w 0 | plain 522: law 0, raw-sum!=w 0 | rand 5000: law 0, raw-sum!=w 0

=== fold_inverse_explainer.html
straight (w=1): plain 13680: law 0, false 0, lost 0, raw-sum!=w 0 | node 49096: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 10089: law 0, false 0, lost 0, root missed 0, raw-sum!=w 4085
bow30 (w=1): plain 13680: law 0, false 0, lost 0, raw-sum!=w 0 | node 49096: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 10089: law 0, false 0, lost 0, root missed 0, raw-sum!=w 609
bow60 (w=1): fold 5104: law 0, odd 0, false 0, lost 0, raw-sum!=w 0 | plain 12096: law 0, false 0, lost 0, raw-sum!=w 0 | node 49096: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 8577: law 0, false 0, lost 0, root missed 0, raw-sum!=w 806 | lattCrit 1512: law 58, false 0, lost 0, root missed 535, raw-sum!=w 142
bow100 (w=-1): fold 4640: law 0, odd 0, false 0, lost 0, raw-sum!=w 0 | plain 12240: law 0, false 0, lost 0, raw-sum!=w 0 | node 49096: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 8685: law 0, false 0, lost 0, root missed 0, raw-sum!=w 950 | lattCrit 1404: law 22, false 0, lost 0, root missed 526, raw-sum!=w 69
hairpin (w=0): fold 696: law 0, odd 0, false 0, lost 0, raw-sum!=w 0 | plain 13464: law 0, false 0, lost 0, raw-sum!=w 0 | node 49096: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 9891: law 0, false 0, lost 0, root missed 0, raw-sum!=w 1300 | lattCrit 198: law 4, false 0, lost 0, root missed 87, raw-sum!=w 16
hook (w=0): fold 58: law 0, odd 0, false 0, lost 0, raw-sum!=w 0 | plain 1062: law 0, false 0, lost 0, raw-sum!=w 0 | rand 5000: law 0, false 0, lost 0, raw-sum!=w 0 | lattReg 531: law 0, false 0, lost 0, root missed 0, raw-sum!=w 99 | lattCrit 9: law 0, false 0, lost 0, root missed 3, raw-sum!=w 0

TOTAL (fold+plain+node+rand): law 0 (unmerged raw: 0), odd 0, false 0, lost 0; lattice regular: root missed 0, law 0; lattice on creases/cusps: root missed 2447
```

#### （4）`tests/probe_band.py` 与 `tests/probe_extra.py`
命令：
```powershell
C:\Users\admin\Documents\AnimeAn\tools\python312\python.exe tests/probe_band.py chord_intersect bow100,bow60,S1.5
C:\Users\admin\Documents\AnimeAn\tools\python312\python.exe tests/probe_extra.py chord_intersect
```
输出（`probe_band.py`）：
```text
=== bow100 (chord_intersect): n_h=100 n_v=100 orient0=+1 w=-1
(a) fold edges  384      : probes  31488 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near    48 | truth-sum != w 0 | raw-sum != w 0
(n) non-fold edges  400  : probes  10400 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(b) corners  300 x 8 dirs: probes  40800 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(b') cusp corners   80   : probes   5200 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(c) random               : probes   2000 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
paths: grid raw vs numpy raw (500 probes) and vs matrix raw (60): 0 mismatches (bitwise tuples)
time 22.4 s

=== bow60 (chord_intersect): n_h=100 n_v=100 orient0=+1 w=1
(a) fold edges  424      : probes  34768 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near    16 | truth-sum != w 0 | raw-sum != w 0
(n) non-fold edges  400  : probes  10400 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(b) corners  300 x 8 dirs: probes  40800 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(b') cusp corners  114   : probes   7410 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(c) random               : probes   2000 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
paths: grid raw vs numpy raw (500 probes) and vs matrix raw (60): 0 mismatches (bitwise tuples)
time 19.7 s

=== S1.5 (chord_intersect): n_h=100 n_v=100 orient0=+1 w=-1
(a) fold edges  314      : probes  25748 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near    32 | truth-sum != w 0 | raw-sum != w 0
(n) non-fold edges  400  : probes  10400 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(b) corners  300 x 8 dirs: probes  40800 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(b') cusp corners   80   : probes   5200 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
(c) random               : probes   2000 | law     0 | probes with false roots     0 (false reports     0) | with lost roots    0 (   0) | odd-near     0 | truth-sum != w 0 | raw-sum != w 0
paths: grid raw vs numpy raw (500 probes) and vs matrix raw (60): 0 mismatches (bitwise tuples)
time 20.1 s
exact fallbacks taken: 16
```
输出（`probe_extra.py`）：
```text
bow100     exact-on-lattice roots (+-2 ulp nudges): node: 5000 pts, root missed 73, law 1, false 0, lost-vs-truth 0; edge-fold: 150 pts, root missed 83, law 0, false 0, lost-vs-truth 0; edge-plain: 4850 pts, root missed 0, law 0, false 0, lost-vs-truth 0
straight   exact-on-lattice roots (+-2 ulp nudges): node: 5000 pts, root missed 0, law 0, false 0, lost-vs-truth 0; edge-fold: 0 pts, root missed 0, law 0, false 0, lost-vs-truth 0; edge-plain: 5000 pts, root missed 0, law 0, false 0, lost-vs-truth 0
thin       w=1: 5300 probes near sides (+far): law 0, false reports 0, lost vs truth 0
bow100     w=-1: 5300 probes near sides (+far): law 0, false reports 0, lost vs truth 0
bow100     guide-end probes 2800: lazy/all/matrix disagreements 0
S1.5       guide-end probes 2800: lazy/all/matrix disagreements 0
bow100*    w=-1: 3700 probes near sides (+far): law 0, false reports 0, lost vs truth 0
ablation bow100: 3700 probes, prefilter vs none differ on 0
thin*      w=1: 3700 probes near sides (+far): law 0, false reports 0, lost vs truth 0
ablation thin: 3700 probes, prefilter vs none differ on 0
exact fallbacks taken: 726
```

#### （5）`tests/bench.py`
命令：
```powershell
C:\Users\admin\Documents\AnimeAn\tools\python312\python.exe tests/bench.py 100
C:\Users\admin\Documents\AnimeAn\tools\python312\python.exe tests/bench.py 60
C:\Users\admin\Documents\AnimeAn\tools\python312\python.exe tests/bench.py 10
```
输出：
```text
bow 100 px, 4000 points (median of 5 interleaved passes), us per point:
  grid     old       6.1   new       6.1   new/old 1.00
  gridall  old       9.5   new      10.6   new/old 1.11
  np       old     227.3   new     213.9   new/old 0.94
  mat      old     855.9   new     856.9   new/old 1.00
  sheet candidates per point (mean): 21.4; exact fallbacks taken: 0

bow 60 px, 4000 points (median of 5 interleaved passes), us per point:
  grid     old       4.8   new       4.9   new/old 1.02
  gridall  old       6.7   new       6.6   new/old 0.99
  np       old     208.5   new     209.9   new/old 1.01
  mat      old     855.1   new     844.3   new/old 0.99
  sheet candidates per point (mean): 23.8; exact fallbacks taken: 0

bow 10 px, 4000 points (median of 5 interleaved passes), us per point:
  grid     old       2.3   new       2.4   new/old 1.04
  gridall  old       4.6   new       4.8   new/old 1.05
  np       old     222.0   new     214.9   new/old 0.97
  mat      old     850.2   new     860.0   new/old 1.01
  sheet candidates per point (mean): 4.7; exact fallbacks taken: 0
```

---

## 五、对下游的影响

1. **奇偶校验命题 $n_+ - n_- = w \cdot \text{orient}_0$ 在所有正则点上真正成为零盲区不变量**：
   - 修改前，文档第 8.3 节不得不为尖点楔形保留一个距离 $\sim \text{\_DEDUP}$ 的例外盲区（`off_by_one > 0`）；
   - 修改后，只要画布点 $p$ 不零误差地重合于有限个奇异点/奇异折缝线（`orient2d == 0`），奇偶守恒律 $\sum \text{orient} = w$ 在任意靠近折缝和任意靠近尖点楔形（低至 $10^{-12}\text{ px}$ 乃至机器精度）处**全部精确成立**。
2. **主支选择（`principal`）与多根计数（`count` / `hits`）**：
   - 在尖点楔形内部，当 $\text{orient}_0$ 恰好等于三个同向格的符号 $+\sigma$ 时，楔形内原本有 **2 个** 正向真根（不同分支）。旧 `_DEDUP` 规则在靠近尖点时会把其中一个正向真根误删，使候选集只剩 1 个，剥夺了 `chord_seed` 距离择优选择另一正向分支的机会；组合去重后，两个正向真根完整保留，`principal` 始终在完整的正向真根集合中按 `chord_seed` 择优。
   - 在折缝边界上，保持 $\tau = 0$ 确保了零层阴影侧绝不产生虚假的 `principal`，拓扑裁断边界清晰锐利。
3. **三张交互说明页（`w_orient0_explainer.html`、`fold_inverse_explainer.html`、`fold_severing_map.html`）**：
   - 鼠标悬停或拖动探针深入尖点楔形尖端时，原像列表不再发生“两个正向点突然跳变成一个、守恒律指示灯变红报错”的假象。

---

## 六、仍然处理不了的情形（已核实的边界）

1. **画布点精确重合于尖点奇异顶点 $c = N_{a,b}$ 本身（或尖点邻接边上 $\operatorname{orient2d} == 0$）**：
   - **现象**：在 `probe_extra.py` 的 `bow100` 5000 个节点探针中，有且仅有 **1 个点**（`law = 1`），以及 `page_band_check.js` 的 `lattCrit` 中若干 `dx=0, dy=0` 的点，使得合并后的 $\sum \text{orient} \ne w$。
   - **已核实的数学原因**：当点 $c$ 在二进制浮点下**一比特不差地等于尖点格角节点 $N_{a,b} = \operatorname{fl}(h_a + v_b)$** 时，环绕 $N_{a,b}$ 的 4 个小格（3 个 $+\sigma$、1 个 $-\sigma$）在指向 $N_{a,b}$ 的内部边上均满足 `_orient == 0`，4 个小格全部命中。在拓扑度理论中，$c = N_{a,b}$ 是映射的**临界值（分歧点 / branch point）**：楔形内的两个 $+\sigma$ 分支和一个 $-\sigma$ 分支在参数空间中坍缩到了**同一个格点 $(s_a, t_b)$** 上，代数重数（multiplicity）为 $(+\sigma) + (+\sigma) + (-\sigma) = +\sigma$。但由于求解器返回的是参数平面上的不同原像点集合（不附带拓扑重数），3 个 $+\sigma$ 小格在 $(s_a, t_b)$ 处的报告确实代表同一个参数点 $(s_a, t_b)$，合并后剩下 1 个 $+\sigma$ 与 1 个 $-\sigma$，点集定向和为 $0 \ne +\sigma$。
   - **为什么不强行凑数**：如果为了在 $c = N_{a,b}$ 这个零测度奇异点上让 $\sum \text{orient} = w$ 而不合并同向格，那么在所有普通平直格角（4 个 $+1$ 格汇合于 $N_{a,b}$，真实只有 1 个 $+1$ 根）上就会多报出 2～3 个重复假根！区分平直格角与尖点格角唯一正确的概念是为原像引入“局部绕数重数”，而对于仅需参数原像位置的求逆器，将同一参数点 $(s_a, t_b)$ 的同向报告合并为 1 个原像是最自然且一致的定义。
2. **非零段长但夹角 $|\sin\theta| \le 10^{-12}$ 的退化近平行小格**：
   - `_cell` 中当 $|\det(r_i, q_j)| \le 10^{-12} (\|r_i\|\|q_j\| + 10^{-300})$ 时返回 `None`。若输入引导线不是零长度段（零长度段面积为 0，已由组合去重中的 `l_h == pl_h` 桥接），而是两条长度非零但几乎完全平行的线段，则该退化格在画布上围出的极细长条带会被跳过。在横纵正交布局的引导线装置中从未触发。
