# Auto Mapping 2 算法说明书

**——基于双中轴线参考架的平移扫掠形变映射：完整规格**

---

## 摘要

本文给出 AnimeAn 中 **Auto Mapping 2**（下称 AM2）的完整算法规格。AM2 将 `child_paint_view` 中绘制的矢量图案，经由两组用户绘制的"中轴线"所定义的参考架，重建到 `main_paint_view` 中。

算法分五个阶段：参考架采集、映射器构造、曲线重建、区域裁剪、结果输出。其核心 $\Phi_2 = H(s) + V(t) - O_m$ 是**可分离**的平移扫掠映射，等价于对边互为平移时的双线性混合 Coons 曲面（§5.5 定理 1，数值验证偏差 $6.4\times10^{-14}$）。由于该映射非线性，逐顶点变换后直接连直线会丢失顶点间的形变，故第三阶段提供三种曲线重建模式（polyline / spline / bezier），均遵循"原始顶点为锚点、仅对插值采样点降采样"的约束。

本文所有公式均与实现一一对应（附录 B 按函数名索引），全部数学论断均经数值验证（§10）。AM2 现在是本软件**唯一的** automapping（工具栏按钮 `Auto Mapping`）；被淘汰的 Auto Mapping 1（脊柱旋转）的代码与两算法比较分析归档于 [`../old_history/auto_mapping_algorithms.md`](../old_history/auto_mapping_algorithms.md) 与 `../old_history/auto_mapping_1.py`。

---

## 1. 记号约定

| 符号 | 含义 |
|---|---|
| $C_h, C_v$ | 子系（child）H/V 中轴线，折线 |
| $M_h, M_v$ | 主系（main）H/V 中轴线，折线 |
| $O_c, O_m$ | 子系 / 主系两轴线交点 |
| $s_\times, t_\times$ | 主系交点在 $M_h$ / $M_v$ 上的弧长坐标 |
| $L_h^c, L_v^c$ | 子系轴线**弦长** |
| $L_h^m, L_v^m$ | 主系轴线**弧长** |
| $H(s), V(t)$ | 主系轴线的弧长参数化求值 |
| $\Phi_2$ | AM2 映射 $\mathbb{R}^2 \to \mathbb{R}^2$ |
| $\mathcal{P}$ | 子系图案笔画集合 |

约定：$\det[\mathbf a,\mathbf b] = a_xb_y - a_yb_x$（二维叉积）；"弦"指折线首末端点连线，"弧"指沿折线的累积长度。

---

## 2. 管线总览

```
                 ┌─────────────────────────────────────────────┐
   用户绘制  ──▶ │ I  参考架采集    guides / mapping area      │
                 │    linefinish 钩子即时移出图层 → 会话字典   │
                 └────────────────────┬────────────────────────┘
                                      ▼
                 ┌─────────────────────────────────────────────┐
   点击 AM2  ──▶ │ II 映射器构造    build_mapper(...)          │
                 │    斜基分解 → 逐侧标定 → 弧长求值 → 扫掠    │
                 │    产出 map_point : R² → R²,  width_scale   │
                 └────────────────────┬────────────────────────┘
                                      ▼
                 ┌─────────────────────────────────────────────┐
                 │ III 曲线重建     polyline / spline / bezier │
                 │     锚点自适应采样 + 区间内 RDP + 拟合      │
                 └────────────────────┬────────────────────────┘
                                      ▼
                 ┌─────────────────────────────────────────────┐
                 │ IV 区域裁剪      child area → 源, main → 果 │
                 └────────────────────┬────────────────────────┘
                                      ▼
                 ┌─────────────────────────────────────────────┐
                 │ V  结果输出      新建 mapped layer（置顶）  │
                 │    property="auto_mapped"，一次历史提交     │
                 └─────────────────────────────────────────────┘
```

---

## 3. 阶段 I：参考架采集

### 3.1 资产模型

中轴线与映射区域**不进入图层栈**。用户用 `H Center Line` / `V Center Line` 工具绘制的笔画，在 `linefinish` 钩子中被**立即移出场景模型**，转存入 Python 侧会话字典 `_MAPPING_ASSETS[view][property]`，并通过通用 C++ 叠加显示服务（`ui.set_overlay`）绘制。

