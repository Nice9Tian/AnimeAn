# 模块与启动（modules）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。本页是角色边界、启动链、装载顺序和模块清单；2026-09-28 从架构文档下沉，按文件头注释整理。

## 角色

| 角色 | 位置 | 管什么 | 不管什么 |
|---|---|---|---|
| **应用壳** | `main.cpp`、`mainwindow.*`、`parentwindow.*`、`centralpaintarea.*`、`paintviewcontainer.*`、`subcontrolframe.*`、`windowedgeresize.*`、`theme.*`、`selectionattention.*` | 启动、嵌入 Python 解释器、菜单栏、窗口层级（带页签的父窗）、面板归属（纹理板单一所有权路由）、主题、帧 / 层 / 资产的选择约束、导入导出、历史的全局排序 | 工具语义、几何算法 |
| **画板** | `openglwidget.*` | 渲染场景与覆盖层、视图变换（缩放 / 平移）、手势路由（Tool 枚举）、把手 / 覆盖层 / 视图按钮机制、hook 事件派发、历史提交、播放缓存、轴向吸附与拟合调用 | 任何工具"意味着什么" |
| **核心模型库** `animean_core` | `algorithm/animemodel.*`、`algorithm/vectorlogic.*`、`algorithm/beziersplit.*`、`algorithm/scenehistory.*`、`algorithm/viewscale.h`、`projectio.*` | 场景 / xsheet 数据模型（列、帧、资产、单元格、图层组与 tag、父层、锁定）、矢量几何与笔画拟合、精确贝塞尔劈分、快照式历史、`.anproj` / `.textureview` 的 JSON 读写 | 界面、Python |
| **面板** | `childrenpanel/*` | 工具栏、声明式选项面板、图层树、资产表、历史表、时间轴、纹理面板、调色板控件、斥力板、新建对话框 | 工具语义 |
| **Python 绑定** | `pythonbind/python_bindings.*`（`animean_python` 模块）、`pythonbind/animemodel.py`（高层封装） | 把模型、几何、`ui` 门面暴露给 Python；把 Python 值转成 Qt 类型 | 任何策略 |
| **工具脚本** | `pyfile/*.py` | 模块装载顺序、hook / 菜单 / 设置 / 视图按钮注册表、工具选项布局、每个工具的行为、Auto Mapping 引擎、第三方库加载 | 渲染、手势采集、模型存储 |
| **导入器** | `clipreader.*`、`opentoonz_tools/toonz_to_dict.py` | 解 `.clip` 矢量笔画；解 `.tnz` / `.pli` | 栅格数据 |
| **共享轮子** | `algorithm/beziersplit.h` ↔ `pyfile/bezier.py`；`algorithm/viewscale.h` ↔ `pyfile/viewscale.py` | 贝塞尔劈分与求值；屏幕 ↔ 画布换算。两边镜像、唯一实现 | 步长密度策略（留给调用方） |
| **第三方与运行时** | `external/pybind11`（子模块）、`tools/python312`（不入库）、`pywheels/`、`pyfile/three_vendor.py` | pybind11；完整 Python 运行时；钉版本的 wheel；离线 three.js | — |
| **测试** | `tests/t_*.py`、`tests/test_legacy_suites.py`、`tests/*_tests.cpp` | Python 引擎的无 GUI 回归；C++ 模型 / 文件 / 拟合的 ctest | — |
| **构建与同步** | `CMakeLists.txt`、`build_scripts/agent_build.ps1`、`build_scripts/qt_env_location.ps1`、`sync_pyfiles.ps1`、`setup_python_pybind11.bat` | 配置、编译、`deploy_AnimeAn` 打包、脚本同步、首次环境准备 | — |

机制 / 策略成对：`ui.set_overlay` ↔ `overlay_stack`；handle 事件 ↔ `edit_tool` / `transfer_tool` / `connect_tool`；`fillrequest` ↔ `fill_tool`；`visibility` ↔ `visibility_tool`；`ui.set_locked_tools` / `ui.set_fill_paint_mode` ↔ `layer_tool_policy`；`ui.windows` ↔ `window_manager`；调色板控件 ↔ `palette_box` / `tool_colors`；斥力板 ↔ `repulsion_tool`。

## 启动链

```
AnimeAn.exe
 1. 默认 GL 表面格式开多重采样（在 QApplication 之前）
 2. AnimeTheme::apply：Fusion 风格 + 保存的深 / 浅色调色板（QSettings）
 3. 定位 Python：PYTHONHOME = exe 旁 python312\，没有则用构建时记下的 tools\python312
    PYTHONPATH = 源码 pyfile\（存在时最优先）→ exe 目录 → pythonbind\ → opentoonz_tools\
 4. 启动嵌入解释器
 5. 构造 MainWindow：中央区（Drawing | Texture）、各父窗与面板、菜单、时间轴、两块板各自的历史基线、Python Debug 窗
 6. 运行 initalize.main()：按 BOOTSTRAP_MODULES → PYTHON_FILE_MODULES 的顺序导入，再发现目录里其余脚本；
    模块在导入时登记 hook、菜单、设置窗、视图按钮、受保护属性、洋葱皮引导属性
 7. extra_tools.tools_json() 生成 Mapping 页按钮；脚本菜单挂上两条菜单栏；状态栏显示 hello_world
 8. 新建对话框询问画布尺寸；主窗口显示
```

