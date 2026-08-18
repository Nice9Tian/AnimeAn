# AnimeAn Python 绑定中文说明

本文说明 AnimeAn 当前通过 `pybind11` 暴露给 Python 的能力，以及 ExtraTool 算法开发的适用边界。底层模块名是 `animean_python`，高层封装在 `pythonbind/animemodel.py`。

## 模块结构

- `animean_python`：C++ 扩展或嵌入式模块，来自 `pythonbind/python_bindings.cpp`。
- `pythonbind/animemodel.py`：Python 友好封装，提供 `AnimeModel`、`animemodel`、`annimemodel`、`ui`、`model_pybind`。
- `animean_python.model_pybind`：把 Python 的 tuple/list/dict 转成 Qt/C++ 值，再返回标准 dict/list。
- `animean_python.vectorlogic`：暴露矢量绘制、命中测试、擦除区间、路径分段和区域填充相关几何算法。
- `animean_python.get_scene()` / `animean_python.get_current()`：在 AnimeAn 内嵌 Python 环境中读取当前 UI scene 和当前帧/层/asset。

## 快速示例

```python
from animemodel import AnimeModel, ui

model = AnimeModel()
model.initialize(layer_count=2, frame_count=24)
ui.set_current(frame=0, layer=0)

model.add_polyline(
    [(0, 0), (100, 80), (160, 20)],
    color=(0, 0, 0, 255),
    width=3.0,
)

print(model.cell(to_poly=True))
```

也可以直接使用底层模型：

```python
import animean_python

scene = animean_python.SceneModel()
scene.initialize_scene(2, 24)
scene.add_polyline(0, 0, [(0, 0), (100, 80)], r=0, g=0, b=0, a=255, width=3.0)
```

## 索引约定

- 底层 `SceneModel` 使用 0 基索引：`row=0` 是第一帧，`layer_index=0` 是第一层。
- 高层封装中，参数名为 `id`、`index`、`frame_id` 时也使用 0 基索引。
- `get_stroke(num=1)` 和 `fillarea(1)` 使用用户可见序号；需要 0 基索引时使用 `index=0`。

## UI facade

内嵌运行时可以从 `animemodel` 导入 `ui`：

```python
from animemodel import ui

ui.refresh()
ui.main.refresh()
ui.children.refresh()
ui.frame.refresh()
ui.layer.refresh()
ui.asset.refresh()
ui.widget.refresh()

ui.set_current(frame=0, layer=0, asset=None)
```

`animean_python.ui` 还提供两个通用显示服务（脚本工具用，具体语义由脚本决定）：

```python
# 在指定画板上层显示一组 overlay（折线或闭合多边形），完全替换之前的 overlay
animean_python.ui.set_overlay("main", [
    {"id": "my_item", "points": [(0, 0), (100, 100)],
     "color": (255, 0, 0, 255), "width": 3.0, "removable": True},
    {"id": "my_zone", "points": [(0, 0), (80, 0), (80, 80)], "closed": True,
     "color": (0, 0, 255, 190), "fill_color": (0, 0, 255, 60)},
])

# 设置当前绘制颜色（不切换工具）
animean_python.ui.set_draw_color((40, 110, 255, 255))
```

overlay 始终绘制在所有图层之上，不属于场景模型，也不会被保存。`removable=True` 的 overlay 会在其包围盒右上角显示 "x" 按钮，点击时 C++ 不做任何删除，只派发 `overlayremove` hook 事件（带 `overlay.id`），由脚本决定如何处理。

脚本修改模型后，如果需要界面立刻更新，调用对应的 `ui.*.refresh()`。长时间同步计算可以用：

```python
ui.freeze()
try:
    run_long_algorithm()
finally:
    ui.unfreeze()
```

这不会把 Python 变成异步执行，只是阻止 UI 在线程繁忙时继续交互。

## ExtraTool 开发边界

当前 ExtraTool 是“Python 辅助的 Pen 工作流”，不是完整自定义鼠标工具框架。用户点击 ExtraTool 后，绘图窗口仍使用原生 Pen，C++ 会给后续笔画设置 `property`，Python 通过 hook 接收事件。

适合现在开始开发的算法：

