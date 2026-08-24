# Fukusato Mapping：面向服装线稿的交互式矢量纹理映射

本文档以技术论文的形式说明 AnimeAn 中 Fukusato Mapping 的问题定义、关联研究、方法、实现边界与验证方式。目标论文为 Fukusato 等人的 *Interactive texture editing for garment line drawings* [1]。本文中的“论文方法”专指原论文提出的 UV 编辑框架；“本实现”专指当前仓库的可执行实现。两者存在的工程差异在第 6 节单独列出。

## 摘要

服装线稿中的褶皱和自遮挡会在二维图像中形成不连续结构，普通自由形变通常会让纹理连续穿过这些结构，产生平面化或错误连接。Fukusato 等人提出在服装线稿的建模面板上直接放置并移动点/曲线手柄，将手柄重心投影到 UV 空间，再以带测地权重的移动最小二乘（Moving Least Squares, MLS）求解 UV 形变。AnimeAn 复现了该核心流程，并将几何处理集中在 Python：从闭合服装区域建立含孔洞和折角线约束的二维三角网格，通过折角线顶点扇区复制形成拓扑切缝，以网格最短路近似切缝感知的测地距离，求解反向刚性或相似 MLS，最后以逐三角形仿射映射把 ChildView 中的矢量描边和奇偶填充准确回写到 MainView。系统还加入独立折角线工具、勾/叉确认事务、双视图权重热力图和三角形拓扑浏览。该实现复现论文的交互 UV 编辑核心，但不声称复现论文使用的神经网络初始 UV、κ-Curve 前端、连续测地求解器或用户研究。

**关键词：** 服装线稿；纹理映射；UV 编辑；移动最小二乘；约束 Delaunay 三角化；测地距离；矢量图形

## 1. 引言

传统纹理编辑通常要求作者在模型空间和 UV 空间之间反复切换。对于服装线稿，褶皱线两侧在二维坐标上可能非常接近，在服装表面的拓扑意义上却应彼此分离。仅使用欧氏距离的形变会跨越褶皱传播，不能表达这种不连续性。

目标论文的关键思想有三点：

1. 用户直接在服装线稿上操作点或曲线手柄，不必在 UV 面板中反向推测对应位置。
2. 手柄通过三角网格的重心坐标投影到 UV 空间，并以刚性 MLS 变形 UV，而不改变服装线稿本身。
3. 服装网格沿折角线切开；权重由测地距离和欧氏距离线性混合，使用户能够在“严格感知折角”和“平滑跨越折角”之间调节。

本仓库进一步解决了论文未规定的生产问题：如何从 AnimeAn 的矢量线稿生成受约束网格，如何把拓扑切缝落实为可计算的数据结构，如何在不栅格化纹理的情况下回写矢量描边与填充，以及如何让用户在正式变形前检查影响范围。

## 2. 关联研究

### 2.1 线稿纹理映射与直接 UV 编辑

自由形变（Free-Form Deformation, FFD）以规则控制格驱动几何或图像变形 [2]。它适合连续区域，但精细编辑需要大量控制点，而且规则格本身不能自然表达服装褶皱产生的拓扑不连续。Noh 和 Igarashi 的 inverse FFD 从用户给定的 UV 约束反推控制格 [3]，降低了直接编辑大量格点的负担，但仍属于基于连续控制格的参数化方法。

Gingold 等人提出直接在三维表面上操纵纹理约束 [5]，减少模型视图与 UV 视图之间的认知切换。Fukusato 等人把这一直接操作思想移植到二维服装线稿，并增加曲线手柄 [1]。AnimeAn 延续这一交互范式：手柄只在 MainView 上创作和移动，ChildView 用于显示纹理及当前 UV 拓扑。

原论文要求输入服装已经具有初始 UV，并在实验数据中采用 Hashimoto 等人的神经纹理方法 [4]；论文同时明确把平面投影列为可用的初始化方式 [1, §3]。当前实现采用平面投影，因此复现的是后续交互编辑算法，而不是 Hashimoto 网络及其训练数据。

### 2.2 曲线手柄与移动最小二乘变形

Schaefer、McPhail 和 Warren 提出的 MLS 图像变形支持仿射、相似和刚性变换，并可由点或线段约束驱动 [6]。对每个查询位置，MLS 依据距离生成局部权重，计算约束的加权质心，并求出最符合局部约束的变换。Fukusato 的式 (1) 将点约束扩展为曲线参数上的积分，是本方法的数学核心。

