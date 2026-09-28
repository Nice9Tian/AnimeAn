# 机制（mechanism）

三级语义：怎么做到。每页一条链路，从属于 `../architecture.md`，与它冲突时以它为准；Auto Mapping 一族的算法不在这里，在 `../auto_mapping/`（一级）。Agent 可以改这里的内容，改了告诉用户。

| 文件 | 链路 | 状态 |
|---|---|---|
| `modules.md` | 角色边界、启动链、装载顺序、C++ 与 Python 模块清单、代码组织约定 | 初稿（2026-09-28 从架构文档下沉） |
| `hook_events.md` | C++ → Python 事件、Python → C++ 绑定、声明式界面、回传约定、覆盖层显示列表 | 初稿（同上） |
| `history_scriptdata.md` | 快照式历史、全局序号、scriptData 命名空间 | 初稿（同上） |
| `windows_theme.md` | 窗口层级、`ui.windows`、纹理板单一所有权、主题、洋葱皮范围、工具锁定开关 | 初稿（同上） |
| `project_files.md` | `.anproj` / `.textureview`、QSettings、导入格式、样例文件 | 初稿（同上） |
| `build_deploy.md` | 构建脚本、脚本同步、测试基线、分支与部署、依赖 wheel | 初稿（同上） |
| `python_binding.md`、`python_binding_zh.md` | Python 绑定 API（英文参考 / 中文说明，中文的「双画板与工具」一节已按现行界面改写） | 2026-09-28 从根目录移入 |
| `stroke_fitting.md` | 画笔采样、稳定器、拟合预算、实时增量拟合、轴向吸附、按住画直线、擦除与切断 | 初稿（2026-09-28） |
| `painting_tools.md` | Arrow 编辑模式、Transfer、Connect、Repulsion Pad、每工具颜色与调色板 | 初稿（2026-09-28） |
| `fill_layers.md` | 填充策略、跟随线稿的填充层、图层决定工具（锁定、区域刷）、可见性与图层锁定 | 初稿（2026-09-28） |
