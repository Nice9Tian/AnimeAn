"""Connect tool: bridge two snapped vertices with a new stroke.

The C++ side is a pure mechanism (the brush ring, hover/click forwarding
through the handle pipeline, the handle glyphs); everything a click MEANS
lives here.

SNAPPING - hovering resolves the vertex a click would take and shows it as
a hint handle. Priority inside brush_radius/3: stroke ENDPOINTS and CORNER
vertices first, then curvature APEXES (拐点); beyond that band, the nearest
vertex of any kind within the brush radius. A corner is a genuine kink (the
tight one-vertex angle alone crosses the threshold); an apex is smooth but
salient (the wide window accumulates enough turn and peaks locally) - one
wide metric for both misread every tight crest as a corner. Auto Snap off
means free clicks: the exact cursor position, no vertex search.

CONNECTING - the first click arms a point, the second builds the stroke.
All three modes are one construction, quad(A, P, B) elevated through the
shared wheel pyfile/bezier.py:
  PolyMode   - a straight line (P unused)
  CurveMode  - P is the FUTURE INTERSECTION of the two endpoints' forward
               tangent rays (tangents look BACKTRACK vertices into their
               strokes), so the bridge leaves both strokes tangentially
  SmoothMode - P is pulled toward the chord by the Smooth slider: 0 IS
               CurveMode, 100 puts P on the chord, which IS the straight
               line - the modes are the ends of one dial.
Rays that diverge or run parallel fall back to the chord; a near-parallel
intersection is clamped to REACH_LIMIT chord lengths so it cannot balloon.

LIFECYCLE - the new stroke lands in the current layer and stays FOCUSED:
tool-option changes recompute it in place, the on-canvas buttons finish it
(accept keeps it, delete removes it), switching tools accepts it silently.
With Auto Accept on there are no buttons; drawing the next connection just
moves the focus. Undo/redo drops the focus outright - stroke indices are
not trustworthy across a history restore.
"""
import math

import bezier
import python_hooks

POLY_STEP = 4.0
BRUSH_RADIUS = 12.0     # mirrors the C++ brush ring (m_eraserRadius)
BACKTRACK = 5           # vertices to look back for a tangent
REACH_LIMIT = 2.0       # future point may sit this many chord lengths out
CORNER_DEG = 35.0
APEX_DEG = 8.0
TURN_WINDOW = 3

SHAPE_ANCHOR = 0
SHAPE_CIRCLE = 1
SHAPE_DIAMOND = 2
SHAPE_ACCEPT = 3
SHAPE_DELETE = 4

HINT_COLORS = {1: (255, 200, 70, 255),    # endpoint / corner: amber diamond
               2: (130, 185, 255, 255),   # curvature apex: blue circle
               3: (235, 235, 235, 255)}   # plain vertex / free point
FIRST_COLOR = (255, 120, 40, 255)
ACCEPT_COLOR = (60, 170, 90, 255)
DELETE_COLOR = (205, 70, 70, 255)

_STATE = {"auto_snap": True, "mode": "poly", "smooth": 50, "auto_accept": False,
          "last_zoom": 1.0}
_SESSIONS = {}   # view name -> session dict


def options_state():
    """What toolcontrol.py shows in the options panel."""
    return dict(_STATE)


def _animean():
    import animean_python
    return animean_python


def _scene_model(view_name):
    # Same lookup edit_tool uses: the per-view model registered on __main__,
    # with the get_scene() info list as the fallback.
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


def _dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


# --- vertex classification ---------------------------------------------------

def _turn_deg(points, i, window):
    n = len(points)
    a = points[max(0, i - window)]
    p = points[i]
    b = points[min(n - 1, i + window)]
    v1 = (p[0] - a[0], p[1] - a[1])
    v2 = (b[0] - p[0], b[1] - p[1])
    l1 = math.hypot(*v1)
    l2 = math.hypot(*v2)
    if l1 < 1e-9 or l2 < 1e-9:
        return 0.0
    cross = v1[0] * v2[1] - v1[1] * v2[0]
    dot = v1[0] * v2[0] + v1[1] * v2[1]
    return math.degrees(math.atan2(cross, dot))


