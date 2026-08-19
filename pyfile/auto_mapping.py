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
   The result always goes into its own FRESH layer ("mapped layer",
   "mapped layer1", ...) - never into whatever layer is currently selected.
   Every click creates a new layer and previous results are left untouched,
   so different runs stack and can be compared, hidden or deleted
   individually. Each mapping asset shows an "x" badge on the canvas -
   click it to delete the item and redraw.
   The "Curve Mode" option controls the output geometry of each mapped
   stroke: because the warp is non-linear, all modes sample between the
   original vertices so the result follows the distortion. "spline" (default)
   and "bezier" fit real curves; "polyline" emits the sampled points joined
   by straight segments. See CURVE_MODES / _CURVE_MODE.

Architecture note: C++ only provides generic services (overlay display list,
"overlayremove" hook event, set_draw_color, geometry bindings, and building a
stroke from a curved path via make_stroke_object_from_path). All tool
semantics - property names, colors, the asset dict, region detection,
clipping, curve fitting - live in this file.
"""

import bisect
import heapq
import json
import math
import os
import re
import tempfile
import time

import bezier
import python_hooks

H_PROPERTY = "h_center_line"
V_PROPERTY = "v_center_line"
GUIDE_PROPERTIES = (H_PROPERTY, V_PROPERTY)
MAPPED_PROPERTY = "auto_mapped"
MAPPED_LAYER_NAME = "mapped layer"
# Dual-layer fold output. Where the map is orientation-reversing (spec (5.1)
# det J < 0) the pattern is mirrored - physically, we are looking at the BACK
# of a fold. Rather than fight that (AM1 tried, and folded at the curvature
# radius for its trouble), the run is split at the fold boundary and the two
# sides go to their own layers, so the back can carry a lining colour and be
# hidden independently.
BACK_PROPERTY = "auto_mapped_back"
SEAL_PROPERTY = "auto_mapped_seal"
BACK_LAYER_NAME = "mapped layer back"
SEAL_LAYER_NAME = "mapped layer crease"
# Every run packs its output into one nested layer group: the two guide axes
# in a collapsed "H/V" subgroup, then the front / back / crease layers.
GUIDE_GROUP_NAME = "H/V"
H_LAYER_NAME = "H axis"
V_LAYER_NAME = "V axis"
# The axis snapshot layers carry WHICH axis they are in the stroke property,
# so restoring them never has to guess from a layer name the user may have
# renamed. GUIDE_LAYER_PROPERTY is the undifferentiated name the first version
# wrote; it is still recognised on load, falling back to the layer name.
GUIDE_LAYER_PROPERTY = "auto_mapped_guide"
H_GUIDE_LAYER_PROPERTY = "auto_mapped_guide_h"
V_GUIDE_LAYER_PROPERTY = "auto_mapped_guide_v"
# The nearest-end anchor's snapshot: recorded next to the two axes so a run's
# provenance is complete - re-expanding restores the stacking too.
NEAREST_LAYER_PROPERTY = "auto_mapped_guide_nearest"
NEAREST_LAYER_NAME = "Nearest Point"
MAPPING_GROUP_NAME = "Auto Mapping"
RESTORE_GUIDES_ACTION = "restore_mapping_guides"
# Everything a run puts on the board. None of it may act as a wall for region
# detection, and none of it may be picked up as pattern by the next run.
MAPPING_OUTPUT_PROPERTIES = (MAPPED_PROPERTY, BACK_PROPERTY, SEAL_PROPERTY,
                             GUIDE_LAYER_PROPERTY, H_GUIDE_LAYER_PROPERTY,
                             V_GUIDE_LAYER_PROPERTY, NEAREST_LAYER_PROPERTY)
_FOLD = {"split": True, "seal": True, "back_color": (104, 112, 140, 255),
         # Qt::PenStyle for the crease strokes: 2 = DashLine. The crease is an
         # annotation of the fold, and a dashed line reads as annotation where
         # a solid one reads as artwork.
         "seal_pen_style": 2}


def fold_split_enabled():
    return _FOLD["split"]


def fold_seal_enabled():
    return _FOLD["seal"]


def fold_back_color():
    return _FOLD["back_color"]
# The one and only automapping button. Its internal id keeps the historical
# "_2" suffix ("Auto Mapping 2", Coons interpolation) so nothing stored in
# old sessions changes meaning; the retired spine-rotation algorithm lives in
# old_history/auto_mapping_1.py.
AUTO_MAPPING2_TOOL = "auto_mapping_2"
MAPPING_AREA_PROPERTY = "mapping_area"
POLY_STEP = 4.0

# The "nearest end" of the folded sheet: a red draggable handle on the main
# board that decides the STACKING of fold layers. Depth of any point = number
# of creases crossed between it and this anchor (in arc space); depth 0 paints
# on top. Stored in the main view's assets as arc coordinates so it rides the
# guides, defaulting to the crossing.
NEAREST_PROPERTY = "fold_nearest"
NEAREST_HANDLE_COLOR = (230, 45, 45, 255)

# "Additional line": a pink refinement guide drawn on either board on top of
# the H/V axes. Each line exists as a PAIR (child version, main version) - the
# side the user did not draw on is synthesized through the current mapping -
# and the pair's difference drives a local warp of the mapping (see
# _AdditionalWarp). Falloff shape and reach are tool options.
ADDITIONAL_PROPERTY = "additional_line"
ADDITIONAL_COLOR = (255, 105, 180, 255)
ADDITIONAL_FALLOFFS = ("linear", "quadratic")
_ADDITIONAL = {"falloff": "linear", "radius_factor": 0.5}


def additional_falloff():
    return _ADDITIONAL["falloff"]

GRID_COLOR = (255, 140, 0, 170)

# How the mapped strokes' geometry is reconstructed after the warp. The warp
# (build_mapper.map_point) is non-linear, so the straight line between two
# mapped ORIGINAL vertices is NOT the image of the source segment - it is only
# its chord. All three modes therefore share the same anchored sampling: the
# original vertices are anchors, samples are inserted between them until the
# warp is well captured, everything is mapped, and only the inserted samples
# are decimated again (RDP). They differ in the output geometry:
#   "polyline" - the decimated points joined by straight segments (no fitting).
#   "spline"   - centripetal Catmull-Rom interpolated through the points
#                (route 1: migrate spline knots).
#   "bezier"   - no polyline sampling at all: the artist's own Bezier segments
#                are kept and each control handle is transported through the
#                warp's local directional derivative, splitting adaptively
#                where that is not accurate enough (route 2: migrate handles).
CURVE_MODES = ("polyline", "spline", "bezier")
# Bezier is the default: it carries the stroke's OWN curve segments through the
# warp instead of resampling it into points and fitting a new curve through
# them, so the output keeps the geometry the artist drew rather than an
# approximation of it.
DEFAULT_CURVE_MODE = "bezier"
_CURVE_MODE = {"value": DEFAULT_CURVE_MODE}

# Curve-fitting tolerances, all in canvas (main-view) pixels.
_CURVE_TOL = 0.4        # max chord/handle deviation before subdividing further
_SPLINE_MAX_DEPTH = 8   # source-segment bisections in spline densification
_BEZIER_MAX_DEPTH = 6   # source-cubic bisections in bezier handle transport
_JAC_EPS = 0.5          # directional-derivative step, in child (source) pixels
_CATMULL_ALPHA = 0.5    # centripetal parametrization (no overshoot on uneven knots)
# Anti-aliasing guards: a straight source segment spanning whole periods of a
# wavy main guide can pass a small fixed set of probe points even though its
# true image oscillates (e.g. a full sine period has zero midpoint deviation).
_FORCE_STEP = 16.0      # output px: always sample at least this dense
_MAX_KNOTS_PER_SPAN = 48  # cap on structural knots forced into one source span
_PROBE_T = 0.381966     # golden-section probe; never rational vs the midpoint

# RDP decimation strength ("RDP" slider in the tool options, in 0.1px units).
# Only the samples INSERTED between two original vertices are decimated; the
# original vertices themselves are anchors and always survive (user rule:
# 原始点不能被降采样).
_RDP_STATE = {"eps": 0.3}


def rdp_eps():
    return _RDP_STATE["eps"]

# Refer-rect debug grid state, PER VIEW. It answers "is my mapping right?",
# and the two boards answer it separately: the child shows the reference frame
# itself, the main shows its image under the mapper, and wanting one is no
# reason to be shown the other.
_REFER_RECT = {"child": False, "main": False}
# Refer-rect grid density: iso-lines per axis over [-1, 1]. 5 is the
# historical look; finer settings are the debugging view the user asked
# for - reading a multi-line warp's topology needs more iso-lines than a
# placement check does. Sample density per iso-line scales with it so a
# fine grid stays smooth through folds.
GRID_DIVISION_CHOICES = (5, 9, 17, 33)
_GRID = {"divisions": 5}
# Grid polylines are O(n^2) in guide points to build (intersection searches):
# cache per view, invalidated whenever the guides change.
_GRID_CACHE = {"child": None, "main": None}

# The child-view "Occluded Areas" toggle (a script-defined view button):
# tint the parts of the child board that the CURRENT mapping folds
# face-down, so the user can see what the lining hides before mapping.
OCCLUSION_BUTTON = "occlusion_preview"
OCCLUSION_FILL = (104, 112, 140, 70)   # the lining color, translucent
_OCCLUSION = {"enabled": False}
_OCCLUSION_CACHE = {"items": None, "note": "", "share": 0.0}


def refer_rect_enabled(view_name="main"):
    return bool(_REFER_RECT.get(view_name, False))


def curve_mode():
    return _CURVE_MODE["value"]


def _invalidate_grid_cache():
    _GRID_CACHE["child"] = None
    _GRID_CACHE["main"] = None
    # Same lifetime: both displays are pure functions of the guide assets.
    _OCCLUSION_CACHE["items"] = None

H_COLOR = (0, 0, 255, 255)
V_COLOR = (0, 255, 0, 255)
AREA_BORDER_COLOR = (120, 185, 250, 190)
AREA_FILL_COLOR = (150, 205, 255, 60)

# How every line this tool draws is DISPLAYED. One dict so the settings
# window has a single thing to read and write, and so nothing has to guess
# what "the crease colour" means. Widths are on-screen px; "style" is a
# Qt::PenStyle number (1 solid, 2 dash, 3 dot, 4 dash-dot).
LINE_STYLES = (("Solid", 1), ("Dashed", 2), ("Dotted", 3), ("Dash-Dot", 4))
_LINE_DISPLAY = {
    "h_color": H_COLOR,
    "h_width": 3.0,
    "h_style": 1,
    "v_color": V_COLOR,
    "v_width": 3.0,
    "v_style": 1,
    # The crease ("fold line") between the front and the back of a fold.
    "seal_color": (104, 112, 140, 255),
    "seal_width": 0.8,      # relative to the mapped stroke width
    "seal_style": 2,
    # The lining: the back-of-the-fold strokes, and the occlusion tint that
    # previews which parts of the texture land there.
    "back_color": (104, 112, 140, 255),
    "occlusion_alpha": 70,
    # Additional (refinement) lines: pink, on both boards.
    "additional_color": ADDITIONAL_COLOR,
    "additional_width": 2.5,
    "additional_style": 1,
}


def line_display():
    return dict(_LINE_DISPLAY)


def _display_color(key):
    value = _LINE_DISPLAY.get(key) or (0, 0, 0, 255)
    return tuple(int(c) for c in value)


def _display_style(key):
    style = int(_LINE_DISPLAY.get(key, 1))
    # 0 is Qt::NoPen: a guide that cannot be seen is never what was meant.
    return style if 1 <= style <= 5 else 1

ITEM_LABELS = {
    H_PROPERTY: "H center line",
    V_PROPERTY: "V center line",
    MAPPING_AREA_PROPERTY: "mapping area",
    NEAREST_PROPERTY: "nearest point",
    ADDITIONAL_PROPERTY: "additional line",
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
    """The page of `view_name`, straight from its scene.

    The globals below are a fallback for older builds; asking the model is
    both current (a global is only as fresh as the last state sync) and
    correct now that the page is a document property rather than the widget's
    size.
    """
    try:
        width, height = _scene_model(view_name).canvas_size()
        if width and height:
            return (0.0, 0.0, float(width), float(height))
    except Exception:
        pass

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
        elif prop == NEAREST_PROPERTY:
            arc = item.get("arc") or []
            if len(arc) >= 2:
                assets[prop] = {"arc": [float(arc[0]), float(arc[1])]}
        elif prop == ADDITIONAL_PROPERTY:
            lines = []
            for line in item.get("lines") or []:
                points = [(float(p[0]), float(p[1]))
                          for p in line.get("points") or []]
                loaded = {"points": points, "width": float(line.get("width", 2.5))}
                if "id" in line:
                    try:
                        loaded["id"] = int(line["id"])
                    except (TypeError, ValueError):
                        pass
                if len(points) < 2:
                    # Keep the slot (with its id): legacy pairs match the
                    # other board by position, and dropping one here would
                    # shift every later pair.
                    loaded["points"] = []
                    lines.append(loaded)
                    continue
                if line.get("commands"):
                    loaded["commands"] = line["commands"]
                third = [[float(t[0]), float(t[1])]
                         for t in line.get("third") or [] if len(t) >= 2]
                # The stored Third polyline is only authoritative while it
                # still matches the drawn points one to one.
                if len(third) == len(points):
                    loaded["third"] = third
                lines.append(loaded)
            # Legacy assets carry no ids; stamp positional ones NOW, on both
            # boards alike, so identity survives later deletes. Left
            # implicit, a delete renumbered the id-less half of a pair out
            # from under its partner (measured: the surviving pair died and
            # its +20 px correction went to 0).
            for index, line in enumerate(lines):
                line.setdefault("id", index)
            if lines:
                assets[prop] = {"lines": lines}
        elif prop in GUIDE_PROPERTIES:
            points = [(float(p[0]), float(p[1])) for p in item.get("points") or []]
            if len(points) >= 2:
                loaded = {"points": points, "width": float(item.get("width", 3.0))}
                # Curve guides round-trip: dropping the commands here would
                # silently demote every guide to its flattening on the first
                # undo or project load.
                if item.get("commands"):
                    loaded["commands"] = item["commands"]
                assets[prop] = loaded
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


def _direction_at_arc(points, cumulative, arc):
    """d/d(arc) of _point_at_arc: the unit direction of the active segment.

    Mirrors _point_at_arc's branch structure exactly, including the linear
    extrapolation past either end, so it really is that function's derivative
    (used as a Jacobian column when inverting the frame).
    """
    total = cumulative[-1]
    if len(points) < 2 or total <= 0.0:
        return (1.0, 0.0)
    if arc <= 0.0:
        return _segment_direction(points, 0)
    if arc >= total:
        return _segment_direction(points, len(points) - 2)
    index = bisect.bisect_right(cumulative, arc) - 1
    index = max(0, min(index, len(points) - 2))
    return _segment_direction(points, index)


def _tangent_at_arc(points, cumulative, arc, window=0.0):
    """Unit tangent of the polyline at arc position (end tangents outside).

    With a positive window the tangent is a central difference over
    [arc-window, arc+window]: hand-drawn guides carry per-segment direction
    jitter and release hooks at the ends, and the raw values would make the
    consumers (the handedness/mirror check in build_mapper, the direction
    arrows) flicker with the noise. The window keeps genuine curvature
    while averaging the noise away.
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


# A vertex turning more than this is a drawn CORNER, not hand jitter. Measured
# across every guide in the test corpus: jitter and genuine curvature stay
# under 15 deg per vertex (p99), the wildest hand wobble reaches 34 deg, and
# deliberately drawn corners start at 113 deg. 45 deg sits in that gap with
# margin on both sides. This flattening heuristic is the only signal a
# POLYLINE guide offers; it cannot go lower, because flattening chords fold
# genuine curvature into the measured turn.
_SHARP_TURN_DEG = 45.0
_SHARP_COS = math.cos(math.radians(_SHARP_TURN_DEG))

# CURVE guides carry a better signal: the stroke fitter emits a corner as a
# DELIBERATE tangent break at a segment joint (every other joint is G1 by
# construction), and its own threshold is 55 - 35*(corner/100) deg - 37.5 at
# the default pen, down to 20 at the slider max. All of those sit BELOW the
# flattening heuristic's 45, so reading the exact one-sided tangents at each
# joint is the lossless test: ~0 deg at a smooth joint, the full break angle
# at a corner, no flattening wobble folded in. 10 deg clears numerical noise
# by orders of magnitude and undercuts every fitter setting.
_SHARP_JOINT_DEG = 10.0
_SHARP_JOINT_COS = math.cos(math.radians(_SHARP_JOINT_DEG))

def _cubic_is_line(seg):
    """True when the control net lies on the chord (a line command in disguise)."""
    (x0, y0), c1, c2, (x3, y3) = seg
    dx, dy = x3 - x0, y3 - y0
    length = math.hypot(dx, dy)
    if length <= 1e-12:
        return True
    for cx, cy in (c1, c2):
        if abs((cx - x0) * dy - (cy - y0) * dx) / length > 1e-9:
            return False
    return True


def _sharp_vertex_arcs(points, arcs):
    """Arc positions of vertices that turn more than _SHARP_TURN_DEG.

    The map itself (`hv`) evaluates guide POSITIONS, so a drawn corner is a
    hard kink in the displacement field and the real fold it causes sits
    exactly on the corner's preimage. The orientation tests, however, read
    window-smoothed tangents (see tangent_at), and a plain central difference
    across a 113 deg corner smears the tangent jump into a ramp: the analytic
    fold locus then lands where the RAMP crosses zero - measured 11 child px
    away from where the artwork actually folds back, drifting with the row.
    Recording the sharp vertices lets tangent_at stop its window at them, so
    the smoothed tangent still de-noises jitter but jumps exactly where the
    position field kinks.
    """
    out = []
    for i in range(1, len(points) - 1):
        ax = points[i][0] - points[i - 1][0]
        ay = points[i][1] - points[i - 1][1]
        bx = points[i + 1][0] - points[i][0]
        by = points[i + 1][1] - points[i][1]
        la = math.hypot(ax, ay)
        lb = math.hypot(bx, by)
        if la < 1e-9 or lb < 1e-9:
            continue
        if (ax * bx + ay * by) / (la * lb) < _SHARP_COS:
            out.append(arcs[i])
    return out


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


# 8-node Gauss-Legendre on [-1, 1]: exact for polynomials to degree 15. The
# speed |r'(t)| of a cubic is not a polynomial, but on the few-px segments the
# smoother emits its quadrature error is far below machine-relevant scales.
_GL8 = ((-0.9602898564975363, 0.1012285362903763),
        (-0.7966664774136267, 0.2223810344533745),
        (-0.5255324099163290, 0.3137066458778873),
        (-0.1834346424956498, 0.3626837833783620),
        (0.1834346424956498, 0.3626837833783620),
        (0.5255324099163290, 0.3137066458778873),
        (0.7966664774136267, 0.2223810344533745),
        (0.9602898564975363, 0.1012285362903763))

# Per-segment arc<->t lookup resolution. Slices are ~seg/16 long and the speed
# barely varies across one, so inverting by linear interpolation in the table
# mis-parameterizes by well under 1e-3 px while every returned POINT still
# lies exactly on the curve - the error is where along the curve, never off it.
_CURVE_LUT = 16


class _Curve:
    """One guide: a chain of line/cubic segments with a Gauss-Legendre arc table.

    Two modes. A plain point list stays a POLYLINE and delegates to the exact
    module functions the frame always used, so every existing polyline guide
    (straight tests, legacy scriptData, stored axis snapshots) reproduces the
    previous arithmetic bit for bit. `commands` - the stroke's real mixed
    line/Bezier path, which capture now keeps - switch the guide to CURVE
    mode: arc length comes from GL(8) per t-slice instead of summed chords,
    and points/tangents are evaluated on the true curve. That is the honest
    fix for the flattening error: geometry and integral upgrade TOGETHER, so
    endpoint anchoring (spec 4.2) keeps holding exactly - patching only the
    integral was measured to overshoot the drawn guide end by 0.56 px.
    """

    def __init__(self, points, commands=None):
        cubics = []
        if commands:
            subpaths = _commands_to_subpaths(commands)
            if subpaths:
                cubics = [cubic for cubic in subpaths[0]
                          if _dist(cubic[0], cubic[3]) > 1e-12
                          or _dist(cubic[0], cubic[1]) > 1e-12
                          or _dist(cubic[2], cubic[3]) > 1e-12]
        self.curved = bool(cubics)
        if not self.curved:
            self.points = [(float(p[0]), float(p[1])) for p in points]
            self.cum = _cumulative_lengths(self.points)
            self.total = self.cum[-1]
            self.knots = self.cum
            # Every vertex lies on the "curve" (the polyline itself) and its
            # chord arc IS its arc, so the flattening table is the cum table.
            # Needed because a frame may pair a curve guide with a polyline
            # one (legacy asset, axis snapshot, absorbed guide) and the
            # crossing search then runs on both flattenings alike.
            self.flat_arcs = self.cum
            self.sharp_arcs = _sharp_vertex_arcs(self.points, self.flat_arcs)
            return

        # --- curve mode ---
        self.segs = cubics
        self.luts = []
        self.cum = [0.0]
        for p0, c1, c2, p3 in cubics:
            lut = [0.0]
            for k in range(_CURVE_LUT):
                a = k / _CURVE_LUT
                b = (k + 1) / _CURVE_LUT
                half, mid = (b - a) * 0.5, (a + b) * 0.5
                length = half * sum(w * self._speed(p0, c1, c2, p3, mid + half * x)
                                    for x, w in _GL8)
                lut.append(lut[-1] + length)
            self.luts.append(lut)
            self.cum.append(self.cum[-1] + lut[-1])
        self.total = self.cum[-1]
        self.knots = self.cum
        # A flattening for crossing seeds and any consumer that wants a point
        # list; every vertex lies exactly on the curve, and flat_arcs carries
        # its TRUE arc position so seeds convert into curve coordinates.
        self.points = []
        self.flat_arcs = []
        for index, (p0, c1, c2, p3) in enumerate(cubics):
            net = bezier.hull_length((p0, c1, c2, p3))
            count = max(1, int(math.ceil(net / POLY_STEP)))
            start = 0 if index == 0 else 1
            for k in range(start, count + 1):
                t = k / count
                self.points.append(_cubic_point((p0, c1, c2, p3), t))
                self.flat_arcs.append(self.cum[index] + self._partial(index, t))
        # Two sharp-vertex sources, merged. The flattening heuristic (45 deg)
        # catches cusps INSIDE a degenerate control net. Joints get the exact
        # test: the fitter emits corners as deliberate tangent breaks between
        # segments, and comparing the two one-sided tangents reads the break
        # angle losslessly - a 40 deg corner (default pen threshold 37.5) is
        # invisible to the flattening heuristic but a real kink in hv(), and
        # missing it re-opens the smeared-locus bug the clamp exists to fix.
        # Line-line joints stay OUT of the exact test: an all-line command
        # path is a polyline in disguise (a joint at every drawn vertex, hand
        # jitter and all), and a 10 deg gate there would clamp the de-noising
        # window at every wobble - the very shredding the window prevents.
        # Those keep the 45 deg flattening rule; a joint touching a genuine
        # cubic is fitter output, where a tangent break is deliberate.
        flat_sharp = _sharp_vertex_arcs(self.points, self.flat_arcs)
        joint_sharp = []
        for index in range(len(self.segs) - 1):
            if (_cubic_is_line(self.segs[index])
                    and _cubic_is_line(self.segs[index + 1])):
                continue
            ax, ay = self._seg_dir(index, 1.0)
            bx, by = self._seg_dir(index + 1, 0.0)
            if ax * bx + ay * by < _SHARP_JOINT_COS:
                joint_sharp.append(self.cum[index + 1])
        self.sharp_arcs = []
        for arc in sorted(flat_sharp + joint_sharp):
            if not self.sharp_arcs or arc - self.sharp_arcs[-1] > 1e-6:
                self.sharp_arcs.append(arc)

    @staticmethod
    def of(spec):
        """Build from a guide spec: a bare point list, or an asset dict
        {"points": ..., "commands": ...} (commands optional)."""
        if isinstance(spec, dict):
            return _Curve(spec.get("points") or [], spec.get("commands"))
        return _Curve(spec)

    @staticmethod
    def _speed(p0, c1, c2, p3, t):
        # |r'(t)| via the shared wheel pyfile/bezier.py (its arithmetic is
        # expression-identical, preserving this table's sub-1e-3px contract).
        return math.hypot(*bezier.cubic_derivative((p0, c1, c2, p3), t))

    def _partial(self, index, t):
        """Arc from the segment's start to parameter t (GL on the last slice)."""
        lut = self.luts[index]
        slot = min(_CURVE_LUT - 1, int(t * _CURVE_LUT))
        a = slot / _CURVE_LUT
        half, mid = (t - a) * 0.5, (t + a) * 0.5
        p0, c1, c2, p3 = self.segs[index]
        if half > 0.0:
            tail = half * sum(w * self._speed(p0, c1, c2, p3, mid + half * x)
                              for x, w in _GL8)
        else:
            tail = 0.0
        return lut[slot] + tail

    def _locate(self, arc):
        """(segment index, t) for an interior arc position."""
        index = bisect.bisect_right(self.cum, arc) - 1
        index = max(0, min(index, len(self.segs) - 1))
        local = arc - self.cum[index]
        lut = self.luts[index]
        slot = bisect.bisect_right(lut, local) - 1
        slot = max(0, min(slot, _CURVE_LUT - 1))
        span = lut[slot + 1] - lut[slot]
        frac = 0.0 if span <= 0.0 else (local - lut[slot]) / span
        return index, (slot + frac) / _CURVE_LUT

    def _seg_dir(self, index, t):
        # Hodograph from the shared wheel pyfile/bezier.py.
        seg = self.segs[index]
        dx, dy = bezier.cubic_derivative(seg, t)
        length = math.hypot(dx, dy)
        if length <= 1e-12:
            # degenerate derivative (a stubby control net): fall back to the
            # segment chord, mirroring _segment_direction's guard
            return _segment_direction([seg[0], seg[3]], 0)
        return dx / length, dy / length

    def point_at(self, arc):
        if not self.curved:
            return _point_at_arc(self.points, self.cum, arc)
        if arc <= 0.0:
            dx, dy = self._seg_dir(0, 0.0)
            p = self.segs[0][0]
            return (p[0] + dx * arc, p[1] + dy * arc)
        if arc >= self.total:
            dx, dy = self._seg_dir(len(self.segs) - 1, 1.0)
            p = self.segs[-1][3]
            extra = arc - self.total
            return (p[0] + dx * extra, p[1] + dy * extra)
        index, t = self._locate(arc)
        return _cubic_point(self.segs[index], t)

    def dir_at(self, arc):
        """Exact unit tangent at the arc position (end tangents outside)."""
        if not self.curved:
            return _direction_at_arc(self.points, self.cum, arc)
        if arc <= 0.0:
            return self._seg_dir(0, 0.0)
        if arc >= self.total:
            return self._seg_dir(len(self.segs) - 1, 1.0)
        index, t = self._locate(arc)
        return self._seg_dir(index, t)

    def tangent_at(self, arc, window=0.0):
        """Window-smoothed tangent; window 0 is the exact curve tangent.

        The window never averages ACROSS a sharp corner: it is clamped to the
        nearest sharp vertex on each side, degenerating to a one-sided
        difference next to one. A one-sided chord still spans up to a full
        window of samples, so jitter suppression survives; but the tangent now
        JUMPS exactly at the corner, matching the kink the position field
        (`hv`) actually has there. Without the clamp the smeared tangent put
        the analytic fold locus 11 child px off the real fold-back
        (see _sharp_vertex_arcs) - strokes visibly overshot the crease and
        folded back beyond it.
        """
        if window > 0.0 and self.sharp_arcs:
            lo, hi = arc - window, arc + window
            index = bisect.bisect_left(self.sharp_arcs, arc)
            if index > 0:
                lo = max(lo, self.sharp_arcs[index - 1])
            if index < len(self.sharp_arcs):
                hi = min(hi, self.sharp_arcs[index])
            if (lo, hi) != (arc - window, arc + window):
                if hi - lo > 1e-9:
                    before = self.point_at(lo)
                    after = self.point_at(hi)
                    dx, dy = after[0] - before[0], after[1] - before[1]
                    length = math.hypot(dx, dy)
                    if length > 1e-9:
                        return (dx / length, dy / length)
                return self.dir_at(arc)
        if not self.curved:
            return _tangent_at_arc(self.points, self.cum, arc, window)
        if window > 0.0:
            before = self.point_at(arc - window)
            after = self.point_at(arc + window)
            dx, dy = after[0] - before[0], after[1] - before[1]
            length = math.hypot(dx, dy)
            if length > 1e-9:
                return (dx / length, dy / length)
        return self.dir_at(arc)


def _curve_crossing(gh, gv):
    """(point, arc_on_h, arc_on_v) where two CURVE guides cross.

    Seeded on the flattenings (whose vertices lie exactly on the curves and
    carry their true arc positions), then converged onto the curves by a 2x2
    Newton in (arc_h, arc_v). Transversal crossings converge in a few steps;
    if the iteration will not settle, the flattening seed - already within a
    chord's deviation of the truth - is kept.
    """
    a, b = gh.points, gv.points
    seed = None
    eps = 1e-6
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = _segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -eps <= t <= 1.0 + eps and -eps <= u <= 1.0 + eps:
                t = min(max(t, 0.0), 1.0)
                u = min(max(u, 0.0), 1.0)
                seed = (gh.flat_arcs[i] + (gh.flat_arcs[i + 1] - gh.flat_arcs[i]) * t,
                        gv.flat_arcs[j] + (gv.flat_arcs[j + 1] - gv.flat_arcs[j]) * u)
                break
        if seed is not None:
            break
    if seed is None:
        best = None
        for i, pa in enumerate(a):
            for j, pb in enumerate(b):
                distance = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
                if best is None or distance < best[0]:
                    best = (distance, i, j)
        _, i, j = best
        point = ((a[i][0] + b[j][0]) * 0.5, (a[i][1] + b[j][1]) * 0.5)
        return point, gh.flat_arcs[i], gv.flat_arcs[j]

    arc_h, arc_v = seed
    for _ in range(12):
        ph = gh.point_at(arc_h)
        pv = gv.point_at(arc_v)
        fx, fy = ph[0] - pv[0], ph[1] - pv[1]
        if math.hypot(fx, fy) <= 1e-10:
            break
        hx, hy = gh.dir_at(arc_h)
        vx, vy = gv.dir_at(arc_v)
        # solve [hx -vx; hy -vy] [dh dv]^T = -[fx fy]^T by Cramer
        det = vx * hy - hx * vy
        if abs(det) < 1e-12:
            break
        step_h = (fx * vy - fy * vx) / det
        step_v = (fx * hy - fy * hx) / det
        if abs(step_h) > gh.total or abs(step_v) > gv.total:
            break
        arc_h = min(max(arc_h + step_h, 0.0), gh.total)
        arc_v = min(max(arc_v + step_v, 0.0), gv.total)
    return gh.point_at(arc_h), arc_h, arc_v


class _Frame:
    """One board's HV frame: two crossing guides plus the coordinate system
    they induce. `hv` is THE shared reconstruction function - child and main
    differ only in the guides handed to it.

    Coordinates (l_h, l_v) are SIGNED ARC LENGTHS measured from the crossing
    along each guide - on the CURVE when the guide carries its commands
    (Gauss-Legendre tables in _Curve), on the polyline otherwise. Arc length
    (not chord projection) is what makes the child side symmetric with the
    main side; see build_mapper.
    """

    def __init__(self, h_spec, v_spec):
        self.gh = _Curve.of(h_spec)
        self.gv = _Curve.of(v_spec)
        # The flattenings stay exposed: crossing seeds, direction arrows and
        # the chord-basis Newton seed all read plain point lists.
        self.h = self.gh.points
        self.v = self.gv.points
        self.h_cum = self.gh.cum
        self.v_cum = self.gv.cum
        self.h_total = self.gh.total
        self.v_total = self.gv.total
        if self.gh.curved or self.gv.curved:
            self.origin, self.h_arc, self.v_arc = _curve_crossing(self.gh, self.gv)
        else:
            self.origin, self.h_arc, self.v_arc = _polyline_intersection(self.h, self.v)
        # Outward extent on each side of the crossing, floored at 1% so a
        # T-shaped crossing cannot divide by (nearly) zero. The RAW values are
        # kept too: the floored ones cannot tell "collapsed side" apart from
        # "genuinely 1% long", which is what the transfer scales need to know.
        self.h_side = _arc_sides(self.h_total, self.h_arc)
        self.v_side = _arc_sides(self.v_total, self.v_arc)
        self.h_side_raw = (self.h_arc, self.h_total - self.h_arc)
        self.v_side_raw = (self.v_arc, self.v_total - self.v_arc)
        # Orientation tests compare guide DIRECTIONS, and the raw per-sample
        # directions of a hand-drawn guide jitter enough to flip that
        # comparison several times within a few px - measured on a real
        # 301-point guide, the fold count oscillated between 2 and 4 eleven
        # times along V, which shredded the output and tangled the crease.
        # CURVE guides keep the window too: the smoother's quad chain is
        # tangent-CONTINUOUS, but its tangent at every joint is exactly
        # (p_i - p_{i-1})/2 - the raw inter-sample direction, i.e. the very
        # jitter signal, merely interpolated continuously between joints.
        # Continuity is not smoothness; zeroing the window here was measured
        # to reverse the joint-to-joint heading 17 times over 161 joints.
        # (The window only feeds orientation tests; the Newton solve keeps
        # the exact tangents via jacobian/dir_at. And the window is CLAMPED at
        # sharp corners - see _Curve.tangent_at - so de-noising cannot move a
        # fold off the corner the artwork actually creases at.)
        self.h_window = max(2.0 * POLY_STEP, 0.03 * self.h_total)
        self.v_window = max(2.0 * POLY_STEP, 0.03 * self.v_total)

    def directions(self, l_h, l_v):
        """Window-smoothed guide directions, for ORIENTATION tests only.

        Never for the Newton solve in `solve`: there the exact tangent is the
        true derivative of `hv`, and smoothing it would break convergence.
        (On curve guides the window is zero and these ARE the exact tangents.)
        """
        return (self.gh.tangent_at(self.h_arc + l_h, self.h_window),
                self.gv.tangent_at(self.v_arc + l_v, self.v_window))

    def hv(self, l_h, l_v):
        """H(l_h) + V(l_v) - O: the Coons patch in its collapsed form."""
        on_h = self.gh.point_at(self.h_arc + l_h)
        on_v = self.gv.point_at(self.v_arc + l_v)
        return (on_h[0] + on_v[0] - self.origin[0],
                on_h[1] + on_v[1] - self.origin[1])

    def jacobian(self, l_h, l_v):
        """Columns of d(hv)/d(l_h, l_v): the two guide tangents."""
        return (self.gh.dir_at(self.h_arc + l_h),
                self.gv.dir_at(self.v_arc + l_v))

    def solve(self, point, guess_h, guess_v, iterations=24, tol=1e-7):
        """Invert hv: find (l_h, l_v) with hv(l_h, l_v) == point.

        DAMPED Newton (the descent variant). hv is piecewise affine - an
        arc-length lookup on two polylines - so an undamped step lands exactly
        once the iterate reaches the right cell, and on mild guides it does:
        the chord seed is exact for straight guides (0 iterations) and two or
        three steps suffice up to about 30 px of guide bow, with the residual
        squaring each step.

        Strongly bowed guides break that. hv becomes non-injective where the
        two tangents turn parallel, and an undamped Newton then oscillates
        between cells instead of settling - a measured residual cycle of
        6.6 -> 5.4 -> 15.9 -> 6.6 -> ... forever. Halving the step until it
        actually reduces the residual turns that into descent: on 60 px bowed
        guides the share of points solved to 1e-7 px went from 59.5% to 89.9%,
        while straight and mildly bowed guides converge in exactly the same
        number of iterations as before (the first trial is accepted, so there
        is no extra evaluation on the easy path).

        See docs/point_mapping_newton.md for the full derivation, the flow
        charts and the measurement tables.

        Never converging is a legitimate outcome, not a bug: some of those
        points have no preimage at all and others have several, because the
        frame genuinely folds. So the search only ever ACCEPTS an improvement,
        returns the best iterate it saw, and leaves the rest to the caller's
        residual term. Iterate 0 is the chord seed - the coordinates the
        pre-2026-08 implementation used - so the answer is never worse than
        that.
        """
        limit = self.h_total + self.v_total + 1.0
        l_h, l_v = guess_h, guess_v
        current = self.hv(l_h, l_v)
        best_error = math.hypot(current[0] - point[0], current[1] - point[1])
        best = (l_h, l_v)
        for _ in range(iterations):
            if best_error <= tol:
                break
            fx = current[0] - point[0]
            fy = current[1] - point[1]
            (hx, hy), (vx, vy) = self.jacobian(l_h, l_v)
            det = hx * vy - hy * vx
            if abs(det) < 1e-9:
                break  # locally folded frame: no usable direction from here
            step_h = (-fx * vy + fy * vx) / det
            step_v = (-hx * fy + hy * fx) / det
            scale = math.hypot(step_h, step_v)
            if scale > limit:
                step_h *= limit / scale
                step_v *= limit / scale

            accepted = None
            damping = 1.0
            for _ in range(12):
                trial_h = l_h + step_h * damping
                trial_v = l_v + step_v * damping
                trial = self.hv(trial_h, trial_v)
                error = math.hypot(trial[0] - point[0], trial[1] - point[1])
                if error < best_error:
                    accepted = (trial_h, trial_v, trial, error)
                    break
                damping *= 0.5
            if accepted is None:
                break  # no downhill step along this direction; keep the best
            l_h, l_v, current, best_error = accepted
            best = (l_h, l_v)
        return best


def _transfer_scales(child_raw, child_total, main_side):
    """Per-side arc scale child -> main, with a CONTINUOUS collapsed-side blend.

    A side collapsed onto the 1% floor (T-shaped crossing at an endpoint) has
    no real extent, so dividing by the floor would catapult stray points by a
    ~100x amplifier; the old code switched to the opposite side's scale with a
    hard `if`, which made the map discontinuous exactly at the threshold -
    moving the crossing by 1e-7 px across it moved a mapped point by ~200 px
    (measured). Ramping the weight over [0, floor] of the RAW side length
    keeps both endpoint behaviours (fallback at 0, plain ratio at/above the
    floor) and removes the cliff.
    """
    raw_neg, raw_pos = child_raw
    m_neg, m_pos = main_side
    floor = 0.01 * child_total
    if floor <= 0.0:
        return 1.0, 1.0
    base_neg = m_neg / max(raw_neg, floor)
    base_pos = m_pos / max(raw_pos, floor)
    w_neg = min(1.0, max(0.0, raw_neg / floor))
    w_pos = min(1.0, max(0.0, raw_pos / floor))
    return ((1.0 - w_neg) * base_pos + w_neg * base_neg,
            (1.0 - w_pos) * base_neg + w_pos * base_pos)


def _branch_window(points, third):
    """Canvas-arc-fraction window (f_lo, f_hi) of the longest
    branch-continuous stretch of a line's Third lift, or None when no
    usable stretch exists.

    On a FOLDED frame the inverse solve is branch-ambiguous, and a line
    drawn ACROSS a fold edge has no continuous Third lift at all -
    canvas points 3.85 px apart landed 399 px apart in Third
    (ADD_TOPO_ERROR), and interpolating a profile across that jump
    fabricated a displacement field that folded clean ground into phantom
    layers. Detection compares each step's Third/canvas STRETCH RATIO to
    the line's own overall ratio (the Third parameterization legitimately
    stretches severalfold on curved guides, so no absolute Third scale
    works, and a Third-median test was blind to 2-3 point polylines - a
    straight drag flattens to exactly 2 points). Windows are measured in
    CANVAS arc fractions: a jump inflates the THIRD arc (one 399 px step
    was 80% of it), and jump-inflated fractions named different physical
    stretches on the pair's two sides - the clip then fabricated the very
    field it exists to prevent.
    """
    if len(points) < 2 or len(third) != len(points):
        return None
    c_cum = _cumulative_lengths(points)
    if c_cum[-1] <= 1e-6:
        return None
    t_cum = _cumulative_lengths(third)
    overall = t_cum[-1] / c_cum[-1]
    if len(points) == 2:
        # A single step IS the overall ratio - only an absolute runaway
        # betrays a jump (normal Third stretch stays single-digit even on
        # strongly curved guides; a branch jump multiplies it wholesale).
        return (0.0, 1.0) if overall <= 40.0 else None
    limit = max(6.0 * max(overall, 1.0), 15.0)
    best = (0.0, None)
    run_start = 0
    count = len(points) - 1
    for index in range(count + 1):
        jump = False
        if index < count:
            c_step = max(c_cum[index + 1] - c_cum[index], 0.75)
            t_step = t_cum[index + 1] - t_cum[index]
            jump = t_step > limit * c_step
        if index == count or jump:
            if index > run_start:
                length = c_cum[index] - c_cum[run_start]
                if length > best[0]:
                    best = (length, (c_cum[run_start] / c_cum[-1],
                                     c_cum[index] / c_cum[-1]))
            run_start = index + 1
    return best[1]


def _slice_third_by_canvas_fraction(points, third, f_lo, f_hi):
    """The Third polyline restricted to a canvas-arc-fraction window."""
    c_cum = _cumulative_lengths(points)
    t_cum = _cumulative_lengths(third)
    lo = c_cum[-1] * f_lo
    hi = c_cum[-1] * f_hi

    def third_at(canvas_arc):
        index = bisect.bisect_right(c_cum, canvas_arc) - 1
        index = max(0, min(index, len(points) - 2))
        span = c_cum[index + 1] - c_cum[index]
        t = 0.0 if span <= 1e-9 else (canvas_arc - c_cum[index]) / span
        return _point_at_arc(third, t_cum,
                             t_cum[index] + (t_cum[index + 1] - t_cum[index]) * t)

    inner = [i for i, c in enumerate(c_cum) if lo < c < hi]
    out = [third_at(lo)] + [tuple(third[i]) for i in inner] + [third_at(hi)]
    return out if len(out) >= 2 else []


class _AdditionalWarp:
    """The influence field of the additional (pink) refinement lines.

    THIRD SPACE. The child frame's arc coordinates (l_h, l_v) already form a
    standard Cartesian plane in which both guide axes are straight - the
    flattened view of the surface. Child->Third is `coords`, Main->Third is
    the main frame's arcs divided by the per-side transfer scales, so both
    boards' additional lines land in ONE shared plane.

    A line drawn to hug the surface has the surface's geodesics crossing it
    ORTHOGONALLY; in the flattened Third plane those geodesics are straight,
    so they run along the normal of the line's START-TO-END CHORD. The warp
    therefore works entirely in the chord frame (o, u, n):

      - the drawn pair's difference is the FULL VECTOR profile
        delta(s) = M(s) - C(s) over the chord parameter s, station-matched
        by arc fraction. The geodesic theorem survives in the DECAY
        direction (the chord normal n), not as a projection filter: a
        normal-only profile provably cannot trace a C, whose arms
        displace ALONG the chord;
      - the profile is TRANSLATION-SWEPT along n - every Third point at
        chord parameter s displaces by w(|r - r_c(s)|/R) * delta(s).
        The weight is centred on the DRAWN LINE (r_c(s) = the child line's
        own normal offset at s), not on the chord, so a bowed or hooked
        line receives its full correction on itself. No spine/ridge
        coordinates anywhere: the sweep direction stays one constant
        vector per line, only the falloff centre rides the curve.
      - w is the falloff (tool option): linear 1-x or quadratic (1-x)^2,
        reaching zero at R = radius_factor * max(chord, arc length).
        Beyond the chord's parameter span the profile tapers linearly to
        zero over R, so a line's influence ends smoothly in every
        direction.

    Multiple lines COMPOSE in drawing order: each line is one STAGE whose
    chord frame, geodesics and profile live in the rendering the earlier
    stages already produce (the user's sequencing rule - addition 2 is
    computed on addition 1's space). Lines whose bands never meet behave
    exactly like an independent sum; where they meet, later refines
    earlier instead of acting on a base-space ghost of it. The warp is
    ALLOWED to fold: a delta beyond the fold threshold (R/gain) is the
    drawn line asking the surface to double back, and that is how the
    tool produces occlusion - back faces, creases and stacking. The fold machinery stays consistent through
    det_sign() (multiplied into _orientation) and fold_loci() (the warp's
    own analytic crease curves, injected beside the frame loci with their
    exact child geometry). Chord-reversing shapes are handled by
    SEGMENTATION in _prepare rather than smoothed away, and nothing is
    capped. unapply() is exact outside folded bands (the
    fixed-point rate is |dF/dr| < 1 there) and best-effort inside, where
    the inverse is genuinely multivalued - the consumers that must be
    exact there (cutters, crease probes) carry direct child geometry
    instead.
    """

    SAMPLES = 65
    # NOTHING here clamps the drawn intent any more. The normal derivative
    # is free: |delta| beyond the fold threshold means the drawn line asks
    # the surface to double back, and that FOLD is the point - it produces
    # the occlusion relations, creases and stacking the tool exists for
    # (the earlier fold-free contraction budget smoothed exactly that away,
    # user report). The along-chord shear is free too: segmentation makes
    # every segment chord-monotone, so steep profiles are drawn geometry,
    # not projection junk (an earlier shear cap took 30% off a C's cap).

    def __init__(self, pairs, falloff="linear", radius_factor=0.5):
        """`pairs` must arrive in DRAWING ORDER - each line becomes one
        STAGE and the stages COMPOSE: line k's chord frame, geodesics and
        profile are computed in the space the earlier lines already
        produced (the Rendering after stages 1..k-1), per the user's
        sequencing rule. The child-side thirds arrive in BASE Third
        coordinates and are pushed forward through the chain built so
        far; the main-side thirds are frame arcs unscaled, which is
        already the rendering-space coordinate at draw time (the frame
        inverse never involves the warp). Composition replaced the flat
        sum of all lines: summing evaluated every line's field in base
        coordinates, so a line drawn on top of a standing warp had its
        ask displaced by exactly that warp's shift."""
        self.falloff = falloff if falloff in ADDITIONAL_FALLOFFS else "linear"
        self.gain = 2.0 if self.falloff == "quadratic" else 1.0
        self.stages = []  # one entry per drawn line, in drawing order
        self.pairs = []   # flat view of every stage's segments
        self._loci = None  # fold_loci memo; stages are immutable after init
        for stage_index, (child_third, main_third) in enumerate(pairs):
            if len(child_third) < 2 or len(main_third) < 2:
                continue
            staged_child = [self.apply(tuple(p)) for p in child_third]
            segments = self._prepare(staged_child, main_third, radius_factor)
            if not segments:
                continue
            peak = 0.0
            for segment in segments:
                segment["group"] = stage_index
                peak = max(peak, max(math.hypot(dx, dy)
                                     for dx, dy in zip(segment["delta_x"],
                                                       segment["delta_y"])))
            for segment in segments:
                segment["peak"] = peak
                # How far the chain below this stage can displace a base
                # point INSIDE this segment's scan window - the fold-locus
                # scan widens by this bound. Proximity-GATED, not a blind
                # running sum: an earlier stage whose reach cannot touch
                # the window leaves it identity, so it contributes nothing
                # (the blind sum tripled fold_loci cost and starved short
                # later bands below their own probe density, silently
                # losing their loci). Gating is inductive: the window is
                # inflated by the pad accumulated so far, which bounds
                # where the partial chain can have moved a window point
                # by the time the candidate stage acts on it.
                pad = 0.0
                box = self._band_bbox(segment, 0.0)
                for prior in self.stages:
                    reach = (prior[0]["peak"]
                             + max(p["prefix_pad"] for p in prior))
                    grown = (box[0] - pad, box[1] - pad,
                             box[2] + pad, box[3] + pad)
                    if any(self._boxes_touch(
                            self._band_bbox(p, reach), grown)
                           for p in prior):
                        pad += prior[0]["peak"]
                segment["prefix_pad"] = pad
            self.stages.append(segments)
            self.pairs.extend(segments)

    @staticmethod
    def _band_bbox(segment, extra):
        """Axis-aligned bounds of one segment's influence band (its own
        input space), inflated by `extra` on every side."""
        origin, u, n = segment["origin"], segment["u"], segment["n"]
        radius = segment["radius"]
        s_lo = segment["keys"][0] - radius
        s_hi = segment["keys"][-1] + radius
        r_lo = min(segment["offsets"]) - 1.25 * radius
        r_hi = max(segment["offsets"]) + 1.25 * radius
        xs = []
        ys = []
        for s in (s_lo, s_hi):
            for r in (r_lo, r_hi):
                xs.append(origin[0] + u[0] * s + n[0] * r)
                ys.append(origin[1] + u[1] * s + n[1] * r)
        return (min(xs) - extra, min(ys) - extra,
                max(xs) + extra, max(ys) + extra)

    @staticmethod
    def _boxes_touch(a, b):
        return not (a[2] < b[0] or b[2] < a[0] or a[3] < b[1] or b[3] < a[1])

    @staticmethod
    def _resample(points, count):
        cum = _cumulative_lengths(points)
        total = cum[-1]
        if total <= 1e-9:
            return None
        return [_point_at_arc(points, cum, total * k / (count - 1))
                for k in range(count)]

    @staticmethod
    def _smooth(values, keys, limit):
        """[1,2,1]-smooth until the worst LOCAL slope |dv/ds| fits `limit`.

        The chord projection of a hooked or S-shaped line interleaves its
        branches, manufacturing steep steps that are projection artifacts,
        not drawn geometry - measured 900x slope inflation on a 250-degree
        hook, which then consumed the whole deformation budget and gutted
        every OTHER line's correction. The slope is measured against the
        LOCAL key gap - the same quantity the gradient budget later
        charges; an average-spacing version left a 5x gap between what was
        smoothed and what was billed. Smoothing is best-effort (a monotone
        ramp is a [1,2,1] fixed point); whatever slope remains is charged
        at full value by _pair_gradient, never assumed away.
        """
        for _ in range(64):
            worst = max((abs(b - a) / max(k1 - k0, 1e-6)
                         for a, b, k0, k1 in zip(values, values[1:],
                                                 keys, keys[1:])),
                        default=0.0)
            if worst <= limit:
                break
            values = ([values[0]]
                      + [(values[i - 1] + 2.0 * values[i] + values[i + 1]) * 0.25
                         for i in range(1, len(values) - 1)]
                      + [values[-1]])
        return values

    def _prepare(self, child_third, main_third, radius_factor, depth=0):
        """One drawn pair -> a LIST of chord-monotone sweep segments.

        The translation sweep needs the chord parameter to advance
        monotonically along both sides. A C- or hook-shaped line reverses
        it: its branches interleave under the chord projection, the merge
        averages them, and the drawn shape came out SMOOTHED away (user
        report, arc/chord 1.95). Where either side's chord parameter
        reverses, the pair is SPLIT at the turning stations and each run
        becomes its own sweep with its own chord frame - a C is three
        sweeps, faithful to what was drawn, still spine-free.
        """
        if len(child_third) < 2 or len(main_third) < 2:
            return []
        child_line = self._resample([tuple(p) for p in child_third], self.SAMPLES)
        main_line = self._resample([tuple(p) for p in main_third], self.SAMPLES)
        if child_line is None or main_line is None:
            return []
        # Station k pairs with station k, so the two sides must run the same
        # way; a reversed partner (a right-to-left redraw of the same shape)
        # otherwise pairs fore against aft and fabricates a delta from
        # identical geometry (measured 13.5 px). Chord dot decides - whole
        # lines only: segments inherit the parent's correspondence and must
        # never be re-flipped.
        if depth == 0:
            m_chord = (main_line[-1][0] - main_line[0][0],
                       main_line[-1][1] - main_line[0][1])
            c_chord = (child_line[-1][0] - child_line[0][0],
                       child_line[-1][1] - child_line[0][1])
            if m_chord[0] * c_chord[0] + m_chord[1] * c_chord[1] < 0.0:
                main_line = list(reversed(main_line))
        arc_total = _cumulative_lengths([tuple(p) for p in child_third])[-1]
        origin = child_line[0]
        chord = (child_line[-1][0] - origin[0], child_line[-1][1] - origin[1])
        length = math.hypot(chord[0], chord[1])
        if length <= 1e-6:
            return []  # a closed scribble has no chord frame
        u = (chord[0] / length, chord[1] / length)
        n = (-u[1], u[0])

        # The profile is a function of the CHILD side's chord parameter, so
        # only the child side must be chord-monotone; a C-shaped child line
        # splits at its trend reversals into sub-sweeps. (The main side may
        # curve freely - its shape rides in the vector profile.)
        def reversal(points):
            # First station where the chord parameter's TREND flips. Zero
            # steps (the flat samples at an extremum) are skipped, not
            # treated as breaks - an adjacency test missed every extremum
            # that sampled onto a plateau.
            s_values = [(p[0] - origin[0]) * u[0] + (p[1] - origin[1]) * u[1]
                        for p in points]
            trend = 0
            for k in range(len(s_values) - 1):
                step = s_values[k + 1] - s_values[k]
                if abs(step) <= 1e-6:
                    continue
                sign = 1 if step > 0.0 else -1
                if trend == 0:
                    trend = sign
                elif sign != trend:
                    return k
            return None

        # The depth cap gates the SPLITTING, not the whole call: a capped
        # sub-line falls through to the table build below and is emitted as
        # one un-split best-effort sweep. Capping at the entry returned []
        # for BOTH depth-5 halves, silently zeroing the whole remainder of
        # any line with enough chord reversals (measured 55/120 stations
        # dead on a 340-degree hook, the drawn tail doing nothing at all).
        split = reversal(child_line) if depth <= 4 else None
        if split is not None and 2 <= split <= len(child_line) - 3:
            return (self._prepare(child_line[:split + 1], main_line[:split + 1],
                                  radius_factor, depth + 1)
                    + self._prepare(child_line[split:], main_line[split:],
                                    radius_factor, depth + 1))
        # Per chord station s: the child line's OWN normal offset r_c(s) -
        # the sweep is centred on the DRAWN line, not on the chord - and
        # the pair's FULL VECTOR difference delta(s) = M(s) - C(s). The
        # first version projected delta onto n (deformation strictly along
        # the chord normal), which can only produce graphs over the chord:
        # a C-shaped ask, whose arms displace ALONG the chord, came out
        # smoothed to nothing (user report). The geodesic story survives
        # in the DECAY direction, which stays the chord normal.
        table = []
        for c, m in zip(child_line, main_line):
            s = (c[0] - origin[0]) * u[0] + (c[1] - origin[1]) * u[1]
            r_c = (c[0] - origin[0]) * n[0] + (c[1] - origin[1]) * n[1]
            table.append((s, m[0] - c[0], m[1] - c[1], r_c))
        table.sort(key=lambda entry: entry[0])
        # Collapse near-coincident chord parameters (sampling jitter at an
        # extremum; averaging keeps the profile a function of s).
        merged = []
        span = max(table[-1][0] - table[0][0], 1e-6)
        epsilon = span / (4.0 * self.SAMPLES)
        for s, dx, dy, r_c in table:
            if merged and s - merged[-1][0] < epsilon:
                prev_s, px, py, pr, count = merged[-1]
                merged[-1] = (prev_s,
                              (px * count + dx) / (count + 1),
                              (py * count + dy) / (count + 1),
                              (pr * count + r_c) / (count + 1),
                              count + 1)
            else:
                merged.append((s, dx, dy, r_c, 1))
        if len(merged) < 2:
            return []
        keys = [entry[0] for entry in merged]
        # Light anti-spike smoothing only: segmentation removed the branch
        # interleave that used to need heavy passes, and heavy smoothing
        # was exactly what flattened the drawn shape out of the mapping.
        delta_x = self._smooth([entry[1] for entry in merged], keys, 4.0)
        delta_y = self._smooth([entry[2] for entry in merged], keys, 4.0)
        offsets = self._smooth([entry[3] for entry in merged], keys, 4.0)
        # Reach follows the line's own ARC length, not its chord: a hooked
        # line has a tiny chord but a long presence on the surface.
        radius = max(radius_factor * max(length, arc_total), 1e-6)
        # MULTISCALE SWEEP: the profile tables get a box-blur pyramid, and
        # sampling picks the level whose smoothing length matches the
        # probe's normal distance from the drawn line. A translation sweep
        # carries the profile VERBATIM across the whole band, so a
        # high-frequency line (a 427-degree coil, ADD_TOPO_ERROR) printed
        # its oscillation onto flat ground 200+ px away and folded it into
        # eight phantom-yet-real creases; physically, distance smooths -
        # far ground feels only the line's average ask (the heat-kernel
        # picture of the geodesic influence). On the line itself blur is
        # zero and every existing behavior is bit-identical.
        step_med = max((keys[-1] - keys[0]) / max(len(keys) - 1, 1), 1e-6)

        def box(values, half):
            prefix = [0.0]
            for v in values:
                prefix.append(prefix[-1] + v)
            out = []
            n = len(values)
            for i in range(n):
                lo = max(0, i - half)
                hi = min(n - 1, i + half)
                out.append((prefix[hi + 1] - prefix[lo]) / (hi - lo + 1))
            return out

        # Half-widths 1,2,4,...: the first level is a light touch, so the
        # exact->blurred onset transition carries a SMALL profile change
        # over its ramp (a coarse first level compressed the whole change
        # into a thin shell around the line whose normal gradient itself
        # read as a fold).
        blur_levels = [(delta_x, delta_y, offsets)]
        for k in range(7):
            half = 2 ** k
            prev = blur_levels[-1]
            blur_levels.append(tuple(box(v, half) for v in prev))
        # How far the blurred sweep centre can drift from the exact one -
        # the support early-out pads by it, or det_sign would short-cut
        # inside a band whose live centre moved past the raw slack.
        rc_span = max((max(abs(a - b) for a, b in zip(level[2], offsets))
                       for level in blur_levels[1:]), default=0.0)
        # The sweep centre's actual worst slope (smoothing is best-effort,
        # so 4.0 is a target, not a bound) - _in_support pads with it.
        offset_slope = max((abs(b - a) / max(k1 - k0, 1e-6)
                            for a, b, k0, k1 in zip(offsets, offsets[1:],
                                                    keys, keys[1:])),
                           default=0.0)
        return [{"origin": origin, "u": u, "n": n, "radius": radius,
                 "keys": keys, "delta_x": delta_x, "delta_y": delta_y,
                 "offsets": offsets, "offset_slope": offset_slope,
                 "blur_levels": blur_levels, "blur_step": step_med,
                 "blur_rc_span": rc_span}]

    def jacobian(self, third, step=0.25):
        """2x2 Jacobian of apply() by the CHAIN RULE: the product of
        per-stage central-difference Jacobians, each taken at that
        stage's own input point with a step calibrated to that stage's
        radius. Differencing the whole composite in base coordinates
        amplified the probe step by the earlier stages' gradients and
        straddled a later stage's band - measured 2.85% of probes
        reporting phantom folds (det<0 where the converged determinant
        is strongly positive) at an earlier-stage gradient of 20, and
        the chain-rule form drops that to 0 with the same eval count."""
        z = (third[0], third[1])
        jac = (1.0, 0.0, 0.0, 1.0)
        for segments in self.stages:
            h = min(step, 0.25 * min(s["radius"] for s in segments))
            a, b, c, d = self._stage_jacobian(segments, z, h)
            jac = (a * jac[0] + b * jac[2], a * jac[1] + b * jac[3],
                   c * jac[0] + d * jac[2], c * jac[1] + d * jac[3])
            dx, dy = self._stage_displacement(segments, z)
            z = (z[0] + dx, z[1] + dy)
        return jac

    def _in_support(self, third, pad):
        """Can any pair move any point within `pad` of `third`? Exact when
        False: the s test is padded by the taper reach plus pad, and the r
        test by pad times (1 + the pair's ACTUAL worst sweep-centre slope,
        stored at build - smoothing only targets 4.0), so displacement is
        identically (0, 0) over the whole difference stencil."""
        for pair in self.pairs:
            rel = (third[0] - pair["origin"][0], third[1] - pair["origin"][1])
            s = rel[0] * pair["u"][0] + rel[1] * pair["u"][1]
            radius = pair["radius"]
            if not (pair["keys"][0] - radius - pad
                    <= s <= pair["keys"][-1] + radius + pad):
                continue
            _dx, _dy, r_c, _taper = self._sample(pair, s)
            r = rel[0] * pair["n"][0] + rel[1] * pair["n"][1]
            slack = (pad * (1.5 + pair.get("offset_slope", 0.0))
                     + pair.get("blur_rc_span", 0.0))
            if abs(r - r_c) < radius + slack:
                return True
        return False

    def det_sign(self, third):
        """Sign of det D(apply) - the warp's own fold parity at a point.

        Outside every pair's influence band the map is the identity over
        the whole difference stencil, so the answer is 1 without touching
        the Jacobian - ~96% of overlay and orientation probes on a real
        document end there (measured; the unconditional version was a 5x
        per-point regression)."""
        if not self._in_support(third, 0.25):
            return 1
        a, b, c, d = self.jacobian(third)
        return 1 if a * d - b * c >= 0.0 else -1

    def fold_loci(self, samples=48):
        """The warp's OWN fold loci as Third-space curves, traced
        numerically: per pair band, det D(apply) sign changes are bisected
        along the chord-normal direction and chained column-to-column by
        nearest band coordinate. Numeric on purpose - the vector profile
        and the group blending have no usable closed form, and tracing the
        SAME det_sign field the pointwise orientation uses means loci and
        cuts cannot disagree. Cost: ~4k det probes per pair (grid plus
        bisections) and the det field sums every pair, so a segmented line
        pays quadratically - hundreds of ms; memoized, so a mapping run
        pays it once.

        The bands of one line's segments (and of separate lines) overlap
        while the det field is GLOBAL, so the same locus gets traced once
        per band it lies in; the duplicates would double depth counts and
        double-draw creases downstream. Dedupe in THIRD space - the
        domain - because a folded sheet legitimately superimposes distinct
        loci in the image (same rationale as _uncovered_runs' arc-space
        coverage).

        A later STAGE's band is expressed in its own input space, but the
        scan runs over base Third coordinates: the base preimage of the
        band is the band displaced by at most prefix_pad - the summed
        peaks of the earlier stages that can actually REACH the window
        (proximity-gated at build) - so the scan window widens by that
        bound while the probe counts scale to keep the unpadded step
        size."""
        if self._loci is not None:
            # Copies: _with_warp_loci registers curves by id() in
            # map_point.warp_curve_child, so each call must hand out
            # distinct list objects.
            return [list(curve) for curve in self._loci]
        traced = []
        for pair in self.pairs:
            origin, u, n = pair["origin"], pair["u"], pair["n"]
            radius = pair["radius"]
            pad = pair.get("prefix_pad", 0.0)
            span_lo = pair["keys"][0] - radius - pad
            span_hi = pair["keys"][-1] + radius + pad
            base_span = max(pair["keys"][-1] - pair["keys"][0] + 2.0 * radius,
                            1e-6)
            # The pad widens the window; the probe count follows so the
            # step size inside the segment's own band NEVER coarsens (a
            # 120 cap once starved a short later band below 2 columns and
            # silently lost its locus; the proximity gate keeps pads - and
            # so these counts - small unless stages genuinely overlap).
            columns = min(400, max(samples, int(math.ceil(
                samples * (span_hi - span_lo) / base_span))))

            def third_at(s, r):
                return (origin[0] + u[0] * s + n[0] * r,
                        origin[1] + u[1] * s + n[1] * r)

            # Chaining by nearest row under a slope gate. The old rule -
            # append by array rank whenever the crossing COUNT matched -
            # welded unrelated loci with long straight jumps through
            # sign-constant territory whenever one locus left the band as
            # another entered (measured a 240 px phantom "locus" wholly in
            # det>0 ground), and shredded real loci into 2-point stubs on
            # every count change (a third of the columns).
            # The chaining gate follows the segment's OWN feature scale
            # (core step, not the padded span): a pad-inflated tol once
            # exceeded the separation between a short segment's two real
            # loci - exactly the weld this gate exists to prevent.
            tol = min(max(4.0 * base_span / samples, 0.05 * radius),
                      0.5 * radius)

            # Row resolution must resolve the blur ramp's normal feature
            # scale, not just the radius: the multiscale field varies
            # along the normal at the blur-step scale, and rows coarser
            # than that reported back faces with no traceable locus.
            row_step = min(2.5 * radius / 40.0,
                           4.0 * pair.get("blur_step", radius))

            def run_sweep(col_lo, col_hi, col_count, row_bounds, point_at):
                open_curves = []  # [points, last_row]
                for k in range(col_count + 1):
                    col = col_lo + (col_hi - col_lo) * k / col_count
                    row_lo, row_hi = row_bounds(col)
                    steps = min(400, max(40, int(math.ceil(
                        (row_hi - row_lo) / max(row_step, 1e-6)))))
                    crossings = []
                    prev_sign = None
                    prev_row = None
                    for j in range(steps + 1):
                        row = row_lo + (row_hi - row_lo) * j / steps
                        sign = self.det_sign(point_at(col, row))
                        if prev_sign is not None and sign != prev_sign:
                            a, b = prev_row, row
                            for _ in range(18):
                                mid = (a + b) * 0.5
                                if self.det_sign(point_at(col, mid)) == prev_sign:
                                    a = mid
                                else:
                                    b = mid
                            crossings.append((a + b) * 0.5)
                        prev_sign = sign
                        prev_row = row
                    taken = [False] * len(open_curves)
                    matches = []
                    for ci, crossing in enumerate(crossings):
                        best = None
                        for oi, (_points, last_row) in enumerate(open_curves):
                            gap = abs(crossing - last_row)
                            if gap <= tol and (best is None or gap < best[0]):
                                if not taken[oi]:
                                    best = (gap, oi)
                        matches.append(best[1] if best else None)
                        if best:
                            taken[best[1]] = True
                    survivors = []
                    for oi, (points, _last_row) in enumerate(open_curves):
                        if not taken[oi]:
                            if len(points) >= 2:
                                traced.append(points)
                    for ci, crossing in enumerate(crossings):
                        oi = matches[ci]
                        if oi is None:
                            survivors.append([[point_at(col, crossing)],
                                              crossing])
                        else:
                            open_curves[oi][0].append(point_at(col, crossing))
                            open_curves[oi][1] = crossing
                            survivors.append(open_curves[oi])
                    open_curves = survivors
                for points, _last_row in open_curves:
                    if len(points) >= 2:
                        traced.append(points)

            # TWO sweep directions, like the frame creases' h/v sweeps: a
            # locus stretch running ALONG the scan columns is invisible to
            # that sweep - the single s-sweep missed the near-vertical
            # stretches that close a fold band at its ends, leaving a
            # depth/side boundary with no locus, no cutter and no crease
            # (measured: a parity step 9.7 px from every cutter). The
            # orthogonal sweep traces them; the Third-space dedupe merges
            # the overlap.
            def s_rows(s):
                _dx, _dy, r_c, _taper = self._sample(pair, s)
                return (r_c - 1.25 * radius - pad, r_c + 1.25 * radius + pad)

            run_sweep(span_lo, span_hi, columns, s_rows,
                      lambda s, r: third_at(s, r))
            r_all_lo = min(pair["offsets"]) - 1.25 * radius - pad
            r_all_hi = max(pair["offsets"]) + 1.25 * radius + pad
            r_columns = min(400, max(samples, int(math.ceil(
                samples * (r_all_hi - r_all_lo) / (2.5 * radius)))))
            run_sweep(r_all_lo, r_all_hi, r_columns,
                      lambda _r: (span_lo, span_hi),
                      lambda r, s: third_at(s, r))
        grid = _ArcGrid(4.0 * POLY_STEP)
        curves = []
        for curve in sorted(traced, key=len, reverse=True):
            for run in _uncovered_runs(curve, grid, POLY_STEP):
                if len(run) >= 2:
                    curves.append(run)
                    grid.add_curve(run)
        self._loci = curves
        return [list(curve) for curve in curves]

    def _sample(self, pair, s, blur=0.0):
        """(delta_x, delta_y, r_c, taper) at chord parameter s.

        The end taper is returned SEPARATELY, not multiplied into the
        deltas: displacement() folds it into the WEIGHT so that a
        tapered-out segment leaves the group's partition-of-unity
        denominator at the same rate its contribution vanishes. Tapering
        the delta alone let the weight drop out of the denominator
        discontinuously at the taper edge - a C0 tear of up to half the
        applied delta along a whole curve (measured 35 px on a C drawn on
        the child board), which det_sign then reported as phantom folds.

        `blur` = the probe's normal distance from the drawn line: the
        profile is read from the box-blur pyramid level whose smoothing
        length matches it (interpolated BETWEEN levels - a level switch
        would itself be a seam), so far ground feels only the line's
        smoothed ask and a high-frequency coil no longer folds flat
        ground hundreds of px away. blur=0 reads the exact tables.
        """
        table_keys = pair["keys"]
        lo, hi = table_keys[0], table_keys[-1]
        taper = 1.0
        if s < lo:
            taper = max(0.0, 1.0 - (lo - s) / pair["radius"])
            s = lo
        elif s > hi:
            taper = max(0.0, 1.0 - (s - hi) / pair["radius"])
            s = hi
        index = bisect.bisect_right(table_keys, s) - 1
        index = max(0, min(index, len(table_keys) - 2))
        s0, s1 = table_keys[index], table_keys[index + 1]
        t = 0.0 if s1 - s0 <= 1e-9 else (s - s0) / (s1 - s0)

        levels = pair.get("blur_levels")
        if levels is None or blur <= 4.0 * pair.get("blur_step", 0.0):
            def lerp(values):
                return values[index] + (values[index + 1] - values[index]) * t

            return (lerp(pair["delta_x"]),
                    lerp(pair["delta_y"]),
                    lerp(pair["offsets"]),
                    taper)
        # Window length ~ the normal distance itself, and the ramp starts
        # gently (onset at 4 steps, one level per octave): wavelengths
        # longer than the distance survive - a plain bow keeps most of its
        # far reach - while the level-to-level change stays small enough
        # that the ramp's own normal gradient cannot read as a fold.
        level = max(0.0, math.log2(blur / pair["blur_step"]) - 2.0)
        k0 = min(int(level), len(levels) - 1)
        k1 = min(k0 + 1, len(levels) - 1)
        frac = min(max(level - k0, 0.0), 1.0) if k1 > k0 else 0.0

        def lerp2(low, high):
            a = low[index] + (low[index + 1] - low[index]) * t
            b = high[index] + (high[index + 1] - high[index]) * t
            return a + (b - a) * frac

        return (lerp2(levels[k0][0], levels[k1][0]),
                lerp2(levels[k0][1], levels[k1][1]),
                lerp2(levels[k0][2], levels[k1][2]),
                taper)

    def _weight(self, x):
        if x >= 1.0:
            return 0.0
        return (1.0 - x) ** 2 if self.falloff == "quadratic" else 1.0 - x

    def _stage_displacement(self, segments, third):
        """ONE stage's field at `third` (the stage's own input space): a
        partition-of-unity blend of that drawn line's chord-monotone
        segments - adjacent segments overlap near their junction, and a
        plain sum double-counted the shared delta there (measured 54 px
        off the drawn C). Where the weights sum below 1 the plain sum
        stands, so a single-segment line is untouched."""
        vx = vy = weight_sum = 0.0
        for pair in segments:
            rel = (third[0] - pair["origin"][0], third[1] - pair["origin"][1])
            s = rel[0] * pair["u"][0] + rel[1] * pair["u"][1]
            delta_x, delta_y, r_c, taper = self._sample(pair, s)
            r = rel[0] * pair["n"][0] + rel[1] * pair["n"][1]
            blur = abs(r - r_c)
            if blur > 4.0 * pair.get("blur_step", 0.0):
                # Far from the line the profile is read blurred; the
                # first (exact) sample only located the sweep centre.
                delta_x, delta_y, r_c, taper = self._sample(pair, s, blur)
            # The taper scales the WEIGHT, not the delta: numerator and
            # denominator must vanish together or the normalizer tears.
            weight = self._weight(abs(r - r_c) / pair["radius"]) * taper
            if weight <= 0.0:
                continue
            vx += weight * delta_x
            vy += weight * delta_y
            weight_sum += weight
        if weight_sum > 1.0:
            vx /= weight_sum
            vy /= weight_sum
        return vx, vy

    def apply(self, third):
        """The COMPOSITE: stages in drawing order, each evaluated at the
        point the earlier stages already produced. Lines whose bands do
        not meet behave exactly like the old flat sum (each acts where
        the others are identity); where they meet, a later line refines
        the earlier line's rendering instead of a base-space copy of it."""
        z = (third[0], third[1])
        for segments in self.stages:
            dx, dy = self._stage_displacement(segments, z)
            if dx != 0.0 or dy != 0.0:
                z = (z[0] + dx, z[1] + dy)
        return z

    def displacement(self, third):
        z = self.apply(third)
        return (z[0] - third[0], z[1] - third[1])

    def _stage_jacobian(self, segments, third, step):
        xp = self._stage_displacement(segments, (third[0] + step, third[1]))
        xm = self._stage_displacement(segments, (third[0] - step, third[1]))
        yp = self._stage_displacement(segments, (third[0], third[1] + step))
        ym = self._stage_displacement(segments, (third[0], third[1] - step))
        return (1.0 + (xp[0] - xm[0]) / (2.0 * step),
                (yp[0] - ym[0]) / (2.0 * step),
                (xp[1] - xm[1]) / (2.0 * step),
                1.0 + (yp[1] - ym[1]) / (2.0 * step))

    def _stage_iterate(self, segments, third, seed, iterations):
        x = seed
        best = None
        newton_h = min(0.1, 0.25 * min(s["radius"] for s in segments))
        for it in range(iterations):
            dx, dy = self._stage_displacement(segments, x)
            rx = x[0] + dx - third[0]
            ry = x[1] + dy - third[1]
            res = math.hypot(rx, ry)
            if best is None or res < best[0]:
                best = (res, x)
            if res < 1e-9:
                return best
            fp = (third[0] - dx, third[1] - dy)
            if it < 8:
                x = fp
                continue
            # Newton polish: fixed-point stalls where the local gradient
            # nears 1 (the fold threshold); the true Jacobian does not.
            # Guarded: the field is piecewise, so a raw Newton step can
            # catapult - keep it only if it beats the fixed-point residual.
            jxx, jxy, jyx, jyy = self._stage_jacobian(segments, x, newton_h)
            det = jxx * jyy - jxy * jyx
            if abs(det) < 1e-6:
                x = fp
                continue
            cand = (x[0] - (jyy * rx - jxy * ry) / det,
                    x[1] - (jxx * ry - jyx * rx) / det)
            cdx, cdy = self._stage_displacement(segments, cand)
            cres = math.hypot(cand[0] + cdx - third[0],
                              cand[1] + cdy - third[1])
            x = cand if cres < res else fp
        return best

    def _stage_unapply(self, segments, third):
        best = self._stage_iterate(segments, third, third, 60)
        if best[0] < 1e-6:
            return best[1]
        # Near a fold apex the fixed point oscillates around the target
        # and the guarded Newton never beats it, so the plain iteration
        # can return a NON-preimage whose residual then reads as drawn
        # deformation downstream (a straight synced line became a
        # full-strength deformer). A true preimage exists (the folded
        # sheet's image covers the band) - restart from seeds offset by
        # fractions of each segment's local ask and keep the best.
        for pair in segments:
            rel = (third[0] - pair["origin"][0], third[1] - pair["origin"][1])
            s = rel[0] * pair["u"][0] + rel[1] * pair["u"][1]
            delta_x, delta_y, _r_c, taper = self._sample(pair, s)
            if abs(delta_x) < 1e-9 and abs(delta_y) < 1e-9:
                continue
            for fraction in (0.5, 1.0, 1.5):
                seed = (third[0] - fraction * delta_x,
                        third[1] - fraction * delta_y)
                candidate = self._stage_iterate(segments, third, seed, 30)
                if candidate[0] < best[0]:
                    best = candidate
                if best[0] < 1e-6:
                    return best[1]
        return best[1]

    def unapply(self, third):
        """Inverse of the composite: stages inverted in REVERSE drawing
        order, each by fixed-point iteration with a guarded Newton
        polish on that single stage's field. Exact outside folded bands
        (each stage's iteration rate is its own |dF/dr| < 1 there);
        inside a folded band the inverse is genuinely multivalued and
        this returns a best-effort branch - the consumers that need
        exactness there (warp-locus cutters and probes) carry direct
        child geometry instead."""
        z = third
        for segments in reversed(self.stages):
            z = self._stage_unapply(segments, z)
        return z


def build_mapper(child_h_spec, child_v_spec, main_h_spec, main_v_spec, info=None,
                 additional_pairs=None):
    """Build point mapper from the child frame to the main frame.

    Each guide spec is a bare point list (polyline guide - chord arithmetic,
    bit-identical to the previous implementation) or an asset dict whose
    optional "commands" carry the stroke's real mixed line/Bezier path, on
    which the frame then evaluates with Gauss-Legendre arc tables (_Curve).

    Returns (map_point, width_scale) or (None, reason).

    DECOUPLED FORM (user request 2026-08-10): both boards go through the SAME
    reconstruction function `_Frame.hv(l_h, l_v) = H(l_h) + V(l_v) - O`, and
    the map is

        Phi(p) = p - hv_child(l_h, l_v) + hv_main(s_h*l_h, s_v*l_v)

    where (l_h, l_v) are p's ARC-LENGTH coordinates in the child frame,
    obtained by inverting hv_child (Newton from the cheap chord-basis guess).

    Two properties the previous chord-only formulation did not have:
      * IDENTITY. Draw the same guides on both boards and Phi is the identity,
        for any guide shape. The old form decomposed p on the child CHORDS and
        rebuilt it on the main ARC, so a curved child guide displaced the whole
        pattern by tens of pixels (measured: 28 px at amplitude 10, 157 px at
        amplitude 60) even when both boards were identical.
      * The child guides' curvature actually participates. Previously only
        their two endpoints did (`eh`/`ev` were half-chords), so drawing a
        curved child center line silently discarded its shape AND injected
        that shape as an off-axis displacement.

    The residual `p - hv_child(...)` is zero wherever the inverse converged;
    it stays in the formula as the fallback that keeps the map defined (and
    identity-preserving) on degenerate cells and outside the frame's coverage.

    Parametrization is still ENDPOINT-ANCHORED: the crossing splits each guide
    into two sides and each child side maps proportionally onto the matching
    main side, so endpoints land on endpoints and the crossing on the crossing.
    Differing side ratios still fold strokes that cross a guide (info dict, if
    given, receives h/v_scale_mismatch). Stroke width uses one global
    geometric-mean scale - an approximation once the sides differ.
    """
    def spec_points(spec):
        return spec["points"] if isinstance(spec, dict) else spec

    child_h_points = spec_points(child_h_spec)
    child_v_points = spec_points(child_v_spec)
    main_h_points = spec_points(main_h_spec)
    main_v_points = spec_points(main_v_spec)
    if min(len(child_h_points), len(child_v_points), len(main_h_points), len(main_v_points)) < 2:
        return None, "a center line has fewer than 2 points"

    child = _Frame(child_h_spec, child_v_spec)
    main = _Frame(main_h_spec, main_v_spec)
    if child.h_total <= 1e-9 or child.v_total <= 1e-9:
        return None, "child center lines are degenerate"
    if main.h_total <= 1e-9 or main.v_total <= 1e-9:
        return None, "main center lines are degenerate"

    # The chord basis is no longer the coordinate system, but it still seeds
    # the inverse - and a near-parallel child pair has no usable frame at all,
    # so keep rejecting it here (same angle test, same message as before).
    eh = ((child_h_points[-1][0] - child_h_points[0][0]) * 0.5,
          (child_h_points[-1][1] - child_h_points[0][1]) * 0.5)
    ev = ((child_v_points[-1][0] - child_v_points[0][0]) * 0.5,
          (child_v_points[-1][1] - child_v_points[0][1]) * 0.5)
    det = eh[0] * ev[1] - eh[1] * ev[0]
    axis_sin = abs(det) / max(1e-9, math.hypot(eh[0], eh[1]) * math.hypot(ev[0], ev[1]))
    if axis_sin < 0.05:  # ~3 degrees
        return None, "child center lines are (nearly) parallel"
    child_h_chord = max(2.0 * math.hypot(eh[0], eh[1]), 1e-6)
    child_v_chord = max(2.0 * math.hypot(ev[0], ev[1]), 1e-6)

    h_scale_neg, h_scale_pos = _transfer_scales(child.h_side_raw, child.h_total, main.h_side)
    v_scale_neg, v_scale_pos = _transfer_scales(child.v_side_raw, child.v_total, main.v_side)

    if info is not None:
        info["h_scale_mismatch"] = (max(h_scale_neg, h_scale_pos)
                                    / max(1e-9, min(h_scale_neg, h_scale_pos)))
        info["v_scale_mismatch"] = (max(v_scale_neg, v_scale_pos)
                                    / max(1e-9, min(v_scale_neg, v_scale_pos)))
        # Handedness: the map is direction-faithful on both axes (child start
        # -> main start, end -> end), so the result is a MIRROR image exactly
        # when the two frames have opposite orientation. Both sides now use
        # the tangents at their own crossing, which is the same quantity for
        # both boards instead of chord-vs-tangent as before.
        child_cross_h, child_cross_v = child.jacobian(0.0, 0.0)
        main_cross_h, main_cross_v = main.jacobian(0.0, 0.0)
        child_cross = child_cross_h[0] * child_cross_v[1] - child_cross_h[1] * child_cross_v[0]
        main_cross = main_cross_h[0] * main_cross_v[1] - main_cross_h[1] * main_cross_v[0]
        info["mirrored"] = (child_cross > 0.0) != (main_cross > 0.0)

    def coords(point, seed=None):
        """A point's arc-length coordinates in the child frame.

        Seeded with the cheap chord-basis estimate, which is exact for
        straight child guides - that is why the whole mapper reduces to the
        previous implementation there. An explicit `seed` (child arcs)
        overrides it: on a FOLDED frame the solve is branch-ambiguous, and
        the caller may know which preimage it means (additional-line
        redraws seed from the replaced line's stored coordinates).
        """
        if seed is not None:
            return child.solve(point, seed[0], seed[1])
        dx = point[0] - child.origin[0]
        dy = point[1] - child.origin[1]
        guess_h = (dx * ev[1] - dy * ev[0]) / det * 0.5 * child_h_chord
        guess_v = (eh[0] * dy - eh[1] * dx) / det * 0.5 * child_v_chord
        return child.solve(point, guess_h, guess_v)

    # The MAIN frame's inverse, mirror of coords: needed to carry points
    # drawn on the main board (additional lines) into Third space.
    m_eh = ((main_h_points[-1][0] - main_h_points[0][0]) * 0.5,
            (main_h_points[-1][1] - main_h_points[0][1]) * 0.5)
    m_ev = ((main_v_points[-1][0] - main_v_points[0][0]) * 0.5,
            (main_v_points[-1][1] - main_v_points[0][1]) * 0.5)
    m_det = m_eh[0] * m_ev[1] - m_eh[1] * m_ev[0]
    m_h_chord = max(2.0 * math.hypot(m_eh[0], m_eh[1]), 1e-6)
    m_v_chord = max(2.0 * math.hypot(m_ev[0], m_ev[1]), 1e-6)

    def main_coords(point, seed=None):
        if seed is not None:
            return main.solve(point, seed[0], seed[1])
        dx = point[0] - main.origin[0]
        dy = point[1] - main.origin[1]
        if abs(m_det) > 1e-9:
            guess_h = (dx * m_ev[1] - dy * m_ev[0]) / m_det * 0.5 * m_h_chord
            guess_v = (m_eh[0] * dy - m_eh[1] * dx) / m_det * 0.5 * m_v_chord
        else:
            guess_h = guess_v = 0.0
        return main.solve(point, guess_h, guess_v)

    def scale_arcs(l_h, l_v):
        """Third (child-arc) coordinates -> main arcs."""
        return (l_h * (h_scale_pos if l_h >= 0.0 else h_scale_neg),
                l_v * (v_scale_pos if l_v >= 0.0 else v_scale_neg))

    def unscale_arcs(arc_h, arc_v):
        """Main arcs -> Third (child-arc) coordinates; scales are positive,
        so the per-side selection is sign-consistent both ways."""
        return (arc_h / (h_scale_pos if arc_h >= 0.0 else h_scale_neg),
                arc_v / (v_scale_pos if arc_v >= 0.0 else v_scale_neg))

    # The additional (pink) lines' influence field, built in Third space
    # from each pair's two boards. `additional_pairs` arrives in DRAWING
    # ORDER and the warp stages COMPOSE in that order - each later line
    # is computed in the rendering the earlier ones produce (child-side
    # thirds are base coordinates, pushed forward inside _AdditionalWarp;
    # main-side thirds are frame arcs unscaled, already rendering-space).
    # A line's STORED Third polyline is authoritative when present: on a
    # folded frame the inverse solve is branch-ambiguous, and re-deriving
    # a synced partner through it was measured to fabricate a 278 px
    # delta on a pair the user never edited. Solving happens only for
    # legacy assets that carry no coordinates. None when there are no
    # pairs - and then every code path below is arithmetic-identical to
    # the warp-free mapper.
    warp = None
    additional_notes = []
    if additional_pairs:
        thirds = []
        for line_index, (child_item, main_item) in enumerate(additional_pairs):
            child_item = child_item or {}
            main_item = main_item or {}
            child_points = [tuple(p) for p in child_item.get("points") or []]
            main_points = [tuple(p) for p in main_item.get("points") or []]
            if len(child_points) < 2 or len(main_points) < 2:
                continue
            child_third = [tuple(t) for t in child_item.get("third") or []]
            if len(child_third) != len(child_points):
                child_third = [coords(p) for p in child_points]
            main_third = [tuple(t) for t in main_item.get("third") or []]
            if len(main_third) != len(main_points):
                main_third = [unscale_arcs(*main_coords(p)) for p in main_points]
            # BRANCH-CONTINUITY CLIP: a line drawn across a fold edge of
            # its frame has no continuous Third lift - the solve jumps
            # branches mid-line and cross-branch deltas are meaningless.
            # Keep the window (canvas arc fractions, jump-proof and
            # commensurate across the two boards) where BOTH sides are
            # continuous; drop the rest, loudly.
            windows = [_branch_window(child_points, child_third),
                       _branch_window(main_points, main_third)]
            if any(w is None for w in windows):
                additional_notes.append(
                    f"additional line {line_index + 1} has no continuous "
                    "Third lift (it crosses a fold edge of its frame) - "
                    "it cannot shape the mapping and was skipped")
                continue
            f_lo = max(w[0] for w in windows)
            f_hi = min(w[1] for w in windows)
            if f_lo > 1e-9 or f_hi < 1.0 - 1e-9:
                if f_hi - f_lo < 1e-6:
                    additional_notes.append(
                        f"additional line {line_index + 1} crosses fold "
                        "edges everywhere - it was skipped")
                    continue
                child_third = _slice_third_by_canvas_fraction(
                    child_points, child_third, f_lo, f_hi)
                main_third = _slice_third_by_canvas_fraction(
                    main_points, main_third, f_lo, f_hi)
                additional_notes.append(
                    f"additional line {line_index + 1} crosses a fold edge "
                    f"of its frame; only its longest continuous stretch "
                    f"({100.0 * (f_hi - f_lo):.0f}% of the drawn length) "
                    "shapes the mapping")
            if len(child_third) < 2 or len(main_third) < 2:
                continue
            thirds.append((child_third, main_third))
        if thirds:
            candidate = _AdditionalWarp(thirds, _ADDITIONAL["falloff"],
                                        _ADDITIONAL["radius_factor"])
            warp = candidate if candidate.pairs else None

    def map_point(point):
        l_h, l_v = coords(point)
        rebuilt = child.hv(l_h, l_v)
        if warp is not None:
            l_h, l_v = warp.apply((l_h, l_v))
        image = main.hv(l_h * (h_scale_pos if l_h >= 0.0 else h_scale_neg),
                        l_v * (v_scale_pos if l_v >= 0.0 else v_scale_neg))
        return (image[0] + point[0] - rebuilt[0],
                image[1] + point[1] - rebuilt[1])

    def inverse_point(point):
        """Main-board point -> child-board point through the full mapping
        (frame inverse, then the warp's fixed-point inverse). Exact wherever
        both frame solves converge; used to sync additional lines."""
        third = unscale_arcs(*main_coords(point))
        if warp is not None:
            third = warp.unapply(third)
        return child.hv(*third)

    # Arc lengths on both sides now, so a curved child guide scales widths by
    # its real length rather than by its chord.
    width_scale = math.sqrt((main.h_total / child.h_total) * (main.v_total / child.v_total))

    map_point.coords = coords
    map_point.main_coords = main_coords
    map_point.scale_arcs = scale_arcs
    map_point.unscale_arcs = unscale_arcs
    map_point.inverse = inverse_point
    map_point.warp = warp
    map_point.additional_notes = additional_notes
    map_point.child_frame = child
    map_point.main_frame = main
    map_point.h_scales = (h_scale_neg, h_scale_pos)
    map_point.v_scales = (v_scale_neg, v_scale_pos)
    # Front/back is measured against the crossing, so the orientation there is
    # the anchor (see _fold_sign). Computed once here rather than per point.
    map_point.fold_reference = 1
    map_point.fold_reference = _orientation(map_point, child.origin) or 1
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
            # Evaluation via the shared wheel pyfile/bezier.py; the 6px /
            # 4..24 density is this sampler's own policy.
            cub = (current[-1],
                   _command_point(command["control1"]),
                   _command_point(command["control2"]),
                   _command_point(command["to"]))
            samples = max(4, min(24, int(math.ceil(bezier.hull_length(cub) / 6.0))))
            for step in range(1, samples + 1):
                current.append(_cubic_point(cub, step / samples))
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


def _segment_polygon_crossings(a, b, polygons):
    """Parameters t in (0,1) where segment a->b crosses a polygon edge.

    Exact: one 2x2 solve per edge. The previous implementation only tested
    the polyline's own vertices and bisected between an inside and an outside
    sample, so the clip resolution was the SAMPLING density - a region
    narrower than the vertex spacing, or a stroke clipping a corner between
    two samples, was simply never seen.
    """
    abx, aby = b[0] - a[0], b[1] - a[1]
    if abx * abx + aby * aby <= 1e-24:
        return []
    crossings = []
    for polygon in polygons:
        count = len(polygon)
        for index in range(count):
            e0 = polygon[index]
            e1 = polygon[(index + 1) % count]
            ex, ey = e1[0] - e0[0], e1[1] - e0[1]
            den = abx * ey - aby * ex
            if abs(den) <= 1e-12:
                continue  # parallel (a grazing overlap changes no inside/outside run)
            wx, wy = e0[0] - a[0], e0[1] - a[1]
            t = (wx * ey - wy * ex) / den
            u = (wx * aby - wy * abx) / den
            if 0.0 < t < 1.0 and -1e-12 <= u <= 1.0 + 1e-12:
                crossings.append(t)
    return crossings


def _clip_runs(a, b, polygons):
    """Sub-intervals [t0, t1] of segment a->b with a definite inside/outside.

    Yields (t0, t1, inside). Between two consecutive crossings the segment
    cannot change side, so one midpoint test decides the whole run.
    """
    bounds = sorted(set(_segment_polygon_crossings(a, b, polygons)))
    bounds = [0.0] + bounds + [1.0]
    for t0, t1 in zip(bounds, bounds[1:]):
        if t1 - t0 <= 1e-12:
            continue
        mid = (t0 + t1) * 0.5
        point = (a[0] + (b[0] - a[0]) * mid, a[1] + (b[1] - a[1]) * mid)
        yield t0, t1, _point_in_polygons(point, polygons)


def _clip_polyline(points, polygons):
    """Split a polyline into the pieces that lie inside the polygons."""
    if not polygons:
        return [points]
    pieces = []
    current = []
    for a, b in zip(points, points[1:]):
        for t0, t1, inside in _clip_runs(a, b, polygons):
            if not inside:
                if len(current) >= 2:
                    pieces.append(current)
                current = []
                continue
            if not current:
                current.append((a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0))
            current.append((a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1))
    if len(current) >= 2:
        pieces.append(current)
    return pieces


# ---------------------------------------------------------------------------
# curve reconstruction (spline / bezier modes)
#
# The whole point of these helpers is that build_mapper.map_point (W below) is
# NON-LINEAR: W(straight source segment) is a curve, so joining mapped points
# with straight lines (the old "polyline" mode) drops the distortion between
# points. W is only PIECEWISE smooth (arc-length lookup on polyline guides is
# C0 at guide vertices; the per-side scales jump across the guide crossing), so
# every routine below is subdivision-based with a depth cap and never assumes a
# globally smooth map: a genuine tangent kink just makes the segments there
# small instead of sending the recursion to infinity.
# ---------------------------------------------------------------------------

def _dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _mid(a, b):
    return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)


# Exact bezier primitives come from the shared wheel pyfile/bezier.py (the
# mirror of algorithm/beziersplit.h) - do not re-derive them here. The
# aliases keep this module's historical names for its many call sites.
_lerp = bezier.lerp
_cubic_point = bezier.eval_cubic
_split_cubic = bezier.cubic_segment
_line_cubic = bezier.line_cubic


def _flatten_cubic(cub, out, step=POLY_STEP):
    """Append samples of one cubic to `out` (excluding its start point)."""
    net = bezier.hull_length(cub)
    samples = max(2, min(64, int(math.ceil(net / max(0.5, step)))))
    for k in range(1, samples + 1):
        out.append(_cubic_point(cub, k / samples))


def _rdp(points, eps):
    """Ramer-Douglas-Peucker decimation; keeps the polyline within eps."""
    if len(points) < 3:
        return list(points)
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        first, last = stack.pop()
        if last <= first + 1:
            continue
        ax, ay = points[first]
        bx, by = points[last]
        dx, dy = bx - ax, by - ay
        seg = math.hypot(dx, dy)
        worst = -1.0
        split = -1
        for i in range(first + 1, last):
            px, py = points[i]
            if seg <= 1e-12:
                d = math.hypot(px - ax, py - ay)
            else:
                # Distance to the SEGMENT, not to the infinite line through it.
                # The map folds wherever the two side scales differ, and a
                # folded-back point sits near that infinite line while being
                # far from the segment - the line metric deleted it silently.
                # Repro: _rdp([(0,0),(120,.1),(-40,-.1),(10,0)], 0.3) used to
                # collapse to the endpoints, a 110 px error.
                t = ((px - ax) * dx + (py - ay) * dy) / (seg * seg)
                t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
                d = math.hypot(px - (ax + dx * t), py - (ay + dy * t))
            if d > worst:
                worst = d
                split = i
        if worst > eps and split > 0:
            keep[split] = True
            stack.append((first, split))
            stack.append((split, last))
    return [points[i] for i in range(len(points)) if keep[i]]


def _catmull_rom_cubics(knots, alpha=_CATMULL_ALPHA):
    """Interpolating centripetal Catmull-Rom spline as a list of cubics.

    Centripetal (alpha=0.5) is used on purpose: the knots come out of RDP and
    are therefore unevenly spaced, and uniform Catmull-Rom overshoots / self-
    intersects on uneven knots while the centripetal form never does.
    """
    n = len(knots)
    if n < 2:
        return []
    if n == 2:
        return [_line_cubic(knots[0], knots[1])]

    ext = [knots[0]] + list(knots) + [knots[-1]]
    cubics = []
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        t0 = 0.0
        t1 = t0 + max(_dist(p0, p1) ** alpha, 1e-9)
        t2 = t1 + max(_dist(p1, p2) ** alpha, 1e-9)
        t3 = t2 + max(_dist(p2, p3) ** alpha, 1e-9)
        # Hermite tangents at p1, p2 (non-uniform), scaled to this segment.
        def tangent(pa, pb, pc, ta, tb, tc):
            m1x = (pb[0] - pa[0]) / (tb - ta) - (pc[0] - pa[0]) / (tc - ta) + (pc[0] - pb[0]) / (tc - tb)
            m1y = (pb[1] - pa[1]) / (tb - ta) - (pc[1] - pa[1]) / (tc - ta) + (pc[1] - pb[1]) / (tc - tb)
            return (m1x * (t2 - t1), m1y * (t2 - t1))
        m1 = tangent(p0, p1, p2, t0, t1, t2)
        m2 = tangent(p1, p2, p3, t1, t2, t3)
        c1 = (p1[0] + m1[0] / 3.0, p1[1] + m1[1] / 3.0)
        c2 = (p2[0] - m2[0] / 3.0, p2[1] - m2[1] / 3.0)
        cubics.append((p1, c1, c2, p2))
    return cubics


def _structural_knots(map_point, a, b):
    """Parameters in (0,1) where the map stops being affine along a->b.

    The map is H(l) + V(l) rebuilt by arc length on both boards, so its
    kinks sit where a coordinate crosses a guide SEGMENT BOUNDARY - on the
    child frame (where the coordinates themselves break) or, after the
    per-side scale, on the main frame. Those are the samples flatness probes
    are worst at finding: a source segment can span whole periods of a wavy
    guide and land every probe back on the chord (the aliasing case in the
    spec). Feeding them in directly costs one coordinate solve per endpoint
    and removes the guesswork; the adaptive probes stay as the safety net for
    everything these do not cover (a non-injective child frame, where the
    coordinates are only a best effort and this whole argument breaks down).

    On POLYLINE guides the map is exactly affine between these knots. On
    CURVE guides (kept commands) it is smooth-but-curved inside a segment, so
    the knots degrade from "the only possible kinks" to "the candidate
    positions curvature concentrates at" - the adaptive probes and the fold
    split's midpoint sampling carry the rest, and each source piece is at
    most one POLY_STEP long, which bounds what a between-knot wiggle can hide.
    """
    coords = getattr(map_point, "coords", None)
    if coords is None:
        return []
    child, main = map_point.child_frame, map_point.main_frame
    start, end = coords(a), coords(b)

    knots = []
    for axis, (l0, l1) in enumerate(zip(start, end)):
        if abs(l1 - l0) <= 1e-12:
            continue
        if axis == 0:
            own_cum, own_arc = child.gh.knots, child.h_arc
            far_cum, far_arc = main.gh.knots, main.h_arc
            scale_neg, scale_pos = map_point.h_scales
        else:
            own_cum, own_arc = child.gv.knots, child.v_arc
            far_cum, far_arc = main.gv.knots, main.v_arc
            scale_neg, scale_pos = map_point.v_scales

        targets = [value - own_arc for value in own_cum[1:-1]]
        for value in far_cum[1:-1]:
            offset = value - far_arc
            scale = scale_pos if offset >= 0.0 else scale_neg
            if abs(scale) > 1e-12:
                targets.append(offset / scale)

        lo, hi = (l0, l1) if l0 < l1 else (l1, l0)
        for target in targets:
            if lo < target < hi:
                knots.append((target - l0) / (l1 - l0))
    knots = sorted({t for t in knots if 1e-9 < t < 1.0 - 1e-9})
    if len(knots) > _MAX_KNOTS_PER_SPAN:
        # Safety valve for a folded child frame, where the coordinates run
        # far and sweep hundreds of guide vertices inside one source segment.
        # Thin them evenly rather than dropping the tail; the probes still
        # cover whatever this misses.
        stride = len(knots) / _MAX_KNOTS_PER_SPAN
        knots = [knots[min(len(knots) - 1, int(i * stride))]
                 for i in range(_MAX_KNOTS_PER_SPAN)]
    return knots


def _adaptive_map_polyline(map_point, points, tol=_CURVE_TOL, max_depth=_SPLINE_MAX_DEPTH):
    """Map `points` through the warp, inserting samples between the ORIGINAL
    vertices so the mapped polyline stays within `tol` of the true warped
    curve.

    Returns [(mapped_point, is_original), ...]. The original vertices are
    anchors: downstream decimation only touches the inserted samples, never
    them. Two flatness probes (midpoint + golden section) plus a forced
    maximum output chord length guard against a straight source segment
    aliasing through the probes when it spans whole periods of a wavy guide.
    """
    if len(points) < 2:
        return [(map_point(p), True) for p in points]

    result = [(map_point(points[0]), True)]

    def recurse(a, b, wa, wb, depth):
        if depth >= max_depth:
            return
        m = _mid(a, b)
        wm = map_point(m)
        g = _lerp(a, b, _PROBE_T)
        wg = map_point(g)
        if (_dist(wa, wb) > _FORCE_STEP
                or _dist(wm, _mid(wa, wb)) > tol
                or _dist(wg, _lerp(wa, wb, _PROBE_T)) > tol):
            recurse(a, m, wa, wm, depth + 1)
            result.append((wm, False))
            recurse(m, b, wm, wb, depth + 1)

    wa = result[0][0]
    for i in range(len(points) - 1):
        a, b = points[i], points[i + 1]
        wb = map_point(b)
        # Split the ORIGINAL interval at the map's own kinks first, then let
        # the probes refine whatever is left inside each smooth piece.
        previous, w_previous = a, wa
        for t in _structural_knots(map_point, a, b):
            knot = _lerp(a, b, t)
            w_knot = map_point(knot)
            recurse(previous, knot, w_previous, w_knot, 0)
            result.append((w_knot, False))
            previous, w_previous = knot, w_knot
        recurse(previous, b, w_previous, wb, 0)
        result.append((wb, True))
        wa = wb
    return result


def _clip_flagged(flagged, polygons):
    """_clip_polyline for (point, is_original) pairs; boundary cuts are anchors."""
    if not polygons:
        return [list(flagged)]
    pieces = []
    current = []
    for first, second in zip(flagged, flagged[1:]):
        a, b = first[0], second[0]
        for t0, t1, inside in _clip_runs(a, b, polygons):
            if not inside:
                if len(current) >= 2:
                    pieces.append(current)
                current = []
                continue
            if not current:
                head = first if t0 <= 0.0 else ((a[0] + (b[0] - a[0]) * t0,
                                                 a[1] + (b[1] - a[1]) * t0), True)
                current.append(head)
            tail = second if t1 >= 1.0 else ((a[0] + (b[0] - a[0]) * t1,
                                              a[1] + (b[1] - a[1]) * t1), True)
            current.append(tail)
    if len(current) >= 2:
        pieces.append(current)
    return pieces


def _decimate_between_anchors(flagged, eps):
    """RDP each run of inserted samples between two anchors; keep every anchor.

    RDP always keeps its endpoints, and each span's endpoints are the mapped
    original vertices, so the artist's points survive verbatim while the dense
    warp samples collapse back to the few knots the curvature needs.
    """
    knots = [flagged[0][0]]
    span = [flagged[0][0]]
    for point, is_anchor in flagged[1:]:
        span.append(point)
        if is_anchor:
            knots.extend(_rdp(span, eps)[1:])
            span = [point]
    if len(span) >= 2:  # tolerate a trailing non-anchor tail
        knots.extend(_rdp(span, eps)[1:])
    return knots


def _directional_image(map_point, base, ctrl):
    """Image of the handle vector (ctrl - base) under the warp at `base`.

    A one-sided derivative in the handle's OWN direction, not a full Jacobian:
    it never straddles the anchor's tangent kink, needs no matrix inverse (so a
    singular / anisotropic warp is harmless), and is first-order exact - the end
    tangent of a cubic is 3*(ctrl-base), and a smooth map sends a tangent vector
    v to (D_v W), so the 1/3 handle maps to base' + D_(ctrl-base) W.
    """
    vx, vy = ctrl[0] - base[0], ctrl[1] - base[1]
    length = math.hypot(vx, vy)
    wb = map_point(base)
    if length <= 1e-9:
        return wb
    eps = min(_JAC_EPS, 0.25 * length)
    ux, uy = vx / length, vy / length
    ahead = map_point((base[0] + ux * eps, base[1] + uy * eps))
    # derivative * length = image of the full handle vector
    return (wb[0] + (ahead[0] - wb[0]) / eps * length,
            wb[1] + (ahead[1] - wb[1]) / eps * length)


def _warp_cubic(map_point, cub, tol=_CURVE_TOL, max_depth=_BEZIER_MAX_DEPTH, depth=0):
    """Transport one source cubic through the warp, subdividing on error.

    Returns a list of output cubics whose union approximates W(cub). The handle
    transport is only first-order, so wherever the warp bends hard (or crosses a
    tangent kink) the transported cubic is checked at t=1/4,1/2,3/4 against the
    true warped point and the SOURCE cubic is bisected until it fits or the depth
    cap is hit (which also stops runaway recursion at a genuine discontinuity).
    """
    p0, c1, c2, p3 = cub
    w0 = map_point(p0)
    w3 = map_point(p3)
    out_c1 = _directional_image(map_point, p0, c1)
    out_c2 = _directional_image(map_point, p3, c2)
    out = (w0, out_c1, out_c2, w3)

    # Probe density scales with the OUTPUT size: a long cubic spanning whole
    # periods of a wavy guide can slip through any small fixed probe set. The
    # golden-section probe additionally breaks periodic alignment. Source
    # anchors always survive as output anchors (subdividing only ADDS knots),
    # matching the "originals are never decimated" rule of spline mode.
    net = bezier.hull_length(out)
    probes = max(3, min(17, int(math.ceil(net / _FORCE_STEP))))
    ts = [(k + 1.0) / (probes + 1.0) for k in range(probes)] + [_PROBE_T]
    worst = 0.0
    for t in ts:
        worst = max(worst, _dist(map_point(_cubic_point(cub, t)), _cubic_point(out, t)))
    if worst <= tol or depth >= max_depth:
        return [out]
    # One shared-wheel split (pyfile/bezier.py) yields both halves at once.
    left, right = bezier.split_cubic(cub, 0.5)
    return (_warp_cubic(map_point, left, tol, max_depth, depth + 1)
            + _warp_cubic(map_point, right, tol, max_depth, depth + 1))


def _commands_to_subpaths(commands):
    """Parse stroke `commands` into subpaths, each a list of cubic tuples.

    line -> a straight cubic; quad -> its exact cubic elevation; cubic -> as is.
    A "move" starts a new subpath so genuinely separate subpaths never get a
    bogus connecting segment (the flaw in _stroke_points that concatenates all
    polylines of a stroke into one list).
    """
    subpaths = []
    current = []
    start = None
    for command in commands or []:
        kind = command.get("type")
        if kind == "move":
            if current:
                subpaths.append(current)
            current = []
            start = _command_point(command["to"])
        elif kind == "line":
            a = start if start is not None else (
                _command_point(command["from"]) if "from" in command else None)
            b = _command_point(command["to"])
            if a is not None:
                current.append(_line_cubic(a, b))
            start = b
        elif kind == "quad":
            a = start if start is not None else (
                _command_point(command["from"]) if "from" in command else None)
            ctrl = _command_point(command["control"])
            b = _command_point(command["to"])
            if a is not None:
                # Elevation via the shared wheel pyfile/bezier.py.
                current.append(bezier.quad_cubic(a, ctrl, b))
            start = b
        elif kind == "cubic":
            a = start if start is not None else (
                _command_point(command["from"]) if "from" in command else None)
            c1 = _command_point(command["control1"])
            c2 = _command_point(command["control2"])
            b = _command_point(command["to"])
            if a is not None:
                current.append((a, c1, c2, b))
            start = b
    if current:
        subpaths.append(current)
    return subpaths


def _bernstein_cubic_roots(b0, b1, b2, b3):
    """Roots in (0, 1) of the cubic with these Bernstein coefficients.

    Isolation instead of a closed form: the derivative is a quadratic whose
    roots split [0, 1] into at most three MONOTONE pieces, and a monotone
    piece holds at most one root, found by bisection to machine precision.
    Robust for the double/triple roots a tangential touch produces, where
    Cardano loses most of its digits.
    """
    c0 = b0
    c1 = 3.0 * (b1 - b0)
    c2 = 3.0 * (b2 - 2.0 * b1 + b0)
    c3 = b3 - 3.0 * b2 + 3.0 * b1 - b0

    def value(t):
        return ((c3 * t + c2) * t + c1) * t + c0

    breaks = [0.0, 1.0]
    qa, qb, qc = 3.0 * c3, 2.0 * c2, c1
    if abs(qa) > 1e-14:
        disc = qb * qb - 4.0 * qa * qc
        if disc > 0.0:
            root = math.sqrt(disc)
            for extremum in ((-qb - root) / (2.0 * qa), (-qb + root) / (2.0 * qa)):
                if 0.0 < extremum < 1.0:
                    breaks.append(extremum)
    elif abs(qb) > 1e-14:
        extremum = -qc / qb
        if 0.0 < extremum < 1.0:
            breaks.append(extremum)
    breaks.sort()

    roots = []
    for lo, hi in zip(breaks, breaks[1:]):
        flo, fhi = value(lo), value(hi)
        if flo == 0.0 and 0.0 < lo < 1.0:
            roots.append(lo)
            continue
        if flo * fhi > 0.0 or flo == fhi:
            continue
        a, b = lo, hi
        for _ in range(60):
            mid = (a + b) * 0.5
            if value(a) * value(mid) <= 0.0:
                b = mid
            else:
                a = mid
        root = (a + b) * 0.5
        if 0.0 < root < 1.0:
            roots.append(root)
    return roots


def _cubic_polygon_crossings(cub, polygons):
    """Parameters t in (0,1) where a cubic crosses a polygon edge.

    Exact, like _segment_polygon_crossings: the signed distance of the cubic
    to an edge's line is itself a cubic whose Bernstein coefficients are just
    that distance evaluated at the four control points, so the convex-hull
    test rejects most edges without any root finding.
    """
    crossings = []
    for polygon in polygons:
        count = len(polygon)
        for index in range(count):
            e0 = polygon[index]
            e1 = polygon[(index + 1) % count]
            ex, ey = e1[0] - e0[0], e1[1] - e0[1]
            extent = ex * ex + ey * ey
            if extent <= 1e-18:
                continue
            side = [(point[0] - e0[0]) * ey - (point[1] - e0[1]) * ex for point in cub]
            if min(side) > 0.0 or max(side) < 0.0:
                continue  # whole curve on one side of this edge's line
            for t in _bernstein_cubic_roots(*side):
                hit = _cubic_point(cub, t)
                along = ((hit[0] - e0[0]) * ex + (hit[1] - e0[1]) * ey) / extent
                if -1e-12 <= along <= 1.0 + 1e-12:
                    crossings.append(t)
    return crossings


def _clip_cubics(cubics, polygons):
    """Split a subpath of cubics into the runs that lie inside `polygons`.

    Mirrors _clip_polyline's semantics but keeps curve segments: the crossing
    parameters are SOLVED (cubic-vs-edge), not sampled, and de Casteljau
    splitting extracts the inside sub-cubics. Runs that stay inside across a
    cubic boundary are kept as one piece.
    """
    if not polygons:
        return [list(cubics)]

    pieces = []
    current = []
    for cub in cubics:
        bounds = sorted({t for t in _cubic_polygon_crossings(cub, polygons)
                         if 1e-9 < t < 1.0 - 1e-9})
        bounds = [0.0] + bounds + [1.0]
        for t0, t1 in zip(bounds, bounds[1:]):
            if t1 - t0 <= 1e-9:
                continue
            if not _point_in_polygons(_cubic_point(cub, (t0 + t1) * 0.5), polygons):
                if current:
                    pieces.append(current)
                    current = []
                continue
            current.append(_split_cubic(cub, t0, t1))
    if current:
        pieces.append(current)
    return pieces


def _cubics_to_commands(cubics):
    """(command list for make_stroke_object_from_path, dense flattening)."""
    if not cubics:
        return [], []
    commands = [{"type": "move", "to": {"x": cubics[0][0][0], "y": cubics[0][0][1]}}]
    flat = [cubics[0][0]]
    for _, c1, c2, p3 in cubics:
        commands.append({
            "type": "cubic",
            "control1": {"x": c1[0], "y": c1[1]},
            "control2": {"x": c2[0], "y": c2[1]},
            "to": {"x": p3[0], "y": p3[1]},
        })
    for cub in cubics:
        _flatten_cubic(cub, flat)
    return commands, flat


# ---------------------------------------------------------------------------
# mapping asset dict + overlay display
# ---------------------------------------------------------------------------

def _direction_arrow_points(points, size):
    """Arrowhead polyline [wing, tip, wing] at a guide's END point.

    The mapping is direction-sensitive: child guide start/end maps onto main
    guide start/end, so drawing a main center line in the opposite direction
    deliberately flips the texture along that axis. The arrow makes the drawn
    direction visible. The tangent is smoothed over a window because raw
    hand-drawn end segments jitter (same smoothing as _tangent_at_arc's
    other consumers).
    """
    cumulative = _cumulative_lengths(points)
    total = cumulative[-1]
    if total <= 1e-6:
        return None
    window = max(2.0 * POLY_STEP, 0.05 * total)
    dx, dy = _tangent_at_arc(points, cumulative, total, window)
    tip = points[-1]
    back_x, back_y = -dx, -dy
    spread = math.radians(28.0)
    cos_s, sin_s = math.cos(spread), math.sin(spread)
    wing1 = (tip[0] + size * (back_x * cos_s - back_y * sin_s),
             tip[1] + size * (back_x * sin_s + back_y * cos_s))
    wing2 = (tip[0] + size * (back_x * cos_s + back_y * sin_s),
             tip[1] + size * (-back_x * sin_s + back_y * cos_s))
    return [wing1, tip, wing2]


def _current_mapper():
    """The mapper for the guide assets as they stand, or (None, why-not)."""
    child_assets = _assets_for("child")
    main_assets = _assets_for("main")
    for view, assets in (("child", child_assets), ("main", main_assets)):
        missing = [ITEM_LABELS[prop] for prop in GUIDE_PROPERTIES if prop not in assets]
        if missing:
            return None, f"the {view} view is missing: {', '.join(missing)}"
    if not _polylines_cross(child_assets[H_PROPERTY]["points"],
                            child_assets[V_PROPERTY]["points"]):
        return None, "the child center lines do not cross"
    if not _polylines_cross(main_assets[H_PROPERTY]["points"],
                            main_assets[V_PROPERTY]["points"]):
        return None, "the main center lines do not cross"
    mapper, _ = build_mapper(
        child_assets[H_PROPERTY], child_assets[V_PROPERTY],
        main_assets[H_PROPERTY], main_assets[V_PROPERTY],
        # The preview must show the mapping the RUN will use - with the
        # additional-line pairs. Without them the face-down bands sat at
        # the unwarped position (measured 6-7% of the domain mis-tinted).
        additional_pairs=_additional_pairs())
    if mapper is None:
        return None, "the mapper refuses these guides"
    return mapper, ""


def _occlusion_overlay_items():
    """Filled bands over the child regions the current mapping turns face-down.

    The domain is the child guide rectangle (both guides' full arc spans) in
    ARC coordinates, so no Newton inversion is ever needed: the fold sign at
    a grid cell is the same determinant test as _orientation, evaluated
    directly at the cell's arcs. Guide tangents are also cached per grid
    row/column - they only depend on one coordinate each - which turns the
    sweep into plain arithmetic (measured: the naive per-cell version spent
    its whole budget re-fetching the same smoothed tangents).

    Output: per scan row, each face-down run becomes one filled quad
    (bottom edge + reversed top edge), so adjacent rows tile without the
    seams or cap-bulges polyline bands would leave.
    """
    if not _OCCLUSION["enabled"]:
        return []
    cached = _OCCLUSION_CACHE["items"]
    if cached is not None:
        return cached

    _OCCLUSION_CACHE["note"] = ""
    _OCCLUSION_CACHE["share"] = 0.0
    items = []
    mapper, note = _current_mapper()
    if mapper is None:
        _OCCLUSION_CACHE["note"] = note
        _OCCLUSION_CACHE["items"] = items
        return items

    child = mapper.child_frame
    main = mapper.main_frame
    h_lo, h_hi = -child.h_arc, child.h_cum[-1] - child.h_arc
    v_lo, v_hi = -child.v_arc, child.v_cum[-1] - child.v_arc
    if h_hi - h_lo <= 1e-6 or v_hi - v_lo <= 1e-6:
        _OCCLUSION_CACHE["note"] = "a guide has no length"
        _OCCLUSION_CACHE["items"] = items
        return items

    rows = 64
    cols = 160
    row_step = (v_hi - v_lo) / rows
    col_step = (h_hi - h_lo) / cols
    reference = mapper.fold_reference

    def tangents_h(arc_h):
        scale = mapper.h_scales[1] if arc_h >= 0.0 else mapper.h_scales[0]
        return (scale,
                child.gh.tangent_at(child.h_arc + arc_h, child.h_window),
                main.gh.tangent_at(main.h_arc + arc_h * scale, main.h_window))

    def tangents_v(arc_v):
        scale = mapper.v_scales[1] if arc_v >= 0.0 else mapper.v_scales[0]
        return (scale,
                child.gv.tangent_at(child.v_arc + arc_v, child.v_window),
                main.gv.tangent_at(main.v_arc + arc_v * scale, main.v_window))

    warp = getattr(mapper, "warp", None)

    def fold_sign(h_cache, v_cache, arc_h=None, arc_v=None):
        scale_h, (tcx, tcy), (tmx, tmy) = h_cache
        scale_v, (ncx, ncy), (nmx, nmy) = v_cache
        denominator = tcx * ncy - tcy * ncx
        if abs(denominator) < 1e-12:
            return 1  # folded child frame: no usable orientation, same as _orientation
        warp_sign = 1
        if warp is not None and arc_h is not None:
            # The additional-line warp moves the fold field's child-space
            # preimage; the cached main tangents are unwarped, so re-fetch
            # them at the warped arcs - the same route _orientation takes,
            # including the warp's own fold parity.
            warp_sign = warp.det_sign((arc_h, arc_v))
            w_h, w_v = warp.apply((arc_h, arc_v))
            scale_h = mapper.h_scales[1] if w_h >= 0.0 else mapper.h_scales[0]
            scale_v = mapper.v_scales[1] if w_v >= 0.0 else mapper.v_scales[0]
            tmx, tmy = main.gh.tangent_at(main.h_arc + w_h * scale_h,
                                          main.h_window)
            nmx, nmy = main.gv.tangent_at(main.v_arc + w_v * scale_v,
                                          main.v_window)
        value = warp_sign * scale_h * scale_v * (tmx * nmy - tmy * nmx) / denominator
        raw = 1 if value > 0.0 else -1
        return 1 if raw == reference else -1

    columns = [tangents_h(h_lo + (index + 0.5) * col_step) for index in range(cols)]
    back_arc = 0.0
    for row in range(rows):
        arc_v = v_lo + (row + 0.5) * row_step
        v_cache = tangents_v(arc_v)

        def sign_at(arc_h):
            return fold_sign(tangents_h(arc_h), v_cache, arc_h, arc_v)

        # cell-centred signs from the cached column tangents
        signs = [fold_sign(h_cache, v_cache,
                           h_lo + (index + 0.5) * col_step, arc_v)
                 for index, h_cache in enumerate(columns)]
        runs = []
        start = h_lo if signs[0] < 0 else None
        for index in range(cols - 1):
            if signs[index] == signs[index + 1]:
                continue
            lo_b = h_lo + (index + 0.5) * col_step
            hi_b = lo_b + col_step
            for _ in range(12):
                mid = (lo_b + hi_b) * 0.5
                if sign_at(mid) == signs[index]:
                    lo_b = mid
                else:
                    hi_b = mid
            boundary = (lo_b + hi_b) * 0.5
            if signs[index] < 0:
                runs.append((start, boundary))
                start = None
            else:
                start = boundary
        if start is not None:
            runs.append((start, h_hi))

        bottom = arc_v - 0.5 * row_step
        top = arc_v + 0.5 * row_step
        for a0, a1 in runs:
            if a1 - a0 <= 1e-6:
                continue
            back_arc += a1 - a0
            count = max(2, int(round((a1 - a0) / (2.0 * POLY_STEP))) + 1)
            arcs = [a0 + (a1 - a0) * k / (count - 1) for k in range(count)]
            polygon = ([child.hv(a, bottom) for a in arcs]
                       + [child.hv(a, top) for a in reversed(arcs)])
            items.append({
                "id": OCCLUSION_BUTTON,
                "points": polygon,
                "closed": True,
                "color": (0, 0, 0, 0),
                "fill_color": (*_display_color("back_color")[:3],
                                int(_LINE_DISPLAY.get("occlusion_alpha", 70))),
                "width": 0.1,
                "removable": False,
            })

    _OCCLUSION_CACHE["share"] = back_arc / ((h_hi - h_lo) * rows)
    _OCCLUSION_CACHE["items"] = items
    return items


def overlay_items(view_name):
    """This view's mapping overlay items (guides + arrows + area + grid).

    Public so other tools (e.g. repulsion_tool's drag preview) can COMPOSE
    their own items with the mapping display instead of clobbering it -
    ui.set_overlay replaces a view's whole item list.
    """
    assets = _assets_for(view_name)
    items = []
    for prop, key in ((H_PROPERTY, "h"), (V_PROPERTY, "v")):
        guide = assets.get(prop)
        if guide and len(guide.get("points") or []) >= 2:
            # Colour, width and style come from the display settings now, not
            # from the stroke that happened to draw the guide.
            color = _display_color(f"{key}_color")
            width = float(_LINE_DISPLAY.get(f"{key}_width", 3.0))
            style = _display_style(f"{key}_style")
            items.append({
                "id": prop,
                "points": guide["points"],
                "color": color,
                "width": width,
                "pen_style": style,
                "removable": True,
                # Grabbable under any tool (C++ routes a draggable item's
                # press through the same "handle" events an edit handle
                # uses): placing an axis is a drag, not a redraw.
                "draggable": True,
            })
            arrow = _direction_arrow_points(guide["points"], max(12.0, 3.5 * width))
            if arrow:
                items.append({
                    "id": prop + "_arrow",
                    "points": arrow,
                    "color": color,
                    "width": width,
                    # The arrow says which way the guide runs; dashing it
                    # would make a short mark nearly invisible.
                    "removable": False,
                })
    for index, line in enumerate((assets.get(ADDITIONAL_PROPERTY) or {}).get("lines") or []):
        if len(line.get("points") or []) < 2:
            continue
        items.append({
            # The pair id rides in the overlay id so removing one line can
            # remove the PAIR (both boards) rather than guessing by geometry.
            "id": f"{ADDITIONAL_PROPERTY}:{_line_id(line, index)}",
            "points": line["points"],
            "draggable": True,
            "color": _display_color("additional_color"),
            "width": float(_LINE_DISPLAY.get("additional_width", 2.5)),
            "pen_style": _display_style("additional_style"),
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
    if view_name == "child":
        try:
            # UNDER the guides and area, so the tint never covers them.
            items[:0] = _occlusion_overlay_items()
        except Exception as error:
            print(f"[auto_mapping] occlusion preview skipped: {error}")
    if view_name == "main":
        try:
            # ON TOP of the guides: the anchor is the one thing here you grab.
            items.extend(_nearest_overlay_items())
        except Exception as error:
            print(f"[auto_mapping] nearest point overlay skipped: {error}")
    return items


def _push_overlay(view_name):
    """Send this view's mapping assets to the generic C++ overlay display."""
    try:
        _animean().ui.set_overlay(view_name, overlay_items(view_name))
    except Exception as error:
        print(f"[auto_mapping] overlay update failed: {error}")


def _set_draw_color(color):
    """Arm this tool's colour THROUGH the per-tool cache: setting it
    directly repainted whatever tool came next (pyfile/tool_colors.py)."""
    try:
        import tool_colors
        tool_colors.apply(color)
    except Exception:
        try:
            _animean().ui.set_draw_color(color)
        except Exception:
            pass


def _view_frame(view_name):
    """The reference frame of ONE board, from that board's own axes.

    Independent by construction: the texture's frame needs the texture's
    axes and nothing else, and the main board's needs the main board's. The
    refer grid used to build the main board's from the CHILD frame pushed
    through the mapper, which made a grid on one board unavailable whenever
    the OTHER board had no axes yet - the common state while a setup is only
    half drawn, and exactly when a reference rectangle is most wanted.
    """
    assets = _assets_for(view_name)
    if H_PROPERTY not in assets or V_PROPERTY not in assets:
        return None
    if (len(assets[H_PROPERTY].get("points") or []) < 2
            or len(assets[V_PROPERTY].get("points") or []) < 2):
        return None
    frame = _Frame(assets[H_PROPERTY], assets[V_PROPERTY])
    if frame.h_total <= 1e-6 or frame.v_total <= 1e-6:
        return None
    return frame


def _child_frame():
    """The child reference frame the MAPPER actually uses.

    This used to build a straight chord frame from the guides' end points,
    which disagreed with the mapper as soon as a guide was curved: the
    coordinates are arc length measured from the CROSSING, not a projection
    onto the start-to-end chord. Measured on sine guides, the nine anchors
    were off by 16 / 40 / 82 px at amplitude 10 / 30 / 60, and the anchor the
    grid calls "the H guide's end point" missed that end point by up to 30 px.
    Since the grid exists to reveal a wrong mapping at a glance, it has to be
    built out of the same frame the mapping is.
    """
    return _view_frame("child")


def _frame_point(frame, u_hat, v_hat):
    """Child-space point at normalised grid coords (u_hat, v_hat in [-1, 1])."""
    du = u_hat * (frame.h_side[1] if u_hat >= 0.0 else frame.h_side[0])
    dv = v_hat * (frame.v_side[1] if v_hat >= 0.0 else frame.v_side[0])
    return frame.hv(du, dv)


def _grid_overlay_items(view_name):
    """Refer-rect grid: the 3x3 anchor lattice (crossing, 4 guide endpoints,
    4 quadrant corners) with quarter-step iso-lines, for ONE board.

    The CHILD board draws its own frame from its own axes - a pure picture
    of the source coordinate system. The MAIN board draws the RENDERING
    mapping whenever the full mapper can be built (user request: the grid
    must reflect the additional lines' influence): the child lattice pushed
    through the mapper, warp included, so it bends where a pink line shapes
    the mapping and comparing the two boards' grids reads the whole mapping
    by eye. Only when the mapper is unavailable - the normal state while a
    setup is half drawn - does the main board fall back to its own frame,
    so a reference rectangle stays available either way.

    A board with no axes, or with axes that do not cross, has no frame and so
    draws nothing - there is no rectangle to refer to."""
    if not _REFER_RECT.get(view_name, False):
        return []
    cached = _GRID_CACHE.get(view_name)
    if cached is not None:
        return cached

    items = []
    divisions = max(2, int(_GRID.get("divisions", 5)))
    levels = [i / (divisions - 1) * 2.0 - 1.0 for i in range(divisions)]
    count = 6 * (divisions - 1) + 1   # divisions=5 -> 25, the historical density
    samples = [i / (count - 1) * 2.0 - 1.0 for i in range(count)]

    # The MAIN board's grid shows the RENDERING mapping when it exists: the
    # child frame's lattice pushed through the full mapper, additional-line
    # warp included - so the grid visibly bends where a pink line shapes
    # the mapping (user request). Comparing it with the child board's own
    # grid reads the whole mapping by eye. Without a full mapper (the
    # normal state while a setup is half drawn) the board falls back to its
    # own frame below, so the reference rectangle stays available.
    if view_name == "main":
        mapper, _note = _current_mapper()
        if mapper is not None:
            child_frame = mapper.child_frame
            for level in levels:
                iso_u = [mapper(_frame_point(child_frame, level, s))
                         for s in samples]
                iso_v = [mapper(_frame_point(child_frame, s, level))
                         for s in samples]
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

    # This board's own axes, and nothing else: a half-drawn setup still
    # deserves a reference rectangle. Failure paths CACHE their empty
    # result too - overlay_items runs per mouse move during drags, and an
    # uncached miss re-entered _current_mapper (two O(n*m) crossing scans)
    # every time.
    assets = _assets_for(view_name)
    if H_PROPERTY not in assets or V_PROPERTY not in assets:
        _GRID_CACHE[view_name] = []
        return []
    if not _polylines_cross(assets[H_PROPERTY]["points"],
                            assets[V_PROPERTY]["points"]):
        _GRID_CACHE[view_name] = []
        return []
    frame = _view_frame(view_name)
    if frame is None:
        _GRID_CACHE[view_name] = []
        return []

    for level in levels:
        iso_u = [_frame_point(frame, level, s) for s in samples]
        iso_v = [_frame_point(frame, s, level) for s in samples]
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
    """Assets changed in view_name: refresh its overlay, and any OTHER view
    whose display genuinely depends on those assets.

    The occlusion tint is drawn on the texture but computed from the MAIN
    guides, so a main-side edit re-pushes the child overlay. The MAIN refer
    grid renders the full mapping (child lattice through the warp), so a
    child-side edit re-pushes the main overlay. The CHILD grid stays a pure
    function of the child's own axes."""
    _invalidate_grid_cache()
    _push_overlay(view_name)
    if _OCCLUSION["enabled"] and view_name != "child":
        _push_overlay("child")
    if _REFER_RECT.get("main") and view_name != "main":
        _push_overlay("main")


def _main_guide_frame():
    """The MAIN board's own frame (for the nearest-end handle), or None."""
    assets = _assets_for("main")
    if H_PROPERTY not in assets or V_PROPERTY not in assets:
        return None
    if (len(assets[H_PROPERTY].get("points") or []) < 2
            or len(assets[V_PROPERTY].get("points") or []) < 2):
        return None
    try:
        frame = _Frame(assets[H_PROPERTY], assets[V_PROPERTY])
    except Exception:
        return None
    if frame.h_total <= 1e-6 or frame.v_total <= 1e-6:
        return None
    return frame


def _nearest_arc():
    """The nearest-end anchor in MAIN arc coordinates (crossing by default)."""
    item = _assets_for("main").get(NEAREST_PROPERTY) or {}
    arc = item.get("arc") or (0.0, 0.0)
    return float(arc[0]), float(arc[1])


def _nearest_overlay_items():
    """The nearest-end anchor as an OVERLAY OBJECT, same family as the guides.

    A small red ring with the same x badge the center lines carry (removing
    it resets the anchor to the crossing, since the depth default is the
    crossing) - and the one overlay item with `draggable`: C++ routes a drag
    on it through the "handle" hook events with its id, so it moves like a
    stroke's edit handle without ever touching the edit-handle channel that
    the Arrow/Connect sessions own.
    """
    frame = _main_guide_frame()
    if frame is None:
        return []
    arc_h, arc_v = _nearest_arc()
    # A stored anchor can outlive the guides it was set on; clamp it back
    # onto the sheet (point_at extrapolates linearly past the ends, which
    # would float the ring off into empty space).
    arc_h = min(max(arc_h, -frame.h_arc), frame.h_total - frame.h_arc)
    arc_v = min(max(arc_v, -frame.v_arc), frame.v_total - frame.v_arc)
    center = frame.hv(arc_h, arc_v)
    radius = 6.0
    ring = [(center[0] + radius * math.cos(2.0 * math.pi * k / 24),
             center[1] + radius * math.sin(2.0 * math.pi * k / 24))
            for k in range(24)]
    fill = (NEAREST_HANDLE_COLOR[0], NEAREST_HANDLE_COLOR[1],
            NEAREST_HANDLE_COLOR[2], 90)
    return [{
        "id": NEAREST_PROPERTY,
        "points": ring,
        "closed": True,
        "color": NEAREST_HANDLE_COLOR,
        "fill_color": fill,
        "width": 1.5,
        "removable": True,
        "draggable": True,
    }]


def _push_nearest_handle():
    """The anchor lives in the main overlay now; re-push that overlay."""
    _push_overlay("main")


_NEAREST_DRAG = {"frame": None, "moved": False, "offset": (0.0, 0.0)}


# Guide/additional-line drags: where the line SITS is a placement, and a
# placement is a drag. The overlay item carries "draggable", C++ reports the
# press/move/release through the handle events, and the geometry is rewritten
# here - the same route the nearest-end anchor already took.
_GUIDE_DRAG = {}


def _guide_drag_event(message):
    overlay_id = str(message.get("handle") or "")
    is_guide = overlay_id in GUIDE_PROPERTIES
    is_additional = overlay_id.startswith(ADDITIONAL_PROPERTY + ":")
    if not is_guide and not is_additional:
        return
    view = message.get("view") or "main"
    phase = message.get("phase")
    position = message.get("position") or {}
    point = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
    assets = _assets_for(view)

    if phase == "press":
        if is_guide:
            item = assets.get(overlay_id)
            points = list(item.get("points") or []) if item else []
        else:
            index, line = _additional_line_by_overlay_id(view, overlay_id)
            points = list(line.get("points") or []) if line else []
        if len(points) < 2:
            return
        _GUIDE_DRAG[view] = {"id": overlay_id, "origin": point,
                             "points": [(float(x), float(y)) for x, y in points],
                             "moved": False}
        return

    if phase not in ("move", "release"):
        return
    drag = _GUIDE_DRAG.get(view)
    if drag is None or drag["id"] != overlay_id:
        return
    delta = (point[0] - drag["origin"][0], point[1] - drag["origin"][1])
    if abs(delta[0]) > 1e-9 or abs(delta[1]) > 1e-9:
        drag["moved"] = True
    moved = [(x + delta[0], y + delta[1]) for x, y in drag["points"]]

    if is_guide:
        item = assets.get(overlay_id)
        if item is None:
            return
        item["points"] = moved
        # The stored Bezier commands describe the OLD position; dropping them
        # keeps the guide honest as a polyline rather than drawing it in two
        # places at once. A redraw restores the curve.
        item.pop("commands", None)
    else:
        _index, line = _additional_line_by_overlay_id(view, overlay_id)
        if line is None:
            return
        line["points"] = moved
        # Its Third coordinates describe where it WAS.
        line.pop("third", None)

    if phase == "move":
        _push_overlay(view)
        _invalidate_grid_cache()
        try:
            _animean().ui.refresh()
        except Exception:
            pass
        return

    # release
    _GUIDE_DRAG.pop(view, None)
    if not drag["moved"]:
        _push_overlay(view)
        return
    if is_guide:
        # Third space IS the child arc plane, so a moved axis retires every
        # additional line's stored coordinates on this board.
        _invalidate_additional_thirds(view)
    _save_assets(view)
    _invalidate_grid_cache()
    # BOTH boards: the mapping is a relation between them, so the other
    # board's refer grid and occlusion tint describe the moved guide too and
    # would otherwise keep drawing the old placement.
    _push_overlay("main")
    _push_overlay("child")
    try:
        animean = _animean()
        animean.ui.refresh()
        animean.ui.history_commit("Move Guide" if is_guide
                                  else "Move Additional Line", view)
    except Exception:
        pass
    print(f"[auto_mapping] {overlay_id} moved by "
          f"({delta[0]:.1f}, {delta[1]:.1f})")


def _additional_line_by_overlay_id(view, overlay_id):
    try:
        pair_id = int(overlay_id.split(":", 1)[1])
    except (IndexError, ValueError):
        return -1, None
    lines = (_assets_for(view).get(ADDITIONAL_PROPERTY) or {}).get("lines") or []
    for index, line in enumerate(lines):
        if _line_id(line, index) == pair_id:
            return index, line
    return -1, None


def _nearest_handle_event(message):
    """Drag the nearest-end anchor; it snaps onto the sheet (arc coords).

    A press only GRABS (the handle accepts clicks up to 7 screen px off
    centre, and applying the press position would teleport the anchor by
    that offset); only actual movement re-solves, and only a drag that moved
    commits history - a stray click must not truncate the redo tail.
    """
    if message.get("view") != "main":
        return
    phase = message.get("phase")
    if phase == "cancel":
        # Arrow/Connect teardown; the anchor lives in the overlay now, so
        # nothing to reclaim - just drop any half-finished drag state.
        _NEAREST_DRAG["frame"] = None
        _NEAREST_DRAG["moved"] = False
        return
    if message.get("handle") != NEAREST_PROPERTY:
        return
    if phase == "press":
        frame = _main_guide_frame()
        _NEAREST_DRAG["frame"] = frame
        _NEAREST_DRAG["moved"] = False
        # The handle accepts presses up to 7 screen px off centre; remember
        # the miss so the first move does not snap the anchor to the cursor.
        offset = (0.0, 0.0)
        if frame is not None:
            position = message.get("position") or {}
            grabbed = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
            shown = frame.hv(*_nearest_arc())
            offset = (shown[0] - grabbed[0], shown[1] - grabbed[1])
        _NEAREST_DRAG["offset"] = offset
        return
    if phase not in ("move", "release"):
        return
    frame = _NEAREST_DRAG.get("frame") or _main_guide_frame()
    if frame is None:
        return
    _NEAREST_DRAG["frame"] = frame  # guides cannot change mid-drag: build once
    if phase == "move":
        position = message.get("position") or {}
        offset = _NEAREST_DRAG.get("offset") or (0.0, 0.0)
        point = (float(position.get("x", 0.0)) + offset[0],
                 float(position.get("y", 0.0)) + offset[1])
        current = _nearest_arc()
        l_h, l_v = frame.solve(point, current[0], current[1])
        # Keep the anchor on the sheet the guides actually span.
        l_h = min(max(l_h, -frame.h_arc), frame.h_total - frame.h_arc)
        l_v = min(max(l_v, -frame.v_arc), frame.v_total - frame.v_arc)
        _assets_for("main")[NEAREST_PROPERTY] = {"arc": [l_h, l_v]}
        _NEAREST_DRAG["moved"] = True
        _push_nearest_handle()
        return
    # release
    moved = _NEAREST_DRAG["moved"]
    _NEAREST_DRAG["frame"] = None
    _NEAREST_DRAG["moved"] = False
    if not moved:
        return
    _save_assets("main")
    try:
        _animean().ui.history_commit("Move Nearest Point", "main")
    except Exception:
        pass  # older builds without the history binding
    l_h, l_v = _nearest_arc()
    print(f"[auto_mapping] nearest point at arc ({l_h:.1f}, {l_v:.1f}); "
          "the next Auto Mapping stacks fold layers from here.")


def _detect_region(scene, view_name, frame, seed):
    """Bucket-style region detection, computed in Python via vectorlogic."""
    segments = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return []
    for layer in structure["layers"]:
        if not layer["visible"] or layer["type"] == "fill":
            continue
        # Cheap probe first (no 4px path flattening): stacked mapping runs add
        # a layer per click, and every mapped layer is 100% MAPPED_PROPERTY
        # strokes, so the expensive to_poly fetch would be pure waste there.
        probe = scene.cell_to_dict(layer["index"], frame, False, POLY_STEP)
        if all((stroke.get("property") or "") in MAPPING_OUTPUT_PROPERTIES
               for stroke in probe["image"]["strokes"]):
            continue
        cell = scene.cell_to_dict(layer["index"], frame, True, POLY_STEP)
        for stroke in cell["image"]["strokes"]:
            # A previous mapping result must not act as a region boundary,
            # otherwise the mapped layer shatters the bucket into tiny pieces.
            # ALL of the run's output counts, not just the front layer: the
            # back and crease layers were already walling the bucket off, and
            # the two guide-axis layers would have cut every shape they cross
            # into quadrants.
            if (stroke.get("property") or "") in MAPPING_OUTPUT_PROPERTIES:
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
    """Migrate guide strokes / area fills that older builds stored in layers.

    Fetched with to_poly=False: this scan runs on EVERY mapping click over
    every layer (a growing set now that runs stack), and flattening each
    stroke's path at 4px just to look for a legacy property is the dominant
    per-click cost. _stroke_points falls back to raw_points, which for a
    hand-drawn legacy guide are the drawn points themselves.
    """
    assets = _assets_for(view_name)
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return
    changed = False
    for layer in structure["layers"]:
        layer_index = layer["index"]
        cell = scene.cell_to_dict(layer_index, frame, False, POLY_STEP)
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

def _collect_pattern_strokes(scene, frame, want_commands=False):
    """Pattern strokes on `frame`. With want_commands the strokes carry their
    real Bezier "commands" (to_poly=False, for bezier mode); otherwise the 4px
    "polylines" flattening (for polyline / spline modes)."""
    pattern = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return pattern
    # "auto_mapping" stays as a literal: strokes drawn while the retired
    # Auto Mapping 1 button was active carry that property in old sessions.
    skip = (*GUIDE_PROPERTIES, *MAPPING_OUTPUT_PROPERTIES, "auto_mapping",
            AUTO_MAPPING2_TOOL, MAPPING_AREA_PROPERTY)
    to_poly = not want_commands
    for layer in structure["layers"]:
        if not layer["visible"] or layer["type"] == "fill":
            continue
        cell = scene.cell_to_dict(layer["index"], frame, to_poly, POLY_STEP)
        for stroke in cell["image"]["strokes"]:
            if (stroke.get("property") or "") in skip:
                continue
            pattern.append(stroke)
    return pattern


def _create_mapped_layer(scene, row, name=MAPPED_LAYER_NAME):
    """Create a FRESH mapped layer for this run and return its index (0 = top).

    Every Auto Mapping click gets its own layer (user request 2026-07-30):
    results stack instead of replacing each other, so different guide setups
    can be compared side by side and bad attempts hidden or deleted
    individually (note: the Layers panel only lists a layer on frames where
    it has a cell, i.e. on the frame it was mapped on). uniqueLayerName()
    drifts the name to "mapped layer1", "mapped layer2", ... automatically.

    The creation cell that add_layer() writes - {private asset, frame_id 1} -
    is kept VERBATIM: the asset belongs to this run alone and holds exactly
    one cell, so frame id 1 is collision-free, and 1 is the canonical id every
    asset-resolution path assumes (assignAssetToLayer hard-codes it). Pointing
    the cell elsewhere would leave an empty canonical image behind: dragging
    the asset from the Asset panel onto the Layers panel would show nothing,
    and every save would carry a dead drawing (review-proven).

    add_layer() appends, and paintGL draws last-index-first, so an appended
    layer lands at the BOTTOM of the z-order - each new run would be occluded
    by the previous ones. The fresh column is therefore moved to index 0
    (top), with the fill source-layer indices remapped to follow the shift.
    The user's frame/layer/asset selection is restored before returning
    (add_layer() selects what it creates; the move shifts old indices by +1).
    """
    saved_frame = scene.current_frame()
    saved_layer = scene.current_layer()
    saved_asset = scene.current_asset()

    layer_index = scene.add_layer()
    if layer_index < 0:
        scene.set_current_frame(saved_frame)
        scene.set_current_layer(saved_layer)
        scene.set_current_asset(saved_asset)
        return -1
    scene.set_layer_name(layer_index, name)

    moved = scene.move_layer(layer_index, 0)
    if moved:
        scene.remap_fill_source_layers_after_move(layer_index, 0)
        layer_index = 0
        if saved_layer >= 0:
            saved_layer += 1

    scene.set_current_frame(saved_frame)
    scene.set_current_layer(saved_layer)
    scene.set_current_asset(saved_asset)
    print(f"[auto_mapping] created layer '{scene.layer_name(layer_index)}' in main_paint_view")
    return layer_index


def _discard_mapped_layer(scene, layer_index):
    """Roll back _create_mapped_layer when a run cannot commit anything.

    A failed or fully-clipped run must not leave an empty layer behind: with
    no history commit of its own, the orphan would silently ride along in the
    NEXT unrelated commit and could never be undone individually.
    """
    try:
        scene.delete_layer(layer_index)
        scene.remap_fill_source_layers_after_delete(layer_index)
    except Exception as error:
        print(f"[auto_mapping] could not roll back the empty mapped layer: {error}")


def _densify(points, step=POLY_STEP):
    """Split runs longer than `step` so straight stretches carry vertices too.

    The C++ flattener only subdivides CURVE elements: a lineTo contributes
    exactly two points no matter how long it is, so a 500 px straight stroke
    arrives here as a 2-point polyline. Two consequences, both real:
      * child mapping-area clipping tests inside/outside per vertex, so a
        region boundary crossing the middle of such a stroke is never seen;
      * the image of a straight source segment is a CURVE, and with only two
        anchors the sampler has to discover the whole deformation by probing
        - exactly the case its probes can be fooled on.
    Already-flattened curves arrive with ~4 px spacing, so this is a no-op
    for them; it only fills in the straight runs.
    """
    if len(points) < 2:
        return list(points)
    out = [points[0]]
    for a, b in zip(points, points[1:]):
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        # round(), not ceil(): a flattened curve arrives with spacing already
        # around `step`, and ceil() would split every one of those in two for
        # a few percent of overshoot. Only genuinely long runs get filled in.
        count = max(1, int(round(length / step))) if step > 0.0 else 1
        for k in range(1, count + 1):
            t = k / count
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out


def _stroke_polylines(stroke):
    """Per-subpath point lists (does NOT concatenate separate subpaths, unlike
    _stroke_points, so distinct subpaths never get a bogus joining segment)."""
    result = []
    for polyline in stroke.get("polylines") or []:
        pts = [(float(point["x"]), float(point["y"])) for point in polyline]
        if len(pts) >= 2:
            result.append(_densify(pts))
    if not result:
        raw = [(float(point["x"]), float(point["y"])) for point in (stroke.get("raw_points") or [])]
        if len(raw) >= 2:
            result.append(_densify(raw))
    return result


def _orientation(map_point, point):
    """Raw sign of det J at `point` (spec (5.1)); +1/-1, or 0 if degenerate."""
    child = map_point.child_frame
    main = map_point.main_frame
    l_h, l_v = map_point.coords(point)
    (tcx, tcy), (ncx, ncy) = child.directions(l_h, l_v)
    denominator = tcx * ncy - tcy * ncx
    if abs(denominator) < 1e-12:
        return 0  # folded child frame: no usable orientation here
    # The additional-line warp sits between the child arcs and the main
    # evaluation. Its own fold parity multiplies in: a strong additional
    # line legitimately folds the map (that is how it creates occlusion),
    # and det D(warp) carries that flip.
    warp = getattr(map_point, "warp", None)
    warp_sign = 1
    if warp is not None:
        warp_sign = warp.det_sign((l_h, l_v))
        l_h, l_v = warp.apply((l_h, l_v))
    scale_h = map_point.h_scales[1] if l_h >= 0.0 else map_point.h_scales[0]
    scale_v = map_point.v_scales[1] if l_v >= 0.0 else map_point.v_scales[0]
    (tmx, tmy), (nmx, nmy) = main.directions(l_h * scale_h, l_v * scale_v)
    value = warp_sign * scale_h * scale_v * (tmx * nmy - tmy * nmx) / denominator
    return 1 if value > 0.0 else -1


def _fold_sign(map_point, point):
    """+1 front / -1 back, measured RELATIVE TO THE CROSSING.

    The raw sign of det J is not the answer on its own. Drawing the main
    guides with the opposite handedness to the child ones flips it
    everywhere, so a plain `det J < 0` test labels the whole pattern - the
    crossing included - as the back of a fold, and the lining colour lands on
    the artwork. That handedness flip is a MIRROR, a global property already
    reported as info["mirrored"]; it is not a fold.

    The crossing is the one place both frames are pinned to each other, so it
    is the natural anchor: it is FRONT by definition, and "back" means the
    orientation has flipped relative to it - which is exactly what crossing a
    fold does. Measured on the reported project (mirrored frames): 69.3% of
    the pattern was being called back; anchored at the crossing the same 69.3%
    is front, and the 30.7% beyond the fold is back.
    """
    raw = _orientation(map_point, point)
    if raw == 0:
        return 1
    return 1 if raw == map_point.fold_reference else -1


def _split_by_fold(map_point, points):
    """Split a source polyline into runs of constant orientation.

    Returns [(run_points, side)] with side in (+1, -1); consecutive runs
    SHARE their boundary point, so front and back meet exactly (the map is
    continuous there - only its derivative flips, so a fold is never a gap).

    The boundary needs no root finding. Guide tangents are piecewise CONSTANT
    on polyline guides, so the orientation is constant inside a cell and can
    only flip at a cell boundary - which is exactly a structural knot (§6.2),
    already computed for the sampler. Measured: every sign flip along a
    densified source lands on a knot (worst gap 0.01 px).
    """
    if len(points) < 2:
        return [(list(points), 1)]

    pieces = []
    for index, (a, b) in enumerate(zip(points, points[1:])):
        bounds = [0.0] + _structural_knots(map_point, a, b) + [1.0]
        for t0, t1 in zip(bounds, bounds[1:]):
            if t1 - t0 <= 1e-12:
                continue
            pieces.append([index, a, b, t0, t1,
                           _fold_sign(map_point, _lerp(a, b, (t0 + t1) * 0.5))])
    if not pieces:
        return [(list(points), 1)]

    # Snap each side change onto the real boundary. The knots come from a
    # LINEAR interpolation of the coordinates over the segment, so on a coarse
    # source they can sit a few px off the true cell boundary - and then the
    # cut would not lie on the crease, which is derived independently from the
    # frames. _fold_sign is exact, so bisecting between the two neighbouring
    # midpoints pins the cut down and the two derivations agree by
    # construction. Folds are rare, so this costs a handful of solves per run.
    for left, right in zip(pieces, pieces[1:]):
        if left[5] == right[5] or left[0] != right[0]:
            continue  # same side, or the change falls on a source vertex
        a, b = left[1], left[2]
        lo = (left[3] + left[4]) * 0.5
        hi = (right[3] + right[4]) * 0.5
        for _ in range(30):
            mid = (lo + hi) * 0.5
            if _fold_sign(map_point, _lerp(a, b, mid)) == left[5]:
                lo = mid
            else:
                hi = mid
        boundary = (lo + hi) * 0.5
        left[4] = boundary
        right[3] = boundary

    runs = []
    first = pieces[0]
    current = [_lerp(first[1], first[2], first[3]), _lerp(first[1], first[2], first[4])]
    side = first[5]
    for piece in pieces[1:]:
        start = _lerp(piece[1], piece[2], piece[3])
        end = _lerp(piece[1], piece[2], piece[4])
        if piece[5] == side:
            current.append(end)
        else:
            runs.append((current, side))
            current = [start, end]
            side = piece[5]
    runs.append((current, side))
    return runs


def _crease_scan(map_point, row_range, axis, samples=48, max_columns=600):
    """One directional sweep of the fold locus, as curves of (l_h, l_v) pairs.

    det J vanishes where the main H direction turns parallel to the main V
    direction: f(l_h, l_v) = T_h(l_h) x T_v(l_v) = 0. A sweep fixes one
    coordinate per row and walks the OTHER guide for sign changes, bracketing
    and bisecting each. axis "h" (rows are V arcs, walk H) finds every locus
    transversal to the H direction; axis "v" is the transpose.

    ONE sweep cannot find loci that run parallel to its walking direction.
    With a coiling V guide and a near-straight H guide the locus is nearly
    HORIZONTAL in (l_h, l_v): at any fixed l_v the sign of f barely varies
    along l_h, so the "h" sweep sees no crossings at all - measured, the
    crease vanished entirely (0 branches, 82 of 82 cuts orphaned) while the
    same coil on the H guide (a near-vertical locus) worked fine. That is why
    _crease_curves runs BOTH sweeps and merges.

    Chaining is BY RANK while the count holds (sign changes along a row
    alternate, so ranks cannot swap); on a count change branches continue by
    nearest arc within a tight window (see the inline note).
    """
    main = map_point.main_frame
    low, high = row_range
    if axis == "h":
        scan_guide, scan_zero, scan_window = main.gh, main.h_arc, main.h_window
        row_guide, row_zero, row_window = main.gv, main.v_arc, main.v_window
    else:
        scan_guide, scan_zero, scan_window = main.gv, main.v_arc, main.v_window
        row_guide, row_zero, row_window = main.gh, main.h_arc, main.h_window
    if high - low <= 1e-6 or len(scan_guide.points) < 3:
        return [], []

    total = scan_guide.total
    step = max(POLY_STEP, total / 400.0)

    def crossings_at(row):
        normal = row_guide.tangent_at(row_zero + row, row_window)

        def side_at(arc):
            tangent = scan_guide.tangent_at(scan_zero + arc, scan_window)
            return tangent[0] * normal[1] - tangent[1] * normal[0]

        found = []
        arc = -scan_zero
        limit = total - scan_zero
        previous = side_at(arc)
        while arc < limit:
            nxt = min(arc + step, limit)
            value = side_at(nxt)
            if (value > 0.0) != (previous > 0.0):
                lo, hi = arc, nxt
                for _ in range(24):
                    mid = (lo + hi) * 0.5
                    if (side_at(mid) > 0.0) == (previous > 0.0):
                        lo = mid
                    else:
                        hi = mid
                found.append((lo + hi) * 0.5)
            arc, previous = nxt, value
        return found

    def hv_of(arc, row):
        return main.hv(arc, row) if axis == "h" else main.hv(row, arc)

    def column(row):
        arcs = crossings_at(row)
        return (row, arcs, [hv_of(arc, row) for arc in arcs])

    columns = [column(low + (high - low) * index / samples)
               for index in range(samples + 1)]

    # Refine in V until the crease polyline is fine enough IN IMAGE SPACE.
    # A uniform grid is not enough in the extreme: on a guide coiling through
    # 944 degrees the crease slid 23 px of H per px of V, so consecutive
    # samples were 70 px apart and a cut could sit 82 px from the polyline.
    #
    # Bounding the H arc alone was not enough either. The V spacing stayed at
    # (v_high - v_low)/48 - about 19 px on a tall drawing - and the image step
    # carries BOTH, so the chord could still cut the corner by more than the
    # anchor tolerance and throw a cut that is genuinely on the locus (0.00 px
    # at high resolution) 3 to 24 px off its own branch. Measured that way, a
    # cut was rejected and its whole branch deleted for want of anchors, and
    # in the worst case the entire crease layer vanished. The step that has to
    # be bounded is the one distances are measured in.
    #
    # Intervals where the crossing COUNT changes are refined too. Skipping
    # them was backwards: that is where a fold pair is born and the one place
    # the chaining below must decide whether two samples are the same branch,
    # and it was deciding it on the coarsest data in the sweep. Halving stops
    # at a floor, so a genuine bifurcation cannot spin.
    floor = (high - low) / 4096.0
    for _ in range(32):
        if len(columns) >= max_columns:
            break
        refined = [columns[0]]
        inserted = False
        for before, after in zip(columns, columns[1:]):
            if after[0] - before[0] > floor and len(refined) + 1 < max_columns:
                if len(before[1]) != len(after[1]):
                    coarse = True
                else:
                    coarse = any(_dist(p, q) > POLY_STEP
                                 for p, q in zip(before[2], after[2]))
                if coarse:
                    refined.append(column((before[0] + after[0]) * 0.5))
                    inserted = True
            refined.append(after)
        columns = refined
        if not inserted:
            break

    # Chain by rank. Branches alternate in sign, so the k-th crossing of one
    # column continues the k-th of the next; nearest-neighbour matching (what
    # this used to do) let a branch jump to a different one when a transient
    # pair appeared, which is what tied the crease in knots.
    curves = []
    open_curves = []
    previous_count = None
    for l_v, found, _image in columns:
        if previous_count == len(found) and open_curves:
            for rank, arc in enumerate(found):
                open_curves[rank].append((arc, l_v))
        elif open_curves:
            # The count changed - a fold pair appeared or vanished. Continuing
            # every branch by rank would now shift them all by one, so match
            # by arc instead, but only within a TIGHT window: refinement above
            # keeps a genuine continuation within POLY_STEP of its previous
            # sample, so anything further away is a different branch. (The
            # original code allowed a quarter of the guide's length here,
            # which is how branches used to swap and tie the crease in knots.)
            limit = 2.0 * POLY_STEP
            taken = set()
            survivors = []
            for curve in open_curves:
                best = None
                for index, arc in enumerate(found):
                    if index in taken:
                        continue
                    gap = abs(arc - curve[-1][0])
                    if best is None or gap < best[0]:
                        best = (gap, index, arc)
                if best is not None and best[0] <= limit:
                    taken.add(best[1])
                    curve.append((best[2], l_v))
                    survivors.append(curve)
                elif len(curve) >= 2:
                    curves.append(curve)
            for index, arc in enumerate(found):
                if index not in taken:
                    survivors.append([(arc, l_v)])
            open_curves = sorted(survivors, key=lambda curve: curve[-1][0])
        else:
            open_curves = [[(arc, l_v)] for arc in found]
        previous_count = len(found)
    curves.extend(curve for curve in open_curves if len(curve) >= 2)

    # The locus samples are exact (each is bisected onto det J = 0), but the
    # scanned position ripples a couple of px between adjacent rows. Smooth
    # that away so the cusp test in _emit_seals fires on real cusps only.
    smoothed = []
    for curve in curves:
        arcs = [arc for arc, _ in curve]
        for _ in range(2):
            arcs = ([arcs[0]]
                    + [(arcs[i - 1] + 2.0 * arcs[i] + arcs[i + 1]) * 0.25
                       for i in range(1, len(arcs) - 1)]
                    + [arcs[-1]])
        smoothed.append([(arcs[i], curve[i][1]) for i in range(len(curve))])

    # Grade each stretch by how well this sweep samples it. Where the locus
    # runs steeper than ~1:1 against the walk direction, consecutive rows
    # land far apart along the scan guide and the chain degenerates into long
    # chords (the refinement floor caps how finely rows can subdivide -
    # measured 12-23 px off the true locus there). The TRANSPOSED sweep sees
    # those stretches at slope < 1 and samples them densely, so they are
    # DEGRADED here, not deleted: an earlier version deleted them outright,
    # and everything the transposed sweep failed to re-cover - branch tips
    # (the slope is steepest there), stretches outside its row window - was
    # gone for good, shrinking branches under the minimum-length guard and
    # orphaning their cuts. The merge in _crease_curves accepts a degraded
    # stretch whenever nothing better covers it. The 25% margin keeps a band
    # both sweeps grade as good; the merge collapses the doubled band.
    good = []
    degraded = []
    for curve in smoothed:
        run = []
        run_ok = None
        for index, sample in enumerate(curve):
            ok = False
            for j in (index - 1, index):
                if 0 <= j < len(curve) - 1:
                    d_arc = abs(curve[j + 1][0] - curve[j][0])
                    d_row = abs(curve[j + 1][1] - curve[j][1])
                    if d_arc <= 1.25 * d_row + 1e-9:
                        ok = True
            if run_ok is None or ok == run_ok:
                run.append(sample)
                run_ok = ok
            else:
                (good if run_ok else degraded).append(run)
                run = [sample]
                run_ok = ok
        if run:
            (good if run_ok else degraded).append(run)

    # Normalize to (l_h, l_v) pairs whichever way the sweep ran.
    if axis != "h":
        good = [[(row, arc) for arc, row in curve] for curve in good]
        degraded = [[(row, arc) for arc, row in curve] for curve in degraded]
    return good, degraded


class _ArcGrid:
    """Spatial hash of locus segments in ARC space, for coverage queries.

    Cell size >= the query tolerance, so a point's 3x3 cell neighbourhood is
    guaranteed to contain every segment within tolerance of it.
    """

    def __init__(self, cell):
        self.cell = cell
        self.cells = {}

    def add_curve(self, curve):
        if len(curve) == 1:
            segments = [(curve[0], curve[0])]
        else:
            segments = list(zip(curve, curve[1:]))
        for a, b in segments:
            lo_x = int(min(a[0], b[0]) // self.cell)
            hi_x = int(max(a[0], b[0]) // self.cell)
            lo_y = int(min(a[1], b[1]) // self.cell)
            hi_y = int(max(a[1], b[1]) // self.cell)
            for cx in range(lo_x, hi_x + 1):
                for cy in range(lo_y, hi_y + 1):
                    self.cells.setdefault((cx, cy), []).append((a, b))

    def covered(self, point, tolerance):
        cx = int(point[0] // self.cell)
        cy = int(point[1] // self.cell)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for a, b in self.cells.get((cx + dx, cy + dy), ()):
                    ux, uy = b[0] - a[0], b[1] - a[1]
                    length_sq = ux * ux + uy * uy
                    if length_sq <= 0.0:
                        t = 0.0
                    else:
                        t = ((point[0] - a[0]) * ux + (point[1] - a[1]) * uy) / length_sq
                        t = min(1.0, max(0.0, t))
                    gx = point[0] - (a[0] + ux * t)
                    gy = point[1] - (a[1] + uy * t)
                    if gx * gx + gy * gy <= tolerance * tolerance:
                        return True
        return False


def _uncovered_runs(curve, grid, tolerance):
    """The stretches of `curve` not already covered in the grid.

    Single-point runs are KEPT: the transposed copy of a culled branch tip is
    often exactly one sample (everything past it is covered), and dropping it
    was measured to cost a whole branch - the tip is what the stitcher needs
    to reach the anchoring cut. An isolated single point that never stitches
    onto anything is filtered after stitching instead.

    Coverage is judged in ARC space rather than image space because a folded
    sheet can bring two DISTINCT loci close together in the image while they
    stay far apart in the domain.
    """
    runs = []
    run = []
    for point in curve:
        if grid.covered(point, tolerance):
            if run:
                runs.append(run)
            run = []
        else:
            run.append(point)
    if run:
        runs.append(run)
    return runs


def _stitch_crease(pieces, tolerance, boundaries):
    """Join locus stretches whose endpoints meet, into whole loci.

    The two sweeps each contribute the stretches they sample well, so one
    physical locus arrives as alternating h- and v-conditioned pieces (plus
    the sweep's own conditioning splits). Anchoring MUST see whole loci:
    trimming fragments independently left each with at most one cut, and the
    single-cut rule then dropped them all - measured 5 of 16 cuts orphaned on
    a project that was fully anchored before the split sweep.

    Endpoints are matched in ARC space, where distinct loci stay far apart.
    A fold pair's two branches genuinely MEET at their birth point, so
    joining them there reconstructs the true topology (one continuous curve
    turning around); the image-space cusp splitter separates the arms again
    for drawing. The one refusal: endpoints that both sit on the SAME sweep
    boundary (the locus continues outside the swept window) are edge
    truncations, not meetings.

    Stitching serves DRAWING AND ANCHORING ONLY. At a junction of crossing
    loci (corner lines over each other or over smooth branches) several
    endpoints meet, the pairing is genuinely under-determined, and any
    greedy choice can chain arms of different loci into one path. That is
    harmless here - every piece is real locus geometry, the trim only needs
    connectivity, and direction-based join guards were each measured to
    break something real (a birth join at slopes near +/-1 on a cornerless
    project; the corner-to-smooth T-continuation, cutting the drawn crease
    from 197 to 113 px). The consumer that CANNOT tolerate chained paths -
    the fill splitter, whose _cut_ring_by_polyline pairs crossings along
    each cutter - takes the UNSTITCHED curve set instead (_crease_curves
    stitch=False), where injected corner lines are whole and every curve is
    a single locus.
    """
    pieces = {index: list(piece) for index, piece in enumerate(pieces) if piece}

    def on_same_boundary(a, b):
        for index, low, high in boundaries:
            for edge in (low, high):
                if (abs(a[index] - edge) <= 1e-6 and abs(b[index] - edge) <= 1e-6):
                    return True
        return False

    # Endpoint bucket grid + a lazily validated heap. The first version
    # rescanned every piece pair after every join - O(P^3) - and on ruffled
    # guides (8-lobe sines on both axes, 480 pieces) one Auto Mapping click
    # froze the UI for 13-15 s, 55+ s at 10 lobes. After a join only the
    # merged piece's endpoints gain new candidates, so the scan happens once
    # and each join adds local work only.
    cell = tolerance

    def bucket_key(point):
        return (int(point[0] // cell), int(point[1] // cell))

    buckets = {}

    def add_ends(pid):
        piece = pieces[pid]
        for end in (0, -1):
            buckets.setdefault(bucket_key(piece[end]), set()).add((pid, end))

    def remove_ends(pid, piece):
        for end in (0, -1):
            entries = buckets.get(bucket_key(piece[end]))
            if entries:
                entries.discard((pid, end))

    def candidates_for(pid):
        found = []
        piece = pieces[pid]
        for end in (0, -1):
            point = piece[end]
            cx, cy = bucket_key(point)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for other_id, other_end in buckets.get((cx + dx, cy + dy), ()):
                        if other_id == pid:
                            continue
                        other = pieces[other_id][other_end]
                        gap = math.hypot(point[0] - other[0], point[1] - other[1])
                        if gap <= tolerance and not on_same_boundary(point, other):
                            found.append((gap, pid, end, point, other_id, other_end, other))
        return found

    heap = []
    for pid in pieces:
        add_ends(pid)
    for pid in pieces:
        for candidate in candidates_for(pid):
            heapq.heappush(heap, candidate)

    while heap:
        gap, a_id, a_end, a_point, b_id, b_end, b_point = heapq.heappop(heap)
        if a_id not in pieces or b_id not in pieces:
            continue  # one side was already merged away
        if pieces[a_id][a_end] != a_point or pieces[b_id][b_end] != b_point:
            continue  # stale candidate: that endpoint moved in a merge
        remove_ends(a_id, pieces[a_id])
        remove_ends(b_id, pieces[b_id])
        a, b = pieces[a_id], pieces[b_id]
        if a_end == -1:
            a.extend(b if b_end == 0 else reversed(b))
        else:
            pieces[a_id] = (b if b_end == -1 else list(reversed(b))) + a
        del pieces[b_id]
        add_ends(a_id)
        for candidate in candidates_for(a_id):
            heapq.heappush(heap, candidate)
    return list(pieces.values())


def _corner_loci(map_point, v_range, h_range, spans=None, step=POLY_STEP):
    """Exact fold loci of guide CORNERS: constant-arc lines in arc space.

    A sharp corner folds the map along the corner's preimage - a straight
    line l_h = m_c (H corner) or l_v = m_c (V corner) in arc space, exactly
    where the position field kinks. The sweeps do trace these lines, but as
    the worst-conditioned geometry they meet: each line is parallel to one
    sweep's walk (invisible), ill-conditioned for the other (a handful of
    samples), and where corner lines cross each other or a smooth locus the
    rank chaining breaks all arms apart - measured on a two-corner-H plus
    one-corner-V project: the V line arrived as three fragments with a 40 px
    hole at a junction, and the fill splitter downstream lost the whole back
    band. There is nothing to trace: the line's position is known, and the
    only computation is its EXTENT - the rows where the sign of T_h x T_v
    actually differs across the corner (the row direction falls inside the
    corner's wedge). Rows are tested with the same windowed tangents the
    sweeps use, so these curves are drop-in members of the same pool - just
    exact, complete, and continuous through every junction.
    """
    main = map_point.main_frame
    curves = []
    # The line POSITION gate uses the artwork's true arc extent (`spans`,
    # unpadded), not the padded scan window: a fold at an arc no material
    # reaches folds nothing - injecting it drew a 140 px dashed crease
    # across visually flat artwork (the corner sat 2.5 px from the guide
    # end, its line 14 px beyond the pattern, inside the pad). The ROW
    # ranges stay padded: along the line, branch tips still need room to
    # reach their anchors (6.1).
    h_span, v_span = spans if spans is not None else (h_range, v_range)

    def lines(guide, zero, window, other_guide, other_zero, other_window,
              span, rows, transpose):
        for raw in guide.sharp_arcs:
            m_c = raw - zero
            if not (span[0] <= m_c <= span[1]):
                continue
            # One-sided WINDOWED tangents (the corner clamp makes tangent_at
            # just beside the vertex exactly one-sided): _fold_sign classifies
            # with these, so the line's extent must be judged with them too -
            # exact tangents were measured to disagree on borderline rows and
            # leave the extent 14 px short of where the field actually flips.
            before = guide.tangent_at(max(0.0, raw - 1e-3), window)
            after = guide.tangent_at(min(guide.total, raw + 1e-3), window)

            def flips(row):
                normal = other_guide.tangent_at(other_zero + row, other_window)
                f_before = before[0] * normal[1] - before[1] * normal[0]
                f_after = after[0] * normal[1] - after[1] * normal[0]
                return (f_before > 0.0) != (f_after > 0.0)

            def boundary(inside, outside):
                for _ in range(24):
                    mid = (inside + outside) * 0.5
                    if flips(mid):
                        inside = mid
                    else:
                        outside = mid
                return inside

            def emit(run):
                if len(run) >= 2:
                    curves.append([(r, m_c) if transpose else (m_c, r)
                                   for r in run])

            run = []
            previous = None
            row = rows[0]
            while True:
                if flips(row):
                    if not run and previous is not None:
                        run.append(boundary(row, previous))
                    run.append(row)
                else:
                    if run:
                        run.append(boundary(run[-1], row))
                        emit(run)
                        run = []
                previous = row
                if row >= rows[1]:
                    break
                row = min(rows[1], row + step)
            emit(run)

    lines(main.gh, main.h_arc, main.h_window, main.gv, main.v_arc,
          main.v_window, h_span, v_range, False)
    lines(main.gv, main.v_arc, main.v_window, main.gh, main.h_arc,
          main.h_window, v_span, h_range, True)
    return curves


def _child_of_arcs(map_point, arcs):
    """Child-space point of a MAIN-arc coordinate pair (loci live in main arcs).

    The inverse route of _arc_of_point: unscale, then UNWARP (additional
    lines), then the child frame - so loci probes and cutters land on the
    same child points the forward map folds at.
    """
    a_h, a_v = arcs
    h_scales, v_scales = map_point.h_scales, map_point.v_scales
    third = (a_h / (h_scales[1] if a_h >= 0.0 else h_scales[0]),
             a_v / (v_scales[1] if a_v >= 0.0 else v_scales[0]))
    warp = getattr(map_point, "warp", None)
    if warp is not None:
        third = warp.unapply(third)
    return map_point.child_frame.hv(*third)


def _curve_is_fold(map_point, curve, probe=POLY_STEP, samples=5):
    """Does the field actually CHANGE SIDE across this traced curve?

    The sweeps trace zeros of the analytic factor per row, but near a corner
    row the windowed tangent swings so fast that isolated per-row zeros
    chain into a long near-horizontal "curve" spanning the guide - while the
    field on either side never flips: at most a fold PAIR thinner than the
    probe hides inside (measured ~1.5 arc px - visually nothing, and the
    pointwise splitters already treat it as parity-neutral). Such a phantom
    still collected anchors from fill-edge crossings and drew a 140 px
    dashed crease across visually flat artwork, and its cutter shifted
    depth counts by a parity-neutral +2. Probing _fold_sign one step to
    each side at a few spots along the curve separates real creases (any
    sample flips - even near a birth point the arms part beyond the probe
    somewhere mid-branch) from phantoms (no sample flips anywhere).
    """
    count = len(curve)
    if count < 2:
        return False
    tested = 0
    for k in range(samples):
        index = max(0, min(count - 2, int((k + 0.5) * (count - 1) / samples)))
        pa = _child_of_arcs(map_point, curve[index])
        pb = _child_of_arcs(map_point, curve[index + 1])
        dx, dy = pb[0] - pa[0], pb[1] - pa[1]
        length = math.hypot(dx, dy)
        if length <= 1e-9:
            continue
        nx, ny = -dy / length, dx / length
        mid = ((pa[0] + pb[0]) * 0.5, (pa[1] + pb[1]) * 0.5)
        tested += 1
        if (_fold_sign(map_point, (mid[0] + nx * probe, mid[1] + ny * probe))
                != _fold_sign(map_point, (mid[0] - nx * probe, mid[1] - ny * probe))):
            return True
    return tested == 0  # nothing to judge: keep, as before this filter


def _crease_curves(map_point, v_range, h_range=None, samples=48, max_columns=600,
                   stitch=True, corner_spans=None):
    """The fold loci, traced from the FRAMES rather than from the strokes.

    stitch=True returns loci joined into whole paths for drawing and
    anchoring; stitch=False returns the raw deduped curve SET (each curve a
    single locus) for the fill splitter and depth counting, where chained
    paths would mis-pair ring crossings - see _stitch_crease.

    corner_spans carries the artwork's UNPADDED (h, v) arc extents for the
    corner-line position gate (_corner_loci): the ranges themselves arrive
    padded so branch tips can reach their anchors, but a corner line at an
    arc no material reaches must not be injected at all.

    Both sweep directions run (see _crease_scan: one sweep is blind to loci
    parallel to its walk - a coiling V guide over a near-straight H guide
    lost the crease entirely). Each sweep keeps its well-conditioned
    stretches, the transposed sweep's duplicates are dropped in arc space,
    and the surviving stretches are stitched back into whole loci so the
    anchoring downstream sees complete branches.

    The crease is NOT in general a translated copy of the V guide - that only
    holds when the main V guide is straight, so that its direction (and hence
    the critical H position) is the same at every height. On a curved V guide
    the locus really does sweep along H: measured on a 211 px V guide bowing
    45 px off its chord, it travels 112-123 px, and that survives even a 65 px
    smoothing window, so it is geometry rather than noise.

    Deriving this per STROKE instead - recording where each stroke happened to
    change side - was wrong: several strokes crossing the SAME fold each
    reported a slightly different arc, so one fold was drawn as four copies.
    """
    h_good, h_bad = _crease_scan(map_point, v_range, "h", samples, max_columns)
    boundaries = [(1, v_range[0], v_range[1])]
    if h_range is None:
        pieces = h_good + h_bad
    else:
        v_good, v_bad = _crease_scan(map_point, h_range, "v", samples, max_columns)
        boundaries.append((0, h_range[0], h_range[1]))
        # Priority merge: geometry is only ever DROPPED when something at
        # least as good already covers it. Well-conditioned stretches first
        # (the h sweep's, then the transposed sweep's exact fill of what h
        # graded badly), then the degraded stretches as fallback for whatever
        # neither sweep sampled well - branch tips, stretches outside the
        # other sweep's row window. Degraded beats deleted: it is exactly the
        # quality the single-sweep tracer always had there.
        tolerance = 2.5 * POLY_STEP
        grid = _ArcGrid(4.0 * POLY_STEP)
        pieces = []
        # Corner lines first: they are exact and continuous through every
        # junction, so the sweeps' fragmented versions of the same lines
        # dedupe away against them instead of the other way round.
        for pool in (_corner_loci(map_point, v_range, h_range, corner_spans),
                     h_good, v_good, h_bad, v_bad):
            for curve in pool:
                for run in _uncovered_runs(curve, grid, tolerance):
                    pieces.append(run)
                    grid.add_curve(run)
    def _with_warp_loci(curves):
        """Append the additional-line warp's own fold loci - exact analytic
        curves the frame sweeps are structurally blind to (they test main
        tangents only). Their child-space geometry is registered by object
        identity so _child_cutters can bypass the fixed-point unwarp,
        which is unreliable exactly where the warp folds; the same
        identity set exempts them from _curve_is_fold, whose probes go
        through that unwarp."""
        warp = getattr(map_point, "warp", None)
        if warp is None:
            return curves
        child_frame = map_point.child_frame
        ids = dict(getattr(map_point, "warp_curve_child", {}) or {})
        thirds = dict(getattr(map_point, "warp_curve_third", {}) or {})
        # Pin every registered curve object: the registry accumulates
        # across calls, and an id() of a garbage-collected list could be
        # recycled by a NEW curve, silently resolving to the wrong child
        # geometry. A held reference makes that impossible.
        keep = list(getattr(map_point, "warp_curve_keep", []) or [])
        for third_curve in warp.fold_loci():
            arc_curve = [map_point.scale_arcs(*warp.apply(t))
                         for t in third_curve]
            if len(arc_curve) < 2:
                continue
            ids[id(arc_curve)] = [child_frame.hv(*t) for t in third_curve]
            # The NATIVE Third geometry rides along: depth rays must count
            # warp loci in base Third - their unfolding space - because in
            # arc space the warp has already folded, so a warp locus there
            # is a fold EDGE image whose two sides are NOT different
            # sheets (counting it in arc space lifted an untouched far
            # region to depth 2 on add_s_error). No extension: fold_loci's
            # two sweep directions close a band's boundary at its ends, so
            # the ray count is the topological winding against a closed
            # boundary - extending each piece to infinity instead stacked
            # spurious crossings and blew the S-line document up to depth
            # 7 on ground the map covers twice.
            thirds[id(arc_curve)] = [tuple(t) for t in third_curve]
            keep.append(arc_curve)
            curves.append(arc_curve)
        map_point.warp_curve_child = ids
        map_point.warp_curve_third = thirds
        map_point.warp_curve_keep = keep
        return curves

    if not stitch:
        # The fill splitter wants the SET of loci, not paths: every curve a
        # single locus (injected corner lines arrive whole), so its ring
        # crossings pair correctly along each cutter. Junction bridges and
        # any chained arms the path-building below may produce are for
        # drawing and anchoring only - see _stitch_crease. Phantom curves
        # the field never flips across (_curve_is_fold) are dropped here so
        # they neither cut fills nor shift depth counts.
        return _with_warp_loci(
            [curve for curve in pieces
             if len(curve) >= 2 and _curve_is_fold(map_point, curve)])
    stitched = _stitch_crease(pieces, 4.0 * POLY_STEP, boundaries)
    return _with_warp_loci(
        [curve for curve in stitched if len(curve) >= 2])


def _split_at_cusps(points):
    """Break a crease polyline where it reverses on itself.

    ON the fold the two guide directions are parallel by definition, so the
    crease point H(s_x + a(t)) + V(t_x + t) - O has velocity
    (a'(t) +/- 1) * T_h: one fixed direction times a scalar. Where that scalar
    passes through zero the curve stops dead and comes back along itself - a
    cusp - and drawing it as one stroke ties the visible knot.

    Measured on the reported project: the branch whose slide rate a'(t) stayed
    below 1 (mean 0.48) never stalled - minimum step 3.72 px, no crossing -
    while the branch whose rate crossed 1 (max 2.58) fell to a 0.20 px step
    and crossed itself once. Note the arc stays MONOTONE through this, which
    is why splitting on arc reversal missed it entirely.
    """
    if len(points) < 3:
        return [points]
    pieces = []
    start = 0
    for index in range(1, len(points) - 1):
        ax = points[index][0] - points[index - 1][0]
        ay = points[index][1] - points[index - 1][1]
        bx = points[index + 1][0] - points[index][0]
        by = points[index + 1][1] - points[index][1]
        if math.hypot(ax, ay) < 1e-9 or math.hypot(bx, by) < 1e-9:
            continue
        if ax * bx + ay * by < 0.0:           # direction reversed: a cusp
            if index + 1 - start >= 2:
                pieces.append(points[start:index + 1])
            start = index
    if len(points) - start >= 2:
        pieces.append(points[start:])
    return pieces


def _polyline_arc_of(point, points):
    """(distance, arc along `points`) of the closest point on the polyline."""
    best = (float("inf"), 0.0)
    run = 0.0
    for a, b in zip(points, points[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length <= 0.0:
            continue
        t = min(1.0, max(0.0, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy)
                         / (length * length)))
        gap = math.hypot(point[0] - (a[0] + dx * t), point[1] - (a[1] + dy * t))
        if gap < best[0]:
            best = (gap, run + t * length)
        run += length
    return best


def _polyline_slice(points, start, end):
    """The part of a polyline between two arc positions along it."""
    sliced = []
    run = 0.0
    for a, b in zip(points, points[1:]):
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        if length <= 0.0:
            continue
        low, run = run, run + length
        t0 = max(0.0, (start - low) / length)
        t1 = min(1.0, (end - low) / length)
        if t1 <= t0:
            continue
        if not sliced:
            sliced.append(_lerp(a, b, t0))
        sliced.append(_lerp(a, b, t1))
    return sliced


def _trim_to_anchors(run, cuts, tolerance):
    """Keep only the part of a crease that lies BETWEEN two cuts.

    The cuts are the only VERTICES the drawing gives the crease: the frames
    say where a fold COULD be, a cut says where one IS.

    A fold ends at a vertex or at the edge of the material; one that just
    stops in the middle of an intact sheet is not a fold. The crease here is
    derived from the FRAMES (§4.5), so it exists along the whole guide whether
    or not anything is drawn there, and the stretch past the outermost cut is
    a locus rather than a fold - nothing changes side across it, so there is
    no front edge for it to terminate. Measured on the reported project, that
    unanchored stretch was 61% of everything drawn, including a 242 px tail.

    Between two cuts the crease is kept even where no stroke touches it. The
    alternative - pair each cut with the one the same RUN of artwork joins it
    to, and keep only those spans - is stricter and was measured: on the two
    reported projects it left 7.6 px of 445 and 0.0 px of 220, because a line
    drawing mostly crosses a fold ONCE per stroke (16 cuts there yielded 6
    two-ended runs), so the pairs that survive are between strokes anyway.

    The cost of the rule is a branch carrying exactly ONE cut: it is dropped
    whole, so a fold the artwork crosses only once gets no crease at all even
    though its BACK run is real and its front edge is left unterminated. That
    is the intended behaviour here - a single crossing gives a point, and a
    crease needs an interval, and inventing one is what this function exists
    to stop - but it is a choice, not a theorem. It costs nothing on dense
    line art, where every branch collects several cuts; it costs the whole
    crease on a sketch of three strokes over the same guides.
    """
    if len(run) < 2 or len(cuts) < 2:
        return []
    arcs = sorted(arc for gap, arc in (_polyline_arc_of(cut, run) for cut in cuts)
                  if gap <= tolerance)
    if len(arcs) < 2:
        return []
    return _polyline_slice(run, arcs[0], arcs[-1])


def _stroke_style(stroke, width_scale):
    color = stroke.get("color") or {}
    color_tuple = (int(color.get("r", 0)), int(color.get("g", 0)),
                   int(color.get("b", 0)), int(color.get("a", 255)))
    width = max(0.5, float(stroke.get("width", 3.0)) * width_scale)
    return color_tuple, width


# ---------------------------------------------------------------------------
# fold depth (layer stacking from the nearest-end anchor) and fill topology
# ---------------------------------------------------------------------------

def _prepare_fold_context(map_point, h_range, v_range):
    """Attach the depth machinery to the mapper, once per run.

    Depth of a sheet point = number of creases crossed between it and the
    nearest-end anchor, walked as a straight segment in ARC space (where the
    loci are clean disjoint curves). Depth decides layer STACKING only;
    front/back COLORING stays with _fold_sign, and a parity mismatch between
    the two (a graze, a missed tangency) is settled in _fold_sign's favour by
    bumping the depth one step - each crease crossed flips the face, so depth
    parity and face must agree.
    """
    main = map_point.main_frame
    anchor = _nearest_arc()
    # Clamp a stale anchor (guides redrawn shorter since it was stored) onto
    # the sheet, and FOLD IT INTO THE SWEEP WINDOW: crossings between the
    # anchor and the artwork that fall outside the traced window would be
    # silently missed, and the parity fix would then merge distinct fold
    # layers instead of stacking them.
    anchor = (min(max(anchor[0], -main.h_arc), main.h_total - main.h_arc),
              min(max(anchor[1], -main.v_arc), main.v_total - main.v_arc))
    pad = 4.0 * POLY_STEP
    map_point.depth_curves = _crease_curves(
        map_point,
        (min(v_range[0], anchor[1]) - pad, max(v_range[1], anchor[1]) + pad),
        (min(h_range[0], anchor[0]) - pad, max(h_range[1], anchor[0]) + pad),
        stitch=False, corner_spans=(h_range, v_range))
    map_point.depth_anchor = anchor
    warp = getattr(map_point, "warp", None)
    if warp is not None:
        # The anchor's BASE Third position, for counting warp loci in
        # their own unfolding space. The anchor normally sits outside
        # every pink line's band, where unapply is the exact identity;
        # inside a fold band it is best-effort like every other consumer.
        map_point.depth_anchor_third = warp.unapply(
            map_point.unscale_arcs(*anchor))
    else:
        map_point.depth_anchor_third = None
    map_point.child_cutters = None
    # Build the cutters NOW, not lazily: for warp loci this also swaps the
    # depth-ray Third geometry to the extended cutter's coords preimage,
    # and the first depth query (stroke runs come before fills) must
    # already see the aligned geometry.
    _child_cutters(map_point)


def _arc_of_point(map_point, point):
    """A child point's MAIN arc coordinates (the space depth is counted in).

    Routed through the additional-line warp: depth counting, the loci and
    the cutters must all live in the SAME arc space the map evaluates in,
    or cuts drift off creases (the positional-consistency rule).
    """
    l_h, l_v = map_point.coords(point)
    warp = getattr(map_point, "warp", None)
    if warp is not None:
        l_h, l_v = warp.apply((l_h, l_v))
    return (l_h * (map_point.h_scales[1] if l_h >= 0.0 else map_point.h_scales[0]),
            l_v * (map_point.v_scales[1] if l_v >= 0.0 else map_point.v_scales[0]))


def _fold_depth(map_point, point, side):
    """Stacking depth of a child point (0 = nearest to the viewer).

    Each crease is counted in ITS OWN unfolding space - the space where
    crossing it really is stepping onto another sheet. Frame creases
    unfold in main-arc space (hv is single-valued there), and their count
    applies uniformly to every warp sheet stacked at an arc position.
    Warp creases unfold in BASE Third space - by the time the arcs exist
    the warp has already folded, so a warp locus in arc space is a fold
    edge IMAGE whose two sides are the same sheet; counting it there
    lifted a physically single-sheet region to depth 2 (add_s_error).
    """
    curves = getattr(map_point, "depth_curves", None)
    if not curves:
        return 0 if side == _MappedOutput.FRONT else 1
    anchor = map_point.depth_anchor
    warp_third = getattr(map_point, "warp_curve_third", {}) or {}
    anchor_third = getattr(map_point, "depth_anchor_third", None)
    # One frame solve, shared by both rays; the arc ray is derived (and
    # the warp applied) only if a frame curve actually needs it.
    point_third = None
    arc = None
    depth = 0
    for curve in curves:
        native = warp_third.get(id(curve)) if warp_third else None
        if native is not None and anchor_third is not None:
            if point_third is None:
                point_third = map_point.coords(point)
            ray_a, ray_b = anchor_third, point_third
            segments = zip(native, native[1:])
        else:
            if arc is None:
                if point_third is None:
                    point_third = map_point.coords(point)
                z = point_third
                warp = getattr(map_point, "warp", None)
                if warp is not None:
                    z = warp.apply(z)
                arc = map_point.scale_arcs(*z)
            ray_a, ray_b = anchor, arc
            segments = zip(curve, curve[1:])
        for a, b in segments:
            hit = _segment_intersection(ray_a, ray_b, a, b)
            if hit is None:
                continue
            t, u = hit
            # Half-open on both parameters so a crossing shared by two
            # consecutive segments is counted once.
            if 0.0 <= t < 1.0 and 0.0 <= u < 1.0:
                depth += 1
    if (depth % 2 == 0) != (side == _MappedOutput.FRONT):
        depth += 1
    return depth


def _run_depth(map_point, run, side):
    if not _FOLD["split"] or len(run) == 0:
        return 0 if side == _MappedOutput.FRONT else 1
    return _fold_depth(map_point, run[len(run) // 2], side)


def _child_cutters(map_point):
    """The crease loci as polylines in CHILD image space, for cutting fills.

    The depth curves live in MAIN arc coordinates; dividing by the per-side
    transfer scales gives child arcs, and the child frame's hv puts them on
    the texture. In child space the loci are plain curves - the cusps are an
    IMAGE-space phenomenon - so they are valid polygon cutters.

    The depth curves are the raw locus SET (stitch=False), so where loci
    cross, a curve can END at the junction - INSIDE a fill. A cutter that
    dead-ends inside a ring crosses it an odd number of times and cuts
    nothing (see t_cutter), which was measured to collapse a three-depth
    fill into one uncut piece. Ends are therefore extended STRAIGHT until
    they leave everything the pattern can occupy: a cut along the extension
    is harmless - the two sides have the same crossing parity against the
    real loci, so they land at the same depth and colour - while a missing
    cut loses whole occlusion bands. The pre-extension geometry is kept on
    map_point.child_cutters_raw: crease anchors must come only from
    crossings with REAL loci, never with an extension.
    """
    cutters = getattr(map_point, "child_cutters", None)
    if cutters is not None:
        return cutters
    child = map_point.child_frame
    h_scales, v_scales = map_point.h_scales, map_point.v_scales
    reach = 2.0 * (child.gh.total + child.gv.total)
    bare = []
    direct = getattr(map_point, "warp_curve_child", {}) or {}
    for curve in getattr(map_point, "depth_curves", None) or []:
        if id(curve) in direct:
            # A warp fold locus carries its exact child geometry: the
            # fixed-point unwarp is unreliable precisely where the warp
            # folds, so it is bypassed.
            points = list(direct[id(curve)])
        else:
            # Through _child_of_arcs so the additional-line warp's inverse
            # is applied: a cutter must lie where the WARPED map folds.
            points = [_child_of_arcs(map_point, arc_pair) for arc_pair in curve]
        if len(points) >= 2:
            bare.append(points)
    raw = [_densify(list(points)) for points in bare]
    cutters = []
    for index, points in enumerate(bare):
        points = list(points)
        for end, other in ((0, 1), (-1, -2)):
            dx = points[end][0] - points[other][0]
            dy = points[end][1] - points[other][1]
            length = math.hypot(dx, dy)
            if length <= 1e-9:
                continue
            # Every end gets the full reach. Junction ends and dedupe splice
            # ends dead-end INSIDE fills and shorter overhangs measurably
            # under-cut (a depth-3 fill vanished at 32 px, and distance
            # heuristics for "which ends are junctions" kept missing cases
            # because arc-space gaps stretch unpredictably through the
            # transfer scales). The cost is cosmetic only - extension
            # crossings split same-depth pieces that render identically -
            # and the bbox gate in _split_ring_by_fold keeps them off
            # rings the real locus never approaches.
            tip = points[end]
            extended = (tip[0] + dx / length * reach, tip[1] + dy / length * reach)
            if end == 0:
                points.insert(0, extended)
            else:
                points.append(extended)
        cutters.append(_densify(points))
    map_point.child_cutters = cutters
    map_point.child_cutters_raw = raw
    return cutters


def _point_in_ring(point, ring):
    """Odd-even test against a closed ring (implicit closing edge)."""
    inside = False
    n = len(ring)
    for i in range(n):
        a = ring[i]
        b = ring[(i + 1) % n]
        if (a[1] > point[1]) != (b[1] > point[1]):
            span = b[1] - a[1]
            x = a[0] + (b[0] - a[0]) * (point[1] - a[1]) / span
            if point[0] < x:
                inside = not inside
    return inside


def _point_in_polygons(point, polygons):
    """Odd-even over a polygon list (the mapping-area convention)."""
    inside = False
    for polygon in polygons or []:
        if len(polygon) >= 3 and _point_in_ring(point, polygon):
            inside = not inside
    return inside


def _ring_interior_point(ring):
    """A point strictly inside the ring.

    The centroid first; then edge midpoints nudged INWARD along the edge
    normal (both signs tried). Plain vertex-pair midpoints were not enough:
    on a densified ring the midpoint of two vertices spanning a locally
    straight stretch lies exactly ON the boundary, and the fold side of such
    a point is decided by rounding noise - after a crease cut, part of every
    piece's boundary IS the crease.
    """
    n = len(ring)
    cx = sum(p[0] for p in ring) / n
    cy = sum(p[1] for p in ring) / n
    if _point_in_ring((cx, cy), ring):
        return (cx, cy)
    stride = max(1, n // 12)
    for i in range(0, n, stride):
        a = ring[i]
        b = ring[(i + 1) % n]
        mx, my = (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length <= 1e-9:
            continue
        nx, ny = -dy / length, dx / length
        for offset in (0.35, -0.35, 1.0, -1.0):
            candidate = (mx + nx * offset, my + ny * offset)
            if _point_in_ring(candidate, ring):
                return candidate
    return (cx, cy)


def _cut_ring_by_polyline(ring, cutter, crossings_out=None):
    """Split a closed ring by an open polyline; returns the resulting rings.

    Crossings of the cutter with the ring boundary are collected exactly and
    ordered ALONG THE CUTTER; each stretch of cutter between two consecutive
    crossings whose midpoint lies inside the ring is a CHORD, and every chord
    splits the ring piece containing its endpoints in two. Consecutive
    stretches alternate inside/outside (each crossing is transversal), so
    chords never share endpoints and never cross each other, which is what
    lets them be applied one at a time.

    A locus that dead-ends INSIDE the ring (a fold pair born within the fill)
    contributes no chord and does not split - correct, since the region wraps
    over such a fold without its boundary changing sides.
    """
    n = len(ring)
    # Bounding-box gate: both operands are densified at POLY_STEP, so the
    # naive all-pairs intersection scan grows with board size squared; most
    # cutter segments are nowhere near the ring.
    ring_x0 = min(p[0] for p in ring)
    ring_x1 = max(p[0] for p in ring)
    ring_y0 = min(p[1] for p in ring)
    ring_y1 = max(p[1] for p in ring)
    cut_x0 = min(p[0] for p in cutter)
    cut_x1 = max(p[0] for p in cutter)
    cut_y0 = min(p[1] for p in cutter)
    cut_y1 = max(p[1] for p in cutter)
    if (cut_x1 < ring_x0 or cut_x0 > ring_x1
            or cut_y1 < ring_y0 or cut_y0 > ring_y1):
        return [ring]
    crossings = []
    for j in range(len(cutter) - 1):
        c0, c1 = cutter[j], cutter[j + 1]
        if (max(c0[0], c1[0]) < ring_x0 or min(c0[0], c1[0]) > ring_x1
                or max(c0[1], c1[1]) < ring_y0 or min(c0[1], c1[1]) > ring_y1):
            continue
        for i in range(n):
            a, b = ring[i], ring[(i + 1) % n]
            hit = _segment_intersection(a, b, c0, c1)
            if hit is None:
                continue
            t, u = hit
            if 0.0 <= t < 1.0 and 0.0 <= u < 1.0:
                point = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                crossings.append((j + u, point))
    if len(crossings) < 2:
        return [ring]
    crossings.sort(key=lambda entry: entry[0])
    deduped = [crossings[0]]
    for entry in crossings[1:]:
        if entry[0] - deduped[-1][0] > 1e-9:
            deduped.append(entry)
    crossings = deduped
    if len(crossings) < 2:
        return [ring]

    def cutter_point(position):
        j = min(len(cutter) - 2, int(position))
        u = position - j
        a, b = cutter[j], cutter[j + 1]
        return (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u)

    def cutter_slice(p0, p1):
        points = [crossings[p0][1]]
        j0 = int(crossings[p0][0])
        j1 = int(crossings[p1][0])
        for j in range(j0 + 1, j1 + 1):
            points.append(cutter[j])
        points.append(crossings[p1][1])
        return points

    chords = []
    for k in range(len(crossings) - 1):
        mid = cutter_point((crossings[k][0] + crossings[k + 1][0]) * 0.5)
        if _point_in_ring(mid, ring):
            chords.append(cutter_slice(k, k + 1))
            if crossings_out is not None:
                # The chord's endpoints are where this ring genuinely changes
                # side - a fill's equivalent of a stroke's cut, and the
                # anchors the crease needs to span the fill's fold edge.
                crossings_out.append(crossings[k][1])
                crossings_out.append(crossings[k + 1][1])

    pieces = [list(ring)]
    for chord in chords:
        for index, piece in enumerate(pieces):
            split = _split_ring_with_chord(piece, chord)
            if split is not None:
                pieces[index:index + 1] = split
                break
    return pieces


def _split_ring_with_chord(ring, chord):
    """Split one ring by a chord whose ends lie on its boundary, or None."""
    n = len(ring)

    def locate(point):
        best = None
        for i in range(n):
            a, b = ring[i], ring[(i + 1) % n]
            dx, dy = b[0] - a[0], b[1] - a[1]
            length_sq = dx * dx + dy * dy
            if length_sq <= 0.0:
                continue
            t = ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / length_sq
            t = min(1.0, max(0.0, t))
            gx = point[0] - (a[0] + dx * t)
            gy = point[1] - (a[1] + dy * t)
            gap = gx * gx + gy * gy
            if best is None or gap < best[0]:
                best = (gap, i, t)
        return best

    start = locate(chord[0])
    end = locate(chord[-1])
    if start is None or end is None or start[0] > 0.25 or end[0] > 0.25:
        return None

    def boundary_path(a_edge, a_t, b_edge, b_t):
        """Ring vertices strictly between (a_edge, a_t) and (b_edge, b_t),
        walking forward; the chord endpoints themselves are appended by the
        caller."""
        if a_edge == b_edge and a_t <= b_t:
            return []
        points = []
        edge = (a_edge + 1) % n
        while True:
            points.append(ring[edge])
            if edge == b_edge:
                return points
            edge = (edge + 1) % n
            if len(points) > n:
                return points  # safety net; cannot loop past a full circle

    _, e0, t0 = start
    _, e1, t1 = end
    interior = chord[1:-1]
    side_a = [chord[0]] + boundary_path(e0, t0, e1, t1) + [chord[-1]] + list(reversed(interior))
    side_b = [chord[-1]] + boundary_path(e1, t1, e0, t0) + [chord[0]] + list(interior)
    result = []
    for candidate in (side_a, side_b):
        cleaned = [candidate[0]]
        for point in candidate[1:]:
            if math.hypot(point[0] - cleaned[-1][0], point[1] - cleaned[-1][1]) > 1e-9:
                cleaned.append(point)
        if (len(cleaned) >= 3
                and math.hypot(cleaned[0][0] - cleaned[-1][0],
                               cleaned[0][1] - cleaned[-1][1]) <= 1e-9):
            cleaned.pop()
        if len(cleaned) >= 3:
            result.append(cleaned)
    return result if len(result) == 2 else None


def _split_ring_by_fold(map_point, ring, crossings_out=None, gate_bbox=None):
    """[(sub_ring, side, interior_point)] after cutting by every crease.

    `gate_bbox` overrides the cosmetic-cut skip gate's bounds. Rings that
    must end up with CONSISTENT partitions - a fill's outer and its holes -
    must gate against the SAME bounds (the fill's union bbox): gating each
    ring by its own bbox let an extension slice the outer while skipping
    the hole (whose bbox missed the cutter's real geometry), so the whole
    hole straddled two outer pieces, attached to only one, and the other
    rendered solid over what the artist cut out (measured: a crescent of
    phantom fill on add_fill_error).

    `crossings_out` collects the ring/crease crossing points (child space):
    the fill's side-change positions, which the crease anchors on exactly
    like it anchors on the strokes' cuts.
    """
    if not _FOLD["split"]:
        return [(ring, _MappedOutput.FRONT, _ring_interior_point(ring))]
    pieces = [ring]
    raw_crossings = [] if crossings_out is not None else None
    cutters = _child_cutters(map_point)
    raws = getattr(map_point, "child_cutters_raw", None) or [None] * len(cutters)
    if gate_bbox is None:
        rx0 = min(p[0] for p in ring)
        rx1 = max(p[0] for p in ring)
        ry0 = min(p[1] for p in ring)
        ry1 = max(p[1] for p in ring)
    else:
        rx0, ry0, rx1, ry1 = gate_bbox
    margin = 2.0 * POLY_STEP
    for cutter, raw in zip(cutters, raws):
        if raw is not None:
            # Crossing a cutter's straight extension never changes the
            # parity, so a cutter whose REAL geometry stays bbox-clear of
            # the gate bounds can only slice cosmetically - skip it.
            if (max(p[0] for p in raw) < rx0 - margin
                    or min(p[0] for p in raw) > rx1 + margin
                    or max(p[1] for p in raw) < ry0 - margin
                    or min(p[1] for p in raw) > ry1 + margin):
                continue
        cut = []
        for piece in pieces:
            cut.extend(_cut_ring_by_polyline(piece, cutter, raw_crossings))
        pieces = cut
    if raw_crossings:
        # Crossings with a cutter's straight EXTENSION are cutting aid, not
        # fold geometry - only crossings on the real loci may anchor the
        # crease (see _child_cutters).
        real = getattr(map_point, "child_cutters_raw", None) or []
        for q in raw_crossings:
            if any(_polyline_arc_of(q, r)[0] <= 2.0 * POLY_STEP for r in real):
                crossings_out.append(q)
    out = []
    for piece in pieces:
        rep = _ring_interior_point(piece)
        out.append((piece, _fold_sign(map_point, rep), rep))
    return out


def _grayscale(color):
    """The lining shade of a fill: same luminance, no hue, same alpha."""
    gray = int(round(0.299 * color[0] + 0.587 * color[1] + 0.114 * color[2]))
    gray = min(255, max(0, gray))
    return (gray, gray, gray, color[3])


def _clip_rings_to_area(rings, polygons):
    """Cut rings along an area boundary and keep the inside pieces.

    The area polygons double as CLOSED cutters for the same ring splitter the
    creases use, then a parity test keeps the pieces inside. This gives fills
    the same exact boundary cut the stroke emitters get from _clip_polyline -
    an earlier whole-piece keep/drop test made a straddling fill either spill
    past the area or vanish from it entirely.
    """
    if not polygons:
        return list(rings)
    kept = []
    for ring in rings:
        pieces = [ring]
        for polygon in polygons:
            if len(polygon) < 3:
                continue
            boundary = list(polygon) + [polygon[0]]
            cut = []
            for piece in pieces:
                cut.extend(_cut_ring_by_polyline(piece, boundary))
            pieces = cut
        for piece in pieces:
            if _point_in_polygons(_ring_interior_point(piece), polygons):
                kept.append(piece)
    return kept


def _collect_pattern_fills(scene, frame):
    """Fill regions on `frame` that are pattern content (not tool artifacts).

    Collected BOTTOM LAYER FIRST (paintGL walks columns last-index-first, so
    index 0 is the top): emission preserves this order inside each depth
    layer, and Qt paints fill regions in list order, so overlapping fills
    keep the stacking the artist set up on the texture board.
    """
    fills = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return fills
    skip = (MAPPING_AREA_PROPERTY, *MAPPING_OUTPUT_PROPERTIES)
    for layer in reversed(structure["layers"]):
        if not layer["visible"]:
            continue
        cell = scene.cell_to_dict(layer["index"], frame, False)
        for fill in cell["image"].get("fills") or []:
            if (fill.get("property") or "") in skip:
                continue
            fills.append(fill)
    return fills


def _pattern_arc_ranges(map_point, pattern, fills):
    """The MAIN-arc extent of everything about to be mapped, or None."""
    h_low = h_high = v_low = v_high = None

    def feed(point):
        nonlocal h_low, h_high, v_low, v_high
        arc_h, arc_v = _arc_of_point(map_point, point)
        h_low = arc_h if h_low is None else min(h_low, arc_h)
        h_high = arc_h if h_high is None else max(h_high, arc_h)
        v_low = arc_v if v_low is None else min(v_low, arc_v)
        v_high = arc_v if v_high is None else max(v_high, arc_v)

    for stroke in pattern:
        for poly in _stroke_polylines(stroke):
            for point in poly:
                feed(point)
    for fill in fills:
        for ring in _path_commands_to_polygons(fill.get("commands")):
            for point in ring:
                feed(point)
    if h_low is None:
        return None
    return (h_low, h_high), (v_low, v_high)


def _emit_fills(animean, out, map_point, fills, child_area, main_area):
    """Map fill regions: cut by the creases, stack by depth, gray the backs.

    Each source ring is cut into constant-side pieces along the crease loci
    (in child space), every piece gets a stacking depth from the nearest-end
    anchor, and the pieces regroup per (depth, side) into one output path so
    holes keep working through the odd-even rule. Painter's algorithm does
    the occlusion: deeper layers are created first, nearer ones paint over
    them - no boolean subtraction anywhere.
    """
    added = 0
    for fill in fills:
        color = fill.get("color") or {}
        base = (int(color.get("r", 0)), int(color.get("g", 0)),
                int(color.get("b", 0)), int(color.get("a", 255)))
        source_rings = []
        for ring in _path_commands_to_polygons(fill.get("commands")):
            if len(ring) >= 2 and _dist(ring[0], ring[-1]) <= 1e-9:
                ring = ring[:-1]
            if len(ring) < 3:
                continue
            source_rings.append(_densify(ring + [ring[0]])[:-1])
        if not source_rings:
            continue
        # Which source rings are HOLES: odd-even nesting among the fill's own
        # subpaths, judged in child space. The nesting probe must sit ON the
        # ring's OWN boundary - an interior point (the centroid) of an
        # annulus's outer ring lies inside the ring's own hole, which scored
        # the outer as nested level 1 and dropped the WHOLE fill (a centred
        # donut/eye/letter-O emitted zero regions, silently). A boundary
        # vertex is never inside a subpath nested within its own ring; the
        # median over a few spread vertices shrugs off a vertex that grazes
        # another ring's edge.
        def nesting_level(index):
            ring = source_rings[index]
            count = min(5, len(ring))
            levels = sorted(
                sum(1 for j, other in enumerate(source_rings)
                    if j != index
                    and _point_in_ring(ring[(k * len(ring)) // count], other))
                for k in range(count))
            return levels[len(levels) // 2]

        ring_levels = [nesting_level(index) for index in range(len(source_rings))]
        is_hole = [level % 2 == 1 for level in ring_levels]

        # (depth, side) -> [outer piece entries], each carrying its CHILD
        # geometry so hole pieces can find the outer piece that contains
        # them. Outer and hole rings split against the SAME gate bounds
        # (the fill's union bbox) so their partitions are consistent - a
        # hole that skipped a cutter the outer took straddled two outer
        # pieces and could attach to only one.
        fill_bbox = (min(p[0] for ring in source_rings for p in ring),
                     min(p[1] for ring in source_rings for p in ring),
                     max(p[0] for ring in source_rings for p in ring),
                     max(p[1] for ring in source_rings for p in ring))
        outers = {}
        holes = []
        fold_crossings = []
        for index, ring in enumerate(source_rings):
            for clipped in _clip_rings_to_area([ring], child_area):
                for piece, side, rep in _split_ring_by_fold(
                        map_point, clipped, fold_crossings,
                        gate_bbox=fill_bbox):
                    if is_hole[index]:
                        # A hole's depth is its outer's - never probed.
                        # Its attachment point must sit ON the hole's own
                        # boundary: an interior point (the centroid) of a
                        # hole ring lies inside any ISLAND nested within
                        # it, and the innermost-outer rule then attached
                        # the hole to that island (4-ring nest: the outer
                        # lost its hole and painted the hole band solid).
                        edge = ((piece[0][0] + piece[1][0]) * 0.5,
                                (piece[0][1] + piece[1][1]) * 0.5) \
                            if len(piece) >= 2 else piece[0]
                        holes.append((piece, edge))
                    else:
                        depth = _fold_depth(map_point, rep, side)
                        entry = {"child": piece, "rings": [piece],
                                 "holes": [], "level": ring_levels[index]}
                        outers.setdefault((depth, side), []).append(entry)
        # A fill's fold-edge endpoints anchor the crease exactly like stroke
        # cuts do: without them the crease stopped at the last STROKE cut and
        # left the fill's own fold boundary without its terminator line.
        out.cuts.extend(map_point(p) for p in fold_crossings)
        # Attach holes by CHILD-space containment alone: with consistent
        # partitions a hole piece lies inside at least one outer piece and
        # shares the INNERMOST one's depth and side - re-deriving them from
        # the hole's own probe made attachment hostage to probe noise
        # beside a crease, and an unattached hole silently rendered as
        # solid fill. Innermost (deepest nesting level) matters for
        # level-2+ subpaths: an island's hole is contained by the level-0
        # outer too, and first-match attached it there, painting the
        # island's hole solid.
        for piece, rep in holes:
            best = None
            for entries in outers.values():
                for entry in entries:
                    if _point_in_ring(rep, entry["child"]):
                        if best is None or entry["level"] > best["level"]:
                            best = entry
            if best is not None:
                best["holes"].append(piece)

        # Every OUTER piece becomes its own fill region (with its holes as
        # extra odd-even subpaths). Same-group pieces are never merged into
        # one path: two pieces of one group can overlap in the image (the
        # warp is not injective), and odd-even would XOR the overlap into a
        # transparent hole.
        for (depth, side), entries in sorted(outers.items()):
            shade = base if side == _MappedOutput.FRONT else _grayscale(base)
            for entry in entries:
                mapped = []
                for child_ring in [entry["child"]] + entry["holes"]:
                    image_ring = [map_point(p) for p in child_ring]
                    mapped.extend(_clip_rings_to_area([image_ring], main_area))
                if out.add_fill(side, depth, mapped, shade):
                    added += 1
    return added


class _MappedOutput:
    """Buffers emitted strokes and fills per STACKING DEPTH, then flushes
    them into layers.

    Buffering (rather than writing straight into an image) keeps two things
    simple: layers are created only for depths that actually got content -
    so a run with no fold produces exactly one layer, as before - and they
    can be created in z-order regardless of generation order. Depth 0 (the
    sheet layer holding the nearest-end anchor) lands on top, each further
    depth below the previous, the crease between depth 0 and the rest.
    Within one layer Qt paints fills before strokes: this depth's lines over
    this depth's colors, and a NEARER depth's colors over both - the normal
    cel stacking. KNOWN LIMIT: a fill the artist deliberately stacked ABOVE
    line art on the texture board (an opaque correction patch dragged over
    strokes) cannot be represented inside one depth layer, because the image
    model paints all fills before all strokes; such a patch comes out under
    the lines again.
    """

    FRONT, BACK, SEAL = 1, -1, 0
    _PROPERTIES = {FRONT: MAPPED_PROPERTY, BACK: BACK_PROPERTY, SEAL: SEAL_PROPERTY}

    def __init__(self, animean, scene, row):
        self.animean = animean
        self.scene = scene
        self.row = row
        self.depths = {}         # depth -> [(kind, payload, color, width, side)]
        self.seal_depths = {}    # depth -> seal strokes, occluded like content
        self.side_counts = {self.FRONT: 0, self.BACK: 0}
        self.layers = []
        # Where the emitters actually cut the artwork, in mapped space. The
        # crease anchors onto these, so they have to come from the geometry
        # that gets DRAWN: bezier mode splits the smoothed cubics, while
        # _stroke_polylines falls back to the artist's raw input trail when a
        # stroke carries commands instead of polylines - a different curve.
        self.cuts = []

    @staticmethod
    def _layer_name(depth, generic=False):
        if generic:
            # The depth scale was renormalized by an ODD shift, so depth 0 no
            # longer means "front face": parity-neutral names keep the panel
            # from calling back-face content "mapped layer".
            return f"{MAPPED_LAYER_NAME} depth {depth}"
        if depth <= 0:
            return MAPPED_LAYER_NAME
        if depth == 1:
            return BACK_LAYER_NAME
        return f"{MAPPED_LAYER_NAME} depth {depth}"

    def _bucket(self, side, depth):
        if side == self.SEAL:
            return self.seal_depths.setdefault(max(0, int(depth or 0)), [])
        if depth is None:
            depth = 0 if side == self.FRONT else 1
        self.side_counts[side] = self.side_counts.get(side, 0) + 1
        return self.depths.setdefault(max(0, int(depth)), [])

    def add_polyline(self, side, points, color, width, depth=None):
        if len(points) < 2:
            return False
        self._bucket(side, depth).append(("polyline", points, color, width, side))
        return True

    def add_curved(self, side, commands, flat, color, width, depth=None):
        if len(commands) < 2 or len(flat) < 2:
            return False
        self._bucket(side, depth).append(("curved", (commands, flat), color, width, side))
        return True

    def add_fill(self, side, depth, rings, color):
        rings = [ring for ring in rings if len(ring) >= 3]
        if not rings:
            return False
        self._bucket(side, depth).append(("fill", rings, color, 0.0, side))
        return True

    def count(self, side=None):
        seal_total = sum(len(items) for items in self.seal_depths.values())
        if side is None:
            return (sum(len(items) for items in self.depths.values())
                    + seal_total)
        if side == self.SEAL:
            return seal_total
        return self.side_counts.get(side, 0)

    def _track_new_layer(self, layer):
        """Record a freshly created layer; everything older shifted down one.

        _create_mapped_layer moves what it makes to index 0, so every index
        this object already holds is now one lower in the stack.
        """
        self.layers = [index + 1 for index in self.layers]
        self.layers.append(layer)
        return layer

    def add_anchor_layer(self, position, arc):
        """Record the nearest-end anchor as its own layer: a small red ring.

        Same contract as the axis snapshots: the marker is provenance the
        restore can read back, so a stored run brings its STACKING back too,
        not just its axes. The ring's centre alone is NOT enough to restore
        from - hv is two-to-one across every crease, so a position on a
        folded sheet has a preimage per flap and a geometric solve can land
        on the wrong one, inverting the stacking. The ARC COORDINATES are
        therefore written into the layer name (readable provenance in the
        panel, exact restore for Re-expand); the ring's centre stays the
        geometric fallback for a renamed layer.
        """
        if position is None:
            return []
        name = f"{NEAREST_LAYER_NAME} ({arc[0]:.2f}, {arc[1]:.2f})"
        layer = _create_mapped_layer(self.scene, self.row, name)
        if layer < 0:
            return []
        self._track_new_layer(layer)
        image = self.scene.image_at(self.row, layer, True)
        if image is None:
            return []
        radius = 5.0
        ring = [(position[0] + radius * math.cos(2.0 * math.pi * k / 16),
                 position[1] + radius * math.sin(2.0 * math.pi * k / 16))
                for k in range(17)]
        obj = self.animean.vectorlogic.make_stroke_object(
            ring, NEAREST_HANDLE_COLOR, 1.5, image.stroke_count() + 1, False, False)
        obj.property = NEAREST_LAYER_PROPERTY
        image.add_stroke_object(obj)
        return [layer]

    def add_guide_layers(self, h_guide, v_guide):
        """Draw the two MAIN guide axes into their own layers.

        Each axis is drawn at ITS OWN width, taken from the guide asset. An
        earlier version drew both at 0.8 * width_scale - a stroke width chosen
        for the crease - and Re-expand then handed that number back as the
        guide's width, so restoring a guide visibly changed its thickness.
        The snapshot has to be lossless in every field the restore reads.

        These are the axes that shaped this run, recorded next to the result
        they produced, on the board the result landed on. The live guides stay
        where they were - session assets drawn as overlays, still editable and
        still removable by their x badge - so this is a snapshot, not a move:
        re-running the mapping after nudging a guide gives a new group whose
        axes show what THAT run used.
        """
        created = []
        for name, guide, prop in ((H_LAYER_NAME, h_guide, H_GUIDE_LAYER_PROPERTY),
                                  (V_LAYER_NAME, v_guide, V_GUIDE_LAYER_PROPERTY)):
            points = (guide or {}).get("points") or []
            if len(points) < 2:
                continue
            width = float((guide or {}).get("width", 3.0))
            layer = _create_mapped_layer(self.scene, self.row, name)
            if layer < 0:
                continue
            self._track_new_layer(layer)
            # Same shift _track_new_layer applies to self.layers: the new layer
            # went to index 0, so everything recorded here moved down one.
            # Without this both axes reported index 0 and the H/V group was
            # built from one layer named twice.
            created = [index + 1 for index in created]
            image = self.scene.image_at(self.row, layer, True)
            if image is None:
                continue
            color = H_COLOR if prop == H_GUIDE_LAYER_PROPERTY else V_COLOR
            commands = (guide or {}).get("commands")
            if commands:
                # The snapshot must be lossless in every field the restore
                # reads, and the restore now reads the CURVE: building the
                # stroke from the real path keeps Re-expand from demoting a
                # curve guide to its flattening.
                obj = self.animean.vectorlogic.make_stroke_object_from_path(
                    commands, [(float(x), float(y)) for x, y in points],
                    color, width, image.stroke_count() + 1)
            else:
                obj = self.animean.vectorlogic.make_stroke_object(
                    [(float(x), float(y)) for x, y in points], color, width,
                    image.stroke_count() + 1, False, False)
            obj.property = prop
            image.add_stroke_object(obj)
            created.append(layer)
        return created

    def group_output(self, guide_layers):
        """Pack this run into one nested group: [H/V [H, V], front, back, crease].

        The guide subgroup starts collapsed - it is provenance, not something
        you edit - which is exactly what nesting buys: one row in the panel
        that opens into two axes.
        """
        try:
            guide_group = 0
            if guide_layers:
                guide_group = self.scene.create_layer_group(
                    GUIDE_GROUP_NAME, guide_layers, [], True)
            members = [index for index in self.layers if index not in guide_layers]
            groups = [guide_group] if guide_group else []
            if not members and not groups:
                return 0
            return self.scene.create_layer_group(
                MAPPING_GROUP_NAME, members, groups, False)
        except AttributeError:
            return 0  # older build without the grouping bindings

    def _write_items(self, image, items, seal=False):
        written = 0
        for kind, payload, color, width, side in items:
            if kind == "fill":
                commands = []
                for ring in payload:
                    commands.append({"type": "move", "to": {"x": ring[0][0], "y": ring[0][1]}})
                    for point in ring[1:]:
                        commands.append({"type": "line", "to": {"x": point[0], "y": point[1]}})
                    commands.append({"type": "line", "to": {"x": ring[0][0], "y": ring[0][1]}})
                # based_on_all_layers=True: removeInvalidFillRegions deletes
                # any region with no valid SOURCE layer, and a mapped fill is
                # baked geometry with no source - without the flag the app
                # silently erased every mapped fill on the next layer edit.
                image.add_fill_region(commands, color, self._PROPERTIES[side],
                                      None, -1, True)
                written += 1
                continue
            if kind == "polyline":
                obj = self.animean.vectorlogic.make_stroke_object(
                    payload, color, width, image.stroke_count() + 1, False, False)
            else:
                commands, flat = payload
                obj = self.animean.vectorlogic.make_stroke_object_from_path(
                    commands, flat, color, width, image.stroke_count() + 1)
            obj.property = self._PROPERTIES[side]
            if seal:
                obj.pen_style = _display_style("seal_style")
            image.add_stroke_object(obj)
            written += 1
        return written

    def flush(self):
        """Create the needed layers bottom-up and write the buffers out."""
        added = 0
        # Normalize so the nearest occupied depth is 0: an anchor parked in a
        # region with no artwork would otherwise leave the top layer empty
        # and hand the front content a "back" layer name.
        occupied = [d for d in self.depths if self.depths[d]]
        shift = min(occupied) if occupied and min(occupied) > 0 else 0
        if shift:
            self.depths = {d - shift: items for d, items in self.depths.items() if items}
            self.seal_depths = {max(0, d - shift): items
                                for d, items in self.seal_depths.items() if items}
        generic = bool(shift % 2)
        # Each depth's crease layer sits DIRECTLY ABOVE its content layer:
        # above it, because a depth-0 fill is opaque right up to the fold
        # edge (its boundary IS the crease) and painted over the seal it
        # swallowed the crease's inner half; but BELOW every nearer depth,
        # because a crease under a covering flap is occluded artwork like
        # anything else - drawn blanket-on-top it poked out of the covering
        # colour (user report). Layers are created bottom-up, so within one
        # depth the order is content first, crease second.
        deep_first = sorted(set(d for d in self.depths if self.depths[d])
                            | set(d for d in self.seal_depths if self.seal_depths[d]),
                            reverse=True)
        plan = []
        for depth in deep_first:
            if self.depths.get(depth):
                plan.append((self._layer_name(depth, generic),
                             self.depths[depth], False))
            if self.seal_depths.get(depth):
                name = (SEAL_LAYER_NAME if depth == 0 and not generic
                        else f"{SEAL_LAYER_NAME} depth {depth}")
                plan.append((name, self.seal_depths[depth], True))
        for name, items, seal in plan:
            layer = _create_mapped_layer(self.scene, self.row, name)
            if layer < 0:
                print(f"[auto_mapping] could not create the '{name}'.")
                continue
            self._track_new_layer(layer)
            image = self.scene.image_at(self.row, layer, True)
            if image is None:
                print(f"[auto_mapping] '{name}' has no editable cell.")
                continue
            added += self._write_items(image, items, seal)
        return added

    def rollback(self):
        for layer in sorted(self.layers, reverse=True):
            _discard_mapped_layer(self.scene, layer)
        self.layers = []


def _fold_runs(map_point, piece, cuts=None):
    """Source runs of constant orientation, or the whole piece when the
    front/back split is off. Records each cut for the crease to anchor on."""
    if not _FOLD["split"]:
        return [(piece, _MappedOutput.FRONT)]
    runs = _split_by_fold(map_point, piece)
    if cuts is not None:
        for before, _after in zip(runs, runs[1:]):
            cuts.append(map_point(before[0][-1]))
    return runs


def _side_style(side, color_tuple):
    return color_tuple if side == _MappedOutput.FRONT else _display_color("back_color")


def _emit_polyline_mode(animean, out, stroke, map_point, child_area, main_area, color_tuple, width):
    """Same anchored sampling as spline mode, but the output stays a polyline.

    Originals are anchors, samples are inserted between them, everything is
    mapped, the inserted samples are decimated span-wise - and the surviving
    points are joined with straight segments instead of a fitted curve.
    (User 2026-07-30: polyline mode must sample too; it is not a legacy mode.)
    """
    added = 0
    eps = rdp_eps()
    for poly in _stroke_polylines(stroke):
        for piece in _clip_polyline(poly, child_area):
            for run, side in _fold_runs(map_point, piece, out.cuts):
                depth = _run_depth(map_point, run, side)
                flagged = _adaptive_map_polyline(map_point, run)
                for clipped in _clip_flagged(flagged, main_area):
                    points = _decimate_between_anchors(clipped, eps)
                    if out.add_polyline(side, points, _side_style(side, color_tuple),
                                        width, depth):
                        added += 1
    return added


def _emit_spline_mode(animean, out, stroke, map_point, child_area, main_area, color_tuple, width):
    """The user's route 1: originals stay anchors, only inserted samples decimate.

    Densify between the original vertices -> map everything -> RDP only the
    inserted samples of each span -> centripetal Catmull-Rom through the knots.
    """
    added = 0
    eps = rdp_eps()
    for poly in _stroke_polylines(stroke):
        for piece in _clip_polyline(poly, child_area):
            for run, side in _fold_runs(map_point, piece, out.cuts):
                depth = _run_depth(map_point, run, side)
                flagged = _adaptive_map_polyline(map_point, run)
                for clipped in _clip_flagged(flagged, main_area):
                    knots = _decimate_between_anchors(clipped, eps)
                    commands, flat = _cubics_to_commands(_catmull_rom_cubics(knots))
                    if out.add_curved(side, commands, flat,
                                      _side_style(side, color_tuple), width, depth):
                        added += 1
    return added


def _emit_bezier_mode(animean, out, stroke, map_point, child_area, main_area, color_tuple, width):
    """Keep the artist's Bezier segments; transport each handle through the warp."""
    added = 0
    for cubics in _commands_to_subpaths(stroke.get("commands")):
        for src_piece in _clip_cubics(cubics, child_area):
            for run, side in _fold_runs_cubic(map_point, src_piece, out.cuts):
                depth = (_run_depth(map_point, [_cubic_point(run[len(run) // 2], 0.5)], side)
                         if run else (0 if side == _MappedOutput.FRONT else 1))
                out_cubics = []
                for cub in run:
                    out_cubics.extend(_warp_cubic(map_point, cub))
                for out_piece in _clip_cubics(out_cubics, main_area):
                    commands, flat = _cubics_to_commands(out_piece)
                    if out.add_curved(side, commands, flat,
                                      _side_style(side, color_tuple), width, depth):
                        added += 1
    return added


def _split_cubic_by_fold(map_point, cub):
    """Split ONE source cubic where the map's orientation flips inside it.

    Unlike the polyline path there is no analytic knot list for a cubic, so
    the crossings are located by scanning the source parameter and bisecting.
    That is legitimate here: the sign is piecewise constant, so a sign change
    between two probes brackets exactly one cell boundary.
    """
    net = bezier.hull_length(cub)
    probes = max(4, min(96, int(math.ceil(net / POLY_STEP))))
    ts = [k / probes for k in range(probes + 1)]
    signs = [_fold_sign(map_point, _cubic_point(cub, t)) for t in ts]

    cuts = []
    for k in range(1, len(ts)):
        if signs[k] == signs[k - 1]:
            continue
        lo, hi = ts[k - 1], ts[k]
        for _ in range(24):
            mid = (lo + hi) * 0.5
            if _fold_sign(map_point, _cubic_point(cub, mid)) == signs[k - 1]:
                lo = mid
            else:
                hi = mid
        cuts.append((lo + hi) * 0.5)

    bounds = [0.0] + cuts + [1.0]
    parts = []
    for t0, t1 in zip(bounds, bounds[1:]):
        if t1 - t0 <= 1e-9:
            continue
        side = _fold_sign(map_point, _cubic_point(cub, (t0 + t1) * 0.5))
        parts.append((_split_cubic(cub, t0, t1), side))
    return parts


def _emit_seals(animean, out, map_point, pattern, child_area, main_area, width_scale):
    """Draw the crease where the surface folds back on itself.

    The map is CONTINUOUS across a fold - only its derivative flips - so the
    front and back runs already meet exactly and there is no gap to fill.
    The crease matters for a different reason: once the back layer is hidden,
    the remaining front edge needs a terminator.

    Geometry: an H-axis fold happens at a fixed coordinate l_h = c, so its
    locus in source space is a straight line, and its image is
    H(c) + V(t) - O over the content's t range - a TRANSLATED COPY OF THE
    MAIN V GUIDE. (Symmetrically for a V-axis fold.) That is exactly the
    "use the V axis as the capping edge" idea, and it needs no new geometry.
    """
    main = map_point.main_frame
    h_low = h_high = v_low = v_high = None
    for stroke in pattern:
        for poly in _stroke_polylines(stroke):
            for piece in _clip_polyline(poly, child_area):
                for point in piece:
                    # Through _arc_of_point, NOT inline scaling: with an
                    # additional-line warp the artwork's true main-arc extent
                    # differs from the unwarped one (measured 52 arc px), and
                    # the crease windows below must agree with the depth
                    # pass's space or anchored loci get windowed out.
                    arc_h, arc_v = _arc_of_point(map_point, point)
                    h_low = arc_h if h_low is None else min(h_low, arc_h)
                    h_high = arc_h if h_high is None else max(h_high, arc_h)
                    v_low = arc_v if v_low is None else min(v_low, arc_v)
                    v_high = arc_v if v_high is None else max(v_high, arc_v)
    if v_low is None:
        return

    # The crease used to be clipped to the artwork's H extent instead. That is
    # both too weak - a bounding box says nothing about whether the sheet folds
    # at that height - and unsound: dropping the out-of-range samples from the
    # middle of a branch silently JOINED the two surviving halves with a
    # straight line across the gap. Anchoring subsumes it, since a cut can
    # never sit outside the artwork.
    #
    # The cuts come from the emitters, not from a second splitting pass here:
    # bezier mode splits the SMOOTHED cubics while _stroke_polylines falls back
    # to the artist's raw trail, so recomputing them would anchor the crease
    # onto a curve the output never contains.
    cuts = out.cuts
    # The tolerance is the crease polyline's own step, not the accuracy of a
    # cut. A cut is exact - bisected onto det J = 0 - and on a smooth stretch
    # it measures 0.01 px from its branch. But where a fold PAIR IS BORN the
    # two branches leave the birth point like +/- sqrt(l_v - l_v0), so the
    # locus turns a square-root corner that halving in V only resolves as
    # sqrt(2) per pass: measured 2.27 px off a branch whose steps are bounded
    # at 4 px, and at 2.0 px tolerance that cut was rejected and its branch
    # lost an anchor. Distinguishing branches does not need a tight number -
    # the nearest other branch in that test was 750 px away - and the two
    # branches that ARE close near a birth are close because they coincide
    # there, so attributing a cut to either is the same crease.
    tolerance = 2.0 * POLY_STEP

    color = _display_color("seal_color")
    # Thinner than the artwork: the crease is an annotation of the fold, not
    # part of the pattern, and at 2x width_scale it out-weighted every stroke
    # it was meant to terminate. The factor is a display setting now.
    width = max(0.5, float(_LINE_DISPLAY.get("seal_width", 0.8)) * width_scale)
    # Pad the sweep windows: a locus stretch whose anchoring cut sits right
    # at the pattern's arc extent needs the rows just beyond it to give the
    # branch tip room to reach the cut.
    pad = 4.0 * POLY_STEP
    # A traced curve can hold stretches OUTSIDE the artwork's arc extent: the
    # padded window traces them (branch tips need the room) and stitching can
    # chain a material stretch to an immaterial one. Nothing folds where no
    # material reaches - yet anchoring alone no longer excludes such
    # stretches: in a heavy-compression zone the image of a cut from a
    # NEIGHBOURING locus lands within the trim tolerance of the immaterial
    # stretch's image and anchors it (measured: a 140 px dashed crease across
    # visually flat artwork, on a corner line 14 px beyond the pattern's arc
    # extent - the cuts anchoring it belonged to a locus 14+ arc px away).
    # So clip in ARC space, where compression cannot confuse neighbours,
    # keeping each inside run as its own branch - splitting, never bridging,
    # so the old clip-and-join hazard (a straight seam across the dropped
    # middle) cannot return. The margin is the anchor tolerance: a cut ON
    # the extent boundary keeps a reachable tip.
    in_h = (h_low - tolerance, h_high + tolerance)
    in_v = (v_low - tolerance, v_high + tolerance)

    def _window_runs(curve):
        run = []
        run_indices = []
        for index, (arc, other) in enumerate(curve):
            if in_h[0] <= arc <= in_h[1] and in_v[0] <= other <= in_v[1]:
                run.append((arc, other))
                run_indices.append(index)
            else:
                if len(run) >= 2:
                    yield run, run_indices
                run = []
                run_indices = []
        if len(run) >= 2:
            yield run, run_indices

    whole_curves = _crease_curves(map_point, (v_low - pad, v_high + pad),
                                  (h_low - pad, h_high + pad),
                                  corner_spans=((h_low, h_high), (v_low, v_high)))
    # Read AFTER the call: _with_warp_loci publishes by REBINDING
    # map_point.warp_curve_child to a new dict, so a pre-call snapshot
    # never holds the ids of the curves this call just built - the whole
    # exact-geometry bypass below was dead code with the snapshot first.
    warp_child = getattr(map_point, "warp_curve_child", {}) or {}
    for whole in whole_curves:
      # A warp fold locus carries its exact child geometry and is verified
      # by construction - the _curve_is_fold probes (and the depth probes
      # below) go through the fixed-point unwarp, which is unreliable
      # exactly where the warp folds.
      whole_child = warp_child.get(id(whole))
      for curve, curve_indices in _window_runs(whole):
        if whole_child is None and not _curve_is_fold(map_point, curve):
            continue
        points = [main.hv(arc, other) for arc, other in curve]
        if len(points) < 2:
            continue
        # Keep the pre-trim image polyline and its 1:1 arc correspondence:
        # the crease's DEPTH is read back through them after trimming and
        # cusp-splitting have reshaped the drawn geometry.
        branch_arcs = list(curve)
        branch_points = list(points)
        branch_child = ([whole_child[i] for i in curve_indices]
                        if whole_child is not None else None)
        branch_depths = {}

        def seal_depth_at(image_point):
            """Depth of the crease at this drawn vertex: the nearer of the
            two flaps meeting at the fold (probed one step to each side in
            child space). Deeper flaps' creases are occluded by everything
            nearer, exactly like the flaps themselves."""
            best = None
            for index, q in enumerate(branch_points):
                d2 = ((q[0] - image_point[0]) ** 2
                      + (q[1] - image_point[1]) ** 2)
                if best is None or d2 < best[0]:
                    best = (d2, index)
            index = best[1]
            if index in branch_depths:
                return branch_depths[index]
            other = index + 1 if index + 1 < len(branch_arcs) else index - 1
            if branch_child is not None:
                pa = branch_child[index]
                pb = branch_child[other]
            else:
                pa = _child_of_arcs(map_point, branch_arcs[index])
                pb = _child_of_arcs(map_point, branch_arcs[other])
            dx, dy = pb[0] - pa[0], pb[1] - pa[1]
            length = math.hypot(dx, dy)
            depth = 0
            if length > 1e-9:
                nx, ny = -dy / length, dx / length
                sides = []
                for direction in (1.0, -1.0):
                    p = (pa[0] + nx * POLY_STEP * direction,
                         pa[1] + ny * POLY_STEP * direction)
                    sides.append(_fold_depth(map_point, p,
                                             _fold_sign(map_point, p)))
                depth = max(0, min(sides))
            branch_depths[index] = depth
            return depth
        # Anchor the WHOLE branch before cutting it up. A cusp is a feature of
        # one continuous fold, not a break in it, so a cut past the cusp still
        # vouches for the stretch leading up to it; anchoring the two arms
        # separately orphaned a cut 145 px from anything drawn.
        points = _trim_to_anchors(points, cuts, tolerance)
        if len(points) < 2:
            continue
        # No minimum span here: a non-empty trim is BY CONSTRUCTION vouched
        # for by two cuts, and a short anchored sliver is real geometry - the
        # map compresses whole source bands into a few px where det J nears
        # zero, so dozens of cuts can legitimately pile onto a 3 px stretch
        # (measured: 36 cuts on a 2.6 px trim; the old 8 px minimum deleted
        # it and orphaned every one of them). Unanchored stubs never reach
        # this point - the trim already returned [] for them.
        # The crease is an annotation, so decimate it like any other output -
        # but BEFORE splitting at cusps, which also dissolves the sub-pixel
        # reversals that would each otherwise become their own quarter-pixel
        # "stroke". Cusps are drawn as separate strokes because one stroke
        # through a cusp ties a visible knot, and every arm that survives is
        # inside the anchored span, so none may be dropped: consecutive arms
        # share their cusp vertex, so dropping a short one does not shorten the
        # crease, it punches a hole in the middle of it, and dropping an END
        # arm pulls the crease off the stroke it exists to terminate (7 px).
        points = _rdp(points, max(rdp_eps(), 0.5))
        # Cusps are drawn as separate strokes because one stroke through a cusp
        # ties a visible knot. A cusp can leave a stub under a pixel long,
        # which is not worth a stroke object of its own - but it may not be
        # DROPPED either: every arm is inside the anchored span, consecutive
        # arms share their cusp vertex, so dropping a short one punches a hole
        # in the middle of one continuous crease rather than shortening it,
        # and dropping an END arm pulls the crease off the very stroke it
        # exists to terminate (measured: 7 px short of it). So a stub is
        # folded back into its neighbour, where a sub-pixel reversal is
        # invisible, instead of being emitted or discarded.
        arms = []
        for arm in _split_at_cusps(points):
            if arms and _cumulative_lengths(arm)[-1] < 2.0 * POLY_STEP:
                arms[-1].extend(arm[1:])
            else:
                arms.append(list(arm))
        if len(arms) > 1 and _cumulative_lengths(arms[0])[-1] < 2.0 * POLY_STEP:
            arms[1][:0] = arms[0][:-1]
            del arms[0]
        for run in arms:
            for piece in _clip_polyline(run, main_area):
                if len(piece) < 2:
                    continue
                # Split where the crease's depth changes (it slides under a
                # nearer flap at locus crossings); consecutive sub-pieces
                # share the transition vertex so the crease stays gapless.
                depth_of = [seal_depth_at(p) for p in piece]
                start = 0
                for index in range(1, len(piece)):
                    if depth_of[index] != depth_of[start]:
                        out.add_polyline(_MappedOutput.SEAL,
                                         piece[start:index + 1], color, width,
                                         depth_of[start])
                        start = index
                out.add_polyline(_MappedOutput.SEAL, piece[start:], color,
                                 width, depth_of[start])


def _fold_runs_cubic(map_point, cubics, cuts=None):
    """Group source cubics into runs of constant orientation."""
    if not _FOLD["split"]:
        return [(list(cubics), _MappedOutput.FRONT)]
    runs = []
    for cub in cubics:
        for part, side in _split_cubic_by_fold(map_point, cub):
            if runs and runs[-1][1] == side:
                runs[-1][0].append(part)
            else:
                runs.append(([part], side))
    if cuts is not None:
        for before, _after in zip(runs, runs[1:]):
            cuts.append(map_point(before[0][-1][3]))
    return runs


_EMITTERS = {
    "polyline": _emit_polyline_mode,
    "spline": _emit_spline_mode,
    "bezier": _emit_bezier_mode,
}


def _perform_mapping():
    animean = _animean()
    child = _scene_model("child")
    main = _scene_model("main")
    mode = curve_mode()
    if mode not in _EMITTERS:
        mode = DEFAULT_CURVE_MODE

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

    child_pattern = _collect_pattern_strokes(child, child_frame, want_commands=mode == "bezier")
    child_fills_probe = _collect_pattern_fills(child, child_frame)
    if not child_pattern and not child_fills_probe:
        print("[auto_mapping] child_paint_view has no pattern strokes or fills to map.")
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
    additional = _additional_pairs()
    map_point, width_scale = build_mapper(
        child_assets[H_PROPERTY],
        child_assets[V_PROPERTY],
        main_assets[H_PROPERTY],
        main_assets[V_PROPERTY],
        mapper_info,
        additional_pairs=additional,
    )
    if map_point is None:
        print(f"[auto_mapping] cannot build mapping: {width_scale}")
        return False
    for note in getattr(map_point, "additional_notes", ()):
        print(f"[auto_mapping] warning: {note}")
    if getattr(map_point, "warp", None) is not None:
        print(f"[auto_mapping] {len(map_point.warp.pairs)} additional line "
              f"pair(s) shaping the mapping ({_ADDITIONAL['falloff']} falloff).")
    worst_mismatch = max(mapper_info.get("h_scale_mismatch", 1.0),
                         mapper_info.get("v_scale_mismatch", 1.0))
    if worst_mismatch > 1.5:
        print(f"[auto_mapping] tip: the crossings sit at different relative positions "
              f"(side scale mismatch x{worst_mismatch:.1f}); strokes crossing a center "
              "line will fold there. Place both crossings at similar positions along "
              "their lines to avoid it.")
    if mapper_info.get("mirrored"):
        print("[auto_mapping] note: the child and main guide frames have OPPOSITE "
              "handedness, so the result is a MIRROR image. If you wanted it "
              "unmirrored, reverse ONE main center line (redraw it in the other "
              "direction - watch the arrows).")

    child_area = (child_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")
    main_area = (main_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")

    child_fills = child_fills_probe
    if _FOLD["split"]:
        ranges = _pattern_arc_ranges(map_point, child_pattern, child_fills)
        if ranges is not None:
            # Depth context: crease loci + the nearest-end anchor, so every
            # run and fill piece can be stacked by how many folds separate it
            # from the red handle.
            _prepare_fold_context(map_point, ranges[0], ranges[1])

    out = _MappedOutput(animean, main, main_frame)
    emit = _EMITTERS[mode]
    generated = 0
    clipped_out = 0
    mapped_fills = 0
    guide_layers = []
    mapping_group = 0
    try:
        for stroke in child_pattern:
            color_tuple, width = _stroke_style(stroke, width_scale)
            before = generated
            generated += emit(animean, out, stroke, map_point, child_area, main_area,
                              color_tuple, width)
            if generated == before:
                clipped_out += 1
        if child_fills:
            mapped_fills = _emit_fills(animean, out, map_point, child_fills,
                                       child_area, main_area)
        if _FOLD["split"] and _FOLD["seal"] and out.count(_MappedOutput.BACK):
            _emit_seals(animean, out, map_point, child_pattern, child_area,
                        main_area, width_scale)
        added = out.flush()
        if added:
            # Provenance first, so the axes sit above the artwork they shaped
            # and the whole run collapses to a single panel row.
            guide_layers = out.add_guide_layers(main_assets.get(H_PROPERTY),
                                                main_assets.get(V_PROPERTY))
            anchor = getattr(map_point, "depth_anchor", None)
            if anchor is None:
                frame = map_point.main_frame
                arc_h, arc_v = _nearest_arc()
                anchor = (min(max(arc_h, -frame.h_arc), frame.h_total - frame.h_arc),
                          min(max(arc_v, -frame.v_arc), frame.v_total - frame.v_arc))
            anchor_layers = out.add_anchor_layer(map_point.main_frame.hv(*anchor), anchor)
            if anchor_layers:
                # The anchor layer went to index 0; every axis index recorded
                # before it moved down one.
                guide_layers = [index + 1 for index in guide_layers] + anchor_layers
            mapping_group = out.group_output(guide_layers)
    except Exception:
        # A half-filled layer without its own history commit would silently
        # ride along in the NEXT unrelated commit; roll it back instead.
        out.rollback()
        animean.ui.refresh()
        raise

    if added == 0:
        out.rollback()
        animean.ui.refresh()
        print(f"[auto_mapping] nothing mapped: all {clipped_out} stroke(s) fell outside "
              "the mapping area(s); the empty layer was discarded.")
        return False

    animean.ui.refresh()
    try:
        animean.ui.history_commit("Auto Mapping", "main")
    except Exception:
        pass  # older builds without the history binding
    layer_names = ", ".join(f"'{main.layer_name(index)}'" for index in sorted(out.layers))
    summary = (f"[auto_mapping] Auto Mapping (coons interpolation, {mode} mode) mapped "
               f"{added} stroke(s) into NEW layer(s) {layer_names} "
               f"(frame {main_frame + 1} of main_paint_view, width x{width_scale:.2f})")
    if mapping_group:
        summary += (f"; packed into layer group '{MAPPING_GROUP_NAME}' with a collapsed "
                    f"'{GUIDE_GROUP_NAME}' subgroup holding the two axes")
    elif guide_layers:
        summary += "; the axes were recorded but this build cannot group layers"
    if mapped_fills:
        deepest = max((d for d in out.depths if out.depths[d]), default=0)
        summary += f"; {mapped_fills} fill group(s) mapped"
        if deepest >= 1:
            summary += (f", stacked over {deepest + 1} depth layer(s) from the "
                        "nearest point (red handle)")
    back_count = out.count(_MappedOutput.BACK)
    if back_count:
        summary += (f"; {back_count} stroke/fill item(s) landed on the BACK of "
                    f"a fold (det J < 0)")
    if mapper_info.get("mirrored"):
        summary += ", MIRRORED (opposite frame handedness)"
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
    if prop not in (H_PROPERTY, V_PROPERTY, MAPPING_AREA_PROPERTY,
                    ADDITIONAL_PROPERTY):
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
    # The stroke's REAL path is a mixed line/Bezier chain (the smoother's quad
    # spline); the polyline above is only its 4 px flattening. Keep the
    # commands so the frame can evaluate on the true curve (_Curve).
    commands = None
    try:
        raw_strokes = scene.cell_to_dict(layer, row, False)["image"]["strokes"]
        if index < len(raw_strokes):
            commands = raw_strokes[index].get("commands") or None
    except Exception as error:
        print(f"[auto_mapping] curve capture unavailable ({error}); "
              "keeping the flattened guide.")
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
    elif prop == ADDITIONAL_PROPERTY:
        # Saves and refreshes both boards itself (the pair lives on both).
        if not run_additional_line_tool(view, points, width, commands):
            message["cancel_history"] = True
        _animean().ui.widget.refresh()
        return
    else:
        # Same entry point Re-expand uses; it saves and refreshes the overlays
        # itself, which is why the shared tail below only runs for the area.
        if not run_center_line_tool(view, prop, points, width, commands):
            message["cancel_history"] = True
            _animean().ui.widget.refresh()
            return
        print(f"[auto_mapping] {ITEM_LABELS[prop]} set in {view} view (redraw replaces it)")
        # NO colour reset here. It existed because there was one global
        # drawing colour and the pen would otherwise inherit the guide's;
        # with per-tool colours (pyfile/tool_colors.py) the pen keeps its own
        # and this reset did active harm - it recorded BLACK against the
        # guide tool, so the first axis drew blue and every one after it drew
        # in the pen's colour (user report).
        _animean().ui.widget.refresh()
        return

    # Written BEFORE the C++ stroke commit fires, so the new guide/area rides
    # in the same history entry and is undone/redone together with it.
    _save_assets(view)
    _overlays_changed(view)
    # The item is captured. The drawing colour is NOT reset: each tool now
    # remembers its own (pyfile/tool_colors.py), so the pen gets its colour
    # back when the pen is armed - and resetting here poisoned this tool's
    # slot with black.
    _animean().ui.widget.refresh()


def _overlay_removed(cell, stroke, message):
    overlay = message.get("overlay") or {}
    item_id = overlay.get("id")
    if not item_id:
        return
    view = message.get("view") or "main"
    if item_id.startswith(ADDITIONAL_PROPERTY + ":"):
        # Additional lines exist as PAIRS - removing one on either board
        # removes the pair (matched by id) on both, and each board's write
        # is committed to ITS OWN history: a one-board commit left the
        # other board's live scriptData ahead of its snapshot, and the next
        # undo silently half-applied.
        try:
            pair_id = int(item_id.split(":", 1)[1])
        except ValueError:
            return
        removed_points = None
        missed = []
        for name in ("child", "main"):
            lines = (_assets_for(name).get(ADDITIONAL_PROPERTY) or {}).get("lines")
            if not lines:
                continue
            for index, line in enumerate(lines):
                if _line_id(line, index) == pair_id:
                    if name == view:
                        removed_points = line.get("points") or None
                    del lines[index]
                    break
            else:
                missed.append(name)
                continue
            if not lines:
                del _assets_for(name)[ADDITIONAL_PROPERTY]
            _save_assets(name)
            _overlays_changed(name)
            try:
                _animean().ui.history_commit("Remove additional line", name)
            except Exception:
                pass
        # Legacy projects can hold pairs whose ids disagree across boards
        # (created before ids existed, or through a since-fixed desync). The
        # id lookup then removes one half and the other board visibly keeps
        # its pink line - fall back to GEOMETRY, guarded twice (each guard's
        # absence was demonstrated to delete a HEALTHY pair's half):
        # only candidates that are THEMSELVES unpaired by id are eligible
        # (a genuine orphan's neighbours are all paired), and matching is by
        # CHORD ENDPOINTS - the two halves of a pair share one Third chord
        # by construction, so their endpoints coincide regardless of the
        # bend the pair encodes (a nearest-polyline match rejected the true
        # partner as soon as the bend exceeded the redraw threshold).
        for name in missed:
            lines = (_assets_for(name).get(ADDITIONAL_PROPERTY) or {}).get("lines")
            if not lines or not removed_points:
                continue
            mapper, _why = _mapper_from_assets(additional=False)
            if mapper is None:
                continue
            forward = mapper if view == "child" else mapper.inverse
            head = forward(removed_points[0])
            tail = forward(removed_points[-1])
            view_ids = {
                _line_id(line, index) for index, line in enumerate(
                    (_assets_for(view).get(ADDITIONAL_PROPERTY) or {}).get("lines") or [])}
            candidates = []
            for index, line in enumerate(lines):
                stored = line.get("points") or []
                if len(stored) < 2 or _line_id(line, index) in view_ids:
                    continue  # paired lines are never fallback targets
                ends = (stored[0], stored[-1])
                direct = max(math.hypot(head[0] - ends[0][0], head[1] - ends[0][1]),
                             math.hypot(tail[0] - ends[1][0], tail[1] - ends[1][1]))
                flipped = max(math.hypot(head[0] - ends[1][0], head[1] - ends[1][1]),
                              math.hypot(tail[0] - ends[0][0], tail[1] - ends[0][1]))
                gap = min(direct, flipped)
                chord = math.hypot(ends[1][0] - ends[0][0], ends[1][1] - ends[0][1])
                if gap <= max(30.0, 0.35 * chord):
                    candidates.append((gap, index))
            candidates.sort()
            if (not candidates
                    or (len(candidates) > 1
                        and candidates[0][0] > 0.5 * candidates[1][0])):
                print(f"[auto_mapping] the removed line's partner on the {name} "
                      "board could not be identified - remove its pink line "
                      "there with its own x.")
                continue
            partner = candidates[0][1]
            del lines[partner]
            if not lines:
                del _assets_for(name)[ADDITIONAL_PROPERTY]
            _save_assets(name)
            _overlays_changed(name)
            try:
                _animean().ui.history_commit("Remove additional line", name)
            except Exception:
                pass
        print(f"[auto_mapping] additional line (id {pair_id}) removed")
        return
    assets = _assets_for(view)
    if item_id in assets:
        del assets[item_id]
        label = ITEM_LABELS.get(item_id, item_id)
        _save_assets(view)
        try:
            _animean().ui.history_commit(f"Remove {label}", view)
        except Exception:
            pass  # older builds without the history binding
        if item_id == NEAREST_PROPERTY:
            # The anchor's default IS the crossing, so its x means "reset":
            # the ring reappears there on the refresh below.
            print("[auto_mapping] nearest point reset to the crossing")
        else:
            print(f"[auto_mapping] removed {label} in {view} view")
        _overlays_changed(view)


def _history_restored(cell, stroke, message):
    """After undo/redo/jump/reset the scene's scriptData is authoritative."""
    if (message.get("view") or "main") == "main":
        # A mid-drag Ctrl+Z invalidates the frame cached at press: the
        # restored guides may be entirely different, and solving against the
        # stale frame wrote the anchor in the OLD arc space and then
        # committed it on top of the state the user just undid. Dropping the
        # drag state makes the next move rebuild from the restored assets,
        # and the untouched `moved` flag stays honest for the release.
        _NEAREST_DRAG["frame"] = None
        _NEAREST_DRAG["moved"] = False
    _load_assets(message.get("view") or "main")


def _auto_mapping_button(cell, stroke, message):
    global _last_run_handled
    _last_run_handled = True
    _run()


def _tool_option_changed(cell, stroke, message):
    hook = message.get("hook")
    if hook == "curve_mode":
        value = str(message.get("value", "")).lower()
        if value in CURVE_MODES and _CURVE_MODE["value"] != value:
            _CURVE_MODE["value"] = value
            print(f"[auto_mapping] curve mode -> {value}")
            # RDP only exists in the sampled modes, and the mode lives in the
            # menu bar - outside the options panel - so the panel has to be
            # told to re-read its layout or the slider would linger a step
            # behind the mode it belongs to.
            try:
                _animean().ui.refresh_tool_options()
            except Exception:
                pass
        return
    if hook == "rdp_eps":
        try:
            eps = max(1, min(20, int(message.get("value", 3)))) / 10.0
        except (TypeError, ValueError):
            return
        if _RDP_STATE["eps"] != eps:
            _RDP_STATE["eps"] = eps
            print(f"[auto_mapping] RDP tolerance -> {eps:.1f}px")
        return
    if hook == "fold_split":
        enabled = str(message.get("value", "")).lower() == "on"
        if _FOLD["split"] != enabled:
            _FOLD["split"] = enabled
            print(f"[auto_mapping] front/back fold split {'ON' if enabled else 'OFF'}")
        return
    if hook == "fold_seal":
        enabled = str(message.get("value", "")).lower() == "on"
        if _FOLD["seal"] != enabled:
            _FOLD["seal"] = enabled
            print(f"[auto_mapping] crease strokes {'ON' if enabled else 'OFF'}")
        return
    if hook == "back_shade":
        try:
            shade = max(0, min(100, int(message.get("value", 45))))
        except (TypeError, ValueError):
            return
        # One slider drives a neutral cool lining colour: 0 = near black,
        # 100 = near white. Tools that want a specific colour can set
        # _FOLD["back_color"] directly from the debug pane.
        level = int(round(20 + shade * 2.0))
        _FOLD["back_color"] = (level, level + 8, level + 32, 255)
        print(f"[auto_mapping] back/lining shade -> {_FOLD['back_color'][:3]}")
        return
    # The refer grid used to be a tool option here. It moved to a per-board
    # View menu: it is a display choice about a BOARD, not a setting of the
    # mapping tool, and as one shared option it could not be answered
    # separately for the two views.


VIEW_MENU_NAME = "view"
REFER_RECT_ITEM = "refer_rect"


def _view_menu_items(view_name):
    """The View menu of one board, re-read on every open so ticks are true."""
    def build():
        items = [{"name": REFER_RECT_ITEM, "title": "Mapping Refer Rect",
                  "kind": "check", "checked": _REFER_RECT.get(view_name, False)}]
        # Grid density lives with the grid it densifies. One shared value
        # for both boards: the point of the fine setting is COMPARING the
        # two boards' grids cell by cell, which needs them to agree.
        items.append({
            "name": "refer_rect_divisions", "title": "Refer Rect Divisions",
            "kind": "submenu",
            "items": [{"name": f"grid_div_{n}", "title": f"{n} x {n}",
                       "kind": "radio",
                       "checked": _GRID["divisions"] == n}
                      for n in GRID_DIVISION_CHOICES]})
        if view_name == "child":
            items.append({"name": OCCLUSION_BUTTON, "title": "Occluded Areas",
                          "kind": "check", "checked": _OCCLUSION["enabled"]})
        return items
    return build


def _view_menu_action(message):
    if message.get("menu") != VIEW_MENU_NAME:
        return
    view = message.get("view") or "main"
    name = message.get("name") or ""
    checked = bool(message.get("checked"))
    if name == REFER_RECT_ITEM:
        if _REFER_RECT.get(view) == checked:
            return
        _REFER_RECT[view] = checked
        _invalidate_grid_cache()
        _push_overlay(view)
        print(f"[auto_mapping] refer rect grid on {view} "
              f"{'ON' if checked else 'OFF'}")
        return
    if name.startswith("grid_div_"):
        try:
            divisions = int(name[len("grid_div_"):])
        except ValueError:
            return
        if divisions not in GRID_DIVISION_CHOICES \
                or _GRID["divisions"] == divisions:
            return
        _GRID["divisions"] = divisions
        _invalidate_grid_cache()
        for board in ("child", "main"):
            if _REFER_RECT.get(board):
                _push_overlay(board)
        print(f"[auto_mapping] refer rect grid -> {divisions} x {divisions} "
              "iso-lines")
        return
    if name == OCCLUSION_BUTTON:
        _set_occlusion(checked)


def _set_occlusion(enabled):
    _OCCLUSION["enabled"] = bool(enabled)
    _OCCLUSION_CACHE["items"] = None
    _push_overlay("child")  # computes and caches the bands as a side effect
    if not _OCCLUSION["enabled"]:
        print("[auto_mapping] occlusion preview OFF")
        return
    _report_occlusion()


def _report_occlusion():
    note = _OCCLUSION_CACHE.get("note")
    items = _OCCLUSION_CACHE.get("items") or []
    if note:
        print(f"[auto_mapping] occlusion preview: nothing to show - {note}.")
    elif not items:
        print("[auto_mapping] occlusion preview: these guides fold nothing face-down.")
    else:
        share = _OCCLUSION_CACHE.get("share", 0.0)
        print(f"[auto_mapping] occlusion preview ON: {share * 100.0:.0f}% of the "
              f"guide rectangle lands face-down (tinted).")


def _legacy_guide_axis(stroke, layer_name):
    """Which axis an OLD snapshot stroke is, when both shared one property.

    By COLOUR, not by layer name. The name looks like the obvious key and is
    the wrong one: _create_mapped_layer routes every name through
    uniqueLayerName, which drifts a taken name to "H axis1", "H axis2", ... -
    so only the very first run in a document ever owns the bare "H axis", and
    keying on it silently lost every later run's snapshot. The colour is
    exact: the old code picked it from the loop's intended name before the
    model renamed anything, so blue IS the H axis and green IS the V axis.
    The name is kept as a last resort, matched as a prefix.
    """
    color = stroke.get("color") or {}
    try:
        rgb = (int(color.get("r", -1)), int(color.get("g", -1)), int(color.get("b", -1)))
    except (TypeError, ValueError):
        rgb = (-1, -1, -1)
    if rgb == H_COLOR[:3]:
        return H_GUIDE_LAYER_PROPERTY
    if rgb == V_COLOR[:3]:
        return V_GUIDE_LAYER_PROPERTY
    if layer_name.startswith(H_LAYER_NAME):
        return H_GUIDE_LAYER_PROPERTY
    if layer_name.startswith(V_LAYER_NAME):
        return V_GUIDE_LAYER_PROPERTY
    return ""


def _guide_axes_in_layers(scene, frame, layer_indices):
    """{property -> (points, width, commands)} for the axis snapshots on
    these layers.

    Identified by the stroke property, so a renamed layer still restores
    correctly; the layer name is only consulted for snapshots written before
    the two axes carried separate properties. `commands` is the snapshot
    stroke's real path (None on snapshots from before guides kept curves) -
    the restore hands it back to run_center_line_tool so a curve guide
    round-trips as a curve.
    """
    found = {}
    for index in layer_indices:
        if index is None or index < 0:
            continue
        try:
            cell = scene.cell_to_dict(index, frame, True, POLY_STEP)
        except Exception:
            continue
        raw_strokes = []
        try:
            raw_strokes = scene.cell_to_dict(index, frame, False)["image"]["strokes"]
        except Exception:
            pass
        name = ""
        try:
            name = scene.layer_name(index)
        except Exception:
            pass
        for position, stroke in enumerate(cell["image"]["strokes"]):
            prop = stroke.get("property") or ""
            if prop == GUIDE_LAYER_PROPERTY:
                prop = _legacy_guide_axis(stroke, name)
            if prop == NEAREST_LAYER_PROPERTY and prop not in found:
                # The layer NAME carries the exact arc; the ring centre is
                # the geometric fallback (renamed layer). The ring closes on
                # a duplicated first vertex - averaging it twice biased the
                # centre by radius/17 px, drifting the anchor a third of a
                # pixel per map->re-expand cycle, always the same direction.
                arc = None
                match = re.search(r"\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)",
                                  name or "")
                if match:
                    arc = (float(match.group(1)), float(match.group(2)))
                points = _stroke_points(stroke)
                if points and len(points) >= 2 and _dist(points[0], points[-1]) <= 1e-9:
                    points = points[:-1]
                if points:
                    cx = sum(p[0] for p in points) / len(points)
                    cy = sum(p[1] for p in points) / len(points)
                    found[prop] = ((cx, cy), 0.0, arc)
                continue
            if prop not in (H_GUIDE_LAYER_PROPERTY, V_GUIDE_LAYER_PROPERTY):
                continue
            points = _stroke_points(stroke)
            if len(points) < 2 or prop in found:
                continue
            commands = None
            if position < len(raw_strokes):
                commands = raw_strokes[position].get("commands") or None
            found[prop] = (points, float(stroke.get("width", 3.0)), commands)
    return found


def _layer_menu_items(context):
    """Offer 'Re-expand' on a group that holds an axis snapshot."""
    if context.get("kind") != "group":
        return []
    view = context.get("view") or "main"
    try:
        scene = _scene_model(view)
    except Exception:
        return []
    axes = _guide_axes_in_layers(scene, scene.current_frame(),
                                 context.get("members") or [])
    if not any(prop in axes for prop in (H_GUIDE_LAYER_PROPERTY,
                                         V_GUIDE_LAYER_PROPERTY)):
        return []  # an anchor marker alone restores nothing
    return [{
        "name": RESTORE_GUIDES_ACTION,
        "title": "Re-expand (restore these axes as the guides)",
    }]


def _layer_menu_action(message):
    """Put a stored axis snapshot back as this view's live H/V guides.

    The snapshot was taken in the coordinates of the board it sits on, so
    restoring it is exact: the mapping that produced the group can be re-run
    from the same axes, and the overlays land back on top of the very strokes
    the group is showing.
    """
    if message.get("action") != RESTORE_GUIDES_ACTION:
        return
    view = message.get("view") or "main"
    try:
        scene = _scene_model(view)
    except Exception as error:
        print(f"[auto_mapping] re-expand skipped: {error}")
        return

    axes = _guide_axes_in_layers(scene, scene.current_frame(),
                                 message.get("members") or [])
    if not axes:
        print("[auto_mapping] re-expand: this group holds no stored axes.")
        return

    # Drive the H and V line tools, one per axis - the same call drawing a
    # guide makes - rather than writing the asset dict from here.
    restored = []
    for prop, target in ((H_GUIDE_LAYER_PROPERTY, H_PROPERTY),
                         (V_GUIDE_LAYER_PROPERTY, V_PROPERTY)):
        if prop not in axes:
            continue
        points, width, commands = axes[prop]
        if run_center_line_tool(view, target, points, width, commands):
            restored.append(ITEM_LABELS[target])
    if not restored:
        print("[auto_mapping] re-expand: nothing to restore.")
        return

    # The anchor marker restores the STACKING the run used: its ring centre,
    # solved against the just-restored guides, is the stored arc position.
    # (run_center_line_tool auto-created a crossing anchor above; the marker
    # overwrites it with the recorded one.)
    marker = axes.get(NEAREST_LAYER_PROPERTY)
    if marker and view == "main":
        frame = _main_guide_frame()
        if frame is not None:
            point, _width, arc = marker
            if arc is None:
                # Renamed layer: fall back to solving the ring's centre.
                # CAVEAT: hv is two-to-one across a crease, so on a folded
                # frame this can pick the other flap; the layer-name arc is
                # the authoritative record precisely because of that.
                arc = frame.solve(point, 0.0, 0.0)
            l_h = min(max(arc[0], -frame.h_arc), frame.h_total - frame.h_arc)
            l_v = min(max(arc[1], -frame.v_arc), frame.v_total - frame.v_arc)
            _assets_for("main")[NEAREST_PROPERTY] = {"arc": [l_h, l_v]}
            _save_assets("main")
            _push_nearest_handle()
            restored.append("nearest point")

    try:
        _animean().ui.refresh()
        _animean().ui.history_commit("Re-expand Guides", view)
    except Exception:
        pass  # older builds without the history binding
    assets = _assets_for(view)
    missing = [ITEM_LABELS[p] for p in GUIDE_PROPERTIES if p not in assets]
    print(f"[auto_mapping] re-expanded {', '.join(restored)} onto {view}_paint_view"
          + (f"; still missing: {', '.join(missing)}" if missing else ""))


MENU_NAME = "auto_mapping"
LINE_SETTINGS_NAME = "auto_mapping_lines"


def _menu_items():
    """Built fresh every time the menu opens, so the ticks are the truth."""
    mode = curve_mode()
    return [
        {"name": "line_settings", "title": "Line Display Settings...",
         "kind": "settings", "settings": LINE_SETTINGS_NAME},
        {"kind": "separator"},
        {"name": "calc_mode", "title": "Calculation Mode", "kind": "submenu",
         "items": [
             {"name": "mode_bezier", "title": "Bezier", "kind": "radio",
              "checked": mode == "bezier"},
             {"name": "mode_spline", "title": "Spline", "kind": "radio",
              "checked": mode == "spline"},
             {"name": "mode_polyline", "title": "Polyline", "kind": "radio",
              "checked": mode == "polyline"},
         ]},
        {"name": "additional_falloff", "title": "Additional Line Falloff",
         "kind": "submenu",
         "items": [
             {"name": "falloff_linear", "title": "Linear", "kind": "radio",
              "checked": _ADDITIONAL["falloff"] == "linear"},
             {"name": "falloff_quadratic", "title": "Quadratic", "kind": "radio",
              "checked": _ADDITIONAL["falloff"] == "quadratic"},
         ]},
        {"kind": "separator"},
        {"name": "to_3d", "title": "To 3D"},
    ]


def _line_settings_layout():
    """The settings window, in the same control schema the tool panel uses."""
    def color(name, title, key, row):
        return {"name": name, "type": "color", "title": title,
                "hook": "line_display", "value": _hex_color(_display_color(key)),
                "row": row, "start_column": 0, "end_column": 1}

    def style(name, title, key, row):
        return {"name": name, "type": "list", "title": title,
                "hook": "line_display", "value": str(_display_style(key)),
                "row": row, "start_column": 0, "end_column": 2,
                "options": [{"title": t, "value": str(v)} for t, v in LINE_STYLES]}

    def width(name, title, key, row, low, high, scale=1.0):
        return {"name": name, "type": "slider", "title": title,
                "hook": "line_display", "min": low, "max": high,
                "value": int(round(float(_LINE_DISPLAY.get(key, 3.0)) * scale)),
                "row": row, "start_column": 0, "end_column": 2}

    return {
        "row_spacing": 8,
        "column_spacing": 6,
        "controls": [
            color("h_color", "H axis colour", "h_color", 0),
            width("h_width", "H axis width", "h_width", 1, 1, 30),
            style("h_style", "H axis line", "h_style", 2),

            color("v_color", "V axis colour", "v_color", 3),
            width("v_width", "V axis width", "v_width", 4, 1, 30),
            style("v_style", "V axis line", "v_style", 5),

            color("seal_color", "Crease colour", "seal_color", 6),
            # Stored as a FACTOR of the mapped stroke width, so the slider is
            # in tenths and the label says so.
            width("seal_width", "Crease width (x0.1)", "seal_width", 7, 1, 40, 10.0),
            style("seal_style", "Crease line", "seal_style", 8),

            color("back_color", "Lining colour", "back_color", 9),
            {"name": "occlusion_alpha", "type": "slider",
             "title": "Occluded-area tint", "hook": "line_display",
             "min": 0, "max": 255,
             "value": int(_LINE_DISPLAY.get("occlusion_alpha", 70)),
             "row": 10, "start_column": 0, "end_column": 2},
        ],
    }


def _hex_color(rgba):
    r, g, b, a = (list(rgba) + [255, 255, 255, 255])[:4]
    return f"#{a:02x}{r:02x}{g:02x}{b:02x}"


def _parse_hex_color(text):
    text = str(text).strip().lstrip("#")
    try:
        if len(text) == 8:      # AARRGGBB, what the colour control reports
            a, r, g, b = (int(text[i:i + 2], 16) for i in (0, 2, 4, 6))
            return (r, g, b, a)
        if len(text) == 6:
            r, g, b = (int(text[i:i + 2], 16) for i in (0, 2, 4))
            return (r, g, b, 255)
    except ValueError:
        pass
    return None


def _menu_action(message):
    if message.get("menu") != MENU_NAME:
        return
    name = message.get("name") or ""
    if name == "to_3d":
        try:
            run_to_3d()
        except Exception as error:
            print(f"[auto_mapping] To 3D failed: {error}")
        return
    if name.startswith("falloff_"):
        falloff = name[len("falloff_"):]
        if falloff in ADDITIONAL_FALLOFFS and _ADDITIONAL["falloff"] != falloff:
            _ADDITIONAL["falloff"] = falloff
            # The occlusion preview bakes the warp, so its cache is stale now.
            _invalidate_grid_cache()
            _push_overlay("child")
            _push_overlay("main")
            print(f"[auto_mapping] additional line falloff -> {falloff} "
                  "(applies on the next run)")
        return
    if not name.startswith("mode_"):
        return
    mode = name[len("mode_"):]
    if mode not in CURVE_MODES or _CURVE_MODE["value"] == mode:
        return
    _CURVE_MODE["value"] = mode
    print(f"[auto_mapping] calculation mode -> {mode}")


def _line_display_changed(message):
    """One hook for the whole settings window; the control name is the key."""
    if message.get("hook") != "line_display":
        return
    name = message.get("name") or ""
    value = message.get("value")
    if name.endswith("_color"):
        parsed = _parse_hex_color(value)
        if parsed is None:
            return
        _LINE_DISPLAY[name] = parsed
    elif name.endswith("_style"):
        try:
            _LINE_DISPLAY[name] = max(1, min(5, int(value)))
        except (TypeError, ValueError):
            return
    elif name == "occlusion_alpha":
        try:
            _LINE_DISPLAY[name] = max(0, min(255, int(value)))
        except (TypeError, ValueError):
            return
    elif name.endswith("_width"):
        try:
            number = float(value)
        except (TypeError, ValueError):
            return
        # The crease width is a factor, so its slider is in tenths.
        _LINE_DISPLAY[name] = number / 10.0 if name == "seal_width" else number
    else:
        return

    # Guides are overlays, so a display change shows immediately; the crease
    # and the lining are baked into strokes and apply to the NEXT run.
    _invalidate_grid_cache()
    _push_overlay("main")
    _push_overlay("child")
    print(f"[auto_mapping] {name} -> {_LINE_DISPLAY[name]}")


def register_hooks():
    python_hooks.set_hook(_capture_mapping_item, linefinish=True, tool="extra")
    python_hooks.set_hook(_overlay_removed, overlayremove=True)
    python_hooks.set_hook(_history_restored, historyrestore=True)
    python_hooks.set_hook(_auto_mapping_button, extra=True, tool=AUTO_MAPPING2_TOOL)
    # tool="extra" keeps this hook from intercepting (and debug-dock-spamming)
    # every built-in tool's option events — refer_rect only exists on extra
    # tools anyway.
    python_hooks.set_hook(_tool_option_changed, option=True, tool="extra")


def run_center_line_tool(view_name, property_value, points, width=3.0, commands=None):
    """Install one center line. THE entry point of the H / V line tools.

    Drawing a guide and re-expanding a stored one are the same act - "this
    line is now the H (or V) axis of this board" - so they go through this
    one function instead of each writing the asset dict themselves. Anything
    that installs a guide gets the save, the overlay refresh and the
    validation for free, and there is one place to change when installing a
    guide has to do more.

    `commands` is the stroke's real mixed line/Bezier path when the caller
    has it (capture does; stored axis snapshots and legacy assets do not).
    With commands the frame evaluates the guide on the true curve with
    Gauss-Legendre arc tables; without, it stays the flattened polyline.

    Note it deliberately does NOT arm the drawing tool: re-expanding from the
    layer panel must not silently swap the pen out from under the user.
    """
    if property_value not in GUIDE_PROPERTIES:
        return False
    cleaned = [(float(x), float(y)) for x, y in points or []]
    if len(cleaned) < 2:
        print(f"[auto_mapping] {ITEM_LABELS.get(property_value, property_value)} "
              "needs at least two points.")
        return False
    item = {"points": cleaned, "width": float(width)}
    if commands:
        try:
            if _Curve(cleaned, commands).curved:
                item["commands"] = commands
        except Exception as error:
            print(f"[auto_mapping] guide commands rejected ({error}); "
                  "keeping the flattened polyline.")
    assets = _assets_for(view_name)
    replacing = property_value in assets
    assets[property_value] = item
    if replacing:
        # An edited guide changes the frame the additional lines' stored
        # coordinates live in; they must re-derive from their drawn points.
        _invalidate_additional_thirds(view_name)
    # The moment BOTH main axes exist, the nearest-end anchor comes into
    # being AT THE CROSSING (arc (0,0)) as a real asset - saved, undoable,
    # and its red handle visible immediately - rather than materializing
    # only when Auto Mapping first needs it.
    if (view_name == "main"
            and NEAREST_PROPERTY not in assets
            and all(prop in assets for prop in GUIDE_PROPERTIES)
            and _polylines_cross(assets[H_PROPERTY]["points"],
                                 assets[V_PROPERTY]["points"])):
        assets[NEAREST_PROPERTY] = {"arc": [0.0, 0.0]}
        print("[auto_mapping] nearest point created at the crossing "
              "(drag the red handle to restack fold layers).")
    _save_assets(view_name)
    _overlays_changed(view_name)
    return True


def _line_id(line, index):
    """A line's pair identity: its stored id, or its index for legacy assets."""
    try:
        return int(line["id"])
    except (KeyError, TypeError, ValueError):
        return index


def _additional_pairs():
    """The (child, main) additional-line pairs, matched BY IDENTITY and
    returned in DRAWING ORDER.

    Positional zip was measured to re-pair surviving lines after a one-board
    undo desynced the lists - marrying unrelated guides into a fabricated
    120 px deformation. Each line carries an id shared with its partner;
    ids missing on one board (an undone half of a pair) are ORPHANS and are
    ignored with a note, never re-matched. Legacy assets without ids fall
    back to their list position, which reproduces the old pairing exactly
    for in-step boards.

    ORDER MATTERS now: the warp stages COMPOSE, each later line computed
    in the rendering the earlier ones produce. Ids are allocated max+1 at
    creation and a redraw keeps its line's id, so ascending id IS the
    drawing order - stable across deletion, save/load and legacy stamping
    (position order approximates creation order there).
    """
    child_lines = (_assets_for("child").get(ADDITIONAL_PROPERTY) or {}).get("lines") or []
    main_lines = (_assets_for("main").get(ADDITIONAL_PROPERTY) or {}).get("lines") or []

    def by_id(lines):
        table = {}
        for index, line in enumerate(lines):
            table.setdefault(_line_id(line, index), line)
        return table

    child_map = by_id(child_lines)
    main_map = by_id(main_lines)
    pairs = []
    orphans = 0
    for key in sorted(child_map):
        child_line = child_map[key]
        main_line = main_map.get(key)
        if main_line is None:
            orphans += 1
            continue
        if (len(child_line.get("points") or []) >= 2
                and len(main_line.get("points") or []) >= 2):
            pairs.append((child_line, main_line))
    orphans += sum(1 for key in main_map if key not in child_map)
    if orphans:
        print(f"[auto_mapping] {orphans} unpaired additional line(s) ignored "
              "(an undo may have split a pair - remove the stray line with "
              "its x to clean up).")
    return pairs or None


def _mapper_from_assets(additional=True):
    """The current mapper straight from both boards' assets, or (None, why)."""
    specs = {}
    for view in ("child", "main"):
        assets = _assets_for(view)
        for prop in GUIDE_PROPERTIES:
            item = assets.get(prop)
            if not item or len(item.get("points") or []) < 2:
                return None, f"draw the H and V center lines on the {view} board first"
            specs[(view, prop)] = item
        if not _polylines_cross(assets[H_PROPERTY]["points"],
                                assets[V_PROPERTY]["points"]):
            return None, f"the H and V center lines on the {view} board do not cross"
    return build_mapper(specs[("child", H_PROPERTY)], specs[("child", V_PROPERTY)],
                        specs[("main", H_PROPERTY)], specs[("main", V_PROPERTY)],
                        additional_pairs=_additional_pairs() if additional else None)


def _nearest_additional_index(lines, points):
    """The stored line the new stroke REDRAWS, or None if it is a new one.

    Matching is by mean distance of the new stroke's points to each stored
    polyline - the H/V redraw-replaces convention, made index-aware because
    additional lines are many. The threshold follows the STORED line's own
    length (the first version scaled it with the NEW stroke, so one long
    stroke could swallow a deliberately distinct neighbour 180 px away),
    and an ambiguous match - the runner-up nearly as close as the winner -
    APPENDS instead: appending a redundant line is one x-click to undo,
    silently mangling an existing pair is not.
    """
    step = max(1, len(points) // 16)
    probes = points[::step]
    candidates = []
    new_arc = _cumulative_lengths(points)[-1]
    for index, line in enumerate(lines):
        stored = line.get("points") or []
        if len(stored) < 2:
            continue
        mean = sum(_polyline_arc_of(p, stored)[0] for p in probes) / len(probes)
        # Capped absolutely and by the NEW stroke's own scale: a 2000 px
        # shaped line must not swallow an obviously separate 120 px line
        # 280 px away (measured - the shaped line was silently destroyed).
        threshold = max(20.0, min(0.15 * _cumulative_lengths(stored)[-1],
                                  0.5 * new_arc, 100.0))
        if mean <= threshold:
            candidates.append((mean, index))
    if not candidates:
        return None
    candidates.sort()
    if len(candidates) > 1 and candidates[0][0] > 0.5 * candidates[1][0]:
        return None
    return candidates[0][1]


def _third_of_drawn(base, view_name, points, previous_third=None,
                    previous_points=None):
    """Third-space coordinates of a freshly drawn line, one per point.

    On a folded frame the inverse solves are branch-ambiguous; a REDRAW
    seeds each point's solve from the replaced line's stored coordinates at
    the matching arc fraction, so the edit stays on the branch the pair
    already lived on. A redraw in the opposite stroke direction samples the
    seeds reversed, matched by which end of the OLD line the new stroke
    starts nearest to.
    """
    seeds = [None] * len(points)
    if previous_third and len(previous_third) >= 2:
        prev = [tuple(t) for t in previous_third]
        reverse = False
        if previous_points and len(previous_points) >= 2:
            head, tail = previous_points[0], previous_points[-1]
            start = points[0]
            reverse = (math.hypot(start[0] - tail[0], start[1] - tail[1])
                       < math.hypot(start[0] - head[0], start[1] - head[1]))
        if reverse:
            prev = list(reversed(prev))
        prev_cum = _cumulative_lengths(prev)
        cum = _cumulative_lengths(points)
        total = cum[-1] or 1.0
        for index in range(len(points)):
            third_seed = _point_at_arc(prev, prev_cum,
                                       prev_cum[-1] * cum[index] / total)
            seeds[index] = (third_seed if view_name == "child"
                            else base.scale_arcs(*third_seed))
    if view_name == "child":
        return [list(base.coords(p, seeds[i])) for i, p in enumerate(points)]
    return [list(base.unscale_arcs(*base.main_coords(p, seeds[i])))
            for i, p in enumerate(points)]


def _invalidate_additional_thirds(view_name):
    """Drop stored Third coordinates made stale by a guide edit.

    Third space IS the child frame's arc plane: a child-guide edit
    redefines it wholesale, and a main-guide edit re-parameterizes the
    main-board lines' relation to their canvas points. The drawn pink
    points stay put (they are what the user sees); coordinates re-derive
    from them on the next build. Without this the stored third kept
    describing the RETIRED frame - measured: a 40 px guide nudge slid the
    correction 40 px off the line that defines it.
    """
    dropped = 0
    boards = ("child", "main") if view_name == "child" else ("main",)
    for name in boards:
        lines = (_assets_for(name).get(ADDITIONAL_PROPERTY) or {}).get("lines") or []
        for line in lines:
            if line.pop("third", None) is not None:
                dropped += 1
        if dropped:
            _save_assets(name)
    if dropped:
        print("[auto_mapping] the guide edit re-anchors the additional lines: "
              "their coordinates re-derive from the drawn points on the next run.")


def run_additional_line_tool(view_name, points, width=2.5, commands=None):
    """Install one additional (pink) refinement line. Entry of the tool.

    Drawing NEAR an existing additional line on this board REPLACES that
    line (redraw-to-edit, the H/V convention) and leaves its partner on the
    other board untouched - the pair's Third-space difference is what
    drives the warp. Drawing elsewhere APPENDS a new pair - the LAST stage
    of the composition chain - whose partner is the drawn line's CHORD in
    the RENDERING the standing lines already produce: a child-drawn line
    is pushed through the chain and straightened there, a main-drawn
    line's frame arcs already ARE that rendering and its child anchor
    pulls back through the chain (best-effort inside a fold band, with a
    loud note). The line's own bend takes effect immediately either way.
    NEUTRALITY is judged in the rendering: a straight main-drawn line asks
    for nothing; a straight child-drawn line inside a standing warp band
    asks to FLATTEN that band - that is the composition rule, not a leak.
    What stays banned is re-deriving a STORED third by inverse solves at
    build time (branch-ambiguous on folded frames) - both sides carry
    authoritative third coordinates from the moment they are made.
    Pairs share an id; the partner write is committed to the OTHER board's
    history too, so a one-board undo cannot silently orphan it.
    """
    cleaned = [(float(x), float(y)) for x, y in points or []]
    if len(cleaned) < 2:
        print("[auto_mapping] an additional line needs at least two points.")
        return False
    other = "main" if view_name == "child" else "child"
    base, why = _mapper_from_assets(additional=False)
    if base is None:
        print(f"[auto_mapping] additional line refused: {why}.")
        return False

    item = {"points": cleaned, "width": float(width)}
    if commands:
        item["commands"] = commands
    assets_here = _assets_for(view_name)
    lines_here = assets_here.setdefault(ADDITIONAL_PROPERTY, {"lines": []})["lines"]
    replaced = _nearest_additional_index(lines_here, cleaned)
    if replaced is not None:
        old = lines_here[replaced]
        item["id"] = _line_id(old, replaced)
        item["third"] = _third_of_drawn(base, view_name, cleaned,
                                        old.get("third"), old.get("points"))
        lines_here[replaced] = item
        print(f"[auto_mapping] additional line {replaced + 1} redrawn on the "
              f"{view_name} board; run Auto Mapping to apply the deformation.")
    else:
        lines_other = _assets_for(other).setdefault(
            ADDITIONAL_PROPERTY, {"lines": []})["lines"]
        new_id = 1 + max(
            (_line_id(line, index)
             for index, line in enumerate(lines_here + lines_other)),
            default=-1)
        item["id"] = new_id
        item["third"] = _third_of_drawn(base, view_name, cleaned)
        # The partner is the drawn line's CHORD - its straightened Third
        # form. Per the geodesic argument the chord IS the line's shape on
        # the flattened surface, so the line's own bend relative to it is
        # the deformation, and drawing a curved line takes effect
        # IMMEDIATELY. The first version mirrored the drawn line verbatim
        # (delta == 0), and the tool appeared to do nothing until the
        # partner was edited.
        #
        # STAGES COMPOSE: this new line is the LAST stage, so its chord
        # and geodesics live in the RENDERING the standing lines already
        # produce - the chord is taken after pushing a child-drawn line
        # through the standing chain, and a main-drawn line's frame arcs
        # already ARE that rendering space (the frame inverse never
        # involves the warp). The synced partner's stored coordinates
        # keep each board's own convention (child stores base Third,
        # main stores rendering), so the child-side chord pulls back
        # through the chain - best-effort inside a fold band, where the
        # preimage is genuinely multivalued. Under the old flat-sum
        # model going through the warp RE-ENCODED the standing
        # deformation (a measured doubling); under composition NOT going
        # through it is what mis-anchors - a partner synthesized in base
        # coordinates landed displaced by exactly the standing warp's
        # shift when drawn over a warped region.
        chain = None
        if lines_here or lines_other:  # the new item is not appended yet
            full, _chain_why = _mapper_from_assets()
            chain = getattr(full, "warp", None) if full is not None else None
        third = [tuple(t) for t in item["third"]]
        staged = ([chain.apply(t) for t in third]
                  if chain is not None and view_name == "child" else third)
        cum = _cumulative_lengths(staged)
        total = cum[-1] or 1.0
        head, tail = staged[0], staged[-1]
        chord_third = [(head[0] + (tail[0] - head[0]) * (cum[i] / total),
                        head[1] + (tail[1] - head[1]) * (cum[i] / total))
                       for i in range(len(staged))]
        if view_name == "child":
            display = [base.main_frame.hv(*base.scale_arcs(*t))
                       for t in chord_third]
            partner_third = chord_third
        else:
            partner_third = ([chain.unapply(t) for t in chord_third]
                             if chain is not None else chord_third)
            if chain is not None:
                # A failed pullback must never become geometry silently:
                # the build derives this stage's delta from the partner's
                # pushed-forward position, so any solver residual here
                # reads as DRAWN deformation downstream.
                worst = max(math.hypot(*(a - b for a, b in
                                         zip(chain.apply(p), t)))
                            for p, t in zip(partner_third, chord_third))
                if worst > 0.5:
                    print(f"[auto_mapping] warning: the new line crosses a "
                          f"fold band of an earlier additional line - its "
                          f"child-side anchor is approximate there "
                          f"({worst:.1f} px off one branch).")
            display = [base.child_frame.hv(*t) for t in partner_third]
        lines_here.append(item)
        lines_other.append({"points": display, "width": float(width),
                            "id": new_id,
                            "third": [list(t) for t in partner_third]})
        _save_assets(other)
        _overlays_changed(other)
        try:
            _animean().ui.history_commit("Additional line pair", other)
        except Exception:
            pass  # older builds without the history binding
        print(f"[auto_mapping] additional line (id {new_id}) set on the "
              f"{view_name} board; its straightened chord landed on the "
              f"{other} board. The line's bend now shapes the mapping - "
              "redraw either side to adjust.")
    _save_assets(view_name)
    _overlays_changed(view_name)
    # Immediate feedback when the fresh line (or any standing one) crosses
    # a fold edge and gets clipped or skipped - a silently degraded line
    # reads as "the tool ignored me".
    check, _why = _mapper_from_assets()
    for note in getattr(check, "additional_notes", ()) if check else ():
        print(f"[auto_mapping] warning: {note}")
    return True


def activate_additional_line_tool(name="additional_line",
                                  property_value=ADDITIONAL_PROPERTY):
    register_hooks()
    _set_draw_color(ADDITIONAL_COLOR)
    print("[auto_mapping] additional line active: draw a refinement line on "
          "either board (needs H/V center lines on both). Redraw near a line "
          "to edit it; its pair on the other board stays, and the difference "
          "bends the mapping.")
    return property_value


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


def run_auto_mapping(name=AUTO_MAPPING2_TOOL, property_value=AUTO_MAPPING2_TOOL):
    global _last_run_handled
    register_hooks()
    if _last_run_handled:
        # the "extra" event hook already performed this click's mapping
        _last_run_handled = False
        return property_value
    _run()
    return property_value


def _signed_area(ring):
    total = 0.0
    for a, b in zip(ring, ring[1:] + ring[:1]):
        total += a[0] * b[1] - b[0] * a[1]
    return 0.5 * total


def _segments_cross(a, b, c, d):
    """Proper crossing of open segments ab and cd (shared endpoints ok)."""
    d1 = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    d2 = (b[0] - a[0]) * (d[1] - a[1]) - (b[1] - a[1]) * (d[0] - a[0])
    d3 = (d[0] - c[0]) * (a[1] - c[1]) - (d[1] - c[1]) * (a[0] - c[0])
    d4 = (d[0] - c[0]) * (b[1] - c[1]) - (d[1] - c[1]) * (b[0] - c[0])
    return d1 * d2 < 0.0 and d3 * d4 < 0.0


def _rdp_ring(ring, epsilon):
    """Douglas-Peucker on a closed ring: geometry-true simplification.

    A COUNT cap (keep every Nth point) turned curved stretches into long
    chords, and the fill boundary visibly parted from the outline stroke
    drawn over the same path (user report). A geometric tolerance keeps
    every deviation below sub-pixel instead."""
    if len(ring) <= 4:
        return list(ring)

    def simplify(points):
        if len(points) < 3:
            return list(points)
        a, b = points[0], points[-1]
        span = math.hypot(b[0] - a[0], b[1] - a[1])
        worst = -1.0
        index = 0
        for k in range(1, len(points) - 1):
            p = points[k]
            if span < 1e-9:
                d = math.hypot(p[0] - a[0], p[1] - a[1])
            else:
                d = abs((b[0] - a[0]) * (a[1] - p[1])
                        - (a[0] - p[0]) * (b[1] - a[1])) / span
            if d > worst:
                worst = d
                index = k
        if worst <= epsilon:
            return [points[0], points[-1]]
        left = simplify(points[:index + 1])
        right = simplify(points[index:])
        return left[:-1] + right

    # Split at the two farthest-apart vertices so the closed ring gets two
    # open runs (DP needs anchored endpoints).
    far = max(range(len(ring)), key=lambda k: (ring[k][0] - ring[0][0]) ** 2
              + (ring[k][1] - ring[0][1]) ** 2)
    first = simplify(ring[:far + 1])
    second = simplify(ring[far:] + [ring[0]])
    out = first[:-1] + second[:-1]
    return out if len(out) >= 3 else list(ring)


def _triangulate_quality(outer, holes, max_area, constraints=None):
    """(vertices, triangles) via the `triangle` LIBRARY (Shewchuk):
    constrained Delaunay with a quality bound and a max triangle area.

    This is the primary mesher: the boundary is an exact constraint, and
    interior refinement comes from the SAME triangulation - no separate
    subdivision pass, so there are no T-junctions and no hairline seams
    between triangles (the longest-edge bisection stand-in split an edge
    on one side but not always its neighbour's, and the cracks showed).
    Returns None when the library is unavailable or rejects the input
    (e.g. a self-intersecting artist ring) - callers fall back to earcut.
    """
    import pydeps
    tri = pydeps.ensure("triangle")
    np = pydeps.ensure("numpy")
    if tri is None or np is None:
        return None
    rings = [list(outer)] + [list(hole) for hole in holes]
    points = []
    segments = []
    for ring in rings:
        base = len(points)
        count = len(ring)
        points.extend(ring)
        segments.extend([[base + k, base + (k + 1) % count]
                         for k in range(count)])
    # Interior CONSTRAINT polylines (fold creases): the mesh edges align
    # with them, so the z kink at a crease is a clean ridge instead of a
    # sawtooth of triangles straddling it (user report). Open chains.
    for chain in constraints or []:
        if len(chain) < 2:
            continue
        base = len(points)
        points.extend(chain)
        segments.extend([[base + k, base + k + 1]
                         for k in range(len(chain) - 1)])
    payload = {"vertices": np.array(points, dtype=float),
               "segments": np.array(segments, dtype=int)}
    hole_points = [_ring_interior_point(hole) for hole in holes]
    if hole_points:
        payload["holes"] = np.array(hole_points, dtype=float)
    try:
        # q30: a 20-degree minimum angle still admits 1:3 slivers, and
        # their interpolated normals rendered zigzag shading along every
        # edge (user report). 30 degrees keeps triangles near-uniform
        # and is still within Ruppert-termination territory.
        result = tri.triangulate(payload, f"pq30a{max_area:.4f}")
        vertices = [tuple(p) for p in result["vertices"]]
        triangles = [tuple(int(i) for i in t) for t in result["triangles"]]
    except Exception as error:
        print(f"[auto_mapping] triangle library rejected a ring ({error}); "
              "falling back to earcut")
        return None
    if not triangles:
        return None
    return vertices, triangles


def _triangulate_with_earcut(outer, holes):
    """(vertices, triangles) via the mapbox_earcut LIBRARY, or None.

    User directive: prefer real Python libraries over hand-rolled
    algorithms - the embedded runtime is a full python312 with pip, the
    repo bundles version-pinned wheels in pywheels/, and pydeps
    self-installs them offline on first use.
    """
    import pydeps
    earcut = pydeps.ensure("mapbox_earcut")
    np = pydeps.ensure("numpy")
    if earcut is None or np is None:
        return None
    rings = [list(outer)] + [list(hole) for hole in holes]
    flat = [p for ring in rings for p in ring]
    ends = []
    total = 0
    for ring in rings:
        total += len(ring)
        ends.append(total)
    try:
        array = np.array(flat, dtype=np.float64).reshape(-1, 2)
        indices = earcut.triangulate_float64(array, ends)
    except Exception as error:
        print(f"[auto_mapping] earcut failed ({error}); using the "
              "built-in triangulator")
        return None
    triangles = [(int(indices[i]), int(indices[i + 1]), int(indices[i + 2]))
                 for i in range(0, len(indices), 3)]
    if not triangles:
        return None
    return flat, triangles


def _triangulate_polygon(outer, holes):
    """(vertices, triangles) for a polygon with holes.

    WHY TRIANGLES AT ALL (user question): GPUs rasterize triangles only -
    there is no vector-topology rendering in WebGL, and SVG's vector fill
    is flat 2D (no bent surface, no orbiting camera, no lighting).
    Three.js's own ShapeGeometry earcuts too, but only for planar shapes;
    our surface carries a z field, so we triangulate the outline
    faithfully here and subdivide interiors to follow the bend.

    Primary path: the mapbox_earcut library (bundled wheels, offline
    self-install via pydeps). The pure-python ear clipper below stays as
    the last-resort fallback for a runtime where even the bundled
    install fails.
    """
    library = _triangulate_with_earcut(outer, holes)
    if library is not None:
        return library
    outer = list(outer)
    if _signed_area(outer) < 0.0:
        outer.reverse()
    chain = outer
    for hole in sorted(holes,
                       key=lambda h: -max(p[0] for p in h)):
        hole = list(hole)
        if _signed_area(hole) > 0.0:
            hole.reverse()
        m_index = max(range(len(hole)), key=lambda k: hole[k][0])
        hole = hole[m_index:] + hole[:m_index]
        m = hole[0]
        # Bridge to a chain vertex visible from the hole's rightmost
        # point: nearest first, fall back to anything unobstructed.
        order = sorted(range(len(chain)),
                       key=lambda k: (chain[k][0] - m[0]) ** 2
                       + (chain[k][1] - m[1]) ** 2)
        pick = None
        for candidate in order:
            p = chain[candidate]
            blocked = False
            for i in range(len(chain)):
                a = chain[i]
                b = chain[(i + 1) % len(chain)]
                if a is p or b is p:
                    continue
                if _segments_cross(m, p, a, b):
                    blocked = True
                    break
            if not blocked:
                for i in range(1, len(hole)):
                    a = hole[i]
                    b = hole[(i + 1) % len(hole)]
                    if _segments_cross(m, p, a, b):
                        blocked = True
                        break
            if not blocked:
                pick = candidate
                break
        if pick is None:
            pick = order[0]
        chain = (chain[:pick + 1] + hole + [hole[0]]
                 + chain[pick:])

    vertices = list(chain)
    indices = list(range(len(vertices)))
    triangles = []

    def convex(i0, i1, i2):
        a, b, c = vertices[i0], vertices[i1], vertices[i2]
        return ((b[0] - a[0]) * (c[1] - a[1])
                - (b[1] - a[1]) * (c[0] - a[0])) > 1e-12

    def contains_other(i0, i1, i2):
        a, b, c = vertices[i0], vertices[i1], vertices[i2]
        for k in indices:
            if k in (i0, i1, i2):
                continue
            p = vertices[k]
            # The hole bridges DUPLICATE two vertices (the bridge runs
            # there and back); a coincident copy is not an obstruction,
            # but treating it as one starved the clipper of ears and the
            # forced fallback filled every hole solid.
            if p == a or p == b or p == c:
                continue
            d1 = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
            d2 = (c[0] - b[0]) * (p[1] - b[1]) - (c[1] - b[1]) * (p[0] - b[0])
            d3 = (a[0] - c[0]) * (p[1] - c[1]) - (a[1] - c[1]) * (p[0] - c[0])
            if d1 > 1e-9 and d2 > 1e-9 and d3 > 1e-9:
                return True
        return False

    guard = 0
    while len(indices) > 3 and guard < 4 * len(vertices):
        guard += 1
        clipped = False
        n = len(indices)
        for pos in range(n):
            i0 = indices[(pos - 1) % n]
            i1 = indices[pos]
            i2 = indices[(pos + 1) % n]
            if convex(i0, i1, i2) and not contains_other(i0, i1, i2):
                triangles.append((i0, i1, i2))
                indices.pop(pos)
                clipped = True
                break
        if not clipped:
            # numeric stalemate (collinear runs): drop the flattest corner
            n = len(indices)
            flattest = min(range(n), key=lambda pos: abs(
                _signed_area([vertices[indices[(pos - 1) % n]],
                              vertices[indices[pos]],
                              vertices[indices[(pos + 1) % n]]])))
            i0 = indices[(flattest - 1) % n]
            i1 = indices[flattest]
            i2 = indices[(flattest + 1) % n]
            triangles.append((i0, i1, i2))
            indices.pop(flattest)
    if len(indices) == 3:
        triangles.append(tuple(indices))
    return vertices, triangles


def _subdivide_mesh(vertices, triangles, max_edge):
    """Longest-edge bisection until no edge exceeds max_edge; midpoints
    are cached per edge so neighbouring triangles stay watertight."""
    vertices = list(vertices)
    max_sq = max_edge * max_edge
    for _round in range(9):
        midpoints = {}

        def midpoint(i, j):
            key = (i, j) if i < j else (j, i)
            index = midpoints.get(key)
            if index is None:
                a, b = vertices[i], vertices[j]
                vertices.append(((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5))
                index = len(vertices) - 1
                midpoints[key] = index
            return index

        out = []
        changed = False
        for (a, b, c) in triangles:
            lengths = []
            for i, j in ((a, b), (b, c), (c, a)):
                dx = vertices[i][0] - vertices[j][0]
                dy = vertices[i][1] - vertices[j][1]
                lengths.append(dx * dx + dy * dy)
            longest = max(range(3), key=lambda k: lengths[k])
            if lengths[longest] <= max_sq:
                out.append((a, b, c))
                continue
            changed = True
            if longest == 0:
                mid = midpoint(a, b)
                out.append((a, mid, c))
                out.append((mid, b, c))
            elif longest == 1:
                mid = midpoint(b, c)
                out.append((b, mid, a))
                out.append((mid, c, a))
            else:
                mid = midpoint(c, a)
                out.append((c, mid, b))
                out.append((mid, a, b))
        triangles = out
        if not changed:
            break
    return vertices, triangles


def _reconstruct_surface_3d(map_point, child_fills, child_pattern,
                            grid_target=52, child_area=None):
    """Isometric shape-from-template over the Third plane.

    THE MODEL (user specification): the Third Cartesian plane is the
    OBJECT in its undeformed state - filled regions are the solid - and
    MainView is a camera looking at the deformed object in the world
    (camera fixed at (0,-100)). The mapping's JACOBIAN carries the
    reconstruction: an isometrically bent sheet projects each unit
    tangent with foreshortening cos(theta) along the bend direction, so
    the SVD of J = dM/d(u,v) gives, pointwise,
      - the local slope magnitude  |dz/ds| = sqrt(s0^2 - sigma_min^2)/s0
        (s0 = the un-foreshortened scale, taken as the high quantile of
        sigma_max over the solid - a developable sheet always has an
        uncompressed direction),
      - the slope DIRECTION (the singular vector of sigma_min - the
        foreshortened axis is the one tilted away from the camera),
      - the FACE via sign(det J): crossing a fold flips the sheet, and
        with it the z-gradient - the parity is read straight off the
        Jacobian, no crease bookkeeping needed.
    Integrating that gradient field (least-squares Poisson, conjugate
    gradients on the solid mask) yields z(u,v); world x/y are the
    MainView image itself, so looking from the camera reproduces the
    MainView picture exactly (weak-perspective lift).
    Returns dict with vertices/faces/colors/strokes3d/bounds, or None.
    """
    warp = getattr(map_point, "warp", None)

    def image_of_third(u, v):
        z = warp.apply((u, v)) if warp is not None else (u, v)
        return map_point.main_frame.hv(*map_point.scale_arcs(*z))

    # The solid: fills' rings pulled to Third; strokes join the extent so
    # linework outside the fills still gets a relief field to drape on.
    # Containment testing decimates each ring to <=192 points and carries
    # a bbox pre-filter: the raw rings arrive densified (thousands of
    # points), and the un-filtered scan froze the UI for tens of seconds.
    def decimate(ring, cap=192):
        if len(ring) <= cap:
            return ring
        step = len(ring) / cap
        return [ring[int(k * step)] for k in range(cap)]

    rings_third = []
    rings_fine = []
    fill_colors = []
    for fill in child_fills or []:
        color = fill.get("color") or {}
        rgba = (int(color.get("r", 0)), int(color.get("g", 0)),
                int(color.get("b", 0)), int(color.get("a", 255)))
        group = []
        group_fine = []
        for source_ring in _path_commands_to_polygons(fill.get("commands")):
            if len(source_ring) >= 2 \
                    and _dist(source_ring[0], source_ring[-1]) <= 1e-9:
                source_ring = source_ring[:-1]  # drop the closing duplicate
            if len(source_ring) < 3:
                continue
            # Respect the child mapping area: the real mapping deletes
            # what falls outside it, and the object must match.
            for ring in _clip_rings_to_area([source_ring], child_area):
                if len(ring) < 3:
                    continue
                exact = [tuple(map_point.coords(p)) for p in ring]
                # TWO resolutions with two jobs: containment probes use a
                # decimated copy (bbox-gated, cheap), but TRIANGULATION
                # keeps the EXACT ring - the boundary must be the same
                # geometry the outline strokes drape along, or the two
                # part with sawtooth gaps (user report; an RDP pass here
                # chorded curves the strokes still followed).
                coarse = decimate(exact)
                bbox = (min(p[0] for p in coarse),
                        min(p[1] for p in coarse),
                        max(p[0] for p in coarse),
                        max(p[1] for p in coarse))
                group.append((coarse, bbox))
                group_fine.append(exact)
        if group:
            rings_third.append(group)
            rings_fine.append(group_fine)
            fill_colors.append(rgba)
    stroke_info = []
    for stroke in child_pattern or []:
        style_color, _w = _stroke_style(stroke, 1.0)
        for poly in _stroke_polylines(stroke):
            for piece in _clip_polyline(poly, child_area):
                if len(piece) >= 2:
                    stroke_info.append(
                        ([tuple(map_point.coords(p)) for p in piece],
                         style_color))
    stroke_third = [poly for poly, _color in stroke_info]
    solid_pts = [p for group in rings_third for ring, _b in group
                 for p in ring]
    if not solid_pts:
        solid_pts = [p for poly in stroke_third for p in poly]
    if not solid_pts:
        return None
    extent_pts = solid_pts + [p for poly in stroke_third for p in poly]
    u0 = min(p[0] for p in extent_pts)
    u1 = max(p[0] for p in extent_pts)
    v0 = min(p[1] for p in extent_pts)
    v1 = max(p[1] for p in extent_pts)
    span = max(u1 - u0, v1 - v0, 1e-6)
    pad = 0.02 * span
    u0 -= pad; u1 += pad; v0 -= pad; v1 += pad
    nx = max(8, int(round(grid_target * (u1 - u0) / span)))
    ny = max(8, int(round(grid_target * (v1 - v0) / span)))
    du = (u1 - u0) / nx
    dv = (v1 - v0) / ny

    def in_group(u, v, group):
        hits = 0
        for ring, (bx0, by0, bx1, by1) in group:
            if bx0 <= u <= bx1 and by0 <= v <= by1 \
                    and _point_in_ring((u, v), ring):
                hits += 1
        return hits % 2 == 1

    def inside_solid(u, v):
        if not rings_third:
            return True
        return any(in_group(u, v, group) for group in rings_third)

    def color_at(u, v):
        # TOP fill wins: _collect_pattern_fills returns bottom-first
        # (paintGL order), and the first hit used to hand overlapping
        # fills the bottom colour.
        for group, rgba in zip(reversed(rings_third),
                               reversed(fill_colors)):
            if in_group(u, v, group):
                return rgba
        return None

    # Node grid: keep nodes whose neighbourhood touches the solid OR a
    # stroke - linework outside the fills needs a relief field too, or it
    # snapped flat to z=0 the moment it left the mask.
    stroke_cells = set()
    for poly in stroke_third:
        cum = _cumulative_lengths(poly)
        count = max(len(poly), int(cum[-1] / max(min(du, dv), 1e-6)) + 2)
        for k in range(count):
            p = _point_at_arc(poly, cum, cum[-1] * k / max(count - 1, 1))
            stroke_cells.add((int((p[0] - u0) / du), int((p[1] - v0) / dv)))

    node_index = {}
    nodes = []
    for j in range(ny + 1):
        for i in range(nx + 1):
            u = u0 + du * i
            v = v0 + dv * j
            near_stroke = any((i + a, j + b) in stroke_cells
                              for a in (-1, 0) for b in (-1, 0))
            if near_stroke or any(
                    inside_solid(u + du * a * 0.5, v + dv * b * 0.5)
                    for a in (-1, 0, 1) for b in (-1, 0, 1)):
                node_index[(i, j)] = len(nodes)
                nodes.append((i, j, u, v))
    if len(nodes) < 4:
        return None

    # Jacobian, SVD-derived slope field and face sign per node. The
    # difference step spans a full grid step: the guides are POLY_STEP
    # polylines, so hv()'s derivative is a staircase at ~4 px scale, and
    # a quarter-cell probe read every tread as a slope change - the
    # integrated surface came out with an unnatural depth-direction
    # ripple (user report). A cell-scale step averages across treads;
    # folds stay sharp because det's SIGN is unaffected by widening.
    h = 0.6 * min(du, dv)
    jac = []
    sigma_max_all = []
    for _i, _j, u, v in nodes:
        px = image_of_third(u + h, v)
        mx = image_of_third(u - h, v)
        py = image_of_third(u, v + h)
        my = image_of_third(u, v - h)
        a = (px[0] - mx[0]) / (2 * h)
        c = (px[1] - mx[1]) / (2 * h)
        b = (py[0] - my[0]) / (2 * h)
        d = (py[1] - my[1]) / (2 * h)
        jac.append((a, b, c, d))
        ee = a * a + c * c
        gg = b * b + d * d
        ff = a * b + c * d
        tr = ee + gg
        det = ee * gg - ff * ff
        disc = max(0.0, tr * tr * 0.25 - det)
        s_max = math.sqrt(max(1e-12, tr * 0.5 + math.sqrt(disc)))
        sigma_max_all.append(s_max)
    # The un-foreshortened scale: the MEDIAN of sigma_max. Most of the
    # sheet lies flat (sigma_max = sigma_min = s0); a high quantile got
    # polluted by the fold band, where the WARP genuinely stretches the
    # material (AM2 is not isometric inside a fold) - reading that
    # stretch as "facing the camera" inflated every slope and tripled
    # the relief. Where sigma exceeds s0 the excess is in-plane stretch,
    # not tilt, and the point is treated as flat (cos = 1).
    scale0 = sorted(sigma_max_all)[len(sigma_max_all) // 2]
    scale0 = max(scale0, 1e-9)

    grads = []
    handed = []
    for (a, b, c, d) in jac:
        ee = a * a + c * c
        gg = b * b + d * d
        ff = a * b + c * d
        tr = ee + gg
        det2 = ee * gg - ff * ff
        disc = max(0.0, tr * tr * 0.25 - det2)
        s_min_sq = max(0.0, tr * 0.5 - math.sqrt(disc))
        s_max_sq = max(1e-12, tr * 0.5 + math.sqrt(disc))
        # TILT is sigma_min falling below the point's UN-TILTED scale:
        #   cos(theta) = sigma_min / min(sigma_max, s0).
        # The denominator choice carries three measured lessons at once:
        # dividing by the global s0 alone misread every isotropic scale
        # wobble as a full-magnitude slope in a rounding-determined
        # direction (hand-jittered guides fabricated relief on a
        # bit-exact identity map), dividing by sigma_max alone misread
        # the fold band's ANISOTROPIC in-plane stretch (sigma_max 2.5,
        # sigma_min 1 - the warp stretches material along the fold-back)
        # as a steep tilt, and min(sigma_max, s0) keeps both flat while
        # a genuine tilt (sigma_max ~ s0, sigma_min below) still reads
        # its full angle. A 2% dead zone absorbs flattening noise.
        cos_t = min(1.0, math.sqrt(s_min_sq) /
                    max(min(math.sqrt(s_max_sq), scale0), 1e-9) / 0.98)
        mag = math.sqrt(max(0.0, 1.0 - cos_t * cos_t))
        # eigenvector of J^T J for the SMALL eigenvalue: foreshortened axis
        lam = s_min_sq
        vx, vy = (ff, lam - ee) if abs(ff) > 1e-12 else \
            ((1.0, 0.0) if ee <= gg else (0.0, 1.0))
        norm = math.hypot(vx, vy) or 1.0
        grads.append((mag * vx / norm, mag * vy / norm))
        handed.append(1.0 if (a * d - b * c) >= 0.0 else -1.0)
    majority = 1.0 if sum(handed) >= 0 else -1.0

    # Orient the (sign-ambiguous) eigenvectors coherently, then flip by
    # face parity: crossing a fold reverses the physical slope.
    oriented = [None] * len(nodes)
    for k, (i, j, _u, _v) in enumerate(nodes):
        best = None
        for (di, dj) in ((-1, 0), (0, -1), (-1, -1), (1, -1)):
            n = node_index.get((i + di, j + dj))
            if n is not None and oriented[n] is not None:
                best = oriented[n]
                break
        g = grads[k]
        if best is not None and g[0] * best[0] + g[1] * best[1] < 0.0:
            g = (-g[0], -g[1])
        oriented[k] = g
    # SIGN RECOVERY ACROSS TANGENCY LINES. Direction coherence alone can
    # only reverse the slope where det J flips, but a crest or trough -
    # the sheet momentarily facing the camera - reverses the physical
    # slope with NO det flip, and integrating the unsigned field turned
    # every wave into a monotone staircase of double the true relief
    # (and broke mirror symmetry on a symmetric squeeze). The two
    # branches are genuinely indistinguishable in the image (the metric
    # sees only |grad z|), so the MINIMAL-RELIEF prior decides: sloped
    # regions are segmented along the low-magnitude valleys (tangency
    # lines), and ADJACENT regions get OPPOSITE signs over a spanning
    # tree - a wave comes back as a wave, a symmetric squeeze as a
    # symmetric bulge, never as a doubled-height staircase. (Per-node
    # annealing was tried first: a chain of nodes must flip together,
    # and single-site moves cannot cross that barrier.)
    mags = [math.hypot(*g) for g in oriented]
    positive = sorted(m for m in mags if m > 1e-6)
    mag_hi = 0.35 * positive[len(positive) // 2] if positive else 0.0
    labels = [-1] * len(nodes)
    region_count = 0
    for seed in range(len(nodes)):
        if labels[seed] != -1 or mags[seed] <= mag_hi:
            continue
        queue = [seed]
        labels[seed] = region_count
        while queue:
            k = queue.pop()
            ki, kj = nodes[k][0], nodes[k][1]
            for (di, dj) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = node_index.get((ki + di, kj + dj))
                if n is not None and labels[n] == -1 and mags[n] > mag_hi:
                    labels[n] = region_count
                    queue.append(n)
        region_count += 1
    region_sign = [1.0] * region_count
    if region_count > 1:
        # Adjacency across valleys: scan rows and columns; consecutive
        # labelled stretches separated only by low-mag nodes are
        # neighbours.
        adjacency = {}
        for axis in (0, 1):
            majors = range(ny + 1) if axis == 0 else range(nx + 1)
            minors = range(nx + 1) if axis == 0 else range(ny + 1)
            for major in majors:
                last_label = -1
                gap = 0
                for minor in minors:
                    key = (minor, major) if axis == 0 else (major, minor)
                    n = node_index.get(key)
                    if n is None:
                        last_label = -1
                        continue
                    lab = labels[n]
                    if lab == -1:
                        gap += 1
                        continue
                    if last_label != -1 and lab != last_label:
                        pair = (min(last_label, lab), max(last_label, lab))
                        adjacency[pair] = adjacency.get(pair, 0) + 1
                    last_label = lab
                    gap = 0
        # Spanning propagation: neighbours alternate (minimal-relief
        # prior; the DEPTH-anchored orientation below overrides it
        # wherever the 2D pipeline has an opinion).
        visited = [False] * region_count
        for root in range(region_count):
            if visited[root]:
                continue
            visited[root] = True
            queue = [root]
            while queue:
                r = queue.pop()
                for (p, q), _votes in adjacency.items():
                    other = q if p == r else (p if q == r else None)
                    if other is not None and not visited[other]:
                        visited[other] = True
                        region_sign[other] = -region_sign[r]
                        queue.append(other)

    # THE RED HANDLE DECIDES NEAR AND FAR. The 2D pipeline stacks its
    # layers by _fold_depth from the nearest-point anchor, and the 3D
    # object must occlude the same way (user report: they disagreed -
    # the sign priors above are geometry-blind). Each node gets the 2D
    # stacking depth of its child point; each sloped region is then
    # oriented so walking UP its slope walks toward SMALLER depth
    # (nearer the camera = larger z).
    node_depth = [None] * len(nodes)
    if getattr(map_point, "depth_curves", None):
        child_frame_local = map_point.child_frame
        for k, (_i, _j, u, v) in enumerate(nodes):
            # Only SOLID nodes carry stacking evidence: the grid extends
            # past the fills to drape strokes, and those overhang nodes
            # (no gradient information, drifting z) once diluted the
            # depth-class means until the ordering check misfired.
            if not inside_solid(u, v):
                continue
            child_pt = child_frame_local.hv(u, v)
            try:
                side = _fold_sign(map_point, child_pt)
                node_depth[k] = _fold_depth(map_point, child_pt, side)
            except Exception:
                node_depth[k] = None
        for region in range(region_count):
            score = 0.0
            votes = 0
            for k in range(len(nodes)):
                if labels[k] != region:
                    continue
                g = oriented[k]
                length = math.hypot(*g)
                if length < 1e-9:
                    continue
                gx = g[0] * region_sign[region] * handed[k] * majority \
                    / length
                gy = g[1] * region_sign[region] * handed[k] * majority \
                    / length
                ki, kj = nodes[k][0], nodes[k][1]
                d_here = node_depth[k]
                if d_here is None:
                    continue

                def first_other_depth(sx, sy):
                    # Walk until the stacking CHANGES: a frame fold's
                    # depth step lives across a narrow crease, and a
                    # fixed two-cell probe often landed short of it.
                    for radius in range(1, 7):
                        n = node_index.get((ki + int(round(sx * radius)),
                                            kj + int(round(sy * radius))))
                        if n is None:
                            return None
                        dn = node_depth[n]
                        if dn is not None and dn != d_here:
                            return dn
                    return None

                da = first_other_depth(gx, gy)
                db = first_other_depth(-gx, -gy)
                if da is None and db is None:
                    continue
                # +grad points toward larger z; depth must SHRINK there.
                if da is not None:
                    score += d_here - da
                    votes += 1
                if db is not None:
                    score += db - d_here
                    votes += 1
            if os.environ.get("ANIMEAN_TO3D_DEBUG"):
                print(f"[to3d-debug] region {region}: votes {votes} "
                      f"score {score:+.1f} sign {region_sign[region]:+.0f}"
                      f"{' -> FLIP' if votes and score < 0.0 else ''}")
            if votes and score < 0.0:
                region_sign[region] = -region_sign[region]
    signs = [region_sign[labels[k]] if labels[k] != -1 else 1.0
             for k in range(len(nodes))]
    # Valley nodes inherit the nearest labelled sign along their row so
    # the near-zero band blends smoothly between the two regions.
    for k in range(len(nodes)):
        if labels[k] != -1:
            continue
        ki, kj = nodes[k][0], nodes[k][1]
        for radius in range(1, 4):
            found = None
            for (di, dj) in ((radius, 0), (-radius, 0),
                             (0, radius), (0, -radius)):
                n = node_index.get((ki + di, kj + dj))
                if n is not None and labels[n] != -1:
                    found = signs[n]
                    break
            if found is not None:
                signs[k] = found
                break
    target = [(oriented[k][0] * signs[k] * handed[k] * majority,
               oriented[k][1] * signs[k] * handed[k] * majority)
              for k in range(len(nodes))]
    # Smooth the slope field between SAME-FACE neighbours before
    # integrating: the per-node SVD carries sampling noise from the
    # polyline guides, and integrating the raw field produced an
    # unnatural depth-direction ripple. Averaging never crosses a fold
    # (handed flips there and the two sides genuinely oppose), so
    # creases keep their kink while flat stretches actually flatten.
    for _pass in range(2):
        smoothed = list(target)
        for k, (i, j, _u, _v) in enumerate(nodes):
            acc_x = 2.0 * target[k][0]
            acc_y = 2.0 * target[k][1]
            weight = 2.0
            for (di, dj) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = node_index.get((i + di, j + dj))
                if n is not None and handed[n] == handed[k]:
                    acc_x += target[n][0]
                    acc_y += target[n][1]
                    weight += 1.0
            smoothed[k] = (acc_x / weight, acc_y / weight)
        target = smoothed

    # Least-squares Poisson: minimize |grad z - target|^2 over the mask
    # (pure-python conjugate gradients; the grid is ~2-3k nodes).
    neigh = []
    for i, j, _u, _v in nodes:
        entry = []
        for (di, dj, axis, sign) in ((1, 0, 0, 1.0), (-1, 0, 0, -1.0),
                                     (0, 1, 1, 1.0), (0, -1, 1, -1.0)):
            n = node_index.get((i + di, j + dj))
            if n is not None:
                entry.append((n, axis, sign))
        neigh.append(entry)

    rhs_grad = [0.0] * len(nodes)
    for k, entry in enumerate(neigh):
        acc = 0.0
        for n, axis, sign in entry:
            step = du if axis == 0 else dv
            tk = target[k][axis]
            tn = target[n][axis]
            # NEGATED divergence so that grad z == +target: the earlier
            # sign made z the NEGATIVE integral (harmless while a global
            # flip absorbed it, but the depth votes assume walking up
            # +target walks up z - with the old sign the votes flipped
            # regions exactly backwards).
            acc -= sign * 0.5 * (tk + tn) * step
        rhs_grad[k] = acc

    def solve(anchor_weight, z_ref):
        def apply_A(z):
            out = [0.0] * len(z)
            for k, entry in enumerate(neigh):
                acc = anchor_weight[k] * z[k] if anchor_weight else 0.0
                for n, _axis, _sign in entry:
                    acc += z[k] - z[n]
                out[k] = acc
            return out

        grounded = bool(anchor_weight) and any(anchor_weight)
        rhs = list(rhs_grad)
        if grounded:
            for k in range(len(rhs)):
                rhs[k] += anchor_weight[k] * z_ref[k]
        z = [0.0] * len(nodes)
        r = [rhs[k] - v for k, v in enumerate(apply_A(z))]
        if not grounded:
            mean_r = sum(r) / len(r)
            r = [v - mean_r for v in r]      # Neumann null space
        p = list(r)
        rs_old = sum(v * v for v in r)
        for _ in range(min(400, 4 * len(nodes))):
            if rs_old < 1e-8:
                break
            ap = apply_A(p)
            denom = sum(pv * apv for pv, apv in zip(p, ap)) or 1e-12
            alpha = rs_old / denom
            z = [zv + alpha * pv for zv, pv in zip(z, p)]
            r = [rv - alpha * apv for rv, apv in zip(r, ap)]
            if not grounded:
                mean_r = sum(r) / len(r)
                r = [v - mean_r for v in r]
            rs_new = sum(v * v for v in r)
            p = [rv + (rs_new / rs_old) * pv for rv, pv in zip(r, p)]
            rs_old = rs_new
        return z

    # PASS 1: pure isometric integration - the shape.
    z = solve(None, None)
    # PASS 2, only on disagreement: ground the layers on the 2D stacking.
    # When pass 1 already stacks every depth class in the red-handle
    # order, the pure isometric answer stands untouched. When it does
    # not (integration cannot see the offset across a fold, and on frame
    # folds it drifted into the wrong order - the user's report), a WEAK
    # anchor (lambda 0.2 against a Laplacian diagonal of ~4) pulls each
    # node toward its layer's reference height: 2D occlusion wins over
    # isometric purity, locally the shape stays gradient-driven. The gap
    # unit comes from pass 1's own relief.
    depths_known = sorted({d for d in node_depth if d is not None})

    def stacking_violations(field):
        """Fraction of NEIGHBOURING node pairs with different 2D depths
        whose z-order contradicts the stacking. Class means once passed
        while whole neighbourhoods interleaved - exactly what the eye
        sees at a fold edge - so the check is local, where occlusion is
        actually felt."""
        bad = 0
        total = 0
        for k, entry in enumerate(neigh):
            dk = node_depth[k]
            if dk is None:
                continue
            for n, _axis, _sign in entry:
                if n <= k:
                    continue
                dn = node_depth[n]
                if dn is None or dn == dk:
                    continue
                total += 1
                if (field[k] - field[n]) * (dn - dk) < 0.0:
                    bad += 1
        return ((bad / total) if total else 0.0), total

    ordered = True
    if len(depths_known) >= 2:
        rate, pair_total = stacking_violations(z)
        ordered = pair_total == 0 or rate < 0.15
        if os.environ.get("ANIMEAN_TO3D_DEBUG"):
            print(f"[to3d-debug] pass1 neighbour stacking violations: "
                  f"{100.0 * rate:.1f}% of {pair_total} pairs")
    if len(depths_known) >= 2 and not ordered:
        zs_sorted = sorted(z)
        low = zs_sorted[int(0.05 * (len(z) - 1))]
        high = zs_sorted[int(0.95 * (len(z) - 1))]
        gap = max((high - low) / (depths_known[-1] - depths_known[0] + 1),
                  4.0 * max(du, dv))
        d_mean = sum(depths_known) / len(depths_known)
        z_ref = [0.0] * len(nodes)
        anchor_weight = [0.0] * len(nodes)
        for k in range(len(nodes)):
            if node_depth[k] is not None:
                z_ref[k] = -(node_depth[k] - d_mean) * gap
                anchor_weight[k] = 0.35
        z = solve(anchor_weight, z_ref)
        # (A boundary-band-only anchor was tried and rejected: freeing
        # the class interiors let the integration balloon between the
        # pinned band and the free middle - waviness rose 45%.)
        if os.environ.get("ANIMEAN_TO3D_DEBUG"):
            rate2, pair_total2 = stacking_violations(z)
            print(f"[to3d-debug] grounded pass: violations "
                  f"{100.0 * rate2:.1f}% of {pair_total2} pairs, "
                  f"gap {gap:.1f}")
    z_mean = sum(z) / len(z)
    z = [(v - z_mean) * scale0 for v in z]   # into canvas units
    # Global concavity: the per-region orientation fixes RELATIVE signs;
    # the remaining all-or-nothing flip is settled the same way - deeper
    # 2D stacking must sit farther from the camera (smaller z).
    known = [(zv, node_depth[k]) for k, zv in enumerate(z)
             if node_depth[k] is not None]
    if known:
        za = sum(v for v, _d in known) / len(known)
        da = sum(d for _v, d in known) / len(known)
        covariance = sum((v - za) * (d - da) for v, d in known)
        if covariance > 0.0:
            z = [-v for v in z]

    # Relief lookup off the node grid; mesh vertices and draped strokes
    # both read it. CATMULL-ROM bicubic where the 4x4 neighbourhood is
    # complete: the render mesh is finer than the solve grid, and
    # bilinear z left a C1 crease along every grid line - a regular
    # depth-direction ripple across the whole sheet (user report).
    # Bilinear remains the fallback near mask edges.
    def z_bilinear(u, v):
        fi = min(max((u - u0) / du, 0.0), nx - 1e-6)
        fj = min(max((v - v0) / dv, 0.0), ny - 1e-6)
        i = int(fi)
        j = int(fj)
        acc = 0.0
        wsum = 0.0
        for (ii, jj, wu, wv) in ((i, j, 1 - (fi - i), 1 - (fj - j)),
                                 (i + 1, j, fi - i, 1 - (fj - j)),
                                 (i, j + 1, 1 - (fi - i), fj - j),
                                 (i + 1, j + 1, fi - i, fj - j)):
            n = node_index.get((ii, jj))
            if n is not None:
                w = wu * wv
                acc += w * z[n]
                wsum += w
        return acc / wsum if wsum > 1e-9 else 0.0

    def spline4(p0, p1, p2, p3, t):
        return 0.5 * ((2.0 * p1)
                      + (-p0 + p2) * t
                      + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t
                      + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t * t)

    def z_at(u, v):
        fi = min(max((u - u0) / du, 0.0), nx - 1e-6)
        fj = min(max((v - v0) / dv, 0.0), ny - 1e-6)
        i = int(fi)
        j = int(fj)
        rows = []
        for jj in (j - 1, j, j + 1, j + 2):
            row = []
            for ii in (i - 1, i, i + 1, i + 2):
                n = node_index.get((ii, jj))
                if n is None:
                    return z_bilinear(u, v)
                row.append(z[n])
            rows.append(row)
        tx = fi - i
        ty = fj - j
        col = [spline4(*row, tx) for row in rows]
        return spline4(col[0], col[1], col[2], col[3], ty)

    # MESH: each fill triangulated FROM ITS OUTLINE (ear clipping with
    # hole bridging, then longest-edge subdivision so the interior
    # follows the bend) - the boundary is the artist's contour, not a
    # grid staircase. Rings nest odd-even by boundary probes; fills stack
    # bottom-to-top with a hair of z separation against z-fighting.
    vertices = []
    colors = []
    faces = []
    vertex_uv = []
    # ONE target edge length everywhere. Boundary rings, crease
    # constraints, stroke draping and the interior area bound all share
    # it: a dense boundary against a coarse interior produced a ring of
    # sliver fans along every edge and crease, and their interpolated
    # normals shaded as zigzags (user report).
    mesh_len = min(du, dv)
    max_edge = 1.4 * mesh_len
    layer_lift = 0.05

    # Fold creases as INTERIOR mesh constraints: without them triangles
    # straddle the crease and its z kink renders as a sawtooth of
    # corners (user report). child_cutters_raw carries every crease -
    # frame folds and warp loci alike - in child space; pull to Third
    # and resample to the shared step.
    crease_step = mesh_len
    crease_chains = []
    if getattr(map_point, "depth_curves", None):
        try:
            _child_cutters(map_point)
        except Exception:
            pass
        for raw in getattr(map_point, "child_cutters_raw", None) or []:
            chain = [tuple(map_point.coords(p)) for p in raw]
            cum = _cumulative_lengths(chain)
            if cum[-1] < 2.0 * crease_step:
                continue
            count = max(2, int(cum[-1] / crease_step))
            crease_chains.append(
                [_point_at_arc(chain, cum, cum[-1] * k / count)
                 for k in range(count + 1)])

    def clip_chain(chain, ring, holes):
        runs = []
        run = []
        for p in chain:
            inside = _point_in_ring(p, ring) \
                and not any(_point_in_ring(p, hole) for hole in holes)
            if inside:
                run.append(p)
            else:
                if len(run) >= 2:
                    runs.append(run)
                run = []
        if len(run) >= 2:
            runs.append(run)
        out = []
        for r in runs:
            # shrink the ends one sample: a constraint endpoint touching
            # the boundary polygon is a degenerate PSLG for the mesher
            trimmed = r[1:-1] if len(r) > 3 else r
            if len(trimmed) >= 2:
                out.append(trimmed)
        return out

    def boundary_probe(ring):
        a, b = ring[0], ring[1 % len(ring)]
        return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)

    for order, (group_fine, rgba) in enumerate(zip(rings_fine, fill_colors)):
        rings = group_fine
        levels = []
        for index, ring in enumerate(rings):
            probe = boundary_probe(ring)
            levels.append(sum(1 for j, other in enumerate(rings)
                              if j != index and _point_in_ring(probe, other)))
        for index, ring in enumerate(rings):
            if levels[index] % 2 != 0:
                continue
            holes = []
            for j, other in enumerate(rings):
                if j != index and levels[j] % 2 == 1 \
                        and _point_in_ring(boundary_probe(other), ring):
                    holes.append(other)
            # Primary: constrained-Delaunay quality mesh (library) - the
            # area bound does the interior refinement, so no separate
            # subdivision and no T-junction seams. The boundary rings are
            # densified to the SAME arc step the strokes drape at, so an
            # outline drawn along the fill edge lands on the mesh
            # boundary point-for-point instead of cutting chords across
            # it. Fallback: earcut on RDP-simplified rings plus bisection
            # (degraded: chords and hairline seams possible).
            def dense_ring(points):
                # Corner-preserving: RDP drops only collinear clutter
                # (0.15 px), then long SEGMENTS get interior points at
                # the shared step. Uniform arc resampling replaced the
                # ring's corners with chords, and strokes drawn along
                # the true outline fell OUTSIDE the mesh (2 px gaps).
                slim = _rdp_ring(points, 0.15)
                out = []
                closed = slim + [slim[0]]
                for a, b in zip(closed, closed[1:]):
                    out.append(a)
                    seg = math.hypot(b[0] - a[0], b[1] - a[1])
                    pieces = int(seg / mesh_len)
                    for k in range(1, pieces):
                        t = k * mesh_len / seg
                        out.append((a[0] + (b[0] - a[0]) * t,
                                    a[1] + (b[1] - a[1]) * t))
                return out

            constraints = []
            for chain in crease_chains:
                constraints.extend(clip_chain(chain, ring, holes))
            # Equilateral area at the shared edge length, so interior
            # triangles come out the same size as the boundary segments
            # instead of 3-6x coarser (the sliver-fan source).
            quality = _triangulate_quality(dense_ring(ring),
                                           [dense_ring(h) for h in holes],
                                           0.433 * mesh_len * mesh_len,
                                           constraints=constraints)
            if quality is not None:
                flat_verts, tris = quality
            else:
                flat_verts, tris = _triangulate_polygon(
                    _rdp_ring(ring, 0.6), [_rdp_ring(h, 0.6) for h in holes])
                flat_verts, tris = _subdivide_mesh(flat_verts, tris, max_edge)
            base = len(vertices)
            for (u, v) in flat_verts:
                q = image_of_third(u, v)
                vertices.append((q[0], q[1], z_at(u, v)
                                 + order * layer_lift))
                colors.append(rgba)
                vertex_uv.append((u, v))
            faces.extend((base + a, base + b, base + c)
                         for a, b, c in tris)

    # Drape strokes ON THE MESH ITSELF: a stroke point's z comes from
    # barycentric interpolation over the triangle that contains it, so a
    # line running along a fill edge lies exactly in the fill's surface
    # (sampling the node-grid field instead left a z-chord gap wherever
    # the boundary edge spanned curvature). Falls back to the grid field
    # off the mesh.
    tri_hash = {}
    hash_cell = max(mesh_len, 1e-6)
    for f_index, (a, b, c) in enumerate(faces):
        us = [vertex_uv[a][0], vertex_uv[b][0], vertex_uv[c][0]]
        vs = [vertex_uv[a][1], vertex_uv[b][1], vertex_uv[c][1]]
        for gx in range(int(min(us) / hash_cell) - 1,
                        int(max(us) / hash_cell) + 2):
            for gy in range(int(min(vs) / hash_cell) - 1,
                            int(max(vs) / hash_cell) + 2):
                tri_hash.setdefault((gx, gy), []).append(f_index)

    def z_on_mesh(u, v):
        for f_index in tri_hash.get((int(u / hash_cell),
                                     int(v / hash_cell)), ()):
            a, b, c = faces[f_index]
            ax, ay = vertex_uv[a]
            bx, by = vertex_uv[b]
            cx, cy = vertex_uv[c]
            det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(det) < 1e-12:
                continue
            w0 = ((by - cy) * (u - cx) + (cx - bx) * (v - cy)) / det
            w1 = ((cy - ay) * (u - cx) + (ax - cx) * (v - cy)) / det
            w2 = 1.0 - w0 - w1
            # Loose tolerance: boundary-riding stroke points sit exactly
            # on mesh edges and must still claim the adjacent face.
            if w0 < -1e-3 or w1 < -1e-3 or w2 < -1e-3:
                continue
            return (w0 * vertices[a][2] + w1 * vertices[b][2]
                    + w2 * vertices[c][2])
        return None

    strokes3d = []
    stroke_colors = []
    for poly, style_color in stroke_info:
        dense = poly
        cum = _cumulative_lengths(poly)
        if cum[-1] > 0:
            # Fine draping step - decoupled from the mesh edge length,
            # since z now comes off the mesh surface itself; bounded,
            # because a tiny solid once made du/dv microscopic and a
            # long stroke exploded.
            count = max(len(poly),
                        min(int(cum[-1] / (0.5 * mesh_len)) + 2,
                            4 * len(poly) + 2048))
            dense = [_point_at_arc(poly, cum, cum[-1] * k / (count - 1))
                     for k in range(count)]
        pts = []
        for (u, v) in dense:
            q = image_of_third(u, v)
            zz = z_on_mesh(u, v)
            if zz is None:
                zz = z_at(u, v)
            pts.append((q[0], q[1], zz))
        if len(pts) >= 2:
            strokes3d.append(pts)
            stroke_colors.append([style_color[0], style_color[1],
                                  style_color[2]])

    return {"vertices": vertices, "faces": faces, "colors": colors,
            "strokes": strokes3d, "stroke_colors": stroke_colors,
            "scale0": scale0, "uv": vertex_uv}


def run_to_3d():
    """Menu entry: reconstruct the mapped sheet's 3D shape and open a
    Three.js viewer for it.

    User specification: the Third Cartesian plane is the OBJECT MATRIX in
    its undeformed state (filled regions are the solid), MainView is the
    camera view of the deformed object (a constant camera, the
    (0,-100)-style world placement), and the mapping's JACOBIAN recovers
    the deformed object state - see _reconstruct_surface_3d.
    """
    child = _scene_model("child")
    main = _scene_model("main")
    child_frame = max(child.current_frame(), 0)
    # Same preflight as a real run: guides may still live in layers on a
    # freshly loaded legacy document, and _mapper_from_assets carries the
    # missing-guide AND crossing checks (a guessed origin from
    # non-crossing guides exports unbounded garbage).
    _absorb_legacy_items("child", child, child_frame)
    _absorb_legacy_items("main", main, max(main.current_frame(), 0))
    child_pattern = _collect_pattern_strokes(child, child_frame,
                                             want_commands=False)
    child_fills = _collect_pattern_fills(child, child_frame)
    if not child_pattern and not child_fills:
        print("[auto_mapping] To 3D: child_paint_view has nothing to map.")
        return False
    map_point, width_scale = _mapper_from_assets()
    if map_point is None:
        print(f"[auto_mapping] To 3D: cannot build mapping: {width_scale}")
        return False
    for note in getattr(map_point, "additional_notes", ()):
        print(f"[auto_mapping] warning: {note}")
    # The 2D pipeline's occlusion is the AUTHORITY on near/far: the red
    # nearest-point handle anchors _fold_depth, and the reconstruction
    # orients its slopes so the 3D stacking matches what the 2D render
    # shows (without it, the sign prior guessed and could disagree).
    if _FOLD["split"]:
        ranges = _pattern_arc_ranges(map_point, child_pattern, child_fills)
        if ranges is not None:
            _prepare_fold_context(map_point, ranges[0], ranges[1])

    child_area = (_assets_for("child").get(MAPPING_AREA_PROPERTY)
                  or {}).get("polygons")
    surface = _reconstruct_surface_3d(map_point, child_fills, child_pattern,
                                      child_area=child_area)
    if surface is None or not surface["faces"]:
        print("[auto_mapping] To 3D: no solid (filled) region to "
              "reconstruct - fill the pattern on the child board first.")
        return False

    path = os.path.join(tempfile.gettempdir(),
                        f"animean_to3d_{int(time.time())}.html")
    _export_three_html(surface, path)
    zs = [v[2] for v in surface["vertices"]]
    print(f"[auto_mapping] To 3D: reconstructed the deformed sheet - "
          f"{len(surface['vertices'])} vertices / {len(surface['faces'])} "
          f"faces, {len(surface['strokes'])} draped stroke(s), relief "
          f"span {max(zs) - min(zs):.1f} px -> {path}")
    try:
        os.startfile(path)
    except Exception as error:
        print(f"[auto_mapping] To 3D: could not open the browser ({error}); "
              "open the file above manually.")
    return True


def _export_three_html(surface, path):
    """Self-contained Three.js viewer for the reconstructed sheet: the
    triangulated solid with per-vertex fill colors, draped strokes, orbit
    controls, a relief-scale slider, and the CONSTANT camera (the user's
    (0,-100)-style world placement, expressed as a fixed matrix over the
    normalized scene)."""
    data = {
        "vertices": [[round(x, 2), round(y, 2), round(z, 2)]
                     for x, y, z in surface["vertices"]],
        "faces": [list(face) for face in surface["faces"]],
        "colors": [[c[0], c[1], c[2]] for c in surface["colors"]],
        "strokes": [[[round(x, 2), round(y, 2), round(z, 2)]
                     for x, y, z in poly]
                    for poly in surface["strokes"]],
        "strokeColors": surface.get("stroke_colors") or [],
    }
    payload = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    html = _TO3D_TEMPLATE.replace("__ANIMEAN_DATA__", payload)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(html)


_TO3D_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>AnimeAn To 3D</title>
<style>
  html, body { margin: 0; height: 100%; overflow: hidden; background: #eef0f4; }
  #ui { position: fixed; top: 10px; left: 10px; color: #23262e;
        font: 13px/1.5 system-ui, sans-serif; background: rgba(255,255,255,.85);
        padding: 10px 14px; border-radius: 8px; user-select: none;
        box-shadow: 0 2px 8px rgba(30,34,44,.18); }
  #ui input[type=range] { width: 180px; vertical-align: middle; }
</style>
</head>
<body>
<div id="ui">
  <div><b>AnimeAn To 3D</b> - reconstructed sheet</div>
  <div>Relief scale <input id="relief" type="range" min="0" max="300"
       value="100"></div>
  <div><label><input id="flip" type="checkbox"> flip relief</label>
       <label style="margin-left:12px"><input id="wire" type="checkbox">
       wireframe</label></div>
  <div><button id="homecam" type="button" style="margin-top:4px">
       original camera</button></div>
  <div style="opacity:.7">drag: rotate &middot; wheel: zoom &middot;
       right-drag: pan</div>
</div>
<script type="importmap">
{"imports":{"three":"https://unpkg.com/three@0.160.0/build/three.module.js",
"three/addons/":"https://unpkg.com/three@0.160.0/examples/jsm/"}}
</script>
<script type="module">
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const DATA = __ANIMEAN_DATA__;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0xeef0f4);
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(innerWidth, innerHeight);
renderer.setPixelRatio(devicePixelRatio);
document.body.appendChild(renderer.domElement);

// Normalize: canvas x/y (y flipped up), z = relief. The camera looks down
// -z exactly like MainView does, so the initial view IS the MainView
// picture; the constant matrix is the (0,-100)-style world placement.
let minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
for (const [x, y] of DATA.vertices) {
  if (x < minX) minX = x; if (x > maxX) maxX = x;
  if (y < minY) minY = y; if (y > maxY) maxY = y;
}
const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
const span = Math.max(maxX - minX, maxY - minY, 1);
const S = 2 / span;

const group = new THREE.Group();
scene.add(group);

const positions = new Float32Array(DATA.vertices.length * 3);
const baseZ = new Float32Array(DATA.vertices.length);
const colors = new Float32Array(DATA.vertices.length * 3);
DATA.vertices.forEach(([x, y, z], i) => {
  positions[3 * i] = (x - cx) * S;
  positions[3 * i + 1] = (cy - y) * S;
  positions[3 * i + 2] = z * S;
  baseZ[i] = z * S;
  const c = DATA.colors[i] || [200, 200, 200];
  colors[3 * i] = c[0] / 255;
  colors[3 * i + 1] = c[1] / 255;
  colors[3 * i + 2] = c[2] / 255;
});
const geo = new THREE.BufferGeometry();
geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
geo.setIndex(DATA.faces.flat());
geo.computeVertexNormals();
const mat = new THREE.MeshStandardMaterial({
  vertexColors: true, side: THREE.DoubleSide, metalness: 0.0,
  roughness: 0.85, flatShading: false });
const mesh = new THREE.Mesh(geo, mat);
group.add(mesh);

const strokeObjs = [];
DATA.strokes.forEach((poly, idx) => {
  const pts = poly.map(([x, y, z]) =>
      new THREE.Vector3((x - cx) * S, (cy - y) * S, z * S));
  const g = new THREE.BufferGeometry().setFromPoints(pts);
  const sc = DATA.strokeColors[idx] || [28, 30, 36];
  const line = new THREE.Line(g, new THREE.LineBasicMaterial({
      color: new THREE.Color(sc[0] / 255, sc[1] / 255, sc[2] / 255) }));
  line.userData.baseZ = poly.map(p => p[2] * S);
  strokeObjs.push(line);
  group.add(line);
});

function applyRelief(factor) {
  const pos = geo.getAttribute('position');
  for (let i = 0; i < baseZ.length; i++) pos.setZ(i, baseZ[i] * factor);
  pos.needsUpdate = true;
  geo.computeVertexNormals();
  for (const line of strokeObjs) {
    const p = line.geometry.getAttribute('position');
    const bz = line.userData.baseZ;
    for (let i = 0; i < bz.length; i++)
      p.setZ(i, bz[i] * factor + 0.004);
    p.needsUpdate = true;
  }
}
let reliefValue = 1.0, flip = false;
const update = () => applyRelief(reliefValue * (flip ? -1 : 1));
document.getElementById('relief').addEventListener('input', e => {
  reliefValue = parseFloat(e.target.value) / 100; update(); });
document.getElementById('flip').addEventListener('change', e => {
  flip = e.target.checked; update(); });
update();   // apply the stroke lift NOW, or lines z-fight until touched
document.getElementById('wire').addEventListener('change', e => {
  mat.wireframe = e.target.checked; });

scene.add(new THREE.AmbientLight(0xffffff, 0.75));
const sun = new THREE.DirectionalLight(0xffffff, 1.1);
sun.position.set(1.2, 1.8, 2.4);
scene.add(sun);
scene.add(new THREE.GridHelper(4, 20, 0xb9bfcc, 0xd4d8e2)
    .translateY(-1.4));

// ORTHOGRAPHIC camera: the reconstruction is a weak-perspective lift, so
// only a parallel projection reproduces MainView exactly - a perspective
// start violated the method's own premise (and the relief slider could
// push the sheet through the near plane).
const aspect0 = innerWidth / innerHeight;
const camera = new THREE.OrthographicCamera(
    -1.35 * aspect0, 1.35 * aspect0, 1.35, -1.35, -50, 50);
// CONSTANT camera matrix: the (0,-100)-style placement - straight back
// along the view axis, looking at the sheet exactly as MainView does
// (world units are normalized; the distance is depth-irrelevant under
// an orthographic projection).
const CAMERA_MATRIX = new THREE.Matrix4().fromArray([
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 2.6, 1]);
CAMERA_MATRIX.decompose(camera.position, camera.quaternion, camera.scale);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.target.set(0, 0, 0);
// "original camera": restore the constant initial framing (the MainView
// angle) after any amount of orbiting.
const HOME = { position: camera.position.clone(),
               quaternion: camera.quaternion.clone(),
               zoom: camera.zoom,
               target: controls.target.clone() };
document.getElementById('homecam').addEventListener('click', () => {
  camera.position.copy(HOME.position);
  camera.quaternion.copy(HOME.quaternion);
  camera.zoom = HOME.zoom;
  camera.updateProjectionMatrix();
  controls.target.copy(HOME.target);
  controls.update();
});

addEventListener('resize', () => {
  const aspect = innerWidth / innerHeight;
  camera.left = -1.35 * aspect;
  camera.right = 1.35 * aspect;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});
(function tick() {
  requestAnimationFrame(tick);
  controls.update();
  renderer.render(scene, camera);
})();
</script>
</body>
</html>
"""


# A View menu on each board. Both are named "view" and differ by HOST, which
# is the point: "show the refer grid" is a question about one board, and the
# answer for the texture is not the answer for the main view. Registered at
# import time because the menus exist from startup, before any tool has been
# armed (register_hooks() only runs on tool activation).
for _host in ("main", "child"):
    python_hooks.register_menu({
        "name": VIEW_MENU_NAME,
        "title": "View",
        "host": _host,
        "items": _view_menu_items(_host),   # callable: re-read on every open
    })
python_hooks.set_hook(_view_menu_action, menu=True)
# The texture board can be locked against accidental edits; these are the
# tools that still work while it is.
python_hooks.register_protected_properties("child", [H_PROPERTY, V_PROPERTY,
                                                     ADDITIONAL_PROPERTY])
# The layer-panel context menu has the same requirement: right-clicking a
# mapping group must work in a fresh session, before any tool is armed.
python_hooks.register_menu_provider(_layer_menu_items)
# The menu bar and its settings window: registered at import so they exist in
# a fresh session, before any tool has been armed.
python_hooks.register_menu({
    "name": MENU_NAME,
    "title": "Auto Mapping",
    "items": _menu_items,          # callable: re-evaluated on every open
})
python_hooks.register_settings(LINE_SETTINGS_NAME, _line_settings_layout)
python_hooks.set_hook(_menu_action, menu=True)
python_hooks.set_hook(_line_display_changed, option=True)
python_hooks.set_hook(_layer_menu_action, layermenu=True)
# The nearest-end anchor (red handle) reacts from startup: its drag events
# arrive whenever the main guides exist, not only while a tool is armed.
python_hooks.set_hook(_nearest_handle_event, handle=True)
# Guides and additional lines are draggable overlays; their drag is reported
# through the same handle events.
python_hooks.set_hook(_guide_drag_event, handle=True)
