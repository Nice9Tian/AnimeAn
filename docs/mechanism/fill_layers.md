# 填充与图层策略（fill_layers）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准；用户看到什么在 `../tools_user_workflow/fill.md` 和 `../user-workflow.md` 第 4 步。代码：`pyfile/fill_tool.py`（填充策略）、`pyfile/layer_tool_policy.py`（图层决定工具）、`pyfile/visibility_tool.py`（可见性）、`algorithm/animemodel.*`（`fillBoundarySegments`、`AnimeColumn::parentLayerId`、`layerLocked`、图层组与 tag）、`openglwidget.cpp`（`fillrequest` 前置事件、内建兜底填充、`ui.set_fill_paint_mode` 的交互事件放行）、`backend/business/animeanbusiness.cpp` 与 `backend/session/backendsession.cpp`（图层树的嵌套数据、`ui.set_locked_tools` 的应用、右键菜单）、`webui/src/ui/panels.js`（图层面板渲染）。

## 一次填充

1. 用户点下 → C++ 先派发前置事件 **`fillrequest`**（带点位、Fill Scope）。Python 处理后置 `message["handled"] = True`，C++ 就不跑内建填充；没有 Python 时内建填充兜底。
2. `fill_tool` 决定落在哪层：当前层是线稿层时自动新建一个填充层，`parentLayerId` 指向线稿层（**跟随**），填完把当前层还给线稿层；当前层已是填充层则直接用。
3. 范围：Current 只用当前层的线找边界（`fillBoundarySegments(frame, layerIndex)`），ALL 用所有可见层（`layerIndex = -1`）。填充层不能给自己当边界，所以在填充层上 Current 自动升级为 ALL（内建兜底同样处理）。
4. 区域：`scene.fill_boundary_path_at(frame, seed, bounds, layer_index)` 追踪种子周围的区域；点在已有区域里则改色 / 更新而不叠一层（`fill_region_contains`）。
5. 提交：`fill_tool` 重新派发一个合成的 `fillfinish`（cell 描述填充落在哪里），然后一条历史记录。处理器在第一次改模型之前就置 `handled`，不然半改的模型会被兜底再跑一遍。

## 跟随线稿的填充层

- 机制是 `parentLayerId`（稳定列 id，`normalizeLayerTree` 校验），本身不带行为；含义全在 Python：何时设、何时尊重、何时可解除。
- 重算：`TOPOLOGY_EVENTS = (linefinish, erasefinish, deletefinish, historyrestore)` 之后，按每个区域存的种子和它自己存的范围重新追踪；`deletefinish` 同时覆盖 Cut Line 与 Delete Line，`historyrestore` 覆盖撤销、重做与打开文件。种子的范围缓存按 scope 分别算。
- 引导线工具画的线不算线稿改动，哪一层的填充都不为它重算。H / V Center Line 等工具的线画完时还是当前层里的一条普通笔画，由捕获钩子移出图层并在消息上置 `captured`；捕获排在重算之前（`hook_events.md`「钩子的先后」），重算见到 `captured` 直接返回，所以先填色后画轴线与先画轴线后填色结果相同，手势被拒绝时也一样。只靠先后不够：捕获新建单元时图层下标后移，消息里的 `cell.layer` 会指到别的层。捕获也不能改成排在重算之后再补算一遍：轴线横跨一个开口时会让开口暂时闭合，那一次重算会把「种子解析不出区域就保留原样」留下来的区域改写掉，补算时开口又开了，改不回来。
- 面板上显示为线稿层的子项，只是显示，绝不写进分组树。
- 右键 **To Independent Layer** 清掉 `parentLayerId`，提交一条历史，再重新评估工具策略。

## 与 Auto Mapping 输出的关系

- Fill 的 ALL 范围把 Auto Mapping 输出的花纹笔画也当墙：封闭的花纹不被底色灌入；Current 只看当前线稿层。
- Mapping Area 的区域探测（`auto_mapping._detect_region`）总是跳过 `MAPPING_OUTPUT_PROPERTIES`，重跑时上一次的花纹不会把要贴的区域切碎。两者是有意的分工，不是缺陷。

## 图层决定工具（`layer_tool_policy.py`）

每次 `layerchange` 和 `historyrestore` 重新评估并整份推送（上一层的答案不是这一层的，"这里什么都不锁"也要说出来，否则旧锁会活过换层）：

| 当前层 | Tools 页 | 锁定 | 备注 |
|---|---|---|---|
| Auto-Mapping 单元里的层 | Mapping | 无 | 单元用 Mapping 组的工具编辑 |
| 普通线稿层 | Painting | 无 | |
| 跟随线稿的填充层 | Painting | Pen、Eraser、Connect、Transfer | 内容由父层拓扑派生，手改会被下次重算覆盖；Arrow 留着能看，Fill 留着（填充交互事件只是升级自己的范围） |
| 独立填充层 | Painting | Connect | `ui.set_fill_paint_mode` 打开：Pen 画完的笔迹经 `linefinish` 否决转成经过的每个封闭区域的填充，Eraser 原生擦填充区域 |

- 区域刷不转引导线工具的线：它在转换之前已被捕获移出图层，消息带 `captured`，区域刷直接返回。在独立填充层上画轴线得到的是轴线，不刷出填充（`hook_events.md`「钩子的先后」）。
- C++ 的两个开关不知道图层是什么：`ui.set_locked_tools(view, tools)` 拒绝一组工具、图标变暗、退回 Arrow（Arrow 永不锁，是逃生口；切换到被锁工具在芯片和主窗口两处都被拒）；`ui.set_fill_paint_mode(view, on)` 放行 Fill 列上的画笔 / 橡皮交互事件。锁定按活动板生效，活动板切换时重读那块板的锁定集。
- 主画板才有单元焦点；纹理板的图层焦点不影响单元。

## 可见性与锁定

- 图层面板勾选 → 前置事件 `visibility` → `visibility_tool` 经 `scene.set_layer_visible` 施加并置 `handled`，再调 `ui.layer.refresh()`，后端按脏键合并推送 `layers/*` 状态，网页重建列表。"独奏""不许藏最后一层"这类规则将来也在这里。
- 图层锁定：模型 `layerLocked` / `setLayerLocked` 与绑定 `layer_locked` / `set_layer_locked` 已有，只有 Transfer 读它；面板开关是规划（`../product_features.md`）。

## 图层的新建与单元

- 右键：New Line Layer（vector 列）、New Fill Layer（fill 列，独立）、New Auto-Mapping Layer（带 `tag="automapping"` 的组加常驻成员层，主画板才有）；单元的行为在 `../auto_mapping/layer_units.md`（一级）。
- 删除图层会回收它孤立的私有资产。

## 测试

- `tests/t_fill_children.py`（跟随填充层：拓扑跟踪、嵌套显示、重算）、`tests/t_layer_policy.py`（页切换、锁定、区域刷）、`tests/t_unit_bake.py`（单元烘焙成线稿层加填充层）、`tests/t_guide_capture_order.py`（先填色后画引导线填充保持完整，在别的层上画轴线不重算旁观层的填充，独立填充层上画轴线不刷填充）。
