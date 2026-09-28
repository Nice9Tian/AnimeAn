# 中轴线（center_line）

Tools ▸ Mapping 的两个按钮：**H Center Line**（横向轴，蓝）和 **V Center Line**（纵向轴，绿）。两块板上各画一对，定义图案的坐标系和它要贴到的位置。

## 在哪

- Mapping 页按钮 **H Center Line**、**V Center Line**。
- Options 面板：Mapping 页所有工具共用的运行选项块（见 `auto_mapping.md`），加 **Stabilizer**、**Width**，最后一行嵌着纹理板。
- 菜单 **Auto Mapping ▸ Line Display Settings…**：轴线的颜色、线型、线宽。

## 怎么用

1. 在**纹理板**上用 H Center Line 画一条大致横向的线，用 V Center Line 画一条大致纵向的线，两条必须真正相交（T 形交在端点上也算）。交点是图案坐标系的原点，线的两端是范围。
2. 在**主画板**上同样各画一条：位置、长度、弯曲随意。端点对应端点、交点对应交点；主画板的线弯，图案就跟着弯。
3. 配合 Alt / Shift 能画出严格水平 / 垂直的轴线；线同样经过防抖和拟合。
4. 画完即成为引导线，不进图层：用 Arrow 直接拖动它，松手后 Auto Mapping 重跑；点右上角的 × 删除后重画。重画会替换同一块板上的同一条。
5. 单元模式下，轴线属于当前有焦点的单元，随单元配置保存。

## 会看到什么

- 线末端有方向小箭头：Auto Mapping 有方向，起点对起点、终点对终点；把主画板上的一条线反着画，图案就镜像。
- 两块板轴线的"手性"相反（比如纹理板 H 竖着画、主画板 H 横着画）时结果是镜像，Python Debug 里提示反画其中一条即可；不会自动纠正，因为反画一条本身就是翻转图案的手段。
- 两条线不相交、或两块板交点位置比例差太大时 Python Debug 给出提示；不相交则拒绝运行 Auto Mapping。

## 边界

- 每块板每种轴线只有一条。
- 轴线按折线保存，画得很弯时折线的段数决定 Auto Mapping 的折点数。
- 轴线随撤销和 `.anproj` / `.textureview` 保存，不出现在 Layers。

规格（一级）：`../auto_mapping/auto_mapping_2_spec.md` 第 3 节（参考架采集）、第 4 节（映射器构造）。
