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

- `qreal(value) -> float`
  - Accepts `int` or `float`.
- `point(value) -> {"x": float, "y": float}`
  - Accepts `(x, y)` or `{"x": x, "y": y}`.
- `point_i(value) -> {"x": int, "y": int}`
  - Same as `point`, rounded to `QPoint`.
- `rect(value) -> {"x": float, "y": float, "width": float, "height": float}`
  - Accepts `(x, y, width, height)`, `{"x","y","width","height"}`, or
    `{"left","top","right","bottom"}`.
- `rect_i(value) -> {"x": int, "y": int, "width": int, "height": int}`
  - Same as `rect`, converted to `QRect`.
- `color(value) -> {"r": int, "g": int, "b": int, "a": int}`
  - Accepts `(r, g, b)`, `(r, g, b, a)`, or `{"r","g","b","a"}`.
- `line(value) -> {"from": point, "to": point, "length": float}`
  - Accepts `(p0, p1)`, `(x1, y1, x2, y2)`, `{"from": p0, "to": p1}`, or
    `{"p1": p0, "p2": p1}`.
- `points(values) -> list[point]`
  - Accepts a sequence of point-like values.
- `lines(values) -> list[line]`
  - Accepts a sequence of line-like values.
- `range(value) -> {"first": float, "second": float}`
  - Accepts `(first, second)` or `{"first": first, "second": second}`.
- `ranges(values) -> list[range]`
  - Accepts a sequence of range-like values.
- `path(value, to_poly=False, poly_step=4.0) -> dict`
  - Accepts a point list or command list.

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

- `epsilon() -> float`
- `filtered_points(points) -> list[point]`
- `make_smoothed_path(points, smooth_value=50, to_poly=False, poly_step=4.0) -> dict`
- `make_polyline_path(points, to_poly=False, poly_step=4.0) -> dict`
- `make_stroke(points, color=(0,0,0,255), width=3.0, id=0, filter_input=True, smooth_path=True, smooth_value=50, to_poly=False, poly_step=4.0) -> dict`
- `make_stroke_object(points, color=(0,0,0,255), width=3.0, id=0, filter_input=True, smooth_path=True, smooth_value=50) -> VectorStroke`
- `stroke_hits_circle(stroke, center, radius) -> bool`
- `stroke_hits_circle(points, center, radius, width=3.0) -> bool`
- `stroke_hits_capsule(stroke, from_point, to_point, radius) -> bool`
- `stroke_hits_capsule(points, from_point, to_point, radius, width=3.0) -> bool`
- `keep_ranges_for_circle(stroke, center, radius) -> list[range]`
- `keep_ranges_for_capsule(stroke, from_point, to_point, radius) -> list[range]`
- `complement_ranges(ranges) -> list[range]`
- `sub_stroke(stroke, from_w, to_w, smooth_value=50, to_poly=False, poly_step=4.0) -> dict`
- `point_at_length(stroke, length) -> point`
- `segments_from_path(path_like) -> list[line]`
- `compute_vector_region_faces(segments, to_poly=False, poly_step=4.0) -> list[path]`
- `vector_region_path_at(seed, segments, canvas_rect, to_poly=False, poly_step=4.0) -> path`
- `fill_path_from_mask(seed, boundary, to_poly=False, poly_step=4.0) -> path`

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
- `to_poly=True` is useful when Python code needs sampled geometry instead of
  path commands.
- `poly_step` controls curve sampling distance for path-to-polyline output.
