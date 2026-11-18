# 构建、测试与部署（build_deploy）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`CMakeLists.txt`、`backend/CMakeLists.txt`、`algorithm/core/CMakeLists.txt`、`build_scripts/agent_build.ps1`、`build_scripts/package_tauri.ps1`、`build_scripts/toolchain_location.ps1`、`build_scripts/backend_env.mjs`、`sync_pyfiles.ps1`、`setup_python_pybind11.bat`、`pyfile/pydeps.py`、`pywheels/`；Linux 上是 `build_scripts/linux_setup.sh`、`build_scripts/linux_build.sh`、`build_scripts/windows_cross_check.sh`、`build_scripts/mingw_cross.cmake`。2026-09-28 从架构文档下沉。

发布包只有 Windows 的，下面各节没有特别说明的都是 Windows 开发机上的做法；Linux 上的做法集中在「在 Linux 上开发」一节。

## 目录

| 位置 | 内容 | 说明 |
|---|---|---|
| `dist\AnimeAn\` | 部署产物：`AnimeAn.exe`（Tauri 外壳）、`AnimeAnBackend.exe`、MinGW 运行库、`python312\`、`pyfile` 与 `animemodel.py` / `toonz_to_dict.py` 的副本、`pywheels\`、`childrenpanel\tool_controls\` | 不入库；用户测试用的就是它 |
| `build\Release`、`build\wt-check`、`webui\dist`、`src-tauri\target` | C++、网页与 Rust 外壳的构建目录 | 不入库 |
| `build/linux`、`build/windows-cross` | Linux 上的开发构建，和在 Linux 上按 Windows 交叉编译的检查构建 | 不入库 |
| `out.txt`、`build_full.log` | 构建脚本的摘要与全量日志 | 不入库 |
| `tools\python312\` | 开发机的 Python 运行时（首次由 `setup_python_pybind11.bat` 准备） | 不入库 |
| `external/pybind11`、`external/clipper2`、`external/stb` | C++ 第三方依赖（`pybind11` 为 Git 子模块） | 新 worktree 要 `git submodule update --init --depth 1` |

## 构建

- **改 C++ / 网页 / 外壳时**跑 `build_scripts\agent_build.ps1`：跨进程互斥排队、清 `build\Release`、CMake 配置（MinGW Makefiles、Release）、构建 `--target AnimeAnBackend --clean-first`，再调 `build_scripts\package_tauri.ps1`（`webui` 的 `npm run build`、`cargo build --release --manifest-path src-tauri/Cargo.toml --features tauri/custom-protocol`，把 `AnimeAn.exe`、`AnimeAnBackend.exe`、MinGW 运行库、`python312\`、脚本、`pywheels\` 与 `childrenpanel\tool_controls\` 拷入 `dist\AnimeAn\`）；结束行 `===== AGENT BUILD DONE EXIT_CODE=0 =====` 为成功。构建慢，必要时才跑；必须在沙箱外运行（编译器、Vite 与 Cargo 会起子进程）。工具链路径在 `build_scripts\toolchain_location.ps1`。
- **便宜的编译检查**（合并前）：只构建 `AnimeAnBackend` 目标，不打包：
  ```powershell
  cmake -S . -B build\wt-check -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM=C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe -DCMAKE_C_COMPILER=C:\Qt\Tools\mingw1310_64\bin\gcc.exe -DCMAKE_CXX_COMPILER=C:\Qt\Tools\mingw1310_64\bin\g++.exe
  cmake --build build\wt-check --target AnimeAnBackend --parallel 10
  ```
  worktree 里没有 `tools\python312`：第一次配置一个构建目录之前，把环境变量 `ANIMEAN_PYTHON_HOME` 设成主仓库的 `tools\python312`。环境变量只在这个目录第一次配置时读，值记在 CMake 缓存里；已经配置过的目录要改，加 `-DANIMEAN_PYTHON_HOME=<目录>`。checkout 里有 `tools\python312` 时用它，环境变量不看。指的目录不存在时配置会给一条警告：编得过，编出来的程序启动时找不到运行时。
- **只改 Python 时**跑 `sync_pyfiles.ps1`：把 `pyfile\*.py`、`pythonbind\animemodel.py`、`opentoonz_tools\toonz_to_dict.py` 与 `pywheels\` 按哈希同步到 `build\` 和 `dist\` 下每个含 `AnimeAnBackend.exe` 的目录（后端读自己旁边的副本；只有旧 `AnimeAn.exe` 的 Qt 时代构建目录不动），并提示比 C++ 源码旧的后端。`build_scripts/package_tauri.ps1` 按通配复制 `pyfile\*.py`，新模块不用登记；两者复制前都先删掉 exe 旁边不在这份来源里的 `.py`（源码里删掉或归档的模块，比如 `archive_feature/` 里的），因为 `initalize.py` 会装载 exe 旁边的每个 `.py`；解压新包覆盖旧文件夹的情况由 `initalize.py` 的 `_ARCHIVED_MODULES` 兜底。`backend/CMakeLists.txt` 只复制 Python 的 DLL。
- 脚本副本只放在 `dist\AnimeAn\` 的根目录（`AnimeAn.exe` 旁），不另放子目录副本。开发机上源码的 `pyfile\`、`pythonbind\`、`opentoonz_tools\` 在 `PYTHONPATH` 里排在 exe 目录之前，解释器也不把工作目录放上 `sys.path`（从资源管理器启动时工作目录就是 exe 目录）；`initalize.py` 同样先在源码目录里找（只写文件名时依次是源码目录、exe 旁、工作目录）。所以改脚本重开就生效，不用同步。发布文件夹没有源码树，从 exe 旁加载；同步脚本是为了让要发出去的 `dist` 带上新脚本。

## 测试

| 项 | 命令 | 什么时候跑 |
|---|---|---|
| Python 回归（无 GUI） | `py tests\t_<名>.py`，或一次全跑 `py -m unittest discover -s tests`（`test_legacy_suites.py` 把 `t_*.py` 逐个当子测试） | 改到 `pyfile/` |
| C++ 单测 | 配置时 `BUILD_TESTING` 开着，`ctest` 跑 `projectio_roundtrip`、`animemodel_asset_gc`、`strokefit_zoom_invariance`、`fileio`、`board_headless_test`、`textpath_tests`、`platform_tests` | 改到 `algorithm/`、`projectio`、`backend/board/`、`backend/platform/` |
| 基础类型差分测试 | 配置时传入 `-DCMAKE_PREFIX_PATH=C:\Qt\6.9.1\mingw_64` 找到 Qt6 才会编译 `core_*` 目标并与真实 Qt 对比；只测这一层时单独配置 `algorithm/core`，见 `algorithm/core/README.md` | 改到 `algorithm/core/` |
| 后端会话冒烟 | `node backend/tests/session_smoke.mjs build\Release\backend\AnimeAnBackend.exe` | 改到 `backend/`、`openglwidget.*` |
| 新旧对比（金标准） | 后端全链路：`node backend/tests/golden/record.mjs --backend <AnimeAnBackend.exe> --out <输出.jsonl>` 与 `node backend/tests/golden/compare.mjs <旧.jsonl> <新.jsonl>`；计算层：`tests\core\golden_run.ps1` | 底层类型或画板 / 会话改动后 |
| 网页单测与类型检查 | 在 `webui/` 下跑 `npm test`、`npx tsc --noEmit` | 改到 `webui/` |
| 外壳单测 | `cargo test --manifest-path src-tauri/Cargo.toml` | 改到 `src-tauri/` |
| 部署验证 | `agent_build.ps1` 结束行 `EXIT_CODE=0` | 改到 C++ / 网页 / 外壳后合并前 |

`t_*.py` 把 `pyfile` 加进 `sys.path`，用 `types.ModuleType` 造一个 `animean_python` 桩，直接 `import auto_mapping`：纯几何函数和 `build_mapper` 不需要 GUI。系统 PATH 上的 `python` 是无效的 Store 存根，用 `py` 启动器或 `dist\AnimeAn\python312\python.exe`。

### 负载敏感性与环境故障诊断

- **回归与基准分离。** 功能回归（`tests/t_*.py`）禁止用墙钟毫秒数断言性能：无窗口 Python 子进程跑几秒后会被 Windows EcoQoS 迁到能效核慢约两倍，核超额占用时调度抢占还会扭曲墙钟比值。`t_*.py` 只断言保证复杂度的确定性结构不变量（PCG 迭代上限与热冷迭代比、缓存复用、向量化批调用次数、包围盒树剪枝候选数，以及建架时实际做了几次精确线段求交）；毫秒级帧预算统一在开启了 `opt_out_of_power_throttling()` 的 `tests/bench_drag_cost.py`（`--warm` / `--preview`）中测量。`t_bezier_knots.py` 原本因测试内纯 Python 双重循环占 99.5% 耗时（50～100 秒），已向量化 `deviation()` 降至约 1 秒。`opt_out_of_power_throttling()` 关闭失败时在 stderr 上说明，此时的毫秒数可能翻倍。
- **套件以 `3221225477`（`0xC0000005`）退出。** 这是 Python 解释器原生崩溃，不是断言失败；`test_legacy_suites.py` 以 `-X faulthandler` 启动子进程并在报错中标明（不自动重跑掩盖崩溃）。2026-11-13 在开发机（i7-14700KF）上排查确认：`t_bezier_knots.py` 658 轮完整运行结果逐位一致，崩溃转储落在 `python312.dll` 内部寄存器保存/恢复错乱；逐核绑核实验显示只钉 8 个性能核（约 5.37 GHz）时 5 次崩溃全部落在逻辑处理器 4（第 5 个性能核），其余 7 个性能核 165 轮零崩溃，20 核全满（约 5.2 GHz）253 轮零崩溃——属开发机第 5 颗性能核高睿频硬件不稳定（重跑或用亲和性掩码 `FFFEF` 避开该核即可）。
- **套件以 `MemoryError` 退出。** 多见于后台泄漏大量无头浏览器耗尽系统可提交内存（Commit），清理泄漏进程后重跑。

## 依赖

- 嵌入运行时是完整的 python312（含 site-packages 与 pip）。`pywheels\` 存钉版本的 wheel（cp312 / win_amd64）与 `requirements.txt`；`pydeps.ensure("模块")` 首次缺库时 `pip --no-index --find-links pywheels` 离线安装，无匹配 wheel 才走网络，全失败返回 None。
- 新依赖：`pip download -d pywheels --only-binary :all: <包>`，更新 `requirements.txt`，装进 `tools\python312` 与 `dist\AnimeAn\python312`，wheel 入库。
- 入库的 wheel 只有 Windows 的（发布包只有 Windows 的）。Linux 开发机的运行时按同一份 `requirements.txt` 从 PyPI 装同样的版本，见下一节。

## 在 Linux 上开发

2026-11-17 起。Linux 上有完整的开发循环：编后端、跑全部自动测试、用浏览器连后端手测、检查改动在 Windows 上还编得过。没有 Linux 发布包；发布前仍要在 Windows 机器上跑 `agent_build.ps1` 和真机验证。系统相关的代码怎么分层、两个系统上哪些行为不一样，见 `platform.md`；计划和回填在 `../plan/2026-11-17-Linux开发环境.md`。

### 机器上要有什么

| 要什么 | 说明 |
|---|---|
| C++17 编译器、CMake（3.16 以上）、Ninja | 实测 GCC 15.2、CMake 4.2、Ninja 1.13；装了 ccache 就自动用 |
| FreeType、Fontconfig 的开发文件 | 画板上的文字转路径要用（Debian / Ubuntu：`libfreetype-dev`、`libfontconfig-dev`） |
| 一份完整的 Python 3.12，带头文件和 `libpython3.12.so` | 环境变量 `ANIMEAN_PYTHON_HOME` 指向它（目录下有 `bin/python3`、`include/`、`lib/`）。小版本和 Windows 包里的一致；python-build-standalone 的 install_only 包解开就能用。这个目录编进程序里，一个构建目录第一次配置时定下来，以后换目录要 `linux_build.sh --clean` |
| 运行时里装好 `pywheels/requirements.txt` 的三个包 | `$ANIMEAN_PYTHON_HOME/bin/python3 -m pip install -r pywheels/requirements.txt` |
| Node.js、Rust | 版本同 Windows 开发机（实测 Node 24、Rust 1.98） |
| WebKitGTK 等外壳的依赖 | 只有编外壳（`cargo test`、`cargo build`）才要（Debian / Ubuntu：`libwebkit2gtk-4.1-dev`、`libgtk-3-dev`、`libayatana-appindicator3-dev`、`librsvg2-dev`、`libssl-dev`、`libxdo-dev`） |
| 字体 | 没有 Segoe UI 等 Windows 字体时用 Noto Sans 和 Noto Sans CJK，见 `platform.md` |
| MinGW-w64（posix 线程版）和一份 Windows 版 Python 3.12 | 只有交叉编译检查才要：`x86_64-w64-mingw32-g++-posix`（Debian / Ubuntu：`g++-mingw-w64-x86-64-posix`）；环境变量 `ANIMEAN_WINDOWS_PYTHON_HOME` 指向带 `include/`、`libs/` 的 Windows Python 目录（python.org 的安装器装出来的，或它发在 NuGet 上的 `python` 包里的 `tools/`）。只用它的头文件和导入库，不运行它，也不用本机的 Python |

### 命令

都在仓库根目录下跑。Linux 上没有 `py` 启动器，用运行时自己的解释器（`$ANIMEAN_PYTHON_HOME/bin/python3`，下面写作 `python3`）。

| 做什么 | 命令 |
|---|---|
| 新 checkout 的首次准备（子模块、`npm ci`、编译、C++ 单测） | `build_scripts/linux_setup.sh` |
| 编后端和 C++ 测试到 `build/linux` | `build_scripts/linux_build.sh`；加 `--test` 编完跑 `ctest`，`--clean` 从空目录开始，`--target <目标>` 只编一个 |
| Python 回归 | `python3 -m unittest discover -s tests` |
| 预览帧预算 | `python3 tests/bench_drag_cost.py --preview --all` |
| 后端会话冒烟、多页面、界面语言覆盖 | `node backend/tests/session_smoke.mjs build/linux/backend/AnimeAnBackend`，`multi_page.mjs`、`i18n_coverage.mjs` 同样 |
| WebSocket 服务端 | `node backend/net/tests/ws_client_test.mjs build/linux/backend/net/ws_echo` |
| 新旧对比 | `node backend/tests/golden/record.mjs --backend build/linux/backend/AnimeAnBackend --out <输出.jsonl>`，再用 `compare.mjs`；基线要在 Linux 上录，不和 Windows 上录的比 |
| 网页 | `webui/` 下 `npm test`、`npx tsc --noEmit` |
| 外壳 | `cargo test --manifest-path src-tauri/Cargo.toml` |
| 后端调试 MCP | `node --test tools/mcp-backend-debug/tests/unit.test.mjs`、`integration.test.mjs` |
| 改动在 Windows 上还编得过吗 | `build_scripts/windows_cross_check.sh`：用 MinGW-w64 把后端和测试程序按 Windows 编译、链接到 `build/windows-cross`。只查编译和链接，产物不运行、不发布 |
| 手测界面 | `node tools/mcp-backend-debug/cli.mjs start --repo-root . --auto --host tailscale` 起后端和 Vite，用 Chrome 打开输出里的 `chromeUrl`；`node tools/mcp-backend-debug/cli.mjs stop --repo-root .` 停掉。参数见下一段 |

手测时浏览器常在另一台机器上。后端只听 127.0.0.1，只认 `http://localhost:<端口>` 来源的页面，这一点不为开发放开。有两种开法：

