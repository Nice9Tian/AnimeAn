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
import json
import math

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
GUIDE_LAYER_PROPERTY = "auto_mapped_guide"
MAPPING_GROUP_NAME = "Auto Mapping"
# Everything a run puts on the board. None of it may act as a wall for region
# detection, and none of it may be picked up as pattern by the next run.
MAPPING_OUTPUT_PROPERTIES = (MAPPED_PROPERTY, BACK_PROPERTY, SEAL_PROPERTY,
                             GUIDE_LAYER_PROPERTY)
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
_CURVE_MODE = {"value": "spline"}

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

# Refer-rect debug grid state.
_REFER_RECT = {"enabled": False}
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


def refer_rect_enabled():
    return _REFER_RECT["enabled"]


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


class _Frame:
    """One board's HV frame: two crossing guides plus the coordinate system
    they induce. `hv` is THE shared reconstruction function - child and main
    differ only in the point lists handed to it.

    Coordinates (l_h, l_v) are SIGNED ARC LENGTHS measured from the crossing
    along each guide. Arc length (not chord projection) is what makes the
    child side symmetric with the main side; see build_mapper.
    """

    def __init__(self, h_points, v_points):
        self.h = h_points
        self.v = v_points
        self.h_cum = _cumulative_lengths(h_points)
        self.v_cum = _cumulative_lengths(v_points)
        self.h_total = self.h_cum[-1]
        self.v_total = self.v_cum[-1]
        self.origin, self.h_arc, self.v_arc = _polyline_intersection(h_points, v_points)
        # Outward extent on each side of the crossing, floored at 1% so a
        # T-shaped crossing cannot divide by (nearly) zero. The RAW values are
        # kept too: the floored ones cannot tell "collapsed side" apart from
        # "genuinely 1% long", which is what the transfer scales need to know.
        self.h_side = _arc_sides(self.h_total, self.h_arc)
        self.v_side = _arc_sides(self.v_total, self.v_arc)
        self.h_side_raw = (self.h_arc, self.h_total - self.h_arc)
        self.v_side_raw = (self.v_arc, self.v_total - self.v_arc)
        # Orientation tests compare guide DIRECTIONS, and a hand-drawn polyline's
        # per-segment tangents jitter enough to flip that comparison several
        # times within a few px - measured on a real 301-point guide, the fold
        # count oscillated between 2 and 4 eleven times along V, which shredded
        # the output into fragments and tangled the crease. Smoothing the
        # direction over an arc window (the rule the direction arrows already
        # use) removes the flicker and leaves the real bend intact: an 8 px
        # window was enough to make the count constant, and a 65 px one does
        # not move the fold position further.
        self.h_window = max(2.0 * POLY_STEP, 0.03 * self.h_total)
        self.v_window = max(2.0 * POLY_STEP, 0.03 * self.v_total)

    def directions(self, l_h, l_v):
        """Window-smoothed guide directions, for ORIENTATION tests only.

        Never for the Newton solve in `solve`: there the exact per-segment
        tangent is the true derivative of `hv`, and smoothing it would break
        convergence.
        """
        return (_tangent_at_arc(self.h, self.h_cum, self.h_arc + l_h, self.h_window),
                _tangent_at_arc(self.v, self.v_cum, self.v_arc + l_v, self.v_window))

    def hv(self, l_h, l_v):
        """H(l_h) + V(l_v) - O: the Coons patch in its collapsed form."""
        on_h = _point_at_arc(self.h, self.h_cum, self.h_arc + l_h)
        on_v = _point_at_arc(self.v, self.v_cum, self.v_arc + l_v)
        return (on_h[0] + on_v[0] - self.origin[0],
                on_h[1] + on_v[1] - self.origin[1])

    def jacobian(self, l_h, l_v):
        """Columns of d(hv)/d(l_h, l_v): the two guide tangents."""
        return (_direction_at_arc(self.h, self.h_cum, self.h_arc + l_h),
                _direction_at_arc(self.v, self.v_cum, self.v_arc + l_v))

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