def _classify(points):
    """[(snap_class, vertex_index)] - 1 endpoints/corners, 2 apexes, 3 rest."""
    n = len(points)
    if n == 0:
        return []
    if n == 1:
        return [(1, 0)]
    out = [(1, 0), (1, n - 1)]
    wide = [abs(_turn_deg(points, i, TURN_WINDOW)) for i in range(n)]
    for i in range(1, n - 1):
        if abs(_turn_deg(points, i, 1)) >= CORNER_DEG:
            out.append((1, i))
            continue
        t = wide[i]
        local_max = t >= APEX_DEG and all(
            wide[j] <= t + 1e-9
            for j in range(max(1, i - TURN_WINDOW), min(n - 1, i + TURN_WINDOW + 1)))
        out.append((2, i) if local_max else (3, i))
    return out


def _stroke_polylines(stroke):
    """The stroke's RENDERED geometry - the flattened fitted path. Snapping
    to the raw input samples put hints (and bridges) off the drawn ink by
    the fit tolerance; the polylines are the curve the user actually sees.
    Raw points are only the fallback for strokes with no path."""
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


def _build_candidates(scene, frame):
    out = []
    structure = scene.get_structure()
    for layer in structure["layers"]:
        if not layer["visible"] or layer.get("locked") or layer["type"] == "fill":
            continue
        cell = scene.cell_to_dict(layer["index"], frame, True, POLY_STEP)
        for index, stroke in enumerate(cell["image"]["strokes"]):
            for poly_index, points in enumerate(_stroke_polylines(stroke)):
                for snap_class, vertex in _classify(points):
                    out.append((snap_class, points[vertex],
                                {"layer": layer["index"], "stroke": index,
                                 "poly": poly_index, "vertex": vertex}))
    return out


def _candidates(session, scene, frame, fresh=False):
    """Every snappable vertex, as (snap_class, point, ref). Serializing the
    whole scene at hover rate was the cost problem, so hovers reuse a cached
    list; anything that can change the model refreshes it (picks, our own
    edits, the finish hooks, history restores)."""
    cache = session.get("snap_cache")
    if not fresh and cache is not None and cache["frame"] == frame:
        return cache["candidates"]
    candidates = _build_candidates(scene, frame)
    session["snap_cache"] = {"frame": frame, "candidates": candidates}
    return candidates


def _resolve(session, scene, frame, pos, fresh=False):
    """The point a click at `pos` would take: a pick dict or None."""
    if not _STATE["auto_snap"]:
        return {"point": pos, "snap_class": 3, "free": True}
    candidates = _candidates(session, scene, frame, fresh)
    inner = BRUSH_RADIUS / 3.0

    def nearest(pool):
        best = None
        best_d = None
        for snap_class, point, ref in pool:
            d = _dist(point, pos)
            if best_d is None or d < best_d:
                best = {"point": point, "snap_class": snap_class,
                        "free": False, **ref}
                best_d = d
        return best

    for wanted in (1, 2):
        pool = [c for c in candidates
                if c[0] == wanted and _dist(c[1], pos) <= inner]
        if pool:
            return nearest(pool)
    pool = [c for c in candidates if _dist(c[1], pos) <= BRUSH_RADIUS]
    return nearest(pool) if pool else None


# --- connection geometry -----------------------------------------------------

def _norm(v):
    length = math.hypot(v[0], v[1])
    return (v[0] / length, v[1] / length) if length > 1e-12 else None


