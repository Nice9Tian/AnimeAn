# 模块与启动（modules）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。本页是角色边界、启动链、装载顺序和模块清单；2026-09-28 从架构文档下沉，按文件头注释整理。

## 角色

| 角色 | 位置 | 管什么 | 不管什么 |
|---|---|---|---|
| **桌面外壳** | `src-tauri/src/lib.rs`、`src-tauri/src/main.rs` | 拉起并监管 `AnimeAnBackend.exe`（Windows Job Object 随父进程退出）、打开 WebView2 窗口、向网页提供后端连接地址（`backend_endpoint`）、窗口标题、独立面板窗口（`open_panel_window` / `close_panel_window`）、原生文件选择对话框（`pick_files`） | 界面布局、业务状态、工具语义 |
| **网页界面** | `webui/src/main.ts`、`webui/src/player.ts`、`webui/src/net.ts`、`webui/src/ui/*.js`、`webui/src/board/*.ts`、`webui/vf/`、`webui/design/` | 停靠窗与页签（`webui/vf/` 运行层 + `webui/design/` 场景）、菜单栏、工具栏、声明式选项面板、图层树、资产表、历史表、时间轴、纹理面板与三处归属槽位、调色板控件、斥力板、新建对话框、画板渲染（WebGL2 `glrenderer.ts` / Canvas 2D `renderer.ts`）与输入采集（`input.ts`） | 工具语义、几何算法、模型存储 |
| **后端进程与会话** | `backend/main.cpp`、`backend/session/backendsession.*`、`backend/business/animeanbusiness.*`、`backend/platform/*`、`backend/net/wsserver.*`、`theme.*`、`selectionattention.*` | 嵌入 Python 解释器、WebSocket 协议与脏状态推送（`BackendSession`）、不依赖界面的业务编排（`AnimeanBusiness`：主 / 子画板模型与历史、工具与选项 JSON、图层树重排、帧与时间轴、播放驱动、缩略图渲染、导入导出、历史的全局排序）、颜色角色与主题（`AnimeTheme`）、帧 / 层 / 资产的选择约束（`SelectionAttention`）、事件循环与定时器（`EventLoop`、`Timer`） | 工具语义、界面排版 |
| **画板** | `openglwidget.*`、`boardbase.h`、`backend/board/*` | 渲染场景与覆盖层（经 `BoardSurface` + `RecordingDevice` 录制成绘制指令表）、视图变换（缩放 / 平移）、交互事件路由（`Tool` 枚举）、控制手柄 / 覆盖层 / 视图按钮机制、hook 事件派发、历史提交、播放缓存、轴向吸附与拟合调用、文字转路径（`textpath.*`） | 任何工具"意味着什么" |
| **核心模型库** `animean_core` | `algorithm/animemodel.*`、`algorithm/vectorlogic.*`、`algorithm/beziersplit.*`、`algorithm/scenehistory.*`、`algorithm/viewscale.h`、`projectio.*`、`clipreader.*`，建在不依赖 Qt 的 `algorithm/core/` 上 | 场景 / xsheet 数据模型（列、帧、资产、单元格、图层组与 tag、父层、锁定）、矢量几何与笔画拟合、精确贝塞尔劈分、快照式历史、`.anproj` / `.textureview` 的 JSON 读写 | 界面、Python |
| **基础类型库** `animean_corelib` | `algorithm/core/*`（命名空间 `AnimeCore`） | 字符串（`String`）、容器（`Vector`、`List`、`Map`、`Hash`、`Set`）、几何与变换（`PointF`、`RectF`、`Transform` 等）、颜色与画笔 / 画刷、贝塞尔路径与布尔运算 / 扩边（`Path`、`strokeOutline`、`united` 等）、位图与软件光栅化（`Image`、`Painter`、`RasterDevice`）、JSON、文件与目录、设置持久化（`Settings`，存 `%APPDATA%\AnimeAn\settings.json`） | 业务规则、界面 |
| **面板兜底配置** | `childrenpanel/tool_controls/*.json` | Python 选项布局不可用时的内建工具（`pen`、`eraser`、`fill`、`connect`）兜底 JSON | 界面控件实现（在 `webui/src/ui/`） |
| **Python 绑定** | `pythonbind/python_bindings.*`（`animean_python` 模块）、`pythonbind/animemodel.py`（高层封装） | 把模型、几何、`ui` 门面暴露给 Python；把 Python 值转成 `AnimeCore` 类型 | 任何策略 |
| **工具脚本** | `pyfile/*.py` | 模块装载顺序、hook / 菜单 / 设置 / 视图按钮注册表、工具选项布局、每个工具的行为、Auto Mapping 引擎、第三方库加载 | 渲染、交互事件采集、模型存储 |
| **导入器** | `clipreader.*`、`opentoonz_tools/toonz_to_dict.py` | 解 `.clip` 矢量笔画；解 `.tnz` / `.pli` | 栅格数据 |
| **共享轮子** | `algorithm/beziersplit.h` ↔ `pyfile/bezier.py`；`algorithm/viewscale.h` ↔ `pyfile/viewscale.py` | 贝塞尔劈分与求值；屏幕 ↔ 画布换算。两边镜像、唯一实现 | 步长密度策略（留给调用方） |
| **第三方与运行时** | `external/pybind11`（子模块）、`external/clipper2`、`external/stb`、`tools/python312`（不入库）、`pywheels/`、`pyfile/three_vendor.py` | pybind11；Clipper2 路径布尔与偏移；stb_image / stb_image_write PNG 读写；完整 Python 运行时；钉版本的 wheel；离线 three.js | — |
| **测试** | `tests/t_*.py`、`tests/test_legacy_suites.py`、`tests/*_tests.cpp`、`tests/core/*`、`backend/board/tests/*`、`backend/platform/tests/*`、`backend/net/tests/*`、`backend/tests/session_smoke.mjs`、`backend/tests/golden/*`、`webui/tests/*` | Python 引擎的无 GUI 回归；C++ 模型 / 文件 / 拟合 / 画板 / 平台 ctest 与 `AnimeCore` 差分测试；后端会话冒烟与新旧录制对比；网页渲染器、输入与面板单测 | — |
| **构建与同步** | `CMakeLists.txt`、`backend/CMakeLists.txt`、`algorithm/core/CMakeLists.txt`、`build_scripts/agent_build.ps1`、`build_scripts/package_tauri.ps1`、`build_scripts/toolchain_location.ps1`、`sync_pyfiles.ps1`、`setup_python_pybind11.bat` | 配置、编译 `AnimeAnBackend`、构建网页与 Tauri 外壳并打包 `dist\AnimeAn\`、脚本同步、首次环境准备 | — |

机制 / 策略成对：`ui.set_overlay` ↔ `overlay_stack`；handle 事件 ↔ `edit_tool` / `transfer_tool` / `connect_tool`；`fillrequest` ↔ `fill_tool`；`visibility` ↔ `visibility_tool`；`ui.set_locked_tools` / `ui.set_fill_paint_mode` ↔ `layer_tool_policy`；`ui.windows` ↔ `window_manager`；调色板控件 ↔ `palette_box` / `tool_colors`；斥力板 ↔ `repulsion_tool`。

## 启动链

### 1. 外壳启动后端与网页

```
AnimeAn.exe（src-tauri/src/lib.rs）
 1. 若环境变量 ANIMEAN_START_PAGE 指定了探针页（如 pen_probe.html），直接打开该页，不启动后端
 2. 生成 32 位随机 hex token，以 --port 0 --token <token> 拉起同目录（或 ANIMEAN_BACKEND_EXE）的 AnimeAnBackend.exe，
    挂入带 JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE 的 Windows Job Object
 3. 读后端 stdout 直到拿到 ANIMEAN_BACKEND_PORT=<端口>，拼出 ws://127.0.0.1:<端口>/?token=<token> 存下
 4. WebView2 加载 webui/index.html；webui/src/main.ts 装配 webui/design/ 场景与 webui/src/ui/app.js 面板工厂
 5. 网页调用 Tauri 命令 backend_endpoint 取回 { url }，连接这个地址
 6. 后端推送全量初始 state；网页收到第一个 window 状态后弹出 New Project 对话框询问画布尺寸
