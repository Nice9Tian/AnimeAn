# 开发指南（developer_guide）

给修改本仓库的开发者（人或 coding agent）的规则。新增内容前先读本文；按改动范围再读下面表里的文件。规则只写在这里，根目录 `CLAUDE.md` 只指向本文。

本文自身的分级：「语义分级」和「规则」两节是一级（用户定，Agent 只能提议）；「先读什么」「验证」「提交与发布」三张表是三级（Agent 可以加行、改命令，改了告诉用户）。

## 语义分级（硬约束）

`docs/` 里的语义分三级。**做决定按级别顺序：先满足一级，再二级，再三级；冲突时上级为准。** 拿不准属于哪一级，按高的一级办。级别以 `README.md` 的索引为准，不以被谁引用为准。

| 级 | 管什么 | 放在 | 谁定 |
|---|---|---|---|
| **一级** | 产品目标、用户体验、整体架构、开发规则，以及 **Auto Mapping 的全部内容**。改了它，产品就不再是这个产品 | `product_purpose.md`、`user-workflow.md`、`tools_user_workflow/<工具>.md`、`architecture.md`、本文的「语义分级」与「规则」、`auto_mapping/`（算法规格、各条管线、映射单元，**连阈值和常量一起**） | **用户**。Agent 只能提议，不能改 |
| **二级** | 产品功能：系统对用户承诺什么。改了它，用户会看到不同的行为或能力 | `product_features.md` | 计划时**用户拍板**（Agent 起草）；执行时穷尽解法表仍过不去可裁，标〔裁〕并告诉用户 |
| **三级** | 机制：怎么做到。改了它，用户看不出区别。阈值、参数、文件格式、内部协议、模块划分（Auto Mapping 的除外）都在这一级 | `mechanism/<链路>.md`、代码目录 README（如 `../opentoonz_tools/README.md`）、代码文件头注释、本文的三张表 | Agent **可改，必须告诉用户**；守住一级二级即可 |

判断一条内容属于哪一级，看改了它用户会不会看到不同：会看到不同的行为或能力，是二级；看不出区别，是三级；改了连「这是什么产品、给谁用、怎么用、由哪几层组成、Auto Mapping 怎么算」都变了，是一级。

- **语义高于代码。** 代码和文档冲突时按文档改代码，并告诉用户冲突在哪、改了什么。
- **一级只有用户能改。** 计划时和执行时都一样。Agent 认为一级有问题，把「修改前 / 修改后」列出来等用户拍板，不先动手。Auto Mapping 的常量也是一级：改一个阈值先改 `auto_mapping/` 里的文档并拍板，再改代码。
- **二级：计划时用户拍板，执行时可裁。** 做计划时，新增或修改的承诺由 Agent 起草进 `product_features.md`，列「修改前 / 修改后」，用户拍板后才开工。执行时穷尽解法表仍达不到，才最小修改那一条：标〔裁〕、告诉用户、继续做，不停下来问。用户可以推翻，推翻的部分连代码一起改回。
- **三级：可改，必须通知。** 计划时和执行时 Agent 都可以改机制，写进对应的机制页，改了就告诉用户，不中断；只要不让二级的承诺变样。执行时改不动的同样标〔裁〕。
- 无人值守的长时间工作按 `long_time_work/README.md`：阶段权限、解法表怎么穷尽、〔裁〕的写法、任务记录都在那里。
- **一句话里前半是承诺、后半是机制的，拆成两句分放两级。** 数字放三级；Auto Mapping 的数字例外，留在 `auto_mapping/`（一级）。
- **二级和三级成对。** `product_features.md` 的一项对应 `mechanism/` 某页或 `auto_mapping/` 某节；机制从属于功能，冲突以功能为准。没有机制内容的功能不必硬配一页。
- **一级文档里指向三级的链接必须标「三级」。** 一级页末尾写「机制（三级）：…」；指向 `auto_mapping/` 的写「规格（一级）：…」。
- **功能名对齐 `glossary.md`。** 功能一律写 Auto Mapping，不写"映射"；单元写 Auto-Mapping 单元，区域写 Mapping Area。
- **Agent 开发的顺序**：读一级 → 起草二级（`product_features.md`）和 `plan/` 里的施工计划，改工具用法的连 `tools_user_workflow/<工具>.md` 一起起草 → 用户拍板一级、二级 → 执行：三级写进 `mechanism/`，改了就告诉用户；过不去的二级三级按〔裁〕处理，不停 → 做完对照拍板的内容逐条核对 → 汇报。