def _tangent(points, index, other):
    """Outward extension direction at a vertex. Endpoints continue past the
    end; interior vertices take the local tangent signed toward the partner."""
    n = len(points)
    if n < 2:
        return None
    if index == 0:
        back = points[min(BACKTRACK, n - 1)]
        return _norm((points[0][0] - back[0], points[0][1] - back[1]))
    if index == n - 1:
        back = points[max(0, n - 1 - BACKTRACK)]
        return _norm((points[-1][0] - back[0], points[-1][1] - back[1]))
    a = points[max(0, index - TURN_WINDOW)]
    b = points[min(n - 1, index + TURN_WINDOW)]
    d = _norm((b[0] - a[0], b[1] - a[1]))
    if d is None:
        return None
    to_other = (other[0] - points[index][0], other[1] - points[index][1])
    return d if d[0] * to_other[0] + d[1] * to_other[1] >= 0.0 else (-d[0], -d[1])


def _future_intersection(a, da, b, db):
    if da is None or db is None:
        return None
    denominator = da[0] * db[1] - da[1] * db[0]
    if abs(denominator) < 1e-9:
        return None
    wx, wy = b[0] - a[0], b[1] - a[1]
    t = (wx * db[1] - wy * db[0]) / denominator
    s = (wx * da[1] - wy * da[0]) / denominator
    if t <= 0.0 or s <= 0.0:
        return None   # behind one of the strokes: no FUTURE meeting point
    return (a[0] + da[0] * t, a[1] + da[1] * t)


def _pick_tangent(scene, frame, pick, other_point):
    if pick.get("free"):
        return None
    cell = scene.cell_to_dict(pick["layer"], frame, True, POLY_STEP)
    strokes = cell["image"]["strokes"]
    if not 0 <= pick["stroke"] < len(strokes):
        return None
    polylines = _stroke_polylines(strokes[pick["stroke"]])
    if not 0 <= pick.get("poly", 0) < len(polylines):
        return None
    points = polylines[pick.get("poly", 0)]
    if not 0 <= pick["vertex"] < len(points):
        return None
    return _tangent(points, pick["vertex"], other_point)


def _control_point(scene, frame, a_pick, b_pick):
    """quad control for Curve/Smooth, or None for the straight chord."""
    a = a_pick["point"]
    b = b_pick["point"]
    p = _future_intersection(a, _pick_tangent(scene, frame, a_pick, b),
                             b, _pick_tangent(scene, frame, b_pick, a))
    if p is None:
        return None
    mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
    chord = _dist(a, b)
    limit = REACH_LIMIT * max(chord, 1e-9)
    d = _dist(p, mid)
    if d > limit:
        k = limit / d
        p = (mid[0] + (p[0] - mid[0]) * k, mid[1] + (p[1] - mid[1]) * k)
    if _STATE["mode"] == "smooth":
        abx, aby = b[0] - a[0], b[1] - a[1]
        len2 = abx * abx + aby * aby
        t = 0.0 if len2 < 1e-12 else max(0.0, min(1.0, (
            (p[0] - a[0]) * abx + (p[1] - a[1]) * aby) / len2))
        on_chord = (a[0] + abx * t, a[1] + aby * t)
        p = bezier.lerp(p, on_chord, max(0.0, min(1.0, _STATE["smooth"] / 100.0)))
    return p


def _pick_style(scene, frame, pick):
    """(color, width) of the stroke a pick sits on, if any."""
    if pick.get("free"):
        return None
    cell = scene.cell_to_dict(pick["layer"], frame, True, POLY_STEP)
    strokes = cell["image"]["strokes"]
    if not 0 <= pick["stroke"] < len(strokes):
        return None
    stroke = strokes[pick["stroke"]]
    color = stroke.get("color") or {}
    return ((int(color.get("r", 0)), int(color.get("g", 0)),
             int(color.get("b", 0)), int(color.get("a", 255))),
            float(stroke.get("width", 3.0)))


