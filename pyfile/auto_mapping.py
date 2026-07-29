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
   The result always goes into its own dedicated layer named "mapped layer"
   - never into whatever layer is currently selected - so it can be hidden,
   reordered or deleted on its own. That layer is reused (not duplicated) on
   every re-run, and the previous result is cleared first. Each mapping asset
   shows an "x" badge on the canvas - click it to delete the item and redraw.

Architecture note: C++ only provides generic services (overlay display list,
"overlayremove" hook event, set_draw_color, geometry bindings). All tool
semantics - property names, colors, the asset dict, region detection,
clipping - live in this file.
"""

import bisect
import json
import math

import python_hooks

H_PROPERTY = "h_center_line"
V_PROPERTY = "v_center_line"
GUIDE_PROPERTIES = (H_PROPERTY, V_PROPERTY)
MAPPED_PROPERTY = "auto_mapped"
MAPPED_LAYER_NAME = "mapped layer"
AUTO_MAPPING_TOOL = "auto_mapping"
AUTO_MAPPING2_TOOL = "auto_mapping_2"
MAPPING_AREA_PROPERTY = "mapping_area"
POLY_STEP = 4.0

GRID_COLOR = (255, 140, 0, 170)

# Refer-rect debug grid state + which mapper variant ran last (drives the grid).
_REFER_RECT = {"enabled": False}
_MAPPER_VERSION = {"value": 1}
# Grid polylines are O(n^2) in guide points to build (intersection searches):
# cache per view, invalidated whenever guides or the mapper version change.
_GRID_CACHE = {"child": None, "main": None}


def refer_rect_enabled():
    return _REFER_RECT["enabled"]


def _invalidate_grid_cache():
    _GRID_CACHE["child"] = None
    _GRID_CACHE["main"] = None

H_COLOR = (0, 0, 255, 255)
V_COLOR = (0, 255, 0, 255)
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
    return (0.0, 0.0, float(width), float(height))


def _assets_for(view_name):
    return _MAPPING_ASSETS.setdefault(view_name, {})


def _save_assets(view_name):
    """Persist this view's mapping assets into the scene's scriptData.

    scriptData travels with every history snapshot and with saved projects, so
    guides/areas become undoable and survive save/load.
    """
    try:
        scene = _scene_model(view_name)
    except Exception:
        return
    scene.set_script_data(json.dumps({"mapping_assets": _assets_for(view_name)}))


def _load_assets(view_name):
    """Rebuild the dict + overlays from the scene's scriptData (post-restore)."""
    try:
        scene = _scene_model(view_name)
    except Exception:
        return
    raw = scene.script_data()
    data = {}
    if raw:
        try:
            data = json.loads(raw).get("mapping_assets") or {}
        except Exception:
            data = {}

    assets = {}
    for prop, item in data.items():
        if prop == MAPPING_AREA_PROPERTY:
            polygons = [[(float(p[0]), float(p[1])) for p in polygon]
                        for polygon in item.get("polygons") or []]
            polygons = [polygon for polygon in polygons if len(polygon) >= 3]
            if polygons:
                assets[prop] = {"polygons": polygons}
        elif prop in GUIDE_PROPERTIES:
            points = [(float(p[0]), float(p[1])) for p in item.get("points") or []]
            if len(points) >= 2:
                assets[prop] = {"points": points, "width": float(item.get("width", 3.0))}
    _MAPPING_ASSETS[view_name] = assets
    _overlays_changed(view_name)


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


def _tangent_at_arc(points, cumulative, arc, window=0.0):
    """Unit tangent of the polyline at arc position (end tangents outside).

    With a positive window the tangent is a central difference over
    [arc-window, arc+window]: hand-drawn guides carry per-segment direction
    jitter and release hooks at the ends, and feeding those raw into the
    spine rotation scrambles the mapped pattern. The window keeps genuine
    curvature while averaging the noise away.
    """
    if len(points) < 2:
        return (1.0, 0.0)
    if window > 0.0:
        before = _point_at_arc(points, cumulative, arc - window)
        after = _point_at_arc(points, cumulative, arc + window)
        dx = after[0] - before[0]
        dy = after[1] - before[1]
        length = math.hypot(dx, dy)
        if length > 1e-9:
            return (dx / length, dy / length)
    if arc <= 0.0:
        return _segment_direction(points, 0)
    if arc >= cumulative[-1]:
        return _segment_direction(points, len(points) - 2)
    index = bisect.bisect_right(cumulative, arc) - 1
    index = max(0, min(index, len(points) - 2))
    return _segment_direction(points, index)


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


