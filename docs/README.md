# 文档（docs）

项目的设计、规则与规划文档。修改代码的人从 `developer_guide.md` 进入；代码目录自己的 README（`../opentoonz_tools/README.md`）只讲那个目录。

## 总纲

语义分三级，决定按级别顺序、上级优先；规则在 `developer_guide.md`「语义分级」。**级别以本索引为准，不以被谁引用为准。**

| 级 | 文件 | 说明 | 谁定 |
|---|---|---|---|
| 一级 | `product_purpose.md` | 产品目的：要解决什么、给谁用、承诺、不做什么、怎么判断做对了 | 用户 |
| 一级 | `user-workflow.md` | 用户工作流：从拿到程序到日常使用的主线 | 用户 |
| 一级 | `tools_user_workflow/<工具>.md` | 每个工具的用户操作：在哪、怎么用、会看到什么、边界（索引在它的 README） | 用户 |
| 一级 | `architecture.md` | 软件架构：Qt 基座不变、C++ 只写基础函数、功能在 Python、工具归属 | 用户 |
| 一级 | `developer_guide.md` 的「语义分级」「规则」 | 开发规则 | 用户 |
| 一级 | `auto_mapping/` | Auto Mapping 的算法规格、各条管线、映射单元，连阈值一起（索引在它的 README） | 用户 |
| 二级 | `product_features.md` | 产品功能：承诺什么，标现有 / 规划 / 待办 / 已删除 | 计划时用户拍板，执行时可裁 |
| 三级 | `mechanism/` | 其余机制：模块与启动、事件流、历史与 scriptData、窗口与主题、文件格式、构建部署、画笔与橡皮、编辑类工具、填充与图层、Python 绑定（索引在它的 README） | Agent 可改，告诉用户 |
| 三级 | `developer_guide.md` 的「先读什么」「验证」「提交与发布」 | 三张表 | Agent 可改，告诉用户 |
| 规则 | `long_time_work/` | 无人值守长时间工作：阶段权限、解法表、〔裁〕、任务记录 | — |
| 术语 | `glossary.md` | 名词表：中英文与代码标识对照，功能名一律 Auto Mapping | — |

## plan/ 施工档案

需求拆分、验收清单和施工计划。新功能开工前在这里加一份计划，做完回填。索引在 `plan/README.md`：现行为空；待开工的规划列在那里；已完成的会话产物有一份（文档重构的任务记录）。

## archive/ 已归档

被一级吸收的原始设计稿。目前为空，说明见 `archive/README.md`；代码层面的归档在 `../old_history/`。

## 检查

`py build_scripts\check_doc_links.py` 检查这里所有反引号引用的 `.md` 路径都存在。
