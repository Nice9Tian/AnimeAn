# 编辑类工具：Arrow、Transfer、Connect、Curve、Round Corner、Repulsion Pad 与颜色（painting_tools）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准；用户看到什么在 `../tools_user_workflow/` 的同名页。代码：`pyfile/edit_tool.py`、`pyfile/transfer_tool.py`、`pyfile/connect_tool.py`、`pyfile/curve_tool.py`、`pyfile/round_corner_tool.py`、`pyfile/repulsion_tool.py`、`pyfile/tool_colors.py`、`pyfile/palette_box.py`、`pyfile/overlay_stack.py`、`pyfile/viewscale.py`、`pyfile/bezier.py`；C++ 侧 `openglwidget.cpp`（控制手柄渲染与命中、handle 事件、拖拽认领）、`algorithm/animemodel.cpp`（`AnimeVectorImageModel::transform`、`replace_stroke_with_pieces`）；网页侧 `webui/src/ui/panels2.js`（`createForcePad`）、`webui/src/ui/palette.js`（`createPalette`）。这些工具全是 Python：C++ 只画控制手柄、报交互事件、施加变换。

## 共用的机制

- **handle 事件是工具面。** 阶段 `arm`（每次换工具或换笔画属性）、`pick`、`press`、`move`、`release`、`cancel`、`hover`、`view`（显示控制手柄时、或脚本工具武装时缩放变了）、`context`（脚本工具武装时右键按下）、`leave`（脚本工具武装时光标离开画板）；消息带 tool / base_tool / property、文档坐标、缩放、修饰键（shift / ctrl / alt / constrain）和当前颜色。`pick` 回 `message["grab"] = id` 即认领这次按下为拖拽。控制手柄按**屏幕**尺寸画、按屏幕命中；可拖的覆盖层项在任何工具下都能拖。
- **覆盖层**经 `overlay_stack.set_items(view, owner, items)` 合成；光标经 `ui.set_cursor(view, name)`，C++ 每次 `setTool` 清掉脚本光标，Python 换会话时要丢自己的光标记忆。
- **每工具颜色**（`tool_colors`）：键是脚本工具的笔画属性、否则是基础工具名；`arm` 时记下当前颜色，工具从不自己"恢复默认色"（曾让第二条 H 轴画成画笔色）。
- **调色板**（`palette_box`）：色块集存主场景 scriptData 键 `palette`，随文档；`color` hook 直接套用颜色，`palette_box` hook 走 `add:#AARRGGBB` / `remove:…`；Pen 和 Fill 面板各按自己的槽取种子。

## Arrow（`edit_tool.py`）

- 三种模式（Options 的 Edit Mode）：**Default** 点选勾轮廓、拖动平移，先命中笔画再命中填充；**Artist** 在笔画展平上找曲率显著的支配点，再按心理物理间距过滤（`CSF_PEAK_CPD` 定义的角周期换算成屏幕像素，经缩放映射到文档空间，放大出现更多控制手柄），拖一个伪控制手柄时局部变形、衰减到相邻关键点；**Debug** 显示存储的每个锚点与控制柄，切向臂画成覆盖线，拖哪个改哪个。
- 模式切换保留选中对象，只重建控制手柄；填充区域在 Artist / Debug 下每个边界顶点一个控制手柄。
- 选中轮廓每种模式都画；颜色线宽在 Arrow 菜单的 Outline Display Settings。
- 撤销 / 重做后清会话（索引不可信）。宿主在 Arrow 上的脚本工具（属性非空，如 Auto Mapping 按钮）`pick` 不认领任何东西，只拖引导。

## Transfer（`transfer_tool.py`）

- 框是**带符号**的轴对齐矩形加一个绕固定原点的角度，不存四边形；控制手柄和轮廓是矩形顶点经旋转得到，缩放拖动先把光标旋回框的自身坐标再按带符号范围算。拖动期间不归一（归一会丢镜像），只在显示时归一。
- 角控制手柄：对角为锚缩放，修饰键锁比例，锁法是 min 规则（长边不超过鼠标拉出的框）；边控制手柄：单轴拉伸，修饰键改为关于中心对称；框内：平移，修饰键锁到先动的轴（与画笔直线吸附同一规则）；角外旋转环 `ROTATE_RING_PX` = 36 屏幕像素，自由旋转，修饰键才吸附 `ROTATE_SNAP_DEG` = 15°。Options 的 Alt / Ctrl / Shift 选项可关掉修饰键约束。
- 变换**增量**施加（每次 move 发 T·T_prev⁻¹），松手提交并按新包围盒重新套框、角度归零；恒等增量直接返回，否则一次点击也会提交历史。移动与松手信任会话记录的拖拽对象，不信任回显的控制手柄 id（旋转"锁死 90°"的教训：环被角控制手柄的命中框遮住）。
- 栅格单元格拒绝旋转（模型只存左上角加图像）：环不响应、光标不提示。Python 检查每层锁定；C++ 只管板级锁。
- C++ 侧 `AnimeVectorImageModel::transform` 同时变换笔画、填充、栅格放置并重采样栅格；笔画包围盒按线宽外扩（是擦除 / 切断的剔除框），线宽不设下限（增量矩阵链要可逆），`lengths` / `totalLength` 跟着几何缩放。

