# 名词表（glossary）

统一一级、二级、三级文档、界面文案和代码标识里的中英文名词。写文案、命名、起草文档前先查这里；新术语进这里。2026-09-28 起草，词条从一级草稿和现有算法文档里提出，用户点名加入的词条在说明里注明。

## 1. 使用规则

- **中文名**用于文档正文和界面文案；**英文名**用于英文界面词与架构说明；**项目标识**保持代码里的大小写、下划线和缩写，不为统一外观改名破坏接口。
- 状态：**现有** 仓库里已有对应代码或界面；**规划** 只在文档里承诺，代码未实现；**通用** 行业或平台基础术语；**归档** 已从软件移除，只在归档文档里出现。
- 界面以英文为主（菜单、按钮），文档照抄界面写法，不另译。
- 功能名一律写 **Auto Mapping**（界面写法），不写"映射"（用户 2026-09-28）。"映射"只作动词（把图案映射到主画板）或数学名词（映射的雅可比、映射器 mapper）。单元写 **Auto-Mapping 单元**（界面 Auto-Mapping Layer），区域写 **Mapping Area**，Mapping 页上的工具统称 **Mapping 页的工具**；纹理选择的下拉按用户命名写「映射纹理」。代码里的工具 id 和属性沿用历史写法 **`auto_mapping_2`**（Coons 版曾叫 Auto Mapping 2），不改。
- "纹理板"是一块板、一个容器；"纹理"或"纹理页"是它里面的一页。现有版本只有一页。

## 2. 产品与架构

