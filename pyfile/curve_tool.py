"""Curve tool: click anchors, the tool joins them with a smooth curve.

Left click places an anchor; a right click ends the line at its last anchor.
An ended line keeps its anchors, so the pointer snaps to either end of it and
a click there continues the SAME line. While drawing, snapping to another
curve line's end joins the two lines into one, and snapping to the line's own
first anchor closes it. Alt held: no snapping. The behaviour was settled in a
browser demo with the user, see docs/plan/2026-11-02-线条与圆角工具/.

SMOOTHING - centripetal Catmull-Rom (alpha 0.5) through the anchors, every
span a cubic Bezier, so the stroke is the same kind of path the pen's fit
makes. An open end has no tangent of its own: the end span is the parabola
into the next anchor's tangent. A new anchor reshapes only the span before
it. Hobby's spline was tried and archived in old_history/hobby_spline.js.

STROKES - a line with two or more anchors is a real stroke in the current
layer, rewritten in place (draw order kept) and committed as one history
entry per anchor, so Ctrl+Z takes the last anchor back and drawing goes on
from the one before. The anchors live in the scene's scriptData under
STORE_KEY, one record per line, keyed by the cell's (asset_index, frame_id);
a record names its stroke by geometry - the stroke whose path runs exactly
through the recorded anchors - never by index, which undo and other tools
renumber. A line nobody can find any more (erased, cut, moved) loses its
record the next time its cell is read.

PREVIEW - what follows the pointer is the curve the next click commits: the
span that changes and the new span, drawn as overlay; the stroke itself only
changes on the click.

Hand-and-eye sizes (snap radius, marker sizes, double-click gap) are SCREEN
pixels, converted through pyfile/viewscale.py; the geometry is canvas pixels.
"""
import math
import uuid

import bezier
import overlay_stack
import python_hooks
import script_store
import viewscale

NAME = "curve"
PROPERTY = "curve_tool"
STORE_KEY = "curve_tool"
OVERLAY_OWNER = "curve_tool"

SNAP_PX = 16.0          # screen px: how close the pointer has to come to an end
STICK = 1.35            # a held snap lets go at SNAP_PX * STICK, no flicker on the rim
MIN_GAP_PX = 3.0        # screen px: a click this close to the last anchor is a double click
POLY_STEP = 2.0         # canvas px between the stroke's flattened points
MATCH_EPS = 0.01        # canvas px: a stroke runs through a record's anchors
EPS = 1e-6

PREVIEW_COLOR = (61, 142, 201, 255)     # the focus colour: what the click will do
END_COLOR = (230, 168, 23, 255)         # amber, Connect's endpoint/corner hint
END_INK = (138, 98, 0, 255)
ANCHOR_INK = (61, 142, 201, 255)

_STATE = {"snap": True, "width": 5.0, "color": (0, 0, 0, 255)}
_SESSIONS = {}   # view name -> session dict


def _animean():
    import animean_python
    return animean_python


def options_state():
    return dict(_STATE)


def remember_width(state):
    """The pen width C++ holds, read whenever Tool Options is built; moving the
    slider afterwards arrives through the pen_width option hook."""
    try:
        _STATE["width"] = max(1.0, float((state or {}).get("pen_width", _STATE["width"])))
    except (TypeError, ValueError):
        pass


# --- smoothing ---------------------------------------------------------------

def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1])


def _mul(a, k):
    return (a[0] * k, a[1] * k)


