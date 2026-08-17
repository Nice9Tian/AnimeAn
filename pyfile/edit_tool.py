"""Arrow-tool edit mode: direct control points, or perceptual pseudo-handles.

Two modes, chosen in the Arrow tool's options:

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

The C++ side is a pure mechanism (render dots, hit-test, report drags as
"handle" events); everything a handle MEANS lives here.
"""
import math

import python_hooks

POLY_STEP = 4.0

MODES = ("artist", "debug")
# last_zoom: option events carry no zoom, so a Debug->Artist switch would
# otherwise rebuild with zoom 1.0 and show the wrong handle density until the
# next wheel tick. Every handle event refreshes it.
_STATE = {"mode": "artist", "last_zoom": 1.0}

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
    s = 1.0 - t
    return (s * s * s * p0[0] + 3.0 * s * s * t * c1[0] + 3.0 * s * t * t * c2[0] + t * t * t * p3[0],
            s * s * s * p0[1] + 3.0 * s * s * t * c1[1] + 3.0 * s * t * t * c2[1] + t * t * t * p3[1])


def _sample_cubic(p0, c1, c2, p3, step=POLY_STEP):
    length = _dist(p0, c1) + _dist(c1, c2) + _dist(c2, p3)
    count = max(2, int(math.ceil(length / step)) + 1)
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
            tolerance = float(stroke.get("width", 3.0)) * 0.5 + 6.0 / zoom
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
    for command in commands:
        kind = command.get("type")
        if kind == "move":
            out.append(("move", [_command_point(command["to"])]))
        elif kind == "line":
            out.append(("line", [_command_point(command["to"])]))
        elif kind == "quad":
            # Elevate so editing sees one uniform cubic shape.
            ctrl = _command_point(command["control"])
            end = _command_point(command["to"])
            out.append(("quad", [ctrl, end]))
        elif kind == "cubic":
            out.append(("cubic", [_command_point(command["control1"]),
                                  _command_point(command["control2"]),
                                  _command_point(command["to"])]))
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
        elif kind == "quad":
            commands.append({"type": "quad",
                             "from": {"x": current[0], "y": current[1]},
                             "control": {"x": pts[0][0], "y": pts[0][1]},
                             "to": {"x": pts[1][0], "y": pts[1][1]}})
            current = pts[1]
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
        elif kind == "quad":
            if current is not None:
                a = current
                ctrl, end = pts
                c1 = (a[0] + 2.0 / 3.0 * (ctrl[0] - a[0]), a[1] + 2.0 / 3.0 * (ctrl[1] - a[1]))
                c2 = (end[0] + 2.0 / 3.0 * (ctrl[0] - end[0]), end[1] + 2.0 / 3.0 * (ctrl[1] - end[1]))
                points.extend(_sample_cubic(a, c1, c2, end, step)[1:])
            current = pts[1]
        elif kind == "cubic":
            if current is not None:
                points.extend(_sample_cubic(current, pts[0], pts[1], pts[2], step)[1:])
            current = pts[2]
    return points


# --- handle construction -----------------------------------------------------

def _debug_handles(geometry):
    """The stored topology, verbatim: real anchors, real controls, real arms."""
    handles = []
    arms = []
    elements = _elements(geometry["commands"])
    if elements:
        previous = None
        for i, (kind, pts) in enumerate(elements):
            if kind == "move":
                handles.append({"id": f"e{i}:p", "x": pts[0][0], "y": pts[0][1],
                                "shape": SHAPE_ANCHOR, "color": ANCHOR_COLOR})
                previous = pts[0]
            elif kind == "line":
                handles.append({"id": f"e{i}:p", "x": pts[0][0], "y": pts[0][1],
                                "shape": SHAPE_ANCHOR, "color": ANCHOR_COLOR})
                previous = pts[0]
            elif kind == "quad":
                handles.append({"id": f"e{i}:c1", "x": pts[0][0], "y": pts[0][1],
                                "shape": SHAPE_CONTROL, "color": CONTROL_COLOR})
                handles.append({"id": f"e{i}:p", "x": pts[1][0], "y": pts[1][1],
                                "shape": SHAPE_ANCHOR, "color": ANCHOR_COLOR})
                if previous is not None:
                    arms.append([previous, pts[0]])
                    arms.append([pts[0], pts[1]])
                previous = pts[1]
            elif kind == "cubic":
                handles.append({"id": f"e{i}:c1", "x": pts[0][0], "y": pts[0][1],
                                "shape": SHAPE_CONTROL, "color": CONTROL_COLOR})
                handles.append({"id": f"e{i}:c2", "x": pts[1][0], "y": pts[1][1],
                                "shape": SHAPE_CONTROL, "color": CONTROL_COLOR})
                handles.append({"id": f"e{i}:p", "x": pts[2][0], "y": pts[2][1],
                                "shape": SHAPE_ANCHOR, "color": ANCHOR_COLOR})
                if previous is not None:
                    arms.append([previous, pts[0]])
                    arms.append([pts[1], pts[2]])
                previous = pts[2]
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

    min_gap = perceptual_min_separation_px() / max(zoom or 1.0, 1e-6)
    kept = [0, count - 1]
    for _, i in candidates:
        if all(abs(cum[i] - cum[j]) >= min_gap for j in kept):
            kept.append(i)
    kept.sort()
    return kept