- **让 Vite 对外**：`start --host <地址>`，Vite 改听这个地址（`tailscale` 表示本机的 Tailscale IPv4 地址），页面经 Vite 的代理 `/__backend` 连后端。代理只放行同源的页面（`Origin` 的主机要等于请求的 `Host`），转给后端时把来源换成 `http://localhost`。`chromeUrl` 里就是这个地址，别的机器（同一 Tailscale 网络里的电脑、手机）直接打开。页面不是安全上下文（`http://` 加 IP），要 `crypto.randomUUID`、剪贴板这类接口的地方得有退路。
- **转发端口**：不加 `--host`，把两个端口都转发过来再开 `chromeUrl`，例如 `ssh -L 1430:localhost:1430 -L 3432:127.0.0.1:3432 <主机>`。

几个账号或会话共用一台机器时各用各的端口。`start --auto` 向端口网关要两个端口：本机有 `port-gate`（或环境变量 `ANIMEAN_PORT_GATE` 指的程序）时从本账号的端口段里分，没有时让系统分空闲端口；会话还活着时沿用它的端口。本机有端口网关时，不加 `--auto` 的端口（含默认的 3432、1430）也要过 `port-gate check`，不在本账号段里就拒绝。启动器只复用会话文件里记着、本会话自己起的后端和 Vite；端口被别的进程占着就报错，不去连它。