def catmull_rom(anchors, closed=False):
    """Centripetal Catmull-Rom through `anchors` as cubic Bezier spans.

    m[i] is the Barry-Goldman tangent at anchor i in the knot parameter; span
    i has knot length sqrt(|P[i+1] - P[i]|). Open ends take the parabola into
    their neighbour's tangent; two anchors make a straight span.
    """
    points = [(float(p[0]), float(p[1])) for p in anchors]
    n = len(points)
    if n < 2:
        return []
    count = n if closed else n - 1
    at = lambda i: points[i % n]
    knots = [max(math.sqrt(bezier.dist(at(i), at(i + 1))), EPS) for i in range(count)]
    gap = lambda i: knots[i % count]
    tangents = {}
    for i in range(n):
        if not closed and (i == 0 or i == n - 1):
            continue
        d0, d1 = gap(i - 1), gap(i)
        a, b, c = at(i - 1), at(i), at(i + 1)
        tangents[i] = _add(_sub(_mul(_sub(b, a), 1.0 / d0), _mul(_sub(c, a), 1.0 / (d0 + d1))),
                           _mul(_sub(c, b), 1.0 / d1))
    spans = []
    for i in range(count):
        a, b, length = at(i), at(i + 1), knots[i]
        ma, mb = tangents.get(i), tangents.get((i + 1) % n)
        if ma is not None and mb is not None:
            c1, c2 = _add(a, _mul(ma, length / 3.0)), _sub(b, _mul(mb, length / 3.0))
        elif mb is not None:
            q = _sub(b, _mul(mb, length / 2.0))
            c1, c2 = bezier.lerp(a, q, 2.0 / 3.0), bezier.lerp(b, q, 2.0 / 3.0)
        elif ma is not None:
            q = _add(a, _mul(ma, length / 2.0))
            c1, c2 = bezier.lerp(a, q, 2.0 / 3.0), bezier.lerp(b, q, 2.0 / 3.0)
        else:
            c1, c2 = bezier.lerp(a, b, 1.0 / 3.0), bezier.lerp(a, b, 2.0 / 3.0)
        spans.append((a, c1, c2, b))
    return spans


def flatten(spans, step=POLY_STEP):
    """Dense points along the spans (pyfile/bezier.py does the evaluation)."""
    if not spans:
        return []
    out = [spans[0][0]]
    for cub in spans:
        samples = max(1, min(256, int(math.ceil(bezier.hull_length(cub) / step))))
        out.extend(bezier.eval_cubic(cub, k / samples) for k in range(1, samples + 1))
    return out


def path_commands(spans):
    def pt(p):
        return {"x": p[0], "y": p[1]}
    commands = [{"type": "move", "to": pt(spans[0][0])}]
    for a, c1, c2, b in spans:
        commands.append({"type": "cubic", "from": pt(a), "control1": pt(c1),
                         "control2": pt(c2), "to": pt(b)})
    return commands


def _pen_color():
    """The curve draws in the pen's colour (pyfile/tool_colors.py keeps it per
    tool); the colour in force when the tool was used is the fallback."""
    try:
        import tool_colors
        color = tool_colors.color_for("pen")
        if color is not None:
            return tuple(int(v) for v in color)
    except Exception:
        pass
    return _STATE["color"]


def _make_stroke(anchors, closed, color, width):
    spans = catmull_rom(anchors, closed)
    return _animean().vectorlogic.make_stroke_object_from_path(
        path_commands(spans), flatten(spans), color, width, 0)


# --- records -----------------------------------------------------------------

def _scene_model(view):
    import __main__
    model = getattr(__main__, f"{view}_model", None)
    if model is not None:
        return model
    target = f"{view}_paint_view"
    for info in _animean().get_scene():
        if info.get("sceneName") == target:
            return info["scene"]
    raise KeyError(f"no scene registered as {target!r}")


def _records(scene):
    data = script_store.read(scene, STORE_KEY) or {}
    lines = data.get("lines") if isinstance(data, dict) else None
    return [dict(r) for r in lines] if isinstance(lines, list) else []


def _write_records(scene, records):
    script_store.write(scene, STORE_KEY, {"lines": records} if records else None)


def _cell_key(scene, layer, row):
    cell = scene.cell_to_dict(layer, row, False, 4.0)
    return [int(cell.get("asset_index", -1)), int(cell.get("frame_id", -1))]


