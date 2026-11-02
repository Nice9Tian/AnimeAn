# 窗口与主题（windows_theme）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`parentwindow.*`、`centralpaintarea.*`、`subcontrolframe.*`、`childrenpanel/texturepanel.*`、`childrenpanel/timelinewindow.*`、`windowedgeresize.*`、`theme.*`、`mainwindow.cpp`（`updateTextureHome`、视口槽位）、`pyfile/window_manager.py`。2026-09-28 从架构文档下沉。

## 窗口层级

- `QMainWindow → ParentWindow（QDockWidget + 顶部页签，一页也显示页签）→ 页`。
- Python 按名字寻址：`paint`（中央区，页 `drawing` / `texture`）、`tools`（`painting` / `mapping`）、`tool_options`、`layers`（`main` / `child`）、`assets`、`history`、`repulsion_pad`、`python_debug`。`ui.windows.list / show / select / current` 是机制，`window_manager.py` 是策略；不认识的名字静默忽略。
- 中央区不是停靠窗：`show()` 对它是空操作；选它的 `texture` 页会把纹理板从子控件里拿走，选 `drawing` 还回去。
- 时间轴是停靠窗但不是父窗（没有页），`ui.windows` 不列它；它的播放条就是标题栏，停在底边是横条，停在左右是竖条加细标题。
- 无边框浮动的顶层窗由 `WindowEdgeResize` 提供四边拉伸。

## Tools 面板（网页界面）

- `toolsDock` 下是 `toolsScrollUp`、`toolsScroll`、`toolsScrollDown`；`toolsScroll` 下直接是 `paintingHeader`、`paintingBody`、`mappingHeader`、`mappingBody`，没有分组容器：VBox 只对 `Flow` 子节点按内容算高度，嵌套容器折叠后不会收缩。定义在 `webui/design/ui/scene.json`。
- 折叠状态是业务状态 `tools.paintingOpen`、`tools.mappingOpen`，点标题发 `toolGroupToggle`。逻辑在 `webui/src/ui/tools.js`：按本机偏好存进 `localStorage` 的 `animean.toolGroups`，不进文档，也不告诉后端。
- 自动展开只看变化：选中的工具换了、或 `windows.tools.current` 变了（图层策略 `window_manager.select("tools", page)`）才展开对应的组，所以用户收起选中工具所在的组后不会被弹开，启动时也不展开。后端的页名 `painting` / `mapping` 就是组名。
- 工具说明写在节点的 `tipTitle`、`tip` 属性上，由 `QtToolChip` 显示：鼠标停 450 毫秒出现，离开 300 毫秒内滑到别的按钮立即出现，点击或移开就消失；右边放不下时翻到左边。Painting 组的文字照抄 Qt 版，Mapping 组是新写的。
- 面板放不下时 `toolsScroll` 裁切内容（`webui/src/ui/shell.css`），`watchToolsScroll`（`webui/src/ui/tools.js`）按 `scrollTop` 写 `tools.canScrollUp`、`tools.canScrollDown`，箭头 `QtScrollArrow` 据此显示。点箭头或滚轮一格走 50，正好一行按钮。箭头显示时占 14 高，所以中间都显示时视口少 28。只有网页界面能滚，VibeFrontend 预览不滚。
- 单列（宽度小于 80）时标题栏收起浮动按钮，分组标题换 10px 字号。
- Layers 面板整页放在 `scrollArea`（`webui/src/ui/panels.js`）里，`.layer-page` 最小高 124，留出两行列表和两排按钮；面板更矮时右侧出现和 History 同款的滚动条。 Tool Options 和图层列表也用 `scrollArea`，全界面不用浏览器原生滚动条，深浅主题都跟 `--sb-*` 变量走。

## Tools 面板的列数（网页界面）

- 两组都是 `Flow` 排版：内边距 8、间距 6，按钮 `QtToolChip` 固定 44×44。列数 = ⌊(面板宽 − 16 + 6) / 50⌋，夹在 1 到这组按钮数之间，所以宽度跨过 110、160、210…… 才变一列。
- 按钮不写坐标，新工具加进 `paintingBody` 或 `mappingBody` 的 `children` 就排进去，同时要在逻辑里加 `tools.locked.<名字>`。
- 停靠在左右区时，宽度取最近的整列宽：n 列是 50n + 10，即 110、160、210……，最大 860。一列是 60；没写档位的面板最窄仍是 80。
- 这条规则写在 `webui/design/ui/scene.json` 的 `toolsDock` 上：`widthStep` 是按钮宽加间距（44 + 6），`widthBase` 是两侧内边距减一个间距（16 − 6）。改按钮尺寸、间距或内边距时要一起改这两个值。
- 吸附由 VibeFrontend 运行层的 `normalizeDockLayout`（`webui/vf/src/dock.ts`）完成，网页页面和 VibeFrontend 编辑器用同一份规则。拖分隔条、打开设计文件、把面板移到另一侧都会吸附。
- 浮动、停在顶部底部、同一区沿宽度方向并排时不吸附，按钮照常换行。