原论文的交互前端使用 κ-Curves 将离散点击点连接为曲率极值位于控制点的光滑曲线 [7]。AnimeAn 当前直接使用作者绘制的矢量笔迹作为曲线，并按弧长均匀采样；因此 MLS 目标一致，但曲线创作前端并非 κ-Curve 的逐点复刻。

### 2.3 折角感知距离

连续曲面上的距离场可由 Eikonal 方程的黏性解框架 [8] 或 Heat Method [9] 等方法求得。原论文引用这两类工作，并把测地权重与欧氏权重线性混合，以避免纯测地权重在折角附近产生过强畸变 [1, §4.2]。

当前实现采用 Dijkstra 最短路 [10] 计算切缝三角网格边图上的离散测地距离。它不等价于 Heat Method 的连续距离，但具有实现简单、与复制顶点形成的硬拓扑屏障自然兼容、结果确定等优点。网格加密时，边图距离通常能更细致地近似服装域内的路径距离，但数值结果仍依赖三角网格的分辨率和边方向。

### 2.4 约束三角化与拓扑切缝

论文假设输入服装是沿折角线切开的二维三角模型，但没有规定从矢量线稿构造该模型的算法。当前实现使用 Shewchuk 的 Triangle [11]：服装外轮廓、孔洞轮廓和折角线共同组成平面直线图（Planar Straight-Line Graph, PSLG），再执行带最小角和最大面积约束的相容 Delaunay 三角化。这样，轮廓和折角线不会仅仅“靠近”网格边，而会成为三角化的真实约束边。

单纯把折角标记为边仍不足以阻止图最短路跨越其端点。当前实现进一步按折角边切断局部三角形扇区，并为互不连通的扇区复制顶点。复制点具有相同二维位置和初始 UV，但具有不同顶点编号，因而在邻接图和逐三角形回写中属于不同的拓扑片。

### 2.5 矢量填充三角化与裁剪

AnimeAn 的纹理不只包含栅格像素，还包含矢量描边和带孔洞填充。本实现使用 Mapbox Earcut 的 Python 绑定对奇偶规则填充进行带孔洞多边形三角化 [12]，再使用 Sutherland-Hodgman 凸窗口裁剪 [13] 求每个源填充三角形与变形 UV 三角形的交集。Earcut 以实时性和工程鲁棒性为目标，并不保证修复任意自交或非法多边形，因此输入路径仍应是有效的简单环及合法嵌套孔洞。

## 3. 问题定义

### 3.1 输入与输出

对当前帧，算法接收：

- MainView 中由闭合线稿围成的服装域 $\Omega\subset\mathbb{R}^2$；
- 独立折角线工具提供的折角折线集合 $\mathcal C$；
- ChildView 中位于 UV/纹理坐标系的矢量描边与填充 $\mathcal P$；
- 用户在 MainView 中给出的手柄原始曲线 $v_i(t)$ 与拖动后曲线 $v'_i(t)$，$t\in[0,1]$；
- 参数 $\alpha$、$\beta$、网格精度、采样数和 MLS 变体。

算法输出：

1. 服装网格每个顶点的新 UV 坐标 $U_j$；
2. 由新 UV 场映射到 MainView 的矢量纹理结果；
3. 可持久化的手柄、网格和 UV 解，用于再次编辑和预览。

### 3.2 符号

| 符号 | 含义 |
| --- | --- |
| $\mathcal M=(V,T)$ | 服装三角网格，$V$ 为顶点，$T$ 为三角形 |
| $X_j\in\mathbb R^2$ | 顶点 $j$ 在 MainView/建模面板中的固定坐标 |
| $U_j^0\in\mathbb R^2$ | 顶点 $j$ 的初始 UV；当前实现令 $U_j^0=X_j$ |
| $U_j\in\mathbb R^2$ | MLS 求解后的 UV |
| $v_i(t),v'_i(t)$ | 第 $i$ 条手柄在拖动前、拖动后的面板曲线 |
| $p_i(t),q_i(t)$ | $v_i(t),v'_i(t)$ 经重心坐标投影得到的 UV 曲线 |
| $d_g,d_e$ | 切缝网格测地距离和面板欧氏距离 |
| $\alpha\in[0,1]$ | 测地/欧氏权重混合系数 |
| $\beta>0$ | 距离权重衰减指数 |
| $F_j$ | 顶点 $j$ 处由 MLS 得到的局部反向 UV 映射 |

### 3.3 基本假设