设计后果：

- 不出现在 Layers / Assets 面板，不参与油漆桶边界检测
- 每视图每类型**唯一**，重画即替换
- 序列化进 `scriptData`，随撤销/重做快照走（`historyrestore` 钩子重建）

### 3.2 方向语义

映射在两轴向均为**起点对起点、终点对终点**。因此主系轴线的绘制方向直接决定结果朝向：反向绘制主 H 轴 $\Rightarrow$ 水平翻转，反向绘制主 V 轴 $\Rightarrow$ 垂直翻转。为使该语义可见，每条轴线终点绘制方向箭头，其方向取**窗口平滑末端切向**（窗口 $= 5\%$ 弧长），避免手绘收笔钩导致箭头乱指。

---

## 4. 阶段 II：映射器构造

### 4.1 子系斜基分解

取两轴线的半弦向量

$$\mathbf e_h = \tfrac12\bigl(C_h(1) - C_h(0)\bigr),\qquad
\mathbf e_v = \tfrac12\bigl(C_v(1) - C_v(0)\bigr),\qquad
\Delta = \det[\mathbf e_h,\mathbf e_v]$$

将 $p - O_c$ 在斜基 $(\mathbf e_h,\mathbf e_v)$ 下展开，Cramer 法则给出

$$u(p) = \frac{\det[\,p-O_c,\ \mathbf e_v\,]}{\Delta},
\qquad
v(p) = \frac{\det[\,\mathbf e_h,\ p-O_c\,]}{\Delta}
\tag{4.1}$$

转为带符号弧长量纲（$u,v$ 以半弦为单位，故乘半弦长）：

$$\xi = u\,\|\mathbf e_h\| = u\cdot\tfrac12 L_h^c,
\qquad
\eta = v\,\|\mathbf e_v\| = v\cdot\tfrac12 L_v^c
\tag{4.2}$$

> **注**：基向量取**弦**而非弧。子系轴线弯曲时 (4.1) 只是近似——这是有意的取舍：弦基使分解为全局仿射（廉价、无奇异），而子系轴线通常只作为"标定尺"，其弯曲程度远小于主系。

### 4.2 端点锚定的逐侧标定

交点将每条轴线一分为二。子系弦被分为 $a^-, a^+$，主系弧被分为 $b^-, b^+$，两者均以 $1\%$ 全长为下限截断：

$$a^- = \max(\tau,\ 0.01L^c),\quad a^+ = \max(L^c-\tau,\ 0.01L^c),\qquad
\tau = \operatorname{clamp}\bigl(\langle O_c - C(0),\hat{\mathbf u}\rangle,\ 0,\ L^c\bigr)$$

$$b^- = \max(s_\times,\ 0.01L^m),\qquad b^+ = \max(L^m - s_\times,\ 0.01L^m)$$

逐侧标定系数：

$$\sigma^- = \frac{b^-}{a^-},\qquad \sigma^+ = \frac{b^+}{a^+}
\tag{4.3}$$

**塌缩侧回退**：若某侧 $a$ 已抵下限（T 形交叉，交点落在端点），该侧无实际延展，直接复用对侧系数

$$a^-\le\text{floor}<a^+ \;\Rightarrow\; \sigma^-\!\leftarrow\!\sigma^+,
\qquad
a^+\le\text{floor}<a^- \;\Rightarrow\; \sigma^+\!\leftarrow\!\sigma^-$$

否则以 $1\%$ 下限作除数将产生约 $100\times$ 的放大器，把杂散点弹射到画布之外。

重参数化算子（**分段线性**）：

$$\mu(x) = \begin{cases}\sigma^+x,& x\ge0\\ \sigma^-x,& x<0\end{cases}
\qquad\Longrightarrow\qquad
s = s_\times + \mu_h(\xi),\quad t = t_\times + \mu_v(\eta)
\tag{4.4}$$

该方案保证子系轴线端点**恰好**落到主系轴线端点（图案严格张满所绘轴线）。代价：$\sigma^-\ne\sigma^+$ 时 $\mu$ 在原点仅 $C^0$，跨中轴线的笔画在此产生有界折角；失配比 $>1.5$ 时打印提示。