def _artist_handles(geometry, zoom):
    """Pseudo-handles on the perceptually distinct key points."""
    elements = _elements(geometry["commands"])
    dense = _flatten_elements(elements) if elements else list(geometry["points"])
    if len(dense) < 2:
        return [], [], dense
    kept = _dominant_indices(dense, zoom)
    handles = [{"id": f"k{i}", "x": dense[i][0], "y": dense[i][1],
                "shape": SHAPE_PSEUDO, "color": PSEUDO_COLOR}
               for i in kept]
    return handles, kept, dense


# --- pushing state to the view ----------------------------------------------

def _push(view, handles, arms):
    # Remember each pushed handle's position: the press handler re-anchors a
    # grab by geometry, since artist ids do not survive a re-flattening.
    session = _SESSIONS.get(view)
    if session is not None:
        session["handles_by_id"] = {h["id"]: (h["x"], h["y"]) for h in handles}
    animean = _animean()
    animean.ui.set_edit_handles(view, handles)
    try:
        import auto_mapping
        items = auto_mapping.overlay_items(view)
    except Exception:
        items = []
    for arm in arms:
        items.append({"id": "edit_arm", "points": arm, "color": ARM_COLOR,
                      "width": 1.0, "removable": False})
    animean.ui.set_overlay(view, items)


def _clear(view):
    session = _SESSIONS.pop(view, None)
    if session is None:
        return
    animean = _animean()
    animean.ui.set_edit_handles(view, [])
    try:
        import auto_mapping
        animean.ui.set_overlay(view, auto_mapping.overlay_items(view))
    except Exception:
        animean.ui.set_overlay(view, [])


def _rebuild(view, session, zoom):
    geometry = session["geometry"]
    if _STATE["mode"] == "debug":
        handles, arms = _debug_handles(geometry)
        session["kept"] = None
        session["dense"] = None
    else:
        handles, kept, dense = _artist_handles(geometry, zoom)
        arms = []
        session["kept"] = kept
        session["dense"] = dense
    _push(view, handles, arms)


# --- applying an edit --------------------------------------------------------

def _polyline_length(points):
    return sum(_dist(a, b) for a, b in zip(points, points[1:]))


def _replace_stroke(view, session):
    geometry = session["geometry"]
    scene = _scene_model(view)
    elements = _elements(geometry["commands"]) if geometry["commands"] else None
    animean = _animean()
    if elements:
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
        end_slot = {"move": 0, "line": 0, "quad": 1, "cubic": 2}[kind]
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
    elif role == "c1" and kind in ("cubic", "quad"):
        pts[0] = pos
    elif role == "c2" and kind == "cubic":
        pts[1] = pos


def _drag_artist(session, dragged, pos):
    """Deform locally: a smooth bump from the dragged key point out to its
    neighbouring key points, in ARC distance - the same falloff both for the
    dense points and, on curve strokes, for the command control points.

    `dragged` is a DENSE index resolved at press time by proximity to the
    grabbed handle - never parsed from the handle id, whose index spoke about
    a previous flattening."""
    dense = session["dense0"]
    cum = session["cum0"]
    kept = session["kept"]
    if dragged not in kept:
        return
    where = kept.index(dragged)
    arc0 = cum[dragged]
    left = cum[kept[where - 1]] if where > 0 else cum[0]
    right = cum[kept[where + 1]] if where + 1 < len(kept) else cum[-1]
    radius_left = max(arc0 - left, POLY_STEP)
    radius_right = max(right - arc0, POLY_STEP)

    start = session["press_pos"]
    delta = (pos[0] - start[0], pos[1] - start[1])

    def weight(arc):
        offset = arc - arc0
        radius = radius_right if offset >= 0.0 else radius_left
        t = min(1.0, abs(offset) / radius)
        c = math.cos(0.5 * math.pi * t)
        return c * c

    moved = [(p[0] + delta[0] * weight(cum[i]), p[1] + delta[1] * weight(cum[i]))
             for i, p in enumerate(dense)]

    if session["elements"] is not None:
        elements = []
        for (kind, pts0) in session["elements0"]:
            new_pts = []
            for point in pts0:
                best = min(range(len(dense)), key=lambda i: _dist(point, dense[i]))
                w = weight(cum[best])
                new_pts.append((point[0] + delta[0] * w, point[1] + delta[1] * w))
            elements.append((kind, new_pts))
        session["elements"] = elements
    else:
        session["points"] = moved
    session["dense"] = moved


# --- hook handlers -----------------------------------------------------------