def build_mapper(child_h_points, child_v_points, main_h_points, main_v_points, info=None):
    """Build point mapper from the child frame to the main frame.

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
    if min(len(child_h_points), len(child_v_points), len(main_h_points), len(main_v_points)) < 2:
        return None, "a center line has fewer than 2 points"

    child = _Frame(child_h_points, child_v_points)
    main = _Frame(main_h_points, main_v_points)
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

    def coords(point):
        """A point's arc-length coordinates in the child frame.

        Seeded with the cheap chord-basis estimate, which is exact for
        straight child guides - that is why the whole mapper reduces to the
        previous implementation there.
        """
        dx = point[0] - child.origin[0]
        dy = point[1] - child.origin[1]
        guess_h = (dx * ev[1] - dy * ev[0]) / det * 0.5 * child_h_chord
        guess_v = (eh[0] * dy - eh[1] * dx) / det * 0.5 * child_v_chord
        return child.solve(point, guess_h, guess_v)

    def map_point(point):
        l_h, l_v = coords(point)
        rebuilt = child.hv(l_h, l_v)
        image = main.hv(l_h * (h_scale_pos if l_h >= 0.0 else h_scale_neg),
                        l_v * (v_scale_pos if l_v >= 0.0 else v_scale_neg))
        return (image[0] + point[0] - rebuilt[0],
                image[1] + point[1] - rebuilt[1])

    # Arc lengths on both sides now, so a curved child guide scales widths by
    # its real length rather than by its chord.
    width_scale = math.sqrt((main.h_total / child.h_total) * (main.v_total / child.v_total))

    map_point.coords = coords
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


def _lerp(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def _cubic_point(cub, t):
    """de Casteljau evaluation of a cubic Bezier (p0, c1, c2, p3) at t."""
    p0, c1, c2, p3 = cub
    a = _lerp(p0, c1, t)
    b = _lerp(c1, c2, t)
    c = _lerp(c2, p3, t)
    d = _lerp(a, b, t)
    e = _lerp(b, c, t)
    return _lerp(d, e, t)


def _split_cubic(cub, t0, t1):
    """Sub-cubic covering the parameter range [t0, t1] of `cub` (de Casteljau)."""
    p0, c1, c2, p3 = cub
    # left split at t1, then right split of that at t0 / t1.
    def split_at(curve, t):
        q0, q1, q2, q3 = curve
        a = _lerp(q0, q1, t)
        b = _lerp(q1, q2, t)
        c = _lerp(q2, q3, t)
        d = _lerp(a, b, t)
        e = _lerp(b, c, t)
        f = _lerp(d, e, t)
        return (q0, a, d, f), (f, e, c, q3)
    if t1 < 1.0:
        cub = split_at(cub, t1)[0]
    if t0 > 0.0:
        denom = t1 if t1 > 1e-12 else 1e-12
        cub = split_at(cub, t0 / denom)[1]
    return cub


def _line_cubic(p0, p3):
    """Represent a straight segment as a cubic so all output shares one type."""
    return (p0, _lerp(p0, p3, 1.0 / 3.0), _lerp(p0, p3, 2.0 / 3.0), p3)


def _flatten_cubic(cub, out, step=POLY_STEP):
    """Append samples of one cubic to `out` (excluding its start point)."""
    p0, c1, c2, p3 = cub
    net = _dist(p0, c1) + _dist(c1, c2) + _dist(c2, p3)
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

    The map is H(l) + V(l) rebuilt by arc length on both boards, so its only
    kinks sit where a coordinate crosses a guide VERTEX - on the child frame
    (where the coordinates themselves break) or, after the per-side scale, on
    the main frame. Those are the samples flatness probes are worst at
    finding: a source segment can span whole periods of a wavy guide and land
    every probe back on the chord (the aliasing case in the spec). Feeding
    them in directly costs one coordinate solve per endpoint and removes the
    guesswork; the adaptive probes stay as the safety net for everything
    these do not cover (a non-injective child frame, where the coordinates
    are only a best effort and this whole argument breaks down).
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
            own_cum, own_arc = child.h_cum, child.h_arc
            far_cum, far_arc = main.h_cum, main.h_arc
            scale_neg, scale_pos = map_point.h_scales
        else:
            own_cum, own_arc = child.v_cum, child.v_arc
            far_cum, far_arc = main.v_cum, main.v_arc
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
    net = _dist(w0, out_c1) + _dist(out_c1, out_c2) + _dist(out_c2, w3)
    probes = max(3, min(17, int(math.ceil(net / _FORCE_STEP))))
    ts = [(k + 1.0) / (probes + 1.0) for k in range(probes)] + [_PROBE_T]
    worst = 0.0
    for t in ts:
        worst = max(worst, _dist(map_point(_cubic_point(cub, t)), _cubic_point(out, t)))
    if worst <= tol or depth >= max_depth:
        return [out]
    left = _split_cubic(cub, 0.0, 0.5)
    right = _split_cubic(cub, 0.5, 1.0)
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
                c1 = (a[0] + 2.0 / 3.0 * (ctrl[0] - a[0]), a[1] + 2.0 / 3.0 * (ctrl[1] - a[1]))
                c2 = (b[0] + 2.0 / 3.0 * (ctrl[0] - b[0]), b[1] + 2.0 / 3.0 * (ctrl[1] - b[1]))
                current.append((a, c1, c2, b))
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
        child_assets[H_PROPERTY]["points"], child_assets[V_PROPERTY]["points"],
        main_assets[H_PROPERTY]["points"], main_assets[V_PROPERTY]["points"])
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
                _tangent_at_arc(child.h, child.h_cum, child.h_arc + arc_h, child.h_window),
                _tangent_at_arc(main.h, main.h_cum, main.h_arc + arc_h * scale, main.h_window))

    def tangents_v(arc_v):
        scale = mapper.v_scales[1] if arc_v >= 0.0 else mapper.v_scales[0]
        return (scale,
                _tangent_at_arc(child.v, child.v_cum, child.v_arc + arc_v, child.v_window),
                _tangent_at_arc(main.v, main.v_cum, main.v_arc + arc_v * scale, main.v_window))

    def fold_sign(h_cache, v_cache):
        scale_h, (tcx, tcy), (tmx, tmy) = h_cache
        scale_v, (ncx, ncy), (nmx, nmy) = v_cache
        denominator = tcx * ncy - tcy * ncx
        if abs(denominator) < 1e-12:
            return 1  # folded child frame: no usable orientation, same as _orientation
        value = scale_h * scale_v * (tmx * nmy - tmy * nmx) / denominator
        raw = 1 if value > 0.0 else -1
        return 1 if raw == reference else -1

    columns = [tangents_h(h_lo + (index + 0.5) * col_step) for index in range(cols)]
    back_arc = 0.0
    for row in range(rows):
        arc_v = v_lo + (row + 0.5) * row_step
        v_cache = tangents_v(arc_v)

        def sign_at(arc_h):
            return fold_sign(tangents_h(arc_h), v_cache)

        # cell-centred signs from the cached column tangents
        signs = [fold_sign(h_cache, v_cache) for h_cache in columns]
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
                "fill_color": OCCLUSION_FILL,
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
    for prop, color in ((H_PROPERTY, H_COLOR), (V_PROPERTY, V_COLOR)):
        guide = assets.get(prop)
        if guide and len(guide.get("points") or []) >= 2:
            width = float(guide.get("width", 3.0))
            items.append({
                "id": prop,
                "points": guide["points"],
                "color": color,
                "width": width,
                "removable": True,
            })
            arrow = _direction_arrow_points(guide["points"], max(12.0, 3.5 * width))
            if arrow:
                items.append({
                    "id": prop + "_arrow",
                    "points": arrow,
                    "color": color,
                    "width": width,
                    "removable": False,
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
    return items


def _push_overlay(view_name):
    """Send this view's mapping assets to the generic C++ overlay display."""
    try:
        _animean().ui.set_overlay(view_name, overlay_items(view_name))
    except Exception as error:
        print(f"[auto_mapping] overlay update failed: {error}")


