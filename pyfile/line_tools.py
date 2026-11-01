"""Choice Line and Deform: two Mapping tools that act on the lines already there
(H/V axes, additional lines, the proxy contour) through their overlay items.

Choice Line selects a line; the 地线 tick in Tool Options turns it into a ground
line. Deform drags a stretch of a line, the pull fading along the line. Both are
Arrow gestures: the overlay item is `draggable`, C++ reports the press and the
moves through the handle events, and the geometry is rewritten here.
"""
import math

import auto_mapping as am
import mapping_preview
import python_hooks

DEFORM_RADIUS = {"value": 60.0}     # canvas px along the line
RESAMPLE_STEP = 4.0

_DRAG = {}


def _animean():
    import animean_python
    return animean_python


def selected_title():
    """What Tool Options should call the selected line."""
    choice = am.chosen_line()
    return am.line_label(choice) if choice else None


def _select(overlay_id):
    am.choose_line(overlay_id)
    am.refresh_line_overlays()
    try:
        _animean().ui.refresh_tool_options()
        _animean().ui.refresh()
    except Exception:
        pass


def _densified(points, closed):
    source = [(float(p[0]), float(p[1])) for p in points]
    if closed:
        source.append(source[0])
    out = [source[0]]
    for a, b in zip(source, source[1:]):
        steps = max(1, int(math.ceil(math.hypot(b[0] - a[0], b[1] - a[1]) / RESAMPLE_STEP)))
        out.extend((a[0] + (b[0] - a[0]) * i / steps, a[1] + (b[1] - a[1]) * i / steps)
                   for i in range(1, steps + 1))
    return out[:-1] if closed else out


def _arcs(points, closed):
    cum = [0.0]
    for a, b in zip(points, points[1:]):
        cum.append(cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = cum[-1]
    if closed:
        total += math.hypot(points[0][0] - points[-1][0], points[0][1] - points[-1][1])
    return cum, total


def _falloff(cum, total, closed, centre, radius):
    weights = []
    for arc in cum:
        gap = abs(arc - centre)
        if closed:
            gap = min(gap, total - gap)
        weights.append(math.exp(-0.5 * (gap / radius) ** 2))
    return weights


def _station_weights(stations, points, weights):
    """Each paired station takes the pull of the contour point it sits on."""
    return [weights[min(range(len(points)), key=lambda i: (points[i][0] - s[0]) ** 2
                                                          + (points[i][1] - s[1]) ** 2)]
            for s in stations]


def _pulled(points, weights, dx, dy):
    return [[p[0] + w * dx, p[1] + w * dy] for p, w in zip(points, weights)]


def _deform(message, overlay_id, view):
    phase = message.get("phase")
    position = message.get("position") or {}
    point = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
    if phase == "press":
        item = am.line_item(view, overlay_id)
        if item is None or len(item.get("points") or []) < 2:
            return
        closed = overlay_id == am.PROXY_PROPERTY
        points = _densified(item["points"], closed)
        cum, total = _arcs(points, closed)
        nearest = min(range(len(points)), key=lambda i: math.hypot(
            points[i][0] - point[0], points[i][1] - point[1]))
        weights = _falloff(cum, total, closed, cum[nearest], DEFORM_RADIUS["value"])
        stations = (item.get(am.PAIRING_KEY) or {}).get("stations")
        mapping_preview.begin(view, overlay_id)
        _DRAG[view] = {
            "id": overlay_id, "origin": point, "points": points, "moved": False,
            "saved": dict(item), "weights": weights, "stations": stations,
            "station_weights": stations and _station_weights(stations, points, weights),
        }
        return
    drag = _DRAG.get(view)
    if drag is None or drag["id"] != overlay_id:
        return
    if phase == "cancel":
        _DRAG.pop(view, None)
        am.restore_line_item(view, overlay_id, drag["saved"])
        mapping_preview.cancel()
        return
    if phase not in ("move", "release"):
        return
    dx, dy = point[0] - drag["origin"][0], point[1] - drag["origin"][1]
    if abs(dx) > 1e-9 or abs(dy) > 1e-9:
        drag["moved"] = True
    if not drag["moved"]:
        if phase == "release":
            _DRAG.pop(view, None)
            mapping_preview.end()
        return
    moved = _pulled(drag["points"], drag["weights"], dx, dy)
    stations = drag["stations"] and _pulled(drag["stations"], drag["station_weights"], dx, dy)
    if phase == "move":
        am.replace_line_points(view, overlay_id, moved, settled=False, stations=stations)
        return
    _DRAG.pop(view, None)
    am.replace_line_points(view, overlay_id, moved, settled=True, stations=stations)
    mapping_preview.end()


def _on_handle(message):
    prop = message.get("property")
    if message.get("phase") == "arm":
        am.arm_line_tools(prop in am.LINE_TOOL_PROPERTIES)
        return
    if prop not in am.LINE_TOOL_PROPERTIES:
        return
    overlay_id = str(message.get("handle") or "")
    if not am.is_line(overlay_id):
        return
    if prop == am.CHOICE_PROPERTY:
        if message.get("phase") == "press":
            _select(overlay_id)
        return
    _deform(message, overlay_id, message.get("view") or "main")


def _on_option(cell, stroke, message):
    hook = message.get("hook")
    if hook == "ground_line":
        choice = am.chosen_line()
        if choice:
            am.set_line_ground(choice, str(message.get("value", "")).lower() == "on")
        else:
            print("[line_tools] choose a line first")
        try:
            _animean().ui.refresh_tool_options()
        except Exception:
            pass
    elif hook == "deform_radius":
        try:
            DEFORM_RADIUS["value"] = max(5.0, float(message.get("value", 60)))
        except (TypeError, ValueError):
            pass


def _history_restored(cell, stroke, message):
    """Undo replaced the line a drag was rewriting: the drag is over."""
    _DRAG.pop(message.get("view") or "main", None)


def register_hooks():
    python_hooks.set_hook(_on_handle, handle=True)
    python_hooks.set_hook(_on_option, option=True)
    python_hooks.set_hook(_history_restored, historyrestore=True)


def activate_choice_line(name="choice_line", property_value=am.CHOICE_PROPERTY):
    am.register_hooks()
    register_hooks()
    am.arm_line_tools(True)
    print("[line_tools] Choice Line: click an axis, an additional line or the contour.")
    return property_value


def activate_deform(name="deform_line", property_value=am.DEFORM_PROPERTY):
    am.register_hooks()
    register_hooks()
    am.arm_line_tools(True)
    print("[line_tools] Deform: drag a line, the pull fades along it.")
    return property_value


register_hooks()
