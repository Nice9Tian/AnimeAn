# 画笔与橡皮：采样、稳定器、拟合、擦除（stroke_fitting）

三级机制。从属于 `../architecture.md`，与它冲突时以它为准；用户看到什么在 `../tools_user_workflow/pen.md`、`../tools_user_workflow/eraser.md`。代码：`openglwidget.cpp`（手势采集、轴向吸附、按住画直线、橡皮路由）、`algorithm/vectorlogic.cpp`（1 Euro 稳定器、`liveFitStrokePath` / `fitStrokePath`、擦除区间、切断计划、`makeStroke`）、`algorithm/viewscale.h`（屏幕 ↔ 画布换算）、`pyfile/draw_settings.py`（三参数与 Draw 菜单）、`pyfile/toolcontrol.py`（Pen 选项面板）。Pen 与 Eraser 是 C++ 内建的基础工具；它们的选项面板和颜色仍由 Python 给出。

## 一笔的流程

1. **按下**：坐标经 `viewscale` 转成文档坐标；捕获的最小点距按屏幕像素算（除以钳制后的缩放）。
2. **稳定器**：1 Euro 滤波在**屏幕坐标**上跑（手的速度是屏幕速度；在文档空间滤波曾在中途平移时弄坏长笔画），带相位滞后补偿。Stabilizer 滑条在 Pen 面板和 Draw Setting 窗是同一个值。
3. **实时增量拟合**（`liveFitStrokePath`）：笔尖后方一段距离之外的墨水一次性"烘焙"、逐位冻结；直线模式在无界并集测试下延伸一条开放弦，曲线模式按块烘焙强制三次段并共享接缝切向；尾段以角点钳制、位移封顶的去噪折线预览。设计原则是"生成一次以后切断远端影响，只有近端在实时计算"。预览显示的就是抬笔会提交的形状；每块烘焙后整段重拟合，只有显示的墨水移动不超过 0.35 屏幕像素才替换（压节点数）。
4. **抬笔**（`fitStrokePath`）：高斯去噪 → 直线段检测 → Schneider 三次贝塞尔拟合 → 尖角处切向断开；产出线段加三次段的混合路径。消费方遇到直线段要经共享轮子升阶，不能拒绝。
5. `makeStroke` 同时保存点列（稠密展平，供命中、擦除、子笔画使用）和路径；随后派发 `linefinish`，hook 处理完再提交历史，脚本的改动并入同一条记录。

## 参数与预算

| 参数 | 来源 | 现值 | 说明 |
|---|---|---|---|
| Stabilizer / Simplify / Corner | Draw Setting 三滑条，0–100，默认 50 / 50 / 50 | — | 50/50/50 复现拆分前的行为 |
| 高斯 σ、直线容差、拟合容差 | Simplify 线性映射 | 基准 1.2 px，随滑条上升 | **噪声预算**（σ、直线容差、步长与角点窗）按手抖标定，是屏幕像素常量，不随笔画长度缩放 |
| 拟合容差 | Simplify | 基准 1.2 px | **形状预算**：随整笔弧长在 60 px 以下按比例缩小，下限 0.8 px；也封顶高斯平滑的位移 |
| 尖角角度 | Corner 线性映射 | 55° − 35°·c，默认 37.5°，滑条最小 20° | 拟合器把尖角作为有意的切向断开发出；曲线段接缝处的一侧切向差就是"艺术家的意图"信号 |
| `pixelScale` | 按下时的 1 / 缩放 | — | 把上述屏幕像素预算换成文档像素；默认 1.0 与旧行为逐位一致 |
| 轴向吸附阈值 | `m_axisSnapThreshold` | 5 屏幕像素 | 位移超过阈值才锁轴，锁前抖动被回溯拉直；松键立即解锁；抬笔点沿用最后一次移动的锁定（不重新读修饰键） |
| 按住画直线 | `kHoldStillRadiusScreenPx` / `kHoldStillMs` | 10 屏幕像素内停 1.5 秒 | 圆心记在画布空间；触发后整笔塌成直线跟随光标 |
| 视口缩放范围 | `kMinZoom` / `kMaxZoom` | 0.1–8 | 以光标为锚 |
| 历史条数 | `SceneHistory::m_maxEntries` | 100 | 每块板一份 |

预算的两条铁律：噪声预算永远不缩到手抖下限之下；任何锚定拟合的平滑，位移都要被拟合容差封顶。

## 橡皮

- **AreaMode**：笔刷半径 `m_eraserRadius` 是画布像素（Connect 的 `BRUSH_RADIUS` 与之镜像，现为 12），沿路径擦掉区间，一条线可断成多段；`subStroke` 从拟合路径里精确切片（de Casteljau），不重拟合，切片弧长必须在测量它的那条笔画上消费。
- **LineMode**（Delete Line）：点中整条删除。
- **CutMode**（Cut Line）：`AnimeCutPlan` 找到点击处两侧最近的交点（含与自身的交点），删掉中间的区间；交点两边的线各自保留端点。
- 三种模式各自提交历史（Erase / Delete Line / Cut Line），并派发 `erasefinish` / `deletefinish`，跟随线稿的填充层据此重算。
- 独立填充层上（`ui.set_fill_paint_mode` 开着）橡皮改为擦填充区域，Pen 的笔迹转成区域填充后移除。

## 已知的空间混淆（未修）

- `FIT_TOL_PX` 是画布像素却叫 px；`min_gap = 9.45/zoom` 在缩放超过 2.36 后被固定的 4.0 画布 `POLY_STEP` 封顶；覆盖层徽章 `kOverlayHandleSize` 按画布尺寸，高倍缩放下命中区膨胀。

## 测试

- C++：`tests/strokefit_tests.cpp`（`strokefit_zoom_invariance`，拟合对缩放不变）。
- Python：`tests/t_toolcontrol.py`（选项布局）。橡皮与切断没有独立套件。
