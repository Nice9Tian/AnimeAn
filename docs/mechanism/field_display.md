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

## 退路

`board.capabilities` 上报能力，WebGL2 为真，Canvas 2D 为假，桌面 Qt 版恒假。不支持时 Python 不开预览，行为和松手重跑完全一样。Canvas 2D 收到作用域整段跳过。

## 求解在 Python

issue #46 第 1 步实测：一次 CG 迭代 numpy 约 60µs。放 GPU 要读回内积，解出来还要和 `hv` 组合，这是 Auto Mapping 的含义；放 C++ 只快 2 到 3 倍。所以求解和取值都在 Python，批量化；GPU 只负责显示。

`pyfile/gridsolve.py` 是通用的共轭梯度和雅可比行列式，不认识 Auto Mapping。

## 节流

`kUpdateHookIntervalMs`（33ms）限制覆盖层拖动的 `move` 钩子，所以预览最快 30Hz。松手前 C++ 补发一次不节流的 `move`。停手但不松手时，最后 33ms 内的移动要到下一次移动或松手才显示。

## 测试

| 项 | 命令或位置 |
|---|---|
| 通用求解 | `py tests/t_gridsolve.py` |
| 热启动和批量取值 | `py tests/t_warm_solve.py`、`py tests/t_batch_map.py` |
| 预览 | `py tests/t_mapping_preview.py` |
| 网页 | `npm test`（`webui/tests/field_view.test.ts`） |
| 真实 WebGL | vite 下打开 `/tests/field_compare.html`，标题以 PASS 开头。mock GL 查不出着色器链接失败，改着色器必跑 |
| 后端 | `backend/tests/session_smoke.mjs`、`board_headless_test` |
| 基准 | `py tests/bench_drag_cost.py --rig A|B|C|D [--warm]` |
