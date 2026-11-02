# 窗口与主题（windows_theme）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`webui/design/ui/scene.json`、`webui/src/player.ts`、`webui/src/ui/app.js`（`DOCK_FOR_WINDOW`、`updateTextureHome`）、`webui/src/ui/panels2.js`、`webui/src/ui/panelwindows.js`、`webui/src/relay.ts`、`webui/src/ui/dialoghost.js`、`webui/src/ui/dialogwindow.js`、`webui/src/dialog.ts`、`src-tauri/src/lib.rs`、`backend/session/backendsession.*`（窗口表、视口槽位、`dialogs` 状态）、`theme.*`、`pyfile/window_manager.py`、`pyfile/dialogs.py`。2026-09-28 从架构文档下沉。

## 窗口层级

- 场景 `webui/design/ui/scene.json` 里是 `停靠窗 → 顶部页签（一页也显示页签）→ 页`。`DOCK_FOR_WINDOW` 把后端的窗口名对到停靠窗，Windows 菜单和面板的关闭按钮都走后端的窗口表。
- Python 按名字寻址：`paint`（中央区，页 `drawing` / `texture`）、`tools`（`painting` / `mapping`）、`tool_options`、`layers`（`main` / `child`）、`assets`、`history`、`repulsion_pad`、`python_debug`。`ui.windows.list / show / select / current` 是机制，`window_manager.py` 是策略；不认识的名字静默忽略。
- 中央区不是停靠窗：`show()` 对它是空操作；选它的 `texture` 页会把纹理板从子控件里拿走，选 `drawing` 还回去。
- `paint` 的当前页按焦点算：焦点在主画板上是 `drawing`，在纹理板上是 `texture`，纹理板在 Texture 页还是子控件框里都一样；焦点不在任何一块板上时，算最后拿到焦点的那块。这块板就是活动板，后端从活动板推出这一页，不另存，所以它不一定是中央区正显示的页。中央区显示哪一页，后端看纹理板的家：`texture.setHome` 报 `central` 就是 Texture 页。
- 时间轴是停靠窗但不是父窗（没有页），`ui.windows` 不列它；它的播放条就是标题栏，停在底边是横条，停在左右是竖条加细标题。
- Tauri 外壳中停靠面板（含 `timelineDock`）浮动时由 `webui/src/ui/panelwindows.js` 打开无边框独立窗口 `panel-<dockId>`（加载 `index.html?only=<dockId>&satellite=1`），经 `BroadcastChannel("animean.relay")`（`webui/src/relay.ts` 的 `RelayHost` / `RelayClient`）与主窗口共享单 WebSocket 连接。四边拉伸靠 Tauri 对无边框窗口的处理。关掉再打开回到关闭时的位置和大小，`close_panel_window` 把它报回来；拖回主窗口再浮出时用新的落点。用户关窗（Alt+F4）由外壳发 `panel-closed` 告诉主窗口，窗口表跟着变；主窗口关闭时 Windows 先带走它拥有的独立窗口，这些窗口收不到关闭请求，不算用户关的，所以不能用页面的 `pagehide` 判断。纹理板子控件框 `texture_view` 因画布上下文不能跨窗口搬迁，继续在主窗口内浮动；它所在的面板变成独立窗口时，框自己在主窗口里浮出来。普通浏览器里浮动都在页面内。
- 打包的窗口是应用窗口：Ctrl、Alt 组合键和功能键的浏览器默认动作、右键浏览器菜单都关掉（输入框除外），否则 F5 重载页面后主窗口就不再认得已打开的独立窗口。浏览器里开发时不关。

## 面板滑动（网页界面）

- `webui/src/dock_glide.ts`，在 `webui/src/player.ts` 的 `applyViews` 里调用；`webui/vf/` 的副本不动。
- 什么时候滑：停靠布局的「形状」变了才滑。形状是 `dockShapeKey`：每个停靠区每组里有哪些**可见**面板、哪些浮动，不含尺寸、当前页签和浮动位置。所以停靠、浮出（含 Tauri 里浮成独立窗口，主窗口里那块变成不可见）、叠页签、并排、Windows 菜单开关会滑；拖分隔条、改窗口大小、切页签、拖着浮动面板走不滑。页面第一次收到按下或按键之前不滑，后端在启动时开的面板直接出现；`prefers-reduced-motion` 时不滑。
- 怎么滑：新布局先整份写上，再让每个顶层面板（舞台的直接子元素，含中央区）从旧框动画到新框（Web Animations 动 `left / top / width / height`，240 ms，`cubic-bezier(0.2, 0.7, 0.2, 1)`）。旧框读计算样式，所以滑到一半又变时从当前位置接着滑。面板里的内容一开始就按最终尺寸排好，滑动时外框 `overflow: hidden` 裁住；里面的 canvas 钉在最终的像素尺寸，画板只给后端发一次 resize，也不会被拉伸。新出现的面板直接出现，消失的直接消失。

