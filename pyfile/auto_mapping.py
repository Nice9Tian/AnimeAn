"""Auto-mapping tools: H/V center lines, mapping area, and child->main mapping.

Mapping assets (the H/V center lines and the mapping area) do NOT live in the
layer stack. They are captured into a Python dict (_MAPPING_ASSETS) the moment
they are drawn, removed from the scene model, and displayed through the
generic C++ overlay service (animean_python.ui.set_overlay). They never show
up in the Layers/Assets panels and are not part of the saved project.

Workflow:
1. In child_paint_view draw one "H Center Line" (blue) and one "V Center Line"
   (green). They define the pattern's UV frame. Redrawing replaces the old one.
2. Draw the pattern in child_paint_view with the normal tools.
3. In main_paint_view draw one H and one V center line to place the frame.
4. Optional: with "Mapping Area" click inside a closed shape (bucket-style
   region detection, computed here in Python via vectorlogic). An area in
   main_paint_view clips the mapped result; an area in child_paint_view
   selects which part of the pattern is used. Light blue, one per view.
5. Click "Auto Mapping": child pattern strokes are converted to UV through
   the child guides and re-created in main_paint_view through the main
   guides (arc-length evaluation, so curved guides bend the pattern).
   Re-running replaces the previous mapping. Each item shows an "x" badge on
   the canvas - click it to delete the item and redraw.

Architecture note: C++ only provides generic services (overlay display list,
"overlayremove" hook event, set_draw_color, geometry bindings). All tool
semantics - property names, colors, the asset dict, region detection,
clipping - live in this file.
"""

import bisect
import math

import python_hooks

H_PROPERTY = "h_center_line"
V_PROPERTY = "v_center_line"
GUIDE_PROPERTIES = (H_PROPERTY, V_PROPERTY)
MAPPED_PROPERTY = "auto_mapped"
AUTO_MAPPING_TOOL = "auto_mapping"
MAPPING_AREA_PROPERTY = "mapping_area"
POLY_STEP = 4.0

H_COLOR = (40, 110, 255, 255)
V_COLOR = (0, 170, 80, 255)
AREA_BORDER_COLOR = (120, 185, 250, 190)
AREA_FILL_COLOR = (150, 205, 255, 60)

ITEM_LABELS = {
    H_PROPERTY: "H center line",
    V_PROPERTY: "V center line",
    MAPPING_AREA_PROPERTY: "mapping area",
}

# view name -> {property -> item}; guide item: {"points": [...], "width": w},
# area item: {"polygons": [[(x, y), ...], ...]}
_MAPPING_ASSETS = {}

_last_run_handled = False


# ---------------------------------------------------------------------------
# scene access helpers
# ---------------------------------------------------------------------------

def _animean():
    import animean_python

    return animean_python


def _scene_model(view_name):
    import __main__

    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model

    wanted = f"{view_name}_paint_view"
    for info in _animean().get_scene():
        if info.get("sceneName") == wanted:
            return info["scene"]
    raise RuntimeError(f"scene for view '{view_name}' is not registered")


def _canvas_rect(view_name):
    import __main__

    width = getattr(__main__, f"{view_name}_canvas_width", None) or getattr(__main__, "canvas_width", 0)
    height = getattr(__main__, f"{view_name}_canvas_height", None) or getattr(__main__, "canvas_height", 0)
    if not width or not height:
        width, height = 4096, 4096
    return (0, 0, float(width), float(height))


def _assets_for(view_name):
    return _MAPPING_ASSETS.setdefault(view_name, {})


# ---------------------------------------------------------------------------
# geometry helpers (pure python, unit-testable without the embedded runtime)
# ---------------------------------------------------------------------------

def _stroke_points(stroke):
    points = []
    for polyline in stroke.get("polylines") or []:
        for point in polyline:
            points.append((float(point["x"]), float(point["y"])))
    if not points:
        for point in stroke.get("raw_points") or []:
            points.append((float(point["x"]), float(point["y"])))
    return points


