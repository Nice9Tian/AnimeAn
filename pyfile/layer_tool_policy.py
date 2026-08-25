"""What the CURRENT LAYER means for the tools: pages, locks, and the pen's
second meaning on a fill layer.

Selecting a layer is the user saying what they are working on, so the tools
follow it without being asked:

- a layer inside an automapping unit  -> the Tools window shows its Mapping
  page (the unit is edited with the mapping tools, not with the pen),
- an ordinary vector layer            -> the Painting page, nothing locked,
- a fill layer that TRACKS a line layer (G4 parenting) -> pen, eraser, fill,
  connect and transfer are locked out: its content is derived from the
  parent's topology, and hand-editing it would be overwritten by the next
  re-trace. Arrow stays, so the layer can still be inspected,
- an INDEPENDENT fill layer           -> connect is locked (there are no
  endpoints to join) and the pen becomes a REGION BRUSH: the stroke is drawn,
  then converted into a fill of every closed region it crossed and removed.
  The eraser rubs those regions out instead (natively, in C++).

Mechanism / policy split: C++ owns two generic switches - ui.set_locked_tools
(refuse a tool, dim its chip, fall back to Arrow) and ui.set_fill_paint_mode
(stop refusing the pen/eraser GESTURES on a Fill column, and let the eraser
remove fill regions under the brush). Neither of them knows what a layer is.
This module is the half that reads layers and decides.

Every layerchange re-evaluates and pushes the WHOLE verdict, locks included:
the previous layer's answer is never the new layer's answer, and "nothing is
locked here" has to be said out loud or a stale lock would survive the move.
"""

import math

import python_hooks
import window_manager

# The main board only. The texture board carries the shared pattern, not
# units or tracked fills, and its focus changes must not re-page the tools.
VIEW = "main"

UNIT_TAG = "automapping"

# A tracked fill child is topology-derived artwork: every tool that would
# write into it by hand is locked, because the next parent edit re-derives it.
CHILD_FILL_LOCKS = ("pen", "eraser", "fill", "connect", "transfer")
# An independent fill layer keeps the pen and eraser (remapped below) and the
# fill tool; connect has no open endpoints to join on a layer with no strokes.
INDEPENDENT_FILL_LOCKS = ("connect",)

# Document px between the samples taken along a pen stroke. Coarser skips a
# thin region the line passed through; finer only costs repeated traces that
# dedup away.
SAMPLE_STEP = 6.0

# The verdict per layer kind. Page None leaves the Tools window where it is.
POLICY = {
    "automapping": {"page": "mapping", "locks": (), "fill_paint": False},
    "vector": {"page": "painting", "locks": (), "fill_paint": False},
    "fill_child": {"page": "painting", "locks": CHILD_FILL_LOCKS, "fill_paint": False},
    "fill": {"page": "painting", "locks": INDEPENDENT_FILL_LOCKS, "fill_paint": True},
    "none": {"page": None, "locks": (), "fill_paint": False},
}

# The mode last pushed, so a caller can ask what the board is in without
# going back to C++. Not authority: the linefinish conversion re-reads the
# layer, because an undo can move the board without the mode following.
_STATE = {"fill_paint": False}

# The conversion writes fill regions and refreshes; a refresh that came back
# through another dispatch would re-enter it while the first pass still has a
# stroke to remove. Module level like fill_tool's retrace guard, because the
# recursion it stops is across hook dispatches, not within one call.
_CONVERT_GUARD = {"depth": 0}


def _animean():
    import animean_python
    return animean_python


def _scene_model(view_name):
    """Resolve a view's SceneModel (the same lookup fill_tool uses)."""
    import __main__

    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model

    wanted = f"{view_name}_paint_view"
    for info in _animean().get_scene():
        if info.get("sceneName") == wanted:
            return info["scene"]
    raise RuntimeError(f"scene for view '{view_name}' is not registered")


# --- reading the layer -------------------------------------------------------

