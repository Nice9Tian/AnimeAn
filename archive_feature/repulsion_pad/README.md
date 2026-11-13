# Repulsion Pad / 斥力板（已归档）

已从发布版中退役归档。`pyfile/initalize.py` 不再加载该模块，后端窗口表与 `Windows` 菜单不再注册 `repulsion_pad`，网页场景 `webui/design/ui/scene.json` 也移除了 `forcePadDock`。

## 目录内容

| 文件 / 目录 | 原位置与用途 |
| --- | --- |
| `repulsion_tool.py` | 原 `pyfile/repulsion_tool.py`：基线斥力场缓存、临时内部层实时预览、保持拓扑的位移写回 |
| `force_pad.js` | 原 `webui/src/ui/panels2.js` 中的 `createForcePad`：十字坐标 + 单位圆手柄控件 |
| `QtForcePad/` | 原 `webui/design/components/QtForcePad/`：VibeFrontend 设计稿占位组件声明 |
| `repulsion_pad.md` | 原 `docs/tools_user_workflow/repulsion_pad.md`：斥力板的用户操作说明 |

## 仍留在主树中的通用机制

按照「C++ 通用，Python 具体」原则，以下底层通用机制未随本工具删除：

- `AnimeColumn.internal`（临时内部层：渲染在最上层，不入面板、不写盘）
- `animean_python.ui.displace_begin / displace_scale / displace_end`（按顶点位移向量缩放的预览通道）
- `pad.event` 调用、`pad` 状态键、`animean_python.ui.set_pad_value` 与 `pad` hook 事件（通用二维手柄事件与状态回写）
- `webui/src/ui/qt3.css` 中的 `--pad-handle` / `.pad-page` 样式与 `webui/src/i18n/*.json` 中的 `"Repulsion"` / `"Repulsion Pad"` 词条

## 归档时未解决的问题

本功能在排查引导线捕获顺序（`CAPTURE_HOOK_ORDER` / `message["captured"]`）后续项时决定直接归档，以下行为取舍**未解决**：

- `repulsion_tool._canvas_changed` 订阅了 `linefinish` 等画板事件，但没有检查 `message.get("captured")`。因此用户在画任何引导线（H/V 轴线、Mapping Area、Additional Line、Free Out Line，包括被拒绝的手势或在纹理板上画轴线）时，`_BASELINE` 都会被清空，斥力板手柄也会跳回 `(0, 0)`。
- 若仅在 `_canvas_changed` 开头加一行 `if message.get("captured"): return`，引导线手势不会清空斥力基线；但在主画板上画引导线触发 Auto Mapping 实时重跑（Live Re-render）时，映射输出笔画已经重新生成，而旧 `_BASELINE` 只认识重跑前的笔画，保留旧基线只对手绘层继续有效、对新生成的映射层无效。究竟该保留手绘层的基线还是因重跑而作废基线，属于未定的产品取舍；现随整个工具一并归档。

## 如何恢复

1. 将 `repulsion_tool.py` 移回 `pyfile/repulsion_tool.py`，并在 `pyfile/initalize.py` 的 `PYTHON_FILE_MODULES` 中恢复 `"repulsion_tool"`（同时从 `_ARCHIVED_MODULES` 移除）。
2. 将 `QtForcePad/` 移回 `webui/design/components/QtForcePad/`，并在 `webui/design/ui/scene.json` 中恢复 `forcePadDock` 与 `forcePadPanel` 节点及右侧停靠组。
3. 将 `force_pad.js` 中的 `createForcePad` 合回 `webui/src/ui/panels2.js`（或直接从同目录导入），并在 `webui/src/ui/app.js` 中恢复 `DOCK_FOR_WINDOW.repulsion_pad = "forcePadDock"` 与 `QtForcePad` 面板工厂。
4. 在 `backend/session/backendsession.cpp` 的窗口表初始化中恢复 `addWin("repulsion_pad", "Repulsion Pad", ...)`。
5. 将 `repulsion_pad.md` 移回 `docs/tools_user_workflow/repulsion_pad.md`，并恢复相关文档索引与 `backend/tests/golden/scenarios.mjs` 中的 `repulsion_pad` 场景。
6. 如需一并处理上述引导线与斥力基线的交互问题，先决定主画板触发 Auto Mapping 重跑时的基线策略。

精确移除片段可查阅创建本目录的 Git 提交记录。