def _stroke_segments(stroke):
    """Per-polyline segment list (no bogus joins between separate polylines)."""
    polylines = stroke.get("polylines") or []
    if not polylines and stroke.get("raw_points"):
        polylines = [stroke["raw_points"]]
    segments = []
    for polyline in polylines:
        for index in range(1, len(polyline)):
            a = polyline[index - 1]
            b = polyline[index]
            segments.append(((float(a["x"]), float(a["y"])),
                             (float(b["x"]), float(b["y"]))))
    return segments


def _cumulative_lengths(points):
    lengths = [0.0]
    for index in range(1, len(points)):
        lengths.append(lengths[-1] + math.hypot(points[index][0] - points[index - 1][0],
                                                points[index][1] - points[index - 1][1]))
    return lengths


def _segment_direction(points, index):
    dx = points[index + 1][0] - points[index][0]
    dy = points[index + 1][1] - points[index][1]
    length = math.hypot(dx, dy)
    if length <= 1e-12:
        return 1.0, 0.0
    return dx / length, dy / length


def _point_at_arc(points, cumulative, arc):
    total = cumulative[-1]
    if len(points) < 2 or total <= 0.0:
        return points[0]
    if arc <= 0.0:
        dx, dy = _segment_direction(points, 0)
        return (points[0][0] + dx * arc, points[0][1] + dy * arc)
    if arc >= total:
        dx, dy = _segment_direction(points, len(points) - 2)
        extra = arc - total
        return (points[-1][0] + dx * extra, points[-1][1] + dy * extra)

    index = bisect.bisect_right(cumulative, arc) - 1
    index = max(0, min(index, len(points) - 2))
    segment = cumulative[index + 1] - cumulative[index]
    t = 0.0 if segment <= 0.0 else (arc - cumulative[index]) / segment
    return (points[index][0] + (points[index + 1][0] - points[index][0]) * t,
            points[index][1] + (points[index + 1][1] - points[index][1]) * t)


def _segment_intersection(a1, a2, b1, b2):
    d1x = a2[0] - a1[0]
    d1y = a2[1] - a1[1]
    d2x = b2[0] - b1[0]
    d2y = b2[1] - b1[1]
    denom = d1x * d2y - d1y * d2x
    if abs(denom) < 1e-12:
        return None
    ox = b1[0] - a1[0]
    oy = b1[1] - a1[1]
    t = (ox * d2y - oy * d2x) / denom
    u = (ox * d1y - oy * d1x) / denom
    return t, u


def _polyline_intersection(a, b):
    """Return (point, arc_on_a, arc_on_b) where the polylines cross.

    Falls back to the closest vertex pair when the lines do not intersect.
    """
    a_cum = _cumulative_lengths(a)
    b_cum = _cumulative_lengths(b)
    eps = 1e-6
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = _segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -eps <= t <= 1.0 + eps and -eps <= u <= 1.0 + eps:
                point = (a[i][0] + (a[i + 1][0] - a[i][0]) * t,
                         a[i][1] + (a[i + 1][1] - a[i][1]) * t)
                arc_a = a_cum[i] + (a_cum[i + 1] - a_cum[i]) * min(max(t, 0.0), 1.0)
                arc_b = b_cum[j] + (b_cum[j + 1] - b_cum[j]) * min(max(u, 0.0), 1.0)
                return point, arc_a, arc_b

    best = None
    for i, pa in enumerate(a):
        for j, pb in enumerate(b):
            distance = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
            if best is None or distance < best[0]:
                best = (distance, i, j)
    _, i, j = best
    point = ((a[i][0] + b[j][0]) * 0.5, (a[i][1] + b[j][1]) * 0.5)
    return point, a_cum[i], b_cum[j]