def _polylines_cross(a, b):
    """True if the two polylines genuinely intersect."""
    eps = 1e-6
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = _segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -eps <= t <= 1.0 + eps and -eps <= u <= 1.0 + eps:
                return True
    return False


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


def _chord_sides(points, origin, chord_len):
    """Split a guide's chord at the origin: (toward-start, toward-end) lengths.

    Both sides are floored at 1% of the chord so a T-shaped crossing at an
    endpoint cannot divide by (nearly) zero.
    """
    ux = (points[-1][0] - points[0][0]) / chord_len
    uy = (points[-1][1] - points[0][1]) / chord_len
    t = (origin[0] - points[0][0]) * ux + (origin[1] - points[0][1]) * uy
    t = min(max(t, 0.0), chord_len)
    floor = 0.01 * chord_len
    return max(t, floor), max(chord_len - t, floor)


def _arc_sides(total, crossing_arc):
    floor = 0.01 * total
    return max(crossing_arc, floor), max(total - crossing_arc, floor)


def build_mapper(child_h_points, child_v_points, main_h_points, main_v_points, info=None,
                 spine_rotation=True):
    """Build point mapper from child UV frame to main frame.

    Returns (map_point, width_scale) or (None, reason).
    The child guides act as chord axes: a stroke point is decomposed into
    (u, v) relative to the guide intersection. The parametrization is
    ENDPOINT-ANCHORED: the crossing splits every guide into two sides, and
    each side of a child chord maps proportionally onto the SAME side of the
    matching main guide's arc — for straight child guides the endpoints
    correspond exactly to the main guide endpoints wherever the lines cross
    (curved child guides make this approximate: decomposition is chord-based).
    The two sides therefore carry DIFFERENT scales when the crossings sit at
    different relative positions, which folds strokes crossing a guide by a
    bounded angle; keep the crossings at similar relative positions to avoid
    it (info dict, if given, receives h/v_scale_mismatch ratios). Stroke
    width uses one global geometric-mean scale — an approximation once the
    sides differ. The main guides are evaluated by arc length, so curved
    main guides bend the mapped pattern.
    """
    if min(len(child_h_points), len(child_v_points), len(main_h_points), len(main_v_points)) < 2:
        return None, "a center line has fewer than 2 points"

    child_origin, _, _ = _polyline_intersection(child_h_points, child_v_points)
    eh = ((child_h_points[-1][0] - child_h_points[0][0]) * 0.5,
          (child_h_points[-1][1] - child_h_points[0][1]) * 0.5)
    ev = ((child_v_points[-1][0] - child_v_points[0][0]) * 0.5,
          (child_v_points[-1][1] - child_v_points[0][1]) * 0.5)
    det = eh[0] * ev[1] - eh[1] * ev[0]
    # Reject by ANGLE, not absolute area: for long guides an absolute det
    # threshold lets near-parallel axes through and the decomposition blows up.
    axis_sin = abs(det) / max(1e-9, math.hypot(eh[0], eh[1]) * math.hypot(ev[0], ev[1]))
    if axis_sin < 0.05:  # ~3 degrees
        return None, "child center lines are (nearly) parallel"

    main_h_cum = _cumulative_lengths(main_h_points)
    main_v_cum = _cumulative_lengths(main_v_points)
    if main_h_cum[-1] <= 1e-9 or main_v_cum[-1] <= 1e-9:
        return None, "main center lines are degenerate"
    main_origin, main_h_arc, main_v_arc = _polyline_intersection(main_h_points, main_v_points)
    tangent_window = max(2.0 * POLY_STEP, 0.03 * main_h_cum[-1])
    base_tangent = _tangent_at_arc(main_h_points, main_h_cum, main_h_arc, tangent_window)

    child_h_len = max(2.0 * math.hypot(eh[0], eh[1]), 1e-6)
    child_v_len = max(2.0 * math.hypot(ev[0], ev[1]), 1e-6)
    child_h_neg, child_h_pos = _chord_sides(child_h_points, child_origin, child_h_len)
    child_v_neg, child_v_pos = _chord_sides(child_v_points, child_origin, child_v_len)
    main_h_neg, main_h_pos = _arc_sides(main_h_cum[-1], main_h_arc)
    main_v_neg, main_v_pos = _arc_sides(main_v_cum[-1], main_v_arc)

    def side_scales(c_neg, c_pos, m_neg, m_pos, chord_len):
        # Per-side scale = matching-main-side / child-side, so +/- one child
        # side length lands exactly on the guide endpoint. A side collapsed
        # to the floor (T-shaped crossing at an endpoint) has no real extent:
        # reuse the opposite side's scale there instead of dividing by the 1%
        # floor, which would catapult stray points by a ~100x amplifier.
        floor = 0.01 * chord_len + 1e-9
        s_neg = m_neg / c_neg
        s_pos = m_pos / c_pos
        if c_neg <= floor < c_pos:
            s_neg = s_pos
        elif c_pos <= floor < c_neg:
            s_pos = s_neg
        return s_neg, s_pos

    h_scale_neg, h_scale_pos = side_scales(child_h_neg, child_h_pos,
                                           main_h_neg, main_h_pos, child_h_len)
    v_scale_neg, v_scale_pos = side_scales(child_v_neg, child_v_pos,
                                           main_v_neg, main_v_pos, child_v_len)
    if info is not None:
        info["h_scale_mismatch"] = (max(h_scale_neg, h_scale_pos)
                                    / max(1e-9, min(h_scale_neg, h_scale_pos)))
        info["v_scale_mismatch"] = (max(v_scale_neg, v_scale_pos)
                                    / max(1e-9, min(v_scale_neg, v_scale_pos)))

    def side_map(units, scale_neg, scale_pos):
        return units * (scale_pos if units >= 0.0 else scale_neg)

    def map_point(point):
        dx = point[0] - child_origin[0]
        dy = point[1] - child_origin[1]
        u = (dx * ev[1] - dy * ev[0]) / det
        v = (eh[0] * dy - eh[1] * dx) / det
        u_units = u * 0.5 * child_h_len
        v_units = v * 0.5 * child_v_len
        arc_u = main_h_arc + side_map(u_units, h_scale_neg, h_scale_pos)
        on_h = _point_at_arc(main_h_points, main_h_cum, arc_u)
        on_v = _point_at_arc(main_v_points, main_v_cum,
                             main_v_arc + side_map(v_units, v_scale_neg, v_scale_pos))
        # The H guide is the spine: the V displacement is expressed in the
        # spine's LOCAL frame, rotating with its tangent. A curved spine
        # therefore keeps deforming the pattern all the way to its far ends
        # (and beyond, where the end tangent freezes the rotation) instead of
        # fading into a translated copy. For a straight spine the rotation is
        # identity and this reduces to the plain affine mapping.
        off_x = on_v[0] - main_origin[0]
        off_y = on_v[1] - main_origin[1]
        if not spine_rotation:
            # "Auto Mapping 2": pure per-quadrant Coons interpolation. With
            # translated boundary curves the patch collapses to exactly
            # H(s) + V(t) - O: the V displacement is translated (never
            # rotated) along the spine.
            return (on_h[0] + off_x, on_h[1] + off_y)
        tangent = _tangent_at_arc(main_h_points, main_h_cum, arc_u, tangent_window)
        cos_t = tangent[0] * base_tangent[0] + tangent[1] * base_tangent[1]
        sin_t = tangent[1] * base_tangent[0] - tangent[0] * base_tangent[1]
        return (on_h[0] + off_x * cos_t - off_y * sin_t,
                on_h[1] + off_x * sin_t + off_y * cos_t)

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
        items.extend(_grid_overlay_items(view_name))
    except Exception as error:
        print(f"[auto_mapping] refer rect grid skipped: {error}")
    try:
        _animean().ui.set_overlay(view_name, items)
    except Exception as error:
        print(f"[auto_mapping] overlay update failed: {error}")