### 4.3 主系弧长求值

$H(s)$：在 $M_h$ 上按累积弧长定位并线性内插；$s<0$ 或 $s>L_h^m$ 时以端点方向线性外推：

$$H(s) = \begin{cases}
M_h[0] + s\,\mathbf d_0, & s\le0\\
\text{lerp}\bigl(M_h[i],M_h[i{+}1]\bigr), & 0<s<L_h^m,\ i = \text{bisect}(s)\\
M_h[n{-}1] + (s-L_h^m)\,\mathbf d_{n-2}, & s\ge L_h^m
\end{cases}
\tag{4.5}$$

$V(t)$ 同理。定位用二分查找，单次求值 $O(\log n)$。

### 4.4 核心：平移扫掠合成

$$\boxed{\;\Phi_2(p) \;=\; H(s) \;+\; \underbrace{\bigl(V(t) - O_m\bigr)}_{\boldsymbol\delta(t)} \;=\; H(s) + V(t) - O_m\;}
\tag{4.6}$$

即：沿 H 轴（脊线）走到弧长 $s$，再叠加 V 轴在 $t$ 处相对交点的位移。**位移只平移、不旋转**。

### 4.5 线宽缩放

$$w' = \max\left(0.5,\ w\cdot\underbrace{\sqrt{\frac{L_h^m}{L_h^c}\cdot\frac{L_v^m}{L_v^c}}}_{\text{width\_scale}}\right)
\tag{4.7}$$

单一全局几何均值。当 $\sigma^-\ne\sigma^+$ 时这是近似（不同象限面积缩放不同）。

---

## 5. 映射的数学性质

### 5.1 定理 1（Coons 退化）

> $\Phi_2$ 是**对边互为平移**时的双线性混合 Coons 曲面的闭式。

**陈述**：设 Coons 曲面
$$S(s,t) = (1-t)C_b(s) + tC_t(s) + (1-s)C_l(t) + sC_r(t) - \text{(双线性角点项)}$$
若 $C_t = C_b + \boldsymbol\delta_v$、$C_r = C_l + \boldsymbol\delta_h$（对边互为平移），则双线性修正项恰好抵消重复计数，$S$ 化简为 Boolean 和 $C_b(s) + C_l(t) - O$。

**验证**（§10.1）：4000 组随机 $(s,t)$，最大偏差 $6.4\times10^{-14}$ px。去掉平移前提（令上边界额外弯曲）后偏差达 $30.0$ px，说明该前提是**本质的**，非退化巧合。

### 5.2 定理 2（可分离性）

由 (4.6) 直接得

$$\frac{\partial\Phi_2}{\partial s} = T(s) \;\;(\text{与 } t \text{ 无关}),
\qquad
\frac{\partial\Phi_2}{\partial t} = N(t) \;\;(\text{与 } s \text{ 无关})$$

**验证**：固定 $s$、取四个不同 $t$ 计算 $\partial\Phi/\partial s$，最大离散度 $6.4\times10^{-14}$。

**推论（工程意义）**：主系某条轴线的绘制误差**不会**经由另一轴向传播放大；映射对轴线抖动的敏感度与离轴距离无关（对比 AM1 在离轴 300 px 处放大 6.0 倍）。

### 5.3 定理 3（雅可比与单射性）

$$\det J_2 = \det[\,T(s),\ N(t)\,] = \sin\angle\bigl(T(s),N(t)\bigr)
\tag{5.1}$$

**仅当两主轴线局部切向平行时退化**，与离轴距离 $\|\boldsymbol\delta\|$ **无关**。故 AM2 不存在 AM1 那种"离脊线超过曲率半径即翻折"的结构性奇异。

实测：圆弧脊线 $R=300$、离轴 $d\in[-400,400]$ 全程 $\det J_2 \equiv 1.0000$。

### 5.4 定理 4（仿射退化）

主系两轴线均为直线时，$T,N$ 为常向量，(4.6) 化为分段仿射映射；若进一步 $\sigma^-=\sigma^+$，则 $\Phi_2$ 是全局仿射（旋转 + 各向异性缩放 + 剪切 + 平移）。

### 5.5 定理 5（方向保真与手性）

