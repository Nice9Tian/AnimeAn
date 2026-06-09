# AnimeAn Python 绑定中文说明

本文总结 AnimeAn 当前通过 `pybind11` 暴露给 Python 的能力。底层模块名是
`animean_python`，高层封装在 `pythonbind/animemodel.py`。

## 模块结构

- `animean_python`：C++ 扩展模块或嵌入式模块，来自 `pythonbind/python_bindings.cpp`。
- `pythonbind/animemodel.py`：Python 友好封装，提供 `AnimeModel`、`animemodel`、`annimemodel`、`model_pybind`。
- `animean_python.model_pybind`：把 Python 的 tuple/list/dict 转为 Qt/C++ 值，再返回标准 dict/list。
- `animean_python.vectorlogic`：绑定矢量绘制、命中测试、擦除区间、路径分段和区域填充相关几何算法。
- `animean_python.get_scene()` / `animean_python.get_current()`：在 AnimeAn 应用内嵌 Python 时读取当前用户界面的 scene 信息。

## 快速示例

```python
from animemodel import AnimeModel

model = AnimeModel()
model.initialize(layer_count=2, frame_count=24)
model.set_current(frame=0, layer=0)

model.add_polyline(
    [(0, 0), (100, 80), (160, 20)],
    color=(0, 0, 0, 255),
    width=3.0,
)

print(model.cell(to_poly=True))
```

也可以直接使用底层模块：

```python
import animean_python

scene = animean_python.SceneModel()
scene.initialize_scene(2, 24)
scene.add_polyline(0, 0, [(0, 0), (100, 80)], r=0, g=0, b=0, a=255, width=3.0)
```

## 坐标和索引约定

- 底层 `SceneModel` 使用 0 基索引：`row=0` 表示第一帧，`layer_index=0` 表示第一层。
- 高层封装里明确写成 `id` 或 `frame_id` 的入口按 0 基 id 处理。
- `frame` 和 `layer` 参数如果表示直接索引，则仍按 0 基传给底层。

## 可接受的数据格式

`model_pybind` 和 `vectorlogic` 的多数参数都可以用简单 Python 数据表示：

- 点：`(x, y)` 或 `{"x": x, "y": y}`。
- 矩形：`(x, y, width, height)`、`{"x","y","width","height"}` 或 `{"left","top","right","bottom"}`。
- 颜色：`(r, g, b)`、`(r, g, b, a)` 或 `{"r","g","b","a"}`，缺省 alpha 为 `255`。
- 线段：`(p0, p1)`、`(x1, y1, x2, y2)`、`{"from": p0, "to": p1}` 或 `{"p1": p0, "p2": p1}`。
- 区间：`(first, second)` 或 `{"first": first, "second": second}`。
- 路径：点列表，或命令列表。

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

未写 `type` 的路径 dict 会按 `line` 处理。

## 高层 AnimeModel 功能

`AnimeModel` 是最适合脚本使用的入口：

```python
AnimeModel(scene=None)
AnimeModel.from_scene(scene)

model.id()
model.scene_name()
model.set_scene_name(name)
model.scene_id()
model.set_scene_id(scene_id)
model.initialize(layer_count=2, frame_count=2)
model.set_current(frame=None, layer=None)
model.get_structure()
model.frame
model.get_frame(id=None, index=None, name=None, Name=None)
model.layer
model.get_layer(id=None, name=None, Name=None, asset_name=None, frame_id=None)
model.cell_image(frame=None, layer=None, create=True)
model.image(frame=None, layer=None, create=True)
model.asset_image(asset_index, frame_id=1, create=False)
model.add_polyline(points, frame=None, layer=None, color=(0,0,0,255), width=3.0)
model.add_stroke(points, frame=None, layer=None, color=(0,0,0,255), width=3.0, smooth=True, smooth_value=50)
model.cell(frame=None, layer=None, to_poly=False)
model.strokes(frame=None, layer=None, to_poly=False)
model.clear_image(frame=None, layer=None)
```

封装层还提供场景注册表，方便脚本按 id 找回模型：

```python
register_scene(model_or_scene)
create_scene(layer_count=2, frame_count=2)
scene
get_scene(id=None, name=None, Name=None)
scenes()
get_scene()
get_current()
```

`get_scene()` 返回当前用户界面注册过的 scene 对象列表。现在主程序只有一个主界面 scene，但返回列表是为了以后支持多个 UI scene。

每个 scene 对象都包装了原始字典数据，支持 `to_dict()` 和 `[]`，也支持 Python 风格属性：

```python
item.scene       # 底层 SceneModel
item.sceneName   # 字符串名称
item.sceneId     # 整数 id
item.to_dict()
```

其中：