| 中文名 | 英文名 / 项目标识 | 状态 | 说明 |
|---|---|---|---|
| 主画板 | Main board / Drawing / `main_paint_view`、视图名 `main` | 现有 | 唯一的线稿画板，中央区的 Drawing 页；Auto Mapping 的目标。 |
| 纹理板 | Texture board / Texture / `child_paint_view`、视图名 `child` | 现有 | 承载纹理的板，无限画布；可在中央 Texture 页、子控件框、停车位三个家之间搬迁。代码里沿用 child 之名。 |
| 纹理页 | Texture tab | 规划 | 纹理板分页栏里的一页，一页一个纹理、各自独立场景；在「映射纹理」下拉里选用哪一页。 |
| 映射纹理 | Mapping Texture（下拉） | 规划 | Auto Mapping 工具选项块里的下拉，点开列出纹理页；选中 Auto-Mapping 图层时选项块自动显示，在那里改该单元的目标纹理。 |
| 目标用户 | 动画人 | 现有 | 用户 2026-09-28 确认的发布对象。 |
| 基座 | Qt base / Qt framework | 现有 | C++ / Qt 这一层：渲染、窗口、数据模型、文件、嵌入 Python。不变。 |
| 基础函数 | generic mechanism / basic function | 现有 | C++ 提供给 Python 的通用能力：手势、覆盖层、模型读写、几何、声明式界面、通用开关。不含任何工具的含义。 |
| 机制 / 策略 | mechanism / policy | 现有 | 一对概念：C++ 的机制不知道工具是什么，Python 的策略决定它意味着什么。每个机制对应一个 Python 策略模块。 |
| 功能层 | Python layer / `pyfile/*.py` | 现有 | 放实验性算法和随功能扩展变化的逻辑；改它不重新编译。 |
| 嵌入 Python | embedded interpreter / `animean_python` | 现有 | 程序内的解释器与绑定模块；`pythonbind/animemodel.py` 是高层封装。 |
| 事件缝 / hook 事件 | hook event / `python_hooks.dispatch` | 现有 | C++ 向 Python 派发的事件族（绘制手势、前置请求、把手、界面、状态）。 |
| 把手事件 | handle event / `"handle"`（arm / pick / press / move / release / cancel / hover / view） | 现有 | 工具面：所有编辑类工具都长在它上面。 |
| 覆盖层 | overlay / `ui.set_overlay`、`overlay_stack` | 现有 | 画在所有图层之上的显示列表：引导线、把手、网格、视平线；可拖、可删（× 徽章）。 |
| scriptData | `scene.script_data()`、`script_store` | 现有 | 场景里 C++ 不解释的字符串，按顶层键分命名空间存脚本状态；随快照和工程文件走。 |
| 快照式历史 | snapshot history / `SceneHistory` | 现有 | 每步整份模型入历史，全局序号排序；两块板各一份。 |
| 共享轮子 | shared wheel / `beziersplit.h` ↔ `bezier.py`、`viewscale.h` ↔ `viewscale.py` | 现有 | 只有一个家的数学：贝塞尔劈分、屏幕 ↔ 画布换算；C++ 与 Python 各一份镜像。 |
| 屏幕像素 / 画布像素 | screen px / canvas (document) px | 现有 | 手和眼的量按屏幕像素、随缩放换算；作品的量按画布像素、不换算。 |
| 自包含文件夹 / 压缩包 | `dist\AnimeAn\` | 现有 | 部署形态：exe、Qt 运行库、`python312\`、脚本、wheel 在一起，离线可用；发布就是这个文件夹打成的压缩包，不做安装器。 |
| 导入 | Import ▸ Raster / OpenToonz Lines / Clip Studio Paint；`toonz_to_dict.py`、`clipreader` | 现有（承诺，待完整测试） | 别的软件的线稿导进主画板或纹理板。 |

## 3. 界面与工作区

| 中文名 | 英文名 / 项目标识 | 状态 | 说明 |
|---|---|---|---|
| 父窗 | ParentWindow / `ui.windows` 名 `tools`、`tool_options`、`layers`、`assets`、`history`、`repulsion_pad`、`python_debug` | 现有 | 带页签的停靠窗；Python 按名字寻址、选页。 |
| 页 | page / `painting`、`mapping`、`main`、`child`、`drawing`、`texture` | 现有 | 父窗或中央区里的一页。不要译成 Pagination。 |
| 中央区 | CentralPaintArea / `paint` | 现有 | Drawing 与 Texture 两页，不是停靠窗。 |
| 子控件框 | SubControlFrame / `texture_view` | 现有 | 可嵌进面板或浮动的框；纹理板的三个家之一。 |
| 纹理板的家 / 单一所有权 | texture home / `updateTextureHome` | 现有 | 纹理面板任一时刻只在一处：中央 Texture 页、子控件框、停车位。 |
| 无限画布 | unbounded canvas / `setUnboundedCanvas` | 现有 | 纹理板没有页面边框，图案画在哪都行。 |
| 活动板 | active view / `active_view` | 现有 | 撤销、面板、工具消息作用的那块板，边缘有蓝色描边。 |
| Changable Timeline / Changable Texture | 纹理板 Setting 菜单的两个开关 | 现有 | 前者决定时间轴是否跟随纹理板；后者关着时纹理板图案受保护，只有引导线工具能画。 |
| 选项面板 | Options / ToolOptPanel、`tool_controls/*.json` | 现有 | 声明式：按 JSON 布局渲染 slider / list / check / button / color / subwindow / settings。 |
| 设置窗 | settings window / `register_settings` | 现有 | 菜单项或右键项打开的声明式窗口（Draw Setting、Line Display Settings、Advanced Settings）。 |
| 脚本菜单 | script menu / `register_menu`（宿主 `main` / `child`） | 现有 | Python 注册的菜单：Draw、Arrow、Auto Mapping、两块板各自的 View。 |
| 主题 | AnimeTheme / Dark、Light | 现有 | 界面颜色角色的唯一出处；画纸恒白。 |
| Python Debug | 调试窗 | 现有 | 脚本输出加一行命令输入。 |

## 4. 绘画工具

| 中文名 | 英文名 / 项目标识 | 状态 | 说明 |
|---|---|---|---|
| 箭头 | Arrow / `Tool::Arrow`、`edit_tool.py` | 现有 | 选择与编辑；Edit Mode：Default / Artist / Debug。 |
| 画笔 | Pen / `Tool::Pen` | 现有 | C++ 内建的基础工具；采样、稳定器、拟合都在 C++。 |
| 稳定器 | Stabilizer / 1 Euro filter、`smooth` 滑条 | 现有 | 画的过程中的实时防抖；与拟合是两件事。 |
| 拟合 | fitting / `fitStrokePath`、Simplify、Corner | 现有 | 抬笔时把采样点变成线段加三次贝塞尔；Simplify 管节点数，Corner 管尖角。 |
| 轴向吸附 | axis snap / Alt、Shift | 现有 | 画笔按住修饰键锁水平或垂直。 |
| 按住画直线 | hold-still straight line | 现有 | 按下不动一小会儿，整笔塌成直线跟随光标。 |
| 橡皮 | Eraser / `Tool::Eraser`、`DeleteLine`、`CutLine`；Eraser Mode：AreaMode / LineMode / CutMode | 现有 | C++ 内建的基础工具；三种模式。 |
| 切线 | Cut Line / CutMode / `AnimeCutPlan` | 现有 | 删掉一条线在两侧相邻交点之间的那一段。 |
| 填充 | Fill / `Tool::Fill`、`fill_tool.py`；Fill Scope：ALL / Current | 现有 | 区域探测是 C++ 基础函数，落在哪层是 Python 策略。 |
| 跟随线稿的填充层 | tracked fill layer / `parentLayerId`、To Independent Layer | 现有 | 自动建在线稿层下、随线稿重算的填充层；解除跟随成独立填充层。 |
| 区域刷 | region brush / `ui.set_fill_paint_mode` | 现有 | 独立填充层上画笔的含义：画过的封闭区域都填上。 |
| 工具锁定 | locked tools / `ui.set_locked_tools`、`layer_tool_policy.py` | 现有 | 当前层决定哪些工具不可用；Arrow 永不锁。与"图层锁定"是两回事。 |
| 图层锁定 | layer lock / `set_layer_locked`、`layer_locked` | 现有（模型与绑定）/ 规划（面板开关） | 锁定的层不被工具改动；现在只有 Transfer 尊重它，Layers 面板加开关是规划（用户 2026-09-28）。 |
| 变形 | Transfer / `Tool::Transfer`、`transfer_tool.py` | 现有 | 当前层内容的缩放、拉伸、平移、旋转框；Python 工具。用户口中的"放大缩小"若指内容变形即此；视口缩放是 C++ 基础能力。 |
| 连接 | Connect / `Tool::Connect`、`connect_tool.py`；PolyMode / CurveMode / SmoothMode | 现有 | 两个点之间生成一条新笔画。 |
| 斥力板 | Repulsion Pad / `ForcePadPanel`、`repulsion_tool.py` | 现有 | 手柄把重叠的线推开；拓扑不变。 |
| 调色板 | palette / `PaletteControl`、`palette_box.py`、`tool_colors.py` | 现有 | 已存色块 + 色轮 + HSV + RGB；色块随文档保存，每个工具记自己的颜色。 |

## 5. Auto Mapping

| 中文名 | 英文名 / 项目标识 | 状态 | 说明 |
|---|---|---|---|
| Auto Mapping | `auto_mapping_2`（工具 id）、`pyfile/auto_mapping.py` | 现有 | 唯一的 Auto Mapping 算法：平移扫掠（Coons 退化形）的纯分级复合 Child→Third→Main。 |
| 中轴线 / 轴线 | Center line / `h_center_line`（蓝）、`v_center_line`（绿） | 现有 | 每块板一对 H、V，必须相交；定义坐标系和范围。不进图层。 |
| 参考架 | frame / `_Frame` | 现有 | 一对轴线张成的坐标架；`hv` 是它的正问题。 |
| 子系 / 主系 | child frame / main frame | 现有 | 纹理侧与主画板侧的参考架。 |
| Third 空间 | Third space / 子系弧坐标平面 $(\ell_h,\ell_v)$；`third_of`、`main_of_third` | 现有 | 两条轴线被拉直的中间空间；Auto Mapping 是 Child→Third→Main。 |
| 弧长参数化 | arc-length parametrisation / `_point_at_arc` | 现有 | 沿轴线按弧长取点，弯轴线让图案跟着弯。端点对端点、交点对交点。 |
| 阻尼牛顿 | damped Newton / `_Frame.solve` | 现有 | 从画布点反解子系坐标。 |
| 折叠 | fold / foldover、$\det J<0$ | 现有 | 轴线弯到两切向平行时一个纹理点对应主画板两处（一对多）；折叠的另一侧是背面。 |
| **不糊合** | no residual blending / 残差项已移除 | 现有 | **用户点名的核心态度**：Auto Mapping 一对多（折叠）的地方，不用残差项把正反面"糊"成连续的一张，而是裁断拓扑——正面、背面、折痕各成一层，图案看起来绕到了背面。旧公式的残差项 $p-\mathrm{HV}[C](\ell)$ 于 2026-08-24 彻底移除。 |
| 糊合 / 残差兜底 | residual patching / `p − HV[C](ℓ)` | 归档 | 不糊合的反面：旧 AM2 用残差项把反解失败的点强行连起来。只在归档与历史注记里出现。 |
| 拓扑裁断 | severing topology / `_sever_source`、`_sever_cubics_by_child_fold`、`_sever_cutters` | 现有 | 不糊合的做法：在折缝处切断笔画、丢掉不可达段；二维裁断伪造三维遮挡。 |
| 正面 / 背面 | front / back / `auto_mapped`、`auto_mapped_back`；front 层、back 层 | 现有 | 折叠两侧的输出层；背面染衬里色（Lining colour）。 |
| 折痕 / 折角线 | crease / seal / `auto_mapped_seal`、crease 层、`_emit_seals` | 现有 | 折叠边界的虚线标注；隐藏背面后正面图案的终结线。 |
| 最近端手柄 | nearest-point handle / 红色手柄、`_fold_depth` 的锚 | 现有 | 决定折叠层叠放次序：离它越近越在上。 |
| 补全拓扑 | Bezier Bridge / `bridge_topology`、Bridge k | 现有 | 用 Third 空间里的三次曲线跨过被裁断的缺口。 |
| Mapping Area | Mapping Area / `mapping_area`（浅蓝） | 现有 | 油漆桶式圈出的区域：主画板上裁结果，纹理板上筛来源。 |
| 附加线 | Additional Line / `additional_line`（粉）、`_FlowFieldWarp` | 现有 | 画下的测地线（额外的网格等参线），引导本族纹理的流向；流场加权泊松积分。跨过 H/V 轴线时跨越处不生效。 |
| 族 | family（H 族 / V 族） | 现有 | 附加线按走向归族；正交族互不影响。 |
| 约束外轮廓 | Constrain Outline / `constrain_outline` | 现有 | 图案外框钉在参考架上；画出边的那一侧释放。 |
| Auto-Mapping 单元 | mapping unit / Auto-Mapping Layer、组 tag `automapping`、`mapping_units` | 现有 | 带全套配置的图层组：两边轴线、Mapping Area、附加线、锚点、选项；输出随时可从配置重生成。 |
| 实时重跑 | Live Re-render / `auto_render` | 现有 | 单元有焦点时，改引导、改选项、改纹理图案都就地重跑。 |
| Advanced Settings | 单元设置窗 / `automapping_unit` | 现有 | 单元的显示开关、front / back / crease 可见性、Live Re-render、Horizon Line。 |
| 烘焙 | To Editable Layer / `_bake_unit_to_layers` | 现有 | 把单元变成一个线稿层加一个填充层。 |
| mapped layer | 逐次运行的输出层 | 现有 | 无单元的旧文档里，每按一次按钮新建一层置顶。 |
| Calculation Mode / 曲线模式 | Bezier / Spline / Polyline、`_CURVE_MODE` | 现有 | 输出几何的重建方式；三种都跟随形变。 |
| RDP | Ramer–Douglas–Peucker、`rdp_eps` | 通用 | 采样模式里对插入采样点的抽稀；原始顶点从不抽掉。 |
| 结构折点 | structural knot / `_structural_knots` | 现有 | Auto Mapping 失去仿射性的位置，由轴线折点解析枚举。 |
| 参考网格 | Refer Rect / Mapping Refer Rect、Refer Rect Divisions | 现有 | 两块板叠的橙色网格；纹理板显示参考架，主画板显示它的像。 |
| 遮挡区域 | Occluded Areas / `_OCCLUSION` | 现有 | 纹理板上染出会折到背面的区域。 |
| 视平线 | Horizon Line / `fold_horizon` | 现有 | 主画板上的橙色虚线，To 3D 用它定哪一端远。 |
| To 3D | `run_to_3d` | 现有 | 从 Auto Mapping 的雅可比反推浮雕，导出离线 HTML 查看器。 |
| Transfer Grid | To 3D 查看器按钮 | 现有 | 把参考网格披到重建曲面上。 |
| 脊柱旋转 / AM1 | spine rotation / `old_history/auto_mapping_1.py` | 归档 | 被淘汰的第一种算法，在曲率半径处结构性奇异。 |
| Midline | 原 `midline_tool.py`、Mapping 页按钮（已无代码） | 已删除（2026-09-28） | hook 系统的遗留示例，只打印日志；用户 2026-09-28 决定删除，同日已从代码中删除并合并进 main。"中线"这个名字可能留给轮廓模式里自动生成的轴线（待定）。 |
| 轮廓模式 | Outline Mode | 规划 | 用封闭轮廓代替手画 H/V 轴线：纹理页上框住纹理、主画板上画变形后的轮廓（任意封闭轮廓），框内轴线与其他约束线自动生成；多边形边的对应算法与交互待定（用户 2026-09-28）。 |
| Fukusato MLS | `old_history/fukusato/` | 归档 | 退役的论文复现工作流。 |

## 6. 图层、帧与文件

| 中文名 | 英文名 / 项目标识 | 状态 | 说明 |
|---|---|---|---|
| xsheet / 摄影表 | `AnimeSceneModel`：列、帧、单元格 | 现有 | 场景模型的骨架（来自 OpenToonz 的概念）。 |
| 图层 / 列 | layer / column / `AnimeColumn`（vector / fill / raster） | 现有 | 界面叫图层，模型叫列。 |
| 资产 | asset / `AnimeAsset` | 现有 | 单元格引用的绘图内容；Assets 面板。 |
| 单元格 | cell | 现有 | 某列某帧引用哪个资产。 |
| 图层组 / tag | layer group / `AnimeLayerNode.tag` | 现有 | 树形分组；tag 标出组的种类（`automapping`）。 |
| 线稿层 / 填充层 | Line Layer / Fill Layer | 现有 | Layers 右键新建的两种普通层。 |
| 保持帧 | Hold / `insertHoldFrameAfter` | 现有 | 沿用上一帧内容的帧。 |
| 逐帧调整的轴线 | per-frame axes | 规划 | Auto-Mapping 单元的主画板轴线可逐帧不同，让花纹跟着每一帧的线稿走；现有版本一个单元的轴线在所有帧上相同。 |
| 洋葱皮 | onion skin / lanes、Guide Line 开关 | 现有 | 前后帧的半透明影子；只在主画板。 |
| 工程文件 | `.anproj` | 现有 | 主画板、全部纹理、scriptData。 |
| 纹理文件 | `.textureview` | 现有 | 单独保存一页纹理。 |
| 历史窗 | History | 现有 | 当前板的操作记录，点任意一行跳转。 |

## 7. 开发与文档规则

| 中文名 | 英文名 / 项目标识 | 状态 | 说明 |
|---|---|---|---|
| 一级 / 二级 / 三级 | semantic tiers | 现有 | 目的、用户流程、架构、开发指南、Auto Mapping 文档为一级；产品功能为二级；其余机制为三级。见 `developer_guide.md`。 |
| 待核 | to verify | 现有 | 文档里写不准、等用户确认的标记。 |
| 〔裁〕 | trim mark | 现有 | 执行中穷尽解法表仍达不到时对二级、三级的最小修改标记。见 `long_time_work/README.md`。 |
| 解法表 | solution table | 现有 | 卡住时的候选、评分与扫描记录。见 `long_time_work/solution_table.md`。 |
| 同步脚本 | `sync_pyfiles.ps1` | 现有 | 改 Python 后把脚本同步到每个 exe 旁。 |
| 构建脚本 | `build_scripts\agent_build.ps1`、`deploy_AnimeAn` | 现有 | 改 C++ 后的完整构建与部署。 |
