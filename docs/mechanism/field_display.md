# 场显示（field_display）

三级机制：让一批图层隔着一张位移场显示，不改模型。从属于 `../architecture.md`。协议细节在 `backend_protocol.md`，绑定在 `python_binding.md`。Auto Mapping 用它做拖线预览，语义在 `../auto_mapping/layer_units.md`（一级）。

这条链路是通用的：C++ 和网页不认识 Auto Mapping，只认场、作用域和标记。

## 三层

| 层 | 做什么 | 代码 |
|---|---|---|
| Python | 算场，决定显示哪些列、分几遍、什么时候清除 | `pyfile/mapping_preview.py` |
| 后端 | 存最新的场，发板帧前发出去；把源画板的列画进 `F`、`f` 作用域；只影响显示的隐藏列 | `openglwidget.cpp` 的 `paintFieldScope`、`backend/session/backendsession.cpp` |
| 网页 | 场存成 RGBA32F 纹理，顶点按场位移，片元按标记丢弃和换色 | `webui/src/board/glrenderer.ts`、`tess.ts` |

## 背压

场不单独发，只在发板帧之前发。等 ack 期间不发，新值覆盖没发出去的旧值，所以网页慢时不堆积。`F` 指令带版本号，场一变，增量绘制的 `keep` 自然停在 `F` 之前，这段之后的指令重画，稳定前缀不动。

## 几何

- 顶点位移靠顶点，所以作用域里的长边要细分到不超过 `maxEdge`。
- 填充不走模板。模板扇形的大三角内部，源坐标是线性插值的，而映射不是线性的，标记取值不准；折叠时绕数也不再等于填充。所以作用域里的填充按 `maxEdge` 切成水平带上的小三角直接画。
- 片元用未变形的源坐标取最近节点的标记，所以切断边缘的精度是一个网格单元。
- 线宽在源空间展开，位移本身已经按映射的局部比例缩放了轮廓。所以 `width_scale` 不能直接用松手时的值，要除以按下时笔画范围内的典型比例，也就是 `sqrt|det J|` 的中位数。这样局部比例和典型值相同的地方，粗细就和松手一致；拉伸不均匀的地方，预览按局部比例变粗变细，松手后统一成一个宽度。

## 退路

`board.capabilities` 上报能力，WebGL2 为真，Canvas 2D 为假，没有页面连着时为假。不支持时 Python 不开预览，行为和松手重跑完全一样。Canvas 2D 收到作用域整段跳过。

## 求解在 Python

issue #46 第 1 步实测：一次 CG 迭代 numpy 约 60µs。放 GPU 要读回内积，解出来还要和 `hv` 组合，这是 Auto Mapping 的含义；放 C++ 只快 2 到 3 倍。所以求解和取值都在 Python，批量化；GPU 只负责显示。

`pyfile/gridsolve.py` 是通用的共轭梯度、雅可比行列式和连通区域标记，不认识 Auto Mapping。

## 帧预算

手快的时候每帧线走好几像素，热启动追不上，CG 用满上限，帧时间能到 190ms。所以一帧的求解有预算：

- 一次求解最多 `WARM_MAX_ITER` 步。超了就标 `warm["unsettled"]`，这帧照样显示。
- 手停下来 `SETTLE_DELAY_MS` 之后，`mapping_preview` 用 `call_later` 接着解，步数上限换成 `WARM_SETTLE_ITER`，到收敛或 `SETTLE_PASSES` 次为止。新的移动会让这次作废，所以不和手抢时间。小上限不能用来收敛：重启的 CG 丢了共轭性，永远到不了停止判据。
- 每次停手的第一遍把拖动中的近似都还回来：最近线段全量搜索（拖动中从上一帧走邻居，弯曲的线上可能停在局部极小），层序表精确重算（继承看不出折痕扫过锚点）。
- 初值在两个方向上做能量投影：本帧上一轮的变化量，和上一帧这一轮的变化量。boost 平稳时前者好，boost 跳变时后者好，能量最小解不比任一个差。
- 停止阈值 `WARM_STEP_PX` 不能再放宽：0.006px 会让折叠的帧走到退让判定的另一边，映射偏 25px，这是悬崖不是斜坡。0.003px 时各种拖动都在 0.25px 以内。
- 线拖出轴线的范围后，求解区域跟着线变大，网格几乎每帧都变。以前网格形状一变就把缓存清空，示例工程上这样的帧要从头解，约 120ms。现在上一帧的解按坐标双线性插值搬到新网格上当初值。越界量变了时 tent 底场也跟着变，旧底场插值到新网格、减去新底场，作为多一个候选方向交给能量投影：只搬解在装置 C 上差 4.4px，加上这个方向是 2.2px，每帧多 2 到 3ms。这样的帧照样受步数上限约束：装置 B、C 上拖动中流场节点的误差约 1px、2px（从零开始是 2.5px、15px），手停后收敛到 0.15px 以内。
- 两块画板的 `_Frame` 按导线内容缓存在热启动状态里，导线不变就不重建：曲线导线建一次要几毫秒。叠加层的 `_main_guide_frame` 也一样。