- 手柄种子位于一个可由桶式区域检测识别的闭合服装区域内。
- 同一帧的全部手柄属于同一个闭合服装区域；不同裁片分别在不同帧求解，避免把互不相干的面板约束混入同一个 MLS 系统。
- 外轮廓和孔洞轮廓为可三角化的简单环；非法自交路径不在保证范围内。
- 折角线可以与服装边界相交，也可以在服装内部形成开放端点或交叉；位于服装外的部分会被裁掉。
- 手柄采样点只有在拖动前、拖动后都能定位到服装网格时才参与求解；若所有采样均无效，本次确认失败且保留旧输出。

## 4. 方法

### 4.1 总体流程

方法可概括为以下九个阶段：

```text
输入：服装线稿、折角线、ChildView 矢量纹理、已接受手柄、待确认手柄
  1. 以手柄中点为种子检测闭合服装域和孔洞
  2. 构建外轮廓 + 孔洞 + 折角线的 PSLG
  3. 执行约束三角化，并沿折角线复制局部顶点扇区
  4. 按弧长离散每条手柄，将 before/after 样本重心投影到初始 UV
  5. 在切缝网格边图上计算每个 after 样本的 Dijkstra 距离
  6. 混合测地/欧氏权重，对每个网格顶点求反向刚性或相似 MLS
  7. 建立“变形 UV 三角形 -> 固定面板三角形”的逐片仿射映射
  8. 裁剪并回写 ChildView 的矢量描边与填充
  9. 仅在全部成功后替换旧输出、保存 UV 解并提交历史记录
输出：新 UV 场和 MainView 矢量纹理层
```

待确认手柄在第 6--9 步之前只存在于覆盖层。权重预览执行第 1--5 步及第 6 步的权重构建，但不执行 MLS 或矢量回写，也不修改作品。

### 4.2 服装域提取与约束 Delaunay 三角化

系统调用 `auto_mapping._detect_region`，把当前帧的可见、非映射输出描边转换为线段，并以手柄中点执行桶式区域检测。包含种子的最大面积环记为外轮廓；位于外轮廓内且不包含种子的其他环作为候选孔洞。

外轮廓、孔洞及裁剪到 $\Omega$ 内的折角线被写成 PSLG。轮廓段使用边界标记，折角段使用独立折角标记。`triangle` 的生产调用为：

```text
pDq20aA
```

其中 `p` 表示输入 PSLG，`D` 请求相容 Delaunay 网格，`q20` 约束最小角，`aA` 约束最大三角形面积 $A$。设外轮廓包围盒宽、高为 $W,H$，网格参数为 $g$，本实现取

$$
A=\max\left(\frac{WH}{2g^2},\ 0.25\right).
$$

因此 `grid` 不是规则网格的边数，而是用于推导目标面积密度的分辨率参数。Triangle 可以插入 Steiner 点；输出三角形只覆盖外轮廓内部并排除孔洞，同时保留折角约束段。

### 4.3 折角线的拓扑切缝

设折角约束边集合为 $E_c$。对位于 $E_c$ 上的顶点 $x$，收集所有关联三角形，并仅通过“不属于 $E_c$ 的共享边”连接这些三角形。由此得到局部三角形扇区的连通分量 $K_1,\ldots,K_r$。

- 若 $r=1$，无需复制。
- 对开放折角的内部端点，保持单一共享顶点，使切缝在端点处闭合。
- 对到达服装边界的端点、折角内部点、闭环或交叉点，为 $K_2,\ldots,K_r$ 各复制一个同坐标顶点，并把对应三角形改指向复制顶点。

复制后的各顶点满足

$$
X_{x^{(1)}}=\cdots=X_{x^{(r)}},\qquad
U^0_{x^{(1)}}=\cdots=U^0_{x^{(r)}},
$$

但顶点编号和三角形邻接不同。后续边图只从实际三角形生成，因此不存在跨切缝边；逐三角形纹理回写也不会把切缝两侧错误地串成同一条描边。

折角线的采集、存储和删除完全位于 `crease_line_tool.py`。映射模块只读取 $\mathcal C$，不包含神经网络预测逻辑；更换人工、规则或神经预测来源时不需要改动 MLS 求解器。

### 4.4 初始 UV 与手柄重心投影

当前实现采用平面投影：

$$
U_j^0=X_j.
$$

对服装域内任一点 $x$，定位其所在三角形 $\tau=(a,b,c)$，求重心坐标 $\lambda_a,\lambda_b,\lambda_c$：

$$
x=\lambda_aX_a+\lambda_bX_b+\lambda_cX_c,
\qquad
\lambda_a+\lambda_b+\lambda_c=1.
$$

该点在初始 UV 场中的投影为

$$
U^0(x)=\lambda_aU_a^0+\lambda_bU_b^0+\lambda_cU_c^0.
$$

于是，第 $i$ 条手柄的两组 UV 约束为