def _set_draw_color(color):
    try:
        _animean().ui.set_draw_color(color)
    except Exception:
        pass


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
    assets = _assets_for("child")
    if H_PROPERTY not in assets or V_PROPERTY not in assets:
        return None
    h_pts = assets[H_PROPERTY]["points"]
    v_pts = assets[V_PROPERTY]["points"]
    if len(h_pts) < 2 or len(v_pts) < 2:
        return None
    frame = _Frame(h_pts, v_pts)
    if frame.h_total <= 1e-6 or frame.v_total <= 1e-6:
        return None
    return frame


def _frame_point(frame, u_hat, v_hat):
    """Child-space point at normalised grid coords (u_hat, v_hat in [-1, 1])."""
    du = u_hat * (frame.h_side[1] if u_hat >= 0.0 else frame.h_side[0])
    dv = v_hat * (frame.v_side[1] if v_hat >= 0.0 else frame.v_side[0])
    return frame.hv(du, dv)


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
            main_assets[H_PROPERTY]["points"], main_assets[V_PROPERTY]["points"])
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
    too when refer rect is on, since the main grid mirrors the child frame.
    Symmetrically, the child's occlusion tint depends on the MAIN guides, so
    a main-side change must re-push the child overlay."""
    _invalidate_grid_cache()
    _push_overlay(view_name)
    if _REFER_RECT["enabled"] and view_name != "main":
        _push_overlay("main")
    if _OCCLUSION["enabled"] and view_name != "child":
        _push_overlay("child")


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
    scale_h = map_point.h_scales[1] if l_h >= 0.0 else map_point.h_scales[0]
    scale_v = map_point.v_scales[1] if l_v >= 0.0 else map_point.v_scales[0]
    (tmx, tmy), (nmx, nmy) = main.directions(l_h * scale_h, l_v * scale_v)
    value = scale_h * scale_v * (tmx * nmy - tmy * nmx) / denominator
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


def _crease_curves(map_point, v_range, samples=48, max_columns=600):
    """The fold loci, traced from the FRAMES rather than from the strokes.

    det J vanishes where the main H direction turns parallel to the main V
    direction, so the crease is a property of the two guides alone. For every
    sampled V arc the H guide is walked and the sign changes are bracketed and
    bisected; the crossings are then chained into curves BY RANK, which is
    valid because sign changes along H alternate and therefore cannot swap
    order.

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
    main = map_point.main_frame
    low, high = v_range
    if high - low <= 1e-6 or len(main.h) < 3:
        return []

    total = main.h_cum[-1]
    step = max(POLY_STEP, total / 400.0)

    def crossings_at(l_v):
        normal = _tangent_at_arc(main.v, main.v_cum, main.v_arc + l_v, main.v_window)

        def side_at(arc):
            tangent = _tangent_at_arc(main.h, main.h_cum, main.h_arc + arc, main.h_window)
            return tangent[0] * normal[1] - tangent[1] * normal[0]

        found = []
        arc = -main.h_arc
        limit = total - main.h_arc
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

    def column(l_v):
        arcs = crossings_at(l_v)
        return (l_v, arcs, [main.hv(arc, l_v) for arc in arcs])

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
    # H position ripples a couple of px between adjacent heights. Smooth that
    # away so the cusp test in _emit_seals fires on real cusps only.
    smoothed = []
    for curve in curves:
        arcs = [arc for arc, _ in curve]
        for _ in range(2):
            arcs = ([arcs[0]]
                    + [(arcs[i - 1] + 2.0 * arcs[i] + arcs[i + 1]) * 0.25
                       for i in range(1, len(arcs) - 1)]
                    + [arcs[-1]])
        smoothed.append([(arcs[i], curve[i][1]) for i in range(len(curve))])
    return smoothed


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


