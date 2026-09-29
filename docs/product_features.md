# 产品功能（product_features）

二级语义：系统对用户承诺什么，改了用户会看到不同的行为或能力。从属于一级（`product_purpose.md`、`user-workflow.md`、`tools_user_workflow/`、`architecture.md`、`auto_mapping/`），与它们冲突时以一级为准；每项怎么做到在 `mechanism/`（三级）或 `auto_mapping/`（一级），机制与本文冲突以本文为准。改本文按 `developer_guide.md`「语义分级」办：计划时用户拍板，执行时穷尽解法表后可裁并标〔裁〕。

标记：**现有** 已实现；**规划** 用户已定、代码未做；**待办** 已实现但缺一步（测试、提交、删除）；**待核** 写不准；**已删除** 曾有、已去掉。机制指针标「三级」，规格指针标「一级」。

> 状态：2026-09-28 用户拍板（二级）。改承诺先改本文并拍板；执行中过不去的按〔裁〕处理。

## 画板与工作区

- 一个主画板（中央区 Drawing 页）和一块纹理板；纹理板是无限画布，可在中央 Texture 页、子控件框、停车位三处之间搬迁，任一时刻只在一处。现有。机制（三级）：`mechanism/windows_theme.md`。
- 视口滚轮缩放、中键平移、滚动条联动，两块板都有。现有。机制（三级）：`mechanism/modules.md`。
- 面板都是带页签的父窗，Windows 菜单开关；浮动的无边框窗口可拉四边改大小。现有。机制（三级）：`mechanism/windows_theme.md`。
- 深 / 浅主题，下次启动沿用；画纸恒白。现有。
- 启动时和 File ▸ Canvas Size… 都能设画布尺寸；取消保留原尺寸。现有。
- 纹理板分页栏：同时放多页纹理，每页一个独立场景。规划。
- 「映射纹理」下拉：Mapping 页工具的选项块里选用哪一页纹理；选中 Auto-Mapping 图层时选项块自动显示，在那里改该单元的目标纹理；一个单元只映射一页。规划。

## 绘画工具

- **Pen**：实时防抖加抬笔拟合，Stabilizer / Simplify / Corner 三个参数（Draw ▸ Draw Setting…），Alt / Shift 锁水平垂直，按住不动画直线；每个工具记住自己的颜色，调色板色块随文档保存。现有。机制（三级）：`mechanism/stroke_fitting.md`、`mechanism/painting_tools.md`。
- **Eraser**：AreaMode 按范围擦、LineMode 删整条、CutMode 删到相邻交点。现有。机制（三级）：`mechanism/stroke_fitting.md`。
- **Fill**：封闭区域点一下填色；Fill Scope 选 Current 或 ALL；在线稿层上填充自动新建跟随它的填充层，线稿改了填充重算；重复点击更新而不叠层；To Independent Layer 解除跟随；独立填充层上 Pen 是区域刷、Eraser 擦区域。现有。机制（三级）：`mechanism/fill_layers.md`。
- Fill 与 Auto Mapping 输出的关系：Scope ALL 时花纹笔画也算边界（封闭花纹不被底色灌入），Current 只看当前线稿层；Mapping Area 的区域探测总是忽略花纹。现有，有意分工。机制（三级）：`mechanism/fill_layers.md`。
- **Arrow**：选中笔画或填充并拖动；Default / Artist / Debug 三种编辑模式；选中轮廓的颜色线宽可设；Auto Mapping 的引导线、手柄、视平线都在 Arrow 下直接拖。现有。机制（三级）：`mechanism/painting_tools.md`。
- **Transfer**：当前层内容的缩放、拉伸、平移、旋转框，修饰键锁比例锁轴吸附角度；栅格不旋转。现有。机制（三级）：`mechanism/painting_tools.md`。
- **Connect**：两点之间生成连接笔画，Auto Snap 吸端点拐角顶点，PolyMode / CurveMode / SmoothMode，接受 / 删除按钮或 Auto Accept。现有。机制（三级）：`mechanism/painting_tools.md`。
- **Repulsion Pad**：手柄把当前帧重叠或贴近的线推开，实时预览，拓扑不变，锁定层不动。现有。机制（三级）：`mechanism/painting_tools.md`。
- 图层决定工具：Auto-Mapping 单元里的层切到 Mapping 页；跟随线稿的填充层锁 Pen / Eraser / Connect / Transfer，只留 Arrow 和 Fill；独立填充层锁 Connect。现有。机制（三级）：`mechanism/fill_layers.md`。
- 图层锁定：模型里有锁，现在只有 Transfer 尊重它、面板没有开关。规划：Layers 面板加锁定开关，锁定的层所有工具都不改。

