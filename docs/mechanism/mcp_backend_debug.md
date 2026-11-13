# Chrome 联调与后端实时调试 MCP

> 在用户用 Chrome 连接 `AnimeAnBackend.exe` 做交互绘画测试时，给 Antigravity、Claude Code、Codex、Cursor、Gemini CLI 等 AI Agent 提供直接、无损、结构化的后端观测与取证通道。

定位与索引见 [README.md](README.md)。本文只记录 `tools/mcp-backend-debug/` 工具集的设计依据、多页面共存边界与扩展点；不改动 `backend/`、`pyfile/`、`webui/` 或 `src-tauri/` 任何运行时代码。

---

## 为什么做后端直连 MCP

用户在 Chrome 里手测绘画或 Auto Mapping 时，如果问题已经发生：

- 让主对话再去派子 Agent 用浏览器检查页面，中间只能拿到转述后的文字与截图摘要，拿不到**精确笔画拟合结果、曲线控制点、填充种子、`scriptData` 内部键值与 `python_hooks` 调用耗时**。
- 后端本身持有权威状态（见 [backend_protocol.md](backend_protocol.md) 与 [python_binding.md](python_binding.md)）：C++ `AnimeSceneModel`、嵌入 Python 解释器状态、`BoardSurface` / `RecordingDevice` 指令表、`window.pythonDebug` 环形日志，全部可以通过一条只读/旁路 WebSocket 拿到。

---

## 核心设计边界

### 1. 严格被动的多端共存

`BackendSession` 支持多个 WebSocket 页面同时连接同一后端，并按如下规则决定画板尺寸与场显示能力：

- 只有发送过 `input`、`resize`、`texture.setHome` 的页面才会接管 `m_presenter`（呈现权）。
- `fieldDisplaySupported` 取所有已汇报 `board.capabilities` 的连接页面的逻辑与；如果调试连接汇报 `fieldDisplay: false` 或发送 `resize`，就会拖垮 Chrome 的 WebGL2 场显示或覆盖 Chrome 画布尺寸。

因此 `tools/mcp-backend-debug/src/backend_client.mjs` 遵守三条硬约束：

1. **绝不主动发送** `input`（含 `resize`）、`texture.setHome` 或 `board.capabilities`；对 `board` 帧立即回 `ack`，不阻塞后续帧队列。
2. **Pre-browser 主动让出呈现权**：`BackendSession` 初始化 `main` 为 `800x600`、`child` 为 `400x400`。若调试端在 Chrome 尚未连入（画板仍为初始默认尺寸且 `trace_hooks` 未激活）时做了一次巡检，`withBackendClient` 会在调用结束后调用 `releaseIfNoBrowserPresenter()` 断开连接，确保随后打开 Chrome 时 Chrome 立即拿到 `m_presenter`。
3. **`eval_python` 提示**：`BackendSession` 收到 `python.run` 会结束一次尚未松笔的拖拽；因此仅在用户静止查看状态时调用 `eval_python`，实时跟笔调试改用 `trace_hooks` 事先埋点、事后读取。

### 2. 带外文件 IPC（不污染 `pythonDebug`）

`window.pythonDebug` 在后端侧上限为 2000 条日志。若把整帧几何控制点或 Auto Mapping 单元树直接 `print(...)` 到 `stdout`，一次查询就会刷爆用户面板上的调试日志。

`tools/mcp-backend-debug/python/_mcp_probe.py` 通过 `<gitCommonDir>/mcp_ipc/` 下的临时请求/响应 JSON 文件完成数据交换：

- Node 端写入 `req_<uuid>.json`，调用 `python.run`（传入 `scriptDir`）执行 `importlib.reload(_mcp_probe).run_ipc(req, resp)`；
- Python 把结构化 JSON 原子写入 `resp_<uuid>.json` 并只打印一行 `[mcp_probe] ok:<op>`；
- `finally` 阶段由 Node 端立即删除临时文件。
- `trace_hooks` 的环形缓冲状态挂在 `sys._animean_mcp_trace_state`，因此即使每次调用执行 `importlib.reload(_mcp_probe)` 也不会丢失已挂载的钩子状态；高频 `handle`/`pad` 移动事件会在缓冲尾部合并，防止笔迹拖动冲掉 `linefinish` 或报错事件。

### 3. 跨 Worktree 自动发现与统一启动