## 层序

`_prepare_fold_context` 要追踪折痕，一次 100ms 以上。所以只在按下时和每次停手时算精确的粗网格深度表。拖动中按正反面做连通区域，区域的深度从上一帧重叠最多的区域继承，新出现的区域取相邻区域加一，再按奇偶和正反面对齐。一帧约 1ms。松手时精确管线重算。

## Refer Rect 网格

网格是叠加层，不走场。每帧用这一帧的 mapper 批量算一遍写进 `_GRID_CACHE`，主画板总跟着动，拖纹理板的线时纹理板的网格也跟着动。点数上限 2000，超了就隔点取。折叠处的线段停在最后一个有效采样点，不二分。纹理板轴线没动时，提升的结果一直复用，只算正向。一帧约 3ms。松手时精确管线重算；取消时丢掉预览的网格，重算原来的。

## 进程优先级

后端启动时关闭 Windows 的执行速度限制（`backend/main.cpp` 的 `optOutOfPowerThrottling`）。没有窗口的进程满载几秒后会被当成后台工作挪到效率核，一切都慢一倍，而且不会自己回来。i7-14700KF 上实测：纯 Python 循环 46ms，满载后 88ms，关闭限制后 45ms。

测量也要注意这一点：同一进程跑几秒之后，绝对毫秒数可能翻倍；机器核被占满时，连两个墙钟时间的比值也会因调度时间片切割而失真。因此功能回归（`tests/t_*.py`）不绑毫秒上限，只断言保证帧预算成立的结构不变量（`WARM_MAX_ITER` 步数上限与热启动迭代次数比、`third_of_batch` 与 `_Frame` 缓存复用、Mapping Area 预光栅化查表、拖动中只继承层序而不重跑 `_prepare_fold_context` 或粗网格深度表、向量化批调用零标量回退）；真实墙钟帧预算（中位 33ms、折叠装置最大 45ms）归口到同样关闭了执行速度限制的专用基准 `tests/bench_drag_cost.py`。

## 节流

`kUpdateHookIntervalMs`（33ms）限制覆盖层拖动的 `move` 钩子，所以预览最快 30Hz。Python 侧只留 8ms 的下限（`MIN_UPDATE_INTERVAL`），不再按上一帧耗时多等半帧：C++ 已经限了频率，移动积压不起来，多等的半帧只把每帧拉长了一半。C++ 在调用 Python 之前就重开 33ms 的窗口，所以比 33ms 慢的帧（拖纹理板轴线约 42ms）一帧接一帧地算，之间只隔这 8ms 下限。无 Qt 后端的定时器按 Windows 默认的 15.6ms 粒度唤醒（`call_later(4)` 实际约 15ms），所以拖动中不能靠 `call_later` 排帧。被节流跳过的移动会启动单次定时器，在当前 33ms 窗口结束时补发最后一次的位置和修饰键并刷新，停手不松手也不会丢掉末尾那一段；松手、取消或拖动清空时定时器随之失效，松手前 C++ 仍补发一次不节流的 `move`。

## 测试

| 项 | 命令或位置 |
|---|---|
| 通用求解 | `py tests/t_gridsolve.py` |
| 热启动和批量取值 | `py tests/t_warm_solve.py`、`py tests/t_batch_map.py` |
| 预览 | `py tests/t_mapping_preview.py` |
| 网页 | `npm test`（`webui/tests/field_view.test.ts`） |
| 真实 WebGL | vite 下打开 `/tests/field_compare.html`，标题以 PASS 开头。mock GL 查不出着色器链接失败，改着色器必跑 |
| 后端 | `backend/tests/session_smoke.mjs`、`board_headless_test` |
| 基准 | `py tests/bench_drag_cost.py --all --warm`、`py tests/bench_drag_cost.py --preview --all` |

基准里拖动每步不到 1 像素，比真手慢得多，帧预算是否够用要看每帧 3 到 10 像素的情况。