- `scene` 是底层 `animean_python.SceneModel` 引用，可以直接调用 `scene.add_polyline()`、`scene.cell_to_dict()` 等方法。
- `sceneName` 是字符串名称。
- `sceneId` 是整数 id。

`get_current()` 返回当前用户界面的 scene 对象，额外带当前选择：

```python
current = get_current()
current.current_frame
current.current_layer
```

- `scene` 是当前 UI 正在使用的底层 `animean_python.SceneModel` 引用。
- `current_frame` 是当前帧索引，0 基；如果没有当前帧则为 `None`。
- `current_layer` 是当前图层索引，0 基；如果没有当前图层则为 `None`。
- 如果当前 Python 环境不是 AnimeAn 应用内嵌环境，或者没有可用 UI scene，则返回 `None`。

示例：

```python
from animemodel import get_scene, get_current

print(get_scene())

current = get_current()
if current is not None:
    scene = current.scene
    frame = current.current_frame
    layer = current.current_layer
    if frame is not None and layer is not None:
        scene.add_polyline(frame, layer, [(0, 0), (120, 80)], r=255, g=0, b=0)
```

链式访问示例：

```python
from animemodel import scene, get_scene

main = get_scene(id=1001)
matches = get_scene(name="main")
all_scenes = scene
main.setname("Shot 010")

first_stroke = main.get_frame(id=0).get_layer(id=0).get_stroke(num=1)
stroke_num = first_stroke.num
stroke_locations = first_stroke.location()
stroke_layers = first_stroke.layerid
stroke_layer_names = first_stroke.layerName
stroke_frames = first_stroke.frame
bezier_lines = first_stroke.line_list(ploy=True)
poly_lines = first_stroke.line_list(ploy=False, simplify=1.5)
all_strokes = main.get_frame(id=0).get_layer(name="Line").stroke
cell = main.get_frame(id=0).get_layer(id=0).cell(to_poly=True)
fill = main.get_frame(id=0).get_layer(id=0).fillarea(1)
all_fills = main.get_frame(id=0).get_layer(id=0).fillarea()
raster = main.get_frame(id=0).get_layer(id=0).raster()
all_frames = main.frame
all_layers = main.get_frame(id=0).layer
top_layer = main.get_frame(id=0).layer[-1]
main.get_frame(id=0).get_layer(id=0).setname("Line")
main.asset(id=0).setname("Character Line")
main_location = main.location()
asset_locations = main.asset(id=0).location()
layer_locations = main.get_frame(id=0).get_layer(id=0).location()
```

`scene(id=1001)` 返回一个精确 scene。`scene(Name="main")` 返回所有名字匹配的 scene。
`scene()` 返回全部 scene。

`frame(id=0)`、`layer(id=0)` 和 `asset(id=0)` 使用 0 基 id。`stroke(1)` 和 `fillarea(1)` 仍然使用用户可见序号；如果要用 0 基索引，可以写：

```python
stroke = get_scene(id=1001).get_frame(index=0).get_layer(index=0).get_stroke(index=0)
```

`layer(id=0, Name="Line")` 会同时按 0 基图层 id 和用户界面里的图层名选择 layer。
`frame()` 返回全部 frame，`frame(id=0)` 返回单个 frame。
`id` 和 `Name` 都允许传入空；`layer()` 会返回当前帧的全部 layer。
`stroke.line_list(ploy=True)` 返回贝塞尔/path 命令。
`stroke.line_list(ploy=False, simplify=1.5)` 返回由 C++ 采样并经过 RDP 简化的折线点列。
`fillarea()` 返回全部填充区域，`fillarea(1)` 返回第一个填充区域。
`scene.setname()`、`layer.setname()` 和 `asset.setname()` 会重命名选中的对象，并返回自身以便继续链式调用。
`location()` 返回对象在软件数据结构中的坐标。单条路径固定为
`[scene, frame|None, layer|None, asset|None]`。scene 返回一条路径，例如
`[0, None, None, None]`；layer、asset 和 stroke 返回路径列表，例如
`[[0, 0, 1, 3], ...]`。这些 id 都是 0 基；缺失位置使用 `None`。`.layerid`、
`.layerName`、`.frame`、`.assetid`、`.assetName` 这类便捷列表都从这些位置坐标中得到。

句柄式操作：

```python
model.get_frame(id=0).get_layer(id=0).add_polyline([(0, 0), (100, 100)])

layer = model.get_layer(name="Line")[0]
cell = layer.cell(frame_id=0)
cell.add_stroke([(0, 0), (50, 40)], smooth=True)
cell.clear()
cell.to_dict(to_poly=True)
```

## 底层类

### VectorRange

```python
r = animean_python.VectorRange(0.2, 0.8)
r.first
r.second
r.to_dict()
```

表示一个归一化区间，常用于擦除或保留笔画局部。

