# AnimeAn Backend Debug MCP (`tools/mcp-backend-debug`)

Zero-intrusion, cross-agent **Model Context Protocol (MCP)** server and Chrome dev launcher for debugging `AnimeAnBackend.exe` live while testing drawing workflows in Google Chrome.

Works out-of-the-box across **Antigravity**, **Claude Code**, **Codex CLI**, **Cursor**, **Gemini CLI**, and any **Streamable HTTP** MCP client—without modifying `backend/`, `pyfile/`, `webui/`, or `src-tauri/`.

## Architecture

1. **Passive WebSocket Observer (`src/backend_client.mjs`)**
   - Uses AnimeAnBackend's multi-client WebSocket support safely alongside Chrome.
   - Never sends `input`, `texture.setHome`, or `board.capabilities`, so Chrome always keeps `m_presenter` board dimensions and WebGL2 `fieldDisplaySupported: true`.
   - Releases its socket automatically after any pre-browser inspection call (`800x600`/`400x400` startup defaults) so Chrome takes `m_presenter` cleanly when opened.
   - Immediately ACKs `board` frames so backend frame/field queues never stall.
2. **Out-of-Band Python Probe (`python/_mcp_probe.py`)**
   - Invoked on demand via `python.run` with `scriptDir` pointing to `tools/mcp-backend-debug/python`.
   - Passes structured JSON through temp IPC files under `<gitCommonDir>/mcp_ipc/` so `window.pythonDebug` stays a single `[mcp_probe] ok:<op>` summary line.
   - Dynamically reloads `_mcp_probe` per call (`importlib.reload`) while keeping hook trace state in `sys._animean_mcp_trace_state`.
3. **Dual-Framing Transport & Declarative Tool Registry (`src/mcp_protocol.mjs`, `src/registry.mjs`)**
   - Stdio (`mcp_server.mjs`) auto-detects both NDJSON (Claude Code, Codex, Cursor) and `Content-Length` headers, and echoes responses in the caller's exact framing.
   - Streamable HTTP (`mcp_server.mjs --http 19440`) serves `POST /mcp` and `GET /health` on `127.0.0.1`.
   - All 14 tools are registered declaratively in `src/tools/*.mjs`.

## Quick Start

### 1. Start Backend + Vite + Chrome for Live Testing

```powershell
.\dev_chrome.ps1
```

Useful variants:

```powershell
.\dev_chrome.ps1 -Action status
.\dev_chrome.ps1 -Action stop
.\dev_chrome.ps1 -Action print-config
.\dev_chrome.ps1 -Port 0 -FrontendPort 1435 -NoBrowser
```

### 2. Endpoint Discovery Order

Every MCP tool and CLI status check discovers the running backend automatically in this order:

1. Explicit `wsUrl` tool argument or `--ws <url>` flag / `ANIMEAN_BACKEND_WS_URL` (or `ANIMEAN_WS_URL`) env var
2. Shared repo/worktree session metadata `<gitCommonDir>/animean_dev_backend_session.json` (only if `backendPid` is alive)
3. Local fallback `.scratch/dev_backend_session.json` (only if `backendPid` is alive)
4. Default `ws://127.0.0.1:3432/?token=animean-dev` (`DEFAULT_DEV_PORT` / `DEFAULT_DEV_TOKEN`)

## Multi-Agent Setup

- **Claude Code / Cursor / workspace MCP**: `.mcp.json` at the repo root configures `animean_backend_debug` automatically.
- **Antigravity**: `.agents/plugins/animean-backend-debug/` bundles both `mcp_config.json` (cwd-agnostic bootstrap) and the `animean-backend-debug` workflow skill.
- **Codex CLI / Claude Desktop / Gemini CLI / HTTP**: run `node tools/mcp-backend-debug/cli.mjs print-config` to copy ready-to-paste snippets.

## Available MCP Tools (14)

| Tool | Category | Purpose |
|---|---|---|
| `backend_status` | Connection | Connect/inspect active tool, view, frame/layer selection, dialogs, history, board sizes, `pythonDebug` summary |
| `backend_connect` | Connection | Switch or force-reconnect observer WebSocket URL |
| `backend_launch_dev` | Connection | Start or reuse managed `AnimeAnBackend.exe` + Vite dev server |
| `get_live_state` | State | Read cached pushed state keys and recent transient UI events |
| `get_python_debug_logs` | State | Read/filter `window.pythonDebug` ring buffer (up to 2000 lines), optional `errorsOnly` |
| `get_board_drawlist_summary` | State | Inspect `BoardSurface` op histogram (`T/P/B/G/S/F`), field scopes, and optional op slices |
| `inspect_scene` | Scene | Inspect C++ `AnimeSceneModel` structure, `layerTree`, non-empty cells, `scriptData` sizes, overlays, hooks, `fieldDisplaySupported` |
| `inspect_cell_geometry` | Scene | Extract exact stroke Bezier `commands` or polylines (`toPoly`), raw points, fill regions, bounds, and closedness |
| `inspect_automapping` | Scene | Inspect Auto Mapping `scriptData` units/assets/curve/round-corner stores, guide strokes, and overlays |
| `trace_hooks` | Trace | Start/read/clear/stop `python_hooks.dispatch` ring-buffer trace with move coalescing |
| `render_frame_png` | Snapshot | Render backend frame via `timeline.thumbnails`, write PNG under `.scratch/mcp-snapshots/`, optional inline image |
| `export_repro_bundle` | Snapshot | Export full self-contained JSON repro bundle (and optional `.anproj`) |
| `eval_python` | Action | Evaluate Python in embedded interpreter (do not run while user holds stylus button down) |
| `call_backend_rpc` | Action | Call `BackendSession` RPC method (`history.undo`, `tool.select`, `layer.select`, etc.) |

## Extending with New Tools

1. Add a tool descriptor object `{ name, description, inputSchema, annotations, handler }` in an existing or new module under `tools/mcp-backend-debug/src/tools/`.
2. Register it in `tools/mcp-backend-debug/src/tools/index.mjs` via `registry.registerAll(...)`.
3. Add probe helper ops in `tools/mcp-backend-debug/python/_mcp_probe.py` if direct C++/Python model inspection is needed.

## Verification

```powershell
node --test tools/mcp-backend-debug/tests/unit.test.mjs
node --test tools/mcp-backend-debug/tests/integration.test.mjs
```
