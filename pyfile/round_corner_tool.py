"""Round Corner tool: pull a sharp corner of a line into a smooth rounding.

The pointer snaps to a corner (a joint where the line's tangent breaks). Press
and drag pulls a CONTROL LINE out of the corner (user 2026-11-02): the longer
the line, the wider and smoother the rounding; turning it sets the tangent at
the top of the rounding, which decides how much each side gives. The end the
user holds stays under the pointer. Dragging back to the tip snaps there and
makes the corner sharp again; dragging back to where the drag started snaps
there and gives the rounding back as it was. Shift: both sides equal. A right
click during the drag cancels it. The behaviour was settled in a browser demo
with the user, see docs/plan/2026-11-02-线条与圆角工具/.

MODEL - a fillet at joint j is two arc lengths: it starts L1 back along the
incoming segment and ends L2 along the outgoing one. The legs are cut with
de Casteljau (pyfile/bezier.py, the shared wheel - never refit), and a cubic
joins the cuts, tangent to both legs, each handle kappa times its cut length.
The control line is the chord between the two cuts, pinned at the corner tip:
its ends are V +- chord/2, and on straight legs it is parallel to the tangent
at the rounding's top. Pinned at the tip, not the top, because the top sinks
as the rounding grows - a line riding on it either left the pointer or swung
the rounding hard to one side (tried 2026-11-02). Straight legs have a closed
form for "the held end is on the pointer"; curved legs refine it with
Levenberg-Marquardt, warm-started from the moment before.

STROKES - the rounded line is written back into the stroke in place (draw
order, id, colour, width, property and pen style kept); a drag commits one
history entry on release. So a rounded corner stays adjustable, the stroke's
unrounded segments and its fillets are remembered in the scene's scriptData
under STORE_KEY, keyed by the cell's (asset_index, frame_id) and matched to
the stroke by geometry: the stroke whose path runs through the recorded
result. A stroke changed by anything else no longer matches, and its corners
are then simply the corners it has now.

Hand-and-eye sizes are SCREEN pixels through pyfile/viewscale.py; geometry is
canvas pixels.
"""
import math
import uuid

import bezier
import overlay_stack
import python_hooks
import script_store
import viewscale

NAME = "round_corner"
PROPERTY = "round_corner_tool"
STORE_KEY = "round_corner"
OVERLAY_OWNER = "round_corner_tool"

CORNER_DEG = 15.0       # a tangent break above this is a corner the tool snaps to
GAP = 0.5               # canvas px kept between two fillets that cut one segment
SAMPLES = 48            # arc-length table per segment
SNAP_PX = 16.0          # screen px
STICK = 1.35
DRAG_START_PX = 2.0     # screen px a press must travel before it changes anything
NEAR_PX = 120.0         # screen px: corners closer than this to the pointer are marked
POLY_STEP = 2.0
MATCH_EPS = 0.01
EPS = 1e-9

FOCUS = (61, 142, 201, 255)
END_COLOR = (230, 168, 23, 255)
END_INK = (138, 98, 0, 255)
DIM = (150, 152, 158, 255)
WHITE = (255, 255, 255, 255)
DASH = 2                # Qt DashLine

_STATE = {"kappa": 0.55}
_SESSIONS = {}


def _animean():
    import animean_python
    return animean_python


def options_state():
    return dict(_STATE)


# --- vectors -----------------------------------------------------------------

def _sub(a, b):
    return (a[0] - b[0], a[1] - b[1])


def _add(a, b):
    return (a[0] + b[0], a[1] + b[1])


def _mul(a, k):
    return (a[0] * k, a[1] * k)


def _dot(a, b):
    return a[0] * b[0] + a[1] * b[1]


def _cross(a, b):
    return a[0] * b[1] - a[1] * b[0]


def _len(a):
    return math.hypot(a[0], a[1])


def _unit(a):
    n = _len(a)
    return (a[0] / n, a[1] / n) if n > EPS else None


# --- paths -------------------------------------------------------------------

def _pt(d):
    return (float(d["x"]), float(d["y"]))