映射在两轴向严格满足"子系起点 $\mapsto$ 主系起点、终点 $\mapsto$ 终点"。零容差审计（不对称字母 F 图案，全部方向组合）：

| 情形 | 结果 | 偏差 | `mirrored` |
|---|---|---:|:---:|
| 两系方向一致 | 恒等 | 0.00 | false |
| 仅反转主 H | 水平镜像 | 0.00 | true |
| 仅反转主 V | 垂直镜像 | 0.00 | true |
| 两者均反转 | $180°$ 旋转 | 0.00 | false |
| 旋转架（均"自然"绘制） | 转置 = **镜像** | 0.00 | true |
| 旋转架 + 反转一条 | 真 $90°$ 旋转 | 0.00 | false |

**手性判据**：结果为镜像 $\iff$ 两参考架**旋向相反**

$$\operatorname{sgn}\det[\mathbf e_h,\mathbf e_v] \;\ne\; \operatorname{sgn}\det\bigl[T(s_\times),N(t_\times)\bigr]
\tag{5.2}$$

这是"每轴向各自方向保真"的**数学必然**，非符号错误。注意陷阱：子系 H 画成竖线、主系 H 画成横线时，每对箭头单独看都正常，但组合旋向翻转 $\Rightarrow$ 必为镜像。修正方法：反向重画**任意一条**主轴线。代码据 (5.2) 在 `info["mirrored"]` 报告并提示。

---

## 6. 阶段 III：曲线重建

### 6.1 问题陈述

$\Phi_2$ **非线性**（$H,V$ 按弧长求值于折线，(4.4) 分段线性）。故对源笔画上一条直线段 $\overline{p_1p_2}$：

$$\Phi_2(\overline{p_1p_2}) \;\ne\; \overline{\Phi_2(p_1)\,\Phi_2(p_2)}$$

左边是曲线，右边只是它的**弦**。若仅变换顶点再连直线，顶点间的形变被整体丢弃——用户观察到"点映射了，但连线还是直的"。实测：4 px 采样的源折线经含波浪主轴的映射后，真实像与弦偏离达 **35 px**。

### 6.2 锚点自适应采样（三模式共用）

**约束（用户规定）**：原始顶点是锚点，**永不被降采样**；只有原始顶点**之间**插入的采样点参与降采样。

```
_adaptive_map_polyline(W, points) → [(映射点, is_original), ...]
  对每个原始区间 [a, b]:
      递归细分 (深度上限 8):
          m ← mid(a,b);  g ← lerp(a,b,φ)          φ = 0.381966
          若 |W(a)-W(b)| > FORCE_STEP            (16 px 强制弦长)
          或 |W(m) - mid(W(a),W(b))| > tol       (0.4 px 中点平坦度)
          或 |W(g) - lerp(W(a),W(b),φ)| > tol    (黄金分割探测)
              则插入 W(m) 并对两半递归
```

**抗混叠（实测缺陷修复）**：单靠中点平坦度会被周期性形变欺骗——一条直线段横跨波浪主轴的**整数个周期**时，中点恰好落在弦上，偏差为零，该弯的地方完全不弯。加入黄金分割探测点（与中点永不成有理比）+ 强制最大输出弦长 16 px 后修复。单元测试：正弦形变（周期 200，幅度 ±30）下，修复前输出为直线，修复后完整捕捉 ±30.0 波形。

### 6.3 区间内降采样

```
_decimate_between_anchors(flagged, ε):
    knots ← [首点]
    对每段「锚点→锚点」的采样序列 span:
        knots += RDP(span, ε)[1:]        # RDP 必保端点 ⇒ 锚点必存活
```

RDP 容差 $\varepsilon$ 由工具选项滑条 `RDP (×0.1px)` 控制，范围 $0.1\text{–}2.0$ px，默认 $0.3$ px。**该滑条仅在采样类模式（spline / polyline）下显示**，bezier 模式不使用 RDP，故隐藏（`visible_when` 通用机制）。

### 6.4 三种输出模式