## 先读什么

| 改动范围 | 先读 |
|---|---|
| 任何新功能 | 一级三份：`product_purpose.md`（是不是目标范围内）、`user-workflow.md`（用户在哪一步看到它）、`architecture.md`（C++ 基础函数还是 Python 功能）；再看 `product_features.md` 有没有已有承诺 |
| Auto Mapping 的算法、轴线、附加线、单元、折叠、To 3D | `auto_mapping/`（一级，先看 `auto_mapping/README.md` 索引）、`tools_user_workflow/auto_mapping.md` 及相关工具页；改动前对照 `tests/t_*.py` 里对应的套件 |
| 某个工具的用法或选项 | `tools_user_workflow/<工具>.md`（一级）→ `mechanism/painting_tools.md`、`mechanism/stroke_fitting.md`、`mechanism/fill_layers.md` |
| 图层、填充层、工具锁定 | `mechanism/fill_layers.md`、`user-workflow.md` 第 4 步 |
| 事件、绑定、菜单、选项面板、设置窗 | `mechanism/hook_events.md`、`mechanism/python_binding.md` |
| 撤销、scriptData | `mechanism/history_scriptdata.md` |
| 窗口、面板、纹理板归属、主题、洋葱皮 | `mechanism/windows_theme.md` |
| 工程文件、导入格式 | `mechanism/project_files.md` |
| 构建、同步脚本、测试、依赖、部署 | `mechanism/build_deploy.md` |
| 模块在哪、启动链、装载顺序 | `mechanism/modules.md` |
| 术语和界面文案 | `glossary.md` |
| 无人值守的长时间工作 | `long_time_work/README.md`（阶段权限、〔裁〕、任务记录） |

## 规则

### 分工

- **C++ 通用，Python 具体。** C++ 只写基础函数：手势与渲染、模型与几何、声明式界面、通用开关、事件派发。功能的含义（工具流程、属性名、颜色、数据结构、菜单内容、图层联动、Auto Mapping 全部算法）在 `pyfile/*.py`。C++ 里出现工具名、属性名、颜色常量或「if 工具 == …」就是放错了地方：抽成机制，语义留在 Python。
- **C++ 内建的基础工具只有三个**：Pen、Eraser、视口缩放平移。其余工具一律 Python；不为了性能把功能搬回 C++。
- **机制 / 策略成对。** 加一个 C++ 机制就要有一个 Python 策略模块用它；handle 事件是工具面。**新工具**只需 `PaintOpenGLWidget::Tool` 一个枚举项、`toolName` / `toolspanel` / `toolcontrolconfig::fileNameForTool` 各一处、加一个 Python 模块；漏一处会静默拿到画笔的选项面板。
- **Python 层装会变的东西。** 实验性算法、随功能扩展变化的逻辑都在 Python。现成的库能省事就用，wheel 入 `pywheels/`、经 `pydeps.ensure()` 加载并保留降级路径；这是不重复造轮子，不是原则。

### 共享与空间

- **共享轮子唯一。** 贝塞尔劈分（`algorithm/beziersplit.h` ↔ `pyfile/bezier.py`）、屏幕 ↔ 画布换算（`algorithm/viewscale.h` ↔ `pyfile/viewscale.py`）只有一个家，两边镜像、调用处注释指回。劈分永不改变画出的形状，切片不重拟合。
- **决定空间再写常数。** 手和眼的量（抖动、点选容差、按住不动半径、最小可见宽度）是屏幕像素，经 `viewscale` 换算；作品的量（几何、弧长、步长）是画布像素，不换算。

### 事件与状态

- 前置事件（`fillrequest`、`visibility`）的处理器要在**第一次改模型之前**置 `message["handled"] = True`；C++ 把异常吞成调试文本，处理器中途崩了和没人处理分不开。
- 处理器已经改了模型却不想让 C++ 提交历史（例如捕获失败、笔画已移除）时置 `message["cancel_history"] = True`；脚本自己改模型后调 `ui.history_commit(label, view)`。
- 新事件先在 `python_hooks.set_hook` 加开关并推订阅掩码；C++ 派发前查掩码，无人订阅的事件不组包。
- 工具状态存 scriptData：经 `script_store.write(scene, 自己的键, …)`，只动自己的键。覆盖层一律经 `overlay_stack.set_items(view, owner, items)`，不直接调 `ui.set_overlay`。
- `animean_python.get_scene()` 返回的是信息字典，不是模型对象；取场景先用 `__main__` 里的 `main_model` / `child_model`。