- 用户画完一笔后做后处理，例如中线提取、清理、自动补线、stroke 替换。
- 读取当前 cell/stroke，调用 `vectorlogic` 或纯 Python 算法，再写回新的 stroke。
- 根据当前帧/层/asset 做矢量区域、路径、命中测试相关分析。

暂时不适合的算法：

- 完全接管鼠标按下、拖动、释放的自定义交互。
- Python 端实时绘制 preview overlay。
- 大量 raster 图像写入或复杂 fill layer 编辑；这些入口目前没有完整 Python API。

ExtraTool 在 `pyfile/extra_tools.py` 中注册：

```python
def extra_tools():
    return [
        {
            "name": "midline",
            "title": "Midline",
            "property": "midline",
            "handler": "midline_tool.activate_midline_tool",
        },
    ]
```

典型 handler：

```python
import python_hooks
from animemodel import get_current, ui


def midline_process(cell, stroke, message):
    if message.get("property") != "midline":
        return

    current = get_current()
    if current is None:
        return

    scene = current.raw_scene
    row = cell["row"]
    layer = cell["layer"]
    stroke_index = stroke.get("index")
    if stroke_index is None:
        return

    stroke_data = scene.cell_to_dict(layer, row, True, 2.0)["image"]["strokes"][stroke_index]
    points = stroke_data.get("polylines", [[]])[0]
    if len(points) >= 2:
        scene.add_polyline(row, layer, points, r=255, g=0, b=0, a=255, width=1.0)
        ui.widget.refresh()


def activate_midline_tool(name="midline", property_value="midline"):
    python_hooks.set_hook(
        midline_process,
        linefinish=True,
        tool="extra",
        property=property_value,
    )
    print(f"{name} tool activated with property={property_value}")
```

注意：选择 ExtraTool 时，C++ 会先发一次 `extra` 事件，再调用 handler 注册 hook。因此 handler 里注册的 hook 通常应该监听后续 `linefinish` 或 `update`，不要依赖捕获同一次点击产生的 `extra` 事件。

## Hook 消息

常见消息字段：

```python
{
    "event": "update" | "linefinish" | "erasefinish" | "deletefinish" | "fillfinish" | "movefinish" | "extra" | "option" | "overlayremove",
    "view": "main" | "child",
    "tool": "pen" | "move" | "eraser" | "delete_line" | "fill" | "extra",
    "base_tool": "pen" | "move" | "eraser" | "delete_line" | "fill",
    "property": "...",
    "cell": {"row": int, "layer": int, "asset": int, "frame_id": int},
    "stroke": {"id": int, "property": str, "width": float, "point_count": int, "total_length": float, "index": int},
    "position": {"x": float, "y": float},
    "delta": {"x": float, "y": float},
}
```

`view` 表示事件来自哪个画板：`main` 是主画板（main_paint_view），`child` 是子画板（child_paint_view）。

`overlayremove` 事件在用户点击 overlay 的 "x" 按钮时派发，额外携带 `"overlay": {"id": "..."}`；模型不会被自动修改，由 hook 自行决定删除什么。

`historyrestore` 事件在撤销/重做/历史跳转/历史重置（含打开工程）之后派发，脚本可借此从场景的 `script_data()` 重建自己的状态。

**否决历史提交**：`linefinish` / `erasefinish` / `deletefinish` / `fillfinish` / `movefinish` 的 hook 若发现这次操作实际没有改变任何东西（例如工具点击落空），可在消息里写 `message["cancel_history"] = True`，C++ 会跳过随后的历史记录，避免产生空记录并误清 redo。

`option` 事件还会携带：

```python
{
    "option": {
        "name": "...",
        "type": "button" | "list" | "slider",
        "value": object,
        "hook": "...",
        "row": int,
        "start_column": int,
        "end_column": int,
    }
}
```

## 双画板（child_paint_view）

主窗口采用**三段式工作区**布局：

- **左列**：上为 `child_paint_view`（子画板/texture view，独立场景模型），下为工具栏（Tools）。子画板是**无限画布**：整个视口都是纸面，没有页面边框，平移不受页面边界限制（滚轮缩放 + 中键平移，无滚动条），图案画在哪都行 — 反正映射按中心线缩放。
- **中间**：`main_paint_view` 主视图 — 作为中央部件锚定，始终可见并随窗口缩放（Qt 的中央区不能浮动，这也是 Krita/PS 的模式：主画布是工作区的锚）。
- **右列**：面板栏（Tool Options / Layers / Assets / History 纵向排列）。
- **底部**：时间轴（Frames）横贯左侧和中间，右侧为 Python Debug。
- 子画板可拖出浮动或停靠到任何区域，View 菜单可开关；打开工程、File 菜单导入、撤销落到子画板时会自动显示并激活。

