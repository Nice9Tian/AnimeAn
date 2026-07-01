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
    "event": "update" | "linefinish" | "erasefinish" | "deletefinish" | "fillfinish" | "movefinish" | "extra" | "option",
    "tool": "pen" | "move" | "eraser" | "delete_line" | "fill" | "extra",
    "base_tool": "pen" | "move" | "eraser" | "delete_line" | "fill",
    "property": "...",
    "cell": {"row": int, "layer": int, "asset": int, "frame_id": int},
    "stroke": {"id": int, "property": str, "width": float, "point_count": int, "total_length": float, "index": int},
    "position": {"x": float, "y": float},
    "delta": {"x": float, "y": float},
}
```

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