## Connect（`connect_tool.py`）

- 吸附：`BRUSH_RADIUS` = 12 画布像素（镜像 C++ 的橡皮环）；半径三分之一内优先端点与拐角、其次曲率顶点；半径内再取最近顶点。拐角是单顶点的真实折角，顶点是宽窗累积的平滑凸起，一把尺子量两者会把每个尖顶都当拐角。Auto Snap 关掉取光标精确位置。
- 连接：三种模式都是 `quad(A, P, B)` 经 `bezier.py` 升阶。PolyMode 直线；CurveMode 的 P 是两端切向射线的未来交点（切向向回追溯几个顶点求得）；SmoothMode 用 Smooth 把 P 向弦拉，0 就是 CurveMode，100 就是直线。射线背离或平行退回弦；近平行交点钳到 `REACH_LIMIT` = 2 倍弦长。
- 生命周期：新笔画落在当前层并保持聚焦，改选项就地重算；画布上的接受 / 删除徽章经 `overlayaction`；换工具静默接受；Auto Accept 开着无徽章，画下一条即移焦点；撤销 / 重做丢弃焦点。

## Curve（`curve_tool.py`）

- 注册：`extra_tools.py` 里 `curve`，属性 `curve_tool`，宿主是 Arrow（Painting 页）。左键是 `pick`，右键是 `context`，光标是 `hover` / `leave`（见 `hook_events.md`）。
- 平滑：向心 Catmull-Rom（α = 0.5），每段一个三次 Bezier；开放端取进入相邻切向的抛物线。新锚点只改它前面一段。Hobby 样条归档在 `../../old_history/hobby_spline.js`。
- 笔画：两个锚点起就是当前层里的一条真实笔画，之后原地重写（保留绘制顺序），每个锚点 `history_commit("Curve")` 一次；颜色取 `tool_colors` 里 Pen 的颜色，粗细是 Pen 的 Width（`pen_width` 选项事件和建选项面板时传入的 state）。选项面板不放颜色行：选颜色会让 C++ 武装 Pen。
- 锚点记在场景 scriptData 键 `curve_tool`：每条线一条记录（uid、所在格的 `(asset_index, frame_id)`、锚点、是否闭合）；记录按几何认笔画——路径的每个控制点都等于这些锚点算出的曲线的那一条，不认下标。笔画被擦、切、改过（哪怕只动了一个手柄）就对不上，悬停时跳过，下次在同一格写线时删掉。悬停不写文档。续画时保留这条线原来的颜色和粗细；每次写线前确认图层仍可画、还是开始画时那一层（`layer_id_at`），不是就结束这条线。
- 撤销：`historyrestore` 后正在画的线按恢复后的记录接着画，方向和撤销前一致（Ctrl+Z 收回最后一个锚点）；撤到线还不是笔画时留下第一个锚点；笔画没了就结束。换帧结束这条线。写线后让跟随这一层的填充层重描（`fill_tool.retrace_after_edit`），并进同一条历史。
- 吸附：`SNAP_PX` = 16 屏幕像素，吸住后放宽到 `STICK` = 1.35 倍；离上一个锚点 `MIN_GAP_PX` = 3 屏幕像素以内的点击不放点；Alt 或 Options 的 Snap 关掉不吸。接线只在同一格里。
- 显示：全部经 `overlay_stack`（owner `curve_tool`），项都 `removable = False`；预览只画下一次点击会改的一段和新的一段（接线时是接上后的整段，闭合时是闭合段），离上一个锚点 `MIN_GAP_PX` 以内不画；缩放（`view`）、换帧、撤销后重画。只剩一个还没成笔画的锚点时撤销，这个锚点也去掉。历史名 `Curve`、`Join Curves`、`Close Curve`。

## Round Corner（`round_corner_tool.py`）