焦点进入某个画板时，Layers/Frames/Assets 面板会切换到该画板的数据。子画板顶部有两个开关：

- `Changable Timeline`（默认关）：关闭时，焦点进入子画板后 Frames 面板仍显示主画板的时间轴（子画板通常只需要单帧）。
- `Changable Layer`（默认开）：关闭时，Layers/Assets 面板同样保持主画板不变。

### Alt / Shift 轴向吸附（画直线）

按住 **Alt 或 Shift** 用画笔绘制时，线条会吸附成**水平或垂直直线**：

- 从起笔点（或中途按下 Alt 的那一点）算起，鼠标位移一旦超过阈值（默认 **5 屏幕 px**），按主方向锁定轴向 — 横向位移大则水平、纵向大则垂直；锁定前累积的微小抖动会被回溯拉直，不留起笔小钩。
- 锁定后继续拖动，所有点都投影在该轴上；**松开 Alt 立即恢复自由绘制**，可在一笔之内自由/直线交替（中途再按 Alt 会以当前点为新锚点重新判定方向）。
- 两个画板都支持；对所有画笔类工具生效 — 配合 `H Center Line` / `V Center Line` 可以直接画出严格水平/垂直的中心线。
- 阈值可通过 C++ 侧 `PaintOpenGLWidget::setAxisSnapThreshold()` 调整（暂未暴露到选项面板）。阈值是**屏幕像素**，经 `algorithm/viewscale.h` 换算 — 此前它被直接拿去和画布距离比较，8x 缩放下要移动 40 屏幕 px 才锁定，0.1x 下几乎瞬间锁定。

### 按住不动画直线（Pen）

按下画笔后**保持不动**，即可把这一笔变成直线，无需任何修饰键：

- 判定条件：笔尖停留在 **10 屏幕 px** 的圆内、持续 **1.5 秒**。半径是屏幕尺寸的，经 `algorithm/viewscale.h` 换算，所以任何缩放下都是"手不动"这同一件事；圆心记在画布空间，绘制中途缩放不会把它拖偏。
- 触发后，整笔立刻塌缩为**从起笔点到当前光标的一条直线**，并**跟随鼠标**直到松开左键 — 期间不再做曲线拟合与平滑，线尾精确落在光标上。
- 手抖不会打断计时：只有真正**离开圆**才重新开始等待；反过来，正常运笔速度会立刻离开圆，绝不会误触发。
- 直线模式下 **Alt / Shift 仍可用**，以直线自身的起点为锚点锁定水平/垂直。
- 提交的几何是一条 `line` 元素，Arrow 编辑模式可直接逐字读入（两个锚点）。

### 画布视口（缩放 / 平移 / 滚动条）

- **滚轮缩放**：以光标为锚点缩放（0.1x – 8x），光标下的内容保持不动。
- **鼠标中键拖动**：平移画布。
- **横/纵滚动条**：画布放大超出视口时可用，与缩放/平移实时联动（`PaintViewContainer` 包裹画布，主画板和子画板都有）。
- 画布外区域显示为深灰色；擦除半径、吸附阈值等以文档坐标为准（视觉上随缩放变化）。导出 PNG 截取的是当前视图。

### 活动画板指示 / Arrow 工具

- **当前活动画板**（撤回/重做、面板、工具消息的作用目标）的画布边缘显示**蓝色描边**，随焦点切换。
- 工具栏新增 **Arrow 工具**：点击画布仅切换焦点/活动画板，不产生任何绘制（后续可扩展为选择工具）。

### 时间轴播放（预渲染）

时间轴面板（Frames）新增 **Play / Pause** 按钮：