def _set_draw_color(color):
    try:
        _animean().ui.set_draw_color(color)
    except Exception:
        pass


def _child_frame():
    """Chord frame of the child guides: origin, unit axes, per-side lengths."""
    assets = _assets_for("child")
    if H_PROPERTY not in assets or V_PROPERTY not in assets:
        return None
    h_pts = assets[H_PROPERTY]["points"]
    v_pts = assets[V_PROPERTY]["points"]
    origin, _, _ = _polyline_intersection(h_pts, v_pts)
    h_len = math.hypot(h_pts[-1][0] - h_pts[0][0], h_pts[-1][1] - h_pts[0][1])
    v_len = math.hypot(v_pts[-1][0] - v_pts[0][0], v_pts[-1][1] - v_pts[0][1])
    if h_len <= 1e-6 or v_len <= 1e-6:
        return None
    h_dir = ((h_pts[-1][0] - h_pts[0][0]) / h_len, (h_pts[-1][1] - h_pts[0][1]) / h_len)
    v_dir = ((v_pts[-1][0] - v_pts[0][0]) / v_len, (v_pts[-1][1] - v_pts[0][1]) / v_len)
    h_neg, h_pos = _chord_sides(h_pts, origin, h_len)
    v_neg, v_pos = _chord_sides(v_pts, origin, v_len)
    return {
        "origin": origin, "h_dir": h_dir, "v_dir": v_dir,
        "h_neg": h_neg, "h_pos": h_pos, "v_neg": v_neg, "v_pos": v_pos,
    }