class _MappedOutput:
    """Buffers emitted strokes per fold side, then flushes them into layers.

    Buffering (rather than writing straight into an image) keeps two things
    simple: layers are created only for sides that actually got content - so
    a run with no fold produces exactly one layer, as before - and they can
    be created in z-order (front on top, crease, then back) regardless of
    the order the strokes were generated in.
    """

    FRONT, BACK, SEAL = 1, -1, 0
    _NAMES = {FRONT: MAPPED_LAYER_NAME, BACK: BACK_LAYER_NAME, SEAL: SEAL_LAYER_NAME}
    _PROPERTIES = {FRONT: MAPPED_PROPERTY, BACK: BACK_PROPERTY, SEAL: SEAL_PROPERTY}

    def __init__(self, animean, scene, row):
        self.animean = animean
        self.scene = scene
        self.row = row
        self.buffers = {self.FRONT: [], self.BACK: [], self.SEAL: []}
        self.layers = []
        # Where the emitters actually cut the artwork, in mapped space. The
        # crease anchors onto these, so they have to come from the geometry
        # that gets DRAWN: bezier mode splits the smoothed cubics, while
        # _stroke_polylines falls back to the artist's raw input trail when a
        # stroke carries commands instead of polylines - a different curve.
        self.cuts = []

    def add_polyline(self, side, points, color, width):
        if len(points) < 2:
            return False
        self.buffers[side].append(("polyline", points, color, width))
        return True

    def add_curved(self, side, commands, flat, color, width):
        if len(commands) < 2 or len(flat) < 2:
            return False
        self.buffers[side].append(("curved", (commands, flat), color, width))
        return True

    def count(self, side=None):
        if side is None:
            return sum(len(items) for items in self.buffers.values())
        return len(self.buffers[side])

    def _track_new_layer(self, layer):
        """Record a freshly created layer; everything older shifted down one.

        _create_mapped_layer moves what it makes to index 0, so every index
        this object already holds is now one lower in the stack.
        """
        self.layers = [index + 1 for index in self.layers]
        self.layers.append(layer)
        return layer

    def add_guide_layers(self, h_points, v_points, width):
        """Draw the two MAIN guide axes into their own layers.

        These are the axes that shaped this run, recorded next to the result
        they produced, on the board the result landed on. The live guides stay
        where they were - session assets drawn as overlays, still editable and
        still removable by their x badge - so this is a snapshot, not a move:
        re-running the mapping after nudging a guide gives a new group whose
        axes show what THAT run used.
        """
        created = []
        for name, points in ((H_LAYER_NAME, h_points), (V_LAYER_NAME, v_points)):
            if not points or len(points) < 2:
                continue
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
            color = H_COLOR if name == H_LAYER_NAME else V_COLOR
            obj = self.animean.vectorlogic.make_stroke_object(
                [(float(x), float(y)) for x, y in points], color, width,
                image.stroke_count() + 1, False, False)
            obj.property = GUIDE_LAYER_PROPERTY
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

    def flush(self):
        """Create the needed layers bottom-up and write the buffers out."""
        added = 0
        for side in (self.BACK, self.SEAL, self.FRONT):
            items = self.buffers[side]
            if not items:
                continue
            layer = _create_mapped_layer(self.scene, self.row, self._NAMES[side])
            if layer < 0:
                print(f"[auto_mapping] could not create the '{self._NAMES[side]}'.")
                continue
            self._track_new_layer(layer)
            image = self.scene.image_at(self.row, layer, True)
            if image is None:
                print(f"[auto_mapping] '{self._NAMES[side]}' has no editable cell.")
                continue
            prop = self._PROPERTIES[side]
            for kind, payload, color, width in items:
                if kind == "polyline":
                    obj = self.animean.vectorlogic.make_stroke_object(
                        payload, color, width, image.stroke_count() + 1, False, False)
                else:
                    commands, flat = payload
                    obj = self.animean.vectorlogic.make_stroke_object_from_path(
                        commands, flat, color, width, image.stroke_count() + 1)
                obj.property = prop
                if side == self.SEAL:
                    obj.pen_style = _FOLD["seal_pen_style"]
                image.add_stroke_object(obj)
                added += 1
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
    return color_tuple if side == _MappedOutput.FRONT else _FOLD["back_color"]


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
                flagged = _adaptive_map_polyline(map_point, run)
                for clipped in _clip_flagged(flagged, main_area):
                    points = _decimate_between_anchors(clipped, eps)
                    if out.add_polyline(side, points, _side_style(side, color_tuple), width):
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
                flagged = _adaptive_map_polyline(map_point, run)
                for clipped in _clip_flagged(flagged, main_area):
                    knots = _decimate_between_anchors(clipped, eps)
                    commands, flat = _cubics_to_commands(_catmull_rom_cubics(knots))
                    if out.add_curved(side, commands, flat,
                                      _side_style(side, color_tuple), width):
                        added += 1
    return added