| 模式 | 源表示 | 重建方式 | 输出几何 |
|---|---|---|---|
| **spline**（默认） | 4 px 展平折线 | 锚点采样 → 区间 RDP → **向心 Catmull-Rom** 插值 | 三次贝塞尔路径 |
| **bezier** | 笔画**自身**的路径命令 | 每段三次曲线：锚点变换 + **控制柄方向导数传输** + 自适应细分 | 三次贝塞尔路径 |
| **polyline** | 4 px 展平折线 | 锚点采样 → 区间 RDP | 直线段路径 |

#### 6.4.1 spline 模式

对降采样后的结点序列作**向心** Catmull-Rom（$\alpha = 0.5$）插值，转为三次贝塞尔：

$$t_{i+1} = t_i + \|P_{i+1}-P_i\|^{\alpha},\qquad
\mathbf m_i = \frac{P_i - P_{i-1}}{t_i - t_{i-1}} - \frac{P_{i+1}-P_{i-1}}{t_{i+1}-t_{i-1}} + \frac{P_{i+1}-P_i}{t_{i+1}-t_i}$$

$$C_1 = P_1 + \tfrac13\mathbf m_1(t_2-t_1),\qquad C_2 = P_2 - \tfrac13\mathbf m_2(t_2-t_1)$$

**为何用向心而非均匀参数化**：结点来自 RDP，间距**高度不均**；均匀 Catmull-Rom 在不均结点上会过冲甚至自交，向心形式（$\alpha=0.5$）可证明无自交、无尖点。

#### 6.4.2 bezier 模式

保留艺术家自己的贝塞尔结构，对每段三次曲线 $(p_0,c_1,c_2,p_3)$：

$$p_0' = \Phi_2(p_0),\qquad p_3' = \Phi_2(p_3)$$

$$c_1' = p_0' + D_{\mathbf h_1}\Phi_2(p_0),\qquad c_2' = p_3' + D_{\mathbf h_2}\Phi_2(p_3)
\tag{6.1}$$

其中 $\mathbf h_1 = c_1-p_0$、$\mathbf h_2 = c_2-p_3$ 为控制柄向量，$D_{\mathbf v}\Phi$ 为**沿该向量的单侧方向导数**（差分步长 $\min(0.5,\ 0.25\|\mathbf v\|)$ px）。

**为何用单侧方向导数而非完整雅可比矩阵**：$\Phi_2$ 只是**分片光滑**——折线顶点处切向跳变、(4.4) 在 $\xi=0/\eta=0$ 处斜率跳变。完整雅可比的中心差分会**跨越**这些折角而得到无意义的平均值；沿控制柄自身方向的单侧导数则不会跨越锚点处的折角，且无需矩阵求逆（各向异性或奇异情形天然安全）。

**一阶精确性**：三次贝塞尔端点切向为 $3(c_1-p_0)$，光滑映射把切向量 $\mathbf v$ 送到 $D_{\mathbf v}\Phi$，系数 3 两侧抵消，故 (6.1) 一阶精确。

误差控制：在 $t$ 上取 $\max(3,\min(17,\ \text{输出弦长}/16))$ 个均布探测点 + 一个黄金分割点，比较变换后曲线与真实像；超差则 de Casteljau **对半细分源曲线**并递归（深度上限 6）。源锚点恒为输出锚点（细分只增不减）。

### 6.5 输出笔画构造

曲线模式经通用绑定 `vectorlogic.make_stroke_object_from_path(commands, points, ...)` 构造：`commands` 携带三次曲线段，`points` 为稠密展平（命中测试、橡皮擦、`subStroke` 均依赖 `stroke.points`）。序列化已验证可往返三次曲线段（`projectio.cpp` 的 `CurveToElement → cubicTo`），存档重载不会退化为折线。

---

## 7. 阶段 IV：区域裁剪

`Mapping Area` 工具以油漆桶方式（Python 侧区域检测）在任一视图产生封闭多边形集合，语义：

- **子系区域**：筛选参与映射的**源**内容
- **主系区域**：裁剪映射**结果**

裁剪对不同模式采用不同表示，均保持"内部为真"的偶奇判定：

| 模式 | 源侧裁剪 | 果侧裁剪 |
|---|---|---|
| spline / polyline | `_clip_polyline`（折线，边界点二分求交） | `_clip_flagged`（带锚点标记，**边界切点标为锚点**） |
| bezier | `_clip_cubics`（对三次曲线**按参数** de Casteljau 分割） | `_clip_cubics` |

