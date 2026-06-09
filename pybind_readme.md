# AnimeAn Python / pybind Design

This document describes how Python code talks to the AnimeAn C++ data model and
`AnimeVectorLogic` helpers through `pybind11`.

## Module Layout

- `animean_python`
  - C++ extension / embedded module produced from `pythonbind/python_bindings.cpp`.
  - Owns the low-level bindings for `SceneModel`, `VectorImage`, `VectorStroke`,
    `Cell`, `VectorRange`, `model_pybind`, and `vectorlogic`.
- `pythonbind/animemodel.py`
  - Python convenience layer.
  - Provides `AnimeModel`, `annimemodel`, `animemodel`, and `model_pybind`.
- `animean_python.model_pybind`
  - Fast value conversion facade for Python input.
  - Accepts Python primitives, tuple/list values, and dict values, then converts
    them to Qt/C++ types internally.
- `animean_python.vectorlogic`
  - Full Python-facing wrapper around `AnimeVectorLogic`.
  - Uses `model_pybind` conversion rules for function arguments.
- `animean_python.get_scene()` / `animean_python.get_current()`
  - Runtime UI helpers available when Python is embedded in the AnimeAn app.
  - Standalone extension-module usage has no UI registry, so these return an
    empty scene list and `None`.

## Interaction Flow

1. Python scripts import either the high-level wrapper or the C++ module:

   ```python
   from animemodel import AnimeModel
   import animean_python
   ```

2. Python passes values in friendly forms:

   ```python
   point = (12.0, 24.0)
   rect = {"x": 0, "y": 0, "width": 1920, "height": 1080}
   color = (255, 80, 20, 255)
   ```

3. `model_pybind` converts those values to C++ Qt types:

   - `float/int` -> `qreal`
   - `(x, y)` or `{"x": x, "y": y}` -> `QPointF`
   - `(x, y, width, height)` or `{"x","y","width","height"}` -> `QRectF`
   - `(r, g, b[, a])` or `{"r","g","b","a"}` -> `QColor`
   - `((x1, y1), (x2, y2))`, `(x1, y1, x2, y2)`, or `{"from": p0, "to": p1}` -> `QLineF`

4. C++ functions operate on native AnimeAn model/vector types.

5. Results are returned as Python objects:

   - Simple Qt values return dicts.
   - Paths return dicts with `bounds`, `commands`, and optional `polylines`.
   - Strokes return `VectorStroke` objects or serializable dicts.

## Python Wrapper Class

`pythonbind/animemodel.py` defines:

```python
from animemodel import AnimeModel

model = AnimeModel()
model.initialize(layer_count=2, frame_count=24)
model.set_current(frame=0, layer=0)
model.add_polyline([(0, 0), (100, 80)], color=(0, 0, 0, 255), width=3.0)
cell_data = model.cell(to_poly=True)
```

Aliases:

- `AnimeModel`: preferred class name.
- `annimemodel`: compatibility alias requested for scripts.
- `animemodel`: lowercase alias.

Important members:

- `model.scene`: underlying `animean_python.SceneModel`.
- `model.model_pybind`: `ModelPybind` conversion helper.
- `model.vectorlogic`: `animean_python.vectorlogic` submodule.

High-level helpers:

```python
AnimeModel(scene: animean_python.SceneModel | None = None)
AnimeModel.from_scene(scene)

model.id() -> str
model.scene_name() -> str
model.set_scene_name(name: str) -> AnimeModel
model.scene_id() -> int
model.set_scene_id(scene_id: int) -> AnimeModel
model.initialize(layer_count: int = 2, frame_count: int = 2) -> AnimeModel
model.set_current(frame: int | None = None, layer: int | None = None) -> AnimeModel
model.get_structure() -> dict
model.frame -> list[FrameHandle]
model.get_frame(id=None, index=None, name=None, Name=None) -> FrameHandle | list[FrameHandle]
model.layer -> list[LayerMatch]
model.get_layer(id=None, name=None, Name=None, asset_name=None, frame_id=None) -> list[LayerMatch]
model.cell_image(frame=None, layer=None, create=True) -> VectorImage
model.image(frame=None, layer=None, create=True) -> VectorImage
model.asset_image(asset_index: int, frame_id: int = 1, create=False) -> VectorImage
model.add_polyline(points, frame=None, layer=None, color=(0,0,0,255), width=3.0) -> AnimeModel
model.add_stroke(points, frame=None, layer=None, color=(0,0,0,255), width=3.0, smooth=True, smooth_value=50) -> VectorStroke
model.cell(frame=None, layer=None, to_poly=False) -> dict
model.strokes(frame=None, layer=None, to_poly=False) -> list[dict]
model.clear_image(frame=None, layer=None) -> AnimeModel
```