- 点 **Play** 会先把该画板的**每一帧预渲染成像素图**（按当前缩放/平移，与你看到的画面一致），再以 **12 fps** 循环播放 — 播放时只做图像 blit，帧率不受矢量内容复杂度影响。状态栏会显示预渲染进度与播放信息。
- 播放中时间轴的高亮跟着走，但**模型完全不动**（不改当前帧、不产生历史记录）。
- 点 **Pause**（或在画布上点一下、滚轮缩放）即停止，**切回矢量显示**，并把可编辑状态落在你暂停的那一帧上，可以直接继续画。
- 播放的是时间轴当前指向的画板（受子画板 `Changable Timeline` 开关影响）。切换画板、撤销/重做、增删帧、打开工程都会自动停止播放（预渲染已失效）。
- 预渲染只包含画面内容 — 引导线、选区、refer rect 网格等编辑辅助不入画。
- 内存上限 512 MB：帧数 × 视口尺寸超出时会拒绝播放并在状态栏说明（缩小窗口或减少帧数即可）。播放帧率常量 `kPlaybackFps` 在 mainwindow.cpp 里，需要别的速度改一处即可。

### 撤回 / 重做与 History 窗口

- **Edit 菜单**新增 `Undo`（Ctrl+Z）和 `Redo`（Ctrl+Y 或 Ctrl+Shift+Z），按**全局时间顺序**执行：Undo 总是先撤最近的一步操作，无论它发生在主画板还是子画板；Redo 严格按相反顺序恢复（两个画板各有独立历史，各 100 条上限）。若撤回/重做落在另一个画板上，会自动显示、聚焦并切换到该画板，状态栏提示 "Undo in <画板>: <被撤回的操作>"。
- **Redo 是撤回链的精确逆操作**：在任何画板上做了新操作，两个画板的 redo 尾巴都会被清除 — Ctrl+Y 永远不会复活一个时间上下文已不存在的旧操作。
- 新增 **History 停靠窗**：列出活动画板的操作记录（笔画、擦除、填充、图层/帧/素材操作、导入、Auto Mapping 等），当前状态高亮，可重做的后续状态灰显；**点击任意一行直接跳到那个状态**（双向跳转）。切换画板焦点时列表跟着切换。
- 实现是**快照制**：每步操作后记录整个场景模型的副本（Qt 隐式共享，未改动部分零拷贝），撤回即恢复快照 — 所以任何来源的模型改动（含 Python 脚本）都能被完整还原。
- 打开工程 / 打开 texture view 会重置对应画板的历史（以打开后的状态为新起点）。历史不写入工程文件。
- mapping_asset（中心线/选区 overlay）是会话数据，不参与撤回。
- Python 脚本改动模型后调用 `animean_python.ui.history_commit(label, view="")` 即可让这次改动成为一条可撤回的记录（`view` 空 = 活动画板）；也可用 `ui.history_undo(view)` / `ui.history_redo(view)`。`auto_mapping` 每次映射后会自动提交一条 "Auto Mapping"。

### 文件导入/导出的画板归属

内容导入不再跟随焦点，而是按菜单显式区分目标画板：

- **File 菜单** 的 `Import Raster` / `Import OpenToonz Lines` / `Import Clip Studio Paint` **始终导入到主画板**（main_paint_view），无论当前焦点在哪，触发时会自动激活主画板。（`.clip` 由内置的无依赖 Clip Studio 矢量读取器 `clipreader` 解析。）
- **Texture View File 菜单**（新增顶层菜单，texture view = 子画板）用于操作子画板的文件：
  - `Import Raster / OpenToonz Lines / Clip Studio Paint into Texture View...`：把这三种格式导入到**子画板**（会先显示并激活子画板窗口）。
  - `Open Texture View...` / `Save Texture View As...`：把子画板场景作为 `.animean` 工程文件独立读写（与主工程互不影响，便于复用同一套纹理图案）。
  - `Export Texture View Image...`：把子画板当前画面导出为 PNG（抓取画布帧缓冲；若此时 overlay 引导线可见会一并出现在图里）。

内嵌 Python 全局变量（每次选择变化时同步）：

```python
model               # 当前焦点画板的 SceneModel
main_model          # 主画板 SceneModel
child_model         # 子画板 SceneModel
active_view         # "main" 或 "child"
canvas_width/height             # 当前焦点画板尺寸
main_canvas_width/height        # 主画板尺寸
child_canvas_width/height       # 子画板尺寸
```