def _frame_point(frame, u_hat, v_hat):
    """Child-space point at normalised grid coords (u_hat, v_hat in [-1, 1])."""
    du = u_hat * (frame["h_pos"] if u_hat >= 0.0 else frame["h_neg"])
    dv = v_hat * (frame["v_pos"] if v_hat >= 0.0 else frame["v_neg"])
    return (frame["origin"][0] + du * frame["h_dir"][0] + dv * frame["v_dir"][0],
            frame["origin"][1] + du * frame["h_dir"][1] + dv * frame["v_dir"][1])


def _grid_overlay_items(view_name):
    """Refer-rect debug grid: the 3x3 anchor lattice (crossing, 4 guide
    endpoints, 4 quadrant corners) with quarter-step iso-lines. The child
    view shows the reference frame itself; the main view shows its image
    under the CURRENT mapper, so a wrong mapping is visible at a glance.
    No grid is shown for guide configurations the mapper itself refuses
    (missing or non-crossing lines)."""
    if not _REFER_RECT["enabled"]:
        return []
    cached = _GRID_CACHE.get(view_name)
    if cached is not None:
        return cached

    child_assets = _assets_for("child")
    if H_PROPERTY not in child_assets or V_PROPERTY not in child_assets:
        return []
    if not _polylines_cross(child_assets[H_PROPERTY]["points"],
                            child_assets[V_PROPERTY]["points"]):
        return []
    frame = _child_frame()
    if frame is None:
        return []

    mapper = None
    if view_name == "main":
        main_assets = _assets_for("main")
        if H_PROPERTY not in main_assets or V_PROPERTY not in main_assets:
            return []
        if not _polylines_cross(main_assets[H_PROPERTY]["points"],
                                main_assets[V_PROPERTY]["points"]):
            return []
        mapper, _ = build_mapper(
            child_assets[H_PROPERTY]["points"], child_assets[V_PROPERTY]["points"],
            main_assets[H_PROPERTY]["points"], main_assets[V_PROPERTY]["points"],
            spine_rotation=_MAPPER_VERSION["value"] == 1)
        if mapper is None:
            return []

    items = []
    levels = (-1.0, -0.5, 0.0, 0.5, 1.0)
    samples = [i / 24.0 * 2.0 - 1.0 for i in range(25)]
    for level in levels:
        iso_u = [_frame_point(frame, level, s) for s in samples]
        iso_v = [_frame_point(frame, s, level) for s in samples]
        if mapper is not None:
            iso_u = [mapper(p) for p in iso_u]
            iso_v = [mapper(p) for p in iso_v]
        for points in (iso_u, iso_v):
            items.append({
                "id": "refer_rect_grid",
                "points": points,
                "color": GRID_COLOR,
                "width": 1.0,
                "removable": False,
            })
    _GRID_CACHE[view_name] = items
    return items