def _emit_bezier_mode(animean, out, stroke, map_point, child_area, main_area, color_tuple, width):
    """Keep the artist's Bezier segments; transport each handle through the warp."""
    added = 0
    for cubics in _commands_to_subpaths(stroke.get("commands")):
        for src_piece in _clip_cubics(cubics, child_area):
            for run, side in _fold_runs_cubic(map_point, src_piece, out.cuts):
                out_cubics = []
                for cub in run:
                    out_cubics.extend(_warp_cubic(map_point, cub))
                for out_piece in _clip_cubics(out_cubics, main_area):
                    commands, flat = _cubics_to_commands(out_piece)
                    if out.add_curved(side, commands, flat,
                                      _side_style(side, color_tuple), width):
                        added += 1
    return added


def _split_cubic_by_fold(map_point, cub):
    """Split ONE source cubic where the map's orientation flips inside it.

    Unlike the polyline path there is no analytic knot list for a cubic, so
    the crossings are located by scanning the source parameter and bisecting.
    That is legitimate here: the sign is piecewise constant, so a sign change
    between two probes brackets exactly one cell boundary.
    """
    net = _dist(cub[0], cub[1]) + _dist(cub[1], cub[2]) + _dist(cub[2], cub[3])
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
    v_low = v_high = None
    for stroke in pattern:
        for poly in _stroke_polylines(stroke):
            for piece in _clip_polyline(poly, child_area):
                for point in piece:
                    l_v = map_point.coords(point)[1]
                    arc_v = l_v * (map_point.v_scales[1] if l_v >= 0.0
                                   else map_point.v_scales[0])
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

    color = _FOLD["back_color"]
    # Thinner than the artwork: the crease is an annotation of the fold, not
    # part of the pattern, and at 2x width_scale it out-weighted every stroke
    # it was meant to terminate.
    width = max(0.5, 0.8 * width_scale)
    for curve in _crease_curves(map_point, (v_low, v_high)):
        points = [main.hv(arc, other) for arc, other in curve]
        if len(points) < 2:
            continue
        # Anchor the WHOLE branch before cutting it up. A cusp is a feature of
        # one continuous fold, not a break in it, so a cut past the cusp still
        # vouches for the stretch leading up to it; anchoring the two arms
        # separately orphaned a cut 145 px from anything drawn.
        points = _trim_to_anchors(points, cuts, tolerance)
        if len(points) < 2 or _cumulative_lengths(points)[-1] < 2.0 * POLY_STEP:
            continue
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
                out.add_polyline(_MappedOutput.SEAL, piece, color, width)


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
        mode = "spline"

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
    if mapper_info.get("mirrored"):
        print("[auto_mapping] note: the child and main guide frames have OPPOSITE "
              "handedness, so the result is a MIRROR image. If you wanted it "
              "unmirrored, reverse ONE main center line (redraw it in the other "
              "direction - watch the arrows).")

    child_area = (child_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")
    main_area = (main_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")

    out = _MappedOutput(animean, main, main_frame)
    emit = _EMITTERS[mode]
    generated = 0
    clipped_out = 0
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
        if _FOLD["split"] and _FOLD["seal"] and out.count(_MappedOutput.BACK):
            _emit_seals(animean, out, map_point, child_pattern, child_area,
                        main_area, width_scale)
        added = out.flush()
        if added:
            # Provenance first, so the axes sit above the artwork they shaped
            # and the whole run collapses to a single panel row.
            guide_layers = out.add_guide_layers(main_assets[H_PROPERTY]["points"],
                                                main_assets[V_PROPERTY]["points"],
                                                max(1.0, 0.8 * width_scale))
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
    back_count = out.count(_MappedOutput.BACK)
    if back_count:
        summary += (f"; {back_count} stroke(s) landed on the BACK of a fold "
                    f"(det J < 0) and went to '{BACK_LAYER_NAME}'")
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
    _run()


def _tool_option_changed(cell, stroke, message):
    hook = message.get("hook")
    if hook == "curve_mode":
        value = str(message.get("value", "")).lower()
        if value in CURVE_MODES and _CURVE_MODE["value"] != value:
            _CURVE_MODE["value"] = value
            print(f"[auto_mapping] curve mode -> {value}")
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
    if hook != "refer_rect":
        return
    enabled = str(message.get("value", "")).lower() == "on"
    if _REFER_RECT["enabled"] == enabled:
        return
    _REFER_RECT["enabled"] = enabled
    _invalidate_grid_cache()
    _push_overlay("child")
    _push_overlay("main")
    print(f"[auto_mapping] refer rect grid {'ON' if enabled else 'OFF'}")


def _view_button_toggled(message):
    if message.get("view") != "child" or message.get("name") != OCCLUSION_BUTTON:
        return
    _OCCLUSION["enabled"] = bool(message.get("on"))
    _OCCLUSION_CACHE["items"] = None
    _push_overlay("child")  # computes and caches the bands as a side effect
    if not _OCCLUSION["enabled"]:
        print("[auto_mapping] occlusion preview OFF")
        return
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


def register_hooks():
    python_hooks.set_hook(_capture_mapping_item, linefinish=True, tool="extra")
    python_hooks.set_hook(_overlay_removed, overlayremove=True)
    python_hooks.set_hook(_history_restored, historyrestore=True)
    python_hooks.set_hook(_auto_mapping_button, extra=True, tool=AUTO_MAPPING2_TOOL)
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


def run_auto_mapping(name=AUTO_MAPPING2_TOOL, property_value=AUTO_MAPPING2_TOOL):
    global _last_run_handled
    register_hooks()
    if _last_run_handled:
        # the "extra" event hook already performed this click's mapping
        _last_run_handled = False
        return property_value
    _run()
    return property_value


# The occlusion toggle lives on the child window itself, so it must work from
# startup - BEFORE any mapping tool has been armed. Register the button and
# its hook at import time (register_hooks() only runs on tool activation).
python_hooks.register_view_button("child", {
    "name": OCCLUSION_BUTTON,
    "title": "Occluded Areas",
    "tooltip": "Tint the parts of this texture that the current mapping "
               "folds face-down (covered by the lining).",
    "checkable": True,
})
python_hooks.set_hook(_view_button_toggled, viewbutton=True)