`animean_python.get_scene()` 会返回两个场景，`sceneName` 分别是 `main_paint_view` 和 `child_paint_view`。

## auto_mapping 工具（脚本实现：pyfile/auto_mapping.py）

工具栏新增 ExtraTool：`H Center Line`、`V Center Line`、`Mapping Area`、`Auto Mapping`。

**映射算法**（Coons 插值，即原 "Auto Mapping 2"）：按象限做纯 Coons 补丁插值（公式坍缩为 `H(s)+V(t)−O`）— V 方向位移沿脊柱只平移不旋转，弯曲处图案是"平移副本"，雅可比与离轴距离无关、恒不退化，绝无旋转引入的折叠。直线引导下等价于精确仿射变换。早期的"脊柱旋转"算法（原 `Auto Mapping`）因在曲率半径处结构性奇异已移除，代码与数学分析归档在 `old_history/`。

**Refer Rect（映射网格检查）**：选中 Auto Mapping 工具后，选项面板有 `Refer Rect` 开关。开启后两个画板同时叠加橙色 3×3 锚点网格 — 锚点即十字线定义的 9 个点（交点、4 个线端点、4 个象限角点），child 侧显示参考框本身，main 侧显示它映射后的像 — 网格哪里歪了，映射问题就在哪里。网格随引导线修改、撤销、重新映射自动刷新。

使用流程：

1. 在 child_paint_view 用 `H Center Line` / `V Center Line` 各画一条中心线。这两条线定义图案的 UV 坐标系。
2. 用普通画笔在 child_paint_view 里画图案内容。
3. 在 main_paint_view 里同样画一条 H 中心线和一条 V 中心线，位置/长度/方向随意（可以是曲线）。
4. 可选：用 `Mapping Area` 圈定映射范围（见下）。
5. 点击 `Auto Mapping`：child 里的图案笔画按 UV 分解（相对两条中心线交点），再沿 main 的两条中心线按弧长重建。**参数化以端点为锚**：交点把每条中心线分成两段，child 侧的每一段按比例对应 main 侧的同一段 — **线的端点对应端点、交点对应交点**（child 中心线接近直线时精确；画成明显弧线时按弦近似），图案范围就是你画的线段范围。两点注意：① 两个画板的**交点相对位置尽量一致**（比如都在中点附近）— 两侧比例差异过大时跨越中心线的笔画会在线处产生折角，超过 1.5 倍会在 Python Debug 打印提示；② 两条中心线**必须实际相交**，否则拒绝映射并提示延长；T 型相交（交在端点上）安全，空侧沿用另一侧比例。线宽用全局几何平均缩放（两侧比例不同时为近似值）。main 的中心线是曲线时，图案跟着弯曲：V 方向位移沿 H 脊柱**平移**（Coons 插值坍缩形式 `H(s)+V(t)−O`），不随切向旋转，所以弯曲处得到的是图案的平移副本，不会出现旋转折叠。直线情形下等价于精确仿射变换。两条中心线若没有实际相交会在 Python Debug 面板给出警告（此时用最近点做兜底原点，建议延长使其相交）。画完中心线/选区后画笔颜色自动恢复黑色。

### Mapping Area（映射区域，油漆桶式取区）

`Mapping Area` 在封闭形状内点一下，Python 用 `vectorlogic.vector_region_path_at`（与油漆桶同一套区域算法，以所有可见图层的描边为边界）探测出这块区域，显示为半透明浅蓝色。映射时按区域裁剪/筛选：

- 区域画在 **main_paint_view**：映射输出被裁剪到区域内（笔画在边界处精确截断），相当于"把图案填进这个形状里"。
- 区域画在 **child_paint_view**：只有区域内的图案内容参与映射（先裁剪源，再做 UV 变换）。
- 两个画板可以同时各设一个区域；重新点击会替换旧区域。

### mapping_asset：不进图层的引导数据

H 中心线、V 中心线、Mapping Area 三者统称 **mapping asset**，由 `auto_mapping.py` 里的字典 `_MAPPING_ASSETS`（按画板分组）维护：