def _stroke_cubics(stroke):
    """The stroke's one path as cubics, or None for anything else. A span
    the binding reports as a line is its straight cubic (control points on
    the thirds), which is what two anchors make."""
    commands = stroke.get("commands") or []
    if not commands or commands[0].get("type") != "move":
        return None
    out, cur = [], None
    for command in commands:
        kind = command.get("type")
        to = command.get("to")
        if not isinstance(to, dict):
            return None
        end = (float(to["x"]), float(to["y"]))
        if kind == "move":
            if cur is not None:
                return None   # a second subpath: not a line this tool made
        elif kind == "line":
            out.append(bezier.line_cubic(cur, end))
        elif kind == "cubic":
            c1, c2 = command["control1"], command["control2"]
            out.append((cur, (float(c1["x"]), float(c1["y"])), (float(c2["x"]), float(c2["y"])), end))
        else:
            return None
        cur = end
    return out


def _matches(cubics, record):
    """The stroke is exactly the curve the record draws - every control point,
    so a handle moved by another tool (anchors kept) is no longer ours."""
    spans = catmull_rom(record["anchors"], bool(record.get("closed")))
    if cubics is None or len(cubics) != len(spans):
        return False
    return all(bezier.dist(p, q) <= MATCH_EPS for a, b in zip(cubics, spans) for p, q in zip(a, b))


def _locate(strokes, record):
    for index, stroke in enumerate(strokes):
        if _matches(_stroke_cubics(stroke), record):
            return index
    return None


def _layer_drawable(layer):
    return layer["visible"] and not layer.get("locked") and layer["type"] != "fill"


def _lines_in_row(scene, row):
    """[(layer, index, record)] for every curve line still drawn on `row` in a
    layer the user may draw on. Reads each cell once. A record whose stroke
    is gone (erased, cut, moved) is skipped here and dropped the next time a
    line in its cell is written - hovering never writes the document."""
    records = _records(scene)
    if not records:
        return []
    found = []
    by_key = {}
    for layer in scene.get_structure()["layers"]:
        if layer["type"] == "fill":
            continue
        cell = scene.cell_to_dict(layer["index"], row, False, 4.0)
        key = (int(cell.get("asset_index", -1)), int(cell.get("frame_id", -1)))
        by_key.setdefault(key, (layer, cell["image"]["strokes"]))
    for record in records:
        hit = by_key.get(tuple(record.get("cell") or ()))
        if hit is None:
            continue
        layer, strokes = hit
        index = _locate(strokes, record)
        if index is not None and _layer_drawable(layer):
            found.append((layer["index"], index, record))
    return found


# --- session -----------------------------------------------------------------

def _session(view):
    return _SESSIONS.setdefault(view, {"active": None, "cursor": None, "target": None,
                                       "alt": False, "zoom": 1.0})


def _px(session, screen_px):
    return viewscale.to_canvas_length(screen_px, max(0.05, session.get("zoom") or 1.0))


def _snap_targets(session, lines):
    """Every end the pointer could snap to, as dicts with 'kind' (resume /
    merge / close), the point, and what it belongs to."""
    out = []
    active = session.get("active")
    for layer, index, record in lines:
        if record.get("closed"):
            continue
        anchors = record["anchors"]
        if active is not None:
            if record.get("uid") == active.get("uid"):
                if len(active["anchors"]) >= 3:
                    out.append({"kind": "close", "end": "head", "at": tuple(active["anchors"][0]),
                                "uid": record["uid"], "layer": layer})
                continue
            if layer != active["layer"]:
                continue        # lines join only inside one cell
            kind = "merge"
        else:
            kind = "resume"
        out.append({"kind": kind, "end": "tail", "at": tuple(anchors[-1]),
                    "uid": record["uid"], "layer": layer})
        if len(anchors) > 1:
            out.append({"kind": kind, "end": "head", "at": tuple(anchors[0]),
                        "uid": record["uid"], "layer": layer})
    if active is not None and active.get("uid") is None and len(active["anchors"]) >= 3:
        out.append({"kind": "close", "end": "head", "at": tuple(active["anchors"][0]),
                    "uid": None, "layer": active["layer"]})
    return out