## 逐帧动画：帧、图层与时间轴

- 时间轴停靠窗：加帧、保持帧、复制帧、删帧、帧名；Play / Pause / Loop、帧率；横条竖条随停靠位置切换；播放前预渲染、播放中不编辑、点画布即停。现有。机制（三级）：`mechanism/windows_theme.md`。
- 洋葱皮：前后帧半透明影子，线、填充、Auto-Mapping 层分别开关，引导线可选显示；只在主画板。现有。
- Layers：树形列表与图层组；New Line Layer / New Fill Layer / New Auto-Mapping Layer；可见性勾选；跟随的填充层显示为线稿层的子项。现有。
- Auto Mapping 跟着每一帧走：单元的输出落在有单元格的帧上；规划：单元的主画板轴线可逐帧调整，让花纹跟着每一帧的线稿动（现有版本一个单元的轴线在所有帧上相同）。
- 撤销 / 重做是整份模型的快照，任何来源（含脚本）的改动都能退回；两块板按全局时间顺序；History 窗点任意一行跳转。现有。机制（三级）：`mechanism/history_scriptdata.md`。

## Auto Mapping

规格与机制都是一级，在 `auto_mapping/`；下面只列承诺。

- **H/V 轴线**：纹理和主画板各一对，必须相交；不进图层，可拖、× 删、方向箭头；两边手性相反时提示镜像而不自动纠正；不相交拒绝运行。现有。规格（一级）：`auto_mapping/auto_mapping_2_spec.md`。
- **算法**：平移扫掠（Coons 退化形）的纯分级复合 Child→Third→Main，无残差；两边轴线相同时恒等；直轴线下精确仿射。现有。规格（一级）：`auto_mapping/auto_mapping_2_spec.md`、`auto_mapping/point_mapping_newton.md`。
- **不糊合**：折叠处裁断拓扑，正面、背面、折痕各成一层，背面衬里色，折痕虚线；最近端红色手柄决定叠放次序；补全拓扑用三次曲线跨过缺口。现有。规格（一级）：`auto_mapping/topology_severing.md`、`auto_mapping/fold_crease_pipeline.md`。
- **Mapping Area**：主画板上裁结果、纹理板上筛来源，每板一个。现有。规格（一级）：`auto_mapping/auto_mapping_2_spec.md` 第 7 节。
- **Additional Line**：画下的线就是等参线，牵引本族纹理流向；另一侧同步出弦；近处重画即替换；同族嵌套过渡、正交族互不影响；跨过 H/V 轴线时照常生效：被跨过的那半条轴线沿自身滑动，轴线形状和原点不动（线同时画出了那一端的版心边时，这半条轴线不滑动）；Constrain Outline 与 Falloff 可选。现有。规格（一级）：`auto_mapping/flow_field_warp.md`。
- **输出几何**：Bezier / Spline / Polyline 三种 Calculation Mode 都跟随形变，原始顶点永远是锚点，RDP 只抽插入的采样点。现有。规格（一级）：`auto_mapping/stroke_mapping_pipeline.md`。Bezier 模式在轴线折点处先劈分再搬运控制柄的修复（2026-09-25，`tests/t_bezier_knots.py`）尚在工作区未提交。待办：提交。
- **Auto-Mapping 单元**：一个单元一套配置（两边轴线、Mapping Area、附加线、锚点、选项），输出可随时重生成；焦点进入单元显示引导、离开隐藏；拖轴线、改附加线、改选项、改纹理图案都就地重跑（Live Re-render 可关）；Duplicate、To Editable Layer、Convert 旧组；Advanced Settings 管显示开关与 front / back / crease 可见性。现有。规格（一级）：`auto_mapping/layer_units.md`。
- 无单元的旧文档：引导线常显，按钮手动跑，每次新建一层置顶。现有。
- **参考网格与遮挡**：两块板叠橙色 Refer Rect，密度可选；纹理板 Occluded Areas 染出会折到背面的区域。现有。
- **Line Display Settings**：轴线、折痕、衬里的颜色线型线宽，遮挡染色。现有。
- **视平线**：主画板一条可上下拖的橙色虚线，To 3D 用它定哪一端远。现有。规格（一级）：`auto_mapping/auto_mapping_2_spec.md` 第 11.6 节。
- **To 3D**：从雅可比反推浮雕，导出离线 HTML 查看器（正交相机、浮雕滑条、翻转、线框、Transfer Grid、original camera）；需要纹理板有填充。现有。规格（一级）：同上。
- **轮廓模式（Outline Mode）**：任意封闭轮廓代替手画轴线，框内轴线与其他约束线自动生成；多边形边的对应算法与交互未定。规划。
- Midline 示例按钮：hook 系统遗留示例。已删除（2026-09-28）。
- AutoMappingState 通道：自动运行留下的无人调用的管道。已删除（2026-09-28，提交 82175f2）。
- 附加线跨越正交轴（样本里是 H 轴）处出现凹槽：轴带被全分量硬钉，线要带过轴的沿轴位移被压成 0（样本凹槽约 6 px，整条线八成请求被吞）。已修（2026-09-30，提交 e811348、0ad1094）：被跨过的那半条轴放开沿轴分量，样本上凹槽消失、线残差均值 31.8 → 6.6 px；线同时画出被跨轴那一端版心边的情形仍钉住（施工时补充，待用户确认）。复核遗留，待用户决定：另一侧的线绕远处支点旋转或缩放时，滑动可能对不上甚至反向；共用半条轴、自己不跨轴的线会随轴浮动，实测可达 31 px。记录：`plan/2026-09-29-附加线跨轴凹槽.md`、`plan/2026-09-30-附加线跨轴放行施工计划.md`。