`_clip_cubics` 在曲线参数上二分定位边界穿越点（18 次迭代），再以 de Casteljau 提取内部子曲线，从而裁剪**不破坏曲线性**（不退化为折线）。

---

## 8. 阶段 V：结果输出

1. **每次点击新建图层**（`_create_mapped_layer`）：命名 `mapped layer`，重名由 `uniqueLayerName` 自动编号；结果**堆叠**，不覆盖旧结果。
2. **移至顶层**：`add_layer` 追加于列尾即 z 序最底（`paintGL` 倒序绘制），故新层 `move_layer(idx, 0)` 置顶，并同步 `remap_fill_source_layers_after_move`。
3. **创建单元格保持原样**：私有资产 + `frame_id = 1`（该 id 是 `assignAssetToLayer` 硬编码的规范值，改写会遗留空白规范图）。
4. **事务性**：异常或 `added == 0` 时回滚删除空图层——无历史提交的孤儿层会被下一次无关提交静默吞并，无法单独撤销。
5. 每条输出笔画标记 `property = "auto_mapped"`；成功后 `ui.refresh()` + 一次 `history_commit("Auto Mapping", "main")`（AM1 移除后 AM2 即 "Auto Mapping"）。

---

## 9. 守卫与诊断

| 条件 | 动作 | 理由 |
|---|---|---|
| 任一轴线点数 $<2$ | 拒绝 | 无法定义弦 |
| $\dfrac{\lvert\Delta\rvert}{\|\mathbf e_h\|\|\mathbf e_v\|} < 0.05\ (\approx3°)$ | 拒绝 | 近平行架使 (4.1) 分解爆炸。以**角度**而非绝对面积判定——长轴线下绝对阈值会放行近平行架 |
| 主系轴线弧长 $\le 10^{-9}$ | 拒绝 | 退化 |
| H/V 在任一视图**不相交** | 拒绝 | 猜测的原点在逐侧标定下产生无界垃圾；**拒绝优于输出垃圾** |
| 子系无图案笔画 | 拒绝 | 无输入 |
| 侧标定失配 $>1.5$ | 提示 | 跨轴笔画将在此折弯 |
| 旋向相反 (5.2) | 提示 | 结果为镜像，建议反向重画一条主轴线 |
| 输出笔画数 $=0$ | 回滚 + 提示 | 全被裁剪区裁掉 |

图案采集会跳过属性为 `h_center_line`、`v_center_line`、`mapping_area`、`auto_mapped`、`auto_mapping`、`auto_mapping_2` 的笔画（避免把工具自身的产物或残留笔画当作图案）。

---

## 10. 数值验证

### 10.1 定理 1（Coons 退化）

| 检验 | 结果 |
|---|---|
| Coons 曲面 vs $H(s)+V(t)-O$（4000 随机样本） | 最大偏差 $6.355\times10^{-14}$ px ✓ |
| 去掉"对边互为平移"前提 | 最大偏差 $30.00$ px（前提本质） ✓ |
| 实现 `map_point` vs 本文公式 (4.1)–(4.6)（5000 随机样本，弯曲双主轴） | 最大偏差 $1.137\times10^{-13}$ px ✓ |

### 10.2 定理 2（可分离性）

固定 $s=50$，取 $t\in\{-120,0,90,200\}$ 计算 $\partial\Phi_2/\partial s$：离散度 $6.355\times10^{-14}$ ✓

### 10.3 定理 3（雅可比）

圆弧脊线 $R=300$、$d\in[-400,400]$：$\det J_2\equiv1.0000$（AM1 同装置下在 $d=-300$ 处归零并翻折）✓

### 10.4 曲线重建保真度

以 `test_document/texture.animean` 真实图案 + 波浪主轴（22 条笔画）测量各模式输出与真实像的最大偏差：