def build_mapper(child_h_points, child_v_points, main_h_points, main_v_points):
    """Build point mapper from child UV frame to main frame.

    Returns (map_point, width_scale) or (None, reason).
    The child guides act as chord axes: a stroke point is decomposed into
    (u, v) relative to the guide intersection, where u = 1 means "half of the
    H guide length away from the crossing". The main guides are evaluated by
    arc length, so curved main guides bend the mapped pattern.
    """
    if min(len(child_h_points), len(child_v_points), len(main_h_points), len(main_v_points)) < 2:
        return None, "a center line has fewer than 2 points"

    child_origin, _, _ = _polyline_intersection(child_h_points, child_v_points)
    eh = ((child_h_points[-1][0] - child_h_points[0][0]) * 0.5,
          (child_h_points[-1][1] - child_h_points[0][1]) * 0.5)
    ev = ((child_v_points[-1][0] - child_v_points[0][0]) * 0.5,
          (child_v_points[-1][1] - child_v_points[0][1]) * 0.5)
    det = eh[0] * ev[1] - eh[1] * ev[0]
    if abs(det) < 1e-9:
        return None, "child center lines are parallel or degenerate"

    main_h_cum = _cumulative_lengths(main_h_points)
    main_v_cum = _cumulative_lengths(main_v_points)
    if main_h_cum[-1] <= 1e-9 or main_v_cum[-1] <= 1e-9:
        return None, "main center lines are degenerate"
    main_origin, main_h_arc, main_v_arc = _polyline_intersection(main_h_points, main_v_points)

    def map_point(point):
        dx = point[0] - child_origin[0]
        dy = point[1] - child_origin[1]
        u = (dx * ev[1] - dy * ev[0]) / det
        v = (eh[0] * dy - eh[1] * dx) / det
        on_h = _point_at_arc(main_h_points, main_h_cum, main_h_arc + u * 0.5 * main_h_cum[-1])
        on_v = _point_at_arc(main_v_points, main_v_cum, main_v_arc + v * 0.5 * main_v_cum[-1])
        return (on_h[0] + on_v[0] - main_origin[0],
                on_h[1] + on_v[1] - main_origin[1])

    child_h_len = max(2.0 * math.hypot(eh[0], eh[1]), 1e-6)
    child_v_len = max(2.0 * math.hypot(ev[0], ev[1]), 1e-6)
    width_scale = math.sqrt((main_h_cum[-1] / child_h_len) * (main_v_cum[-1] / child_v_len))
    return map_point, width_scale


# ---------------------------------------------------------------------------
# mapping area geometry (region polygons -> polyline clipping)
# ---------------------------------------------------------------------------

def _command_point(value):
    return (float(value["x"]), float(value["y"]))


def _path_commands_to_polygons(commands):
    """Sample path commands (move/line/cubic) into closed polygon point lists."""
    polygons = []
    current = []

    def flush():
        if len(current) >= 3:
            polygons.append(list(current))

    for command in commands or []:
        kind = command.get("type")
        if kind == "move":
            flush()
            current = [_command_point(command["to"])]
        elif kind == "line":
            if not current and "from" in command:
                current.append(_command_point(command["from"]))
            current.append(_command_point(command["to"]))
        elif kind == "cubic":
            if not current and "from" in command:
                current.append(_command_point(command["from"]))
            if not current:
                continue
            p0 = current[-1]
            c1 = _command_point(command["control1"])
            c2 = _command_point(command["control2"])
            p3 = _command_point(command["to"])
            net = (math.hypot(c1[0] - p0[0], c1[1] - p0[1])
                   + math.hypot(c2[0] - c1[0], c2[1] - c1[1])
                   + math.hypot(p3[0] - c2[0], p3[1] - c2[1]))
            samples = max(4, min(24, int(math.ceil(net / 6.0))))
            for step in range(1, samples + 1):
                t = step / samples
                omt = 1.0 - t
                current.append((
                    omt * omt * omt * p0[0] + 3 * omt * omt * t * c1[0] + 3 * omt * t * t * c2[0] + t * t * t * p3[0],
                    omt * omt * omt * p0[1] + 3 * omt * omt * t * c1[1] + 3 * omt * t * t * c2[1] + t * t * t * p3[1],
                ))
    flush()
    return polygons


def _point_in_polygons(point, polygons):
    """Even-odd containment test over all polygons (each implicitly closed)."""
    inside = False
    px, py = point
    for polygon in polygons:
        j = len(polygon) - 1
        for i in range(len(polygon)):
            xi, yi = polygon[i]
            xj, yj = polygon[j]
            if (yi > py) != (yj > py):
                cross = xj + (py - yj) * (xi - xj) / (yi - yj)
                if px < cross:
                    inside = not inside
            j = i
    return inside


