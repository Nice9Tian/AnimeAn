# Argon 研究：EPS 接受带不等宽的改法

2026-10-30，Gemini 4 Argon（high），只读运行，读了 PROBLEM.md 与 chord_intersect.py、说明页 solve、回归、验收脚本、失败样例。第一轮在 agy 的 30 分钟上限内超时未作答，接着原会话要求不再读文件、直接作答，第二轮交回下文（状态仍标超时，正文完整）。原文未改动。

---
### 1. 可选方案、一致性证明与五条约束影响

问题的根源有两个：(1) 几何上，段参数容差 `_EPS` 映射到画布法向距离是 $\text{EPS}\cdot |\text{den}_{i,j}| / \|e\|$，随两格夹角正弦 $|\text{den}_{i,j}|$ 变化；(2) 浮点上，格 $i$ 在 $t=1$ 处用左端点 $N_{i,j}=h_i+v_j$ 算 $t\le 1+\text{EPS}$，格 $i+1$ 在 $t=0$ 处用 $N_{i+1,j}=h_{i+1}+v_j$ 算 $t\ge -\text{EPS}$，两格没有在公共边上共享同一个边方程求值。

#### 方案一：共享四条边叉积（半平面符号）直接判定（推荐）
将小格 $(i,j)$ 的平行四边形四个边界统一写成以四个格点 $N_{i,j}, N_{i+1,j}, N_{i,j+1}, N_{i+1,j+1}$ 为锚点的有向叉积：
- 竖向边 $i$（$h_i$ 处）：$E^v_{i,j} = (c - N_{i,j}) \times s_j$；竖向边 $i+1$（$h_{i+1}$ 处）：$E^v_{i+1,j} = (c - N_{i+1,j}) \times s_j$（其中 $s_j = -q_j$）。
- 横向边 $j$（$v_j$ 处）：$E^h_{i,j} = (c - N_{i,j}) \times r_i$；横向边 $j+1$（$v_{j+1}$ 处）：$E^h_{i,j+1} = (c - N_{i,j+1}) \times r_i$。
- 设 $\sigma_{i,j} = \text{sign}(\text{den}_{i,j}) = -\text{orient}_{i,j}$（因 $\text{den}_{i,j} = r_i \times (-q_j) = -r_i \times q_j$）。格 $(i,j)$ 在实数上满足 $0 \le t \le 1 \iff \sigma_{i,j} E^v_{i,j} \ge 0 \land \sigma_{i,j} E^v_{i+1,j} \le 0$（首段无下界则省去 $E^v_{0,j}$ 比较，末段无上界则省去 $E^v_{n_h,j}$ 比较；$u$ 同理）。

* **一致性证明（依赖的浮点性质）**：
  1. **IEEE-754 运算确定性**：共边两格 $(i,j)$ 与 $(i+1,j)$ 对公共边 $i+1$ 计算的是**完全相同的浮点表达式** $E^v_{i+1,j} = (c_x - N_{i+1,j,x}) s_{j,y} - (c_y - N_{i+1,j,y}) s_{j,x}$，得出的浮点数 $E$ 逐位相同。
  2. **非折缝边不漏根（依赖 IEEE-754 非 NaN 浮点数全序性 $E \le 0 \lor E \ge 0$）**：同向两格 $\sigma_{i,j} = \sigma_{i+1,j} = \sigma$。格 $i$ 要求 $\sigma E \le 0$，格 $i+1$ 要求 $\sigma E \ge 0$。对任意浮点值 $E$，必有其一成立（$E = \pm 0.0$ 时两者均成立并由 [`_merge_duplicates`] 合并），**无需 EPS 带即可保证跨边零缝隙**。
  3. **折缝边零层彻底无假根（依赖相反数符号翻转）**：折缝两侧 $\sigma_{i+1,j} = -\sigma_{i,j}$。格 $i$ 的内侧条件 $\sigma_{i,j} E \le 0$ 与格 $i+1$ 的内侧条件 $\sigma_{i+1,j} E \ge 0 \iff -\sigma_{i,j} E \ge 0 \iff \sigma_{i,j} E \le 0$ **完全等价**。因此在零层侧（$\sigma_{i,j} E > 0$）两格**同时拒绝**，在叠合侧（$\sigma_{i,j} E < 0$）两格**同时接受**。