### Cell

```python
cell = animean_python.Cell()
cell.asset_index
cell.frame_id
cell.is_empty()
```

表示 xsheet 中某一格引用哪个 asset 和 frame id。

### VectorStroke

```python
stroke = animean_python.VectorStroke()
stroke.id
stroke.width
stroke.total_length
stroke.to_dict(to_poly=False, poly_step=4.0)
stroke.line_list(ploy=False, simplify=0.0)
```

通常通过 `vectorlogic.make_stroke_object()` 创建，再加入 `VectorImage` 或 `SceneModel`。

### VectorImage

```python
image.stroke_count()
image.fill_count()
image.clear()
image.bounds()
image.to_dict(to_poly=False, poly_step=4.0)
image.add_polyline(points, r=0, g=0, b=0, a=255, width=3.0)
image.add_stroke_object(stroke)
```

表示某个 asset/frame 的矢量图像，包含 strokes 和 fills。

### SceneModel

`SceneModel` 是底层场景、图层、帧、asset 和 cell 的主要操作面：

```python
scene.id()
scene.set_id(id)
scene.scene_name()
scene.set_scene_name(name)
scene.scene_id()
scene.set_scene_id(scene_id)
scene.initialize_scene(layer_count, frame_count)
scene.set_current_layer(layer_index)
scene.set_current_frame(frame_index)
scene.set_current_asset(asset_index)
scene.current_layer()
scene.current_frame()
scene.current_asset()
scene.layer_count()
scene.frame_count()
scene.asset_count()
scene.get_structure()

scene.layer_name(layer_index)
scene.set_layer_name(layer_index, name)
scene.frame_name(frame_index)
scene.asset_name(asset_index)
scene.set_asset_name(asset_index, name)
scene.layer_visible(layer_index)
scene.set_layer_visible(layer_index, visible)
scene.layer_locked(layer_index)
scene.set_layer_locked(layer_index, locked)
scene.layer_opacity(layer_index)
scene.set_layer_opacity(layer_index, opacity)

scene.add_layer()
scene.add_asset(type="vector", name="")
scene.delete_layer(layer_index)
scene.move_layer(from_index, to_index)
scene.add_frame()
scene.delete_frame(frame_index)
scene.move_frame(from_index, to_index)

scene.cell_at(row, layer_index)
scene.set_cell(row, layer_index, cell)
scene.clear_cell(row, layer_index)
scene.image_at(row, layer_index, create=False)
scene.current_image(create=False)
scene.asset_image(asset_index, frame_id=1, create=False)
scene.cell_asset_index(row, layer_index)
scene.stroke_count(row, layer_index)
scene.clear_image(row, layer_index)
scene.cell_to_dict(layer_index, frame_index, to_poly=False, poly_step=4.0)
scene.cell_strokes(layer_index, frame_index, to_poly=False, poly_step=4.0)
scene.add_polyline(row, layer_index, points, r=0, g=0, b=0, a=255, width=3.0)
scene.add_stroke_object(row, layer_index, stroke)
```

`add_asset(type=...)` 支持 `vector`、`raster`、`fill`。当前绑定里无法识别的类型会退回为 `vector`。

## model_pybind 功能

所有函数都会先走 C++ 转换逻辑，再返回标准 Python 值：

```python
qreal(value) -> float
point(value) -> {"x": float, "y": float}
point_i(value) -> {"x": int, "y": int}
rect(value) -> {"x": float, "y": float, "width": float, "height": float}
rect_i(value) -> {"x": int, "y": int, "width": int, "height": int}
color(value) -> {"r": int, "g": int, "b": int, "a": int}
line(value) -> {"from": PointDict, "to": PointDict, "length": float}
points(values) -> list[PointDict]
lines(values) -> list[LineDict]
range(value) -> {"first": float, "second": float}
ranges(values) -> list[RangeDict]
path(value, to_poly=False, poly_step=4.0) -> PathDict
```

转换失败时会抛出 `TypeError`，错误信息会指出是哪个字段无法转换。

## vectorlogic 功能

`animean_python.vectorlogic` 暴露了主要矢量算法：

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

常见用途：

- 平滑手绘点列：`make_smoothed_path()`。
- 创建可加入场景的笔画对象：`make_stroke_object()`。
- 判断橡皮擦或选择圆是否碰到笔画：`stroke_hits_circle()`。
- 计算笔画被保留或删除的区间：`keep_ranges_for_circle()`、`complement_ranges()`、`sub_stroke()`。
- 从路径提取线段并计算闭合区域：`segments_from_path()`、`compute_vector_region_faces()`。
- 用种子点找封闭区域：`vector_region_path_at()`。
- 从灰度边界 mask 做洪泛填充：`fill_path_from_mask()`。