def _boundary_point(inside_point, outside_point, polygons):
    """Bisect between an inside and an outside point to approximate the border."""
    a = inside_point
    b = outside_point
    for _ in range(14):
        mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
        if _point_in_polygons(mid, polygons):
            a = mid
        else:
            b = mid
    return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)


def _clip_polyline(points, polygons):
    """Split a polyline into the pieces that lie inside the polygons."""
    if not polygons:
        return [points]
    pieces = []
    current = []
    previous = None
    previous_inside = False
    for point in points:
        inside = _point_in_polygons(point, polygons)
        if previous is None:
            if inside:
                current.append(point)
        elif inside and previous_inside:
            current.append(point)
        elif inside and not previous_inside:
            current = [_boundary_point(point, previous, polygons), point]
        elif not inside and previous_inside:
            current.append(_boundary_point(previous, point, polygons))
            if len(current) >= 2:
                pieces.append(current)
            current = []
        previous = point
        previous_inside = inside
    if len(current) >= 2:
        pieces.append(current)
    return pieces


# ---------------------------------------------------------------------------
# mapping asset dict + overlay display
# ---------------------------------------------------------------------------

def _push_overlay(view_name):
    """Send this view's mapping assets to the generic C++ overlay display."""
    assets = _assets_for(view_name)
    items = []
    for prop, color in ((H_PROPERTY, H_COLOR), (V_PROPERTY, V_COLOR)):
        guide = assets.get(prop)
        if guide and len(guide.get("points") or []) >= 2:
            items.append({
                "id": prop,
                "points": guide["points"],
                "color": color,
                "width": float(guide.get("width", 3.0)),
                "removable": True,
            })
    area = assets.get(MAPPING_AREA_PROPERTY)
    if area:
        for polygon in area.get("polygons") or []:
            items.append({
                "id": MAPPING_AREA_PROPERTY,
                "points": polygon,
                "closed": True,
                "color": AREA_BORDER_COLOR,
                "fill_color": AREA_FILL_COLOR,
                "width": 1.5,
                "removable": True,
            })
    try:
        _animean().ui.set_overlay(view_name, items)
    except Exception as error:
        print(f"[auto_mapping] overlay update failed: {error}")


def _set_draw_color(color):
    try:
        _animean().ui.set_draw_color(color)
    except Exception:
        pass


def _detect_region(scene, view_name, frame, seed):
    """Bucket-style region detection, computed in Python via vectorlogic."""
    segments = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return []
    for layer in structure["layers"]:
        if not layer["visible"] or layer["type"] == "fill":
            continue
        cell = scene.cell_to_dict(layer["index"], frame, True, POLY_STEP)
        for stroke in cell["image"]["strokes"]:
            segments.extend(_stroke_segments(stroke))
    if not segments:
        return []
    path = _animean().vectorlogic.vector_region_path_at(seed, segments, _canvas_rect(view_name))
    return _path_commands_to_polygons(path.get("commands"))


def _absorb_legacy_items(view_name, scene, frame):
    """Migrate guide strokes / area fills that older builds stored in layers."""
    assets = _assets_for(view_name)
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return
    changed = False
    for layer in structure["layers"]:
        layer_index = layer["index"]
        cell = scene.cell_to_dict(layer_index, frame, True, POLY_STEP)
        strokes = cell["image"]["strokes"]
        for index in range(len(strokes) - 1, -1, -1):
            prop = strokes[index].get("property") or ""
            if prop in GUIDE_PROPERTIES:
                if prop not in assets:
                    assets[prop] = {
                        "points": _stroke_points(strokes[index]),
                        "width": float(strokes[index].get("width", 3.0)),
                    }
                scene.remove_stroke(frame, layer_index, index)
                changed = True
        fills = cell["image"]["fills"]
        for index in range(len(fills) - 1, -1, -1):
            if (fills[index].get("property") or "") == MAPPING_AREA_PROPERTY:
                if MAPPING_AREA_PROPERTY not in assets:
                    polygons = _path_commands_to_polygons(fills[index].get("commands"))
                    if polygons:
                        assets[MAPPING_AREA_PROPERTY] = {"polygons": polygons}
                scene.remove_fill_area(frame, layer_index, index)
                changed = True
    if changed:
        print(f"[auto_mapping] moved legacy mapping items out of {view_name} layers")
        _push_overlay(view_name)