def _build_stroke(scene, frame, a_pick, b_pick):
    """The connection as a stroke object, matching the bridged line's look."""
    a = a_pick["point"]
    b = b_pick["point"]
    style = _pick_style(scene, frame, a_pick) or _pick_style(scene, frame, b_pick)
    color, width = style if style else ((0, 0, 0, 255), 3.0)

    control = None
    if _STATE["mode"] in ("curve", "smooth"):
        control = _control_point(scene, frame, a_pick, b_pick)
    if control is None:
        commands = [{"type": "move", "to": {"x": a[0], "y": a[1]}},
                    {"type": "line",
                     "from": {"x": a[0], "y": a[1]},
                     "to": {"x": b[0], "y": b[1]}}]
        flat = [a, b]
    else:
        # Elevation and flattening via the shared wheel pyfile/bezier.py.
        cub = bezier.quad_cubic(a, control, b)
        commands = [{"type": "move", "to": {"x": a[0], "y": a[1]}},
                    {"type": "cubic",
                     "from": {"x": cub[0][0], "y": cub[0][1]},
                     "control1": {"x": cub[1][0], "y": cub[1][1]},
                     "control2": {"x": cub[2][0], "y": cub[2][1]},
                     "to": {"x": cub[3][0], "y": cub[3][1]}}]
        samples = max(2, min(64, int(math.ceil(bezier.hull_length(cub) / POLY_STEP))))
        flat = [bezier.eval_cubic(cub, k / samples) for k in range(samples + 1)]
    return _animean().vectorlogic.make_stroke_object_from_path(
        commands, flat, color, width, 0)


# --- handle display ----------------------------------------------------------

def _push_handles(view, session):
    handles = []
    hint = session.get("hint")
    if hint is not None:
        # interactive=False: the hint sits UNDER the cursor, and a
        # hit-testable marker there swallowed the very click it advertised.
        shape = SHAPE_DIAMOND if hint["snap_class"] == 1 else (
            SHAPE_CIRCLE if hint["snap_class"] == 2 else SHAPE_ANCHOR)
        handles.append({"id": "connect:hint",
                        "x": hint["point"][0], "y": hint["point"][1],
                        "shape": shape, "interactive": False,
                        "color": HINT_COLORS[hint["snap_class"]]})
    first = session.get("first")
    if first is not None:
        handles.append({"id": "connect:first",
                        "x": first["point"][0], "y": first["point"][1],
                        "shape": SHAPE_DIAMOND, "interactive": False,
                        "color": FIRST_COLOR})
    pending = session.get("pending")
    buttons = {}
    if pending is not None and not _STATE["auto_accept"]:
        a = pending["a"]["point"]
        b = pending["b"]["point"]
        mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
        offset = 16.0 / max(0.05, _STATE["last_zoom"])
        buttons["connect:accept"] = (mid[0] - offset, mid[1] - offset)
        buttons["connect:delete"] = (mid[0] + offset, mid[1] - offset)
        handles.append({"id": "connect:accept",
                        "x": buttons["connect:accept"][0],
                        "y": buttons["connect:accept"][1],
                        "shape": SHAPE_ACCEPT, "color": ACCEPT_COLOR})
        handles.append({"id": "connect:delete",
                        "x": buttons["connect:delete"][0],
                        "y": buttons["connect:delete"][1],
                        "shape": SHAPE_DELETE, "color": DELETE_COLOR})
    session["buttons"] = buttons
    try:
        _animean().ui.set_edit_handles(view, handles)
    except Exception:
        pass


# --- lifecycle ---------------------------------------------------------------

def _session(view):
    return _SESSIONS.setdefault(view, {})


def _commit(label, view):
    try:
        _animean().ui.refresh()
    except Exception:
        pass
    try:
        _animean().ui.history_commit(label, view)
    except Exception:
        pass


def _finalize(view, session, commit_adjust=True):
    """Accept the focused connection: keep the stroke, drop the focus. The
    recompute edits were applied without history entries (a slider drag must
    not burn one per tick), so a changed connection commits once here."""
    pending = session.pop("pending", None)
    if pending is not None and pending.get("changed") and commit_adjust:
        _commit("Adjust Connect Line", view)
    return pending