### Auto Mapping

- `auto_mapping/` 是一级：算法、性质、阈值都由用户定。改一个常量，先在对应文档列「修改前 / 修改后」拍板。
- 不糊合：一对多的地方裁断拓扑，不用残差或插值糊成连续。任何新的折叠分析要从 `hv` 位置或角点钳制的切向推导。
- `tests/t_*.py` 是各条链路的权威回归；改 `pyfile/auto_mapping.py` 必跑，新行为加套件。

### 部署与脚本

- 新增 `pyfile/` 模块要进 `CMakeLists.txt` 的**两份**复制清单（构建后复制、`deploy_AnimeAn`），漏了就不进 `dist\AnimeAn\`。
- 改了 `.py` 不重建时跑 `sync_pyfiles.ps1`；用户测试用的是主仓库的 `dist\AnimeAn\AnimeAn.exe`。
- 系统 PATH 上的 `python` 是无效的 Store 存根；用 `py` 启动器或 `dist\AnimeAn\python312\python.exe`。
- 程序不落盘用户数据：文档级状态进 scriptData，机器级偏好才进 QSettings。

### 文档

- 改了用户会看到不同行为的，先改 `product_features.md`（二级）并由用户拍板，再改代码；改工具用法的先改 `tools_user_workflow/<工具>.md`（一级）；动到一级的同样等用户拍板。执行中过不去的按 `long_time_work/README.md` 裁。
- 机制写 `mechanism/<链路>.md`，新链路加一页并在 `mechanism/README.md` 索引加一行；Auto Mapping 的机制写 `auto_mapping/`（一级，拍板后写）。
- 新术语进 `glossary.md`；界面文案照抄界面写法，代码标识沿用既有写法，不为统一外观改名破坏接口。
- 文件名不用中文标点、不用双后缀；提到文档写文件名，不用编号简称。
- 新功能开工前在 `plan/` 加一份计划：目标、已定决策、施工顺序、风险；做完回填实现方法、文件、测试结果。

## 验证

进入 `main` 的代码要过下面的基线。

| 项 | 命令 | 什么时候必跑 |
|---|---|---|
| Python 回归（无 GUI） | `py -m unittest discover -s tests`（`test_legacy_suites.py` 把 `t_*.py` 逐个当子测试跑）；单跑 `py tests\t_<名>.py` | 改到 `pyfile/` |
| C++ 单测 | 配置时 `BUILD_TESTING` 开着，`ctest` 跑 `projectio_tests`、`animemodel_tests`、`strokefit_tests` | 改到 `algorithm/`、`projectio.*` |
| 编译检查 | `cmake --build build\wt-check --target AnimeAn --parallel 10`（新 worktree 先 `git submodule update --init --depth 1`） | 改到 C++，合并前 |
| 部署构建 | `PowerShell -ExecutionPolicy Bypass -File .\build_scripts\agent_build.ps1`，结束行 `===== AGENT BUILD DONE EXIT_CODE=0 =====`；必须在沙箱外 | 改到 C++ 合并后；发布前 |
| 脚本同步 | `powershell -ExecutionPolicy Bypass -File sync_pyfiles.ps1` | 每次改 `.py` 而不重建 |
| 真机验证 | 在 `dist\AnimeAn\AnimeAn.exe` 里按 `tools_user_workflow/` 的步骤手测 | 改到用户看得见的行为 |
| 文档链接 | `py build_scripts\check_doc_links.py`（反引号里的 `.md` 路径都要存在） | 改到 `docs/` 或根目录的 README / AGENTS / CLAUDE |

## 提交与发布

- `main` 是集成分支。功能在 worktree / 特性分支上做，测试与审查过了以 `--no-ff` 合并回 `main`（提交信息沿用「Merge: …（with review fixes）」的写法），然后推 `origin`。
- 合并后在主仓库跑一次 `agent_build.ps1` 重建 `dist`，用户看的是它。分支在 worktree 里检出时主仓库直接 merge 分支引用，不用 checkout。
- 提交信息一句话说清用户看到什么变了。
- 发布形态是 `dist\AnimeAn\` 打成的压缩包，解开即用，不做安装器。发布前全量跑一遍验证表，`dist` 不入库。
- 新依赖：`pip download -d pywheels --only-binary :all: <包>`，更新 `requirements.txt`，装进 `tools\python312` 与 `dist\AnimeAn\python312`，wheel 入库。