装载顺序有意义：`toolcontrol`、`python_hooks` 先于一切；`tool_colors` 先于 `palette_box`（后者从前者的缓存取种子）；`window_manager` 与 `fill_tool` 先于 `layer_tool_policy`。开发机上源码 `pyfile\` 排在 exe 目录之前，所以改脚本不重建也生效；发布的文件夹没有源码树，从 exe 旁加载。`bind_test()` 会改场景，只能从调试窗手动跑，启动不跑。

## C++ 模块

| 文件 | 职责 |
|---|---|
| `main.cpp` | 启动链 1–4；Python 路径与 DLL 路径 |
| `mainwindow.*` | 窗口装配、菜单、面板归属、导入导出、撤销 / 重做的全局排序、Python 全局变量同步、播放驱动、脚本菜单 / 设置窗 / 图层右键菜单的构建、工具锁定的应用 |
| `openglwidget.*` | `PaintOpenGLWidget`：场景渲染、覆盖层、把手、视图变换、手势与工具路由、hook 派发与订阅掩码、历史提交、播放缓存、无边界画布模式、背景模式、活动板指示 |
| `centralpaintarea.*` | 中央区两页（drawing / texture），页签样式与父窗一致；`ui.windows` 名 `paint` |
| `parentwindow.*` | 带页签的停靠窗：名字给脚本寻址、标题给用户看 |
| `paintviewcontainer.*` | 画板 + 与视图变换联动的滚动条 + 底部一条可选的时间轴区 |
| `subcontrolframe.*` | 可嵌入任一宿主面板或浮动的子控件框；注册表按名字找框、按位置找宿主 |
| `windowedgeresize.*` | 无边框顶层窗的四边拉伸 |
| `theme.*` | `AnimeTheme`：颜色角色的唯一出处，深 / 浅模式，`themeChanged` |
| `selectionattention.*` | 帧 / 层 / 资产选择的一致性约束 |
| `projectio.*` | `.anproj` / `.textureview` 的 JSON 序列化与校验 |
| `clipreader.*` | `.clip` 容器遍历与矢量块解码 |
| `algorithm/animemodel.*` | `AnimeSceneModel`：列（vector / fill / raster）、帧、资产、单元格、笔画 / 填充 / 栅格、图层组与 tag、父层、内部层、锁定、scriptData |
| `algorithm/vectorlogic.*` | `AnimeVectorLogic`：笔画构造与拟合（稳定器、拟合预算、实时增量拟合）、命中、擦除区间、切断计划、区域填充、路径 ↔ 折线 |
| `algorithm/beziersplit.*` | 精确贝塞尔劈分与路径定位 |
| `algorithm/scenehistory.*` | 快照式撤销 / 重做，全局序号 |
| `algorithm/viewscale.h` | 屏幕 ↔ 文档换算 |
| `childrenpanel/toolspanel.*` | 内建工具图标条 + 脚本声明的额外工具按钮（按页路由） |
| `childrenpanel/tooloptpanel.*` + `toolcontrolconfig.*` + `tool_controls/*.json` | 声明式选项面板：JSON 布局（slider / list / check / button / color / subwindow / settings），`visible_when` 联动，选项变化走 option hook |
| `childrenpanel/layerpanel.*`、`assetpanel.*`、`historypanel.*` | 图层树、资产表、历史表 |
| `childrenpanel/timelinewindow.*` | 时间轴停靠窗：播放条即标题栏、横 / 竖条、洋葱皮与引导线开关、帧命令 |
| `childrenpanel/texturepanel.*` | 纹理面板：自己的菜单栏（File / Setting / View）、脚本按钮行、画板容器；可在三个家之间搬迁 |
| `childrenpanel/palettecontrol.*` | 调色板控件（HSV 为权威） |
| `childrenpanel/forcepad.*` | 二维向量输入板（斥力板的机制） |
| `childrenpanel/newprojectdialog.*` | 画布尺寸对话框 |
| `pythonbind/python_bindings.*` | `animean_python`：`SceneModel`、`VectorImage`、`VectorStroke`、`Cell`、`vectorlogic`、`model_pybind`、`get_scene` / `get_current`、`ui` 门面及各回调注册 |
| `tests/*_tests.cpp` | `projectio_tests`、`animemodel_tests`、`strokefit_tests` |

## Python 模块

| 文件 | 职责 |
|---|---|
| `pyfile/initalize.py` | 装载顺序与目录发现；`bind_test()` |
| `pyfile/python_hooks.py` | 注册表：hook（含订阅掩码推送）、菜单（按宿主 main / child）、菜单提供者、设置窗、视图按钮、受保护属性、洋葱皮引导属性与层 tag；`dispatch` |
| `pyfile/toolcontrol.py` | 内建与脚本工具的选项布局：Mapping 页所有工具共用的运行策略块、Stabilizer 滑条只是稳定器 |
| `pyfile/draw_settings.py` | Draw 菜单与 Draw Setting 窗：稳定器 / 简化 / 尖角三参数推给 C++ |
| `pyfile/tool_colors.py` | 每个工具自己的颜色缓存与策略 |
| `pyfile/palette_box.py` | 调色板的色块集合（存 scriptData） |
| `pyfile/edit_tool.py` | Arrow 的三种编辑模式；Arrow 菜单（选中轮廓的显示设置） |
| `pyfile/transfer_tool.py` | Transfer 的框、把手、旋转与修饰键 |
| `pyfile/connect_tool.py` | Connect 的吸附与三种连接 |
| `pyfile/fill_tool.py` | 填充策略：落在哪层、范围升级、重复点击、跟随线稿层、To Independent Layer |
| `pyfile/layer_tool_policy.py` | 当前层决定 Tools 页与锁定的工具、填充层上的区域刷 |
| `pyfile/visibility_tool.py` | 图层可见性开关的策略 |
| `pyfile/repulsion_tool.py` | 斥力板：基线场、临时内部层预览、拓扑保持的施加 |
| `pyfile/window_manager.py` | 窗口 / 页策略（`ui.windows` 的调用方） |
| `pyfile/overlay_stack.py` | 多个工具的覆盖层合成到一张显示列表 |
| `pyfile/script_store.py` | scriptData 的命名空间读写 |
| `pyfile/viewscale.py`、`pyfile/bezier.py` | 两只共享轮子的 Python 镜像 |
| `pyfile/pydeps.py` | 第三方库自愈安装 |
| `pyfile/extra_tools.py` | Mapping 页按钮清单与处理器解析 |
| `pyfile/auto_mapping.py` | Auto Mapping 引擎全体：轴线 / Mapping Area / 附加线 / 视平线资产、映射器、曲线重建、裁断、折痕、单元与实时重跑、参考网格、遮挡染色、洋葱皮引导、To 3D、Auto Mapping 菜单与两块板的 View 菜单、设置窗、图层右键菜单 |
| `pyfile/hook_test.py`、`hello_world.py`、`linefinish.py`、`bind_test.py` | 示例、调试与兼容垫片 |
| `pyfile/three_vendor.py` | 离线 three.js（以 `.py` 形式随脚本通道部署） |
| `pythonbind/animemodel.py` | `AnimeModel` / `LayerRef` 等高层封装、`ui` 门面、`current` |
| `opentoonz_tools/toonz_to_dict.py` | OpenToonz 解析器（也可命令行） |

## 代码组织约定

- **共享轮子唯一。** 贝塞尔劈分（`algorithm/beziersplit.h` ↔ `pyfile/bezier.py`）、屏幕 ↔ 画布换算（`algorithm/viewscale.h` ↔ `pyfile/viewscale.py`）只有一个家，C++ 与 Python 各一份镜像，调用处注释指回；劈分永不改变画出的形状，切片不重拟合；步长密度策略留给调用方。
- **决定空间再写常数。** 手和眼的量（抖动、点选容差、按住不动半径、最小可见宽度）是屏幕像素，经 `viewscale` 换算；作品的量（几何、弧长、步长）是画布像素，不换算。手势预算按屏幕像素一次性换算，几何按真实缩放。
- **省事就用库，不重复造轮子。** Python 侧有现成库能用的（numpy、三角化）就直接用，不是原则，是省事：wheel 入 `pywheels/`，经 `pydeps.ensure()` 加载——首次缺库先从随包 wheel 离线安装，再退网络，全失败返回 None 由调用方降级（例如三角化退回纯 Python 实现）。
- **新工具**：`PaintOpenGLWidget::Tool` 一个枚举项，`toolName` / `toolspanel` / `toolcontrolconfig::fileNameForTool` 各加一处，其余是一个 Python 模块；漏了任一处会静默拿到画笔的选项面板。

## 已核实的边角

- `tool_controls\*.json` 只在 Python 的选项布局取不到时作兜底（`loadBuiltInToolLayout`）；`pen.json` / `fill.json` 里的三个固定色块按钮因此只在无 Python 时出现，内容陈旧但无害。改它是改代码资产，本次只记录。
- 没有 Python 热重载：改脚本后重启程序是唯一支持的方式。

## 待核

（无。`AutoMappingState` 通道（`algorithm/automappingstate.h`，2026-08-31 自动化多 Agent 运行留下的脚手架，`pyfile/` 无调用者）已于 2026-09-28 移除，提交 82175f2；见任务记录 `long_time_work/2026-09-28-文档语义分级重构.md`。）