```

### 2. 后端启动与 Python 装载顺序

```
AnimeAnBackend.exe（backend/main.cpp）
 1. 解析 --port 与 --token；调 AnimeTheme::mode() 从 %APPDATA%\AnimeAn\settings.json 读取保存的深 / 浅色模式
 2. 定位 Python：PYTHONHOME = exe 旁 python312\，没有则用构建时记下的 tools\python312
    PYTHONPATH = 源码 pyfile\（存在时最优先）→ exe 目录 → pythonbind\ → opentoonz_tools\
    PATH 前置 python312\ 与 python312\DLLs
 3. 启动嵌入解释器（py::scoped_interpreter）
 4. 构造 WsServer 与 BackendSession（内含 AnimeanBusiness 及 main / child 两块 PaintOpenGLWidget 画板，注册 ui 回调）
 5. BackendSession::initializePythonStartup()：
    a. 注册 ui 回调；syncEmbeddedPythonState() 往 __main__ 写 main_model、child_model、model、active_view、ui、current 等
    b. 首轮收集：extra_tools.tools_json() 生成 Mapping 组按钮，刷新工具选项，python_hooks.menus_json() 记下 main / child 脚本菜单名，读 child 受保护属性
    c. runPythonInitializationScript() 运行 initalize.main()：按 BOOTSTRAP_MODULES → PYTHON_FILE_MODULES 的顺序导入，
       再发现目录里其余脚本；模块在导入时登记 hook、菜单、设置窗、视图按钮、受保护属性、洋葱皮引导属性
    d. 再读一次 child 受保护属性，重置两块板各自的历史基线，刷新工具选项
 6. pythonStartupText() 调 hello_world.hello_world() 设置状态栏初始文字
 7. WsServer::listen 监听 127.0.0.1，向 stdout 打印 ANIMEAN_BACKEND_PORT=<端口>，进入 EventLoop::instance().exec()
