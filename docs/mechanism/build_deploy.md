# 构建、测试与部署（build_deploy）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`CMakeLists.txt`、`backend/CMakeLists.txt`、`algorithm/core/CMakeLists.txt`、`build_scripts/agent_build.ps1`、`build_scripts/package_tauri.ps1`、`build_scripts/toolchain_location.ps1`、`sync_pyfiles.ps1`、`setup_python_pybind11.bat`、`pyfile/pydeps.py`、`pywheels/`。2026-09-28 从架构文档下沉。

## 目录

| 位置 | 内容 | 说明 |
|---|---|---|
| `dist\AnimeAn\` | 部署产物：`AnimeAn.exe`（Tauri 外壳）、`AnimeAnBackend.exe`、MinGW 运行库、`python312\`、`pyfile` 与 `animemodel.py` / `toonz_to_dict.py` 的副本、`pywheels\`、`childrenpanel\tool_controls\` | 不入库；用户测试用的就是它 |
| `build\Release`、`build\wt-check`、`webui\dist`、`src-tauri\target` | C++、网页与 Rust 外壳的构建目录 | 不入库 |
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
- **只改 Python 时**跑 `sync_pyfiles.ps1`：把 `pyfile\*.py`、`pythonbind\animemodel.py`、`opentoonz_tools\toonz_to_dict.py` 与 `pywheels\` 按哈希同步到 `build\` 和 `dist\` 下每个含 `AnimeAn.exe` 的目录，并提示比源码旧的可执行文件。`build_scripts/package_tauri.ps1` 按通配复制 `pyfile\*.py`，新模块不用登记；`backend/CMakeLists.txt` 只复制 Python 的 DLL。
- 脚本副本在 `dist\AnimeAn\` 的根目录（`AnimeAn.exe` 旁）；打包时另放的 `dist\AnimeAn\pyfile\` 等子目录副本没人读，`sync_pyfiles.ps1` 也不更新。开发机上源码 `pyfile\` 在 `PYTHONPATH` 里排第一，所以源码优先；例外是 `initalize.py`（按文件执行，先找 exe 旁的副本）和 `animemodel.py`、`toonz_to_dict.py`（源码目录排在 exe 目录之后），改这三个要先跑 `sync_pyfiles.ps1`。发布文件夹从 exe 旁加载。

## 测试

| 项 | 命令 | 什么时候跑 |
|---|---|---|
| Python 回归（无 GUI） | `py tests\t_<名>.py`，或一次全跑 `py -m unittest discover -s tests`（`test_legacy_suites.py` 把 `t_*.py` 逐个当子测试） | 改到 `pyfile/` |
| C++ 单测 | 配置时 `BUILD_TESTING` 开着，`ctest` 跑 `projectio_roundtrip`、`animemodel_asset_gc`、`strokefit_zoom_invariance`、`board_headless_test`、`textpath_tests`、`platform_tests` | 改到 `algorithm/`、`projectio`、`backend/board/`、`backend/platform/` |
| 基础类型差分测试 | 配置时传入 `-DCMAKE_PREFIX_PATH=C:\Qt\6.9.1\mingw_64` 找到 Qt6 才会编译 `core_*` 目标并与真实 Qt 对比；只测这一层时单独配置 `algorithm/core`，见 `algorithm/core/README.md` | 改到 `algorithm/core/` |
| 后端会话冒烟 | `node backend/tests/session_smoke.mjs build\Release\AnimeAnBackend.exe` | 改到 `backend/`、`openglwidget.*` |
| 新旧对比（金标准） | 后端全链路：`node backend/tests/golden/record.mjs --backend <AnimeAnBackend.exe> --out <输出.jsonl>` 与 `node backend/tests/golden/compare.mjs <旧.jsonl> <新.jsonl>`；计算层：`tests\core\golden_run.ps1` | 底层类型或画板 / 会话改动后 |
| 网页单测与类型检查 | 在 `webui/` 下跑 `npm test`、`npx tsc --noEmit` | 改到 `webui/` |
| 外壳单测 | `cargo test --manifest-path src-tauri/Cargo.toml` | 改到 `src-tauri/` |
| 部署验证 | `agent_build.ps1` 结束行 `EXIT_CODE=0` | 改到 C++ / 网页 / 外壳后合并前 |

`t_*.py` 把 `pyfile` 加进 `sys.path`，用 `types.ModuleType` 造一个 `animean_python` 桩，直接 `import auto_mapping`：纯几何函数和 `build_mapper` 不需要 GUI。系统 PATH 上的 `python` 是无效的 Store 存根，用 `py` 启动器或 `dist\AnimeAn\python312\python.exe`。

## 依赖

- 嵌入运行时是完整的 python312（含 site-packages 与 pip）。`pywheels\` 存钉版本的 wheel（cp312 / win_amd64）与 `requirements.txt`；`pydeps.ensure("模块")` 首次缺库时 `pip --no-index --find-links pywheels` 离线安装，无匹配 wheel 才走网络，全失败返回 None。
- 新依赖：`pip download -d pywheels --only-binary :all: <包>`，更新 `requirements.txt`，装进 `tools\python312` 与 `dist\AnimeAn\python312`，wheel 入库。

## 分支与部署

- `main` 是集成分支。功能在 worktree / 特性分支（`claude/*` 等）上做，完成后以 `--no-ff` 合并回 `main`（提交信息沿用「Merge …（with review fixes）」的写法），然后推 `origin`。
- 合并后在主仓库重建 dist：用户测试的是主仓库的 `dist\AnimeAn\AnimeAn.exe`，worktree 的构建产物与它隔离。
- 分支在 worktree 里检出时主仓库不能同名检出，直接 merge 分支引用即可。