### 和 Windows 上不一样的地方

- 没有 `dist`，也不用同步脚本：后端从编译它的那份源码的 `pyfile/` 读脚本，改了重开就生效。
- 没有 `tools/python312`，运行时由 `ANIMEAN_PYTHON_HOME` 指定，可以是几个 checkout 共用的只读目录。只读时缺的包由管理这个目录的人装进去；`pydeps.ensure()` 自己装会落到 pip 的退路上（用户自己的 site-packages）。
- 后端和测试程序没有 `.exe` 后缀。Node 脚本里的程序名和启动环境统一从 `build_scripts/backend_env.mjs` 取，脚本自己不写机器上的目录。
- `tests/ui_pixel/` 和隐藏桌面上的真机测试是 Windows 专用的。
- 2026-11-17 在 Ubuntu 26.04 上实测：`ctest` 7 项、Python 回归、`bench_drag_cost.py --preview --all`（三个装置中位 11 到 14 ms）、会话冒烟、多页面、界面语言覆盖、WebSocket 服务端、网页 176 项（1 项要另指后端，跳过）、外壳 14 项、调试 MCP 8 项全部通过；新旧对比连录两次一致；无头 Chrome 打开界面无报错；交叉编译检查通过。

## 分支与部署

- `main` 是集成分支。功能在 worktree / 特性分支（`claude/*` 等）上做，完成后以 `--no-ff` 合并回 `main`（提交信息沿用「Merge …（with review fixes）」的写法），然后推 `origin`。
- 合并后在主仓库重建 dist：用户测试的是主仓库的 `dist\AnimeAn\AnimeAn.exe`，worktree 的构建产物与它隔离。
- 分支在 worktree 里检出时主仓库不能同名检出，直接 merge 分支引用即可。