# ---------------------------------------------------------------------------
# scene scanning + mapping
# ---------------------------------------------------------------------------

def _collect_pattern_strokes(scene, frame):
    pattern = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return pattern
    skip = (*GUIDE_PROPERTIES, MAPPED_PROPERTY, AUTO_MAPPING_TOOL, MAPPING_AREA_PROPERTY)
    for layer in structure["layers"]:
        if not layer["visible"] or layer["type"] == "fill":
            continue
        cell = scene.cell_to_dict(layer["index"], frame, True, POLY_STEP)
        for stroke in cell["image"]["strokes"]:
            if (stroke.get("property") or "") in skip:
                continue
            pattern.append(stroke)
    return pattern


def _remove_previous_mapping(image):
    strokes = image.to_dict(False, POLY_STEP)["strokes"]
    removed = 0
    for index in range(len(strokes) - 1, -1, -1):
        if (strokes[index].get("property") or "") == MAPPED_PROPERTY:
            image.remove_stroke(index)
            removed += 1
    return removed


def _perform_mapping():
    animean = _animean()
    child = _scene_model("child")
    main = _scene_model("main")

    child_frame = max(child.current_frame(), 0)
    main_frame = max(main.current_frame(), 0)

    _absorb_legacy_items("child", child, child_frame)
    _absorb_legacy_items("main", main, main_frame)

    child_assets = _assets_for("child")
    main_assets = _assets_for("main")

    ok = True
    for view_label, assets in (("child_paint_view", child_assets), ("main_paint_view", main_assets)):
        missing = [ITEM_LABELS[prop] for prop in GUIDE_PROPERTIES if prop not in assets]
        if missing:
            print(f"[auto_mapping] {view_label} is missing: {', '.join(missing)}")
            ok = False
    if not ok:
        print("[auto_mapping] draw the guides with the 'H Center Line' / 'V Center Line' tools first.")
        return False

    child_pattern = _collect_pattern_strokes(child, child_frame)
    if not child_pattern:
        print("[auto_mapping] child_paint_view has no pattern strokes to map.")
        return False

    map_point, width_scale = build_mapper(
        child_assets[H_PROPERTY]["points"],
        child_assets[V_PROPERTY]["points"],
        main_assets[H_PROPERTY]["points"],
        main_assets[V_PROPERTY]["points"],
    )
    if map_point is None:
        print(f"[auto_mapping] cannot build mapping: {width_scale}")
        return False

    child_area = (child_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")
    main_area = (main_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")

    image = main.current_image(True)
    if image is None:
        print("[auto_mapping] main_paint_view has no editable cell to draw into.")
        return False

    replaced = _remove_previous_mapping(image)
    added = 0
    clipped_out = 0
    for stroke in child_pattern:
        source_pieces = _clip_polyline(_stroke_points(stroke), child_area)
        output_pieces = []
        for piece in source_pieces:
            mapped = [map_point(point) for point in piece]
            output_pieces.extend(_clip_polyline(mapped, main_area))
        if not output_pieces:
            clipped_out += 1
            continue
        color = stroke.get("color") or {}
        width = max(0.5, float(stroke.get("width", 3.0)) * width_scale)
        for points in output_pieces:
            if len(points) < 2:
                continue
            stroke_object = animean.vectorlogic.make_stroke_object(
                points,
                (int(color.get("r", 0)), int(color.get("g", 0)), int(color.get("b", 0)), int(color.get("a", 255))),
                width,
                image.stroke_count() + 1,
                False,
                False,
            )
            stroke_object.property = MAPPED_PROPERTY
            image.add_stroke_object(stroke_object)
            added += 1

    animean.ui.refresh()
    summary = f"[auto_mapping] mapped {added} stroke(s) into main_paint_view (width x{width_scale:.2f})"
    if replaced:
        summary += f", replaced {replaced} previous"
    if child_area:
        summary += ", source limited by child mapping area"
    if main_area:
        summary += ", output clipped by main mapping area"
    if clipped_out:
        summary += f", {clipped_out} stroke(s) fell fully outside"
    print(summary)
    return True


def _run():
    try:
        _perform_mapping()
    except Exception as error:  # keep the UI alive; feedback goes to the debug dock
        print(f"[auto_mapping] error: {error!r}")


# ---------------------------------------------------------------------------
# hooks + tool handlers
# ---------------------------------------------------------------------------

def _capture_mapping_item(cell, stroke, message):
    """Move a freshly drawn guide/area click out of the model into the dict."""
    prop = message.get("property")
    if prop not in (H_PROPERTY, V_PROPERTY, MAPPING_AREA_PROPERTY):
        return
    view = message.get("view") or "main"
    row = cell.get("row")
    layer = cell.get("layer")
    index = stroke.get("index")
    if row is None or layer is None or index is None or row < 0 or layer < 0 or index < 0:
        return
    try:
        scene = _scene_model(view)
    except Exception as error:
        print(f"[auto_mapping] capture skipped: {error}")
        return

    strokes = scene.cell_to_dict(layer, row, True, POLY_STEP)["image"]["strokes"]
    if index >= len(strokes):
        return
    points = _stroke_points(strokes[index])
    width = float(strokes[index].get("width", 3.0))
    scene.remove_stroke(row, layer, index)

    assets = _assets_for(view)
    if prop == MAPPING_AREA_PROPERTY:
        if not points:
            _animean().ui.widget.refresh()
            return
        polygons = _detect_region(scene, view, row, points[0])
        if not polygons:
            print(f"[auto_mapping] no closed region around the click in {view} view; "
                  "draw a closed shape first.")
            _animean().ui.widget.refresh()
            return
        assets[MAPPING_AREA_PROPERTY] = {"polygons": polygons}
        print(f"[auto_mapping] mapping area set in {view} view (click 'x' to remove)")
    else:
        if len(points) < 2:
            print("[auto_mapping] center line too short; draw a longer line.")
            _animean().ui.widget.refresh()
            return
        assets[prop] = {"points": points, "width": width}
        print(f"[auto_mapping] {ITEM_LABELS[prop]} set in {view} view (redraw replaces it)")

    _push_overlay(view)
    _animean().ui.widget.refresh()


def _overlay_removed(cell, stroke, message):
    overlay = message.get("overlay") or {}
    item_id = overlay.get("id")
    if not item_id:
        return
    view = message.get("view") or "main"
    assets = _assets_for(view)
    if item_id in assets:
        del assets[item_id]
        print(f"[auto_mapping] removed {ITEM_LABELS.get(item_id, item_id)} in {view} view")
        _push_overlay(view)


def _auto_mapping_button(cell, stroke, message):
    global _last_run_handled
    _last_run_handled = True
    _run()


def register_hooks():
    python_hooks.set_hook(_capture_mapping_item, linefinish=True, tool="extra")
    python_hooks.set_hook(_overlay_removed, overlayremove=True)
    python_hooks.set_hook(_auto_mapping_button, extra=True, tool=AUTO_MAPPING_TOOL)


def activate_center_line_tool(name="h_center_line", property_value=H_PROPERTY):
    register_hooks()
    _set_draw_color(H_COLOR if property_value == H_PROPERTY else V_COLOR)
    axis = "horizontal" if property_value == H_PROPERTY else "vertical"
    print(f"[auto_mapping] {name} active: draw ONE {axis} center line (property={property_value}).")
    return property_value


def activate_mapping_area_tool(name="mapping_area", property_value=MAPPING_AREA_PROPERTY):
    register_hooks()
    _set_draw_color(AREA_BORDER_COLOR)
    print("[auto_mapping] mapping area active: click inside a closed shape.")
    print("[auto_mapping] area in main view clips the mapped output; area in child view selects the source.")
    return property_value


def run_auto_mapping(name=AUTO_MAPPING_TOOL, property_value=AUTO_MAPPING_TOOL):
    global _last_run_handled
    register_hooks()
    if _last_run_handled:
        # the "extra" event hook already performed this click's mapping
        _last_run_handled = False
        return property_value
    _run()
    return property_value
