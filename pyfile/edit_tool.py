"""Arrow-tool edit modes: outline and move, perceptual handles, stored points.

Three modes, chosen in the Arrow tool's options. All three describe the SAME
picked object, so switching between them keeps the pick and re-derives the new
mode's affordances from it - a mode switch used to end the session and cost a
re-click, which is the one thing the user could not get back.

DEFAULT is the resting mode: a click OUTLINES whatever it lands on - a stroke
first, a filled region second - and dragging inside that outline translates it.

DEBUG shows the geometry as it is stored. Every path element grows its true
handles - a cubic its p1/c1/c2/p4, a line its two end points, a raw polyline
every vertex - with the tangent arms drawn as overlay lines. Dragging edits
that exact point. This is the diagnostic view: dense, but honest.

ARTIST shows only the points a viewer can actually individuate. Dominant
points (curvature-salient vertices) are detected on the stroke's flattening
and then filtered by a PSYCHOPHYSICAL spacing threshold: the eye resolves
detail down to a fixed ANGULAR period, so the minimum useful handle spacing
is fixed in screen pixels and maps through the zoom into document space -
zooming in reveals more handles, zooming out melts them away. Dragging a
pseudo-handle deforms the stroke locally with a smooth falloff that reaches
to the neighbouring key points.

A picked FILL REGION is editable in Artist and Debug too: one handle per
stored boundary vertex, dragged straight onto the region's path. Both modes
show the same vertex set - the artist fit is a stroke idea and a region has no
stroke to fit - and a cubic's control points ride with the vertex they belong
to instead of becoming handles of their own.

The selection outline is drawn in EVERY mode: it is the pick made visible.

The C++ side is a pure mechanism (render dots, hit-test, report drags as
"handle" events); everything a handle MEANS lives here.
"""
import math

import overlay_stack

import bezier
import python_hooks
import viewscale

POLY_STEP = 4.0

MODES = ("default", "artist", "debug")
# last_zoom: option events carry no zoom, so a Debug->Artist switch would
# otherwise rebuild with zoom 1.0 and show the wrong handle density until the
# next wheel tick. Every handle event refreshes it.
_STATE = {"mode": "default", "last_zoom": 1.0}
# The Default mode's outline: colour and width are settings-window controls
# (Arrow menu -> Outline Display Settings), so the highlight can be seen
# against any artwork.
_OUTLINE = {"outline_color": (255, 140, 40, 235), "outline_width": 2.0}

# view name -> live edit session
_SESSIONS = {}

SHAPE_ANCHOR = 0    # square: an on-curve point
SHAPE_CONTROL = 1   # circle: a Bezier control point
SHAPE_PSEUDO = 2    # diamond: a synthesized artist-mode key point

ANCHOR_COLOR = (255, 255, 255, 255)
CONTROL_COLOR = (130, 185, 255, 255)
PSEUDO_COLOR = (255, 200, 70, 255)
ARM_COLOR = (150, 165, 205, 210)

# --- the perceptual spacing threshold ---------------------------------------
# The contrast sensitivity function of human vision (Campbell & Robson 1968;
# Barten's model) peaks at roughly 2-6 cycles per degree and this app's
# calibration work uses 4 cpd as the peak. Two marks closer than one period
# of the peak-sensitivity grating stop being individually resolvable objects
# and fuse into texture - which is exactly the failure the artist mode must
# avoid: a wall of handles nobody can pick apart.
#
# Converting that angular period to screen pixels: at the canonical viewing
# distance of 57 cm (where 1 cm on screen subtends 1 degree) a 96-dpi logical
# pixel grid puts 96/2.54 = 37.8 px into that centimetre, so one degree is
# 37.8 px and one period at 4 cpd is 37.8 / 4 = 9.45 px. Handles closer than
# that ON SCREEN are dropped; dividing by the zoom turns the threshold into
# document units, which is what makes the filter zoom-dependent.
VIEWING_DISTANCE_CM = 57.0
LOGICAL_DPI = 96.0
CSF_PEAK_CPD = 4.0


def perceptual_min_separation_px():
    px_per_degree = math.tan(math.radians(1.0)) * VIEWING_DISTANCE_CM * (LOGICAL_DPI / 2.54)
    return px_per_degree / CSF_PEAK_CPD


def edit_mode():
    return _STATE["mode"]


# --- small geometry helpers --------------------------------------------------

def _dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _cubic_point(p0, c1, c2, p3, t):
    # Evaluation via the shared wheel pyfile/bezier.py (the mirror of
    # algorithm/beziersplit.h); this module's four-loose-points signature
    # stays for its call sites.
    return bezier.eval_cubic((p0, c1, c2, p3), t)


def _sample_cubic(p0, c1, c2, p3, step=POLY_STEP):
    # Density (t-uniform, hull-driven, uncapped) is THIS module's policy -
    # _dominant_indices measures perceptual gaps on exactly these samples.
    count = max(2, int(math.ceil(bezier.hull_length((p0, c1, c2, p3)) / step)) + 1)
    return [_cubic_point(p0, c1, c2, p3, k / (count - 1.0)) for k in range(count)]


def _cumulative(points):
    cum = [0.0]
    for a, b in zip(points, points[1:]):
        cum.append(cum[-1] + _dist(a, b))
    return cum


def _animean():
    import animean_python
    return animean_python


def _scene_model(view_name):
    """The scene model registered under this view's canonical name.

    get_scene() takes NO argument and returns [{"scene", "sceneName", ...}]
    info dicts - repulsion_tool's docstring records shipping this exact
    mistake once already; the review caught it here before it shipped twice.
    """
    import __main__
    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model

    import animean_python
    target = f"{view_name}_paint_view"
    for info in animean_python.get_scene():
        if info.get("sceneName") == target:
            return info["scene"]
    raise KeyError(f"no scene registered as {target!r}")


# --- the stroke under the cursor ---------------------------------------------