def segments_of(commands):
    """(cubics, closed) for a single-subpath path, lines and quads raised to
    cubics (same shape), or None for anything else."""
    cubics, start, cur, closed = [], None, None, False
    for command in commands or []:
        kind = command.get("type")
        if kind == "move":
            if start is not None:
                return None
            start = cur = _pt(command["to"])
        elif cur is None:
            return None
        elif kind == "line":
            to = _pt(command["to"])
            cubics.append(bezier.line_cubic(cur, to))
            cur = to
        elif kind == "quad":
            to = _pt(command["to"])
            cubics.append(bezier.quad_cubic(cur, _pt(command["control"]), to))
            cur = to
        elif kind == "cubic":
            to = _pt(command["to"])
            cubics.append((cur, _pt(command["control1"]), _pt(command["control2"]), to))
            cur = to
        elif kind == "close":
            if bezier.dist(cur, start) > EPS:
                cubics.append(bezier.line_cubic(cur, start))
                cur = start
            closed = True
        else:
            return None
    if not cubics:
        return None
    if not closed and len(cubics) >= 2 and bezier.dist(cur, start) <= 1e-6:
        closed = True
    return [tuple(map(tuple, c)) for c in cubics], closed


def _lengths(cub):
    acc, prev = [0.0], cub[0]
    for i in range(1, SAMPLES + 1):
        p = bezier.eval_cubic(cub, i / SAMPLES)
        acc.append(acc[-1] + bezier.dist(prev, p))
        prev = p
    return acc


def _t_at(acc, length):
    if length <= 0.0:
        return 0.0
    total = acc[-1]
    if length >= total:
        return 1.0
    i = 1
    while acc[i] < length:
        i += 1
    return (i - 1 + (length - acc[i - 1]) / (acc[i] - acc[i - 1])) / SAMPLES


def _start_dir(c):
    return _unit(_sub(c[1], c[0])) or _unit(_sub(c[2], c[0])) or _unit(_sub(c[3], c[0])) or (1.0, 0.0)


def _end_dir(c):
    return _unit(_sub(c[3], c[2])) or _unit(_sub(c[3], c[1])) or _unit(_sub(c[3], c[0])) or (1.0, 0.0)


def _dir_at(c, t):
    return _unit(bezier.cubic_derivative(c, t)) or (_start_dir(c) if t < 0.5 else _end_dir(c))


def corner_joints(cubics, closed):
    """Joint indices where the tangent breaks by more than CORNER_DEG. Joint j
    joins segment j to segment j + 1 (the closing joint wraps)."""
    n = len(cubics)
    joints = n if closed else n - 1
    out = []
    for j in range(joints):
        d1, d2 = _end_dir(cubics[j]), _start_dir(cubics[(j + 1) % n])
        turn = math.degrees(math.acos(max(-1.0, min(1.0, _dot(d1, d2)))))
        if turn > CORNER_DEG:
            out.append(j)
    return out


# --- the rounded line --------------------------------------------------------

def _corner(cubics, closed, rounds, j, accs, totals):
    n = len(cubics)
    joints = n if closed else n - 1
    prev_joint = lambda i: i - 1 if i > 0 else (n - 1 if closed else -1)
    head_cut = lambda i: (rounds.get(prev_joint(i)) or {}).get("L2", 0.0)
    tail_cut = lambda i: (rounds.get(i) or {}).get("L1", 0.0) if i < joints else 0.0
    nxt = (j + 1) % n
    a, b = cubics[j], cubics[nxt]
    return {
        "joint": j, "V": a[3], "u1": _mul(_end_dir(a), -1.0), "u2": _start_dir(b),
        "seg1": a, "acc1": accs[j], "seg2": b, "acc2": accs[nxt],
        "avail1": totals[j] - head_cut(j) - GAP, "avail2": totals[nxt] - tail_cut(nxt) - GAP,
    }


def fillet_at(c, L1, L2, kappa):
    t1 = _t_at(c["acc1"], c["acc1"][-1] - L1)
    t2 = _t_at(c["acc2"], L2)
    p1, p2 = bezier.eval_cubic(c["seg1"], t1), bezier.eval_cubic(c["seg2"], t2)
    return (p1, _add(p1, _mul(_dir_at(c["seg1"], t1), kappa * L1)),
            _sub(p2, _mul(_dir_at(c["seg2"], t2), kappa * L2)), p2)


