# 系统相关的代码（platform）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准。发布对象是 Windows（`../product_purpose.md`）；Linux 是开发用的第二个系统，后端、测试和外壳都能在上面编译运行，没有 Linux 发布包。2026-11-17 起，计划见 `../plan/2026-11-17-Linux开发环境.md`，Linux 上怎么构建见 `build_deploy.md`「在 Linux 上开发」。

## 系统调用收在哪

要问操作系统的 C++ 代码只在下面几处，各有一份 Windows 实现和一份 Linux 实现；其余代码不包含系统头文件。

| 位置 | 管什么 | Windows | Linux |
|---|---|---|---|
| `algorithm/core/native.h` | 文件读写、目录、当前目录 / 临时目录 / 设置目录 / 程序路径、进程号、单调时钟 | `native_win32.cpp`（宽字符 API，长路径加 `\\?\` 前缀） | `native_posix.cpp`（UTF-8） |
| `algorithm/core/environment.h` | 环境变量的读写、路径列表的分隔符（`;` 或 `:`） | 同上两个文件 | 同上 |
| `backend/platform/sockets.*` | 127.0.0.1 上的 TCP：监听、接受、收发、轮询、休眠 | Winsock | POSIX 套接字 |
| `backend/platform/process.*` | 进程级的系统设置（退出省电节流） | `SetProcessInformation` | 空实现 |
| `backend/board/textglyphs.h` | 界面字体的字形轮廓和度量 | `textglyphs_gdi.cpp`（GDI） | `textglyphs_freetype.cpp`（Fontconfig 找字体，FreeType 取轮廓） |
| `pythonbind/python_environment.*` | 嵌入解释器的 `PYTHONHOME`、`PYTHONPATH` 和 `PATH` | 运行时目录和 `DLLs` 进 `PATH` | 运行时的 `bin` 进 `PATH` |
| `src-tauri/src/lib.rs` 里的 `#[cfg(windows)]` / `#[cfg(not(windows))]` | 后端随外壳退出、后端退出的监视、对话框的模态、菜单挖洞 | 作业对象、进程句柄、窗口句柄与窗口区域 | `prctl`、轮询子进程；模态和挖洞没有实现（见下） |
| `build_scripts/backend_env.mjs` | Node 脚本启动后端时的环境、程序文件名（带不带 `.exe`） | MinGW 运行库和 Python 的目录进 `PATH` | 只加程序自己的目录 |

上层各自一份、两个系统共用：`algorithm/core/file.cpp`（`File`、`SaveFile`）、`fileinfo.cpp`（`FileInfo`、`Dir`）、`settings.cpp`、`backend/platform/eventloop.cpp`、`backend/net/wsserver.cpp`、`backend/board/textpath.cpp`（排版、缩放、缓存）。CMake 里按 `WIN32` 选实现文件和链接库：`algorithm/core/CMakeLists.txt`、`backend/CMakeLists.txt`、`backend/platform/CMakeLists.txt`。

路径文字的读法不属于系统层：`algorithm/core/pathtext.h` 在每个系统上都把 `/` 和 `\` 当分隔符、把 `X:` 当盘符、把 `//server/share` 当 UNC 根，所以 Windows 上写的工程在别的系统上打开，里面的相对路径意思不变。

## 两个系统上不一样的行为

| 事 | Windows | Linux |
|---|---|---|
| 设置文件 | `%APPDATA%\AnimeAn\settings.json` | `$XDG_CONFIG_HOME/AnimeAn/settings.json`，没设时是 `~/.config/AnimeAn/settings.json` |
| 文本模式写文件 | `\n` 写成 `\r\n` | 原样 |
| 文本模式读文件 | `\r\n` 读成 `\n` | 一样，Windows 写的文件读出来相同 |
| 隐藏的目录项 | 文件属性 | 名字以点开头 |
| 界面字体（画板上的文字） | Segoe UI、Microsoft YaHei UI、Yu Gothic UI，按这个顺序找有这个字的 | 同样按顺序找有这个字的，每种 Windows 字体后面跟着它的替身：Segoe UI、Noto Sans、DejaVu Sans、Microsoft YaHei UI、Noto Sans CJK SC、Yu Gothic UI、Noto Sans CJK JP，最后是系统的 sans-serif；没装的跳过 |
| 定时器和轮询的粒度 | 约 15.6 ms | 毫秒级 |
| 监听套接字的 `SO_REUSEADDR` | 不开（在 Windows 上它允许两个进程同时监听一个端口） | 开：监听的进程退出后端口马上能再用，固定端口的后端重启不用等 |
| 向已断开的连接发送 | 返回错误 | 用 `MSG_NOSIGNAL` 发送，同样返回错误而不是收到 `SIGPIPE` |
| 后端随外壳退出 | 作业对象 | `prctl(PR_SET_PDEATHSIG)` |
| 对话框模态时把输入交给最上面的对话框（`focus_modal`） | 按窗口 Z 序找 | 只把焦点给主窗口 |
| 菜单不被浮出的面板挡住（`set_menu_area`） | 给上面的窗口挖洞 | 不做，面板会挡住菜单 |
| 退出省电节流 | 做 | 没有这回事 |

由此得出的须知：

- 新旧录制对比（`backend/tests/golden/`）只在同一个系统上比。字体不同，文字的路径就不同；两边各自录、各自比。2026-11-17 实测同一份代码在 Linux 上连录两次完全一致，Windows 上改动前后完全一致。
- `std::int64_t` 在 Windows 上是 `long long`，在 64 位 Linux 上是 `long`。`JsonValue` 为此多了一个 `long` 的构造函数；`Settings` 的整数接口用 `long long`。新代码里跨系统的整数接口照这个写。
- 外壳在 Linux 上能编译、能启动后端、能开窗口，对话框模态和菜单挖洞是退化的。Linux 上手测界面走浏览器（`build_deploy.md`），不靠外壳。

## 测试

| 测什么 | 在哪 |
|---|---|
| 文件层和环境变量，对着真实文件系统，两个系统都跑 | `tests/fileio_tests.cpp`（ctest `fileio`） |
| 文件层和设置与真实 Qt 的差分 | `tests/core/file_tests.cpp`、`settings_tests.cpp`（要 Qt6，只在 Windows 开发机上编） |
| 事件循环、定时器、套接字就绪 | `backend/platform/tests/platform_tests.cpp` |
| WebSocket 服务端 | `backend/net/tests/ws_client_test.mjs`（对 `ws_echo`） |
| 字形和文字路径 | `backend/board/tests/textpath_tests.cpp` |