#### 方案二：共享边叉积 + 画布等宽容差带 $\delta_{\text{canvas}}$
仍用方案一的公共边函数 $E^v, E^h$，但保留接受带：将判定放宽为 $\sigma_{i,j} E^v_{i,j} \ge -\delta \|s_j\|$ 且 $\sigma_{i,j} E^v_{i+1,j} \le \delta \|s_j\|$（例如 $\delta = 10^{-6}\text{ px}$，等价于参数容差按 $\delta \|s_j\| / |\text{den}_{i,j}|$ 随格缩放）。
* **一致性证明**：公共边两侧对比的是同一个浮点数 $E^v_{i+1,j}$ 与同一个阈值 $\pm \delta \|s_j\|$。在折缝零层侧，两格接受条件同为 $\sigma_{i,j} E^v_{i+1,j} \le \delta \|s_j\|$，**由单边接受变为严格同步接受/同步拒绝**，消除单边假根。

#### 方案三：参数空间保持 `_EPS`，在 `_finish` 中过滤无内部支撑的折缝越界根
不改求交循环，在 [`_finish`] 钳位前标记内部段是否越界（如 $i>0 \land t<0$ 或 $i<n_h-1 \land t>1$）。若某越界根所在边是折缝边，直接丢弃该越界部分。
* **缺陷**：由于 $t = (d_{i,j} \times s_j)/\text{den}_{i,j}$ 在 $t=1$ 处受除法和基准点舍入影响，恰好落在折缝边上的点可能在一格算得 $t = 1 + 10^{-16}$、另一格算得 $t = -10^{-16}$，按严格 $[0,1]$ 丢弃越界会导致折缝边真根偶发漏报。

#### 对五条约束的影响对比

| 约束 | 方案一（共享边叉积，0 容差） | 方案二（共享边叉积 + 画布等宽 $\delta$） | 方案三（后处理过滤折缝越界） |
|---|---|---|---|
| **1. 格边/格角不漏根** | **满足**（全序性保证共边/共角无缝衔接） | **满足**（有 $\delta$ 接受带兜底） | **不完全满足**（折缝上精确点可能因除法舍入双双判越界） |
| **2. grid/matrix/numpy 逐位一致** | **满足**（三路径改用同一套边叉积公式） | **满足** | **满足** |
| **3. 外延与 `on_sheet` 正确** | **满足**（首末段用 `0`/`last` 边叉积精确切分面与外延） | **满足** | **满足** |
| **4. 性能与 JS 同步** | **满足**（先做叉积符号过滤，省去绝大多数格的除法，更快） | **满足** | **满足** |
| **5. 消掉零层假根对（个数错 2）** | **彻底消除**（零层侧两格均不接受） | **不消除**（$[0,\delta]$ 内仍成对出现） | **消除** |

---

### 2. 推荐方案与实现要点

**推荐方案一**（若担心网格预筛选桶边界或折缝顶点精确命中，可在方案一基础上仅对折缝叠合侧开放极小容差，但纯方案一已兼具消除单边假根与消除零层假根对的优势，且最快）。

#### 关键判定式（伪代码）
预计算阶段存储节点坐标 $h_i, v_j$（注意为保证相邻格在公共边上 `nx, ny` 逐位一致，统一定义为 $N_{i,j} = (h_{i,x} + v_{j,x}, h_{i,y} + v_{j,y})$，其中 $i \in [0..n_h], j \in [0..n_v]$）：