def _overlays_changed(view_name):
    """Assets changed in view_name: refresh its overlay — and the main view's
    too when refer rect is on, since the main grid mirrors the child frame."""
    _invalidate_grid_cache()
    _push_overlay(view_name)
    if _REFER_RECT["enabled"] and view_name != "main":
        _push_overlay("main")


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
            # A previous mapping result must not act as a region boundary,
            # otherwise the mapped layer shatters the bucket into tiny pieces.
            if (stroke.get("property") or "") == MAPPED_PROPERTY:
                continue
            segments.extend(_stroke_segments(stroke))
    if not segments:
        return []
    if view_name == "child":
        # The child board is an infinite canvas: the detection bounds follow
        # the actual content (plus a generous margin) instead of the nominal
        # page, so a shape drawn or panned anywhere still buckets correctly
        # and is never silently truncated at a page edge.
        bounds_rect = _content_rect(segments, seed)
    else:
        bounds_rect = _canvas_rect(view_name)
    path = _animean().vectorlogic.vector_region_path_at(seed, segments, bounds_rect)
    return _path_commands_to_polygons(path.get("commands"))


def _content_rect(segments, seed):
    xs = [seed[0]]
    ys = [seed[1]]
    for a, b in segments:
        xs.extend((a[0], b[0]))
        ys.extend((a[1], b[1]))
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    pad = max(max_x - min_x, max_y - min_y, 512.0)
    return (min_x - pad, min_y - pad,
            (max_x - min_x) + 2.0 * pad, (max_y - min_y) + 2.0 * pad)


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
        _save_assets(view_name)
        _overlays_changed(view_name)


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


def _remove_previous_mapping(scene, row):
    """Drop every previously mapped stroke on this frame, in any layer.

    Scanning all layers (not just the mapped layer) also cleans up results
    produced before the mapping got its own layer, so a re-run never leaves
    a duplicate copy behind in whatever layer used to be current.
    """
    structure = scene.get_structure()
    if row < 0 or row >= structure["frame_count"]:
        return 0

    removed = 0
    for layer in structure["layers"]:
        layer_index = layer["index"]
        strokes = scene.cell_to_dict(layer_index, row, False, POLY_STEP)["image"]["strokes"]
        for index in range(len(strokes) - 1, -1, -1):
            if (strokes[index].get("property") or "") == MAPPED_PROPERTY:
                scene.remove_stroke(row, layer_index, index)
                removed += 1
    return removed


def _is_mapped_layer_name(name):
    """True for "mapped layer" and its "mapped layer1"/"mapped layer2" variants.

    Deleting a layer keeps its asset, and uniqueLayerName() refuses a name any
    remaining asset still holds, so set_layer_name() can hand back a numbered
    variant. Matching those too keeps the layer reusable instead of appending a
    fresh one on every run.
    """
    if not name or not name.startswith(MAPPED_LAYER_NAME):
        return False
    suffix = name[len(MAPPED_LAYER_NAME):]
    return suffix == "" or suffix.isdigit()


def _free_frame_id(layer, row, asset_index):
    """Pick a frame id no other row of this layer/asset already uses.

    A cell resolves its drawing through (asset_index, frame_id), so two rows
    sharing a frame id would silently share one image. add_layer() hard-codes
    frame_id 1 on its creation row whatever that row number is, so row + 1 on
    its own is not collision-free.
    """
    used = set()
    for cell in (layer or {}).get("cells") or []:
        if cell["frame_index"] != row and cell["asset_index"] == asset_index:
            used.add(cell["frame_id"])
    frame_id = row + 1
    while frame_id in used:
        frame_id += 1
    return frame_id


def _layer_info(scene, layer_index):
    for layer in scene.get_structure()["layers"]:
        if layer["index"] == layer_index:
            return layer
    return None