## 对话框窗口

承诺见 `../product_features.md`「画板与工作区」，规则见 `../developer_guide.md`「分工」第二条，计划见 `../plan/2026-11-03-对话框窗口.md`。

- 开着哪些对话框是 Python 的（`pyfile/dialogs.py`）：一个栈，从下到上，每项有 `id`、`kind`、`modal` 和该种类的字段，经 `ui.set_dialogs` 交给 C++。C++ 存下来，广播成 `dialogs` 状态，不认识种类。种类：`message`（`title`、`text`、`buttons`）、`canvasSize`（`width`、`height`）、`color`（`value`、`accept`，确认按钮的字）、`settings`（`name`、`layout`，和选项面板同一套控件）、`file`（`mode`、`filters`、`multiple`、`defaultName`）。文字是英文原文，显示时翻译。
- 谁开：File 菜单的流程在 `pyfile/file_menu.py`（选择框、先探测再问覆盖、矢量导入问缩放、失败提示，再调 `project.*`、`file.import`、`file.texture`），启动时的 Canvas Size 也是它开的；颜色控件点色块发 `dialog.request`，`dialogs.py` 开取色框，选好后按那个控件的选项发回（设置窗里的走 `settings.option`，用哪个方法由 Python 定，网页不指定），设置窗里选的颜色由 Python 重发设置窗的布局，设置窗的色块才显示新颜色；调色板的 + 发 `palette_box` 的 `new:<颜色>`，`palette_box.py` 开 New Color；脚本设置窗由 C++ 把菜单项或图层右键项的名字转给 `dialogs.py`。网页自己不开对话框，调用失败只记日志。例外是「后端意外退出」「连不上后端」两条提示，这时没有 Python 可问。
- 回答：对话框页把按钮和取值发 `dialog.result`（`button`、`accept`、`cancel`），C++ 转成 Python 的 `dialog` 事件，Python 关掉这个对话框再交给它的回调，回调可以接着开下一个（选完文件再问覆盖）。每个对话框只回答一次。
- 兜底：Windows 关掉窗口（Alt+F4）是回答 `closed`；对话框页出错、窗口 30 秒内没显示出来（冷启动的 WebView2 第一次开对话框页要几秒），报 `error`。这两种、Python 处理回答时抛错、Python 没有接（`dialogs.py` 不认识这个 id，没置 `message["handled"]`），C++ 都从 `dialogs` 里删掉这一条并记下 id，Python 之后再发旧栈也带不回来。`dialog.request` 和转来的 File 菜单项没有 Python 接时，C++ 报错（`Nothing opens a dialog for …`、`Nothing handles the menu item …`）。网页和后端断开时，主窗口页关掉所有对话框窗口并解锁。
- 窗口（`src-tauri/src/lib.rs`）：主窗口页按 `dialogs` 状态开 `dialog-<id>`，加载 `dialog.html?id=<id>&owner=<所有者>`。窗口无边框、不进任务栏，先隐藏；对话框页画好后调 `show_dialog_window`，按内容定大小、居中到所有者上、显示并拿到输入，再在 `BroadcastChannel("animean.dialog")` 上说一声。位置限在所有者所在显示器的工作区里；所有者最小化时（Windows 把它放在 -32000 附近），对话框放在那块显示器的正中。第一个对话框归主窗口所有，上面的归下面那个对话框所有，所以总在所有者之上，切到别的程序时跟着所有者退到后面，不用系统级的始终置顶。Windows 关掉所有者时连带关掉它所有的窗口，所以下面的对话框走了而上面的还在时，主窗口页先关掉上面的，再在新的所有者下重开。`file` 种类用系统选择框（`pick_files`）。
- 模态：有模态对话框时，主窗口页禁用它的所有者（主窗口或下面的对话框，`set_window_enabled`）。这是 Windows 自己的模态：点被禁用的所有者，系统把它最后激活的弹出窗口提到最前。独立面板窗口（外壳里所有 `panel-*` 窗口）设为不能激活（`set_panels_focusable`），点它们不会升到对话框上面。每一页在模态对话框之下都盖一层 `.modal-blocker` 吞掉输入，按下时调 `focus_modal`，把输入交给主窗口最后激活的弹出窗口（系统选择框也算）。系统选择框自己禁用主窗口，外壳不另外禁用。有窗口要关时先全部解锁、关完再按状态锁上，否则 Windows 会把焦点交给别的程序；用户关窗也走状态：外壳拦下关闭请求，发 `dialog-close-requested` 给主窗口页，等状态更新了才真正关。
- 没有外壳时（普通浏览器里开发），对话框画在主窗口页面上，选择框直接回答 Cancel。

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
- Mapping 组工具的 Options 面板最后一行嵌的就是 `texture_view` 子控件；框浮动或缺失时面板显示一行说明。卫星页不显示纹理板，Options 外出时那一行是说明文字，框在主窗口里浮着。
- 两个图层面板目标固定（Main Layers → 主画板，Child Layers → 纹理板）；`Changable Texture` 只管内容可否编辑和资产面板，`Changable Timeline` 决定时间轴是否跟随纹理板。