Scene registry helpers:

```python
register_scene(model_or_scene) -> AnimeModel
create_scene(layer_count: int = 2, frame_count: int = 2) -> AnimeModel
scene -> list[SceneRef]
get_scene(id=None, name=None, Name=None) -> SceneRef | list[SceneRef]
scenes() -> dict[str, AnimeModel]
get_current() -> CurrentRef | None
```

`get_scene()` returns Python object wrappers for scenes registered by the
running user interface. The current application has one main UI scene, but the
API is a list so additional UI scenes can be represented later.

Each scene wrapper exposes the original dictionary data through `to_dict()` and
`[]`, plus Python-style attributes:

```python
item.scene       # live SceneModel
item.sceneName   # string name
item.sceneId     # integer id
item.to_dict()
```

`get_current()` returns the current scene wrapper, with extra selection
properties:

```python
current = get_current()
current.current_frame
current.current_layer
```

`scene` is the live `animean_python.SceneModel` reference, so scripts can operate
on it directly:

```python
from animemodel import get_current

current = get_current()
if current is not None:
    scene = current.scene
    frame = current.current_frame
    layer = current.current_layer
    if frame is not None and layer is not None:
        scene.add_polyline(frame, layer, [(0, 0), (120, 80)], r=255, g=0, b=0)
```

If no UI scene is available, `get_current()` returns `None`. If the UI currently
has no selected frame or layer, that position in the returned list is `None`.

Scene lookup and chained access:

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

`get_scene(id=1001)` returns one exact scene. `get_scene(name="main")` returns
every scene whose UI name matches. `scene` lazily returns all scenes.

`get_frame(id=0)`, `get_layer(id=0)`, and `asset(id=0)` use 0-based ids.
`get_stroke(num=1)` and `fillarea(1)` use the user-visible ordinal argument;
use `index=0` for a raw 0-based stroke or fill index:

```python
stroke = get_scene(id=1001).get_frame(index=0).get_layer(index=0).get_stroke(index=0)
```

`frame` returns all frames. `get_frame(id=0)` returns one frame.
`get_layer(id=0, name="Line")` selects by both 0-based layer id and UI layer
name. `id` and `name` may each be `None`; `layer` returns all layers at the
selected frame.
`stroke.line_list(ploy=True)` returns Bezier/path commands.
`stroke.line_list(ploy=False, simplify=1.5)` returns C++ sampled polylines after
RDP simplification.
`fillarea()` returns all fill areas; `fillarea(1)` returns the first fill area.
`scene.setname()`, `layer.setname()`, and `asset.setname()` rename the selected
object and return the same wrapper for chaining.
`location()` returns software data-structure coordinates. A single path is always
`[scene, frame|None, layer|None, asset|None]`. Scene returns one path, such as
`[0, None, None, None]`; layer, asset, and stroke return path lists, such as
`[[0, 0, 1, 3], ...]`. All ids in these paths are 0-based, and missing positions
use `None`. Convenience lists such as `.layerid`, `.layerName`, `.frame`,
`.assetid`, and `.assetName` are derived from these locations.

Handle helpers:

```python
FrameHandle.layer -> list[LayerMatch]
FrameHandle.get_layer(id=None, name=None, Name=None, asset_name=None, frame_id=None)
LayerMatch.cell(frame_id: int | None = None) -> CellHandle
LayerMatch.add_polyline(points, frame_id=None, color=(0,0,0,255), width=3.0) -> CellHandle
CellHandle.add_polyline(points, color=(0,0,0,255), width=3.0) -> CellHandle
CellHandle.add_stroke(points, color=(0,0,0,255), width=3.0, smooth=True, smooth_value=50) -> VectorStroke
CellHandle.clear() -> CellHandle
CellHandle.to_dict(to_poly=False) -> dict
CellHandle.asset_index -> int
```

