# 拖动时实时显示映射结果（issue #46）

计划本身不是承诺：承诺在 `../../product_features.md`，语义在 `../../tools_user_workflow/auto_mapping.md` 和 `../../auto_mapping/layer_units.md`。

## 目标

拖动轴线、附加线、轮廓线时实时看到映射结果，松手换成精确结果。

## 已定决策（用户 2026-11-02）

1. 全量预览：流场、正反面、切断都实时；切断边缘允许亚网格级差别；预览不写模型、不进历史；松手跑现有精确管线替换。WebGL 不可用时退回松手重跑。Live Re-render 关掉时没有预览。
2. 全局层序低频更新：拖动中最多每 200ms 一次，来不及就跳过，松手由精确管线重算。
3. 分支从 `claude/mapping-tools` 的 `cb87d32` 切出；原 worktree 未提交的改动不带过来，原会话提交后再合并。

## 第 1 步测量

脚本：`tests/bench_drag_cost.py`（Argon 写，主 Agent 复测过 C 装置，数字一致）。单步中位数，单位 ms：

| 项 | A 直轴无附加线 | B 曲轴 3 条附加线 | C 再加 1 条地线且可折叠 |
|---|---|---|---|
| `build_mapper` | 0.04 | 248 | 147 |
| 其中 CG（4 轮 × 2 分量） | 0 | 211（约 3460 次迭代） | 107（约 1300 次） |
| 其中场构造 | 0 | 36 | 38 |
| `_occlusion_overlay_items` | 2.7 | 33 | 38 |
| 129×129 逐点 `mapper(p)` | 37 | 121 | 126 |
| 200 条笔画 40 段（全管线） | 198 | 597 | 719 |

issue 里的 160ms 对应 C 装置的求解加叠加层。

主 Agent 在临时副本上测了热启动（16 步拖动，和每步的冷启动精确解比）：

| 做法 | B 迭代 / 流场误差 | C 迭代 / 流场误差 |
|---|---|---|
| 沿用上一帧 boost，只做 1 轮 | 138 / 5.1px | 11 / 7.4px |
| 不沿用 boost，每帧重放全部轮次，每轮用上一帧同一轮的解作初值，tol 1e-6 | 168 / 0.07px | 1 / 9.7px |
| 同上，tol 1e-8 | 581 / 0.006px | 27 / 0.52px |
| 同上，tol 1e-10 | 1068 / 0.001px | 102 / 0.017px |

结论：

- boost 依赖路径，**不能沿用上一帧的 boost**，每帧要从原始 ask 重放。
- 现有的停止条件相对冷启动的初始残差，C 装置走 Jacobi 范数，tol 要比 B 严两个量级；热启动要换停止条件，不能照搬一个 tol。
- 一次 CG 迭代 numpy 约 60µs，热启动后 CG 在 10ms 量级。

## 求解放在哪：Python（numpy）

放 GPU 不划算：CG 每步要读回内积；解出来还要和 `hv` 组合，这是 Auto Mapping 的含义，放进着色器就违反「GPU 能力通用」。放 C++ 只有 2 到 3 倍，而热启动已经省了 20 倍以上。所以求解和逐点取值都留在 Python，批量化；GPU 只负责显示。

帧时间目标：单次预览更新中位数不超过 33ms（B、C 两种装置），线本身每次移动都跟手。

## 接口契约（三个包都以此为准）

### 场数据

一张场是 `W × H` 个节点、每个节点 4 个 Float32（小端）：`dx, dy, tag, 保留`。逐行排列，下标 `(j * W + i) * 4`，第 0 行是 `rect` 的上边。节点落在 `rect` 的四条边上，节点 `(i, j)` 的画布坐标是 `(x + w * i / (W - 1), y + h * j / (H - 1))`。

### 后端 → 网页

文本帧后紧跟一个二进制帧，和 `image` 同样的配对方式：

```json
{"t":"field","key":"k","w":129,"h":129,"version":7,"bytes":266256}
```

- `version` 全局单调递增。
- 同一个 `key` 新值覆盖旧值。后端只在发板帧之前发场，收到 ack 前不发，来不及的旧场直接丢弃。

### 绘制指令

```text
["F", key, version, x, y, w, h, maxEdge, passes]
["f"]
```