## 导入与文件

- `.anproj` 保存主画板、全部纹理和脚本状态；`.textureview` 单独保存一页纹理；Export Texture View Image 导出纹理板画面。现有。机制（三级）：`mechanism/project_files.md`。
- 导入 Raster、OpenToonz Lines（`.tnz` / `.pli` 线条）、Clip Studio Paint（`.clip` 矢量笔画），主画板和纹理板各一套入口。承诺；现有实现未做过完整测试。待办：完整测试。机制（三级）：`mechanism/project_files.md`。
- 不解 `.tlv` 栅格，不写回 OpenToonz。不做。

## 脚本与调试

- 工具行为在 `pyfile/` 里改，同步脚本后重启即生效，不重新编译；Python Debug 窗看输出、执行命令。现有。机制（三级）：`mechanism/hook_events.md`、`mechanism/build_deploy.md`。
- 脚本注册的菜单、设置窗、选项面板、图层右键项由 C++ 按声明渲染。现有。机制（三级）：`mechanism/hook_events.md`。

## 部署

- 发布形态是 `dist\AnimeAn\` 文件夹打成的压缩包，解开即用，不做安装器；离线可运行；缺库时首次使用从随包 wheel 自装。现有。机制（三级）：`mechanism/build_deploy.md`。

## 待核

（无。）