## 路径和 polyline 输出

返回路径的函数通常带 `to_poly` 和 `poly_step`：

- `to_poly=False`：返回 Qt 风格路径命令，`geometry_type` 为 `path`。
- `to_poly=True`：额外返回采样后的 `polylines`，`geometry_type` 为 `polyline`。
- `poly_step` 控制曲线采样间距，值越小点越密，值越大点越稀。

示例：

```python
path = animean_python.vectorlogic.make_smoothed_path(
    [(0, 0), (50, 80), (100, 0)],
    to_poly=True,
    poly_step=2.0,
)
print(path["polylines"])
```

## 区域填充说明

`compute_vector_region_faces(segments)` 会根据线段找出所有闭合面，每个结果额外带有 `signed_area`。它不需要种子点，也不会替调用方选择唯一面。

`vector_region_path_at(seed, segments, canvas_rect)` 会在闭合面里找包含 `seed` 的区域；找不到时返回空路径 dict。

`fill_path_from_mask(seed, boundary)` 使用灰度边界图做洪泛填充。`0` 表示可填充区域，非 `0` 表示边界或阻挡。`boundary` 可以是二维灰度数组，也可以是：

```python
{
    "width": 3,
    "height": 3,
    "pixels": [
        [255, 255, 255],
        [255,   0, 255],
        [255, 255, 255],
    ],
}
```

## 返回数据结构

点：

```python
{"x": 10.0, "y": 20.0}
```

线段：

```python
{"from": {"x": 0.0, "y": 0.0}, "to": {"x": 10.0, "y": 0.0}, "length": 10.0}
```

路径：

```python
{
    "geometry_type": "path",
    "bounds": {"x": 0.0, "y": 0.0, "width": 100.0, "height": 80.0},
    "commands": [...],
}
```

笔画：

```python
{
    "id": 0,
    "width": 3.0,
    "color": {"r": 0, "g": 0, "b": 0, "a": 255},
    "bounds": {...},
    "raw_points": [...],
    "lengths": [...],
    "total_length": 128.4,
    "geometry_type": "path",
    "commands": [...],
}
```

图像：

```python
{
    "empty": False,
    "stroke_count": 1,
    "fill_count": 0,
    "has_raster": False,
    "bounds": {...},
    "raster": {"empty": True},
    "strokes": [...],
    "fills": [...],
}
```

填充区域：

```python
{
    "id": 0,
    "seed": {"x": 120.0, "y": 80.0},
    "color": {"r": 255, "g": 0, "b": 0, "a": 255},
    "bounds": {...},
    "source_layer_index": 0,
    "based_on_all_layers": False,
    "commands": [...],
}
```

栅格信息：

```python
{
    "empty": False,
    "bounds": {...},
    "top_left": {"x": 0.0, "y": 0.0},
    "width": 1920,
    "height": 1080,
}
```

场景结构：

```python
{
    "sceneName": "...",
    "sceneId": 1,
    "scene_id": "...",
    "current_frame": 0,
    "current_layer": 0,
    "current_asset": 0,
    "frame_count": 24,
    "layer_count": 2,
    "asset_count": 2,
    "frames": [{"index": 0, "num": 1, "name": "Frame 1"}],
    "layers": [
        {
            "index": 0,
            "num": 1,
            "column_name": "...",
            "name": "...",
            "visible": True,
            "locked": False,
            "opacity": 1.0,
            "type": "vector",
            "cells": [...],
        }
    ],
    "assets": [{"index": 0, "num": 1, "name": "...", "type": "vector"}],
}
```

## 适合脚本调用的组合

新增一条平滑笔画：

```python
from animemodel import create_scene

model = create_scene(2, 24)
stroke = model.add_stroke([(0, 0), (40, 90), (120, 30)], color=(20, 20, 20, 255), width=4)
print(stroke.total_length)
```

查找指定图层并写入第一帧：

```python
for layer in model.get_layer(name="Line", frame_id=0):
    layer.cell().add_polyline([(10, 10), (80, 80)], color=(255, 0, 0, 255))
```

提取闭合区域：

```python
path = model.strokes(to_poly=False)[0]["commands"]
segments = model.vectorlogic.segments_from_path(path)
faces = model.vectorlogic.compute_vector_region_faces(segments, to_poly=True)
```

## 当前边界

- 文档只描述 Python 绑定功能，不包含构建命令。
- Python 侧目前主要暴露矢量图像和场景结构；栅格图像设置、fill layer 的更多编辑入口存在于 C++ 模型中，但没有完整绑定成 Python API。
- `VectorStroke` 的点列、颜色和路径主要通过 `make_stroke_object()` 或 `add_polyline()` 构造；直接在 Python 修改复杂内部字段不是主要用法。