def _layer_kind(scene, structure, index):
    """One of "automapping", "fill_child", "fill", "vector", "none".

    Unit membership is asked FIRST: a unit's member layers are ordinary
    vector and fill columns, and their kind on their own would send the tools
    to the painting page in the middle of a mapping edit.
    """
    if not isinstance(index, int) or index < 0:
        return "none"
    try:
        if scene.group_id_for_layer(index, UNIT_TAG):
            return "automapping"
    except AttributeError:
        pass  # older build without group tags: fall through to the type

    info = None
    for layer in structure.get("layers") or []:
        if layer.get("index") == index:
            info = layer
            break
    if info is None:
        return "none"
    if (info.get("type") or "") != "fill":
        return "vector"
    return "fill_child" if int(info.get("parent_layer_id") or 0) else "fill"


# --- pushing the verdict -----------------------------------------------------

def _set_locked_tools(view, names):
    try:
        _animean().ui.set_locked_tools(view, list(names))
    except AttributeError:
        pass  # build without the binding: the locks are simply not enforced
    except Exception as error:
        print(f"[layer policy] set_locked_tools failed: {error}")


def _set_fill_paint_mode(view, on):
    try:
        _animean().ui.set_fill_paint_mode(view, bool(on))
    except AttributeError:
        pass  # build without the binding: the pen keeps refusing fill layers
    except Exception as error:
        print(f"[layer policy] set_fill_paint_mode failed: {error}")


def _tools_window_visible():
    """False only when the shell says the Tools window is turned OFF.

    Anything unreadable counts as visible: refusing to switch pages because
    the question could not be answered would disable the whole policy on a
    build whose window list is missing, which is the louder failure.
    """
    try:
        for entry in window_manager.layout():
            if entry.get("name") == "tools":
                return bool(entry.get("visible", True))
    except Exception:
        return True
    return True


def _apply(view, kind):
    policy = POLICY.get(kind) or POLICY["none"]
    _set_locked_tools(view, policy["locks"])
    _set_fill_paint_mode(view, policy["fill_paint"])
    _STATE["fill_paint"] = bool(policy["fill_paint"])
    page = policy["page"]
    # Only on a real layerchange, and never into a window the user closed: a
    # page switch is the tools answering a selection, not an opinion about
    # what should be on screen.
    if page and _tools_window_visible():
        window_manager.select("tools", page)


def _layer_changed(message):
    if (message.get("view") or "") != VIEW:
        return
    try:
        scene = _scene_model(VIEW)
        kind = _layer_kind(scene, scene.get_structure(), message.get("layer"))
    except Exception:
        import traceback

        print(f"[layer policy] layer change failed:\n{traceback.format_exc()}")
        return
    _apply(VIEW, kind)


# --- the pen as a region brush ----------------------------------------------

def _sample_points(points, step):
    """Points every `step` of ARC LENGTH along the polyline, ends kept.

    Interpolated inside a segment rather than picked from the captured
    points: a straight-line stroke is two points from corner to corner, and
    sampling only what was captured would fill the two regions its ends
    happen to sit in and skip everything it drew through.
    """
    if not points:
        return []
    step = max(float(step), 1.0)
    result = [points[0]]
    previous = points[0]
    carried = 0.0        # distance since the last sample
    for point in points[1:]:
        segment = math.dist(previous, point)
        if segment <= 0.0:
            continue
        travelled = step - carried
        while travelled <= segment:
            ratio = travelled / segment
            result.append((previous[0] + (point[0] - previous[0]) * ratio,
                           previous[1] + (point[1] - previous[1]) * ratio))
            travelled += step
        carried = segment - (travelled - step)
        previous = point
    if result[-1] != previous:
        result.append(previous)
    return result


def _region_containing(image, point):
    """Index of the topmost stored region covering the point, or None."""
    infos = image.fill_regions_info()
    for info in reversed(infos):
        if image.fill_region_contains(info["index"], point):
            return info["index"]
    return None


