---
name: animean-backend-debug
description: Debug live AnimeAnBackend.exe geometry, Auto Mapping scriptData, hook traces, draw-lists, and rendered PNGs via MCP while the user tests drawing in Chrome.
---

# AnimeAn Backend Debug Workflow

Use the `animean_backend_debug` MCP tools when the user is testing AnimeAn in Google Chrome (or another browser) and asks you to diagnose a bug, inspect live canvas/stroke data, trace Python hooks, or capture a full reproduction bundle.

## 1. Starting or Connecting to a Live Dev Session

- From a shell, the user can launch Backend + Vite + Chrome in one step:
  ```powershell
  .\dev_chrome.ps1
  ```
- Or start/reuse the session via MCP tool `backend_launch_dev` (`openBrowser: true` if Chrome should be opened).
- Always check connection health first with `backend_status` (auto-discovers `<gitCommonDir>/animean_dev_session.json`, `.scratch/dev_session.json`, `ANIMEAN_WS_URL`, or default port `3432`).
- Use `backend_connect` only when overriding or reconnecting to a specific `wsUrl`.

## 2. Post-Drawing Bug Triage (User Says "Something Just Broke")

1. `backend_status` — confirm endpoint, active view (`main` / `child`), active tool, current frame/layer, open dialogs, and history position.
2. `get_python_debug_logs` (`errorsOnly: true`, then `errorsOnly: false`) — check for Python exceptions or `[python] ...` tracebacks first.
3. `inspect_scene` — inspect non-empty cells across `main` and `child`, `scriptDataSummary`, active overlays (`overlay_stack`), registered hooks, and `fieldDisplaySupported`.
4. `inspect_cell_geometry` — read exact stroke Bezier `commands` (or `toPoly: true` polylines), `width`, `color`, `property`, `bounds`, `startPoint`/`endPoint`/`isClosed`, fill seeds/regions, and raw points (`includeRawPoints: true`) without flooding `pythonDebug`.
5. `inspect_automapping` — inspect Auto Mapping `scriptData` (units, assets, curve/round-corner stores), guide/property strokes, overlays, and `fieldDisplaySupported`.
6. `get_board_drawlist_summary` — verify what `BoardSurface` / `RecordingDevice` actually sent to the browser (`histogram` of `T/P/B/G/S/F`, active deformation field scopes `F`, or slice raw ops).
7. `render_frame_png` — render the real backend frame offscreen (`inlineImage: true` for multimodal inspection, or open the saved `.png` in `.scratch/mcp-snapshots/`).
8. `export_repro_bundle` — save a complete offline JSON bundle (scenes, cells, `scriptData`, hook trace buffer, draw-lists, live state) under `.scratch/mcp-snapshots/`.

## 3. Live Hook Tracing (While User Draws in Chrome)

1. Call `trace_hooks` with `{ "action": "start" }` (optionally filter with `eventsFilter`, e.g. `["linefinish", "handle", "fillrequest"]`; `coalesceMoves: true` keeps pen drags from evicting press/release/`linefinish`).
2. Tell the user to perform the drawing action in Chrome.
3. Call `trace_hooks` with `{ "action": "read" }` (incrementally with `sinceSeq`) to inspect timing (`durationMs`), `matchedHooks`, `handledAfter`, `cancelHistory`, and exceptions.
4. Call `trace_hooks` with `{ "action": "stop" }` (and optionally `clear`) when finished.

## 4. Direct Inspection & RPC Actions

- `get_live_state`: inspect cached pushed state keys (`window`, `tools`, `toolOptions`, `drawColor`, `drawSettings`, `layers/main`, `layers/child`, `timeline`, `history`, `dialogs`, `windows`, `viewport/*`, `py/*`) and recent `event` messages.
- `eval_python`: evaluate read-only queries or small repairs inside the live embedded Python interpreter (`main_model`, `child_model`, `animean_python`).
- `call_backend_rpc`: invoke `BackendSession` RPC methods (`history.undo`, `history.redo`, `tool.select`, `layer.select`, `frame.select`, etc.).

## Safety Rules

- **Never call `eval_python` while the user holds the stylus/mouse button down**: `BackendSession` finishes an active pen stroke on `python.run`. Wait until the user finishes the stroke or stop tracing safely first.
- **Do not inject `input`, `texture.setHome`, or `board.capabilities`** through `call_backend_rpc`: the MCP client intentionally stays a passive observer so Chrome retains `m_presenter` board dimensions and WebGL2 `fieldDisplay` support.
- Print ready-to-paste configs for Codex CLI, Claude Desktop, Gemini CLI, Cursor, or Streamable HTTP `--http 19440` with:
  ```bash
  node tools/mcp-backend-debug/cli.mjs print-config
  ```