def _pending_valid(scene, pending):
    """The recorded index still names OUR connection: the stroke exists and
    its ends are the recorded pick points. Anything else (an undo slipped
    through, another view edited the layer) means the focus must be dropped,
    not acted on - a stale index edits an innocent stroke."""
    try:
        cell = scene.cell_to_dict(pending["layer"], pending["row"], True, POLY_STEP)
        strokes = cell["image"]["strokes"]
        if not 0 <= pending["stroke_index"] < len(strokes):
            return False
        pts = strokes[pending["stroke_index"]].get("raw_points") or []
        if len(pts) < 2:
            return False
        first = (float(pts[0]["x"]), float(pts[0]["y"]))
        last = (float(pts[-1]["x"]), float(pts[-1]["y"]))
        return (_dist(first, pending["a"]["point"]) < 0.25
                and _dist(last, pending["b"]["point"]) < 0.25)
    except Exception:
        return False


def _layer_writable(scene, layer_index):
    try:
        for layer in scene.get_structure()["layers"]:
            if layer["index"] == layer_index:
                return (layer["visible"] and not layer.get("locked")
                        and layer["type"] != "fill")
    except Exception:
        pass
    return False


def _delete_pending(view, session):
    pending = session.pop("pending", None)
    if pending is None:
        return
    try:
        scene = _scene_model(view)
        if not _pending_valid(scene, pending):
            return
        scene.remove_stroke(pending["row"], pending["layer"], pending["stroke_index"])
        session.pop("snap_cache", None)
        _commit("Remove Connect Line", view)
    except Exception:
        import traceback
        traceback.print_exc()


def _recompute(view, session):
    pending = session.get("pending")
    if pending is None:
        return
    try:
        scene = _scene_model(view)
        if not _pending_valid(scene, pending):
            session.pop("pending", None)
            _push_handles(view, session)
            return
        image = scene.image_at(pending["row"], pending["layer"], True)
        stroke = _build_stroke(scene, pending["row"], pending["a"], pending["b"])
        image.replace_stroke_with_pieces(pending["stroke_index"], [stroke])
        pending["changed"] = True
        session.pop("snap_cache", None)
        _animean().ui.refresh()
    except Exception:
        import traceback
        traceback.print_exc()
        session.pop("pending", None)
    _push_handles(view, session)


def _create_connection(view, session, scene, frame, layer, second):
    if not _layer_writable(scene, layer):
        # The lock that gates every drawing tool gates this one too. The
        # armed point stays armed: unlocking the layer lets the same second
        # click finish the gesture.
        return
    first = session.pop("first")
    # The previous connection, if still focused, is accepted by starting a
    # new one - with or without Auto Accept.
    _finalize(view, session)
    try:
        image = scene.image_at(frame, layer, True)
        stroke = _build_stroke(scene, frame, first, second)
        index = len(scene.cell_to_dict(layer, frame, True, POLY_STEP)["image"]["strokes"])
        image.add_stroke_object(stroke)
        session.pop("snap_cache", None)
        _commit("Connect Lines", view)
        session["pending"] = {"row": frame, "layer": layer, "stroke_index": index,
                              "a": first, "b": second, "changed": False}
    except Exception:
        import traceback
        traceback.print_exc()
        session["first"] = first   # the gesture failed; keep the armed point
    _push_handles(view, session)