## model_pybind API

All helpers return normalized Python dict/list values after C++ conversion.

```python
def qreal(value: int | float) -> float: ...
def point(value: PointLike) -> PointDict: ...
def point_i(value: PointLike) -> PointIntDict: ...
def rect(value: RectLike) -> RectDict: ...
def rect_i(value: RectLike) -> RectIntDict: ...
def color(value: ColorLike) -> ColorDict: ...
def line(value: LineLike) -> LineDict: ...
def points(values: list[PointLike]) -> list[PointDict]: ...
def lines(values: list[LineLike]) -> list[LineDict]: ...
def range(value: RangeLike) -> RangeDict: ...
def ranges(values: list[RangeLike]) -> list[RangeDict]: ...
def path(value: PathLike, to_poly: bool = False, poly_step: float = 4.0) -> PathDict: ...
```

Accepted value forms:

- `PointLike`: `(x, y)` or `{"x": x, "y": y}`.
- `RectLike`: `(x, y, width, height)`, `{"x","y","width","height"}`, or
  `{"left","top","right","bottom"}`.
- `ColorLike`: `(r, g, b)`, `(r, g, b, a)`, or `{"r","g","b","a"}`.
- `LineLike`: `(p0, p1)`, `(x1, y1, x2, y2)`, `{"from": p0, "to": p1}`, or
  `{"p1": p0, "p2": p1}`.
- `RangeLike`: `(first, second)` or `{"first": first, "second": second}`.
- `PathLike`: a point list or command list.

Path command format:

```python
[
    {"type": "move", "to": (0, 0)},
    {"type": "line", "to": (100, 0)},
    {"type": "quad", "control": (120, 40), "to": (100, 80)},
    {"type": "cubic", "control1": (60, 90), "control2": (20, 90), "to": (0, 80)},
    {"type": "close"},
]
```

`PathLike` also accepts a rectangle command:

```python
{"type": "rect", "rect": {"x": 0, "y": 0, "width": 100, "height": 80}}
```

When a path command omits `type`, it is treated as a `line` command.

## Low-Level Bound Classes

These classes are available directly from `animean_python`.

```python
VectorRange()
VectorRange(first: float, second: float)
range.first
range.second
range.to_dict() -> {"first": float, "second": float}

Cell()
cell.asset_index
cell.frame_id
cell.is_empty() -> bool

VectorStroke()
stroke.id
stroke.width
stroke.total_length
stroke.to_dict(to_poly=False, poly_step=4.0) -> dict
stroke.line_list(ploy=False, simplify=0.0) -> list

VectorImage
image.stroke_count() -> int
image.fill_count() -> int
image.clear() -> None
image.bounds() -> tuple[float, float, float, float]
image.to_dict(to_poly=False, poly_step=4.0) -> dict
image.add_polyline(points, r=0, g=0, b=0, a=255, width=3.0) -> None
image.add_stroke_object(stroke: VectorStroke) -> None

SceneModel()
```

`SceneModel` exposes the editable scene/xsheet surface:

