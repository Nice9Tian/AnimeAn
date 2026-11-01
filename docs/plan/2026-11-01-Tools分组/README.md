# Tools 面板分组

用户 2026-11-01 定：去掉 Painting / Mapping 两个分页，两组工具同在 Tools 面板，每组可折叠。

已定决策：

- Mapping 组用图标格子，图形定稿前用文字占位。
- 折叠状态下次启动沿用，存本机。
- 工具换到折叠的组时自动展开，图层策略切页同样展开对应的组。

实现：`webui/design/ui/scene.json`、`logic/app.logic.yaml`、`components/QtGroupHeader`、`components/QtToolChip`，逻辑在 `webui/src/ui/tools.js`，机制见 `../../mechanism/windows_theme.md`。测试：`webui/tests/tool_groups.test.ts`。

`tools_groups.html` 是用户审核过的设计稿，保留作记录；Mapping 组后来改成了图标格子，以软件为准。

一级文档（`user-workflow.md`、`tools_user_workflow/`、`glossary.md`）的「Painting 组」「Mapping 组」用户 2026-11-01 批准。