def rounded(cubics, closed, rounds):
    """{"segs": the line's cubics with every fillet in, "corners": {joint:
    corner}} - corners carry the tip, the leg directions, how much each leg
    can give, and for a rounded one its top, cuts and control-line ends."""
    n = len(cubics)
    joints = n if closed else n - 1
    accs = [_lengths(c) for c in cubics]
    totals = [a[-1] for a in accs]
    prev_joint = lambda i: i - 1 if i > 0 else (n - 1 if closed else -1)
    trimmed = []
    for i, cub in enumerate(cubics):
        c0 = (rounds.get(prev_joint(i)) or {}).get("L2", 0.0)
        c1 = (rounds.get(i) or {}).get("L1", 0.0) if i < joints else 0.0
        if c0 <= 0.0 and c1 <= 0.0:
            trimmed.append(cub)
        else:
            # The shared wheel cuts the leg exactly (pyfile/bezier.py).
            trimmed.append(tuple(bezier.cubic_segment(cub, _t_at(accs[i], c0), _t_at(accs[i], totals[i] - c1))))
    segs, corners = [], {}
    wanted = set(corner_joints(cubics, closed)) | set(rounds)
    for i in range(n):
        segs.append(trimmed[i])
        if i >= joints or i not in wanted:
            continue
        c = _corner(cubics, closed, rounds, i, accs, totals)
        r = rounds.get(i)
        if r:
            fillet = fillet_at(c, r["L1"], r["L2"], r.get("k", _STATE["kappa"]))
            segs.append(fillet)
            c["top"] = bezier.eval_cubic(fillet, 0.5)
            c["cuts"] = (fillet[0], fillet[3])
            half = _mul(_sub(fillet[3], fillet[0]), 0.5)
            c["knobs"] = ((1, _add(c["V"], half)), (-1, _sub(c["V"], half)))
        corners[i] = c
    return {"segs": segs, "corners": corners}


def _knob(c, L1, L2, s):
    p1 = bezier.eval_cubic(c["seg1"], _t_at(c["acc1"], c["acc1"][-1] - L1))
    p2 = bezier.eval_cubic(c["seg2"], _t_at(c["acc2"], L2))
    return _add(c["V"], _mul(_sub(p2, p1), s / 2.0))


def legs_for(c, m, warm=None, shift=False):
    """{"L1", "L2", "s"}: the fillet whose control-line end `s` sits on m.

    Straight legs: knob - V = s (L1 e1 + L2 e2) / 2 with e1 = -u1, e2 = u2.
    The line has no direction, so m on either side of the corner works and s
    says which end is held. A pointer outside what the legs allow leans the
    rounding fully onto the nearer leg. Curved legs refine the closed form
    (and the legs a moment ago, when dragging) on the real knob.
    """
    D = _sub(m, c["V"])
    if _len(D) < EPS:
        return {"L1": 0.0, "L2": 0.0, "s": 1}
    e1, e2 = _mul(c["u1"], -1.0), c["u2"]
    det = _cross(e1, e2)
    clamp1 = lambda v: min(max(0.0, v), c["avail1"])
    clamp2 = lambda v: min(max(0.0, v), c["avail2"])
    if shift or abs(det) < 0.05:
        w = _add(e1, e2)
        s = 1 if _dot(D, w) >= 0.0 else -1
        L = min(clamp1(2.0 * abs(_dot(D, w)) / max(_dot(w, w), EPS)),
                clamp2(2.0 * abs(_dot(D, w)) / max(_dot(w, w), EPS)))
        for _ in range(8):
            f = _knob(c, L, L, s)
            dF = _mul(_sub(_knob(c, L + 0.5, L + 0.5, s), f), 2.0)
            dd = _dot(dF, dF)
            if dd < 1e-12:
                break
            step = _dot(_sub(m, f), dF) / dd
            L = min(clamp1(L + step), clamp2(L + step))
            if abs(step) < 0.05:
                break
        return {"L1": L, "L2": L, "s": s}
    a, b = _cross(D, e2) / det, _cross(e1, D) / det
    if a >= 0.0 and b >= 0.0:
        s = 1
    elif a <= 0.0 and b <= 0.0:
        a, b, s = -a, -b, -1
    else:
        t1, t2 = _dot(D, e1), _dot(D, e2)
        if abs(t1) >= abs(t2):
            a, b, s = abs(t1), 0.0, (-1 if t1 < 0 else 1)
        else:
            a, b, s = 0.0, abs(t2), (-1 if t2 < 0 else 1)
    best = _refine(c, m, s, clamp1(2.0 * a), clamp2(2.0 * b))
    if warm and warm.get("s") == s:
        other = _refine(c, m, s, clamp1(warm["L1"]), clamp2(warm["L2"]))
        if other[2] < best[2]:
            best = other
    return {"L1": best[0], "L2": best[1], "s": s}