- 画完的瞬间（linefinish hook）几何数据被取出存入字典，同时**从场景模型中删除** — 它们不会出现在 Layers/Assets 面板里，不占用图层，也不算图案内容。
- 显示走通用 overlay 通道（`ui.set_overlay`）：**始终置于顶层**，颜色固定绑定 — 水平线**纯蓝 (0,0,255)**、垂直线**纯绿 (0,255,0)**、区域半透明浅蓝；选择工具时画笔颜色也会自动切到对应色。
- 选中 `H/V Center Line` 工具时，选项面板提供 **Smooth / Width 滑条**（与画笔同一套参数）：中心线按当前平滑度成形后再被捕获。
- 每个元素包围盒右上角有 **"x" 按钮**，点击派发 `overlayremove` 事件，脚本从字典删除并刷新 overlay，然后直接重画即可。
- 水平线/垂直线/区域**每个画板各只有一个**（字典键唯一），重画/重点自然替换旧的。
- **可撤销、可存档**：字典的每次变化会序列化进场景的通用 `scriptData` 字段（C++ 只存不解释）。它随历史快照一起被记录 — 画中心线、点选区、点 "x" 删除都产生/并入历史记录，Ctrl+Z/Ctrl+Y 可完整还原（撤销/重做后脚本经 `historyrestore` 事件自动重建字典和 overlay）；`scriptData` 也随 `.animean` 保存，中心线/选区现在**跨会话保留**。旧版本存放在图层里的中心线/区域仍会在下次运行 `Auto Mapping` 时自动迁移。

### 映射结果落在专属的 mapped layer

点击 `Auto Mapping` 后，映射内容**不会写进当前选中的图层**，而是自动创建/复用主画板中一个名为 **`mapped layer`** 的专属图层：

- 第一次运行时自动新建该图层（沿用应用新建图层的惯例，追加在图层栈末尾＝最下层；需要的话可在 Layers 面板拖动调整顺序）。
- 之后每次重跑都**复用同一个图层**（按名字查找，`mapped layer1` 之类的自动去重命名也认），不会每点一次就多出一个图层。
- **不会抢走你的图层选择**：新建图层后会把当前帧/层/素材恢复成你原来的选择，所以点完 Auto Mapping 继续画，笔画仍然落在你自己的图层里。
- 重跑前会清除本帧上所有旧的映射笔画（会扫描全部图层，所以旧版本写进当前图层的历史结果也会被一并清掉，不会残留重影）。
- 在不同帧上运行会在同一个 `mapped layer` 里各自占一格（帧号唯一、互不别名），一帧的重跑不会破坏其它帧已有的映射。
- 因为它是独立图层，可以单独隐藏/锁定/删除/调整顺序；删除后再运行会重新建立并继续复用。
- 映射结果不会参与 `Mapping Area` 的区域探测（否则上一次的图案会把油漆桶区域切碎）。

说明：

- 映射生成的笔画带有 `property="auto_mapped"`，再次点击 `Auto Mapping` 会先删除上一次的映射结果再重新生成（可反复调整 main 中心线/区域后重跑）。
- 线宽按两个方向缩放比例的几何平均缩放。
- 目前映射矢量笔画；填充区域和栅格内容不参与映射。
- 反馈信息输出在 Python Debug 面板。

### 分工原则

C++ 端只提供通用机制：场景模型、几何算法绑定（vectorlogic）、hook 事件派发、overlay 显示服务（`set_overlay` / `set_draw_color` / `overlayremove` 事件）、ExtraTool 的 `base_tool` 声明。具体工具的属性名、颜色、字典结构、区域探测、裁剪逻辑全部在 Python 端（`pyfile/auto_mapping.py`），修改工具行为不需要重新编译。

## AnimeModel 常用接口

```python
AnimeModel(scene=None)
AnimeModel.from_scene(scene)

model.initialize(layer_count=2, frame_count=2)
model.get_structure()
model.get_frame(id=None, index=None, name=None, Name=None)
model.get_layer(id=None, name=None, Name=None, asset_name=None, frame_id=None)
model.cell_image(frame=None, layer=None, create=True)
model.add_polyline(points, frame=None, layer=None, color=(0,0,0,255), width=3.0)
model.add_stroke(points, frame=None, layer=None, color=(0,0,0,255), width=3.0, smooth=True, smooth_value=50)
model.cell(frame=None, layer=None, to_poly=False)
model.strokes(frame=None, layer=None, to_poly=False)
model.clear_image(frame=None, layer=None)
model.remove_stroke(frame, layer, stroke)
model.remove_fill_area(frame, layer, fill_area)
model.clear_raster(frame, layer)
```