def _find_target(session, lines):
    cursor = session.get("cursor")
    if cursor is None or not _STATE["snap"] or session.get("alt"):
        return None
    held = session.get("target")
    best, best_d = None, None
    radius = _px(session, SNAP_PX)
    for target in _snap_targets(session, lines):
        same = held is not None and held["uid"] == target["uid"] and held["end"] == target["end"]
        d = bezier.dist(cursor, target["at"])
        if d <= radius * (STICK if same else 1.0) and (best_d is None or d < best_d):
            best, best_d = target, d
    return best


def _record_by_uid(scene, uid):
    for record in _records(scene):
        if record.get("uid") == uid:
            return record
    return None


def _store_record(scene, record, strokes):
    """Write `record`, and drop the records of its cell whose stroke is gone."""
    records = []
    for other in _records(scene):
        if other.get("uid") == record["uid"]:
            continue
        if other.get("cell") == record["cell"] and _locate(strokes, other) is None:
            continue
        records.append(other)
    records.append(record)
    _write_records(scene, records)


def _drop_record(scene, uid):
    _write_records(scene, [r for r in _records(scene) if r.get("uid") != uid])


def _commit(view, label="Curve"):
    try:
        _animean().ui.refresh()
    except Exception:
        pass
    try:
        _animean().ui.history_commit(label, view)
    except Exception:
        pass


def _layer_id(scene, layer):
    try:
        return scene.layer_id_at(layer)
    except Exception:
        return None


def _retrace_fills(view, layer):
    """Fill layers that follow this line layer re-trace inside the same
    history entry, the way they do after a pen stroke (pyfile/fill_tool.py)."""
    try:
        import fill_tool
        fill_tool.retrace_after_edit(view, layer)
    except Exception:
        pass


def _write_line(view, scene, active, label="Curve"):
    """Put the active line into the model and its record into scriptData,
    then commit one history entry. Returns False when the line can no longer
    be written - its layer was locked, hidden or moved, or its stroke is not
    where its record says - and the session must end."""
    row, layer = active["row"], active["layer"]
    if not _layer_writable(scene, layer) or _layer_id(scene, layer) != active.get("layer_id"):
        return False
    anchors = [tuple(p) for p in active["anchors"]]
    image = scene.image_at(row, layer, True)
    if active.get("uid") is None:
        image.add_stroke_object(_make_stroke(anchors, active.get("closed", False), _pen_color(), _STATE["width"]))
        active["uid"] = uuid.uuid4().hex
        active["cell"] = _cell_key(scene, layer, row)
    else:
        previous = active.get("written")
        strokes = scene.cell_to_dict(layer, row, False, 4.0)["image"]["strokes"]
        index = _locate(strokes, previous) if previous else None
        if index is None:
            return False
        # A line carried on keeps its own colour and width.
        old = strokes[index]
        color = old.get("color") or {}
        rgba = (int(color.get("r", 0)), int(color.get("g", 0)), int(color.get("b", 0)), int(color.get("a", 255)))
        image.replace_stroke_with_pieces(index, [_make_stroke(anchors, active.get("closed", False), rgba,
                                                              float(old.get("width", _STATE["width"])))])
    record = {"uid": active["uid"], "cell": active["cell"],
              "anchors": [list(p) for p in anchors], "closed": bool(active.get("closed"))}
    _store_record(scene, record, scene.cell_to_dict(layer, row, False, 4.0)["image"]["strokes"])
    active["written"] = record
    _retrace_fills(view, layer)
    _commit(view, label)
    return True


def _interrupt(view, session):
    """End the line at its last anchor. A lone first anchor never became a
    stroke, so it simply goes."""
    session["active"] = None
    session["target"] = None