def _refine(c, m, s, L1, L2):
    """Levenberg-Marquardt on |knob - m|; an unreachable pointer leaves the
    knob at the closest point it can reach."""
    clamp1 = lambda v: min(max(0.0, v), c["avail1"])
    clamp2 = lambda v: min(max(0.0, v), c["avail2"])
    f = _knob(c, L1, L2, s)
    err = _len(_sub(f, m))
    lam = 1e-3
    for _ in range(30):
        if err < 0.05:
            break
        F = _sub(f, m)
        j1 = _mul(_sub(_knob(c, L1 + 0.5, L2, s), f), 2.0)
        j2 = _mul(_sub(_knob(c, L1, L2 + 0.5, s), f), 2.0)
        a11, a12, a22 = _dot(j1, j1) + lam, _dot(j1, j2), _dot(j2, j2) + lam
        g1, g2 = _dot(j1, F), _dot(j2, F)
        det = a11 * a22 - a12 * a12
        if abs(det) < 1e-12:
            break
        n1 = clamp1(L1 - (a22 * g1 - a12 * g2) / det)
        n2 = clamp2(L2 - (a11 * g2 - a12 * g1) / det)
        nf = _knob(c, n1, n2, s)
        ne = _len(_sub(nf, m))
        if ne < err:
            gain = err - ne
            L1, L2, f, err = n1, n2, nf, ne
            lam /= 3.0
            if gain < 1e-4:
                break
        else:
            lam *= 4.0
            if lam > 1e6:
                break
    return L1, L2, err


# --- strokes and records -----------------------------------------------------

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
    items = data.get("strokes") if isinstance(data, dict) else None
    return [dict(r) for r in items] if isinstance(items, list) else []


def _write_records(scene, records):
    script_store.write(scene, STORE_KEY, {"strokes": records} if records else None)


def _ends(segs):
    return [list(segs[0][0])] + [list(c[3]) for c in segs]


def _matches_record(cubics, record):
    result = record.get("result") or []
    ends = [cubics[0][0]] + [c[3] for c in cubics]
    if len(ends) != len(result):
        return False
    return all(bezier.dist(p, q) <= MATCH_EPS for p, q in zip(ends, result))


def _rounds_from(record):
    return {int(k): dict(v) for k, v in (record.get("rounds") or {}).items()}


def _layer_drawable(layer):
    return layer["visible"] and not layer.get("locked") and layer["type"] != "fill"


def _lines(scene, row):
    """Every line on `row` the tool may round: [{layer, index, stroke, base,
    closed, rounds, record}]. Strokes that carry a property (guide lines of
    other tools) are left alone."""
    records = _records(scene)
    out = []
    for layer in scene.get_structure()["layers"]:
        if not _layer_drawable(layer):
            continue
        cell = scene.cell_to_dict(layer["index"], row, False, 4.0)
        key = [int(cell.get("asset_index", -1)), int(cell.get("frame_id", -1))]
        mine = [r for r in records if r.get("cell") == key]
        for index, stroke in enumerate(cell["image"]["strokes"]):
            if stroke.get("property"):
                continue
            parsed = segments_of(stroke.get("commands"))
            if parsed is None:
                continue
            cubics, closed = parsed
            record = next((r for r in mine if _matches_record(cubics, r)), None)
            if record is not None:
                base = [tuple(tuple(p) for p in c) for c in record["base"]]
                closed, rounds = bool(record.get("closed")), _rounds_from(record)
            else:
                base, rounds = cubics, {}
            out.append({"layer": layer["index"], "index": index, "stroke": stroke, "cell": key,
                        "base": base, "closed": closed, "rounds": rounds, "record": record})
    return out


