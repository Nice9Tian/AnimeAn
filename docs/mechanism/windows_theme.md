# 窗口与主题（windows_theme）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`parentwindow.*`、`centralpaintarea.*`、`subcontrolframe.*`、`childrenpanel/texturepanel.*`、`childrenpanel/timelinewindow.*`、`windowedgeresize.*`、`theme.*`、`mainwindow.cpp`（`updateTextureHome`、视口槽位）、`pyfile/window_manager.py`。2026-09-28 从架构文档下沉。

## 窗口层级

- `QMainWindow → ParentWindow（QDockWidget + 顶部页签，一页也显示页签）→ 页`。
- Python 按名字寻址：`paint`（中央区，页 `drawing` / `texture`）、`tools`（`painting` / `mapping`）、`tool_options`、`layers`（`main` / `child`）、`assets`、`history`、`repulsion_pad`、`python_debug`。`ui.windows.list / show / select / current` 是机制，`window_manager.py` 是策略；不认识的名字静默忽略。
- 中央区不是停靠窗：`show()` 对它是空操作；选它的 `texture` 页会把纹理板从子控件里拿走，选 `drawing` 还回去。
- 时间轴是停靠窗但不是父窗（没有页），`ui.windows` 不列它；它的播放条就是标题栏，停在底边是横条，停在左右是竖条加细标题。
- 无边框浮动的顶层窗由 `WindowEdgeResize` 提供四边拉伸。

## 纹理板的单一所有权

- 纹理面板（菜单栏 + 选项行 + 画板容器）是一个可搬迁的部件，有三个家：中央 Texture 页、名为 `texture_view` 的子控件框（可嵌进 Options 或 Tools 面板，也可浮动）、停车位。任一时刻只在一处，`updateTextureHome` 是唯一路由。
- 每个家记自己的视口（缩放、平移）；子控件里自动按长边适配，用户手动缩放后到下次进入前不再自动适配。
- Mapping 页工具的 Options 面板最后一行嵌的就是 `texture_view` 子控件；框浮动或缺失时面板显示一行说明。
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