| 笔画 | 源长 (px) | 真实像弯曲度 | polyline 模式 | spline 模式 | bezier 模式 |
|---|---:|---:|---:|---:|---:|
| #2（长腿） | 384 | 35.5 | *弦，丢失全部* | **0.25** | **0.26** |
| #19（长腿） | 383 | 34.1 | *弦，丢失全部* | **0.43** | **0.25** |
| #13 | 138 | 11.6 | — | 0.24 | 0.24 |
| #5 | 113 | 9.3 | — | 0.26 | 0.43 |
| 全部 22 条最差 | — | — | — | **0.96** | **0.43** |

两种曲线模式均将偏差控制在亚像素级。

### 10.5 抗混叠

正弦形变（周期 200 px、幅度 $\pm30$ px），直线段横跨整周期：

| | 修复前 | 修复后 | 真值 |
|---|---:|---:|---:|
| spline 输出 $\max\lvert y\rvert$ | $\approx0$（退化为直线） | **30.0** | 30 |
| bezier 输出 $\max\lvert y\rvert$ | $\approx0$ | **30.0** | 30 |

---

## 11. 复杂度

设主系轴线各 $n$ 点、图案 $K$ 条笔画共 $V$ 个采样顶点、裁剪多边形共 $E$ 条边。

| 步骤 | 复杂度 | 备注 |
|---|---|---|
| 轴线求交 | $O(n^2)$ | 逐段暴力；refer-rect 网格因此**按视图缓存** |
| 单次 $\Phi_2$ 求值 | $O(\log n)$ | 弧长二分 |
| 自适应采样 | $O(V\cdot 2^{D})$ 最坏，实测 $\approx O(V)$ | $D=8$；深度上限亦是折角处的收敛保险 |
| RDP | $O(V\log V)$ 平均 | 最坏 $O(V^2)$ |
| 裁剪 | $O(V\cdot E)$ | 偶奇判定 |
| 输出 | $O(V)$ | — |

---

## 12. 已知局限

1. **弦基近似**：(4.1) 用弦定义子系基，子系轴线弯曲时分解仅为近似。
2. **$C^0$ 折角**：$\sigma^-\ne\sigma^+$ 时 (4.4) 在轴线处仅 $C^0$；$H(s)$ 在折线顶点处切向跳变。曲线重建的自适应细分会在这些位置加密（深度上限保证终止），但折角本身不会被抹平。
3. **越界外推**：图案超出轴线张成范围时按 (4.5) 线性外推，无几何依据。
4. **线宽单一缩放**：(4.7) 为全局几何均值，$\sigma^-\ne\sigma^+$ 时各象限失配。
5. **不映射填充与栅格**：仅笔画被映射（Python 侧无添加填充区域的 API）。
6. **图层可见性**：mapped layer 仅在其映射所在帧持有单元格，故其他帧的图层面板不列出它。
7. **油漆桶不一致**：Python 侧区域检测跳过 `auto_mapped` 笔画，C++ 内建填充工具不过滤——多次映射堆叠后普通填充可能被切碎（已记录为待决事项）。

---

## 附录 A：常量表

| 常量 | 值 | 作用 |
|---|---:|---|
| `POLY_STEP` | 4.0 px | 源笔画展平步长 |
| `_CURVE_TOL` | 0.4 px | 采样 / 手柄传输的偏差容差 |
| `_FORCE_STEP` | 16.0 px | 强制最大输出弦长（抗混叠） |
| `_PROBE_T` | 0.381966 | 黄金分割探测点（抗混叠） |
| `_SPLINE_MAX_DEPTH` | 8 | 采样细分深度上限 |
| `_BEZIER_MAX_DEPTH` | 6 | 三次曲线细分深度上限 |
| `_JAC_EPS` | 0.5 px | 方向导数差分步长上界 |
| `_CATMULL_ALPHA` | 0.5 | 向心参数化指数 |
| RDP $\varepsilon$ | 0.3 px（可调 0.1–2.0） | 区间内降采样容差 |
| 侧长下限 | $1\%$ 全长 | T 形交叉防除零 |
| 轴夹角下限 | $\sin\theta = 0.05$ | 近平行架拒绝 |

## 附录 B：代码索引

全部位于 `pyfile/auto_mapping.py`（除注明者）。

