# AnimeAn 动画工作台界面说明（完整第一版）

本设计项目完整表达 AnimeAn 动画软件第一版的主窗口骨架、多级菜单栏（含快捷键、可勾选项、单选组、右键菜单、按钮弹出菜单）、顶部流式自动换行工具栏（8 种基础工具 + 5 种 Mapping 扩展工具 + 工具锁定策略 + 压感板 `forcePadBox`）、动态生成的工具选项面板（`DataForm`，严格对齐 `animean/childrenpanel/tool_controls/*.json` 与 `toolcontrol.py`）、左侧统一的图层树（`layerList`，`Tree`）与主/子视图图层页签及素材面板（含素材转图层与多行备注 `TextArea`）、中央双页签绘图/纹理区（`TabBar` + 纹理子视图 `FloatBox` 归属栏 + `External` 画布）、右侧属性/历史/导出检查器（`Stack` + 支持回退置灰的历史 `List`）、底部支持窄窗口自动换行的流式时间轴面板（`Flow` + 关键帧/延长帧/复制帧/前移帧 + 洋葱皮 4 项过滤 + `Slider` + `ScrollBar`），以及“新建项目”“画笔与压感设置”和“纹理浮动子视图”三个对话框。

<!-- ui-section: tools-dock -->
## 顶部工具栏（toolsDock）

位于主窗口顶部停靠区，采用自动换行流式布局（`Flow`），在宽窗口（1280/1440）下单行排布，在窄窗口（1024/800）下自动换为两行并撑高顶栏：
- 包含“新建项目”按钮、8 个基础工具可勾选按钮（选择 `arrow`、画笔 `brush`、橡皮 `eraser`、填充 `fill`、连接 `shape`、套索 `select`、变换 `move`、吸管 `eyedropper`）；
- 包含 5 个 Mapping 扩展工具按钮（`H中线`、`V中线`、`映射区`、`辅助线`、`自动映射`）及“工具锁定”策略开关 `toolLockPolicyBtn`（启用锁定时若当前处于扩展工具则自动回退至 `arrow`）；
- 包含带弹出菜单（`popupMenu`）的“预设 ▾”按钮 `toolMoreMenuBtn` 以及二维向量压感板 `forcePadBox`。
<!-- /ui-section -->

<!-- ui-section: layers-dock -->
## 图层与素材面板（layersDock）

位于主窗口左侧停靠区，采用竖排（VBox）布局：
- 顶部 `layerViewTabBar`（`TabBar`）在主视图图层（`Main Layers`）与子视图图层（`Child Layers`）之间切换；
- **统一的图层树 `layerList`（`Tree`）**：对照原版 `layerpanel.ui` 的单一 `QTreeWidget`，统一承载图层与分组，支持新建图层、新建分组、删除、上移/下移、取消选中（`unselectLayerBtn`）、层级展开/折叠（`toggleCollapse`）、可见性勾选（`toggleCheck`）、拖拽重排（`reorder`）以及右键菜单（复制图层、切换锁定、删除图层、解组保留图层 `ctxDissolveGroup`、删组及成员图层 `ctxDeleteGroup`）。
<!-- /ui-section -->

<!-- ui-section: toggle-visible-btn -->
## 切换图层可见性（toggleVisibleBtn）

点击时发出业务事件 `layerToggleVisible`（`owner: business`）。未连接业务时执行模拟步骤；在 `examples/animean/qt/` 中由 C++ 业务模型 `LayerModel` 接管，切换可见性并写回 `layer.visible` 状态。
<!-- /ui-section -->

<!-- ui-section: assets-panel -->
## 素材库面板（assetList / assetNoteInput）

位于左侧停靠面板下半区，包含导入（`addAssetBtn`）、将选中素材转为图层（`assetToLayerBtn`）、移除（`removeAssetBtn`）、取消选中（`unselectAssetBtn`）、支持拖动重排的素材列表 `assetList`（`List`），以及绑定 `assets.note` 的多行文本编辑框 `assetNoteInput`（`TextArea`）。
<!-- /ui-section -->

<!-- ui-section: central-area -->
## 中央绘图与纹理工作区（centralArea）

主窗口中央区：
- 第一行 `canvasHeader` 包含 `workspaceTabBar`（`TabBar`，在 `Drawing` 绘图区与 `Texture` 纹理贴图区之间切换）、画布标题、缩放滑条 `zoomSlider`（`Slider`）和当前工具指示；
- 第二行 `textureSubBar` 管理纹理子视图（`FloatBox` / `SubControlFrame`）的宿主位置（中央页签、嵌入工具面板槽位、独立浮动窗口 `textureFloatDialog`）以及时间轴跟随（`followTimelineCheck`）与 UV 同步（`followTextureCheck`）开关；
- 下方为外部容器 `canvasHost`。
<!-- /ui-section -->

<!-- ui-section: canvas-host -->
## 绘图画布外部容器（canvasHost）

类型为 `External`，导出成 Qt 后在 `vf::BusinessInterface` 上生成 `setCanvasHostWidget(QWidget *)`，由业务工程放入真实的绘图画布控件 `CanvasWidget`，并配置了画布右键菜单。
<!-- /ui-section -->

<!-- ui-section: inspector-dock -->
## 右侧工具选项与检查器面板（inspectorDock）