```python
scene.id() -> str
scene.set_id(id: str) -> None
scene.scene_name() -> str
scene.set_scene_name(name: str) -> None
scene.scene_id() -> int
scene.set_scene_id(scene_id: int) -> None
scene.initialize_scene(layer_count: int, frame_count: int) -> None
scene.set_current_layer(layer_index: int) -> None
scene.set_current_frame(frame_index: int) -> None
scene.set_current_asset(asset_index: int) -> None
scene.current_layer() -> int
scene.current_frame() -> int
scene.current_asset() -> int
scene.layer_count() -> int
scene.frame_count() -> int
scene.asset_count() -> int
scene.get_structure() -> dict
scene.layer_name(layer_index: int) -> str
scene.set_layer_name(layer_index: int, name: str) -> None
scene.frame_name(frame_index: int) -> str
scene.asset_name(asset_index: int) -> str
scene.set_asset_name(asset_index: int, name: str) -> None
scene.layer_visible(layer_index: int) -> bool
scene.set_layer_visible(layer_index: int, visible: bool) -> None
scene.layer_locked(layer_index: int) -> bool
scene.set_layer_locked(layer_index: int, locked: bool) -> None
scene.layer_opacity(layer_index: int) -> float
scene.set_layer_opacity(layer_index: int, opacity: float) -> None
scene.add_layer() -> int
scene.add_asset(type: str = "vector", name: str = "") -> int
scene.delete_layer(layer_index: int) -> bool
scene.move_layer(from_index: int, to_index: int) -> bool
scene.add_frame() -> int
scene.delete_frame(frame_index: int) -> bool
scene.move_frame(from_index: int, to_index: int) -> bool
scene.cell_at(row: int, layer_index: int) -> Cell
scene.set_cell(row: int, layer_index: int, cell: Cell) -> None
scene.clear_cell(row: int, layer_index: int) -> None
scene.image_at(row: int, layer_index: int, create=False) -> VectorImage
scene.current_image(create=False) -> VectorImage
scene.asset_image(asset_index: int, frame_id: int = 1, create=False) -> VectorImage
scene.cell_asset_index(row: int, layer_index: int) -> int
scene.stroke_count(row: int, layer_index: int) -> int
scene.clear_image(row: int, layer_index: int) -> None
scene.cell_to_dict(layer_index: int, frame_index: int, to_poly=False, poly_step=4.0) -> dict
scene.cell_strokes(layer_index: int, frame_index: int, to_poly=False, poly_step=4.0) -> dict
scene.add_polyline(row, layer_index, points, r=0, g=0, b=0, a=255, width=3.0) -> None
scene.add_stroke_object(row: int, layer_index: int, stroke: VectorStroke) -> None
```

Layer, frame, and asset ids in the high-level wrapper are 0-based where the
method name says `id` or `frame_id`; low-level `SceneModel` row/layer indexes
are also 0-based.

## vectorlogic API

`animean_python.vectorlogic` binds the C++ `AnimeVectorLogic` namespace.

```python
def epsilon() -> float: ...
def filtered_points(points: list[PointLike]) -> list[PointDict]: ...
def make_smoothed_path(
    points: list[PointLike],
    smooth_value: int = 50,
    to_poly: bool = False,
    poly_step: float = 4.0,
) -> PathDict: ...
def make_polyline_path(
    points: list[PointLike],
    to_poly: bool = False,
    poly_step: float = 4.0,
) -> PathDict: ...
def make_stroke(
    points: list[PointLike],
    color: ColorLike = (0, 0, 0, 255),
    width: float = 3.0,
    id: int = 0,
    filter_input: bool = True,
    smooth_path: bool = True,
    smooth_value: int = 50,
    to_poly: bool = False,
    poly_step: float = 4.0,
) -> StrokeDict: ...
def make_stroke_object(
    points: list[PointLike],
    color: ColorLike = (0, 0, 0, 255),
    width: float = 3.0,
    id: int = 0,
    filter_input: bool = True,
    smooth_path: bool = True,
    smooth_value: int = 50,
) -> VectorStroke: ...
def stroke_hits_circle(stroke: VectorStroke, center: PointLike, radius: float) -> bool: ...
def stroke_hits_circle(
    points: list[PointLike],
    center: PointLike,
    radius: float,
    width: float = 3.0,
) -> bool: ...
def stroke_hits_capsule(
    stroke: VectorStroke,
    from_point: PointLike,
    to_point: PointLike,
    radius: float,
) -> bool: ...
def stroke_hits_capsule(
    points: list[PointLike],
    from_point: PointLike,
    to_point: PointLike,
    radius: float,
    width: float = 3.0,
) -> bool: ...
def keep_ranges_for_circle(stroke: VectorStroke, center: PointLike, radius: float) -> list[RangeDict]: ...
def keep_ranges_for_capsule(
    stroke: VectorStroke,
    from_point: PointLike,
    to_point: PointLike,
    radius: float,
) -> list[RangeDict]: ...
def complement_ranges(ranges: list[RangeLike]) -> list[RangeDict]: ...
def sub_stroke(
    stroke: VectorStroke,
    from_w: float,
    to_w: float,
    smooth_value: int = 50,
    to_poly: bool = False,
    poly_step: float = 4.0,
) -> StrokeDict: ...
def point_at_length(stroke: VectorStroke, length: float) -> PointDict: ...
def segments_from_path(path_like: PathLike) -> list[LineDict]: ...
def compute_vector_region_faces(
    segments: list[LineLike],
    to_poly: bool = False,
    poly_step: float = 4.0,
) -> list[PathDict]: ...
def vector_region_path_at(
    seed: PointLike,
    segments: list[LineLike],
    canvas_rect: RectLike,
    to_poly: bool = False,
    poly_step: float = 4.0,
) -> PathDict: ...
def fill_path_from_mask(
    seed: PointLike,
    boundary: MaskLike,
    to_poly: bool = False,
    poly_step: float = 4.0,
) -> PathDict: ...
```