主仓库与各个 `git worktree` 共享同一个 `<gitCommonDir>`。`dev_chrome.ps1` / `tools/mcp-backend-debug/cli.mjs` 启动管理进程后，会把当前实例的 `backendPid`、`backendPort`、`token`、`wsUrl`、`frontendPort`、`chromeUrl` 同时写入 `<gitCommonDir>/animean_dev_backend_session.json` 与 `<repoRoot>/.scratch/dev_backend_session.json`。

服务端点按以下优先级解析：

1. 显式参数 `--ws` / 工具参数 `wsUrl` / 环境变量 `ANIMEAN_BACKEND_WS_URL`
2. `<gitCommonDir>/animean_dev_backend_session.json`（校验 `isPidAlive(backendPid)`）
3. `.scratch/dev_backend_session.json`（校验 `isPidAlive(backendPid)`）
4. 默认 `ws://127.0.0.1:3432/?token=animean-dev`

若当前在未单独 `npm install` 的 git worktree 中启动，启动器会自动复用主仓库的 `webui/node_modules` 启动 Vite；且 Chrome URL 使用 `http://localhost:<frontendPort>/?ws=...` 以满足 `WsServer::isOriginAllowed` 校验。

### 4. 跨 Agent 传输协议与扩展性

- **Stdio 双帧兼容（`src/transport_stdio.mjs`）**：自动识别 NDJSON（Claude Code、Codex CLI、Cursor 等主流客户端）与 `Content-Length` 字节头帧格式，并用同一格式回包（支持 UTF-8 多字节切片与非首行 `Content-Length` 头）。
- **Streamable HTTP（`src/transport_http.mjs`）**：传入 `--http [port]`（默认 `19440`）启动 `POST /mcp` 与 `GET /health`，便于 HTTP 类 Agent 或多客户端共享同一服务。
- **插件与配置文件**：仓库根 `.mcp.json`、`tools/mcp-backend-debug/cli.mjs print-config`（导出 Antigravity / Codex CLI / Claude Desktop / Gemini CLI 配置），以及 `.agents/plugins/animean-backend-debug/`（内置与 `cwd` 无关的 ESM 引导配置与工作流说明 [SKILL](../../.agents/plugins/animean-backend-debug/skills/animean-backend-debug/SKILL.md)）。

---

## 已注册的 14 个工具

| 模块 | 工具名 | 用途 |
|---|---|---|
| `connection.mjs` | `backend_status` | 连接并返回工具、视图、帧/图层选区、弹窗、历史、画板尺寸、`pythonDebug` 摘要 |
| `connection.mjs` | `backend_connect` | 切换目标 `wsUrl` 或触发重新发现/重连 |
| `connection.mjs` | `backend_launch_dev` | 启动或复用受管 `AnimeAnBackend.exe` + Vite 开发环境 |
| `state.mjs` | `get_live_state` | 读取后端实时推送的状态键与最近 `event` UI 事件 |
| `state.mjs` | `get_python_debug_logs` | 过滤并读取 `window.pythonDebug`（支持 `errorsOnly` 与正则） |
| `state.mjs` | `get_board_drawlist_summary` | 读取 `BoardSurface` 指令直方图（`T/P/B/G/S/F`）、变形场作用域与切片 |
| `scene.mjs` | `inspect_scene` | 检查主/辅场景结构、非空格、`scriptData` 大小、overlay 与注册钩子 |
| `scene.mjs` | `inspect_cell_geometry` | 提取指定单元格笔画的贝塞尔命令/采样折线、属性、包围盒、端点与填充区域 |
| `scene.mjs` | `inspect_automapping` | 检查 Auto Mapping 的 `scriptData`、引导线笔画、overlay 栈与 `fieldDisplaySupported` |
| `trace.mjs` | `trace_hooks` | 启停/读取/清空 `python_hooks.dispatch` 环形追踪缓冲（`durationMs`、异常、命中数） |
| `snapshot.mjs` | `render_frame_png` | 调 `timeline.thumbnails` 离屏渲染并编码为 PNG，可选内联返回图像内容块 |
| `snapshot.mjs` | `export_repro_bundle` | 导出含双视图、非空格几何、`scriptData`、trace 与 draw-list 的自包含 JSON 复现包 |
| `action.mjs` | `eval_python` | 在嵌入 Python 中执行表达式或语句 |
| `action.mjs` | `call_backend_rpc` | 直接调用 `BackendSession` RPC 方法（如 `history.undo`、`tool.select`、`layer.select`） |

详细参数与扩展新工具的步骤见 [../../tools/mcp-backend-debug/README.md](../../tools/mcp-backend-debug/README.md)。