```python
# 对格 (i, j)，den = rx * sy - ry * sx，记 sgn = 1.0 if den > 0.0 else -1.0
# 竖向边叉积 E_v(k, j) = (cx - (h[k].x + v[j].x)) * sy - (cy - (h[k].y + v[j].y)) * sx
# 横向边叉积 E_h(i, m) = (cx - (h[i].x + v[m].x)) * ry - (cy - (h[i].y + v[m].y)) * rx

ev0 = (cx - nx0) * sy - (cy - ny0) * sx          # k = i
ev1 = (cx - nx1) * sy - (cy - ny1) * sx          # k = i + 1
if i > 0 and sgn * ev0 < 0.0: continue           # t < 0 (非首段)
if i < last_i and sgn * ev1 > 0.0: continue      # t > 1 (非末段)

eh0 = (cx - nx0) * ry - (cy - ny0) * rx          # m = j
eh1 = (cx - nx0_1) * ry - (cy - ny0_1) * rx      # m = j + 1 (锚点 h[i] + v[j+1])
if j > 0 and sgn * eh0 < 0.0: continue           # u < 0 (非首段)
if j < last_j and sgn * eh1 > 0.0: continue      # u > 1 (非末段)

# 通过边界测试后再做除法求弧长参数：
t = ev0 / den
u = eh0 / den
on_sheet = (sgn * ev0 >= 0.0) and (sgn * ev1 <= 0.0) and (sgn * eh0 >= 0.0) and (sgn * eh1 <= 0.0)
```
*(若采用方案二保留等宽带，只需把 `< 0.0`、`> 0.0` 换为 `< -EPS_PX * q_len`、`> EPS_PX * q_len`，其中 `q_len = hypot(sx, sy)`，横向同理换为 `r_len`)。*

#### 需修改的函数与三路径/双语言对齐
1. **Python 逐格与全矩阵**：
   - [`ChordIntersect.prepare_grid`]：候选元组 `cand` 存入右/上角点 `nx1 = h[i+1][0]+v[j][0], ny1 = ...` 及 `nx0_1 = h[i][0]+v[j+1][0], ny0_1 = ...`，以及标志位 `is_sheet_only`。对于首末格拆出的面内专属元组（原 388 行），将其四界限标记为全部启用。[`_family`] 中 `lo/hi` 改为 `[0.0, 1.0]`（桶边界已有 `1e-7` 裕量）。
   - [`ChordIntersect._test`] 与 [`ChordIntersect._pairs_python`]：按上述伪代码用完全相同的加减乘顺序算 `ev0, ev1, eh0, eh1`，即可逐位一致。
   - [`ChordIntersect._finish`]：`on_sheet` 由 `ev0, ev1, eh0, eh1` 的符号决定（或由 `raw` 传入），内部段将 `t, u` 钳位到 `[0.0, 1.0]`。
2. **Numpy 路径逐位一致**：
   - 当前 [`_prepare_numpy`] 用了展开式 `cxS - K1`，这与逐格路径 `(cx - nx)*sy - (cy - ny)*sx` 相差一次分配律舍入！
   - 要使 [`_pairs_numpy`] 与 [`_test`] 逐位一致，在 [`_prepare_numpy`] 中构造全节点数组 `N = H[:, None, :] + V[None, :, :]`（形状 `(n_h+1, n_v+1, 2)`），在 [`_pairs_numpy`] 中算 `d = c[:, None, None, :] - N[None, ...]`，再切片算 `ev = d[:, :, :-1, 0]*S[None, None, :, 1] - d[:, :, :-1, 1]*S[None, None, :, 0]`（形状 `(m, n_h+1, n_v)`），则 `ev[:, :-1, :]` 和 `ev[:, 1:, :]` 分别是全部格的 `ev0` 和 `ev1`，浮点指令顺序与标量完全一致。
3. **JS 说明页对齐**：
   - 在 [`page_geometry_w_orient0.js`] 的 [`build`] 中为每个 cell 增存 `nx1, ny1, nx01, ny01`，在 [`solve`] 中使用同样的 `ev0, ev1, eh0, eh1` 判定（IEEE-754 双精度在 JS `Number` 与 Python `float` 间逐位相同）。

---

### 3. 仍处理不了的情形与验收办法

#### 仍处理不了的情形
1. **格角附近的 `_DEDUP` 盲区（尖点格角与双折缝格角）**：
   - **方案一**消除了格角处由 `_EPS` 接受带引起的第四格假根报告，但**距离格角像 $\sim \text{DEDUP}$ 以内的尖点楔形区**内，三个真根中同向的两个在弧长空间距离小于 `_DEDUP`，仍会被 [`_merge_duplicates`] 合并成一个，使 $\sum \text{orient}$ 偏离 $w$ 达 $\pm 1$（详见 [`doc_8_5_and_8_7.md`]）。
   - 若采用**方案二**（有 $\delta$ 带），格角处两条共交边的接受带交集在不同格内夹角不同，角顶附近 $O(\delta)$ 极小菱形区域内仍存在角效应。