```

装载顺序有意义：`toolcontrol`、`python_hooks` 先于一切；`tool_colors` 先于 `palette_box`（后者从前者的缓存取种子）；`window_manager` 与 `fill_tool` 先于 `layer_tool_policy`。开发机上源码 `pyfile\` 排在 exe 目录之前，所以改脚本不重建也生效；发布的文件夹没有源码树，从 exe 旁加载。`bind_test()` 会改场景，只能从调试窗手动跑，启动不跑。

## C++ 模块

| 文件 | 职责 |
|---|---|
| `backend/main.cpp` | 解析 `--port` / `--token`、初始化 `AnimeTheme`、配置 Python 路径与 DLL 路径、启动嵌入解释器、`WsServer` 与 `BackendSession`、打印监听端口、进入 `EventLoop` |
| `backend/session/backendsession.*` | `BackendSession`：WebSocket 消息分发（`dispatchCall`、`handleInputMessage`、`handleAckMessage`）、脏状态合并推送、图片与缩略图二进制帧下发、`ui` 回调桥接、窗口状态表（`m_windowInfos`）、纹理板三处归属（`central` / `control` / `parked`）视口保存与恢复、时间轴停靠方向持久化（`Settings`） |
| `backend/business/animeanbusiness.*` | `AnimeanBusiness`：不依赖界面的业务编排——主 / 子画板模型与历史、撤销 / 重做全局排序、图层树构建与拖拽重排（`layerRowsForView`、`applyLayerPanelStructure`）、工具切换与锁定、工具选项 JSON 与内建兜底加载（`loadBuiltInToolLayout`）、帧操作与播放驱动、帧缩略图离屏渲染（`renderFrameThumbnail`）、导入导出、Python 全局状态同步与 `initalize.py` 执行 |
| `openglwidget.*`、`boardbase.h` | `PaintOpenGLWidget`（基类 `PaintBoardBase = BoardSurface`）：场景渲染、覆盖层、控制手柄、视图变换、交互事件与工具路由、hook 派发与订阅掩码、历史提交、播放缓存、无边界画布模式、背景模式、活动板指示 |
| `backend/board/boardsurface.*` | `BoardSurface`：无界面画板基类（继承 `AnimeCore::PaintDevice`），持有 `RecordingDevice`，把 `update()` 合并成 0 ms 定时调用 `paintGL()`，注入鼠标 / 滚轮 / 尺寸 / 焦点事件，维护视口滚动条状态与光标回调 |
| `backend/board/recordingdevice.*` | `RecordingDevice`：把 `AnimeCore::Painter` 绘制调用录制成带 64 位哈希的 JSON 指令表与图片帧，计算 `keep` 稳定前缀条数 |
| `backend/board/inputevent.h`、`backend/board/cursor.h` | `AnimeCore` 输入事件（`MouseEvent`、`WheelEvent`、`EnterEvent`、`FocusEvent`、`ResizeEvent`）与光标类型（`Cursor`、`CursorShape`） |
| `backend/board/textpath.*` | `textMetrics`、`textWidth`、`textPath`：用 Win32 GDI `GetGlyphOutlineW` 提取字形轮廓并转成 `AnimeCore::Path` |
| `backend/platform/signalslot.h`、`eventloop.*`、`timer.*`、`elapsedtimer.h` | 轻量信号（`Signal`）、事件循环（`EventLoop`）、定时器（`Timer`）、高精度计时（`ElapsedTimer`） |
| `backend/net/wsserver.*` | `WsServer`：基于 WinSock2 的本地单客户端 WebSocket 服务端，校验 token 与 `Origin` |
| `theme.*` | `AnimeTheme`：颜色角色的唯一出处，深 / 浅模式（读写 `Settings`），`themeChanged` |
| `selectionattention.*` | `SelectionAttention`：帧 / 层 / 资产选择的一致性约束 |
| `projectio.*` | `.anproj` / `.textureview` 的 JSON 序列化与校验 |
| `clipreader.*` | `.clip` 容器遍历与矢量块解码 |
| `algorithm/animemodel.*` | `AnimeSceneModel`：列（vector / fill / raster）、帧、资产、单元格、笔画 / 填充 / 栅格、图层组与 tag、父层、内部层、锁定、scriptData |
| `algorithm/vectorlogic.*` | `AnimeVectorLogic`：笔画构造与拟合（稳定器、拟合预算、实时增量拟合）、命中、擦除区间、切断计划、区域填充、路径 ↔ 折线 |
| `algorithm/beziersplit.*` | 精确贝塞尔劈分与路径定位 |
| `algorithm/scenehistory.*` | 快照式撤销 / 重做，全局序号 |
| `algorithm/viewscale.h` | 屏幕 ↔ 文档换算 |
| `algorithm/core/*` | `animean_corelib`（`AnimeCore`）：字符串、容器、几何、颜色、画笔 / 画刷、路径与 Clipper2 布尔 / 扩边、位图与 `Painter` / `RasterDevice`、JSON、文件、设置（`Settings`） |
| `childrenpanel/tool_controls/*.json` | 内建工具（`pen`、`eraser`、`fill`、`connect`）的无 Python 兜底选项布局 JSON |
| `pythonbind/python_bindings.*` | `animean_python`：`SceneModel`、`VectorImage`、`VectorStroke`、`Cell`、`vectorlogic`、`model_pybind`、`get_scene` / `get_current`、`ui` 门面及各回调注册 |
| `tests/*_tests.cpp`、`tests/core/*_tests.cpp`、`backend/board/tests/*`、`backend/platform/tests/*` | `projectio_tests`、`animemodel_tests`、`strokefit_tests`、`AnimeCore` 差分测试、画板 / 录制器 / 文字路径单测、平台事件循环单测 |

## Python 模块

| 文件 | 职责 |
|---|---|
| `pyfile/initalize.py` | 装载顺序与目录发现；`bind_test()` |
| `pyfile/python_hooks.py` | 注册表：hook（含订阅掩码推送）、菜单（按宿主 main / child）、菜单提供者、设置窗、视图按钮、受保护属性、洋葱皮引导属性与层 tag；`dispatch` |
| `pyfile/toolcontrol.py` | 内建与脚本工具的选项布局：Mapping 组所有工具共用的运行策略块、Stabilizer 滑条只是稳定器 |
| `pyfile/draw_settings.py` | Draw 菜单与 Draw Setting 窗：稳定器 / 简化 / 尖角三参数推给 C++ |
| `pyfile/tool_colors.py` | 每个工具自己的颜色缓存与策略 |
| `pyfile/palette_box.py` | 调色板的色块集合（存 scriptData） |
| `pyfile/edit_tool.py` | Arrow 的三种编辑模式；Arrow 菜单（选中轮廓的显示设置） |
| `pyfile/transfer_tool.py` | Transfer 的框、控制手柄、旋转与修饰键 |
| `pyfile/connect_tool.py` | Connect 的吸附与三种连接 |
| `pyfile/ui_language.py` | 记下网页当前的界面语言，给 To 3D 页和画板上的折叠气泡挑文字 |
| `pyfile/fill_tool.py` | 填充策略：落在哪层、范围升级、重复点击、跟随线稿层、To Independent Layer |
| `pyfile/layer_tool_policy.py` | 当前层决定 Tools 页与锁定的工具、填充层上的区域刷 |
| `pyfile/visibility_tool.py` | 图层可见性开关的策略 |
| `pyfile/repulsion_tool.py` | 斥力板：基线场、临时内部层预览、拓扑保持的施加 |
| `pyfile/proxy_rect.py` | 代理矩形的角点对应，算出主画板轴线和边；未接入界面，规格（一级）：`../auto_mapping/proxy_rect.md` |
| `pyfile/proxy_shape.py` | 自由闭合轮廓的配对和主画板自动轴线（纯函数），算法与常量是提案，见 issue #9 |
| `pyfile/line_tools.py` | Choice Line 和 Deform：选中轴线、附加线、轮廓线，勾选地线，沿线局部拖动变形 |
| `pyfile/window_manager.py` | 窗口 / 页策略（`ui.windows` 的调用方） |
| `pyfile/overlay_stack.py` | 多个工具的覆盖层合成到一张显示列表 |
| `pyfile/script_store.py` | scriptData 的命名空间读写 |
| `pyfile/viewscale.py`、`pyfile/bezier.py` | 两只共享轮子的 Python 镜像 |
| `pyfile/gridsolve.py` | 二维规则网格上的通用预条件共轭梯度（PCG）求解器、四单侧模板最小雅可比行列式与连通区域标记 |
| `pyfile/mapping_preview.py` | 拖动轴线、附加线、轮廓线时的主画板实时场预览（热启动求解、批量采样、正反面与连通区域层序打包） |
| `pyfile/pydeps.py` | 第三方库自愈安装 |
| `pyfile/extra_tools.py` | Mapping 组按钮清单与处理器解析 |
| `pyfile/auto_mapping.py` | Auto Mapping 引擎全体（含折叠气泡 `_fold_regions`）：轴线 / Mapping Area / 附加线 / 视平线资产、映射器、曲线重建、裁断、折痕、单元与实时重跑、参考网格、遮挡染色、洋葱皮引导、To 3D、Auto Mapping 菜单与两块板的 View 菜单、设置窗、图层右键菜单 |
| `pyfile/hook_test.py`、`hello_world.py`、`linefinish.py`、`bind_test.py` | 示例、调试与兼容垫片 |
| `pyfile/three_vendor.py` | 离线 three.js（以 `.py` 形式随脚本通道部署） |
| `pythonbind/animemodel.py` | `AnimeModel` / `LayerRef` 等高层封装、`ui` 门面、`current` |
| `opentoonz_tools/toonz_to_dict.py` | OpenToonz 解析器（也可命令行） |

## 代码组织约定

- **共享轮子唯一。** 贝塞尔劈分（`algorithm/beziersplit.h` ↔ `pyfile/bezier.py`）、屏幕 ↔ 画布换算（`algorithm/viewscale.h` ↔ `pyfile/viewscale.py`）只有一个家，C++ 与 Python 各一份镜像，调用处注释指回；劈分永不改变画出的形状，切片不重拟合；步长密度策略留给调用方。
- **决定空间再写常数。** 手和眼的量（抖动、点选容差、按住不动半径、最小可见宽度）是屏幕像素，经 `viewscale` 换算；作品的量（几何、弧长、步长）是画布像素，不换算。交互事件预算按屏幕像素一次性换算，几何按真实缩放。
- **省事就用库，不重复造轮子。** Python 侧有现成库能用的（numpy、三角化）就直接用，不是原则，是省事：wheel 入 `pywheels/`，经 `pydeps.ensure()` 加载——首次缺库先从随包 wheel 离线安装，再退网络，全失败返回 None 由调用方降级（例如三角化退回纯 Python 实现）。
- **新工具**：`PaintOpenGLWidget::Tool` 与 `AnimeanBusiness::Tool` 各加一个枚举项，`openglwidget.cpp` 的 `toolName`、`AnimeanBusiness::toolControlName` / `fileNameForBuiltInTool`、`BackendSession` 的 `parseToolValue` / `toolEnumDisplayName`、网页 `webui/src/ui/tools.js` 与兜底 `childrenpanel/tool_controls/*.json` 各加一处，其余是一个 Python 模块；漏了任一处会静默拿到画笔的选项面板。

## 已核实的边角

- `childrenpanel/tool_controls/*.json` 只在 Python 的选项布局取不到时作兜底（`AnimeanBusiness::loadBuiltInToolLayout`）；`pen.json` / `fill.json` 里的三个固定色块按钮因此只在无 Python 时出现，内容陈旧但无害。改它是改代码资产，本次只记录。
- 没有 Python 热重载：改脚本后重启程序是唯一支持的方式。

## 待核

（无。`AutoMappingState` 通道（`algorithm/automappingstate.h`，2026-08-31 自动化多 Agent 运行留下的脚手架，`pyfile/` 无调用者）已于 2026-09-28 移除，提交 82175f2；见任务记录 `long_time_work/2026-09-28-文档语义分级重构.md`。）