## SceneModel 常用接口

```python
scene.get_structure()
scene.set_current_layer(layer_index)
scene.set_current_frame(frame_index)
scene.current_layer()
scene.current_frame()
scene.add_layer()
scene.add_frame()
scene.delete_layer(layer_index)
scene.delete_frame(frame_index)
scene.cell_at(row, layer_index)
scene.image_at(row, layer_index, create=False)
scene.current_image(create=False)
scene.cell_to_dict(layer_index, frame_index, to_poly=False, poly_step=4.0)
scene.cell_strokes(layer_index, frame_index, to_poly=False, poly_step=4.0)
scene.stroke_line_list(row, layer_index, stroke_index, ploy=False, simplify=0.0)
scene.add_polyline(row, layer_index, points, r=0, g=0, b=0, a=255, width=3.0)
scene.add_stroke_object(row, layer_index, stroke)
scene.remove_stroke(row, layer_index, stroke_index)
scene.remove_fill_area(row, layer_index, fill_index)
scene.clear_raster(row, layer_index)
```

## model_pybind 数据格式

- 点：`(x, y)` 或 `{"x": x, "y": y}`。
- 矩形：`(x, y, width, height)`、`{"x","y","width","height"}` 或 `{"left","top","right","bottom"}`。
- 颜色：`(r, g, b)`、`(r, g, b, a)` 或 `{"r","g","b","a"}`，缺省 alpha 为 `255`。
- 线段：`(p0, p1)`、`(x1, y1, x2, y2)`、`{"from": p0, "to": p1}` 或 `{"p1": p0, "p2": p1}`。
- 区间：`(first, second)` 或 `{"first": first, "second": second}`。
- 路径：点列表或命令列表。

路径命令示例：

```python
[
    {"type": "move", "to": (0, 0)},
    {"type": "line", "to": (100, 0)},
    {"type": "quad", "control": (120, 40), "to": (100, 80)},
    {"type": "cubic", "control1": (60, 90), "control2": (20, 90), "to": (0, 80)},
    {"type": "rect", "rect": {"x": 0, "y": 0, "width": 100, "height": 80}},
    {"type": "close"},
]
```

## vectorlogic 常用接口

```python
epsilon()
filtered_points(points)
make_smoothed_path(points, smooth_value=50, to_poly=False, poly_step=4.0)
make_polyline_path(points, to_poly=False, poly_step=4.0)
make_stroke(points, color=(0,0,0,255), width=3.0, id=0, filter_input=True, smooth_path=True, smooth_value=50, to_poly=False, poly_step=4.0)
make_stroke_object(points, color=(0,0,0,255), width=3.0, id=0, filter_input=True, smooth_path=True, smooth_value=50)
stroke_hits_circle(stroke_or_points, center, radius, width=3.0)
stroke_hits_capsule(stroke_or_points, from_point, to_point, radius, width=3.0)
keep_ranges_for_circle(stroke, center, radius)
keep_ranges_for_capsule(stroke, from_point, to_point, radius)
complement_ranges(ranges)
sub_stroke(stroke, from_w, to_w, smooth_value=50, to_poly=False, poly_step=4.0)
point_at_length(stroke, length)
segments_from_path(path_like)
compute_vector_region_faces(segments, to_poly=False, poly_step=4.0)
vector_region_path_at(seed, segments, canvas_rect, to_poly=False, poly_step=4.0)
fill_path_from_mask(seed, boundary, to_poly=False, poly_step=4.0)
```

`to_poly=True` 会在返回值中增加采样后的 `polylines`，`poly_step` 控制曲线采样密度。

## 当前边界

- 文档只描述 Python 绑定功能，不包含构建命令。
- Python 侧目前主要适合矢量图像和场景结构编辑。
- 栅格图像写入、fill layer 的更多编辑入口存在于 C++ 模型中，但尚未完整绑定成 Python API。
- `VectorStroke` 的点列、颜色和路径主要通过 `make_stroke_object()` 或 `add_polyline()` 构造；不建议直接在 Python 修改复杂内部字段。