## 纹理板的单一所有权

- 纹理面板（菜单栏 + 选项行 + 画板容器，`webui/src/ui/panels2.js` 的 `createTexturePanel`）是一个可搬迁的部件，有三个家：中央 Texture 页、名为 `texture_view` 的子控件框（`createSubControlFrame`，可嵌进 Options 或 Tools 面板，也可在页面内浮动）、停车位。任一时刻只在一处，`webui/src/ui/app.js` 的 `updateTextureHome` 是唯一路由，搬迁时直接移动同一个 DOM 节点而不重建 WebGL 上下文，并通过 `texture.setHome` 通知后端。
- 每个家记自己的视口（缩放、平移）；子控件里自动按长边适配，用户手动缩放后到下次进入前不再自动适配。
- Mapping 组工具的 Options 面板最后一行嵌的就是 `texture_view` 子控件；框浮动或缺失时面板显示一行说明。
- 两个图层面板目标固定（Main Layers → 主画板，Child Layers → 纹理板）；`Changable Texture` 只管内容可否编辑和资产面板，`Changable Timeline` 决定时间轴是否跟随纹理板。

## 主题

- `AnimeTheme` 是界面颜色的唯一出处：颜色角色（窗口地、表面、文字、分隔线、强调色、芯片色、画板围场、洋葱皮前后色）+ 深 / 浅模式，`themeChanged` 通知所有自绘部件重画。模式存 QSettings，启动时在构造窗口之前施加。
- 画纸在两种模式下都是白的（是作品的地，不是界面）。`.ui` 里的样式表用调色板角色，不写死颜色。
- 画板围场随画纸走：`AnimeTheme::surroundFor(paper)` 按画纸亮度选色，深色画纸（亮度小于 0.5）用 `ViewportSurroundDeep`（`#141516`），浅色画纸用 `ViewportSurround`（深色主题 `#2a2c2e`，和 Tools 按钮同色；浅色主题 `#cfd0d3`，因为那边的按钮色几乎是白的，和白纸分不开）。网页里的 `--viewport-surround` 跟着 `ViewportSurround` 走。
- 当前活动的画板有一圈 3 像素的框，有焦点时用 `Accent`，失去焦点时用 `BoardFrameIdle`（深色 `#b4b6bc`，浅色 `#bcbdc0`）。框只标在活动画板上，失焦不会让它消失。网页的 `blur` 经 `focused: false` 送到 `BoardSurface::injectBlur`。
- 强调色分两级（网页界面，`--accent` 和 `--accent-2`）：蓝色 `--accent` 只给当前焦点，即正在用的工具、有焦点的画板、正在拖动的滑动条、输入框的焦点框和选中文字、折叠组上表示当前工具的点、时间轴当前帧、播放中的播放键、开启的洋葱皮总开关、停靠落点提示；其余选中或开启的状态（页签、列表和图层选中项、复选框、静止的滑动条、开关按钮、调色板当前色、菜单悬停）用次强调色 `--accent-2`：两个主题都是比主蓝更靠近背景的蓝，深色主题 `#34566f`，浅色主题 `#a6d0ee`，和 `--accent` 的对比度都约 2.2:1，上面的文字用 `--on-accent-2`。它和背景的对比度不高，只用作大面积的底色和填充，不用作文字或图标的颜色。hover 边框、页签下划线、当前色外框这些细线用更亮的 `--accent-2-line`（深色 `#7aa7c7`，浅色 `#5f9ecd`），否则和静止时的 `--divider` 分不开。
- `ui.theme()` 给 Python 只读的 "dark" / "light"。

## 洋葱皮

- 只在主画板渲染：时间轴指向纹理板时洋葱皮一族禁用。
- 影子可分别开关线、填充、Auto-Mapping 层的内容；引导线属性和单元 tag 由 Python 登记（`register_onion_guide_properties`、`register_onion_layer_tag`），C++ 按登记过滤，Guide Line 开关可把引导线要回来。
- `onion` 事件让脚本为其他帧的单元画幽灵引导线。

## 工具锁定

- `ui.set_locked_tools` / `ui.set_fill_paint_mode` 见 `hook_events.md`；锁定按活动板生效，活动板切换时重新读那块板的锁定集。