位于主窗口右侧停靠区，顶部固定展示当前工具的动态选项表单 `toolOptionsForm`，中部用 3 列网格页签按钮在“图层属性”“历史记录”“渲染导出”三个层叠页（`inspectorStack`）之间切换，底部包含状态图片按钮 `colorPanelToggleBtn` 与可展开收起的调色盘面板 `colorPanel`。导出页通过 `call: exportClip` 调用动作，支持在预览栏切换“真跑 / 模拟成功 / 模拟失败”。
<!-- /ui-section -->

<!-- ui-section: tool-options-form -->
## 动态工具选项表单（toolOptionsForm）

类型为 `DataForm`，严格对齐 `animean/childrenpanel/tool_controls/*.json` 与 `animean/pyfile/toolcontrol.py` 的 `options_for_tool_json` / `options_for_extra_tool_json` 数据格式，动态生成滑条（`slider`）、对象数组下拉列表（`list`：`[{"title": ..., "value": ...}]`）、字符串 `"on"`/`"off"` 复选框（`check`）、带 `value` 与 `state.color` 的同名颜色按钮（`button`）、调色板（`palette`）和子窗口槽位（`subwindow`），支持 `visible_when: {"name": ..., "values": [...]}` 条件显隐。
<!-- /ui-section -->

<!-- ui-section: history-panel -->
## 历史记录面板（historyList）

位于检查器“历史记录”页签，包含撤销、重做、清空按钮与 `historyList`（`List`）。点击早期历史条目或执行撤销时，通过 `functions/rewindHistory.js` 更新 `history.mutedFrom`，将该位置之后的未生效历史条目置灰显示；当后续产生新操作时自动截断置灰分支并追加新记录。
<!-- /ui-section -->

<!-- ui-section: color-panel -->
## 调色盘展开按钮与面板（colorPanelToggleBtn / colorPanel）

- `colorPanelToggleBtn`（类型为 `StateButton`）：使用同一套 5 状态 SVG（`normal.svg` / `hover.svg` / `pressed.svg` / `selected.svg` / `disabled.svg`）按 `[8, 8, 8, 8]` 九宫格拉伸绘制，状态切换带 120ms 透明度过渡，文字由控件单独画在上面（随 `inspector.colorPanelOpen` 在“收起调色盘”和“展开调色盘”之间切换；锁定图层时切换到 `disabled` 状态）。
- `colorPanel`：关联过渡效果 `PanelExpand`（`effects/PanelExpand`），显示和隐藏时播放 180ms 面板展开与淡入淡出过渡动画，在 Qt 端由 `ui_effects/PanelExpandEffect` 实现。
<!-- /ui-section -->

<!-- ui-section: color-picker -->
## 自绘调色盘控件（colorPickerCanvas）

类型为 `ColorPicker`，在 `component.json` 中声明需要翻译的自绘效果 `ColorPickerDraw`（`effects/ColorPickerDraw`）。在工具端用 Canvas 2D 绘制 HSV 饱和度-明度渐变方块、色相条与颜色预览条；在 Qt 端由 `ui_effects/ColorPickerDrawEffect` 用 `QPainter` 与 `QLinearGradient` 实现，未提供实现或加 `--no-effects` 时显示虚线占位框。
<!-- /ui-section -->

<!-- ui-section: onion-skin-box -->
## 洋葱皮预览浮动嵌入框（onionSkinBox）

类型为内置容器 `FloatBox`，默认嵌入在右侧检查器底部，支持点击标题栏“浮出”按钮切换为窗口内独立浮出卡片并拖动定位，点击“还原”按钮归回原容器。
<!-- /ui-section -->

<!-- ui-section: timeline-dock -->
## 底部时间轴面板（timelineDock）

位于主窗口底部停靠区：
- 第一行 `timelineTransportFlow` 采用自动换行流式布局（`Flow`），包含播放/暂停、上一帧/下一帧、新增关键帧（`addFrameBtn`）、插入延长帧（`addHoldFrameBtn`）、复制帧（`duplicateFrameBtn`）、帧前移重排（`moveFrameBackBtn`）、删除帧、循环开关、洋葱皮总开关及 4 项分层过滤复选框（`线稿`/`填充`/`映射`/`辅助`）、帧率选择、帧定位滑条 `frameSlider`（`Slider`）与帧信息标签。在宽窗口（1280/1440）下单行排布，在窄窗口（1024/800）下自动换为两行；
- 第二行包含时间轴视口滚动条 `timelineScrollBar`（`ScrollBar`）；
- 当处于播放状态时，预览时钟（`clock`）自动以 100ms 步进触发 `clock.tick`，实时向前推进 `project.currentFrame`。
<!-- /ui-section -->

<!-- ui-section: new-project-dialog -->
## 新建项目模态对话框（newProjectDialog）

模态对话框（`type: Dialog, modal: true`），内部使用表单（Form）排版组织项目名称、画幅预设、画布宽高和透明底色选项。切换画幅预设时调用纯逻辑函数 `functions/syncPresetDimensions.js` 自动同步宽高与帧率。
<!-- /ui-section -->

<!-- ui-section: draw-settings-dialog -->
## 画笔与压感设置模态对话框（drawSettingsDialog）

模态对话框（`type: Dialog, modal: true`），可从“绘图设置 → 画笔与压感设置...”、工具栏弹出菜单或画笔工具选项中的“压感曲线...”按钮打开。底部使用标准对话框按钮组 `drawSettingsButtonBox`（`DialogButtonBox`），响应 `accept` 与 `reject` 事件。
<!-- /ui-section -->
