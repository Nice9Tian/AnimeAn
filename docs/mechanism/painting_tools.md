# 编辑类工具：Arrow、Transfer、Connect、Repulsion Pad 与颜色（painting_tools）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准；用户看到什么在 `../tools_user_workflow/` 的同名页。代码：`pyfile/edit_tool.py`、`pyfile/transfer_tool.py`、`pyfile/connect_tool.py`、`pyfile/repulsion_tool.py`、`pyfile/tool_colors.py`、`pyfile/palette_box.py`、`pyfile/overlay_stack.py`、`pyfile/viewscale.py`、`pyfile/bezier.py`；C++ 侧 `openglwidget.cpp`（把手渲染与命中、handle 事件、拖拽认领）、`childrenpanel/forcepad.*`、`childrenpanel/palettecontrol.*`、`algorithm/animemodel.cpp`（`AnimeVectorImageModel::transform`、`replace_stroke_with_pieces`）。这些工具全是 Python：C++ 只画把手、报手势、施加变换。

## 共用的机制

- **handle 事件是工具面。** 阶段 `arm`（每次换工具或换笔画属性）、`pick`、`press`、`move`、`release`、`cancel`、`hover`、`view`（显示把手时缩放变了）；消息带 tool / base_tool / property、文档坐标、缩放、修饰键（shift / ctrl / alt / constrain）和当前颜色。`pick` 回 `message["grab"] = id` 即认领这次按下为拖拽。把手按**屏幕**尺寸画、按屏幕命中；可拖的覆盖层项在任何工具下都能拖。
- **覆盖层**经 `overlay_stack.set_items(view, owner, items)` 合成；光标经 `ui.set_cursor(view, name)`，C++ 每次 `setTool` 清掉脚本光标，Python 换会话时要丢自己的光标记忆。
- **每工具颜色**（`tool_colors`）：键是脚本工具的笔画属性、否则是基础工具名；`arm` 时记下当前颜色，工具从不自己"恢复默认色"（曾让第二条 H 轴画成画笔色）。
- **调色板**（`palette_box`）：色块集存主场景 scriptData 键 `palette`，随文档；`color` hook 直接套用颜色，`palette_box` hook 走 `add:#AARRGGBB` / `remove:…`；Pen 和 Fill 面板各按自己的槽取种子。

## Arrow（`edit_tool.py`）

- 三种模式（Options 的 Edit Mode）：**Default** 点选勾轮廓、拖动平移，先命中笔画再命中填充；**Artist** 在笔画展平上找曲率显著的支配点，再按心理物理间距过滤（`CSF_PEAK_CPD` 定义的角周期换算成屏幕像素，经缩放映射到文档空间，放大出现更多把手），拖一个伪把手时局部变形、衰减到相邻关键点；**Debug** 显示存储的每个锚点与控制柄，切向臂画成覆盖线，拖哪个改哪个。
- 模式切换保留选中对象，只重建把手；填充区域在 Artist / Debug 下每个边界顶点一个把手。
- 选中轮廓每种模式都画；颜色线宽在 Arrow 菜单的 Outline Display Settings。
- 撤销 / 重做后清会话（索引不可信）。宿主在 Arrow 上的脚本工具（属性非空，如 Auto Mapping 按钮）`pick` 不认领任何东西，只拖引导。

## Transfer（`transfer_tool.py`）