## 主题

- `AnimeTheme` 是后端绘制颜色的唯一出处：颜色角色（窗口地、表面、文字、分隔线、强调色、芯片色、画板围场、洋葱皮前后色）+ 深 / 浅模式。模式存 `Settings`（键 `theme/mode`），经 `theme` 状态告诉网页；网页的颜色是 `webui/src/ui/qt.css` 的 CSS 变量，跟 `data-theme` 切换。
- 画纸在两种模式下都是白的（是作品的地，不是界面）。
- 画板围场随画纸走：`AnimeTheme::surroundFor(paper)` 按画纸亮度选色，深色画纸（亮度小于 0.5）用 `ViewportSurroundDeep`（`#141516`），浅色画纸用 `ViewportSurround`（深色主题 `#2a2c2e`，和 Tools 按钮同色；浅色主题 `#cfd0d3`，因为那边的按钮色几乎是白的，和白纸分不开）。网页里的 `--viewport-surround` 跟着 `ViewportSurround` 走。
- 当前活动的画板有一圈 3 像素的框，有焦点时用 `Accent`，失去焦点时用 `BoardFrameIdle`（深色 `#b4b6bc`，浅色 `#bcbdc0`）。框只标在活动画板上，失焦不会让它消失。网页的 `blur` 经 `focused: false` 送到 `BoardSurface::injectBlur`。板子成为活动板时网页把键盘焦点交给它（切页签、Windows ▸ Texture View、撤销重做落到另一块板、打开或导入），和 Qt 的 `setActivePaintView` 一样，所以蓝框总在活动板上；对话框开着时不抢，连接时的第一份状态也不算变化（`webui/src/ui/app.js` 的 `createBoards`）。
- 强调色分两级（网页界面，`--accent` 和 `--accent-2`）：蓝色 `--accent` 只给当前焦点，即正在用的工具、有焦点的画板、正在拖动的滑动条、输入框的焦点框和选中文字、折叠组上表示当前工具的点、时间轴当前帧、播放中的播放键、开启的洋葱皮总开关、停靠落点提示；其余选中或开启的状态（页签、列表和图层选中项、复选框、静止的滑动条、开关按钮、调色板当前色、菜单悬停）用次强调色 `--accent-2`：两个主题都是比主蓝更靠近背景的蓝，深色主题 `#34566f`，浅色主题 `#a6d0ee`，和 `--accent` 的对比度都约 2.2:1，上面的文字用 `--on-accent-2`。它和背景的对比度不高，只用作大面积的底色和填充，不用作文字或图标的颜色。hover 边框、页签下划线、当前色外框这些细线用更亮的 `--accent-2-line`（深色 `#7aa7c7`，浅色 `#5f9ecd`），否则和静止时的 `--divider` 分不开。
- `ui.theme()` 给 Python 只读的 "dark" / "light"。

## 洋葱皮

- 只在主画板渲染：时间轴指向纹理板时洋葱皮一族禁用。
- 影子可分别开关线、填充、Auto-Mapping 层的内容；引导线属性和单元 tag 由 Python 登记（`register_onion_guide_properties`、`register_onion_layer_tag`），C++ 按登记过滤，Guide Line 开关可把引导线要回来。
- `onion` 事件让脚本为其他帧的单元画幽灵引导线。

## 工具锁定

- `ui.set_locked_tools` / `ui.set_fill_paint_mode` 见 `hook_events.md`；锁定按活动板生效，活动板切换时重新读那块板的锁定集。