def _point_to_polyline(point, polyline):
    best = float("inf")
    for a, b in zip(polyline, polyline[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length_sq = dx * dx + dy * dy
        if length_sq <= 0.0:
            best = min(best, _dist(point, a))
            continue
        t = max(0.0, min(1.0, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / length_sq))
        best = min(best, _dist(point, (a[0] + dx * t, a[1] + dy * t)))
    return best


def _stroke_polylines(stroke):
    result = []
    for polyline in stroke.get("polylines") or []:
        pts = [(float(p["x"]), float(p["y"])) for p in polyline]
        if len(pts) >= 2:
            result.append(pts)
    if not result:
        raw = [(float(p["x"]), float(p["y"])) for p in (stroke.get("raw_points") or [])]
        if len(raw) >= 2:
            result.append(raw)
    return result


def _find_stroke(scene, frame, pos, zoom):
    """Topmost editable stroke near `pos`: (layer_index, stroke_index) or None."""
    structure = scene.get_structure()
    zoom = zoom or 1.0
    for layer in structure["layers"]:
        if not layer["visible"] or layer.get("locked") or layer["type"] == "fill":
            continue
        cell = scene.cell_to_dict(layer["index"], frame, True, POLY_STEP)
        best = None
        for index, stroke in enumerate(cell["image"]["strokes"]):
            # Two spaces in one sum, deliberately: half the stroke's own width
            # is CANVAS (you grabbed the ink), plus 6 SCREEN px of slop (your
            # aim is only so good), converted through pyfile/viewscale.py.
            tolerance = (float(stroke.get("width", 3.0)) * 0.5
                         + viewscale.to_canvas_length(6.0, zoom))
            for polyline in _stroke_polylines(stroke):
                gap = _point_to_polyline(pos, polyline)
                if gap <= tolerance and (best is None or gap < best[0]):
                    best = (gap, index)
        if best is not None:
            return layer["index"], best[1]
    return None


# --- session -----------------------------------------------------------------

def _fetch(scene, frame, layer, index):
    """The stroke's editable geometry, or None if it no longer exists."""
    cell = scene.cell_to_dict(layer, frame, False, POLY_STEP)
    strokes = cell["image"]["strokes"]
    if index < 0 or index >= len(strokes):
        return None
    stroke = strokes[index]
    color = stroke.get("color") or {}
    return {
        "commands": list(stroke.get("commands") or []),
        "points": [(float(p["x"]), float(p["y"])) for p in (stroke.get("raw_points") or [])],
        "color": (int(color.get("r", 0)), int(color.get("g", 0)),
                  int(color.get("b", 0)), int(color.get("a", 255))),
        "width": float(stroke.get("width", 3.0)),
        "pen_style": int(stroke.get("pen_style", 1)),
        "property": stroke.get("property") or "",
        "id": int(stroke.get("id", 0)),
    }


def _command_point(entry):
    return (float(entry["x"]), float(entry["y"]))


def _elements(commands):
    """[(kind, [pt, ...])]: 'move' [p], 'line' [p], 'cubic' [c1, c2, p]."""
    out = []
    current = None
    for command in commands:
        kind = command.get("type")
        if kind == "move":
            current = _command_point(command["to"])
            out.append(("move", [current]))
        elif kind == "line":
            current = _command_point(command["to"])
            out.append(("line", [current]))
        elif kind == "quad":
            # Elevate (shared wheel pyfile/bezier.py) so editing really does
            # see one uniform cubic shape - storing the quad raw made the
            # cubic-chain fast path reject the stroke and re-fit it.
            ctrl = _command_point(command["control"])
            end = _command_point(command["to"])
            start = current if current is not None else (
                _command_point(command["from"]) if "from" in command else ctrl)
            _, c1, c2, _ = bezier.quad_cubic(start, ctrl, end)
            out.append(("cubic", [c1, c2, end]))
            current = end
        elif kind == "cubic":
            current = _command_point(command["to"])
            out.append(("cubic", [_command_point(command["control1"]),
                                  _command_point(command["control2"]),
                                  current]))
    return out


def _elements_to_commands(elements):
    commands = []
    current = None
    for kind, pts in elements:
        if kind == "move":
            commands.append({"type": "move", "to": {"x": pts[0][0], "y": pts[0][1]}})
            current = pts[0]
        elif kind == "line":
            commands.append({"type": "line",
                             "from": {"x": current[0], "y": current[1]},
                             "to": {"x": pts[0][0], "y": pts[0][1]}})
            current = pts[0]
        elif kind == "cubic":
            commands.append({"type": "cubic",
                             "from": {"x": current[0], "y": current[1]},
                             "control1": {"x": pts[0][0], "y": pts[0][1]},
                             "control2": {"x": pts[1][0], "y": pts[1][1]},
                             "to": {"x": pts[2][0], "y": pts[2][1]}})
            current = pts[2]
    return commands


def _flatten_elements(elements, step=POLY_STEP):
    points = []
    current = None
    for kind, pts in elements:
        if kind == "move":
            current = pts[0]
            if not points:
                points.append(current)
        elif kind == "line":
            if current is not None:
                length = _dist(current, pts[0])
                count = max(1, int(math.ceil(length / step)))
                for k in range(1, count + 1):
                    t = k / count
                    points.append((current[0] + (pts[0][0] - current[0]) * t,
                                   current[1] + (pts[0][1] - current[1]) * t))
            current = pts[0]
        elif kind == "cubic":
            if current is not None:
                points.extend(_sample_cubic(current, pts[0], pts[1], pts[2], step)[1:])
            current = pts[2]
    return points


# --- handle construction -----------------------------------------------------

def _debug_handles(geometry):
    """EVERY VERTEX of the stored topology - and only vertices.

    Debug is the "show me the points this stroke is really made of" view: one
    handle per on-curve point, no Bezier control handles and no tangent arms
    (user report: with the controls in, Debug looked no different from
    Artist, since our own strokes ARE sparse cubic chains). Dragging one
    still edits that exact stored point, arms and all - see _drag_debug.
    """
    handles = []
    arms = []
    elements = _elements(geometry["commands"])
    if elements:
        for i, (kind, pts) in enumerate(elements):
            end_slot = {"move": 0, "line": 0, "cubic": 2}.get(kind)
            if end_slot is None:
                continue
            point = pts[end_slot]
            handles.append({"id": f"e{i}:p", "x": point[0], "y": point[1],
                            "shape": SHAPE_ANCHOR, "color": ANCHOR_COLOR})
        return handles, arms

    for k, point in enumerate(geometry["points"]):
        handles.append({"id": f"v{k}", "x": point[0], "y": point[1],
                        "shape": SHAPE_ANCHOR, "color": ANCHOR_COLOR})
    return handles, arms


def _dominant_indices(points, zoom):
    """Key vertices of a dense polyline, filtered perceptually.

    Candidates are the local maxima of the windowed turning angle (how hard
    the curve bends there), ranked by that salience; both endpoints are
    always key. The perceptual filter then walks the ranking and keeps a
    candidate only if it sits at least one CSF period - in SCREEN pixels,
    mapped through the zoom - of ARC away from everything already kept.
    """
    count = len(points)
    if count < 2:
        return list(range(count))
    cum = _cumulative(points)
    total = cum[-1]
    if total <= 1e-9:
        return [0, count - 1]

    window = max(2.0 * POLY_STEP, 0.02 * total)

    def tangent(index, direction):
        base = points[index]
        arc = cum[index]
        j = index
        while 0 <= j + direction < count and abs(cum[j + direction] - arc) < window:
            j += direction
        j = max(0, min(count - 1, j + direction if 0 <= j + direction < count else j))
        other = points[j]
        vec = (other[0] - base[0], other[1] - base[1]) if direction > 0 else (base[0] - other[0], base[1] - other[1])
        norm = math.hypot(*vec)
        return (vec[0] / norm, vec[1] / norm) if norm > 1e-12 else (0.0, 0.0)

    salience = [0.0] * count
    for i in range(1, count - 1):
        t_in = tangent(i, -1)
        t_out = tangent(i, 1)
        dot = max(-1.0, min(1.0, t_in[0] * t_out[0] + t_in[1] * t_out[1]))
        salience[i] = math.degrees(math.acos(dot))

    candidates = []
    for i in range(1, count - 1):
        if salience[i] < 3.0:   # noise floor: straighter than 3 degrees is not a feature
            continue
        left = salience[i - 1] if i - 1 >= 1 else 0.0
        right = salience[i + 1] if i + 1 <= count - 2 else 0.0
        if salience[i] >= left and salience[i] >= right:
            candidates.append((salience[i], i))
    candidates.sort(reverse=True)

    # A CSF period is a SCREEN length; viewscale is the shared home of that
    # conversion (pyfile/viewscale.py, mirroring algorithm/viewscale.h).
    min_gap = viewscale.to_canvas_length(perceptual_min_separation_px(), zoom)
    kept = [0, count - 1]
    for _, i in candidates:
        if all(abs(cum[i] - cum[j]) >= min_gap for j in kept):
            kept.append(i)
    kept.sort()
    return kept


# --- the artist-mode Bezier chain -------------------------------------------
#
# Artist mode is not a displacement field over the flattening any more: it
# FITS a cubic chain whose anchors are exactly the perceptual key points, and
# every edit operates on that chain. That is what makes the three reported
# failures impossible by construction: the curve passes THROUGH a dragged
# anchor (it is an interpolation point, so it cannot lag the mouse), an edit
# rewrites the stroke as the same sparse chain (no surprise vertices, and a
# raw polyline is auto-smoothed into a chain on its first edit), and the
# anchors carry visible tangent handles like any Bezier editor.

# An anchor whose sides meet harder than this is a CORNER: its two handles
# are independent, so at a right angle they form the right angle.
CORNER_DEG = 35.0
# A fitted handle may lean at most this far off its chord, and may not be
# longer than the chord. A cubic that cannot satisfy that is split - one
# segment therefore never turns much more than ~90 degrees.
MAX_HANDLE_ANGLE_DEG = 45.0
FIT_TOL_PX = 0.75
MAX_SPLIT_DEPTH = 8


def _norm(v):
    length = math.hypot(v[0], v[1])
    return (v[0] / length, v[1] / length) if length > 1e-12 else (0.0, 0.0)


def _one_sided_tangents(dense, cum, index, window):
    """Unit tangents arriving at and leaving dense[index], chord-windowed."""
    count = len(dense)

    def reach(direction):
        j = index
        while 0 <= j + direction < count and abs(cum[j + direction] - cum[index]) < window:
            j += direction
        j = max(0, min(count - 1, j if j != index else index + direction))
        return dense[j]

    before = reach(-1)
    after = reach(1)
    t_in = _norm((dense[index][0] - before[0], dense[index][1] - before[1]))
    t_out = _norm((after[0] - dense[index][0], after[1] - dense[index][1]))
    return t_in, t_out


def _fit_span(dense, cum, i0, i1, tan_out, tan_in):
    """One cubic over dense[i0..i1] with FIXED end tangent directions.

    Schneider's least-squares (Graphics Gems "FitCurve"): with c1 = p0 +
    alpha*tan_out and c2 = p3 + beta*tan_in, the squared distance to the
    samples is quadratic in (alpha, beta) - a 2x2 solve. Returns
    (cubic, max_error, worst_index).
    """
    p0, p3 = dense[i0], dense[i1]
    arc0, arc1 = cum[i0], cum[i1]
    span = max(arc1 - arc0, 1e-9)
    chord = _dist(p0, p3)

    c11 = c12 = c22 = x1 = x2 = 0.0
    for k in range(i0, i1 + 1):
        t = (cum[k] - arc0) / span
        s = 1.0 - t
        b1 = 3.0 * s * s * t
        b2 = 3.0 * s * t * t
        base = (s * s * s * p0[0] + b1 * p0[0] + b2 * p3[0] + t * t * t * p3[0],
                s * s * s * p0[1] + b1 * p0[1] + b2 * p3[1] + t * t * t * p3[1])
        a1 = (tan_out[0] * b1, tan_out[1] * b1)
        a2 = (tan_in[0] * b2, tan_in[1] * b2)
        rhs = (dense[k][0] - base[0], dense[k][1] - base[1])
        c11 += a1[0] * a1[0] + a1[1] * a1[1]
        c12 += a1[0] * a2[0] + a1[1] * a2[1]
        c22 += a2[0] * a2[0] + a2[1] * a2[1]
        x1 += a1[0] * rhs[0] + a1[1] * rhs[1]
        x2 += a2[0] * rhs[0] + a2[1] * rhs[1]

    det = c11 * c22 - c12 * c12
    if abs(det) > 1e-12:
        alpha = (x1 * c22 - x2 * c12) / det
        beta = (c11 * x2 - c12 * x1) / det
    else:
        alpha = beta = chord / 3.0
    if alpha <= 1e-6 or beta <= 1e-6:
        alpha = beta = max(chord / 3.0, 1e-6)

    cubic = [p0,
             (p0[0] + tan_out[0] * alpha, p0[1] + tan_out[1] * alpha),
             (p3[0] + tan_in[0] * beta, p3[1] + tan_in[1] * beta),
             p3]

    worst = i0
    error = 0.0
    for k in range(i0, i1 + 1):
        t = (cum[k] - arc0) / span
        gap = _dist(dense[k], _cubic_point(*cubic, t))
        if gap > error:
            error, worst = gap, k
    return cubic, error, worst


def _handle_constraints_ok(cubic):
    """The fitted-handle rules: within 45 degrees of the chord, no longer
    than the chord."""
    p0, c1, c2, p3 = cubic
    chord = (p3[0] - p0[0], p3[1] - p0[1])
    chord_len = math.hypot(*chord)
    if chord_len < 1e-9:
        return False
    chord_dir = (chord[0] / chord_len, chord[1] / chord_len)
    limit = math.cos(math.radians(MAX_HANDLE_ANGLE_DEG))
    for handle, sign in (((c1[0] - p0[0], c1[1] - p0[1]), 1.0),
                         ((p3[0] - c2[0], p3[1] - c2[1]), 1.0)):
        length = math.hypot(*handle)
        if length < 1e-9:
            continue
        if length > chord_len:
            return False
        cos_angle = (handle[0] * chord_dir[0] + handle[1] * chord_dir[1]) * sign / length
        if cos_angle < limit:
            return False
    return True


def _fit_chain(dense, kept):
    """Cubic chain anchored at the kept indices, splitting where one cubic
    cannot both stay within FIT_TOL_PX and keep its handles legal.

    Returns (anchors, cubics): anchors = [{"pos", "corner"}], one cubic per
    consecutive anchor pair.
    """
    cum = _cumulative(dense)
    total = cum[-1]
    window = max(2.0 * POLY_STEP, 0.02 * total)

    tangents = {}
    corner = {}
    for i in kept:
        t_in, t_out = _one_sided_tangents(dense, cum, i, window)
        dot = max(-1.0, min(1.0, t_in[0] * t_out[0] + t_in[1] * t_out[1]))
        is_corner = math.degrees(math.acos(dot)) > CORNER_DEG and 0 < i < len(dense) - 1
        corner[i] = is_corner
        if is_corner:
            tangents[i] = (t_in, t_out)
        else:
            merged = _norm((t_in[0] + t_out[0], t_in[1] + t_out[1]))
            if merged == (0.0, 0.0):
                merged = t_out if t_out != (0.0, 0.0) else t_in
            tangents[i] = (merged, merged)

    anchor_indices = []
    cubics = []

    def tangent_out(i):
        return tangents[i][1] if i in tangents else _one_sided_tangents(dense, cum, i, window)[1]

    def tangent_in(i):
        # incoming handle points BACK along the curve from the end anchor
        t = tangents[i][0] if i in tangents else _one_sided_tangents(dense, cum, i, window)[0]
        return (-t[0], -t[1])

    def fit(i0, i1, depth):
        if i1 - i0 < 2 or depth >= MAX_SPLIT_DEPTH \
                or cum[i1] - cum[i0] < 2.0 * POLY_STEP:
            cubic, _, _ = _fit_span(dense, cum, i0, i1, tangent_out(i0), tangent_in(i1))
            cubics.append(cubic)
            anchor_indices.append(i1)
            return
        cubic, error, worst = _fit_span(dense, cum, i0, i1, tangent_out(i0), tangent_in(i1))
        if error <= FIT_TOL_PX and _handle_constraints_ok(cubic):
            cubics.append(cubic)
            anchor_indices.append(i1)
            return
        # Split where the fit is worst (mid-span for a pure constraint
        # violation, whose worst sample can sit at an end).
        split = worst if i0 + 1 < worst < i1 - 1 else (i0 + i1) // 2
        if split not in tangents:
            t_in, t_out = _one_sided_tangents(dense, cum, split, window)
            merged = _norm((t_in[0] + t_out[0], t_in[1] + t_out[1]))
            tangents[split] = (merged, merged)
            corner[split] = False
        fit(i0, split, depth + 1)
        fit(split, i1, depth + 1)

    anchor_indices.append(kept[0])
    for a, b in zip(kept, kept[1:]):
        fit(a, b, 0)

    anchors = [{"pos": dense[i], "corner": corner.get(i, False)}
               for i in anchor_indices]
    return anchors, cubics


def _split_cubic(cubic, t=0.5):
    """de Casteljau split, from the shared wheel pyfile/bezier.py."""
    return bezier.split_cubic(cubic, t)


def _chain_from_elements(elements):
    """Read a chain straight back from a stroke that IS one (lines + cubics).

    Returns (anchors, cubics) or (None, None) when the elements are anything
    else - then the caller falls back to detect-and-fit. Corners are
    re-derived from the handle geometry: an anchor whose arms meet harder
    than CORNER_DEG stays a corner.

    LINE elements are elevated on the spot (shared wheel pyfile/bezier.py):
    the chord IS that cubic, so the chain stays the stored geometry
    verbatim. Rejecting mixed line+cubic strokes - exactly what the live
    fitter emits for straight runs - used to force the detect-and-fit
    fallback, whose refit no longer matched the drawn curve: the first drag
    then visibly reshaped the WHOLE stroke.
    """
    if not elements or elements[0][0] != "move" \
            or any(kind not in ("line", "cubic") for kind, _ in elements[1:]) \
            or len(elements) < 2:
        return None, None
    cubics = []
    current = elements[0][1][0]
    for kind, pts in elements[1:]:
        if kind == "line":
            cubics.append(bezier.line_cubic(current, pts[0]))
            current = pts[0]
        else:
            cubics.append([current, pts[0], pts[1], pts[2]])
            current = pts[2]
    anchors = [{"pos": cubics[0][0], "corner": False}]
    for k in range(1, len(cubics)):
        into = _norm((cubics[k - 1][3][0] - cubics[k - 1][2][0],
                      cubics[k - 1][3][1] - cubics[k - 1][2][1]))
        out = _norm((cubics[k][1][0] - cubics[k][0][0],
                     cubics[k][1][1] - cubics[k][0][1]))
        dot = max(-1.0, min(1.0, into[0] * out[0] + into[1] * out[1]))
        corner = math.degrees(math.acos(dot)) > CORNER_DEG
        anchors.append({"pos": cubics[k][0], "corner": corner})
    anchors.append({"pos": cubics[-1][3], "corner": False})
    return anchors, cubics


def _chain_handles(chain):
    """Anchors as diamonds, Bezier handle tips as circles, arms as overlay
    lines. Corner anchors keep both arms independent - at a right angle the
    arms ARE the right angle."""
    handles = []
    arms = []
    anchors = chain["anchors"]
    cubics = chain["cubics"]
    for k, anchor in enumerate(anchors):
        pos = anchor["pos"]
        handles.append({"id": f"a{k}", "x": pos[0], "y": pos[1],
                        "shape": SHAPE_PSEUDO, "color": PSEUDO_COLOR})
        if k < len(cubics):
            tip = cubics[k][1]
            handles.append({"id": f"h{k}:out", "x": tip[0], "y": tip[1],
                            "shape": SHAPE_CONTROL, "color": CONTROL_COLOR})
            arms.append([pos, tip])
        if k > 0:
            tip = cubics[k - 1][2]
            handles.append({"id": f"h{k}:in", "x": tip[0], "y": tip[1],
                            "shape": SHAPE_CONTROL, "color": CONTROL_COLOR})
            arms.append([pos, tip])
    return handles, arms


def _chain_commands(chain):
    cubics = chain["cubics"]
    if not cubics:
        return []
    start = cubics[0][0]
    commands = [{"type": "move", "to": {"x": start[0], "y": start[1]}}]
    current = start
    for p0, c1, c2, p3 in cubics:
        commands.append({"type": "cubic",
                         "from": {"x": current[0], "y": current[1]},
                         "control1": {"x": c1[0], "y": c1[1]},
                         "control2": {"x": c2[0], "y": c2[1]},
                         "to": {"x": p3[0], "y": p3[1]}})
        current = p3
    return commands


def _chain_flat(chain, step=POLY_STEP):
    points = []
    for cubic in chain["cubics"]:
        sampled = _sample_cubic(*cubic, step)
        points.extend(sampled if not points else sampled[1:])
    return points


def _dirty_segments(chain, chain0):
    """Indices of the segments a drag actually moved, by diffing the press
    snapshot. None (= examine everything) if the counts ever disagree."""
    if not chain0 or len(chain["cubics"]) != len(chain0["cubics"]):
        return None
    return {i for i, (c, c0) in enumerate(zip(chain["cubics"], chain0["cubics"]))
            if any(_dist(p, q) > 1e-9 for p, q in zip(c, c0))}


def _enforce_chain_constraints(chain, dirty=None):
    """Split any segment whose handles ended up oversized after a drag.

    Only the segments the drag moved are examined (`dirty`; None means all).
    The stroke fitter legally emits harder-bent handles on strongly curved
    spans, so "correcting" untouched stored geometry would grow anchors the
    user never asked for - a fresh stroke gained several on every release.

    de Casteljau keeps the SHAPE bit-identical while inserting an anchor and
    shrinking the handles, so repeated halving always converges to legal
    segments - this is the "automatically split into more vertices" rule.
    """
    changed = False
    for _ in range(MAX_SPLIT_DEPTH):
        for index, cubic in enumerate(chain["cubics"]):
            if dirty is not None and index not in dirty:
                continue
            if _handle_constraints_ok(cubic):
                continue
            if _dist(cubic[0], cubic[3]) < 2.0 * POLY_STEP:
                continue    # too short to split meaningfully
            left, right = _split_cubic(cubic)
            chain["cubics"][index:index + 1] = [left, right]
            chain["anchors"].insert(index + 1, {"pos": left[3], "corner": False})
            if dirty is not None:
                dirty = {i + 1 if i > index else i for i in dirty}
                dirty.add(index + 1)
            changed = True
            break
        else:
            return changed
    return changed


# --- pushing state to the view ----------------------------------------------

def _overlay_items(session, arms):
    """The composed overlay: the picked object's outline, then the arms.

    Through overlay_stack: this tool owns ONE slot of the composed display
    list. Re-merging auto_mapping's items by hand (and calling ui.set_overlay
    directly) predates the stack and would clobber every other owner's
    overlays - guides, creases, previews.
    """
    items = _selection_items(session) if session is not None else []
    for arm in arms:
        items.append({"id": "edit_arm", "points": list(arm), "color": ARM_COLOR,
                      "width": 1.0, "removable": False})
    return items


def _push(view, handles, arms):
    # Remember each pushed handle's position: the press handler re-anchors a
    # grab by geometry, since artist ids do not survive a re-flattening. The
    # arms are remembered too, so a settings change can recompose the overlay
    # without re-running a fit.
    session = _SESSIONS.get(view)
    if session is not None:
        session["handles_by_id"] = {h["id"]: (h["x"], h["y"]) for h in handles}
        session["arms"] = [list(arm) for arm in arms]
    animean = _animean()
    animean.ui.set_edit_handles(view, handles)
    overlay_stack.set_items(view, "edit_tool", _overlay_items(session, arms))


def _clear(view):
    session = _SESSIONS.pop(view, None)
    if session is None:
        return
    animean = _animean()
    animean.ui.set_edit_handles(view, [])
    overlay_stack.set_items(view, "edit_tool", [])


def _rebuild(view, session, zoom):
    """The picked object's affordances in the CURRENT mode.

    The session says WHAT is picked, _STATE says HOW it is shown; keeping the
    mode out of the session is what lets a mode switch redraw the same pick.
    """
    if _STATE["mode"] == "default":
        # Default owns no handles: its only gesture is the body drag the pick
        # claimed, and the outline rides in the overlay.
        _push(view, [], [])
        return
    if session["kind"] == "fill":
        _push(view, _fill_handles(session), [])
        return
    geometry = session["geometry"]
    if _STATE["mode"] == "debug":
        handles, arms = _debug_handles(geometry)
        session["chain"] = None
        _push(view, handles, arms)
        return
    elements = _elements(geometry["commands"])
    # A stroke that already IS a clean cubic chain - one of ours, or a
    # sparse fit - is shown verbatim. This is what keeps undo/redo honest:
    # restoring the pre-drag stroke re-derives the exact pre-drag anchors,
    # instead of re-detecting and re-fitting what a fit just produced (a
    # fit-of-a-fit drifts a little every round trip).
    if elements:
        anchors, cubics = _chain_from_elements(elements)
        if anchors is not None and len(anchors) <= 200:
            session["chain"] = {"anchors": anchors, "cubics": cubics}
            handles, arms = _chain_handles(session["chain"])
            _push(view, handles, arms)
            return
    dense = _flatten_elements(elements) if elements else list(geometry["points"])
    if len(dense) < 2:
        session["chain"] = None
        _push(view, [], [])
        return
    kept = _dominant_indices(dense, zoom)
    anchors, cubics = _fit_chain(dense, kept)
    session["chain"] = {"anchors": anchors, "cubics": cubics}
    handles, arms = _chain_handles(session["chain"])
    _push(view, handles, arms)


# --- applying an edit --------------------------------------------------------

def _polyline_length(points):
    return sum(_dist(a, b) for a, b in zip(points, points[1:]))


def _replace_stroke(view, session):
    geometry = session["geometry"]
    scene = _scene_model(view)
    animean = _animean()
    if _STATE["mode"] == "artist" and session.get("chain"):
        # The chain IS the stroke now: sparse anchors, legal handles, cubic
        # commands. A former raw polyline comes out auto-smoothed - editing
        # never multiplies vertices.
        commands = _chain_commands(session["chain"])
        flat = _chain_flat(session["chain"])
        if len(flat) < 2 or _polyline_length(flat) < 0.01:
            return False
        piece = animean.vectorlogic.make_stroke_object_from_path(
            commands, flat, geometry["color"], geometry["width"], geometry["id"])
    elif session.get("elements") is not None:
        commands = _elements_to_commands(session["elements"])
        flat = _flatten_elements(session["elements"])
        # A drag that collapses the stroke onto itself must not DELETE it:
        # replace_stroke_with_pieces drops a degenerate piece, and the stroke
        # silently vanished. Freeze at the last valid shape instead - the
        # release re-fetch then restores truth.
        if len(flat) < 2 or _polyline_length(flat) < 0.01:
            return False
        piece = animean.vectorlogic.make_stroke_object_from_path(
            commands, flat, geometry["color"], geometry["width"], geometry["id"])
    else:
        points = session["points"]
        if len(points) < 2 or _polyline_length(points) < 0.01:
            return False
        piece = animean.vectorlogic.make_stroke_object(
            points, geometry["color"], geometry["width"], geometry["id"], False, False)
    piece.property = geometry["property"]
    piece.pen_style = geometry["pen_style"]
    image = scene.image_at(session["frame"], session["layer"], True)
    if image is None:
        return False
    return image.replace_stroke_with_pieces(session["index"], [piece]) > 0


def _drag_debug(session, handle_id, pos):
    elements = session["elements"]
    kind_of = {"p": None, "c1": 0, "c2": 1}
    part, _, role = handle_id.partition(":")
    index = int(part[1:]) if part[1:].isdigit() else -1
    if part.startswith("v"):
        k = int(part[1:])
        points = session["points"]
        if 0 <= k < len(points):
            points[k] = pos
        return
    if index < 0 or index >= len(elements):
        return
    kind, pts = elements[index]
    if role == "p":
        end_slot = {"move": 0, "line": 0, "cubic": 2}[kind]
        old = pts[end_slot]
        delta = (pos[0] - old[0], pos[1] - old[1])
        pts[end_slot] = pos
        # An anchor drags its tangent arms with it, like every Bezier editor:
        # the incoming c2 of this element and the outgoing c1 of the next.
        if kind == "cubic":
            pts[1] = (pts[1][0] + delta[0], pts[1][1] + delta[1])
        if index + 1 < len(elements):
            next_kind, next_pts = elements[index + 1]
            if next_kind == "cubic":
                next_pts[0] = (next_pts[0][0] + delta[0], next_pts[0][1] + delta[1])
    elif role == "c1" and kind == "cubic":
        pts[0] = pos
    elif role == "c2" and kind == "cubic":
        pts[1] = pos


def _drag_chain(session, target, pos):
    """Apply a drag to the artist chain.

    `target` is ("anchor", k) or ("tip", k, "in"/"out"), resolved at press
    time by geometry. An anchor drag is an INTERPOLATION move: the anchor and
    both its handle tips translate with the mouse exactly, so the curve
    passes through the cursor - the old displacement-field approach steered
    only control points and the curve lagged behind on any large drag. A tip
    drag re-aims that handle; on a smooth anchor the opposite arm mirrors the
    direction (keeping its own length), on a corner both arms stay free.
    """
    chain = session["chain"]
    anchors = chain["anchors"]
    cubics = chain["cubics"]
    kind = target[0]
    k = target[1]
    if kind == "anchor":
        if not 0 <= k < len(anchors):
            return
        old = session["chain0"]["anchors"][k]["pos"]
        delta = (pos[0] - old[0], pos[1] - old[1])
        anchors[k]["pos"] = pos
        if k < len(cubics):
            base = session["chain0"]["cubics"][k]
            cubics[k][0] = pos
            cubics[k][1] = (base[1][0] + delta[0], base[1][1] + delta[1])
        if k > 0:
            base = session["chain0"]["cubics"][k - 1]
            cubics[k - 1][3] = pos
            cubics[k - 1][2] = (base[2][0] + delta[0], base[2][1] + delta[1])
        return
    role = target[2]
    if role == "out":
        if not 0 <= k < len(cubics):
            return
        cubics[k][1] = pos
        if not anchors[k]["corner"] and k > 0:
            anchor = anchors[k]["pos"]
            direction = _norm((anchor[0] - pos[0], anchor[1] - pos[1]))
            other = cubics[k - 1][2]
            length = _dist(anchor, other)
            cubics[k - 1][2] = (anchor[0] + direction[0] * length,
                                anchor[1] + direction[1] * length)
    elif role == "in":
        if not 1 <= k <= len(cubics):
            return
        cubics[k - 1][2] = pos
        if not anchors[k]["corner"] and k < len(cubics):
            anchor = anchors[k]["pos"]
            direction = _norm((anchor[0] - pos[0], anchor[1] - pos[1]))
            other = cubics[k][1]
            length = _dist(anchor, other)
            cubics[k][1] = (anchor[0] + direction[0] * length,
                            anchor[1] + direction[1] * length)


# --- picked objects: outlines, fills, and the default drag ---------------------
# Default is the Arrow's resting mode. A click OUTLINES whatever it lands on -
# a stroke or a filled region - and dragging inside that outline translates it.
# No handles, no geometry rewrite: this is the "grab the thing and move it"
# mode every drawing app opens with, and it is what makes the mapping guides
# (H/V axes, additional lines) draggable without arming a special tool.
# The outline itself belongs to the PICK, not to the mode, so Artist and Debug
# keep it too, under their handles.

def _outline_color():
    return tuple(_OUTLINE["outline_color"])


def _find_fill(scene, frame, pos):
    """Topmost fill region containing `pos`: (layer, index) or None."""
    structure = scene.get_structure()
    for layer in structure["layers"]:
        if not layer["visible"] or layer.get("locked"):
            continue
        try:
            image = scene.image_at(frame, layer["index"], False, "vector")
        except Exception:
            image = None
        if image is None:
            continue
        try:
            regions = list(image.fill_regions_info())
        except Exception:
            continue
        for info in reversed(regions):
            index = int(info.get("index", -1))
            if index < 0:
                continue
            try:
                if image.fill_region_contains(index, {"x": pos[0], "y": pos[1]}):
                    return layer["index"], index
            except Exception:
                continue
    return None


def _fill_outline(commands):
    """The fill's boundary as polylines. A fill dict carries only its path
    COMMANDS (fillRegionToDict has no polylines key), so the outline is
    flattened here - through the same element flattening the strokes use."""
    rings = []
    ring = []
    for element in _elements(commands):
        kind, pts = element
        if kind == "move":
            if len(ring) >= 3:
                rings.append(ring)
            ring = [pts[0]]
        elif kind == "line":
            ring.append(pts[0])
        elif kind == "cubic":
            start = ring[-1] if ring else pts[0]
            ring.extend(_sample_cubic(start, pts[0], pts[1], pts[2])[1:])
    if len(ring) >= 3:
        rings.append(ring)
    return rings


def _fill_geometry(scene, frame, layer, index):
    cell = scene.cell_to_dict(layer, frame, True, POLY_STEP)
    fills = cell["image"].get("fills") or []
    if index < 0 or index >= len(fills):
        return None
    fill = fills[index]
    seed = fill.get("seed") or {}
    commands = list(fill.get("commands") or [])
    return {
        "commands": commands,
        "polylines": _fill_outline(commands),
        "seed": (float(seed.get("x", 0.0)), float(seed.get("y", 0.0))),
        # The region's identity: an undo can renumber the fills, and the index
        # alone would then point at a DIFFERENT region.
        "id": int(fill.get("id", 0)),
    }


# --- fill regions as editable boundaries -------------------------------------
# Artist and Debug show a picked region the same way: one handle per stored
# boundary vertex. Control points are not handles of their own - a region's
# cubics come from the topology it was flooded into, so what an artist wants
# to move is where the boundary PASSES; the controls ride along with the
# vertex they belong to, exactly as the debug stroke drag carries its arms.

def _copy_elements(elements):
    return [(kind, list(pts)) for kind, pts in elements]


def _fill_vertex_refs(elements):
    """Boundary vertices as [position, [(element_index, slot), ...]].

    A ring whose last element lands back on its 'move' point is ONE vertex
    with two references: dragging those apart would tear the ring open.
    """
    refs = []
    ring_start = None
    for index, (kind, pts) in enumerate(elements):
        if kind == "move":
            refs.append([pts[0], [(index, 0)]])
            ring_start = len(refs) - 1
            continue
        slot = 2 if kind == "cubic" else 0
        point = pts[slot]
        if ring_start is not None and _dist(point, refs[ring_start][0]) <= 1e-6:
            refs[ring_start][1].append((index, slot))
            continue
        refs.append([point, [(index, slot)]])
    return refs


def _fill_handles(session):
    """One square handle per stored boundary vertex."""
    elements = _elements(session["geometry"]["commands"])
    session["elements"] = elements
    return [{"id": f"f{k}", "x": point[0], "y": point[1],
             "shape": SHAPE_ANCHOR, "color": ANCHOR_COLOR}
            for k, (point, _slots) in enumerate(_fill_vertex_refs(elements))]


def _fill_vertex_index(handle_id, refs, handles_by_id):
    """Which boundary vertex the press grabbed - resolved by GEOMETRY, like
    the artist chain does it, because the re-read may have renumbered them."""
    grabbed_at = handles_by_id.get(handle_id)
    if grabbed_at is None or not refs:
        return None
    return min(range(len(refs)), key=lambda k: _dist(refs[k][0], grabbed_at))


def _drag_fill(elements, refs, index, pos):
    """Put boundary vertex `index` at `pos`, its control points in tow."""
    if refs is None or not 0 <= index < len(refs):
        return
    point, slots = refs[index]
    delta = (pos[0] - point[0], pos[1] - point[1])
    for element_index, slot in slots:
        kind, pts = elements[element_index]
        pts[slot] = pos
        if kind == "cubic":
            pts[1] = (pts[1][0] + delta[0], pts[1][1] + delta[1])
        following = element_index + 1
        if following < len(elements):
            next_kind, next_pts = elements[following]
            if next_kind == "cubic":
                next_pts[0] = (next_pts[0][0] + delta[0], next_pts[0][1] + delta[1])


def _replace_fill(view, session, commands):
    """Rewrite the picked region's boundary. False = nothing was written."""
    if commands == session["geometry"]["commands"]:
        return False    # no-op guard: an unmoved drag must not touch the model
    image = _scene_model(view).image_at(session["frame"], session["layer"], True)
    if image is None:
        return False
    # path takes the command SEQUENCE itself (objectToPath iterates it); a
    # {"commands": ...} wrapper raises TypeError.
    if not image.set_fill_region(session["index"], path=commands):
        return False
    session["geometry"]["commands"] = commands
    session["outline"] = _fill_outline(commands)
    return True


def _shift_commands(commands, delta):
    """Path commands translated by `delta` - every point they carry."""
    dx, dy = delta
    out = []
    for command in commands:
        moved = dict(command)
        for key in ("to", "control", "control1", "control2", "from"):
            point = command.get(key)
            if isinstance(point, dict) and "x" in point and "y" in point:
                moved[key] = {"x": float(point["x"]) + dx,
                              "y": float(point["y"]) + dy}
        out.append(moved)
    return out


def _selection_items(session):
    """The highlight: the object's own outline, drawn over itself."""
    color = _outline_color()
    width = float(_OUTLINE.get("outline_width", 2.0))
    closed = session["kind"] == "fill"
    items = []
    for k, polyline in enumerate(session.get("outline") or []):
        if len(polyline) < (3 if closed else 2):
            continue
        item = {"id": f"edit_outline{k}", "points": list(polyline),
                "color": color, "width": width, "removable": False}
        if closed:
            item["closed"] = True
        items.append(item)
    return items


def _pick(view, pos, zoom, message):
    """Pick whatever is under the cursor and show it in the current mode.

    Hit-test priority is strokes first, fills second, in EVERY mode: the ink
    is what the cursor is aimed at, and a region always sits under some of it.
    """
    scene = _scene_model(view)
    frame = max(scene.current_frame(), 0)
    session = None
    found = _find_stroke(scene, frame, pos, zoom)
    if found is not None:
        layer, index = found
        geometry = _fetch(scene, frame, layer, index)
        if geometry is not None:
            cell = scene.cell_to_dict(layer, frame, True, POLY_STEP)
            stroke = cell["image"]["strokes"][index]
            session = {
                "kind": "stroke", "frame": frame,
                "layer": layer, "index": index, "geometry": geometry,
                "elements": _elements(geometry["commands"]) if geometry["commands"] else None,
                "points": list(geometry["points"]),
                "outline": _stroke_polylines(stroke),
            }
    if session is None:
        fill = _find_fill(scene, frame, pos)
        if fill is not None:
            layer, index = fill
            geometry = _fill_geometry(scene, frame, layer, index)
            if geometry is not None:
                session = {
                    "kind": "fill", "frame": frame,
                    "layer": layer, "index": index, "geometry": geometry,
                    "elements": _elements(geometry["commands"]),
                    "outline": geometry["polylines"],
                }
    if session is None:
        _clear(view)
        return
    session.update({"press_pos": pos, "applied": (0.0, 0.0),
                    "moved": False, "changed": False,
                    "chain": None, "chain0": None, "drag_target": None,
                    "fill_refs": None, "vertex": None})
    _SESSIONS[view] = session
    # Re-registering moves this hook to the END of the dispatch order, AFTER
    # auto_mapping's historyrestore hook (which re-pushes the overlay and
    # would otherwise clobber the tangent arms we re-draw on restore).
    python_hooks.set_hook(_history_restored, historyrestore=True)
    _rebuild(view, session, zoom)
    if _STATE["mode"] == "default":
        # Claim the gesture: this press becomes a drag, so click-and-move grabs
        # the object in one motion, the way every drawing app does it.
        message["grab"] = "default:body"
    print(f"[edit_tool] editing {session['kind']} {session['index']} on layer "
          f"{session['layer']} ({_STATE['mode']} mode; min spacing "
          f"{perceptual_min_separation_px():.1f}px on screen)")


def _refresh_session(view, session):
    """Re-read the picked object from the model. False = it is not there.

    Identity, not just presence: an undo can renumber strokes and regions, and
    the index alone would then describe an innocent bystander.
    """
    scene = _scene_model(view)
    if session["kind"] == "fill":
        geometry = _fill_geometry(scene, session["frame"], session["layer"],
                                  session["index"])
        if geometry is None or geometry["id"] != session["geometry"]["id"]:
            return False
        session["geometry"] = geometry
        session["elements"] = _elements(geometry["commands"])
        session["outline"] = geometry["polylines"]
        return True
    refreshed = _fetch(scene, session["frame"], session["layer"], session["index"])
    if refreshed is None or refreshed["id"] != session["geometry"]["id"] \
            or refreshed["property"] != session["geometry"]["property"]:
        return False
    cell = scene.cell_to_dict(session["layer"], session["frame"], True, POLY_STEP)
    session["geometry"] = refreshed
    session["elements"] = _elements(refreshed["commands"]) if refreshed["commands"] else None
    session["points"] = list(refreshed["points"])
    session["outline"] = _stroke_polylines(cell["image"]["strokes"][session["index"]])
    return True


def _revalidate(view, session):
    """_refresh_session, but a scene that cannot be read counts as gone."""
    try:
        return _refresh_session(view, session)
    except Exception:
        return False


def _translate_default(view, session, delta):
    """Put the outlined object at press_pos + delta.

    `delta` is measured from the PRESS, and what has already been applied is
    subtracted, so a long drag never accumulates its own rounding and a drag
    that returns to the start returns the object with it."""
    animean = _animean()
    scene = _scene_model(view)
    dx = delta[0] - session["applied"][0]
    dy = delta[1] - session["applied"][1]
    if abs(dx) < 1e-9 and abs(dy) < 1e-9:
        return False
    image = scene.image_at(session["frame"], session["layer"], True)
    if image is None:
        return False
    if session["kind"] == "stroke":
        geometry = session["geometry"]
        if session.get("elements") is not None:
            session["elements"] = [(kind, [(p[0] + dx, p[1] + dy) for p in pts])
                                   for kind, pts in session["elements"]]
            commands = _elements_to_commands(session["elements"])
            flat = _flatten_elements(session["elements"])
            if len(flat) < 2:
                return False
            piece = animean.vectorlogic.make_stroke_object_from_path(
                commands, flat, geometry["color"], geometry["width"], geometry["id"])
        else:
            session["points"] = [(p[0] + dx, p[1] + dy) for p in session["points"]]
            if len(session["points"]) < 2:
                return False
            piece = animean.vectorlogic.make_stroke_object(
                session["points"], geometry["color"], geometry["width"],
                geometry["id"], False, False)
        piece.property = geometry["property"]
        piece.pen_style = geometry["pen_style"]
        if image.replace_stroke_with_pieces(session["index"], [piece]) <= 0:
            return False
    else:
        geometry = session["geometry"]
        geometry["commands"] = _shift_commands(geometry["commands"], (dx, dy))
        seed = (geometry["seed"][0] + dx, geometry["seed"][1] + dy)
        geometry["seed"] = seed
        # path takes the command SEQUENCE itself (objectToPath iterates it);
        # a {"commands": ...} wrapper raises TypeError.
        if not image.set_fill_region(session["index"],
                                     path=geometry["commands"],
                                     seed={"x": seed[0], "y": seed[1]}):
            return False
    session["outline"] = [[(p[0] + dx, p[1] + dy) for p in poly]
                          for poly in (session.get("outline") or [])]
    session["applied"] = tuple(delta)
    return True


def _default_drag(view, session, phase, pos):
    delta = (pos[0] - session["press_pos"][0], pos[1] - session["press_pos"][1])
    if abs(delta[0]) > 1e-9 or abs(delta[1]) > 1e-9:
        session["moved"] = True
    if session.get("moved") and _translate_default(view, session, delta):
        session["changed"] = True
    _push(view, [], [])
    _animean().ui.refresh()
    if phase != "release":
        return
    if session.get("changed"):
        try:
            _animean().ui.history_commit(
                "Move Fill" if session["kind"] == "fill" else "Move Stroke", view)
        except Exception:
            pass
    session["moved"] = False
    session["changed"] = False
    session["press_pos"] = pos
    session["applied"] = (0.0, 0.0)
    # Re-read the object as the model now holds it, so the next drag - or a
    # mode switch - starts from truth rather than from this session's
    # arithmetic.
    if not _revalidate(view, session):
        _clear(view)
        return
    _push(view, [], [])


# --- hook handlers -----------------------------------------------------------

def _handle_event(message):
    # Handle events are shared plumbing now (the Connect tool routes its
    # hover/click/buttons through the same pipeline); this module owns only
    # the Arrow's. The cancel that fires on LEAVING Arrow still arrives with
    # base_tool == "arrow" - setTool reports the tool being left.
    if message.get("base_tool") != "arrow":
        return
    view = message.get("view") or "main"
    phase = message.get("phase")
    zoom = float(message.get("zoom") or 1.0)
    _STATE["last_zoom"] = zoom
    pos_dict = message.get("position") or {}
    pos = (float(pos_dict.get("x", 0.0)), float(pos_dict.get("y", 0.0)))

    if phase == "pick":
        if message.get("property"):
            # The Arrow is hosting a SCRIPT tool (Auto Mapping arms it): its
            # canvas gestures belong to that tool's own overlay, not to the
            # artwork underneath. Grabbing a stroke here relocated drawings
            # while the user was aiming at a guide.
            _clear(view)
            return
        _pick(view, pos, zoom, message)
        return
    if phase == "arm":
        # Arming the Arrow shows nothing until something is picked; a stale
        # session from the last time it was armed would outline an object
        # the user is no longer pointing at.
        _clear(view)
        return
    if phase == "cancel":
        # The view left the Arrow tool: its handles are gone, so the debug
        # tangent arms this module drew into the overlay must go with them.
        _clear(view)
        return
    session = _SESSIONS.get(view)
    if session is None:
        return
    if phase == "view":
        _rebuild(view, session, zoom)
        return

    handle_id = message.get("handle") or ""
    if _STATE["mode"] == "default":
        # The default mode owns no handles: its only gesture is the body
        # drag the pick claimed.
        if phase in ("move", "release") and handle_id.startswith("default:"):
            _default_drag(view, session, phase, pos)
        return
    if session["kind"] == "fill":
        _fill_drag(view, session, phase, handle_id, pos, zoom)
        return
    if phase == "press":
        # Between the pick and this press the model may have moved under us -
        # an undo, a redo, another tool. The index alone would then point at a
        # DIFFERENT stroke and the drag would edit an innocent bystander, so
        # re-read and only proceed if it is still the same stroke.
        #
        # Where did the user actually grab? Resolved by GEOMETRY, not by the
        # id: ids are positions in a chain the previous drag may have
        # re-shaped. The pushed handle's position is remembered at push time
        # and re-anchored to the nearest counterpart after re-reading.
        grabbed_at = session.get("handles_by_id", {}).get(handle_id)
        grabbed_kind = "anchor" if handle_id.startswith("a") \
            else "tip" if handle_id.startswith("h") else "any"
        if not _revalidate(view, session):
            _clear(view)
            return
        session["drag_target"] = None
        if _STATE["mode"] == "artist":
            _rebuild(view, session, zoom)   # refresh the chain from truth
            chain = session.get("chain")
            if chain and grabbed_at is not None:
                candidates = []
                for k, anchor in enumerate(chain["anchors"]):
                    candidates.append((("anchor", k), anchor["pos"]))
                for k, cubic in enumerate(chain["cubics"]):
                    candidates.append((("tip", k, "out"), cubic[1]))
                    candidates.append((("tip", k + 1, "in"), cubic[2]))
                if grabbed_kind == "anchor":
                    candidates = [c for c in candidates if c[0][0] == "anchor"]
                elif grabbed_kind == "tip":
                    candidates = [c for c in candidates if c[0][0] == "tip"]
                if candidates:
                    session["drag_target"] = min(
                        candidates, key=lambda c: _dist(c[1], grabbed_at))[0]
            session["chain0"] = {
                "anchors": [dict(a) for a in chain["anchors"]],
                "cubics": [list(map(tuple, c)) for c in chain["cubics"]],
            } if chain else None
        session["press_pos"] = pos
        session["elements0"] = [(k, list(p)) for k, p in session["elements"]] \
            if session["elements"] is not None else None
        session["changed"] = False
        session["moved"] = False
        return
    if phase == "move" or phase == "release":
        press_pos = session.get("press_pos") or pos
        if _dist(pos, press_pos) > 1e-9:
            session["moved"] = True
        # A press-and-release without motion must be a NO-OP: rewriting the
        # stroke through the round trip and committing "Edit Stroke" for a
        # plain click would burn the redo stack for nothing.
        if session.get("moved"):
            if _STATE["mode"] == "debug":
                _drag_debug(session, handle_id, pos)
            elif session.get("chain") and session.get("chain0") \
                    and session.get("drag_target") is not None:
                _drag_chain(session, session["drag_target"], pos)
                if phase == "release":
                    # A drag can leave a segment with oversized handles; the
                    # release splits it (shape-identical) until legal. Only
                    # the segments this drag moved are examined.
                    _enforce_chain_constraints(
                        session["chain"],
                        _dirty_segments(session["chain"], session["chain0"]))
            if _replace_stroke(view, session):
                session["changed"] = True
                # Mirror what _replace_stroke just wrote. The artist chain
                # branch used to record the STALE pre-drag elements here, so
                # a zoom mid-drag rebuilt the chain from the old commands and
                # the stroke visibly snapped back.
                if _STATE["mode"] == "artist" and session.get("chain"):
                    session["geometry"]["commands"] = _chain_commands(session["chain"])
                    flat = _chain_flat(session["chain"])
                elif session["elements"] is not None:
                    session["geometry"]["commands"] = _elements_to_commands(session["elements"])
                    flat = _flatten_elements(session["elements"])
                else:
                    session["geometry"]["commands"] = []
                    session["geometry"]["points"] = list(session["points"])
                    flat = list(session["points"])
                # The highlight is glued to the ink: left at the pre-drag shape
                # it would trail the stroke like a ghost of where it was.
                session["outline"] = [flat] if len(flat) >= 2 else []
        if phase == "release":
            if session.get("changed"):
                try:
                    _animean().ui.history_commit("Edit Stroke", view)
                except Exception:
                    pass
            # Re-read the stroke as the model now holds it, so the next drag
            # starts from truth rather than from this session's arithmetic.
            if not _revalidate(view, session):
                _clear(view)
                return
            refreshed = session["geometry"]
            if session.get("changed") and _STATE["mode"] == "artist":
                # The model now holds exactly the chain we wrote; showing that
                # chain verbatim keeps the anchor set stable instead of
                # re-detecting and re-fitting what we just built.
                elements = _elements(refreshed["commands"])
                anchors, cubics = _chain_from_elements(elements)
                if anchors is not None:
                    session["chain"] = {"anchors": anchors, "cubics": cubics}
                    handles, arms = _chain_handles(session["chain"])
                    _push(view, handles, arms)
                    return
            _rebuild(view, session, zoom)
            return
        if session.get("moved"):
            _rebuild_after_edit(view, session, zoom)


def _rebuild_after_edit(view, session, zoom):
    if _STATE["mode"] == "debug":
        geometry = dict(session["geometry"])
        if session["elements"] is not None:
            geometry["commands"] = _elements_to_commands(session["elements"])
        else:
            geometry["points"] = session["points"]
        handles, arms = _debug_handles(geometry)
        _push(view, handles, arms)
    elif session.get("chain"):
        # Mid-gesture: show the chain as it currently stands; ids stay valid
        # because the chain STRUCTURE is fixed while a drag is in flight.
        handles, arms = _chain_handles(session["chain"])
        _push(view, handles, arms)


def _fill_drag(view, session, phase, handle_id, pos, zoom):
    """One boundary-vertex gesture on a picked fill region.

    The press snapshots the region as the model holds it; every move re-applies
    the drag to THAT snapshot, so a long drag never accumulates its own
    rounding and a drag that returns to the start returns the boundary with it
    - the same discipline the stroke translate uses.
    """
    if phase == "press":
        grabbed = session.get("handles_by_id", {})
        if not _revalidate(view, session):
            _clear(view)
            return
        elements = _elements(session["geometry"]["commands"])
        session["elements"] = elements
        session["elements0"] = _copy_elements(elements)
        session["fill_refs"] = _fill_vertex_refs(elements)
        session["vertex"] = _fill_vertex_index(handle_id, session["fill_refs"], grabbed)
        session["press_pos"] = pos
        session["moved"] = False
        session["changed"] = False
        _rebuild(view, session, zoom)
        return
    if phase not in ("move", "release"):
        return
    press_pos = session.get("press_pos") or pos
    if _dist(pos, press_pos) > 1e-9:
        session["moved"] = True
    # A press-and-release without motion must be a NO-OP: rewriting the path
    # and committing "Edit Fill" for a plain click would burn the redo stack
    # for nothing.
    if session.get("moved") and session.get("vertex") is not None:
        elements = _copy_elements(session["elements0"])
        _drag_fill(elements, session["fill_refs"], session["vertex"], pos)
        session["elements"] = elements
        if _replace_fill(view, session, _elements_to_commands(elements)):
            session["changed"] = True
        _animean().ui.refresh()
    if phase != "release":
        _rebuild(view, session, zoom)
        return
    if session.get("changed"):
        try:
            _animean().ui.history_commit("Edit Fill", view)
        except Exception:
            pass
    session["moved"] = False
    session["changed"] = False
    session["vertex"] = None
    session["fill_refs"] = None
    if not _revalidate(view, session):
        _clear(view)
        return
    _rebuild(view, session, zoom)


def _reset_gesture(session):
    """Drop everything a live drag was carrying; keep WHAT is picked."""
    session["chain"] = None
    session["chain0"] = None
    session["drag_target"] = None
    session["fill_refs"] = None
    session["vertex"] = None
    session["applied"] = (0.0, 0.0)
    session["moved"] = False
    session["changed"] = False


def _history_restored(message):
    """Undo/redo/jump replaced the model: the session and the handles on
    screen still describe the OLD object. Left alone, the handles float at
    stale positions and the next press resolves its grab against geometry
    that no longer exists - which is exactly the reported "handles computed
    wrong after undo". Revalidate against the restored model: same object ->
    rebuild from restored truth; anything else -> clear."""
    view = message.get("view") or "main"
    session = _SESSIONS.get(view)
    if session is None:
        return
    if not _revalidate(view, session):
        _clear(view)
        return
    _reset_gesture(session)
    _rebuild(view, session, _STATE["last_zoom"])


OUTLINE_SETTINGS_NAME = "arrow_outline"
ARROW_MENU_NAME = "arrow_tool"


def _hex_color(rgba):
    r, g, b, a = (list(rgba) + [255, 255, 255, 255])[:4]
    return f"#{a:02x}{r:02x}{g:02x}{b:02x}"


def _parse_hex_color(text):
    text = str(text).strip().lstrip("#")
    try:
        if len(text) == 8:
            a, r, g, b = (int(text[i:i + 2], 16) for i in (0, 2, 4, 6))
            return (r, g, b, a)
        if len(text) == 6:
            r, g, b = (int(text[i:i + 2], 16) for i in (0, 2, 4))
            return (r, g, b, 255)
    except ValueError:
        pass
    return None


def _outline_settings_layout():
    """Menu bar -> Arrow -> Outline Display Settings."""
    return {
        "row_spacing": 8,
        "column_spacing": 6,
        "controls": [
            {"name": "outline_color", "type": "color",
             "title": "Selection outline colour", "hook": "arrow_outline",
             "value": _hex_color(_OUTLINE["outline_color"]),
             "row": 0, "start_column": 0, "end_column": 1},
            {"name": "outline_width", "type": "slider",
             "title": "Selection outline width", "hook": "arrow_outline",
             "min": 1, "max": 12,
             "value": int(round(float(_OUTLINE["outline_width"]))),
             "row": 1, "start_column": 0, "end_column": 2},
        ],
    }


def _arrow_menu_items():
    return [
        {"name": "outline_settings", "title": "Outline Display Settings...",
         "kind": "settings", "settings": OUTLINE_SETTINGS_NAME},
    ]


def _outline_setting_changed(message):
    if message.get("hook") != "arrow_outline":
        return
    name = message.get("name") or ""
    value = message.get("value")
    if name == "outline_color":
        parsed = _parse_hex_color(value)
        if parsed is None:
            return
        _OUTLINE["outline_color"] = parsed
    elif name == "outline_width":
        try:
            _OUTLINE["outline_width"] = max(0.5, float(value))
        except (TypeError, ValueError):
            return
    else:
        return
    for view, session in list(_SESSIONS.items()):
        # Only the overlay changed colour; recompose it from what is already
        # on screen rather than re-running a fit for a slider tick.
        overlay_stack.set_items(view, "edit_tool",
                                _overlay_items(session, session.get("arms") or []))
    print(f"[edit_tool] {name} -> {_OUTLINE[name]}")


def _option_changed(message):
    if message.get("hook") != "edit_mode":
        return
    value = str(message.get("value", "")).lower()
    if value not in MODES or _STATE["mode"] == value:
        return
    _STATE["mode"] = value
    # The pick SURVIVES the switch. The three modes are three views of the same
    # object - an outline, a fitted chain, the stored vertices - so the new
    # mode re-derives its own affordances from what is already picked. Ending
    # the session here (which is what it used to do across the default
    # boundary) made every mode switch cost a re-click, and the selection the
    # user was looking at vanished with it. Only the mid-gesture state is
    # dropped: an option event cannot arrive inside a drag.
    # Option events carry no zoom; the last handle event does.
    zoom = _STATE["last_zoom"]
    for view, session in list(_SESSIONS.items()):
        if not _revalidate(view, session):
            _clear(view)
            continue
        _reset_gesture(session)
        _rebuild(view, session, zoom)
    print(f"[edit_tool] edit mode -> {value}")


python_hooks.set_hook(_handle_event, handle=True)
python_hooks.set_hook(_option_changed, option=True, tool="arrow")
python_hooks.set_hook(_history_restored, historyrestore=True)
# The outline colour is a menu-bar setting, registered at import so it exists
# in a fresh session before the Arrow has ever been armed.
python_hooks.register_menu({
    "name": ARROW_MENU_NAME,
    "title": "Arrow",
    "items": _arrow_menu_items,
})
python_hooks.register_settings(OUTLINE_SETTINGS_NAME, _outline_settings_layout)
python_hooks.set_hook(_outline_setting_changed, option=True)
