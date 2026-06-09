from __future__ import annotations

import math

import animean_python
from animemodel import current


def _check(name: str, condition: bool) -> None:
    if not condition:
        raise AssertionError(name)
    print(f"[OK] {name}")


def main() -> str:
    print("===== AnimeAn Python bind_test.py =====")

    scenes = animean_python.get_scene()
    _check("animean_python.get_scene() returned at least one UI scene", len(scenes) >= 1)

    _check("animemodel.current is available", bool(current))

    scene = current.raw_scene
    print(f"Scene name={scene.scene_name()}, id={scene.scene_id()}")

    structure = scene.get_structure()
    if structure["frame_count"] <= 0:
        frame = scene.add_frame()
        print(f"Created frame={frame + 1}")
    else:
        frame_ref = current.frame
        if frame_ref is None:
            frame = 0
        else:
            frame = frame_ref.id
        if frame < 0 or frame >= structure["frame_count"]:
            frame = 0
        scene.set_current_frame(frame)

    if structure["layer_count"] <= 0:
        layer = scene.add_layer()
        print(f"Created layer={layer + 1}")
    else:
        layer_ref = current.layer
        if layer_ref is None:
            layer = 0
        else:
            layer = layer_ref.id
        if layer < 0 or layer >= structure["layer_count"]:
            layer = 0
        scene.set_current_layer(layer)

    structure = scene.get_structure()
    _check("scene has frames", structure["frame_count"] > 0)
    _check("scene has layers", structure["layer_count"] > 0)
    _check("test frame is valid", 0 <= frame < structure["frame_count"])
    _check("test layer is valid", 0 <= layer < structure["layer_count"])
    print(f"Using frame={frame + 1}, layer={layer + 1}")

    point = animean_python.model_pybind.point({"x": 12.5, "y": 34.0})
    _check("model_pybind.point converts dict input", math.isclose(point["x"], 12.5) and math.isclose(point["y"], 34.0))

    color = animean_python.model_pybind.color((12, 34, 56, 200))
    _check("model_pybind.color converts tuple input", color == {"r": 12, "g": 34, "b": 56, "a": 200})

    path = animean_python.vectorlogic.make_polyline_path([(0, 0), (40, 20), (80, 0)], to_poly=True)
    _check("vectorlogic.make_polyline_path returns commands", len(path["commands"]) >= 2)
    _check("vectorlogic.make_polyline_path returns polylines", len(path["polylines"]) >= 1)

    before = scene.cell_to_dict(layer, frame, True, 4.0)["image"]["strokes"]
    scene.add_polyline(
        frame,
        layer,
        [(24.0, 24.0), (160.0, 96.0), (240.0, 48.0)],
        0,
        120,
        255,
        255,
        3.0,
    )
    after = scene.cell_to_dict(layer, frame, True, 4.0)["image"]["strokes"]
    _check("scene.add_polyline appends one stroke to current cell", len(after) == len(before) + 1)

    print(f"Strokes before={len(before)}, after={len(after)}")
    print("===== bind_test.py PASS =====")
    return "PASS"


if __name__ == "__main__":
    main()
