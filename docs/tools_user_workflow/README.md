# 工具的用户操作（tools_user_workflow）

一级语义的一部分：每个工具一页，钉住用户看到什么、做什么、得到什么。从属于 `../user-workflow.md`（主线的第 3、6、7 步展开到这里），与它冲突时以它为准。这里不写怎么实现；Auto Mapping 一族的规格与机制在 `../auto_mapping/`（同为一级），其余工具的机制在 `../mechanism/`（三级）。

**改一个工具的用法，先改这一页，用户拍板后再改代码；执行中一级不可改。新增工具**：Agent 先起草这里的一页和 `../product_features.md` 里的承诺，用户拍板后开工。

每页固定四节：**在哪**（入口是按钮、面板还是菜单）、**怎么用**（按用户动作顺序）、**会看到什么**（状态、提示、变化）、**边界**（用户会碰到的限制和前提）。

| 工具 | id | 在哪 | 页 |
|---|---|---|---|
| 箭头（选择与编辑） | arrow | Tools ▸ Painting；菜单 Arrow | `arrow.md` |
| 画笔 | pen | Tools ▸ Painting；菜单 Draw | `pen.md` |
| 橡皮（擦除、删线、切线） | eraser | Tools ▸ Painting | `eraser.md` |
| 填充 | fill | Tools ▸ Painting；Layers 右键 | `fill.md` |
| 变形 | transfer | Tools ▸ Painting | `transfer.md` |
| 连接 | connect | Tools ▸ Painting | `connect.md` |
| 斥力板 | repulsion_pad | Windows ▸ Repulsion Pad | `repulsion_pad.md` |
| 中轴线（H / V Center Line） | center_line | Tools ▸ Mapping | `center_line.md` |
| Mapping Area | mapping_area | Tools ▸ Mapping | `mapping_area.md` |
| 附加线 | additional_line | Tools ▸ Mapping；菜单 Auto Mapping | `additional_line.md` |
| Auto Mapping（运行与单元） | auto_mapping | Tools ▸ Mapping；Layers 右键；菜单 Auto Mapping | `auto_mapping.md` |
| 视平线 | horizon_line | 菜单 Auto Mapping | `horizon_line.md` |
| To 3D | to_3d | 菜单 Auto Mapping | `to_3d.md` |

`id` 是代码里的工具名（`PaintOpenGLWidget::Tool` 的小写，或 `pyfile/extra_tools.py` 里的 `name`）。旧版本的 Mapping 页曾有一个 **Midline** 按钮：hook 系统的遗留示例，只打印日志，没有用户功能。用户 2026-09-28 决定删除，同日已从代码中删除，不另立页。

图层、时间轴、洋葱皮、历史、纹理板、导入导出不是工具，见 `../user-workflow.md` 第 4、5、8、9 步。