def _start_or_place(view, session, scene, row, layer, point):
    active = session.get("active")
    if active is None:
        session["active"] = {"layer": layer, "layer_id": _layer_id(scene, layer), "row": row,
                             "anchors": [point], "closed": False, "uid": None, "cell": None,
                             "written": None}
        return
    if bezier.dist(point, active["anchors"][-1]) < _px(session, MIN_GAP_PX):
        return
    active["anchors"].append(point)
    if len(active["anchors"]) >= 2 and not _write_line(view, scene, active):
        session["active"] = None


def _resume(session, scene, target, row):
    record = _record_by_uid(scene, target["uid"])
    if record is None:
        return
    anchors = [tuple(p) for p in record["anchors"]]
    if target["end"] == "head":
        anchors.reverse()       # always draw on from the tail; same curve backwards
    session["active"] = {"layer": target["layer"], "layer_id": _layer_id(scene, target["layer"]),
                         "row": row, "anchors": anchors, "closed": False, "uid": record["uid"],
                         "cell": record["cell"], "written": record}


def _merge(view, session, scene, target):
    active = session["active"]
    other = _record_by_uid(scene, target["uid"])
    if other is None:
        return
    tail = [tuple(p) for p in other["anchors"]]
    if target["end"] == "tail":
        tail.reverse()
    row, layer = active["row"], active["layer"]
    if active.get("uid") is None:
        # A lone anchor joining a line: the line grows in its own place.
        merged = dict(active, anchors=[active["anchors"][0]] + tail, uid=other["uid"],
                      cell=other["cell"], written=other)
    else:
        strokes = scene.cell_to_dict(layer, row, False, 4.0)["image"]["strokes"]
        index = _locate(strokes, other)
        if index is not None:
            scene.remove_stroke(row, layer, index)
        _drop_record(scene, other["uid"])
        merged = dict(active, anchors=active["anchors"] + tail)
    _write_line(view, scene, merged, "Join Curves")
    session["active"] = None


def _close(view, session, scene):
    active = session["active"]
    active["closed"] = True
    _write_line(view, scene, active, "Close Curve")
    session["active"] = None


def _click(view, session, scene, row, layer, point):
    target = _find_target(session, _lines_in_row(scene, row))
    session["target"] = target
    active = session.get("active")
    if active is None:
        if target is not None:
            _resume(session, scene, target, row)
            return
        if not _layer_writable(scene, layer):
            return
        _start_or_place(view, session, scene, row, layer, point)
        return
    if active["row"] != row:
        # The timeline moved under the line: its geometry belongs to the old
        # frame, so the old line ends and this click starts afresh.
        session["active"] = None
        _click(view, session, scene, row, layer, point)
        return
    if target is not None and target["kind"] == "close":
        _close(view, session, scene)
    elif target is not None and target["kind"] == "merge":
        _merge(view, session, scene, target)
    else:
        _start_or_place(view, session, scene, row, active["layer"], point)


def _layer_writable(scene, layer_index):
    try:
        for layer in scene.get_structure()["layers"]:
            if layer["index"] == layer_index:
                return _layer_drawable(layer)
    except Exception:
        pass
    return False


# --- overlay -----------------------------------------------------------------

def _diamond(p, r):
    return [(p[0], p[1] - r), (p[0] + r, p[1]), (p[0], p[1] + r), (p[0] - r, p[1])]


def _square(p, r):
    return [(p[0] - r, p[1] - r), (p[0] + r, p[1] - r), (p[0] + r, p[1] + r), (p[0] - r, p[1] + r)]


def _ring(p, r, steps=20):
    return [(p[0] + r * math.cos(2 * math.pi * k / steps), p[1] + r * math.sin(2 * math.pi * k / steps))
            for k in range(steps)]