- `F` 打开一段场作用域，`f` 关闭。作用域里是普通指令（`P`、`B`、`S` 等），坐标在**源画板**空间。
- `passes` 是 `[[tagLo, tagHi, opacity, r, g, b, a], ...]`，从后往前画。每一遍把作用域里的全部指令画一次。`r, g, b, a` 是 0 到 255 的换色，`a` 为 0 时不换色，否则按 `a / 255` 把片元颜色混向它，用于背面的衬里色。
- 顶点位置 = `p + D(p)`，`D` 由 `(dx, dy)` 在节点上双线性插值。片元用未变形的 `p` 取最近节点的 `tag`，不在 `[tagLo, tagHi]` 里或 `p` 在 `rect` 外就丢弃。
- `maxEdge` 是画布单位：几何的边长不许超过它，长边先细分，否则两个顶点之间不跟场走。填充的覆盖四边形要盖满视口。
- `key` 没到网页、或版本对不上时整段不画。Canvas 2D 渲染器跳过整段作用域。
- `version` 在指令里，所以场一变，增量绘制的 `keep` 自然停在 `F` 之前，这段之后的指令重画。

### Python 绑定（`animean_python.ui`，都是通用函数）

```python
ui.set_field(key, width, height, data)   # data 是 bytes，长度 width*height*16
ui.set_field_view(view, spec)            # spec 为 None 时清除
ui.field_display_supported(view) -> bool
ui.call_later(ms, callable)              # 一次性定时，回主线程调
```

`spec`：

```python
{"field": key, "rect": (x, y, w, h), "max_edge": 8.0,
 "source_view": "child", "columns": [列下标...], "frame": 帧号,
 "width_scale": 1.0,
 "passes": [{"tag": (lo, hi), "opacity": 1.0, "tint": (r, g, b, a)}, ...],
 "hide_columns": [目标画板的列下标...],
 "skip_properties": [源画板上不画的笔画属性...]}
```

- C++ 在 `paintSceneContent` 之后、覆盖层之前发 `F`，用源画板的模型画那些列的填充和笔画（不含引导线属性的笔画由调用方挑列决定），线宽乘 `width_scale`，再发 `f`。
- `hide_columns` 只影响这块板的显示，不改模型，不影响导出和缩略图。
- `field_display_supported` 读网页上报的能力，缺省 False。桌面 Qt 版恒为 False。

### 能力上报

```json
{"t":"call","id":1,"method":"board.capabilities","params":{"view":"main","fieldDisplay":true}}
```

网页在渲染器创建和切换时发。WebGL2 渲染器 true，Canvas 2D false。

### Python 通用模块

`pyfile/gridsolve.py`，不认识 Auto Mapping：

```python
pcg(apply_a, b, x0=None, precond=None, tol=..., max_iter=...) -> (x, iterations)
det2(dx, dy, du, dv) -> 每个节点的雅可比行列式，四个单侧模板取最小
```

## 施工顺序

可以并行的三个包，各开子 worktree，分支从本分支切：

- **网页包**：`field` 帧、场纹理、`F`/`f` 作用域、细分、能力上报。
- **后端包**：`set_field` 等绑定、`F`/`f` 的记录、场的发送和丢弃、`hide_columns`、`call_later`。
- **求解包**：`gridsolve.py`；`_FlowFieldWarp` 的热启动入口；批量取值；每条线的场缓存。无 `warm` 时行为逐位不变。

依赖上面三个的一个包，等它们合并后开工：

- **预览包**：`pyfile/mapping_preview.py`，接拖动的按下、移动、松手、取消；正反面、切断、层序写进 `tag`；差分测试。

## 差分测试阈值（待用户确认，属 Auto Mapping 语义，暂放这里）

- 流场：网格节点上和精确解差不超过 0.5 画布像素。
- 正反面：离 `det = 0` 一格以外的节点判断全部一致。
- 切断：和精确切断位置差不超过 1 个网格单元。

## 风险

- 笔画线宽在源空间展开后再变形，局部缩放大的地方和松手结果粗细有差。
- 拖动幅度大时一帧的热启动追不上，当帧误差超阈值，下一帧收敛；差分测试按小步长量。
- 预览不画折痕线（承诺里没有），松手才有。

## 回填

（做完填写实现方法、文件、测试结果。）
