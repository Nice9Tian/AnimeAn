# 用户工作流（user-workflow）

用户从拿到程序到日常使用，主线是下面这几步。每一步写用户看到什么、做什么，不写怎么实现；实现见 `architecture.md`。每个工具的详细用法各有一页，在 `tools_user_workflow/`（索引在它的 README），本文只写主线，工具页与本文同为一级。界面文案以英文为主（菜单、按钮），本文照抄界面写法。

> 状态：2026-09-28 用户拍板（一级，含 `tools_user_workflow/` 十三页）。

## 1. 拿到程序与首次启动

1. 现在没有安装包：拿到的是一个 `AnimeAn` 文件夹（开发机上是仓库里的 `dist\AnimeAn\`，由构建脚本生成），里面有 `AnimeAn.exe`、Qt 运行库、`python312\` 运行时、随包的 Python 脚本和 wheel。双击 `AnimeAn.exe` 运行，不需要联网。发布给别人的形态就是这个文件夹打成的压缩包，解开即用，不做安装器。
2. 每次启动都先弹出画布尺寸对话框（Canvas Size），选预设或输入宽高；取消则保留默认尺寸，不会退出程序。
3. 主窗口打开后状态栏显示一行 Python 问候语，表示嵌入的 Python 已就绪；Python Debug 窗里能看到脚本模块逐个导入的记录。
4. 需要第三方库的功能（To 3D 的三角化、流场求解）第一次用到时会自动从随包的 wheel 安装，之后不再提示。

## 2. 认识工作区

- **中央区**是两页：**Drawing** 是主画板，始终存在、随窗口放大；**Texture** 页在用户要把纹理板放大看时使用。
- **面板窗**（Windows 菜单可开关）：**Tools**（Painting / Mapping 两页）、**Options**（当前工具的选项）、**Layers**（Main Layers / Child Layers 两页，分别对应主画板和纹理板）、**Assets**、**History**、**Repulsion Pad**（默认隐藏）、**Python Debug**、时间轴。面板带页签，一页也显示页签；可拖出浮动，浮动的无边框窗口能拉四边改大小。
- **菜单栏**：**File**（Canvas Size…、Open…、Save、Save As…、Import ▸ Raster… / OpenToonz Lines… / Clip Studio Paint…、Texture ▸ 纹理板的同组命令）、**Edit**（Undo、Redo）、**Draw**（Draw Setting…）、**Arrow**（Outline Display Settings…）、**Auto Mapping**（见第 7 步）、**View**（主画板的参考网格开关）、**Theme**（Dark / Light）、**Windows**（Texture View 和各面板的显示开关）。Draw、Arrow、Auto Mapping、View 四个菜单由脚本提供。
- 主题选 Dark 或 Light 后记住，下次启动沿用；画纸在两种主题下都是白的。

## 3. 画线稿（Tools ▸ Painting）

Painting 页一排图标，选中后 Options 面板换成该工具的选项。每个工具的用法见 `tools_user_workflow/` 里的同名页，这里只说各自是干什么的：

| 工具 | 干什么 | 页 |
|---|---|---|
| **Arrow** | 选中笔画或填充并拖动；三种编辑模式（Default / Artist / Debug）编辑锚点 | `tools_user_workflow/arrow.md` |
| **Pen** | 画一笔，抬笔时拟合成曲线；Stabilizer、Width、调色板；Alt / Shift 画水平垂直线，按住不动画直线 | `tools_user_workflow/pen.md` |
| **Eraser** | AreaMode 按范围擦、LineMode 删整条、CutMode 删到相邻交点 | `tools_user_workflow/eraser.md` |
| **Fill** | 封闭区域里点一下填色；自动建跟随线稿的填充层 | `tools_user_workflow/fill.md` |
| **Transfer** | 当前层内容的缩放、拉伸、平移、旋转框 | `tools_user_workflow/transfer.md` |
| **Connect** | 两个端点之间生成一条连接笔画 | `tools_user_workflow/connect.md` |
| **Repulsion Pad**（Windows 菜单） | 用手柄把重叠或贴太近的线推开 | `tools_user_workflow/repulsion_pad.md` |

**Draw ▸ Draw Setting…** 是画笔的文档级偏好：Stabilizer（和面板上的同一个值）、Simplify、Corner。调色板（已存色块、色轮、HSV、RGB）在 Pen 和 Fill 的 Options 里，色块随文档保存，每个工具记住自己上次的颜色。

## 4. 逐帧动画：帧、图层与时间轴

逐帧动画是主线：线稿一帧一帧画，花纹要跟着每一帧走（第 6 步）。

- **时间轴**：一条可停靠的窗，标题栏就是播放条。加帧、加保持帧（Hold，沿用上一帧的内容）、复制帧、删帧、给帧命名；Play / Pause / Loop、帧率、当前帧号。停在底边是横条，停在左右是竖条。播放前先把每一帧预渲染，播放中不能编辑，点画布即停。
- **洋葱皮**：显示前后帧的半透明影子，可以分别开关线、填充、Auto-Mapping 层的内容，以及要不要显示引导线；只在主画板上有效。
- **Layers**：树形列表，图层组可展开。右键新建 New Line Layer（线稿层）、New Fill Layer（填充层）、New Auto-Mapping Layer（Auto-Mapping 单元，见第 6 步）；勾选框控制可见性；图层锁定：模型里有，现有版本面板上没有开关，只有 Transfer 尊重它（规划：Layers 面板加锁定开关，锁定的层所有工具都不改它）；选中哪一层就画在哪一层。选中的层决定工具：Auto-Mapping 单元里的层把 Tools 切到 Mapping 页；跟随线稿的填充层锁掉 Pen / Eraser / Connect / Transfer，只留 Arrow 和 Fill。
- **Assets**：一个图层在各帧引用的绘图资产；一般不用直接操作。
- **Auto Mapping 跟着帧走**：单元的输出落在有单元格的帧上。现有版本一个单元的主画板轴线在所有帧上是同一组；规划是轴线可逐帧调整，让花纹跟着每一帧的线稿动。
- **History**：列出当前画板的每一步操作，当前状态高亮；点任意一行跳到那个状态。**Edit ▸ Undo / Redo**（Ctrl+Z、Ctrl+Y）按两块板合起来的时间顺序撤销；撤到另一块板上的操作时会自动切到那块板。

## 5. 纹理板（Texture）

- **Windows ▸ Texture View** 把纹理板的子控件显示出来：它可以浮动，也可以拖到 Options 或 Tools 面板里嵌成一行；中央区的 Texture 页则把它放大到整个中央区。任何时刻只有一个地方显示纹理板。选中 Mapping 页的任何工具时，Options 面板最后一行就嵌着它。
- 纹理板是**无限画布**：没有页面边框，图案画在哪里都行，滚轮缩放、中键平移。
- 纹理板自己有一条菜单栏：**File**（Import ▸ 三种格式导入到纹理板、Open Texture View…、Save Texture View、Save Texture View As…、Export Texture View Image…）、**Setting**（Changable Timeline：关着时时间轴始终指主画板；Changable Texture：关着时纹理板的图案受保护，只有引导线工具能画；Background：White / Black / Transparent）、**View**（Mapping Refer Rect、Refer Rect Divisions、Occluded Areas）。
- Layers 的 Child Layers 页固定对应纹理板；Changable Texture 只管能不能改图案，不改变面板的指向。
- **规划**：纹理板会有分页栏，每页一个纹理，同时放多种纹理。用哪一页在 Auto Mapping 工具的选项里选：选项块里多一个「映射纹理」下拉，点开列出纹理页；选中一个 Auto-Mapping 图层时选项块自动显示，在那里改这个单元的目标纹理。现有版本只有一个纹理。

## 6. Auto Mapping 主线（Tools ▸ Mapping）

Mapping 页的按钮：**H Center Line**、**V Center Line**、**Mapping Area**、**Additional Line**、**Auto Mapping**。（旧版本还有一个遗留的 Midline 示例按钮，只打印日志，已于 2026-09-28 删除。）它们的详细用法在 `tools_user_workflow/center_line.md`、`tools_user_workflow/mapping_area.md`、`tools_user_workflow/additional_line.md`、`tools_user_workflow/auto_mapping.md`，主线是：

1. **建单元**：在 Main Layers 右键 New Auto-Mapping Layer，得到一个带常驻成员层的图层组。选中它的任一成员，Tools 自动切到 Mapping 页，这个单元的引导线显示出来；选到别的层，引导线隐藏。
2. **画纹理板的轴线**：用 H Center Line 画一条横向轴（蓝），用 V Center Line 画一条纵向轴（绿），两条要真正相交。这两条线定义图案的坐标系。它们不进图层，画完即变成置顶的引导线，右上角有 × 可删，可以直接拖动。（规划：纹理板有多页时，先在选项块的「映射纹理」下拉里选好用哪一页。）
3. **画图案**：在纹理板上用普通工具画图案，可以填色。
4. **画主画板的轴线**：在主画板上同样画一条 H、一条 V，位置、长度、弯曲随意；线的端点对应端点、交点对应交点，主画板的轴线弯，图案就弯。线的末端有方向小箭头，把一条线反着画，图案就镜像。
5. **可选：Mapping Area**：像油漆桶一样在封闭形状里点一下（浅蓝）。画在主画板上，Auto Mapping 的输出被裁到形状里；画在纹理板上，只有形状里的图案参与 Auto Mapping。
6. **可选：Additional Line**：在任一块板上画一条粉色细线，另一块板同步出它的弦；把线画弯，纹理就顺着这条线的走向流动，像多加了一条网格等参线。线可以跨过 H/V 轴线：跨越处的纹理顺着轴线滑到画的位置，轴线本身不变形。
7. **生成**：单元有焦点时，拖动轴线、增删附加线、改生成选项、改纹理板图案，都会自动就地重跑（Live Re-render）；没有单元的旧文档按 Auto Mapping 按钮手动跑。结果是单元组里的 front 层（正面）、back 层（背面，衬里色）和 crease 层（折痕，虚线）。逐帧动画里每一帧都要有花纹：现有版本一个单元的轴线在所有帧上相同，规划是轴线可逐帧调整。
8. **主画板上的红色手柄**是"最近端"：拖它决定折叠层的叠放次序，哪一侧在上面。
9. **生成选项**分三处：Options 面板（Mapping 页所有工具共用）有 RDP、Front/Back Split、Crease Line、补全拓扑与 Bridge k；输出几何在菜单 Auto Mapping ▸ Calculation Mode（Bezier / Spline / Polyline）；颜色和线型在 Auto Mapping ▸ Line Display Settings…。参考网格在每块板的 View 菜单里开关。
10. **右键单元 ▸ Advanced Settings…**：这个单元的显示开关、front / back / crease 各层的可见性、Live Re-render 总开关。**Duplicate Auto-Mapping Layer** 复制配置得到新单元；**To Editable Layer** 把单元烘焙成普通线稿层加填充层；旧的手工图层组可以 **Convert to Auto-Mapping Layer**。
11. **规划：轮廓模式（Outline Mode）**。不画轴线，在纹理页用一个封闭轮廓框住纹理，在主画板画出这个轮廓变形后的样子，轴线和约束线自动生成。算法与交互未定，见 `tools_user_workflow/auto_mapping.md` 第 11 条。

## 7. Auto Mapping 菜单

- **Line Display Settings…**：H 轴、V 轴、折痕、衬里的颜色、线型、线宽，纹理板的遮挡染色。
- **Calculation Mode**：Bezier / Spline / Polyline。
- **Additional Line Falloff**：Linear / Quadratic。
- **约束外轮廓 / Constrain Outline**：勾上时图案外框钉在参考架上，画出边界的那一侧自动释放。
- **视平线 / Horizon Line**：勾上后主画板出现一条可上下拖动的橙色虚线；To 3D 用它判断哪一端远。见 `tools_user_workflow/horizon_line.md`。
- **To 3D**：把当前 Auto Mapping 的结果反推成三维浮雕，导出一个离线 HTML 在浏览器里看。见 `tools_user_workflow/to_3d.md`。

## 8. 导入

- **File ▸ Import ▸ Raster…** 把图片导入主画板，成为栅格参考层。
- **File ▸ Import ▸ OpenToonz Lines…** 读 OpenToonz 的场景或矢量 level（`.tnz` / `.pli`），只导入线条。
- **File ▸ Import ▸ Clip Studio Paint…** 读 `.clip` 里的矢量笔画，不读栅格。
- 纹理板的 File ▸ Import 是同一组命令，目标是纹理板；导入前会先把纹理板显示出来。
- OpenToonz 和 Clip Studio Paint 两种导入都是承诺的功能；现有实现还没做过完整测试（`product_features.md` 里标待办）。

## 9. 保存与打开

- **File ▸ Save / Save As…** 保存 `.anproj`，里面同时有主画板和纹理板，以及所有脚本状态：Auto-Mapping 单元的配置、轴线、附加线、视平线、调色板。**Open…** 打开时两块板一起替换，历史清空。
- **Save Texture View** 把纹理板单独存成 `.textureview`，换一个工程再 Open Texture View 就能复用同一套图案；打开它只替换纹理板。
- **Export Texture View Image…** 把纹理板当前画面导出为图片。

## 10. 脚本与调试

- **Python Debug** 窗：上半是脚本的输出（Auto Mapping 摘要、警告、异常），下半是一行命令输入，可以直接执行 Python。工具"没反应"时先看这里。
- 想改工具行为：编辑 `pyfile\` 下的脚本，运行仓库根目录的 `sync_pyfiles.ps1` 把脚本同步到每个 exe 旁，重启程序即生效；没有热重载，重启是唯一支持的方式。改了 C++ 才需要重新构建。
- 需要更详细的事件日志时在 Python Debug 里执行 `hook_test.enable_verbose()`。

## 11. 各工具的详细用法

每个工具一页，固定四节（在哪、怎么用、会看到什么、边界），索引在 `tools_user_workflow/README.md`。Painting 页：`arrow`、`pen`、`eraser`、`fill`、`transfer`、`connect`；面板窗：`repulsion_pad`；Mapping 页：`center_line`、`mapping_area`、`additional_line`、`auto_mapping`；Auto Mapping 菜单：`horizon_line`、`to_3d`。

## 待核

（本文没有待核项。Midline 按钮、图层锁定、启动对话框、热重载等均已于 2026-09-28 核实或由用户决定。）