def _preview_spans(session):
    """The spans the next click would change or add, or [] when the pointer
    offers nothing (no line in progress, or it sits on the last anchor)."""
    active = session.get("active")
    cursor = session.get("cursor")
    if active is None or cursor is None:
        return []
    anchors = list(active["anchors"])
    target = session.get("target")
    closed = False
    if target is not None and target["kind"] == "close":
        closed = True
    elif target is not None and target["kind"] == "merge":
        other = session.get("merge_preview")
        if other is None:
            return []
        anchors += other
    else:
        if bezier.dist(cursor, anchors[-1]) < _px(session, MIN_GAP_PX):
            return []   # a click here would place nothing, so nothing is previewed
        anchors.append(target["at"] if target is not None else cursor)
    spans = catmull_rom(anchors, closed)
    first_changed = max(0, len(active["anchors"]) - 2)
    return spans[first_changed:]


def _push_overlay(view, session, lines=()):
    items = []
    r_end = _px(session, 5.0)
    r_anchor = _px(session, 3.0)
    if lines:
        active_uid = (session.get("active") or {}).get("uid")
        for _layer, _index, record in lines:
            if record.get("closed") or record.get("uid") == active_uid:
                continue
            anchors = record["anchors"]
            for p in ({tuple(anchors[0]), tuple(anchors[-1])}):
                items.append({"id": "curve:end", "points": _diamond(p, r_end), "closed": True,
                              "color": END_INK, "fill_color": END_COLOR, "width": 1.2})
    spans = _preview_spans(session)
    if spans:
        items.append({"id": "curve:preview", "points": flatten(spans, 1.0),
                      "color": PREVIEW_COLOR, "width": 2.0})
    active = session.get("active")
    if active is not None:
        anchors = active["anchors"]
        for i, p in enumerate(anchors):
            if i == len(anchors) - 1:
                items.append({"id": "curve:last", "points": _square(p, r_anchor * 1.3), "closed": True,
                              "color": (255, 255, 255, 255), "fill_color": ANCHOR_INK, "width": 1.0})
            elif i == 0:
                items.append({"id": "curve:head", "points": _diamond(p, r_end), "closed": True,
                              "color": END_INK, "fill_color": END_COLOR, "width": 1.2})
            else:
                items.append({"id": "curve:anchor", "points": _square(p, r_anchor), "closed": True,
                              "color": ANCHOR_INK, "fill_color": (255, 255, 255, 255), "width": 1.0})
    target = session.get("target")
    cursor = session.get("cursor")
    if target is not None and cursor is not None:
        items.append({"id": "curve:pull", "points": [cursor, target["at"]],
                      "color": PREVIEW_COLOR, "width": 1.0})
        items.append({"id": "curve:ring", "points": _ring(target["at"], _px(session, 11.0)),
                      "closed": True, "color": PREVIEW_COLOR, "width": 2.0})
        items.append({"id": "curve:target", "points": _diamond(target["at"], _px(session, 7.0)),
                      "closed": True, "color": PREVIEW_COLOR, "fill_color": END_COLOR, "width": 1.2})
    for item in items:
        item["removable"] = False    # markers, not things the user deletes: no x badge
    try:
        overlay_stack.set_items(view, OVERLAY_OWNER, items)
    except Exception:
        pass


def _clear(view):
    _SESSIONS.pop(view, None)
    try:
        overlay_stack.set_items(view, OVERLAY_OWNER, [])
    except Exception:
        pass


# --- events ------------------------------------------------------------------

def _refresh_target(session, scene, lines):
    session["target"] = _find_target(session, lines)
    session["merge_preview"] = None
    target = session["target"]
    if target is not None and target["kind"] == "merge":
        other = _record_by_uid(scene, target["uid"])
        if other is not None:
            tail = [tuple(p) for p in other["anchors"]]
            if target["end"] == "tail":
                tail.reverse()
            session["merge_preview"] = tail