def _grips(lines):
    """What the pointer can take hold of: a sharp corner by its tip (s 0), a
    rounded one by either end of its control line (s +-1)."""
    out = []
    for line in lines:
        cubics, closed, rounds = line["base"], line["closed"], line["rounds"]
        joints = set(corner_joints(cubics, closed)) | set(rounds)
        if not joints:
            continue
        if rounds:
            view = rounded(cubics, closed, rounds)
            for j, c in view["corners"].items():
                if j in rounds:
                    for s, at in c["knobs"]:
                        out.append({"line": line, "joint": j, "s": s, "at": at, "V": c["V"], "top": c["top"]})
                else:
                    out.append({"line": line, "joint": j, "s": 0, "at": c["V"], "V": c["V"]})
        else:
            n = len(cubics)
            for j in joints:
                out.append({"line": line, "joint": j, "s": 0, "at": cubics[j][3], "V": cubics[j][3]})
    return out


def _stroke_object(line, segs):
    stroke = line["stroke"]
    color = stroke.get("color") or {}
    commands = [{"type": "move", "to": {"x": segs[0][0][0], "y": segs[0][0][1]}}]
    for a, c1, c2, b in segs:
        commands.append({"type": "cubic", "from": {"x": a[0], "y": a[1]},
                         "control1": {"x": c1[0], "y": c1[1]}, "control2": {"x": c2[0], "y": c2[1]},
                         "to": {"x": b[0], "y": b[1]}})
    flat = [segs[0][0]]
    for cub in segs:
        k = max(1, min(256, int(math.ceil(bezier.hull_length(cub) / POLY_STEP))))
        flat.extend(bezier.eval_cubic(cub, i / k) for i in range(1, k + 1))
    obj = _animean().vectorlogic.make_stroke_object_from_path(
        commands, flat,
        (int(color.get("r", 0)), int(color.get("g", 0)), int(color.get("b", 0)), int(color.get("a", 255))),
        float(stroke.get("width", 3.0)), int(stroke.get("id", 0)))
    obj.pen_style = int(stroke.get("pen_style", 1))
    return obj


def _original_object(line):
    """The stroke exactly as it was before the drag began."""
    stroke = line["stroke"]
    color = stroke.get("color") or {}
    points = [(float(p["x"]), float(p["y"])) for p in stroke.get("raw_points") or []]
    obj = _animean().vectorlogic.make_stroke_object_from_path(
        stroke["commands"], points,
        (int(color.get("r", 0)), int(color.get("g", 0)), int(color.get("b", 0)), int(color.get("a", 255))),
        float(stroke.get("width", 3.0)), int(stroke.get("id", 0)))
    obj.pen_style = int(stroke.get("pen_style", 1))
    return obj


# --- session -----------------------------------------------------------------

def _session(view):
    return _SESSIONS.setdefault(view, {"cursor": None, "target": None, "drag": None,
                                       "zoom": 1.0, "cache": None})


def _px(session, screen_px):
    return viewscale.to_canvas_length(screen_px, max(0.05, session.get("zoom") or 1.0))


def _cached_lines(session, scene, row):
    cache = session.get("cache")
    if cache is None or cache["row"] != row:
        cache = {"row": row, "lines": _lines(scene, row)}
        cache["grips"] = _grips(cache["lines"])
        session["cache"] = cache
    return cache


def _find_target(session, grips):
    cursor = session.get("cursor")
    if cursor is None:
        return None
    held = session.get("target")
    best, best_d = None, None
    radius = _px(session, SNAP_PX)
    for g in grips:
        same = (held is not None and held["line"]["index"] == g["line"]["index"]
                and held["line"]["layer"] == g["line"]["layer"]
                and held["joint"] == g["joint"] and held["s"] == g["s"])
        d = bezier.dist(cursor, g["at"])
        if d <= radius * (STICK if same else 1.0) and (best_d is None or d < best_d):
            best, best_d = g, d
    return best