- 框是**带符号**的轴对齐矩形加一个绕固定原点的角度，不存四边形；把手和轮廓是矩形顶点经旋转得到，缩放拖动先把光标旋回框的自身坐标再按带符号范围算。拖动期间不归一（归一会丢镜像），只在显示时归一。
- 角把手：对角为锚缩放，修饰键锁比例，锁法是 min 规则（长边不超过鼠标拉出的框）；边把手：单轴拉伸，修饰键改为关于中心对称；框内：平移，修饰键锁到先动的轴（与画笔直线吸附同一规则）；角外旋转环 `ROTATE_RING_PX` = 36 屏幕像素，自由旋转，修饰键才吸附 `ROTATE_SNAP_DEG` = 15°。Options 的 Alt / Ctrl / Shift 选项可关掉修饰键约束。
- 变换**增量**施加（每次 move 发 T·T_prev⁻¹），松手提交并按新包围盒重新套框、角度归零；恒等增量直接返回，否则一次点击也会提交历史。移动与松手信任会话记录的拖拽对象，不信任回显的把手 id（旋转"锁死 90°"的教训：环被角把手的命中框遮住）。
- 栅格单元格拒绝旋转（模型只存左上角加图像）：环不响应、光标不提示。Python 检查每层锁定；C++ 只管板级锁。
- C++ 侧 `AnimeVectorImageModel::transform` 同时变换笔画、填充、栅格放置并重采样栅格；笔画包围盒按线宽外扩（是擦除 / 切断的剔除框），线宽不设下限（增量矩阵链要可逆），`lengths` / `totalLength` 跟着几何缩放。

## Connect（`connect_tool.py`）

- 吸附：`BRUSH_RADIUS` = 12 画布像素（镜像 C++ 的橡皮环）；半径三分之一内优先端点与拐角、其次曲率顶点；半径内再取最近顶点。拐角是单顶点的真实折角，顶点是宽窗累积的平滑凸起，一把尺子量两者会把每个尖顶都当拐角。Auto Snap 关掉取光标精确位置。
- 连接：三种模式都是 `quad(A, P, B)` 经 `bezier.py` 升阶。PolyMode 直线；CurveMode 的 P 是两端切向射线的未来交点（切向向回追溯几个顶点求得）；SmoothMode 用 Smooth 把 P 向弦拉，0 就是 CurveMode，100 就是直线。射线背离或平行退回弦；近平行交点钳到 `REACH_LIMIT` = 2 倍弦长。
- 生命周期：新笔画落在当前层并保持聚焦，改选项就地重算；画布上的接受 / 删除徽章经 `overlayaction`；换工具静默接受；Auto Accept 开着无徽章，画下一条即移焦点；撤销 / 重做丢弃焦点。

## Repulsion Pad（`repulsion_tool.py`）

- 机制：`ForcePadPanel`（十字坐标加单位圆手柄，松手锁住不回弹，`pad` 事件带 press / move / release，移动发射节流）；预览走临时**内部层**（`AnimeColumn.internal`，不进面板、不进存档、渲染在最上层）：按下把可编辑笔画复制过去、隐藏源层、`ui.displace_begin` 一次上传基线点与每顶点位移，之后每次移动只发 `ui.displace_scale(x, y)` 一个数值调用。
- 策略：基线在首次需要时按活动板当前帧缓存——每条可见笔画的原始几何（真实路径命令加原始点）加一份展平，每个顶点的斥力向量由所有空间上相近的顶点求和（其他笔画和自身折返都算，只排除同一折线上弧距小于 `ARC_EXCLUDE` = 1.5 × `RADIUS` 的邻点），`RADIUS` = 30 画布像素、(1 − d/R)² 衰减、沿笔画平滑、全局归一到最大为 1。精确重合的顶点用与画线方向无关的规范法线加半平面连续的符号，让两侧反向。
- 施加：位移 = 手柄 × `MAX_PUSH`（60 画布像素）× 力，永远相对基线；拓扑保持——命令点与原始点 1:1 位移，不重采样不拆分，`replace_stroke_with_pieces` 就地换入（保留 z、id、属性、颜色、线宽），每次松手一条 "Repulsion" 记录；只在真的插入了才提交。
- 安全：缓存按图像身份（资产、帧号）而非图层键；施加时按资产图像寻址并核对按下时的指纹；只点不拖不施加；`historyrestore` 与内容变化事件（画线、擦除、删除、填充、移动）作废基线并让手柄回中心；锁定层只产生斥力不被改。

## 测试

- `tests/t_transfer.py`、`tests/t_edit_modes.py`、`tests/t_toolcontrol.py`、`tests/t_palette.py`。Connect 与 Repulsion 的离线套件当时留在会话临时目录，仓库里没有。
