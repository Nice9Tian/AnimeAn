# 构建、测试与部署（build_deploy）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。代码：`CMakeLists.txt`、`build_scripts/agent_build.ps1`、`build_scripts/qt_env_location.ps1`、`sync_pyfiles.ps1`、`setup_python_pybind11.bat`、`pyfile/pydeps.py`、`pywheels/`。2026-09-28 从架构文档下沉。

## 目录

| 位置 | 内容 | 说明 |
|---|---|---|
| `dist\AnimeAn\` | 部署产物：exe、Qt 运行库、`python312\`、`pyfile` 与 `animemodel.py` / `toonz_to_dict.py` 的副本、`pywheels\`、`childrenpanel\tool_controls\` | 不入库；用户测试用的就是它 |
| `build\Desktop_Qt_6_9_1_MinGW_64_bit-Release`、`…-Debug`、`build\wt-check` | 构建目录 | 不入库 |
| `out.txt`、`build_full.log` | 构建脚本的摘要与全量日志 | 不入库 |
| `tools\python312\` | 开发机的 Python 运行时（首次由 `setup_python_pybind11.bat` 准备） | 不入库 |
| `external/pybind11` | Git 子模块 | 新 worktree 要 `git submodule update --init --depth 1` |

## 构建

- **改 C++ 时**跑 `build_scripts\agent_build.ps1`：跨进程互斥排队、清 release 目录、CMake 配置（MinGW Makefiles、Release）、`deploy_AnimeAn --clean-first`（编译、windeployqt、复制 Python 运行时、脚本、wheel 与选项面板 JSON 到 `dist\AnimeAn\`）；结束行 `===== AGENT BUILD DONE EXIT_CODE=0 =====` 为成功。构建慢，必要时才跑；必须在沙箱外运行（autogen 会起子进程）。Qt 路径在 `build_scripts\qt_env_location.ps1`。
- **便宜的编译检查**（合并前）：只构建 `AnimeAn` 目标，不打包：
  ```powershell
  cmake -S . -B build\wt-check -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:\Qt\6.9.1\mingw_64 -DCMAKE_MAKE_PROGRAM=C:\Qt\Tools\mingw1310_64\bin\mingw32-make.exe -DCMAKE_C_COMPILER=C:\Qt\Tools\mingw1310_64\bin\gcc.exe -DCMAKE_CXX_COMPILER=C:\Qt\Tools\mingw1310_64\bin\g++.exe
  cmake --build build\wt-check --target AnimeAn --parallel 10
  ```
- **只改 Python 时**跑 `sync_pyfiles.ps1`：把 `pyfile\*.py`、`pythonbind\animemodel.py`、`opentoonz_tools\toonz_to_dict.py` 与 `pywheels\` 按哈希同步到每个含 `AnimeAn.exe` 的目录，并提示比源码旧的 exe。CMake 的两份复制清单（构建后 + deploy）都要列出每个 `pyfile` 模块，漏了就不会进 dist。
- 三个 exe 目录（build Debug、build Release、dist）各有一份脚本副本；开发机上源码 `pyfile\` 在 PYTHONPATH 里排第一，所以源码优先；发布文件夹从 exe 旁加载。

## 测试

| 项 | 命令 | 什么时候跑 |
|---|---|---|
| Python 回归（无 GUI） | `py tests\t_<名>.py`，或一次全跑 `py -m unittest discover -s tests`（`test_legacy_suites.py` 把 `t_*.py` 逐个当子测试） | 改到 `pyfile/` |
| C++ 单测 | 配置时 `BUILD_TESTING` 开着，目标 `projectio_tests`、`animemodel_tests`、`strokefit_tests`，`ctest` 跑（待核：是否在日常验证里） | 改到 `algorithm/`、`projectio` |
| 部署验证 | `agent_build.ps1` 结束行 `EXIT_CODE=0` | 改到 C++ 后合并前 |

`t_*.py` 把 `pyfile` 加进 `sys.path`，用 `types.ModuleType` 造一个 `animean_python` 桩，直接 `import auto_mapping`：纯几何函数和 `build_mapper` 不需要 GUI。系统 PATH 上的 `python` 是无效的 Store 存根，用 `py` 启动器或 `dist\AnimeAn\python312\python.exe`。

## 依赖

- 嵌入运行时是完整的 python312（含 site-packages 与 pip）。`pywheels\` 存钉版本的 wheel（cp312 / win_amd64）与 `requirements.txt`；`pydeps.ensure("模块")` 首次缺库时 `pip --no-index --find-links pywheels` 离线安装，无匹配 wheel 才走网络，全失败返回 None。
- 新依赖：`pip download -d pywheels --only-binary :all: <包>`，更新 `requirements.txt`，装进 `tools\python312` 与 `dist\AnimeAn\python312`，wheel 入库。

## 分支与部署

- `main` 是集成分支。功能在 worktree / 特性分支（`claude/*` 等）上做，完成后以 `--no-ff` 合并回 `main`（提交信息沿用「Merge …（with review fixes）」的写法），然后推 `origin`。
- 合并后在主仓库重建 dist：用户测试的是主仓库的 `dist\AnimeAn\AnimeAn.exe`，worktree 的构建产物与它隔离。
- 分支在 worktree 里检出时主仓库不能同名检出，直接 merge 分支引用即可。
