# Auto Mapping（auto_mapping）

软件的核心：把纹理板上的图案沿两块板的 H/V 轴线映射到主画板。这一页讲运行、Auto-Mapping 单元、生成选项和输出；轴线、Mapping Area、附加线各有一页。

## 在哪

- Tools ▸ Mapping 的按钮 **Auto Mapping**：手动跑一次（没有单元的旧文档用它）。它的画布手势是 Arrow 的：不画线，只拖引导。
- **Layers（Main Layers）右键**：**New Auto-Mapping Layer**、**Duplicate Auto-Mapping Layer**、**To Editable Layer**、**Advanced Settings…**；旧的手工图层组上有 **Convert to Auto-Mapping Layer**。
- **Options 面板**（Mapping 页所有工具共用）：**RDP**（只在 Spline / Polyline 模式显示）、**Front/Back Split**、**Crease Line**（拆分开着时显示）、**补全拓扑**、**Bridge k**（补全拓扑开着时显示）；画线类工具再加 Stabilizer、Width；最后一行嵌着纹理板。
- 菜单 **Auto Mapping**：**Line Display Settings…**、**Calculation Mode**（Bezier / Spline / Polyline）、Additional Line Falloff、约束外轮廓、视平线、To 3D。
- 每块板的 **View** 菜单：**Mapping Refer Rect**、**Refer Rect Divisions**；纹理板另有 **Occluded Areas**。

## 怎么用

1. 右键 **New Auto-Mapping Layer** 建一个单元：一个带常驻成员层的图层组。选中它的任一成员层（点组行也算），Tools 切到 Mapping 页，这个单元的轴线、Mapping Area、附加线、红色手柄、参考网格按它的设置显示；选到别的层全部隐藏。
2. 在纹理板画轴线和图案，在主画板画轴线（见 `center_line.md`）。规划：纹理板有多页纹理时，Options 的 Auto Mapping 选项块里多一个「映射纹理」下拉，点开列出纹理页，选一页；选中 Auto-Mapping 图层时 Options 自动切到映射选项，在那里改这个单元的目标纹理。多页纹理各自映射到同一块主画板，靠多个单元。
3. 单元有焦点时不用按按钮：拖轴线松手、重画轴线、增删附加线、拖红色手柄、删 Mapping Area、改 Options 里的选项、改纹理板上的图案，都会就地重跑（**Live Re-render**）。
4. 结果进单元组：**front** 层（正面）、**back** 层（背面，衬里色）、**crease** 层（折痕，虚线）。折叠的叠放次序由主画板上的红色"最近端"手柄决定，拖它换哪一侧在上。
5. **Calculation Mode** 决定输出几何：Spline（平滑样条）、Bezier（保留原笔画的贝塞尔结构，搬运控制柄）、Polyline（采样点直连）。三种都跟随形变；**RDP** 只对采样模式有意义，决定插进去的采样点抽多稀，原始顶点从不抽掉。
6. **Front/Back Split** 关掉则正背面不拆层；**Crease Line** 决定要不要画折痕。**补全拓扑**开着时，同一条笔画被折缝切断的两段之间用一条曲线接起来，**Bridge k** 调张力。
7. **Refer Rect**：两块板叠一张橙色参考网格，纹理板显示参考架本身，主画板显示它映射后的样子（含附加线）；密度在 Refer Rect Divisions 里选，两块板共用。网格哪里歪，映射就哪里有问题。
8. 右键 ▸ **Advanced Settings…**：这个单元的 H Axis、V Axis、Additional Lines、Mapping Area、Nearest-Point Handle、Refer-Rect Grid、Grid Density、Occluded Areas (texture board)、Front / Front Lines、Back / Back Lines、Crease Lines、Live Re-render、Horizon Line。前几项是显示开关，Front / Back / Crease 三项直接就是对应成员层的可见性。
9. **Duplicate Auto-Mapping Layer** 复制配置得到一个新单元再微调；**To Editable Layer** 把单元烘焙成一个线稿层加一个填充层，之后当普通图层编辑。
10. 没有单元的旧文档：引导线常显，按 **Auto Mapping** 按钮手动跑，每次新建一个 mapped layer 置顶，旧结果保留，可单独隐藏、删除。
11. **规划：轮廓模式（Outline Mode）。** 不画 H/V 轴线，改画抽象轮廓：在纹理页上用一个封闭轮廓把纹理围住，在主画板上只画这个轮廓变形后的样子，任意封闭轮廓都行、不限四边。Auto Mapping 从两个轮廓的对应自动生成框内的一对 H/V 轴线，再自动生成其他约束线（用户判断就是"中线"和 Additional Line 这一类）。轮廓是多边形时，边与边怎么对应回去要有专门算法。算法与交互都未定，是待办；仓库里目前没有任何实现。

## 会看到什么

- Python Debug 里每次运行一行摘要：映射了几条笔画、进了哪层、哪一帧、线宽倍率，以及是否镜像。
- 轴线不相交、纹理板没有图案、交点比例差太大时有提示，前两者拒绝运行。
- 纹理板 Occluded Areas 开着时，会被折到背面的区域染色标出。

## 边界

- 单元在建立它的那一帧上可见（图层面板只列有单元格的帧）。一个单元的主画板轴线在所有帧上是同一组；规划：轴线可逐帧调整，让花纹跟着每一帧的线稿走。
- 用户手动拖进单元组的层不会被重跑删掉，也不受 Front / Back / Crease 开关控制。
- Advanced Settings 窗一次只开一个：开着时右键另一个单元会把窗重定向到它。
- Fill 的 ALL 范围把 Auto Mapping 输出的花纹当边界（封闭花纹不被底色灌入），Current 范围只看当前线稿层；Mapping Area 的区域探测总是忽略花纹。

规格与机制（一级）：`../auto_mapping/auto_mapping_2_spec.md`、`../auto_mapping/layer_units.md`、`../auto_mapping/topology_severing.md`、`../auto_mapping/fold_crease_pipeline.md`、`../auto_mapping/stroke_mapping_pipeline.md`、`../auto_mapping/point_mapping_newton.md`。
