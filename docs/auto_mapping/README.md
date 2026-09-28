# Auto Mapping 文档（auto_mapping）

一级语义：本软件核心功能的算法规格、各条管线和映射单元。这里的任何内容，**包括阈值和常量**，都由用户定；Agent 只能列「修改前 / 修改后」提议，拍板后才改（见 `../developer_guide.md`「语义分级」）。用户看到什么在 `../tools_user_workflow/`（一级），承诺在 `../product_features.md`（二级），其余机制在 `../mechanism/`（三级）。

| 文件 | 内容 | 适用版本 |
|---|---|---|
| `auto_mapping_2_spec.md` | 完整规格：记号、五阶段管线、映射的数学性质与定理、曲线重建、区域裁剪、守卫与诊断、数值验证、附加线流场（§11.5）、To 3D 与视平线（§11.6）、已知局限、常量表、代码索引、复现装置 | 2026-09-15；§11.6 措辞、§12.5、§12.7 于 2026-09-28 经用户授权更正 |
| `point_mapping_newton.md` | 单个点怎么算出来：参考架、正问题 `hv`、阻尼牛顿反解、逐侧标定、恒等性、实测表、裁断链 | 2026-08 解耦重构后，2026-08-25 补注 |
| `stroke_mapping_pipeline.md` | 笔画这一层：三级离散、自适应采样、四种切断、三种拟合模式、精确裁剪、误差常量 | 2026-08-15；§0 于 2026-09-28 按现行拟合器改写 |
| `fold_crease_pipeline.md` | 折痕：折缝轨迹的数学、切断点、双向扫描、合并与缝合、锚定发射、不变量 | 2026-08-18；§11 补入 2026-08-19 的角点注入与双空间深度计数 |
| `topology_severing.md` | 拓扑裁断：无残差的 Child→Third→Main、两半判定、笔画与填充的裁断、贝塞尔桥、闸门 | 2026-08-24 重构，2026-08-25 审查修复 |
| `flow_field_warp.md` | 附加线：族测地线关键帧模型、帐篷、加权泊松积分、约束外轮廓 | 2026-08-25 |
| `layer_units.md` | 属性图层与 Auto-Mapping 单元：焦点语义、实时重跑、Advanced Settings、C++ 通用机制 | 2026-08-25 |

不糊合、拓扑裁断、Third 空间、族、约束外轮廓等名词见 `../glossary.md`。

归档（不再据此施工）：`../../old_history/auto_mapping_algorithms.md`（AM1 与 AM2 的比较，AM1 已移除）、`../../old_history/fukusato/`（Fukusato MLS 工作流，已退役）。

回归套件（`tests/`）：`t_sever.py`、`t_bridge.py`（裁断与桥）、`t_flowfield.py`、`t_additional.py`、`t_compose.py`、`t_twofix.py`、`t_third_invalidate.py`、`t_removal.py`（附加线）、`t_units.py`、`t_unit_bake.py`、`t_layer_policy.py`（单元）、`t_fill_children.py`（填充）、`t_onion_guides.py`（洋葱皮引导）、`t_warp3d.py`、`t_horizon3d.py`（To 3D）、`t_bezier_knots.py`（bezier 模式折点劈分）。改 `pyfile/auto_mapping.py` 必跑：`py -m unittest discover -s tests`。