def _begin(session, target):
    line = target["line"]
    rounds = {j: dict(r) for j, r in line["rounds"].items()}
    view = rounded(line["base"], line["closed"], rounds)
    corner = view["corners"][target["joint"]]
    start = rounds.get(target["joint"])
    stops = [{"kind": "tip", "at": corner["V"]}]
    if start:
        stops.append({"kind": "start", "at": target["at"]})
    for stop in stops:
        stop["armed"] = bezier.dist(stop["at"], target["at"]) > _px(session, SNAP_PX)
    session["drag"] = {"line": line, "joint": target["joint"], "corner": corner, "rounds": rounds,
                       "start": dict(start) if start else None, "grab": target["at"],
                       "down": session["cursor"], "moved": False, "changed": False,
                       "stops": stops, "snap": None}


def _apply(view, scene, drag):
    line = drag["line"]
    out = rounded(line["base"], line["closed"], drag["rounds"])
    image = scene.image_at(_row_of(drag), line["layer"], True)
    image.replace_stroke_with_pieces(line["index"], [_stroke_object(line, out["segs"])])
    drag["result"] = out["segs"]
    drag["changed"] = True
    try:
        _animean().ui.refresh()
    except Exception:
        pass


def _row_of(drag):
    return drag["row"]


def _move(view, session, scene, point, shift):
    drag = session["drag"]
    if not drag["moved"] and bezier.dist(point, drag["down"]) < _px(session, DRAG_START_PX):
        return
    drag["moved"] = True
    m = _add(drag["grab"], _sub(point, drag["down"]))
    drag["snap"] = None
    radius = _px(session, SNAP_PX)
    for stop in drag["stops"]:
        d = bezier.dist(m, stop["at"])
        if d > radius:
            stop["armed"] = True
        elif stop["armed"] and (drag["snap"] is None or d < bezier.dist(m, drag["snap"]["at"])):
            drag["snap"] = stop
    joint = drag["joint"]
    if drag["snap"] is not None and drag["snap"]["kind"] == "tip":
        drag["rounds"].pop(joint, None)
    elif drag["snap"] is not None:
        drag["rounds"][joint] = dict(drag["start"])
    else:
        legs = legs_for(drag["corner"], m, drag["rounds"].get(joint), shift)
        if legs["L1"] < GAP and legs["L2"] < GAP:
            drag["rounds"].pop(joint, None)
        else:
            legs["k"] = _STATE["kappa"]
            drag["rounds"][joint] = legs
    _apply(view, scene, drag)


def _finish(view, session, scene):
    drag = session.pop("drag", None)
    session["cache"] = None
    if drag is None or not drag["changed"]:
        return
    line = drag["line"]
    unchanged = (drag["snap"] is not None and drag["snap"]["kind"] == "start") or \
                (drag["snap"] is not None and drag["start"] is None)
    if unchanged:
        # Back where it began: put the stroke back exactly, nothing to record.
        scene.image_at(drag["row"], line["layer"], True).replace_stroke_with_pieces(
            line["index"], [_original_object(line)])
        _refresh()
        return
    records = [r for r in _records(scene) if r is not line["record"]
               and not (line["record"] and r.get("uid") == line["record"].get("uid"))]
    if drag["rounds"]:
        records.append({"uid": (line["record"] or {}).get("uid") or uuid.uuid4().hex,
                        "cell": line["cell"], "closed": line["closed"],
                        "base": [[list(p) for p in c] for c in line["base"]],
                        "rounds": {str(j): r for j, r in drag["rounds"].items()},
                        "result": _ends(drag["result"])})
    _write_records(scene, records)
    _refresh()
    try:
        _animean().ui.history_commit("Round Corner", view)
    except Exception:
        pass


def _cancel(view, session, scene):
    drag = session.pop("drag", None)
    session["cache"] = None
    if drag is None or not drag["changed"]:
        return
    line = drag["line"]
    scene.image_at(drag["row"], line["layer"], True).replace_stroke_with_pieces(
        line["index"], [_original_object(line)])
    _refresh()