def _ensure_mapped_layer(scene, row):
    """Return the index of the dedicated "mapped layer", creating it if missing.

    The mapping result never touches whatever layer the user has selected: it
    always lands in its own layer, so it can be hidden, reordered or deleted
    independently. The layer is reused across runs (matched by name) instead of
    piling up a new layer every time Auto Mapping is clicked. The user's own
    frame/layer/asset selection is restored before returning, because
    add_layer()/add_asset() select what they create.
    """
    saved_frame = scene.current_frame()
    saved_layer = scene.current_layer()
    saved_asset = scene.current_asset()

    def restore():
        scene.set_current_frame(saved_frame)
        scene.set_current_layer(saved_layer)
        scene.set_current_asset(saved_asset)

    exact = None
    numbered = None
    for layer in scene.get_structure()["layers"]:
        name = layer["name"] or layer["column_name"]
        if not _is_mapped_layer_name(name):
            continue
        if name == MAPPED_LAYER_NAME:
            if exact is None:
                exact = layer
        elif numbered is None:
            numbered = layer
    found = exact if exact is not None else numbered

    layer_index = -1
    asset_index = -1
    if found is not None:
        layer_index = found["index"]
        for cell in found["cells"]:
            if cell["asset_index"] >= 0:
                asset_index = cell["asset_index"]
                break
    else:
        layer_index = scene.add_layer()
        if layer_index < 0:
            restore()
            return -1
        scene.set_layer_name(layer_index, MAPPED_LAYER_NAME)
        created_row = scene.current_frame()
        asset_index = scene.cell_asset_index(created_row, layer_index)
        # add_layer() writes frame_id 1 on the creation row; normalise it to the
        # row + 1 convention used everywhere else so a later run on a different
        # row cannot end up aliasing this row's drawing.
        if asset_index >= 0:
            cell = _animean().Cell()
            cell.asset_index = asset_index
            cell.frame_id = created_row + 1
            scene.set_cell(created_row, layer_index, cell)
        print(f"[auto_mapping] created layer '{scene.layer_name(layer_index)}' in main_paint_view")

    # A reused layer may have no cell on this frame yet: back it with the
    # layer's own asset so image_at() cannot silently rename the column.
    if scene.cell_asset_index(row, layer_index) < 0:
        if asset_index < 0:
            asset_index = scene.add_asset("vector", MAPPED_LAYER_NAME)
        cell = _animean().Cell()
        cell.asset_index = asset_index
        cell.frame_id = _free_frame_id(_layer_info(scene, layer_index), row, asset_index)
        scene.set_cell(row, layer_index, cell)

    restore()
    return layer_index