$$
p_i(t)=U^0(v_i(t)),\qquad q_i(t)=U^0(v'_i(t)).
$$

这里始终从 `base_uv` 投影所有已接受手柄，而不是在上一次结果上继续累积误差。每次确认都会从同一基准网格重新求解全部已接受约束。

### 4.5 曲线积分离散化

原论文的 MLS 目标包含 $\int_0^1\cdot\,dt$。当前实现先在每条笔迹上构造累计弧长，再进行等弧长重采样。最长手柄使用参数 `samples` 指定的样本数；其他手柄按长度使用相同近似采样间距；长度小于一个像素的手柄作为单点约束。

若第 $i$ 条手柄保留 $N_i$ 个有效样本，则每个样本的积分测度为

$$
m_{ik}=\frac{1}{N_i},\qquad k=1,\ldots,N_i.
$$

因此每条手柄的总测度为 1，较长曲线通过更多样本描述形状，但不会仅因样本更多而获得更大的总权重。

### 4.6 切缝感知权重

将第 $i$ 条手柄的第 $k$ 个拖动后面板样本记为 $s_{ik}=v'_i(t_{ik})$。对查询顶点 $X_j$，欧氏距离为

$$
d_e(X_j,s_{ik})=\lVert X_j-s_{ik}\rVert_2.
$$

测地距离由切缝网格边图计算。算法先定位 $s_{ik}$ 所在三角形，以样本到该三角形三个顶点的真实欧氏距离作为多源初值，再运行 Dijkstra：

$$
d_g(X_j,s_{ik})=\min_{r\in\tau(s_{ik})}
\left(\lVert s_{ik}-X_r\rVert_2+\operatorname{dist}_{G}(r,j)\right).
$$

定义未经积分测度缩放的两类权重

$$
\widetilde w^e_{ik}(X_j)=
\frac{1}{\max(d_e(X_j,s_{ik}),\varepsilon)^{2\beta}},
$$

$$
\widetilde w^g_{ik}(X_j)=
\begin{cases}
\dfrac{1}{\max(d_g(X_j,s_{ik}),\varepsilon)^{2\beta}}, & d_g<\infty,\\
0, & d_g=\infty,
\end{cases}
$$

其中 $\varepsilon=10^{-6}$。最终权重复现论文 §4.2 的线性混合：

$$
w_{ik}(X_j)=m_{ik}\left[(1-\alpha)\widetilde w^g_{ik}(X_j)
+\alpha\widetilde w^e_{ik}(X_j)\right].
$$

- $\alpha=0$：纯测地权重，折角线是硬影响屏障；
- $\alpha=1$：纯欧氏权重，忽略拓扑切缝；
- $0<\alpha<1$：在不连续表达与局部平滑之间插值；
- $\beta$ 越大，影响越集中在手柄附近。$\beta$ 是本实现显式提供的 MLS 衰减参数，原论文没有固定其数值。

若 $\alpha=0$ 且某个连通分量被切缝与全部手柄完全隔离，则该分量的所有权重保持为零，MLS 按恒等映射保留其原 UV。这样不会用隐式欧氏回退穿过硬折角屏障。若用户希望隔离片仍受手柄影响，应显式令 $\alpha>0$；此时上式中的欧氏项会提供跨切缝支持。

权重以拖动后的样本 $s_{ik}$ 为距离源，因为求解的定义域控制点是 $q_i$。这保留了 MLS 在 $q_i\mapsto p_i$ 方向的插值极限；有限 $\varepsilon$ 下为数值近似插值。

### 4.7 反向刚性/相似 MLS

纹理应跟随用户把手柄从 $v_i$ 拖到 $v'_i$。由于当前 UV 表示的是“面板位置到纹理位置”的逆映射，求解方向必须是

$$
q_i(t)\longmapsto p_i(t),
$$

即从拖动后手柄 UV 映回拖动前手柄 UV。这对应原论文式 (1)：

$$
M_j^*=\arg\min_{M\in\mathcal G}
\sum_i\int_0^1 w_i(t;X_j)
\left\|(q_i(t)-q_j^*)M-(p_i(t)-p_j^*)\right\|^2dt,
$$

其中 $\mathcal G=SO(2)$ 时为刚性 MLS；允许统一缩放时为相似 MLS。离散后，把所有样本展平为 $\ell$，令源点 $a_\ell=q_\ell$、目标点 $b_\ell=p_\ell$。对每个查询顶点 $j$，加权质心为

$$
a_j^*=\frac{\sum_\ell w_{j\ell}a_\ell}{\sum_\ell w_{j\ell}},\qquad
b_j^*=\frac{\sum_\ell w_{j\ell}b_\ell}{\sum_\ell w_{j\ell}}.
$$

代码把二维点表示为复数，并计算

$$
S_j=\sum_\ell w_{j\ell}(b_\ell-b_j^*)
\overline{(a_\ell-a_j^*)},\qquad
\mu_j=\sum_\ell w_{j\ell}|a_\ell-a_j^*|^2.
$$

刚性变换的闭式解为

$$
F_j^{\mathrm{rigid}}(z)=b_j^*+(z-a_j^*)\frac{S_j}{|S_j|},
$$

相似变换为

$$
F_j^{\mathrm{similarity}}(z)=b_j^*+(z-a_j^*)\frac{S_j}{\mu_j}.
$$

最终 UV 为

$$
U_j=F_j(U_j^0).
$$

默认使用论文实现中的刚性变体。相似变体是仓库提供的扩展，对应原论文在未来工作中提出的统一缩放需求。若 $|S_j|$ 或 $\mu_j$ 退化，代码退回质心平移 $b_j^*+(z-a_j^*)$；若总权重无效，则保持 $z$ 不变。

### 4.8 逐三角形矢量纹理回写

论文主要讨论 UV 坐标编辑和导出。AnimeAn 需要把 ChildView 的矢量纹理直接生成到 MainView，因此本实现增加了一个与线性 UV 场一致的精确分片回写阶段。

对网格三角形 $\tau=(a,b,c)$，定义唯一仿射映射 $A_\tau$：

$$
A_\tau(U_a)=X_a,\qquad
A_\tau(U_b)=X_b,\qquad
A_\tau(U_c)=X_c.
$$

对位于变形 UV 三角形内的纹理点 $y$，求其关于 $(U_a,U_b,U_c)$ 的重心坐标，再以同一组坐标组合 $(X_a,X_b,X_c)$，即可得到 $A_\tau(y)$。因此“精确”是指相对于离散后的折线/多边形和分片线性 UV 场不存在额外栅格近似，并不表示恢复原始 Bézier 曲线的解析形式。

**描边。** 系统为所有变形 UV 三角形包围盒建立均匀空间索引，并把查询范围裁到已占用的索引格，避免远离服装的纹理线触发与坐标距离成比例的空格遍历。每个纹理线段只与候选三角形做半平面参数裁剪；裁剪片段经 $A_\tau$ 映射后，仅在以下条件同时满足时串接：源参数连续、映射后端点连续、三角形相同或共享非切缝边、UV 三角形行列式符号相同。复制顶点使折角两侧不再邻接，因此跨折角线的纹理会真正断开。共享边上的重复片段只有在两侧仿射结果一致时才去重；重叠 UV 片的不同图像均保留。单点描边不经过线段裁剪，而以重心坐标映射到每个覆盖该 UV 点的片层；普通描边的颜色、线宽及 Qt 画笔样式一并保留。

**填充。** 系统把路径解析为环，根据奇偶规则计算嵌套层级，将奇数层环分配为最近偶数层外环的孔洞，再用 Earcut 三角化。每个源填充三角形通过 Sutherland-Hodgman 算法与候选 UV 三角形求交，交多边形经 $A_\tau$ 映射并以原颜色写回。

**翻转与线宽。** 若

$$
\det[U_b-U_a,\ U_c-U_a]<0,
$$

该片标记为 `fukusato_mapped_back`，与正向片分类保存；算法检测但不自动修复翻转。收集 ChildView 描边时按底层到顶层排列，以匹配合并到一个输出图像后的绘制顺序。描边宽度使用全局近似比例

$$
s_w=\frac{\sum_{\tau}\sum_{(a,b)\in\partial\tau}\|X_a-X_b\|}
{\sum_{\tau}\sum_{(a,b)\in\partial\tau}\|U_a-U_b\|},
$$

并设置为原宽度乘 $s_w$。这保持整体视觉尺度，但不是逐三角形各向异性笔宽变换。

### 4.9 勾/叉确认事务

手柄工作流被实现为显式事务：

1. 用户用 `Fukusato Guide / 引导线` 在 MainView 绘制手柄。
2. 完笔后，原笔迹立即从作品层移除，并以可拖动覆盖层保存；`before=after`，状态为 `pending`。
3. 覆盖层右上角显示勾和叉。每帧最多有一条待确认手柄。
4. 拖动只在内存中临时平移覆盖层的 `after` 曲线，不求解 UV、不修改输出纹理，也不覆盖已持久化的手柄状态；只有鼠标释放后才保存这次编辑。工具切换或帧切换会发送取消事件并恢复拖动前状态。
5. 点击勾后，把手柄标记为已接受，从基准网格重新求解该帧全部已接受手柄，并执行矢量回写。
6. 新输出层完整生成后才删除旧的 Fukusato 输出层；任一步失败都会丢弃新层并保留旧结果。
7. 点击叉会删除新手柄；若正在修改一条已接受手柄，则恢复其修改前位置并记录可撤销的取消操作。删除已接受手柄时，系统用剩余手柄重新求解。

原论文在拖动时实时更新 UV；“确认前不变形”是本项目为支持影响预览和可撤销提交而有意采用的交互差异。

### 4.10 权重预览与三角形拓扑浏览

`Fukusato Mapping > Weight Preview / 权重预览` 只显示当前待确认手柄。对顶点 $j$，先计算总支持度

$$
h_j=\sum_{i,k}w_{ik}(X_j),
$$

再对正值取 $\log_{10}$。为避免手柄附近的奇异大权重压缩其余颜色，使用 5% 和 95% 分位数 $Q_{0.05},Q_{0.95}$ 归一化：

$$
r_j=\operatorname{clamp}\left(
\frac{\log_{10}h_j-Q_{0.05}}{Q_{0.95}-Q_{0.05}},0,1\right).
$$

每个三角形取三个顶点 $r_j$ 的平均值，并使用“蓝 - 青 - 黄 - 红”的半透明色带。MainView 以 $X_j$ 绘制，ChildView 以当前已保存的 $U_j$ 绘制，因此两侧展示同一权重场在两个坐标域中的对应关系。

`Triangle Topology / 三角形拓扑浏览` 对每个无向顶点编号边只绘制一次。MainView 显示受约束服装网格，ChildView 显示当前 UV 网格。由于切缝两侧使用不同顶点编号，即使几何坐标重合，拓扑浏览仍会保留两套边。

### 4.11 状态、缓存与历史

手柄及每帧解使用命名空间 `fukusato_mapping` 持久化；折角线使用独立命名空间 `fukusato_creases`。每帧 UV 解同时保存网格序列化结果，只有当前网格与已保存网格完全一致时才复用 UV。内存网格缓存键包含帧号、外轮廓、孔洞、折角线和 `grid`；修改折角线或相关参数会使缓存失效。确认、移动、删除均接入 AnimeAn 历史记录，撤销/重做后重新生成覆盖层。

## 5. 参数、复杂度与实现追踪

### 5.1 参数

| 参数 | 默认值 | 可选范围 | 作用 |
| --- | ---: | ---: | --- |
| `alpha` | 0.0 | 0.0--1.0 | 0 为纯测地，1 为纯欧氏 |
| `beta` | 2.0 | 0.1--4.0 | 权重指数中的 $2\beta$，越大越局部 |
| `grid` | 32 | 8--64 | 通过 $A=WH/(2g^2)$ 控制网格密度 |
| `samples` | 16 | 2--64 | 最长手柄的目标样本数 |
| `variant` | `rigid` | `rigid` / `similarity` | MLS 局部变换族 |

`alpha=0` 最忠实地表达折角拓扑，但可能在折角附近形成更强畸变；逐渐增加 `alpha` 会使结果平滑地接近普通欧氏 MLS。提高 `grid` 可改善折角轮廓与图测地近似，同时增加三角化、最短路、MLS 和矢量裁剪成本。

### 5.2 复杂度

设 $n=|V|$、$e$ 为网格无向边数、$f=|T|$、$m=\sum_iN_i$ 为总手柄样本数。

- 每个样本运行一次二叉堆 Dijkstra，时间为 $O(m(e+n)\log n)$，存储为 $O(n+e)$，另需保存 $O(nm)$ 权重矩阵。
- 每个顶点对全部样本计算一次 MLS，时间为 $O(nm)$。
- 权重预览复用同一权重计算，着色和拓扑覆盖层分别为 $O(f)$。
- 描边/填充回写使用 UV 包围盒空间索引。最坏情况仍可能退化为“源图元数 × 三角形数”，普通局部查询只访问相交网格桶中的候选三角形。

### 5.3 模块与算法对应

| 模块 | 主要职责 | 关键入口 |
| --- | --- | --- |
| `pyfile/crease_line_tool.py` | 独立折角线创作、持久化、删除和变更通知 | `get_creases`, `_capture` |
| `pyfile/fukusato_mesh.py` | PSLG、孔洞种子、约束三角化、折角扇区复制、重心定位、边图 | `GarmentMesh.triangulate`, `_split_cut_vertex_fans` |
| `pyfile/fukusato_mapping.py` | 弧长采样、Dijkstra、权重、刚性/相似 MLS、描边裁剪和仿射回写 | `geodesic_from`, `build_weights`, `mls_deform`, `emit_pattern` |
| `pyfile/fukusato_workflow.py` | 论文工作流、勾/叉事务、填充回写、菜单、预览、缓存、历史 | `_sample_controls`, `_solve_uv`, `_emit`, `refresh_overlays` |
| `pyfile/auto_mapping.py` | 复用桶式区域检测、路径离散和 Earcut 多边形三角化 | `_detect_region`, `_triangulate_polygon` |
| `pyfile/overlay_stack.py` | 合并折角、手柄、热力图和拓扑覆盖层 | `set_items` |
| `pyfile/script_store.py` | 多工具互不覆盖的场景脚本状态 | `read`, `write` |

## 6. 论文复现范围与差异

“方法复现”与“论文全部实验复现”需要区分。当前代码覆盖论文的核心 UV 编辑链路，但下表中的替代和扩展会导致数值或交互结果不可能与作者程序逐像素一致。

| 项目 | 原论文 | 当前实现 | 结论 |
| --- | --- | --- | --- |
| 初始 UV | 接收预先生成的 UV；实验使用 Hashimoto 网络，§3 允许平面投影 | $U^0=X$ 的平面投影 | 方法接口兼容；未复现神经网络实验输入 |
| 折角线 | 假设二维服装模型沿输入折角线切开 | 独立人工折角线工具 + PSLG 约束 + 顶点扇区复制 | 完整落实切缝语义；折角来源是工程替代 |
| 点/曲线手柄 | 点手柄及 κ-Curve 曲线手柄 | 单点/自由笔迹曲线 + 弧长采样 | MLS 目标形式一致；实际曲线与前端不同 |
| 手柄投影 | 三角网格重心坐标 | 同一方法 | 已复现 |
| 变形器 | 刚性 MLS；相似变形列为未来工作 | 默认刚性，另提供相似模式 | 核心已复现并扩展 |
| 距离 | 测地权重与欧氏权重混合；引用连续测地算法 | 切缝边图 Dijkstra 与欧氏距离混合 | 拓扑行为一致；距离为离散近似 |
| 交互时机 | 拖动时实时更新 | 勾选后才求解，拖动期间可预览 | 按本项目需求有意改变 |
| 输出 | 编辑并导出 UV | 保存 UV，并逐片回写矢量描边/填充 | UV 核心保留，输出能力扩展 |
| 可视化 | 双面板显示模型与 UV | 另增双视图权重热力图和拓扑浏览 | 工程扩展 |
| 实验 | 6 名参与者的两组用户研究 | 自动化几何/状态测试 | 未复现论文用户研究 |

因此，当前实现可表述为“完整实现论文核心方法，并对 AnimeAn 矢量工作流做了明确的工程扩展”。若要求严格的作者程序级复现，还需补齐相同初始 UV 数据、κ-Curve 输入、作者使用的连续测地求解配置、实时交互时序和原用户研究。

## 7. 用户工作流

1. 在 ChildView 放置待映射的纹理描边和填充；在 MainView 准备闭合服装线稿。
2. 如需表达褶皱或自遮挡，选择 `Crease Line / 折角线`，在 MainView 绘制折角。折角独立存储，不成为普通作品描边。
3. 选择 `Fukusato Guide / 引导线`，在服装内部画点或曲线手柄。
4. 拖动洋红色覆盖层到目标位置。此时作品尚未变形。
5. 可打开 `Fukusato Mapping > Weight Preview / 权重预览`，在 MainView 与 ChildView 检查影响范围；可同时打开 `Triangle Topology / 三角形拓扑浏览` 检查切缝和网格。
6. 点击手柄右上角的勾，求解并提交变形；点击叉取消新手柄或撤回本次手柄编辑。
7. 已接受手柄可再次拖动并重新确认，也可删除；系统始终用当前帧全部已接受手柄从基准 UV 重新求解。

## 8. 验证与已知限制

### 8.1 自动化验证

`tests/test_fukusato_mapping.py` 包含 24 个测试，覆盖：

- 外轮廓、孔洞和折角约束的三角化；
- 凹孔洞内部种子的合法性；
- 边界到边界折角对网格连通性的切断；
- 纯测地权重不会越过断开的网格分量，欧氏混合可显式恢复跨缝支持；
- 曲线积分测度的应用与非法测度输入拒绝；
- 重心 UV 投影；
- 刚性 MLS 对刚体运动的重现；
- 曲线确认后的投影与插值行为；
- 重叠 UV 片和单点纹理不被错误去重；
- 源参数相邻但面板图像不连续的 UV 片不会被错误串接；
- UV 空间索引对超远查询的有界处理；
- 带嵌套孔洞填充的奇偶规则；
- UV 三角形裁剪；
- 拓扑浏览的唯一边输出；
- 画笔样式、跨层绘制顺序和输出层索引维护；
- Fukusato 拖动预览仅在释放时提交，AutoMapping 锚点在取消时恢复；
- 旧矩形工作流不会在导入时被误激活；
- Fukusato 命名空间不会覆盖其他工具状态。

运行方式：

```powershell
python -m unittest discover -s tests -p test_fukusato_mapping.py
```

### 8.2 已知限制

- Dijkstra 在网格边图上近似测地距离，存在方向偏差并依赖 `grid`；若需要与论文引用算法做数值对齐，应实现 Heat Method 或连续 Eikonal 求解并进行误差比较。
- 当前平面初始 UV 无法替代高质量自动初始映射。若输入 UV 已严重翻转，局部手柄不一定能修复；这与原论文 §7.4 的限制一致。
- 系统检测负面积 UV 三角形并分为背面输出，但没有施加局部单射或无翻转约束。
- 自交、重复点或互相重叠的非法填充环不在 Earcut 的正确性保证范围内。
- 自由笔迹在几何处理中会离散为折线；“精确回写”只相对于该折线和分片线性 UV 场成立。
- 描边宽度采用全局比例，不能表达局部各向异性缩放。
- 当前每帧只允许一个服装闭合区域的手柄参与求解，并在捕获和应用阶段双重校验；不同裁片之间的缝线纹理连续性不在当前范围内，这也是原论文列出的未来工作。

## 参考文献

[1] T. Fukusato, R. Shibata, S.-T. Noh, and T. Igarashi. “Interactive texture editing for garment line drawings.” *Computer Animation and Virtual Worlds*, 33(6):e2117, 2022. https://doi.org/10.1002/cav.2117

[2] T. W. Sederberg and S. R. Parry. “Free-form deformation of solid geometric models.” *SIGGRAPH '86*, pp. 151--160, 1986. https://doi.org/10.1145/15922.15903

[3] S.-T. Noh and T. Igarashi. “Inverse free-form deformation for interactive UV map editing.” *SIGGRAPH Asia 2021 Technical Communications*, Article 5, 2021. https://doi.org/10.1145/3478512.3488614

[4] M. Hashimoto, T. Fukusato, and T. Igarashi. “Neurally-guided texturing for garment line drawings.” *SIGGRAPH Asia 2020 Technical Communications*, Article 3, 2020. https://doi.org/10.1145/3410700.3425428

[5] Y. I. Gingold, P. L. Davidson, J. Y. Han, and D. Zorin. “A direct texture placement and editing interface.” *UIST '06*, pp. 23--32, 2006. https://doi.org/10.1145/1166253.1166259

[6] S. Schaefer, T. McPhail, and J. Warren. “Image deformation using moving least squares.” *SIGGRAPH '06*, pp. 533--540, 2006. https://doi.org/10.1145/1179352.1141920

[7] Z. Yan, S. Schiller, G. Wilensky, N. Carr, and S. Schaefer. “κ-Curves: Interpolation at local maximum curvature.” *ACM Transactions on Graphics*, 36(4), Article 129, 2017. https://doi.org/10.1145/3072959.3073692

[8] E. Rouy and A. Tourin. “A viscosity solutions approach to shape-from-shading.” *SIAM Journal on Numerical Analysis*, 29(3):867--884, 1992. https://doi.org/10.1137/0729053

[9] K. Crane, C. Weischedel, and M. Wardetzky. “Geodesics in heat: A new approach to computing distance based on heat flow.” *ACM Transactions on Graphics*, 32(5), Article 152, 2013. https://doi.org/10.1145/2516971.2516977

[10] E. W. Dijkstra. “A note on two problems in connexion with graphs.” *Numerische Mathematik*, 1:269--271, 1959. https://doi.org/10.1007/BF01386390

[11] J. R. Shewchuk. “Triangle: Engineering a 2D quality mesh generator and Delaunay triangulator.” *Applied Computational Geometry*, LNCS 1148, pp. 203--222, 1996. https://doi.org/10.1007/BFb0014497

[12] V. Agafonkin. “Earcut.” Mapbox open-source polygon triangulation library. https://github.com/mapbox/earcut

[13] I. E. Sutherland and G. W. Hodgman. “Reentrant polygon clipping.” *Communications of the ACM*, 17(1):32--42, 1974. https://doi.org/10.1145/360767.360802