def _on_handle(message):
    view = message.get("view") or "main"
    phase = message.get("phase")
    ours = message.get("property") == PROPERTY
    if phase == "arm":
        if not ours:
            if view in _SESSIONS:
                _clear(view)
            return
        _session(view)
    elif phase == "cancel":
        if view in _SESSIONS:
            _clear(view)
        return
    if not ours:
        return
    session = _session(view)
    zoom = float(message.get("zoom") or 0.0)
    if zoom > 0.0:
        session["zoom"] = zoom
    color = message.get("color")
    if isinstance(color, dict):
        _STATE["color"] = (int(color.get("r", 0)), int(color.get("g", 0)),
                           int(color.get("b", 0)), int(color.get("a", 255)))
    session["alt"] = bool((message.get("modifiers") or {}).get("alt"))
    cell = message.get("cell") or {}
    row, layer = int(cell.get("row", 0)), int(cell.get("layer", 0))
    try:
        scene = _scene_model(view)
    except Exception:
        return
    position = message.get("position") or {}
    point = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))

    if phase in ("hover", "pick"):
        session["cursor"] = point
    elif phase == "leave":
        session["cursor"] = None
    if phase == "pick":
        _click(view, session, scene, row, layer, point)
    elif phase == "context":
        _interrupt(view, session)
    elif phase not in ("arm", "hover", "leave", "view"):
        return
    lines = _lines_in_row(scene, row)
    _refresh_target(session, scene, lines)
    _push_overlay(view, session, lines)


def _on_option(message):
    option = message.get("option") or {}
    hook = message.get("hook") or option.get("hook")
    value = option.get("value", message.get("value"))
    if hook == "pen_width":
        try:
            _STATE["width"] = max(1.0, float(value))
        except (TypeError, ValueError):
            pass
    elif hook == "curve_snap":
        _STATE["snap"] = value in (True, "on", "true", 1)


def _on_history(message):
    """Undo or redo replaced the model. A line being drawn carries on from
    whatever its record says now, in the direction it was being drawn (Ctrl+Z
    took the last anchor back); undoing the second anchor leaves the first one
    placed, as before the line became a stroke; a line that is gone ends."""
    for view, session in list(_SESSIONS.items()):
        session["target"] = None
        active = session.get("active")
        try:
            scene = _scene_model(view)
        except Exception:
            continue
        if active is not None and active.get("uid") is None:
            # A lone anchor never was in the model: an undo stepped back past
            # it (it undid something else), so it goes too.
            session["active"] = None
        elif active is not None:
            try:
                record = _record_by_uid(scene, active["uid"])
                strokes = scene.cell_to_dict(active["layer"], active["row"], False, 4.0)["image"]["strokes"]
                if record is None:
                    # The line never existed in the restored state: back to its
                    # lone first anchor.
                    active.update(anchors=active["anchors"][:1], uid=None, cell=None, written=None)
                elif record.get("closed") or _locate(strokes, record) is None:
                    session["active"] = None
                else:
                    anchors = [tuple(p) for p in record["anchors"]]
                    drawn = [tuple(p) for p in active["anchors"][:len(anchors)]]
                    backwards = list(reversed(anchors))
                    if all(bezier.dist(p, q) <= MATCH_EPS for p, q in zip(drawn, backwards)):
                        anchors = backwards   # it was being drawn on from its head
                    active.update(anchors=anchors, written=record)
            except Exception:
                session["active"] = None
        row = (session.get("active") or {}).get("row", scene.current_frame())
        _push_overlay(view, session, _lines_in_row(scene, row))


def _on_frame(message):
    """A new frame: the line in progress ends, and what is shown is this
    frame's lines."""
    row = int((message.get("cell") or {}).get("row", 0))
    for view, session in list(_SESSIONS.items()):
        session["active"] = None
        session["target"] = None
        try:
            _push_overlay(view, session, _lines_in_row(_scene_model(view), row))
        except Exception:
            pass


def register_hooks():
    python_hooks.set_hook(_on_handle, handle=True)
    python_hooks.set_hook(_on_option, option=True)
    python_hooks.set_hook(_on_history, historyrestore=True)
    python_hooks.set_hook(_on_frame, framechange=True)


def activate(name=NAME, property_value=PROPERTY):
    register_hooks()
    print("[curve_tool] Curve: left click places anchors, right click ends the line.")
    return property_value


register_hooks()