- 注册：`extra_tools.py` 里 `round_corner`，属性 `round_corner_tool`，宿主是 Arrow。`pick` 吸到抓点时回 `grab = "round:drag"` 认领拖动，`move` / `release` 带这个 id；拖动中 `context` 取消。
- 尖角：单子路径的笔画（直线、二次段升成三次）里相邻两段在接点的切向差超过 `CORNER_DEG` = 15°；带属性的笔画（别的工具的线）不碰。
- 圆角：接点 j 记两段弧长 `L1`、`L2`、控制线抓的是哪一端 `s`、拖动时的形状 `k`；两条腿用 `bezier.cubic_segment` 精确切片（共享轮子，不重拟合），切点之间补一段三次，手柄长 k × 切掉长度。每边最多切到这一段另一端（含那一端已切掉的）前 `GAP` = 0.5 画布像素。作用于当前帧所有可见、未锁、非填充层上不带属性的笔画，不只当前层。缩放（`view`）、撤销、换帧换层后重画或清掉标记。
- 控制线：两个切点的弦，钉在角尖，两端是 角尖 ± 弦 / 2；抓着的一端等于鼠标。直腿闭式解 `端点 − 角尖 = s (L1 e1 + L2 e2) / 2`（e1 = −u1，e2 = u2），弯腿用 Levenberg-Marquardt 在真实的弦上解，拖动时从上一刻接着解；够不到的位置停在最近处。Shift 两边等长。
- 吸附点：悬停吸附 `SNAP_PX` = 16 屏幕像素，吸住后放宽到 `STICK` = 1.35 倍；按下后移动不到 `DRAG_START_PX` = 2 屏幕像素什么都不改。拖动中角尖和起点各是一个停靠点，半径 `SNAP_PX`；按下时就在范围里的停靠点要先离开一次才生效。松手时这个角的圆角和按下前一样（吸到起点、新角吸回角尖、或拉得太小两边都不到 `GAP`），笔画按原命令放回、不提交历史。抓着的端点保持按下时离它的偏移；Shift、转出范围、腿切到头时端点停在够得到的最近处。
- 写回：每次移动 `replace_stroke_with_pieces` 原地换（保留 id、颜色、线宽、线型），松手先让跟随这一层的填充层重描，再 `history_commit("Round Corner")` 一次；scriptData 键 `round_corner` 记每条倒过圆角的笔画：所在格、未倒圆角的段、各接点的圆角、结果路径的全部控制点（用来认笔画）。认不出的笔画（包括被别的工具动过一个手柄的）就按它现在的尖角处理。松手没到达就开始下一次按下时，上一次拖动按松手收尾。
- 显示：经 `overlay_stack`（owner `round_corner_tool`），只标光标 `NEAR_PX` = 120 屏幕像素以内的角。

## Repulsion Pad（`repulsion_tool.py`）

- 机制：网页斥力板控件 `createForcePad`（`webui/src/ui/panels2.js`，十字坐标加单位圆手柄，松手锁住不回弹，经 `pad.event` 发 press / move / release，移动发射节流）；预览走临时**内部层**（`AnimeColumn.internal`，不进面板、不进存档、渲染在最上层）：按下把可编辑笔画复制过去、隐藏源层、`ui.displace_begin` 一次上传基线点与每顶点位移，之后每次移动只发 `ui.displace_scale(x, y)` 一个数值调用。
- 策略：基线在首次需要时按活动板当前帧缓存——每条可见笔画的原始几何（真实路径命令加原始点）加一份展平，每个顶点的斥力向量由所有空间上相近的顶点求和（其他笔画和自身折返都算，只排除同一折线上弧距小于 `ARC_EXCLUDE` = 1.5 × `RADIUS` 的邻点），`RADIUS` = 30 画布像素、(1 − d/R)² 衰减、沿笔画平滑、全局归一到最大为 1。精确重合的顶点用与画线方向无关的规范法线加半平面连续的符号，让两侧反向。
- 施加：位移 = 手柄 × `MAX_PUSH`（60 画布像素）× 力，永远相对基线；拓扑保持——命令点与原始点 1:1 位移，不重采样不拆分，`replace_stroke_with_pieces` 就地换入（保留 z、id、属性、颜色、线宽），每次松手一条 "Repulsion" 记录；只在真的插入了才提交。
- 安全：缓存按图像身份（资产、帧号）而非图层键；施加时按资产图像寻址并核对按下时的指纹；只点不拖不施加；`historyrestore` 与内容变化事件（画线、擦除、删除、填充、移动）作废基线并让手柄回中心；锁定层只产生斥力不被改。

## 测试

- `tests/t_transfer.py`、`tests/t_edit_modes.py`、`tests/t_toolcontrol.py`、`tests/t_palette.py`、`tests/t_curve_round_tools.py`（Curve 与 Round Corner，在一个照绑定格式回报笔画的假场景上走同样的 handle 消息）。Connect 与 Repulsion 的离线套件当时留在会话临时目录，仓库里没有。