def _handle_event(message):
    view = message.get("view") or "main"
    phase = message.get("phase")
    zoom = float(message.get("zoom") or 1.0)
    _STATE["last_zoom"] = zoom
    pos_dict = message.get("position") or {}
    pos = (float(pos_dict.get("x", 0.0)), float(pos_dict.get("y", 0.0)))

    if phase == "pick":
        _pick(view, pos, zoom)
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
    if phase == "press":
        # Between the pick and this press the model may have moved under us -
        # an undo, a redo, another tool. The index alone would then point at a
        # DIFFERENT stroke and the drag would edit an innocent bystander, so
        # re-read and only proceed if it is still the same stroke.
        current = _fetch(_scene_model(view), session["frame"],
                         session["layer"], session["index"])
        if current is None or current["id"] != session["geometry"]["id"] \
                or current["property"] != session["geometry"]["property"]:
            _clear(view)
            return
        # Where did the user actually grab? Resolved by GEOMETRY, not by the
        # id: an artist id is an index into the flattening, and the previous
        # drag changed the flattening, so yesterday's "k37" means nothing in
        # today's dense array. The pushed handle's position is remembered at
        # push time and re-anchored to the nearest key point after re-reading.
        grabbed_at = session.get("handles_by_id", {}).get(handle_id)
        session["geometry"] = current
        session["elements"] = _elements(current["commands"]) if current["commands"] else None
        session["points"] = list(current["points"])
        session["drag_index"] = None
        if _STATE["mode"] == "artist":
            _rebuild(view, session, zoom)   # refresh dense/kept from truth
            dense = session.get("dense") or []
            kept = session.get("kept") or []
            if grabbed_at is not None and kept:
                session["drag_index"] = min(
                    kept, key=lambda i: _dist(dense[i], grabbed_at))
        session["press_pos"] = pos
        session["elements0"] = [(k, list(p)) for k, p in session["elements"]] \
            if session["elements"] is not None else None
        session["dense0"] = list(session["dense"]) if session.get("dense") else None
        session["cum0"] = _cumulative(session["dense0"]) if session.get("dense0") else None
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
            elif session.get("dense0") and session.get("drag_index") is not None:
                _drag_artist(session, session["drag_index"], pos)
            if _replace_stroke(view, session):
                session["changed"] = True
                session["geometry"]["commands"] = (
                    _elements_to_commands(session["elements"]) if session["elements"] is not None else [])
                if session["elements"] is None:
                    session["geometry"]["points"] = list(session["points"])
        if phase == "release":
            if session.get("changed"):
                try:
                    _animean().ui.history_commit("Edit Stroke", view)
                except Exception:
                    pass
            # Re-read the stroke as the model now holds it, so the next drag
            # starts from truth rather than from this session's arithmetic.
            refreshed = _fetch(_scene_model(view), session["frame"],
                               session["layer"], session["index"])
            if refreshed is None:
                _clear(view)
                return
            session["geometry"] = refreshed
            session["elements"] = _elements(refreshed["commands"]) if refreshed["commands"] else None
            session["points"] = list(refreshed["points"])
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
    else:
        dense = session.get("dense") or []
        kept = session.get("kept") or []
        handles = [{"id": f"k{i}", "x": dense[i][0], "y": dense[i][1],
                    "shape": SHAPE_PSEUDO, "color": PSEUDO_COLOR}
                   for i in kept if i < len(dense)]
        _push(view, handles, [])


def _pick(view, pos, zoom):
    scene = _scene_model(view)
    frame = max(scene.current_frame(), 0)
    found = _find_stroke(scene, frame, pos, zoom)
    if found is None:
        _clear(view)
        return
    layer, index = found
    geometry = _fetch(scene, frame, layer, index)
    if geometry is None:
        _clear(view)
        return
    session = {
        "frame": frame,
        "layer": layer,
        "index": index,
        "geometry": geometry,
        "elements": _elements(geometry["commands"]) if geometry["commands"] else None,
        "points": list(geometry["points"]),
        "kept": None,
        "dense": None,
        "changed": False,
    }
    _SESSIONS[view] = session
    _rebuild(view, session, zoom)
    mode = _STATE["mode"]
    print(f"[edit_tool] editing stroke {index} on layer {layer} ({mode} mode; "
          f"min spacing {perceptual_min_separation_px():.1f}px on screen)")


def _option_changed(message):
    if message.get("hook") != "edit_mode":
        return
    value = str(message.get("value", "")).lower()
    if value not in MODES or _STATE["mode"] == value:
        return
    _STATE["mode"] = value
    # Option events carry no zoom; the last handle event does.
    zoom = _STATE["last_zoom"]
    for view, session in list(_SESSIONS.items()):
        _rebuild(view, session, zoom)
    print(f"[edit_tool] edit mode -> {value}")


python_hooks.set_hook(_handle_event, handle=True)
python_hooks.set_hook(_option_changed, option=True, tool="arrow")