def _fill_regions_along(scene, frame, layer, points, color, property_value):
    """Fill every distinct closed region the stroke crossed. Returns how many
    regions were added or recolored."""
    samples = _sample_points(points, SAMPLE_STEP)
    if not samples:
        return 0
    # Scope ALL, always: a fill layer contributes no walls of its own, so
    # "current layer" would find nothing to bound a region with. Same upgrade
    # a fill click makes (pyfile/fill_tool.py).
    bounds_info = scene.fill_boundary_bounds(frame, -1)
    if not bounds_info:
        return 0  # no walls on this frame: nothing could be bounded
    bounds = (float(bounds_info["x"]), float(bounds_info["y"]),
              float(bounds_info["width"]), float(bounds_info["height"]))
    image = scene.image_at(frame, layer, True, "fill")
    if image is None:
        return 0

    touched = set()
    changed = 0
    for point in samples:
        existing = _region_containing(image, point)
        if existing is not None and existing in touched:
            continue  # already inside a region this stroke has done
        path = scene.fill_boundary_path_at(frame, point, bounds, -1)
        if path is None:
            continue  # the stroke crossed open paper here, not a shape
        commands = path["commands"]
        if existing is None:
            touched.add(image.add_fill_region(
                commands, color, property=property_value, seed=point,
                source_layer_index=-1, based_on_all_layers=True))
        else:
            # Brushing over a region that is already there recolors and
            # re-traces it instead of stacking a second copy underneath.
            image.set_fill_region(existing, path=commands, color=color, seed=point)
            touched.add(existing)
        changed += 1
    return changed


def _convert_stroke(scene, view, row, layer, index, message):
    strokes = scene.cell_to_dict(layer, row)["image"]["strokes"]
    if index >= len(strokes):
        return
    drawn = strokes[index]
    points = [(float(p["x"]), float(p["y"])) for p in (drawn.get("raw_points") or [])]
    # The stroke's own colour IS the current pen colour, and it is the one the
    # user watched being drawn - reading the pen back separately could pick up
    # a change made between the release and this handler.
    color = drawn.get("color") or {"r": 0, "g": 0, "b": 0, "a": 255}
    property_value = message.get("property") or ""

    filled = _fill_regions_along(scene, row, layer, points, color, property_value)

    # The stroke goes whatever happened above. A line on a fill layer is the
    # GESTURE, not artwork: the layer renders no walls of its own, so one left
    # behind would be ink nothing can ever erase with the eraser (which on
    # this layer removes regions).
    scene.remove_stroke(row, layer, index)
    message["cancel_history"] = True
    animean = _animean()
    animean.ui.refresh()
    if not filled:
        # Not an error: drawing across open paper is a legitimate miss.
        print("[layer policy] the stroke crossed no closed region.")
        return
    animean.ui.history_commit("Region Fill", view)


def _stroke_finished(cell, stroke, message):
    """linefinish on an independent fill layer: the stroke becomes fills.

    The stroke is already committed to the image when this runs (C++ adds it,
    then dispatches), which is exactly what makes the conversion possible -
    the fitted geometry is readable, and cancel_history keeps the round trip
    out of the undo stack.
    """
    if _CONVERT_GUARD["depth"]:
        return
    if (message.get("view") or "") != VIEW:
        return
    row = cell.get("row")
    layer = cell.get("layer")
    index = stroke.get("index")
    if not all(isinstance(value, int) and value >= 0 for value in (row, layer, index)):
        return

    _CONVERT_GUARD["depth"] += 1
    try:
        scene = _scene_model(VIEW)
        # The KIND decides, not the pushed mode: an undo can move the board
        # to another layer without the mode following, and converting a real
        # line stroke into fills would destroy the drawing.
        if _layer_kind(scene, scene.get_structure(), layer) != "fill":
            return
        _convert_stroke(scene, VIEW, row, layer, index, message)
    except Exception:
        import traceback

        print(f"[layer policy] region-fill conversion failed:\n{traceback.format_exc()}")
    finally:
        _CONVERT_GUARD["depth"] -= 1


def register_hooks():
    python_hooks.set_hook(_layer_changed, layerchange=True)
    python_hooks.set_hook(_stroke_finished, linefinish=True)


register_hooks()
