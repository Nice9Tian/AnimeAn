# 图层管理重构：属性图层与 Auto-Mapping 单元

**适用版本**：2026-08-25 图层管理重构之后
**回归套件**：`tests/t_units.py`（单元生命周期，十一项断言）；既有 10 个 t_* 套件全部保持通过（无单元时的旧工作流逐位不变）

---

## 1. 概念：一个 automapping_layer = 一个"映射单元"

图层面板现在支持**属性图层**：

| 面板右键项 | 实体 | 说明 |
|---|---|---|
| New Line Layer | 普通 vector 列 | 线稿层 |
| New Fill Layer | fill 列（`add_fill_layer`） | 填充层 |
| New Auto-Mapping Layer | **带 `tag="automapping"` 的图层组** + 一个常驻主成员层 | 映射单元 |

一个映射单元拥有自己的**全套配置**：两块板的 H/V 轴、映射区、附加线、
nearest 锚点、显示开关与生成选项。配置存在 `scriptData["mapping_units"]`
里、以组 id 为键（main 场景存自己板的资产 + 单元 meta；child 场景存自己板
的资产）——随撤销快照与工程存档一起走。**图层栈里只有输出**；配置是权威，
输出随时可以从配置重新生成。

“复制一个 automapping_layer 再微调”因此就是复制配置：右键 → Duplicate
Auto-Mapping Layer 建新组、拷配置、自动重渲染。旧的 Re-expand（expander）
及其轴快照层（H axis / V axis / Nearest Point 子组）整体退役。

## 2. 焦点语义

- **焦点进入单元**（main 板当前层落在单元的任一成员上，包括点击组行——
  组行点击现在会聚焦组内第一个成员层）：显示该单元的 H/V 轴、附加线、
  映射区、nearest 手柄、参考网格等覆盖层（受该单元的 Advanced Settings
  勾选控制）。
- **焦点离开单元**：以上覆盖层全部自动消失。
- child 板的图层焦点**不影响**单元激活——纹理板的图层承载共享图案，
  用户正是在那里编辑轴线/图案。
- 文档里**没有任何单元**时保持旧行为（覆盖层常显、手动运行按钮），一切
  既有测试与旧工程不受影响。

机制：新的 C++ 钩子事件 **`layerchange`**（`notifyLayerChangedIfNeeded`，
与 `framechange` 同一基线模式；消息携带 `layer`、稳定的 `layer_id` 与
`previous`）。Python 侧 `_layer_focus_event` 经
`scene.group_id_for_layer(index, tag)` 解析归属单元。

## 3. 实时渲染（Live Re-render）

焦点在单元内时，以下**提交点**自动重跑映射（不再需要手动点 Auto Mapping，
也不删旧层）：

- 拖动 H/V 轴释放（同时自动重建参考网格）；重画轴（center-line 工具捕获）；
- 附加线新建/删除；nearest 手柄拖动释放；映射区/轴的 x 删除；
- 工具面板生成选项变更（curve mode、RDP、Front/Back Split、Crease、
  补全拓扑、Bridge k、lining shade）；
- **纹理板图案编辑**（linefinish / erase / delete / fill / move）。

重跑是**就地替换**（`_install_unit_output`）：新输出层并入单元组
（`add_layers_to_group`）→ 焦点先移到新的 front 层 → 只删除单元**登记过**
的旧成员（用户手动拖进组里的层不动）→ 更新成员登记（每层记录
role=front/back/seal 与 depth，由 `_MappedOutput.layer_roles` 提供）。
单元的 Live Re-render 勾选可整体关掉自动重跑。重跑期间 `_RUN_GUARD`
屏蔽自触发的 layerchange/图案事件回声。

## 4. Advanced Settings（右键 → 设置窗）

图层右键菜单现在支持 `kind:"settings"` 条目（与菜单栏同一 `ToolOptPanel`
+ `openScriptSettings` 声明式机制；provider 在右键时把目标单元 stash 进
`_SETTINGS_TARGET`，因为 C++ 设置窗按设计不携带行上下文）。每单元勾选项：

| 勾选 | 作用 |
|---|---|
| H Axis / V Axis | 对应轴覆盖层显隐 |
| Additional Lines / Mapping Area / Nearest-Point Handle | 对应覆盖层显隐 |
| Refer-Rect Grid（+ Grid Density 5/9/17/33） | 参考网格 |
| Occluded Areas (texture board) | 纹理板遮挡染色 |
| Front / Front Lines、Back / Back Lines、Crease Lines | **直接映射为对应成员层的可见性** |
| Live Re-render | 本单元的自动重跑总开关 |

旧的全局显示入口（View 菜单的 Mapping Refer Rect / Occluded Areas）仅在
无单元的旧文档里继续生效；单元模式下网格/遮挡显隐一律由激活单元的设置
决定（`refer_rect_enabled` / `_occlusion_enabled` / `_grid_divisions`
统一解析）。

## 5. C++ 通用机制（本次新增，全部工具无关）

| 机制 | 位置 |
|---|---|
| `layerchange` 钩子事件 + `m_pythonNotifiedLayer` 基线 | `openglwidget.cpp/h` |
| `AnimeLayerNode.tag`（组 kind 字符串）+ 存档 + 撤销 | `algorithm/animemodel.h`、`projectio.cpp` |
| `setLayerGroupTag` / `layerGroupTag` / `groupIdForLayer(index, tag)`（沿路径取最内层命中） | `algorithm/animemodel.cpp` |
| `addLayersToGroup(gid, indices)`（createLayerGroup 缺失的另一半） | `algorithm/animemodel.cpp` |
| 绑定：`set_layer_group_tag` / `layer_group_tag` / `group_id_for_layer` / `add_layers_to_group`；`layer_tree()` 透出 `tag` | `pythonbind/python_bindings.cpp` |
| 图层右键菜单：空白处右键（kind `panel`）、条目支持 `kind:"settings"`/`separator`、上下文携带 `layer_id`/`tag`/`owner_group`/`owner_tag` | `mainwindow.cpp`、`pyfile/python_hooks.py` |
| 组行点击聚焦首个成员层 | `mainwindow.cpp` |
| `python_hooks.set_hook(..., layerchange=True)` | `pyfile/python_hooks.py` |

策略（哪个单元、显示什么、何时重跑）全部在 `pyfile/auto_mapping.py` ——
机制/策略分界与既有架构一致。

## 6. 已知边界

1. 图层面板只在图层**有 cell 的帧**上显示该层：单元在建立它的帧上可见。
2. 成员登记按稳定列 id；用户把别的层拖进单元组不会被重跑删除，但也不会
   被当作 front/back/crease 控制。
3. `_MAPPING_ASSETS` 旧全局仍在（无单元文档、以及 t_* 测试直接使用）；
   `_assets_for` 是唯一分流点。
4. 单元 meta 只存于 main 场景；child 场景只存它自己板的资产——各自的
   撤销恢复各自的部分（与旧 per-view 存储同构）。
5. Advanced Settings 窗按"右键时 stash 的目标单元"工作（C++ 设置窗不携带
   行上下文）：窗口开着期间再右键另一个单元会把已开窗口静默重定向到新
   目标。一次只开一个设置窗即可避免。
6. 单元组被整组删除后，配置在下一次运行时自动"重新安家"到新组（或在
   重新载入时被清理）；期间的孤儿配置无害。
