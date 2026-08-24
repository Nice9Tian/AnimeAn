"""Manual garment crease authoring, independent from Fukusato MLS.

The paper obtains crease lines with its upstream model.  AnimeAn deliberately
keeps prediction out of the mapping algorithm: this tool captures exact user
strokes, persists them in the modeling scene and exposes them to any consumer
that needs a cut garment mesh.
"""

from __future__ import annotations

import math

import fukusato_mapping as core
import overlay_stack
import python_hooks
import script_store

PROPERTY = "fukusato_cut"
TOOL_NAME = "fukusato_cut"
STORE_KEY = "fukusato_creases"
COLOR = (255, 140, 0, 255)
POLY_STEP = 2.0
_OWNER = "fukusato_creases"
_CHANGE_LISTENERS = []


def _animean():
    import animean_python
    return animean_python


def _scene_model():
    return core._scene_model("main")


def _state(scene=None):
    scene = scene or _scene_model()
    data = script_store.read(scene, STORE_KEY, {})
    if not isinstance(data, dict):
        data = {}
    lines = data.get("lines")
    if not isinstance(lines, list):
        lines = []
    return {"next_id": max(1, int(data.get("next_id", 1))), "lines": lines}


def _save(scene, state):
    script_store.write(scene, STORE_KEY, state)


def add_change_listener(function):
    if function not in _CHANGE_LISTENERS:
        _CHANGE_LISTENERS.append(function)


def _notify_changed():
    for function in tuple(_CHANGE_LISTENERS):
        try:
            function()
        except Exception as error:
            print(f"[crease] change listener failed: {error}")


def get_creases(scene=None, frame=None):
    scene = scene or _scene_model()
    if frame is None:
        frame = max(0, scene.current_frame())
    return [[tuple(map(float, p)) for p in line.get("points") or []]
            for line in _state(scene)["lines"]
            if int(line.get("frame", 0)) == frame and len(line.get("points") or []) >= 2]


def overlay_items(scene=None):
    scene = scene or _scene_model()
    frame = max(0, scene.current_frame())
    result = []
    for line in _state(scene)["lines"]:
        if int(line.get("frame", 0)) != frame:
            continue
        points = line.get("points") or []
        if len(points) < 2:
            continue
        result.append({
            "id": f"crease:{int(line['id'])}",
            "points": points,
            "color": COLOR,
            "width": float(line.get("width", 2.5)),
            "removable": True,
        })
    return result


def refresh_overlay():
    try:
        overlay_stack.set_items("main", _OWNER, overlay_items())
    except Exception as error:
        print(f"[crease] overlay update failed: {error}")


def _stroke_points(stroke):
    result = []
    for polyline in stroke.get("polylines") or []:
        points = [(float(p["x"]), float(p["y"])) for p in polyline]
        if len(points) >= 2:
            result.extend(points if not result else points[1:])
    if not result:
        result = [(float(p["x"]), float(p["y"]))
                  for p in stroke.get("raw_points") or []]
    return result


def _capture(cell, stroke, message):
    if message.get("view") != "main" or message.get("property") != PROPERTY:
        if message.get("property") == PROPERTY:
            row, layer, index = cell.get("row"), cell.get("layer"), stroke.get("index")
            if all(value is not None and int(value) >= 0 for value in (row, layer, index)):
                try:
                    wrong_scene = core._scene_model(message.get("view") or "child")
                    wrong_scene.remove_stroke(row, layer, index)
                    _animean().ui.widget.refresh()
                except Exception:
                    pass
            message["cancel_history"] = True
            print("[crease] crease lines belong on MainView.")
        return
    row, layer, index = cell.get("row"), cell.get("layer"), stroke.get("index")
    if any(value is None or int(value) < 0 for value in (row, layer, index)):
        return
    scene = _scene_model()
    strokes = scene.cell_to_dict(layer, row, True, POLY_STEP)["image"]["strokes"]
    if index >= len(strokes):
        return
    points = _stroke_points(strokes[index])
    width = float(strokes[index].get("width", 2.5))
    scene.remove_stroke(row, layer, index)
    if len(points) < 2 or sum(math.dist(a, b) for a, b in zip(points, points[1:])) < 1.0:
        message["cancel_history"] = True
        _animean().ui.widget.refresh()
        return
    state = _state(scene)
    line_id = state["next_id"]
    state["next_id"] += 1
    state["lines"].append({"id": line_id, "frame": int(row),
                           "points": [list(p) for p in points], "width": width})
    _save(scene, state)
    refresh_overlay()
    _notify_changed()
    _animean().ui.widget.refresh()
    print(f"[crease] crease {line_id} stored on frame {row + 1}; it will be inserted "
          "as an exact constrained edge at the next Fukusato solve.")


def _overlay_action(cell, stroke, message):
    overlay = message.get("overlay") or {}
    item_id = str(overlay.get("id") or "")
    action = str(overlay.get("action") or "remove")
    if action != "remove" or not item_id.startswith("crease:"):
        return
    try:
        line_id = int(item_id.split(":", 1)[1])
    except ValueError:
        return
    scene = _scene_model()
    state = _state(scene)
    before = len(state["lines"])
    state["lines"] = [line for line in state["lines"] if int(line.get("id", -1)) != line_id]
    if len(state["lines"]) == before:
        return
    _save(scene, state)
    refresh_overlay()
    _notify_changed()
    try:
        _animean().ui.history_commit("Remove Crease", "main")
    except Exception:
        pass
    print(f"[crease] crease {line_id} removed")


def _history_restored(cell, stroke, message):
    refresh_overlay()
    _notify_changed()


def _frame_changed(cell, stroke, message):
    if message.get("view") == "main":
        refresh_overlay()


def register_hooks():
    python_hooks.set_hook(_capture, linefinish=True, property=PROPERTY)
    python_hooks.set_hook(_overlay_action, overlayremove=True, overlayaction=True)
    python_hooks.set_hook(_history_restored, historyrestore=True)
    python_hooks.set_hook(_frame_changed, framechange=True)


def activate_crease_line(name=TOOL_NAME, property_value=PROPERTY):
    register_hooks()
    try:
        # Through the per-tool colour cache (pyfile/tool_colors.py): setting
        # the colour directly would repaint whatever tool comes next.
        import tool_colors
        tool_colors.apply(COLOR)
    except Exception:
        try:
            _animean().ui.set_draw_color(COLOR)
        except Exception:
            pass
    print("[crease] draw crease lines on MainView. They are stored separately from "
          "the artwork; click x to remove one.")
    return property_value


register_hooks()