| 公式 / 步骤 | 实现 |
|---|---|
| 参考架采集钩子 | `_capture_mapping_item` |
| 叠加显示项（含方向箭头） | `overlay_items` / `_direction_arrow_points` |
| 交点 $O,s_\times,t_\times$ | `_polyline_intersection` |
| 累积弧长 | `_cumulative_lengths` |
| $H(s),V(t)$ (4.5) | `_point_at_arc` |
| 子系分侧 $a^\pm$ | `_chord_sides` |
| 主系分侧 $b^\pm$ | `_arc_sides` |
| 逐侧标定 (4.3) | `side_scales` |
| 重参数化 $\mu$ (4.4) | `side_map` |
| $\Phi_2$ (4.6) | `map_point`（AM1 移除后为唯一分支；原 `spine_rotation=False`） |
| 映射器入口 | `build_mapper` |
| 锚点自适应采样 §6.2 | `_adaptive_map_polyline` |
| 区间内降采样 §6.3 | `_decimate_between_anchors` |
| 向心 Catmull-Rom §6.4.1 | `_catmull_rom_cubics` |
| 方向导数传输 (6.1) | `_directional_image` |
| 三次曲线自适应变换 | `_warp_cubic` |
| 三模式发射器 | `_emit_polyline_mode` / `_emit_spline_mode` / `_emit_bezier_mode` |
| 折线 / 曲线裁剪 | `_clip_polyline` / `_clip_flagged` / `_clip_cubics` |
| 输出层管理 | `_create_mapped_layer` / `_discard_mapped_layer` |
| 主流程 | `_perform_mapping` |
| 曲线笔画构造绑定 | `make_stroke_object_from_path`（`pythonbind/python_bindings.cpp`） |

## 附录 C：伪代码

```python
def auto_mapping_2():
    mode = curve_mode()                       # spline | bezier | polyline

    # ---- I 参考架 ----
    for view in (child, main):
        absorb_legacy_items(view)             # 兼容旧版存于图层的资产
        assert has_H_and_V(view)              # 否则拒绝
        assert H_crosses_V(view)              # 否则拒绝

    pattern = collect_pattern_strokes(        # 跳过工具自身属性的笔画
        child, child_frame, want_commands=(mode == "bezier"))
    assert pattern                            # 否则拒绝

    # ---- II 映射器 ----
    W, width_scale = build_mapper(C_h, C_v, M_h, M_v)   # Coons 是唯一算法
    assert W                                  # 角度 / 退化守卫
    warn_if(side_scale_mismatch > 1.5)
    warn_if(mirrored)                         # 旋向相反 (5.2)

    # ---- V 输出层（先建，便于失败回滚）----
    layer = create_mapped_layer(main, main_frame)   # 新建 + 置顶

    added = 0
    for stroke in pattern:
        color, width = stroke.color, max(0.5, stroke.width * width_scale)

        if mode == "bezier":
            for sub in commands_to_subpaths(stroke.commands):
                for src in clip_cubics(sub, child_area):        # IV 源侧裁剪
                    out = []
                    for cubic in src:
                        out += warp_cubic(W, cubic)             # III (6.1) + 细分
                    for piece in clip_cubics(out, main_area):   # IV 果侧裁剪
                        emit_curved(piece); added += 1
        else:
            for poly in stroke.polylines:
                for src in clip_polyline(poly, child_area):     # IV 源侧裁剪
                    flagged = adaptive_map_polyline(W, src)     # III §6.2 锚点采样
                    for piece in clip_flagged(flagged, main_area):
                        knots = decimate_between_anchors(piece, rdp_eps())
                        if mode == "spline":
                            emit_curved(catmull_rom(knots))     # §6.4.1
                        else:
                            emit_polyline(knots)
                        added += 1

    if added == 0:
        discard_mapped_layer(main, layer)     # 事务性回滚
        return False

    ui.refresh()
    ui.history_commit("Auto Mapping", "main")
    return True
```

## 附录 D：复现

§10 数据由会话临时目录中的脚本生成：

- `coons_check.py` — 定理 1、2 与实现一致性（§10.1、10.2）
- `jacobian_probe.py` — 定理 3（§10.3）
- `repro_legs.py` — 真实图案的曲线重建保真度（§10.4）
- `test_curves.py` — 抗混叠与曲线工具单元测试（§10.5）
- `test_direction.py` — 定理 5 方向保真审计（§5.5）