def _handle_event(message):
    if message.get("base_tool") != "connect":
        return
    view = message.get("view", "")
    phase = message.get("phase")
    session = _session(view)
    zoom = float(message.get("zoom") or 0.0)
    if zoom > 0.0:
        _STATE["last_zoom"] = zoom

    if phase == "cancel":
        # Tool switched away: the connection stays as drawn (silent accept),
        # the buttons and hints just go.
        _finalize(view, session)
        _SESSIONS.pop(view, None)
        return

    pos_dict = message.get("position") or {}
    pos = (float(pos_dict.get("x", 0.0)), float(pos_dict.get("y", 0.0)))
    cell = message.get("cell") or {}
    frame = int(cell.get("row", 0))
    layer = int(cell.get("layer", 0))

    try:
        scene = _scene_model(view)
    except Exception:
        return

    if phase == "view":
        # Zoom changed: the button offsets are screen-sized, so re-place.
        _push_handles(view, session)
        return

    if phase == "hover":
        session["hint"] = _resolve(session, scene, frame, pos)
        _push_handles(view, session)
        return

    if phase == "pick":
        # Clicks resolve FRESH - the hover cache may be seconds old, and a
        # click is where correctness matters.
        target = _resolve(session, scene, frame, pos, fresh=True)
        if target is None:
            _push_handles(view, session)
            return
        first = session.get("first")
        if first is not None and session.get("first_row") != frame:
            # The timeline moved under the armed point: its geometry belongs
            # to another frame, so re-arm rather than bridge across frames.
            first = None
        if first is None:
            session["first"] = target
            session["first_row"] = frame
        elif _dist(first["point"], target["point"]) <= 1e-9:
            pass   # clicking the armed point again is not a connection
        else:
            _create_connection(view, session, scene, frame, layer, target)
        _push_handles(view, session)
        return

    if phase == "press":
        session["pressed"] = message.get("handle", "")
        return

    if phase == "release":
        pressed = session.pop("pressed", "")
        handle = message.get("handle", "")
        if handle != pressed:
            return
        # A button acts only when RELEASED over itself - C++ echoes the
        # pressed id wherever the mouse ends up, and dragging off a button
        # is the universal way to back out of a misclick.
        center = (session.get("buttons") or {}).get(handle)
        if center is None or _dist(pos, center) > 16.0 / max(0.05, _STATE["last_zoom"]):
            return
        if handle == "connect:accept":
            _finalize(view, session)
            _push_handles(view, session)
        elif handle == "connect:delete":
            _delete_pending(view, session)
            _push_handles(view, session)
        return


def _option_event(message):
    option = message.get("option") or {}
    name = option.get("name", "")
    value = option.get("value")
    if name == "connect_auto_snap":
        _STATE["auto_snap"] = value in (True, "on", "true", 1)
    elif name == "connect_mode":
        if value in ("poly", "curve", "smooth"):
            _STATE["mode"] = value
    elif name == "connect_smooth":
        try:
            _STATE["smooth"] = max(0, min(100, int(value)))
        except (TypeError, ValueError):
            return
    elif name == "connect_auto_accept":
        _STATE["auto_accept"] = value in (True, "on", "true", 1)
    else:
        return
    if name in ("connect_mode", "connect_smooth"):
        # 实时重计算: the focused connection follows the dials.
        for view in list(_SESSIONS):
            _recompute(view, _SESSIONS[view])
    elif name == "connect_auto_accept":
        for view in list(_SESSIONS):
            _push_handles(view, _SESSIONS[view])


def _history_restored(message):
    # Undo/redo may have added, removed or renumbered strokes: the recorded
    # index is no longer trustworthy, so the focus is dropped, not repaired.
    for view in list(_SESSIONS):
        session = _SESSIONS[view]
        session.pop("pending", None)
        session.pop("first", None)
        session.pop("first_row", None)
        session.pop("hint", None)
        session.pop("snap_cache", None)
        _push_handles(view, session)


def _model_changed(cell, stroke, message):
    # Any committed drawing/erasing (this view or another) may have moved
    # vertices: the hover cache re-reads on the next event.
    for session in _SESSIONS.values():
        session.pop("snap_cache", None)


python_hooks.set_hook(_handle_event, handle=True)
python_hooks.set_hook(_option_event, option=True, tool="connect")
python_hooks.set_hook(_history_restored, historyrestore=True)
python_hooks.set_hook(_model_changed, linefinish=True, erasefinish=True,
                      deletefinish=True, fillfinish=True, movefinish=True)