2. **退化近平行格（$|\text{den}| \le 10^{-12} \|r_i\|\|q_j\|$）**：
   - 被 `continue` 跳过（[`chord_intersect.py`]），若探针落在该退化格与邻格的公共边附近，退化格不参与报告，守恒律在此处不闭合。
3. **面与外延分界 / 首末段**：
   - 首段 $i=0$ 格与内段 $i=1$ 格在 $h_1$ 处共边（均由 $E^v_{1,j}$ 控制），外延分界本身在 $h_0$（$E^v_{0,j}=0$）与 $h_{n_h}$（$E^v_{n_h,j}=0$）。方案一在同一格内用 $E^v_{0,j}$ 的符号区分 `on_sheet` 与外延，无跨格裂缝；但若 `extensions="lazy"`，当面内已有同向真根时不会再去查外延上的额外折叠根（这是 `lazy` 的设计语义）。

#### 验收办法
1. **说明页验收（[`page_dedup_check.js`]）**：
   - 去掉或保留第 90 行 `if (near % 2 === 1) { res.band++; continue; }` 豁免，运行 `node page_dedup_check.js`：三张页面的 `res.band`（原 612 + 612 + 388 = 1612）、`res.fail`、`res.randomFail` 应**全部为 0**。
   - 核对 [`failing_probes_bow100_page.txt`] 中的 40 条折缝边探针，确认零层侧 `raw near` 为空、`sum == w (-1)`。
2. **Python 回归（[`t_chord_intersect.py`]）**：
   - 原有 6 段测试全部通过；在 384 条折缝边测试（第 309–349 行）中，补测零层侧（`-side`）全部 41 个 `deltas`（$10^{-10}\dots 10^{-5}\text{ px}$），断言 `len(near) == 0` 且 `sum(h.orient for h in res.hits) == w`；补测精确落在非折缝格边/格角上的点 `hv(h_cum[i] - h_arc, v_cum[j] - v_arc)` 不漏根。

---

### 4. 代码核对结论与推测的区分

* **已核对代码的结论**：
  1. [`chord_intersect.py`] 中 [`_test`] 与 [`_pairs_python`] 算式完全一致，但 [`_pairs_numpy`] 用了预展开的 `(cxS - _K1) / den`，在边界 $0$ 附近与标量路径存在 1 ulp 舍入差；[`t_chord_intersect.py`] 目前只测了 81 个稀疏点因此未触发。
  2. [`failing_probes_bow100_page.txt`] 中所有失败样例（如 `edge 4 6 side 1 d 1.0e-7` 只报了 `[4,6,...,-1]`，或 `edge 4 17` 额外叠合了另一处折缝的成对假根 `[4,16,...,1]` 与 `[5,18,...,-1]`）均发生在 $d \in [5.6\times 10^{-8}, 5.6\times 10^{-7}]\text{ px}$，与 $\text{EPS}\cdot |\text{den}|/\|e\|$ 的理论带宽完全吻合。
  3. [`prepare_grid`] 哈希入桶带有 `eps = 1e-6`、[`_family`] 带有 `1e-7` 的桶索引裕量，因此格边上的点在网格路径中必定包含两侧候选格，不会在分桶阶段漏格。
* **推测（未改文件运行验证）**：
  1. 仓库外层的三张 HTML（[`page_dedup_check.js`] 引用的 `../w_orient0_explainer.html` 等）不在当前子目录内，推测其 `solve` 结构与 [`page_geometry_w_orient0.js`] 相同。
  2. 方案一去掉 `_EPS` 接受带后，[`t_chord_intersect.py`] 第 389 行的断言 `assert off_by_one > 0` 在 Python 侧（`_DEDUP = 1e-6`）仍会成立（因尖点扇形角平分线在 $d = 10^{-12}\dots 10^{-7}\text{ px}$ 处三个真根间距小于 `1e-6`），但 `off_by_one` 的探针数会从 3746 个减少（原本由第 4 格 `_EPS` 假根引起的偏差消失，只剩纯 `_DEDUP` 合并引起的偏差）。

