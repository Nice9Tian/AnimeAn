# AnimeAn Python / pybind Design

This document describes how Python code talks to the AnimeAn C++ data model and
`AnimeVectorLogic` helpers through `pybind11`.

## Module Layout

- `animean_python`
  - C++ extension / embedded module produced from `python_bindings.cpp`.
  - Owns the low-level bindings for `SceneModel`, `VectorImage`, `VectorStroke`,
    `Cell`, `VectorRange`, `model_pybind`, and `vectorlogic`.
- `animemodel.py`
  - Python convenience layer.
  - Provides `AnimeModel`, `annimemodel`, `animemodel`, and `model_pybind`.
- `animean_python.model_pybind`
  - Fast value conversion facade for Python input.
  - Accepts Python primitives, tuple/list values, and dict values, then converts
    them to Qt/C++ types internally.
- `animean_python.vectorlogic`
  - Full Python-facing wrapper around `AnimeVectorLogic`.
  - Uses `model_pybind` conversion rules for function arguments.

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

`animemodel.py` defines:

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

## Notes

- Build commands are intentionally not part of this document.
- The conversion helpers raise `TypeError` when Python input cannot be converted.