### Path-to-polyline output

Functions that return paths often accept `to_poly` and `poly_step`.

- `to_poly=False` returns path commands such as `move`, `line`, `quad`, `cubic`,
  and `close`.
- `to_poly=True` adds sampled `polylines` to the returned dict. This is useful
  when Python code wants point lists instead of Qt-style path commands.
- `poly_step` controls the approximate sampling distance, in canvas units, used
  when converting curves to polylines. Smaller values produce more points and a
  closer approximation; larger values produce fewer points and a rougher
  approximation.

For example, `poly_step=1.0` samples curves densely, while `poly_step=12.0`
samples them sparsely. This does not change the C++ path or fill region itself;
it only changes the density of the `polylines` data returned to Python.

### Vector region fill helpers

`compute_vector_region_faces` computes every closed vector face that can be formed
from the input line segments. It does not need a seed point and does not choose a
single region. Each returned item is a path dict with an extra `signed_area`
field:

```python
faces = animean_python.vectorlogic.compute_vector_region_faces(segments)
for face in faces:
    print(face["bounds"], face["signed_area"])
```

Use this when you want to auto-detect closed regions, for example after the user
draws a closed circle or polygon. If multiple faces are returned, choose one in
Python using your own rule, such as largest area, smallest area, closest bounds
to the new stroke, or a before/after comparison of newly created faces.

`vector_region_path_at` uses the same face computation internally, then returns
only the closed face that contains `seed`. If no face contains the seed, it
returns an empty path dict.

```python
path = animean_python.vectorlogic.vector_region_path_at(
    seed=(120, 80),
    segments=segments,
    canvas_rect={"x": 0, "y": 0, "width": 800, "height": 600},
)
```

`fill_path_from_mask` is a raster-mask flood fill helper. It does not inspect
stroke objects or return the strokes that formed the boundary. The caller must
provide a grayscale boundary mask where `0` means fillable empty space and any
non-zero value means boundary/blocked pixel.

`fill_path_from_mask` accepts either:

```python
pixels = [
    [255, 255, 255],
    [255,   0, 255],
    [255, 255, 255],
]
```

or:

```python
boundary = {"width": 3, "height": 3, "pixels": pixels}
```

## Data Return Shapes

Point:

```python
{"x": 10.0, "y": 20.0}
```

Line:

```python
{"from": {"x": 0.0, "y": 0.0}, "to": {"x": 10.0, "y": 0.0}, "length": 10.0}
```

Path:

```python
{
    "geometry_type": "path",
    "bounds": {"x": 0.0, "y": 0.0, "width": 100.0, "height": 80.0},
    "commands": [...],
}
```

Stroke dict:

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

Image dict:

```python
{
    "empty": False,
    "stroke_count": 1,
    "fill_count": 0,
    "has_raster": False,
    "bounds": {...},
    "raster": {"empty": True},
    "strokes": [StrokeDict],
    "fills": [FillDict],
}
```

Fill dict:

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

Raster dict:

```python
{
    "empty": False,
    "bounds": {...},
    "top_left": {"x": 0.0, "y": 0.0},
    "width": 1920,
    "height": 1080,
}
```

Structure dict:

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

## Notes

- Build commands are intentionally not part of this document.
- The conversion helpers raise `TypeError` when Python input cannot be converted.
- Asset/layer type strings are `vector`, `raster`, and `fill`; unrecognized
  values fall back to `vector` in the current binding.