def _refresh():
    try:
        _animean().ui.refresh()
    except Exception:
        pass


# --- overlay -----------------------------------------------------------------

def _diamond(p, r):
    return [(p[0], p[1] - r), (p[0] + r, p[1]), (p[0], p[1] + r), (p[0] - r, p[1])]


def _ring(p, r, steps=20):
    return [(p[0] + r * math.cos(2 * math.pi * k / steps), p[1] + r * math.sin(2 * math.pi * k / steps))
            for k in range(steps)]


def _square(p, r):
    return [(p[0] - r, p[1] - r), (p[0] + r, p[1] - r), (p[0] + r, p[1] + r), (p[0] - r, p[1] + r)]


def _control_items(session, corner, s, live):
    half = _mul(_sub(corner["cuts"][1], corner["cuts"][0]), 0.5 * (s or 1))
    near, far = _add(corner["V"], half), _sub(corner["V"], half)
    ink = FOCUS if live else DIM
    r = _px(session, 3.5 if live else 3.0)
    items = [{"id": "round:line", "points": [far, near], "color": ink, "width": 1.5 if live else 1.0},
             {"id": "round:far", "points": _ring(far, r, 12), "closed": True, "color": ink,
              "fill_color": WHITE, "width": 1.2}]
    items.append({"id": "round:near", "points": _ring(near, r * (1.2 if live else 1.0), 12), "closed": True,
                  "color": WHITE if live else ink, "fill_color": ink if live else WHITE, "width": 1.2})
    return items


def _push_overlay(view, session):
    items = []
    cursor = session.get("cursor")
    drag = session.get("drag")
    cache = session.get("cache")
    if drag is not None:
        line = drag["line"]
        live = rounded(line["base"], line["closed"], drag["rounds"])["corners"].get(drag["joint"])
        if live is not None and "cuts" in live:
            items.append({"id": "round:ghost", "points": [live["cuts"][0], live["V"], live["cuts"][1]],
                          "color": DIM, "width": 1.0, "pen_style": DASH})
            for p in live["cuts"]:
                items.append({"id": "round:cut", "points": _square(p, _px(session, 3.0)), "closed": True,
                              "color": FOCUS, "fill_color": FOCUS, "width": 1.0})
            items.extend(_control_items(session, live, drag["rounds"][drag["joint"]].get("s", 1), True))
        for stop in drag["stops"]:
            snapped = stop is drag.get("snap")
            if stop["kind"] == "tip":
                items.append({"id": "round:stop", "points": _diamond(stop["at"], _px(session, 6.5 if snapped else 5.0)),
                              "closed": True, "color": FOCUS if snapped else DIM,
                              "fill_color": END_COLOR if snapped else WHITE, "width": 1.2})
            else:
                items.append({"id": "round:stop", "points": _ring(stop["at"], _px(session, 4.5 if snapped else 4.0), 12),
                              "closed": True, "color": FOCUS if snapped else DIM,
                              "fill_color": FOCUS if snapped else WHITE, "width": 1.2})
            if snapped:
                items.append({"id": "round:snap", "points": _ring(stop["at"], _px(session, 11.0)),
                              "closed": True, "color": FOCUS, "width": 2.0})
    elif cache is not None and cursor is not None:
        near = _px(session, NEAR_PX)
        target = session.get("target")
        shown = set()
        for g in cache["grips"]:
            if bezier.dist(g["V"], cursor) > near:
                continue
            key = (g["line"]["layer"], g["line"]["index"], g["joint"])
            if key in shown:
                continue
            shown.add(key)
            is_target = target is not None and (target["line"]["layer"], target["line"]["index"],
                                                target["joint"]) == key
            if g["s"]:
                line = g["line"]
                corner = rounded(line["base"], line["closed"], line["rounds"])["corners"][g["joint"]]
                s = target["s"] if is_target else line["rounds"][g["joint"]].get("s", 1)
                items.extend(_control_items(session, corner, s, is_target))
                items.append({"id": "round:top", "points": _ring(corner["top"], _px(session, 3.0), 10),
                              "closed": True, "color": END_INK, "fill_color": END_COLOR, "width": 1.0})
            elif not is_target:
                items.append({"id": "round:corner", "points": _diamond(g["V"], _px(session, 5.0)),
                              "closed": True, "color": END_INK, "fill_color": END_COLOR, "width": 1.2})
        if target is not None:
            items.append({"id": "round:pull", "points": [cursor, target["at"]], "color": FOCUS, "width": 1.0})
            items.append({"id": "round:ring", "points": _ring(target["at"], _px(session, 11.0)),
                          "closed": True, "color": FOCUS, "width": 2.0})
            if not target["s"]:
                items.append({"id": "round:target", "points": _diamond(target["at"], _px(session, 6.5)),
                              "closed": True, "color": FOCUS, "fill_color": END_COLOR, "width": 1.2})
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