def _perform_mapping(version=1):
    animean = _animean()
    child = _scene_model("child")
    main = _scene_model("main")
    _MAPPER_VERSION["value"] = version

    child_frame = max(child.current_frame(), 0)
    main_frame = max(main.current_frame(), 0)

    _absorb_legacy_items("child", child, child_frame)
    _absorb_legacy_items("main", main, main_frame)

    # The refer-rect grid tracks the selected algorithm even when the run
    # below is refused (e.g. no pattern yet — checking the grid first is a
    # legitimate workflow).
    _invalidate_grid_cache()
    if _REFER_RECT["enabled"]:
        _push_overlay("child")
        _push_overlay("main")

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

    crossings_ok = True
    for view_label, assets in (("child_paint_view", child_assets), ("main_paint_view", main_assets)):
        if not _polylines_cross(assets[H_PROPERTY]["points"], assets[V_PROPERTY]["points"]):
            print(f"[auto_mapping] the H and V center lines in {view_label} do NOT cross — "
                  "extend them so they intersect, then run Auto Mapping again.")
            crossings_ok = False
    if not crossings_ok:
        # A guessed origin produces unbounded garbage under per-side scaling;
        # refusing beats mapping nonsense.
        return False

    mapper_info = {}
    map_point, width_scale = build_mapper(
        child_assets[H_PROPERTY]["points"],
        child_assets[V_PROPERTY]["points"],
        main_assets[H_PROPERTY]["points"],
        main_assets[V_PROPERTY]["points"],
        mapper_info,
        spine_rotation=version == 1,
    )
    if map_point is None:
        print(f"[auto_mapping] cannot build mapping: {width_scale}")
        return False
    worst_mismatch = max(mapper_info.get("h_scale_mismatch", 1.0),
                         mapper_info.get("v_scale_mismatch", 1.0))
    if worst_mismatch > 1.5:
        print(f"[auto_mapping] tip: the crossings sit at different relative positions "
              f"(side scale mismatch x{worst_mismatch:.1f}); strokes crossing a center "
              "line will fold there. Place both crossings at similar positions along "
              "their lines to avoid it.")

    child_area = (child_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")
    main_area = (main_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")

    replaced = _remove_previous_mapping(main, main_frame)
    mapped_layer = _ensure_mapped_layer(main, main_frame)
    if mapped_layer < 0:
        print(f"[auto_mapping] could not create the '{MAPPED_LAYER_NAME}' in main_paint_view.")
        return False

    image = main.image_at(main_frame, mapped_layer, True)
    if image is None:
        print(f"[auto_mapping] '{MAPPED_LAYER_NAME}' has no editable cell to draw into.")
        return False

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
    algorithm = "Auto Mapping" if version == 1 else "Auto Mapping 2"
    try:
        animean.ui.history_commit(algorithm, "main")
    except Exception:
        pass  # older builds without the history binding
    summary = (f"[auto_mapping] {algorithm} "
               f"({'spine rotation' if version == 1 else 'coons interpolation'}) mapped "
               f"{added} stroke(s) into '{main.layer_name(mapped_layer)}' "
               f"(layer {mapped_layer + 1} of main_paint_view, width x{width_scale:.2f})")
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


def _run(version=1):
    try:
        _perform_mapping(version)
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
            # The click left the model unchanged: veto the pending commit so
            # no empty history entry is created (and no redo tail is lost).
            message["cancel_history"] = True
            _animean().ui.widget.refresh()
            return
        polygons = _detect_region(scene, view, row, points[0])
        if not polygons:
            print(f"[auto_mapping] no closed region around the click in {view} view; "
                  "draw a closed shape first.")
            message["cancel_history"] = True
            _animean().ui.widget.refresh()
            return
        assets[MAPPING_AREA_PROPERTY] = {"polygons": polygons}
        print(f"[auto_mapping] mapping area set in {view} view (click 'x' to remove)")
    else:
        if len(points) < 2:
            print("[auto_mapping] center line too short; draw a longer line.")
            message["cancel_history"] = True
            _animean().ui.widget.refresh()
            return
        assets[prop] = {"points": points, "width": width}
        print(f"[auto_mapping] {ITEM_LABELS[prop]} set in {view} view (redraw replaces it)")

    # Written BEFORE the C++ stroke commit fires, so the new guide/area rides
    # in the same history entry and is undone/redone together with it.
    _save_assets(view)
    _overlays_changed(view)
    # The guide is captured; put the pen back to black so the next ordinary
    # stroke is not accidentally drawn in the guide colour.
    _set_draw_color((0, 0, 0, 255))
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
        label = ITEM_LABELS.get(item_id, item_id)
        _save_assets(view)
        try:
            _animean().ui.history_commit(f"Remove {label}", view)
        except Exception:
            pass  # older builds without the history binding
        print(f"[auto_mapping] removed {label} in {view} view")
        _overlays_changed(view)


def _history_restored(cell, stroke, message):
    """After undo/redo/jump/reset the scene's scriptData is authoritative."""
    _load_assets(message.get("view") or "main")


def _auto_mapping_button(cell, stroke, message):
    global _last_run_handled
    _last_run_handled = True
    _run(1)


def _auto_mapping2_button(cell, stroke, message):
    global _last_run_handled
    _last_run_handled = True
    _run(2)


def _tool_option_changed(cell, stroke, message):
    if message.get("hook") != "refer_rect":
        return
    enabled = str(message.get("value", "")).lower() == "on"
    if _REFER_RECT["enabled"] == enabled:
        return
    _REFER_RECT["enabled"] = enabled
    _invalidate_grid_cache()
    _push_overlay("child")
    _push_overlay("main")
    print(f"[auto_mapping] refer rect grid {'ON' if enabled else 'OFF'} "
          f"(algorithm: {'Auto Mapping' if _MAPPER_VERSION['value'] == 1 else 'Auto Mapping 2'})")


def register_hooks():
    python_hooks.set_hook(_capture_mapping_item, linefinish=True, tool="extra")
    python_hooks.set_hook(_overlay_removed, overlayremove=True)
    python_hooks.set_hook(_history_restored, historyrestore=True)
    python_hooks.set_hook(_auto_mapping_button, extra=True, tool=AUTO_MAPPING_TOOL)
    python_hooks.set_hook(_auto_mapping2_button, extra=True, tool=AUTO_MAPPING2_TOOL)
    # tool="extra" keeps this hook from intercepting (and debug-dock-spamming)
    # every built-in tool's option events — refer_rect only exists on extra
    # tools anyway.
    python_hooks.set_hook(_tool_option_changed, option=True, tool="extra")


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
    _run(1)
    return property_value


def run_auto_mapping_2(name=AUTO_MAPPING2_TOOL, property_value=AUTO_MAPPING2_TOOL):
    global _last_run_handled
    register_hooks()
    if _last_run_handled:
        _last_run_handled = False
        return property_value
    _run(2)
    return property_value
