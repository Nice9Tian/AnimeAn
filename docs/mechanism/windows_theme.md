# 窗口与主题（windows_theme）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`parentwindow.*`、`centralpaintarea.*`、`subcontrolframe.*`、`childrenpanel/texturepanel.*`、`childrenpanel/timelinewindow.*`、`windowedgeresize.*`、`theme.*`、`mainwindow.cpp`（`updateTextureHome`、视口槽位）、`pyfile/window_manager.py`。2026-09-28 从架构文档下沉。

## 窗口层级

- `QMainWindow → ParentWindow（QDockWidget + 顶部页签，一页也显示页签）→ 页`。
- Python 按名字寻址：`paint`（中央区，页 `drawing` / `texture`）、`tools`（`painting` / `mapping`）、`tool_options`、`layers`（`main` / `child`）、`assets`、`history`、`repulsion_pad`、`python_debug`。`ui.windows.list / show / select / current` 是机制，`window_manager.py` 是策略；不认识的名字静默忽略。
- 中央区不是停靠窗：`show()` 对它是空操作；选它的 `texture` 页会把纹理板从子控件里拿走，选 `drawing` 还回去。
- 时间轴是停靠窗但不是父窗（没有页），`ui.windows` 不列它；它的播放条就是标题栏，停在底边是横条，停在左右是竖条加细标题。
- 无边框浮动的顶层窗由 `WindowEdgeResize` 提供四边拉伸。

## Tools 面板（网页界面）

- `toolsDock` 下直接是 `paintingHeader`、`paintingBody`、`mappingHeader`、`mappingBody`，没有分组容器：VBox 只对 `Flow` 子节点按内容算高度，嵌套容器折叠后不会收缩。定义在 `webui/design/ui/scene.json`。
- 折叠状态是业务状态 `tools.paintingOpen`、`tools.mappingOpen`，点标题发 `toolGroupToggle`。逻辑在 `webui/src/ui/tools.js`：按本机偏好存进 `localStorage` 的 `animean.toolGroups`，不进文档，也不告诉后端。
- 自动展开只看变化：选中的工具换了、或 `windows.tools.current` 变了（图层策略 `window_manager.select("tools", page)`）才展开对应的组，所以用户收起选中工具所在的组后不会被弹开，启动时也不展开。后端的页名 `painting` / `mapping` 就是组名。
- 单列（宽度小于 80）时标题栏收起浮动按钮，分组标题换 10px 字号。Mapping 组的按钮暂时用 `text` 占位，图形定稿后改成 `glyph`。

## Tools 面板的列数（网页界面）

- 两组都是 `Flow` 排版：内边距 8、间距 6，按钮 `QtToolChip` 固定 44×44。列数 = ⌊(面板宽 − 16 + 6) / 50⌋，夹在 1 到这组按钮数之间，所以宽度跨过 110、160、210…… 才变一列。
- 按钮不写坐标，新工具加进 `paintingBody` 或 `mappingBody` 的 `children` 就排进去，同时要在逻辑里加 `tools.locked.<名字>`。
- 停靠在左右区时，宽度取最近的整列宽：n 列是 50n + 10，即 110、160、210……，最大 860。一列是 60；没写档位的面板最窄仍是 80。
- 这条规则写在 `webui/design/ui/scene.json` 的 `toolsDock` 上：`widthStep` 是按钮宽加间距（44 + 6），`widthBase` 是两侧内边距减一个间距（16 − 6）。改按钮尺寸、间距或内边距时要一起改这两个值。
- 吸附由 VibeFrontend 运行层的 `normalizeDockLayout`（`webui/vf/src/dock.ts`）完成，网页页面和 VibeFrontend 编辑器用同一份规则。拖分隔条、打开设计文件、把面板移到另一侧都会吸附。
- 浮动、停在顶部底部、同一区沿宽度方向并排时不吸附，按钮照常换行。

## 纹理板的单一所有权

- 纹理面板（菜单栏 + 选项行 + 画板容器）是一个可搬迁的部件，有三个家：中央 Texture 页、名为 `texture_view` 的子控件框（可嵌进 Options 或 Tools 面板，也可浮动）、停车位。任一时刻只在一处，`updateTextureHome` 是唯一路由。
- 每个家记自己的视口（缩放、平移）；子控件里自动按长边适配，用户手动缩放后到下次进入前不再自动适配。
- Mapping 组工具的 Options 面板最后一行嵌的就是 `texture_view` 子控件；框浮动或缺失时面板显示一行说明。
- 两个图层面板目标固定（Main Layers → 主画板，Child Layers → 纹理板）；`Changable Texture` 只管内容可否编辑和资产面板，`Changable Timeline` 决定时间轴是否跟随纹理板。

## 主题

- `AnimeTheme` 是界面颜色的唯一出处：颜色角色（窗口地、表面、文字、分隔线、强调色、芯片色、画板围场、洋葱皮前后色）+ 深 / 浅模式，`themeChanged` 通知所有自绘部件重画。模式存 QSettings，启动时在构造窗口之前施加。
- 画纸在两种模式下都是白的（是作品的地，不是界面）。`.ui` 里的样式表用调色板角色，不写死颜色。
- `ui.theme()` 给 Python 只读的 "dark" / "light"。

## 洋葱皮

- 只在主画板渲染：时间轴指向纹理板时洋葱皮一族禁用。
- 影子可分别开关线、填充、Auto-Mapping 层的内容；引导线属性和单元 tag 由 Python 登记（`register_onion_guide_properties`、`register_onion_layer_tag`），C++ 按登记过滤，Guide Line 开关可把引导线要回来。
- `onion` 事件让脚本为其他帧的单元画幽灵引导线。

## 工具锁定

- `ui.set_locked_tools` / `ui.set_fill_paint_mode` 见 `hook_events.md`；锁定按活动板生效，活动板切换时重新读那块板的锁定集。