def _on_handle(message):
    view = message.get("view") or "main"
    phase = message.get("phase")
    ours = message.get("property") == PROPERTY
    if phase in ("arm", "cancel") and not (phase == "arm" and ours):
        session = _SESSIONS.get(view)
        if session is not None:
            try:
                if session.get("drag") is not None:
                    _cancel(view, session, _scene_model(view))
            except Exception:
                pass
            _clear(view)
        return
    if not ours:
        return
    session = _session(view)
    zoom = float(message.get("zoom") or 0.0)
    if zoom > 0.0:
        session["zoom"] = zoom
    cell = message.get("cell") or {}
    row = int(cell.get("row", 0))
    position = message.get("position") or {}
    point = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
    shift = bool((message.get("modifiers") or {}).get("shift"))
    try:
        scene = _scene_model(view)
    except Exception:
        return

    if phase == "arm":
        session["cache"] = None
        return
    if phase == "leave":
        session["cursor"] = None
        session["target"] = None
    elif phase == "hover":
        session["cursor"] = point
        session["target"] = _find_target(session, _cached_lines(session, scene, row)["grips"])
    elif phase == "pick":
        session["cursor"] = point
        session["cache"] = None
        target = _find_target(session, _cached_lines(session, scene, row)["grips"])
        session["target"] = target
        if target is not None:
            _begin(session, target)
            session["drag"]["row"] = row
            message["grab"] = "round:drag"
    elif phase == "move":
        if session.get("drag") is not None and message.get("handle") == "round:drag":
            session["cursor"] = point
            _move(view, session, scene, point, shift)
    elif phase == "release":
        if session.get("drag") is not None and message.get("handle") == "round:drag":
            _finish(view, session, scene)
            session["cursor"] = point
            session["target"] = _find_target(session, _cached_lines(session, scene, row)["grips"])
    elif phase == "context":
        if session.get("drag") is not None:
            _cancel(view, session, scene)
            session["cursor"] = point
            session["target"] = _find_target(session, _cached_lines(session, scene, row)["grips"])
    else:
        return
    _push_overlay(view, session)


def _on_option(message):
    option = message.get("option") or {}
    hook = message.get("hook") or option.get("hook")
    if hook != "round_shape":
        return
    try:
        _STATE["kappa"] = max(0.2, min(0.9, float(option.get("value", message.get("value"))) / 100.0))
    except (TypeError, ValueError):
        pass


def _model_changed(*_args):
    for session in _SESSIONS.values():
        session["cache"] = None
        session["target"] = None


def _on_history(message):
    for session in _SESSIONS.values():
        session["drag"] = None
        session["cache"] = None
        session["target"] = None


def register_hooks():
    python_hooks.set_hook(_on_handle, handle=True)
    python_hooks.set_hook(_on_option, option=True)
    python_hooks.set_hook(_on_history, historyrestore=True)
    python_hooks.set_hook(_model_changed, linefinish=True, erasefinish=True, deletefinish=True,
                          fillfinish=True, movefinish=True, framechange=True, layerchange=True)


def activate(name=NAME, property_value=PROPERTY):
    register_hooks()
    print("[round_corner_tool] Round Corner: drag a sharp corner to round it.")
    return property_value


register_hooks()
