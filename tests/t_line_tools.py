"""The Mapping tools on lines (issue #9, #12): Free Out Line's contour pairing and
axes, Choice Line's 地线 tick reaching the real mapper, Deform's local pull. The
tools run on the legacy asset scratch here; gestures are driven through the same
handle messages C++ sends."""
import json
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
sys.modules["animean_python"].ui = types.SimpleNamespace(
    set_overlay=lambda *args: None, history_commit=lambda *args: None,
    refresh=lambda: None, refresh_tool_options=lambda: None)
import auto_mapping as am
import line_tools
import proxy_shape


def ring(points, steps=40):
    out = []
    for a, b in zip(points, points[1:] + points[:1]):
        out.extend([a[0] + (b[0] - a[0]) * i / steps, a[1] + (b[1] - a[1]) * i / steps]
                   for i in range(steps))
    return out


def line(a, b, n=24):
    return [[a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n] for i in range(n + 1)]


def reset():
    am._MAPPING_ASSETS.clear()
    am._LINE_TOOLS.update({"armed": False, "chosen": None})
    am._PROXY_PAIRING.update({"key": None, "value": None})
    am._invalidate_grid_cache()


RECT = [(-150.0, -100.0), (150.0, -100.0), (150.0, 100.0), (-150.0, 100.0)]
KEYSTONE = [(-120.0, -110.0), (130.0, -105.0), (190.0, 120.0), (-200.0, 125.0)]


def texture():
    assets = am._assets_for("child")
    assets[am.H_PROPERTY] = {"points": line((-150.0, 0.0), (150.0, 0.0)), "width": 3.0}
    assets[am.V_PROPERTY] = {"points": line((0.0, -100.0), (0.0, 100.0)), "width": 3.0}


def contours(main=KEYSTONE):
    am._assets_for("child")[am.PROXY_PROPERTY] = {"points": ring(RECT), "width": 3.0}
    am._assets_for("main")[am.PROXY_PROPERTY] = {"points": ring(main), "width": 3.0}


# 1) Pairing: a rectangle's corners land on the keystone's, whichever corner and
#    winding each was drawn from, and a contour too small or crossed is refused.
paired, why = proxy_shape.match(ring(RECT), ring(KEYSTONE))
assert paired, why
assert len(paired["child"]) == proxy_shape.STATIONS == len(paired["main"])
assert paired["corners"] == 4


def corner_error(result):
    return max(min(math.hypot(c[0] - m[0], c[1] - m[1]) for m in result["main"])
               for c in KEYSTONE)


assert corner_error(paired) < 3.0
reordered, why = proxy_shape.match(list(reversed(ring(RECT[2:] + RECT[:2]))), ring(KEYSTONE[1:] + KEYSTONE[:1]))
assert reordered and reordered["corners"] == 4
near = min(reordered["child"], key=lambda p: math.hypot(p[0] + 150.0, p[1] + 100.0))
landed = reordered["main"][reordered["child"].index(near)]
assert math.hypot(landed[0] - KEYSTONE[0][0], landed[1] - KEYSTONE[0][1]) < 3.0
assert proxy_shape.match(ring([(0, 0), (5, 0), (5, 5), (0, 5)]), ring(KEYSTONE))[0] is None
assert proxy_shape.match(ring([(0, 0), (100, 100), (100, 0), (0, 100)]), ring(KEYSTONE))[0] is None
print("1) contours pair corner to corner")

# 1b) A clean polygon arrives as a handful of points (straight runs flatten to
#     their ends): it is judged by length, and stored dense.
reset()
assert am.run_proxy_contour_tool("main", KEYSTONE + KEYSTONE[:1])
stored = am._assets_for("main")[am.PROXY_PROPERTY]["points"]
assert len(stored) > 100
assert not am.run_proxy_contour_tool("child", [(0, 0), (10, 0), (10, 10)])
print("1b) clean polygons are accepted and densified")

# 1c) A drawn contour is a ground line from the start, and two of them take the
#     pattern onto the main contour with no tick. Unticking still works.
reset()
texture()
assert am.run_proxy_contour_tool("child", RECT + RECT[:1])
assert am.run_proxy_contour_tool("main", KEYSTONE + KEYSTONE[:1])
assert am.line_is_ground(am.PROXY_PROPERTY) and len(am._ground_pairs()) == 1
mp, why = am._mapper_from_assets()
assert mp is not None and mp.warp.grounds[0]["closed"], why
paired = am.proxy_pairing()
assert max(math.hypot(mp(tuple(c))[0] - m[0], mp(tuple(c))[1] - m[1])
           for c, m in zip(paired["child"], paired["main"])) < 2.0
am.set_line_ground(am.PROXY_PROPERTY, False)
assert am._ground_pairs() is None
print("1c) a drawn contour is a ground line")

# 2) Undrawn main axes follow the contours, and the frame they make is usable.
reset()
texture()
contours()
assert am._refresh_proxy_axes()
main = am._assets_for("main")
assert main[am.H_PROPERTY]["auto"] and main[am.V_PROPERTY]["auto"]
mp, why = am._mapper_from_assets()
assert mp is not None, why
print("2) main axes come from the contours")

# 3) An axis the user drew or moved keeps its data.
drawn = line((-170.0, 10.0), (170.0, 10.0))
main[am.H_PROPERTY] = {"points": drawn, "width": 3.0}
am._refresh_proxy_axes()
assert main[am.H_PROPERTY]["points"] == drawn and "auto" not in main[am.H_PROPERTY]
print("3) a user's axis is not overwritten")

# 3b) Contour-owned axes follow the texture axes; an unchanged source is left alone.
reset()
texture()
contours()
am._refresh_proxy_axes()
assert am._refresh_proxy_axes() is False
was = [list(p) for p in am._assets_for("main")[am.V_PROPERTY]["points"]]
am._assets_for("child")[am.V_PROPERTY] = {"points": line((40.0, -100.0), (40.0, 100.0)), "width": 3.0}
assert am._refresh_proxy_axes() is True
assert am._assets_for("main")[am.V_PROPERTY]["points"] != was
print("3b) auto axes follow the texture axes")

# 4) THE TICK: the contour shapes the mapping only once it is a ground line, and
#    then the texture contour lands on the main contour.
reset()
texture()
contours()
am._refresh_proxy_axes()
plain, _ = am._mapper_from_assets()
assert am._ground_pairs() is None and plain.warp is None


def contour_miss(mapper):
    paired = am.proxy_pairing()
    return max(math.hypot(mapper(tuple(c))[0] - m[0], mapper(tuple(c))[1] - m[1])
               for c, m in zip(paired["child"], paired["main"]))


before = contour_miss(plain)
am.set_line_ground(am.PROXY_PROPERTY, True)
assert am.line_is_ground(am.PROXY_PROPERTY)
ground, _ = am._mapper_from_assets()
assert ground.warp is not None and ground.warp.grounds[0]["closed"]
after = contour_miss(ground)
assert before > 10.0 and after < 2.0, (before, after)
print(f"4) ticking the contour: {before:.1f} px -> {after:.2f} px")

# 5) Unticking gives the plain mapping back; the mapper cache sees the flag.
am.set_line_ground(am.PROXY_PROPERTY, False)
assert am._ground_pairs() is None
assert am._mapper_from_assets()[0].warp is None
print("5) unticking restores the mapping")

# 6) An additional line ticked as ground leaves the flow set for the ground set.
reset()
texture()
for view in ("child", "main"):
    assets = am._assets_for(view)
    assets[am.H_PROPERTY] = {"points": line((-150.0, 0.0), (150.0, 0.0)), "width": 3.0}
    assets[am.V_PROPERTY] = {"points": line((0.0, -100.0), (0.0, 100.0)), "width": 3.0}
child_line = line((40.0, 60.0), (130.0, 60.0))
main_line = line((40.0, 75.0), (130.0, 75.0))
am._assets_for("child")[am.ADDITIONAL_PROPERTY] = {"lines": [{"points": child_line, "id": 3, "width": 2.5}]}
am._assets_for("main")[am.ADDITIONAL_PROPERTY] = {"lines": [{"points": main_line, "id": 3, "width": 2.5}]}
assert len(am._additional_pairs()) == 1 and am._ground_pairs() is None
am.set_line_ground(am.ADDITIONAL_PROPERTY + ":3", True)
assert am._additional_pairs() is None and len(am._ground_pairs()) == 1
mp, _ = am._mapper_from_assets()
assert math.hypot(mp((85.0, 60.0))[0] - 85.0, mp((85.0, 60.0))[1] - 75.0) < 0.2
print("6) a ticked additional line pins position")

# 7) A ticked axis stays where the frame puts it, even inside a contour that lets
#    every other axis go.
reset()
texture()
contours()
am._refresh_proxy_axes()
am.set_line_ground(am.PROXY_PROPERTY, True)
am.set_line_ground(am.H_PROPERTY, True)
assert len(am._ground_pairs()) == 2
mp, why = am._mapper_from_assets()
assert mp is not None, why
frame = mp.main_frame
for l_h in (-100.0, -40.0, 50.0, 110.0):
    p = mp(tuple(am._Frame(am._assets_for("child")[am.H_PROPERTY],
                           am._assets_for("child")[am.V_PROPERTY]).hv(l_h, 0.0)))
    q = frame.hv(*mp.scale_arcs(l_h, 0.0))
    assert math.hypot(p[0] - q[0], p[1] - q[1]) < 0.5, l_h
print("7) a ticked axis holds")

# 8) Choice Line: a press selects, on either board, without moving the line.
reset()
texture()
contours()
am._refresh_proxy_axes()
am.arm_line_tools(True)
points_before = [list(p) for p in am._assets_for("child")[am.H_PROPERTY]["points"]]


def handle(handle_id, phase, x, y, prop, view="main"):
    message = {"handle": handle_id, "phase": phase, "view": view, "property": prop,
               "position": {"x": x, "y": y}}
    line_tools._on_handle(message)
    am._guide_drag_event(message)


handle(am.H_PROPERTY, "press", 0.0, 0.0, am.CHOICE_PROPERTY, "child")
handle(am.H_PROPERTY, "move", 30.0, 40.0, am.CHOICE_PROPERTY, "child")
handle(am.H_PROPERTY, "release", 30.0, 40.0, am.CHOICE_PROPERTY, "child")
assert am.chosen_line() == am.H_PROPERTY
assert am._assets_for("child")[am.H_PROPERTY]["points"] == points_before
print("8) Choice Line selects and moves nothing")

# 9) Deform pulls locally and fades along the line; the far end stays. It works
#    on the contour too, wrapping past the seam, and the axes follow the contour.
reset()
texture()
contours()
am._refresh_proxy_axes()
am.arm_line_tools(True)
line_tools.DEFORM_RADIUS["value"] = 40.0
target = am.H_PROPERTY
item = am._assets_for("child")[target]
handle(target, "press", 0.0, 0.0, am.DEFORM_PROPERTY, "child")
handle(target, "move", 0.0, 30.0, am.DEFORM_PROPERTY, "child")
handle(target, "release", 0.0, 30.0, am.DEFORM_PROPERTY, "child")
points = am._assets_for("child")[target]["points"]
mid = min(points, key=lambda p: abs(p[0]))
assert abs(mid[1] - 30.0) < 1.0
assert abs(points[0][1]) < 0.05 and abs(points[-1][1]) < 0.05
print("9) Deform pulls a stretch")

axes_before = [list(p) for p in am._assets_for("main")[am.V_PROPERTY]["points"]]
seam = am._assets_for("main")[am.PROXY_PROPERTY]["points"][0]
handle(am.PROXY_PROPERTY, "press", seam[0], seam[1], am.DEFORM_PROPERTY)
handle(am.PROXY_PROPERTY, "move", seam[0] - 25.0, seam[1] - 25.0, am.DEFORM_PROPERTY)
handle(am.PROXY_PROPERTY, "release", seam[0] - 25.0, seam[1] - 25.0, am.DEFORM_PROPERTY)
contour = am._assets_for("main")[am.PROXY_PROPERTY]["points"]
assert abs(contour[0][0] - seam[0] + 25.0) < 1.0
assert math.hypot(contour[-1][0] - contour[0][0], contour[-1][1] - contour[0][1]) < 8.0
assert am._assets_for("main")[am.V_PROPERTY]["points"] != axes_before
print("10) Deform wraps on the contour and the auto axes follow")

# 11) Cancel restores what the drag started from.
reset()
texture()
item = am._assets_for("child")[am.H_PROPERTY]
original = [list(p) for p in item["points"]]
handle(am.H_PROPERTY, "press", 0.0, 0.0, am.DEFORM_PROPERTY, "child")
handle(am.H_PROPERTY, "move", 0.0, 30.0, am.DEFORM_PROPERTY, "child")
handle(am.H_PROPERTY, "cancel", 0.0, 30.0, am.DEFORM_PROPERTY, "child")
assert am._assets_for("child")[am.H_PROPERTY]["points"] == original
print("11) a cancelled drag restores the line")

# 12) Overlays: paired contours carry the same hue at the same station on both
#     boards, the ring is grabbable only under a line tool, and pins mark ground.
reset()
texture()
contours()
am._refresh_proxy_axes()
items = am._contour_items("child")
assert items[0]["id"] == am.PROXY_PROPERTY and not items[0]["draggable"]
assert len(items) == am.PROXY_HUES + 1
assert [i["color"] for i in items[1:]] == [i["color"] for i in am._contour_items("main")[1:]]
am.arm_line_tools(True)
assert am._contour_items("child")[0]["draggable"]
am.set_line_ground(am.PROXY_PROPERTY, True)
assert sum(i["id"].startswith("line_pin:") for i in am._line_mark_items("child")) == 4
print("12) contour overlays")


def counted_matches():
    calls = []
    original = proxy_shape.match

    def match(*args):
        calls.append(args)
        return original(*args)

    proxy_shape.match = match
    return calls, lambda: setattr(proxy_shape, "match", original)


# 13) Deform keeps the pairing: the stations move with the line and nothing is
#     matched again. This pull is one where matching from scratch lands a quarter
#     turn off, which swung the auto H across the V until they no longer crossed.
reset()
texture()
contours()
am._refresh_proxy_axes()
am.arm_line_tools(True)
line_tools.DEFORM_RADIUS["value"] = 120.0
paired = am.proxy_pairing()
texture_stations = [list(p) for p in paired["child"]]
drawn = [list(p) for p in paired["main"]]
seam = am._assets_for("main")[am.PROXY_PROPERTY]["points"][0]
near = min(range(len(drawn)), key=lambda k: math.dist(drawn[k], seam))
far = (near + len(drawn) // 2) % len(drawn)
calls, restore = counted_matches()
handle(am.PROXY_PROPERTY, "press", seam[0], seam[1], am.DEFORM_PROPERTY)
handle(am.PROXY_PROPERTY, "move", seam[0] + 100.0, seam[1] - 100.0, am.DEFORM_PROPERTY)
handle(am.PROXY_PROPERTY, "move", seam[0] + 250.0, seam[1] - 250.0, am.DEFORM_PROPERTY)
handle(am.PROXY_PROPERTY, "release", seam[0] + 250.0, seam[1] - 250.0, am.DEFORM_PROPERTY)
restore()
assert not calls
paired = am.proxy_pairing()
assert paired["child"] == texture_stations
assert math.dist(paired["main"][near], (seam[0] + 250.0, seam[1] - 250.0)) < 20.0
assert math.dist(paired["main"][far], drawn[far]) < 1.0
contour = am._assets_for("main")[am.PROXY_PROPERTY]["points"]
fresh, why = proxy_shape.match(am._assets_for("child")[am.PROXY_PROPERTY]["points"], contour)
assert fresh and sum(math.dist(a, b) for a, b in zip(fresh["main"], paired["main"])) / len(drawn) > 100.0
h_axis = am._assets_for("main")[am.H_PROPERTY]
assert h_axis["auto"] and abs(h_axis["points"][-1][0] - h_axis["points"][0][0]) > abs(
    h_axis["points"][-1][1] - h_axis["points"][0][1])
assert am._polylines_cross(h_axis["points"], am._assets_for("main")[am.V_PROPERTY]["points"])
print("13) Deform keeps the pairing")

# 14) A cancelled pull puts the stations back with the line.
before = [list(p) for p in am.proxy_pairing()["main"]]
seam = am._assets_for("main")[am.PROXY_PROPERTY]["points"][0]
handle(am.PROXY_PROPERTY, "press", seam[0], seam[1], am.DEFORM_PROPERTY)
handle(am.PROXY_PROPERTY, "move", seam[0] + 40.0, seam[1], am.DEFORM_PROPERTY)
handle(am.PROXY_PROPERTY, "cancel", seam[0] + 40.0, seam[1], am.DEFORM_PROPERTY)
assert am.proxy_pairing()["main"] == before
print("14) a cancelled pull restores the stations")

# 15) Undo, redo and loading rebuild the assets from scriptData: the contour, its
#     pairing, the ground ticks and the axes' auto mark all come back, and nothing
#     is matched or carried again.
am.set_line_ground(am.PROXY_PROPERTY, True)
am.set_line_ground(am.V_PROPERTY, True)
am._assets_for("child")[am.ADDITIONAL_PROPERTY] = {"lines": [
    {"id": 0, "points": line((-90.0, -50.0), (90.0, -50.0)), "width": 2.5, am.GROUND_FLAG: True}]}
stored = {view: json.loads(json.dumps(am._assets_for(view))) for view in ("child", "main")}
stations = am.proxy_pairing()
calls, restore = counted_matches()
for view in stored:
    am._MAPPING_ASSETS[view] = am._sanitize_assets(stored[view])
am._PROXY_PAIRING.update({"key": None, "value": None})
main_assets = am._assets_for("main")
assert main_assets[am.PROXY_PROPERTY]["points"] == stored["main"][am.PROXY_PROPERTY]["points"]
assert main_assets[am.PROXY_PROPERTY][am.GROUND_FLAG]
assert main_assets[am.H_PROPERTY]["auto"] and "source" in main_assets[am.H_PROPERTY]
assert am.line_is_ground(am.V_PROPERTY) and am.line_is_ground(am.PROXY_PROPERTY)
assert am._assets_for("child")[am.ADDITIONAL_PROPERTY]["lines"][0][am.GROUND_FLAG]
assert am.proxy_pairing()["main"] == stations["main"] and am.proxy_pairing()["child"] == stations["child"]
assert not am._refresh_proxy_axes()
restore()
assert not calls
print("15) the assets survive a restore")

# 16) Only a new line is a new pairing: a redraw matches again, on both contours.
kept = am._assets_for("child")[am.PROXY_PROPERTY][am.PAIRING_KEY]["id"]
calls, restore = counted_matches()
am.run_proxy_contour_tool("main", ring(KEYSTONE))
restore()
assert len(calls) == 1
ids = {am._assets_for(view)[am.PROXY_PROPERTY][am.PAIRING_KEY]["id"] for view in ("child", "main")}
assert len(ids) == 1 and kept not in ids
print("16) a redraw pairs again")


def counted_builds():
    calls = []
    original = am.build_mapper

    def build_mapper(*args, **kwargs):
        calls.append(args)
        return original(*args, **kwargs)

    am.build_mapper = build_mapper
    return calls, lambda: setattr(am, "build_mapper", original)


def guide_drag(phase, x, y):
    am._guide_drag_event({"handle": am.V_PROPERTY, "phase": phase, "view": "main",
                          "position": {"x": x, "y": y}})


# 17) A drag moves the line and nothing else: with the refer grid on, no move
#     rebuilds the mapping or the grid; the release does, once. Same for Deform
#     and for an axis dragged whole.
reset()
texture()
contours()
am._refresh_proxy_axes()
am.arm_line_tools(True)
am._REFER_RECT.update(child=True, main=True)
assert am._grid_overlay_items("main")
frozen = am._grid_overlay_items("main")
calls, restore = counted_builds()
seam = am._assets_for("main")[am.PROXY_PROPERTY]["points"][0]
handle(am.PROXY_PROPERTY, "press", seam[0], seam[1], am.DEFORM_PROPERTY)
for step in (1, 2, 3):
    handle(am.PROXY_PROPERTY, "move", seam[0] - 8.0 * step, seam[1], am.DEFORM_PROPERTY)
    assert math.dist(am._assets_for("main")[am.PROXY_PROPERTY]["points"][0],
                     (seam[0] - 8.0 * step, seam[1])) < 0.1
assert not calls and am._grid_overlay_items("main") is frozen
handle(am.PROXY_PROPERTY, "release", seam[0] - 24.0, seam[1], am.DEFORM_PROPERTY)
assert len(calls) == 1 and am._grid_overlay_items("main") is not frozen

frozen = am._grid_overlay_items("main")
del calls[:]
grab = am._assets_for("main")[am.V_PROPERTY]["points"][0]
guide_drag("press", grab[0], grab[1])
for step in (1, 2, 3):
    guide_drag("move", grab[0] + 4.0 * step, grab[1])
assert not calls and am._grid_overlay_items("main") is frozen
guide_drag("release", grab[0] + 12.0, grab[1])
restore()
assert len(calls) == 1 and am._grid_overlay_items("main") is not frozen
am._REFER_RECT.update(child=False, main=False)
print("17) a drag rebuilds nothing until the release")


def rounded(points, index, radius, steps=12):
    """`ring(points)` with one corner cut to a quadratic curve."""
    corner = points[index]
    before, after = points[index - 1], points[(index + 1) % len(points)]
    near = [corner[k] + (before[k] - corner[k]) * radius / math.dist(corner, before) for k in (0, 1)]
    far = [corner[k] + (after[k] - corner[k]) * radius / math.dist(corner, after) for k in (0, 1)]
    curve = [[(1 - t) ** 2 * near[k] + 2 * t * (1 - t) * corner[k] + t ** 2 * far[k] for k in (0, 1)]
             for t in (i / steps for i in range(steps + 1))]
    outline = []
    for a, b in zip(points, points[1:] + points[:1]):
        i = points.index(a)
        start = far if i == index else a
        end = near if (i + 1) % len(points) == index else b
        outline.extend([start[0] + (end[0] - start[0]) * j / 40, start[1] + (end[1] - start[1]) * j / 40]
                       for j in range(40))
        if (i + 1) % len(points) == index:
            outline.extend(curve)
    return outline


# 18) A hand-drawn corner is round: it still pairs with the sharp corner it is, and
#     the pins stand on the corners, one pattern point on both boards, whichever
#     point each line was drawn from.
reset()
texture()
am._assets_for("child")[am.PROXY_PROPERTY] = {"points": ring(RECT), "width": 3.0, am.GROUND_FLAG: True}
main_ring = rounded(KEYSTONE, 1, 45.0)
am._assets_for("main")[am.PROXY_PROPERTY] = {"points": main_ring[37:] + main_ring[:37], "width": 3.0,
                                             am.GROUND_FLAG: True}
paired = am.proxy_pairing()
assert len(paired["corner_stations"]) == 4
pin = lambda view: sorted(
    (sum(p[0] for p in i["points"]) / 16, sum(p[1] for p in i["points"]) / 16)
    for i in am._line_mark_items(view) if i["id"].startswith(f"line_pin:{am.PROXY_PROPERTY}"))
child_pins, main_pins = pin("child"), pin("main")
assert len(child_pins) == 4 and len(main_pins) == 4
for corner in RECT:
    assert min(math.dist(corner, p) for p in child_pins) < 4.0
for corner in (KEYSTONE[0], KEYSTONE[2], KEYSTONE[3]):
    assert min(math.dist(corner, p) for p in main_pins) < 4.0
assert min(math.dist(KEYSTONE[1], p) for p in main_pins) < 30.0
stored = {view: json.loads(json.dumps(am._assets_for(view))) for view in ("child", "main")}
for view in stored:
    am._MAPPING_ASSETS[view] = am._sanitize_assets(stored[view])
am._PROXY_PAIRING.update({"key": None, "value": None})
assert pin("child") == child_pins and pin("main") == main_pins

# One contour alone has no partner yet and still pins its own corners.
am._assets_for("main").pop(am.PROXY_PROPERTY)
assert pin("child") and min(math.dist(RECT[0], p) for p in pin("child")) < 4.0
print("18) pins stand on the paired corners")


def committed():
    calls = []
    original = sys.modules["animean_python"].ui.history_commit
    sys.modules["animean_python"].ui.history_commit = lambda label, view: calls.append((label, view))
    return calls, lambda: setattr(sys.modules["animean_python"].ui, "history_commit", original)


def fresh_pair():
    reset()
    texture()
    contours()
    am._refresh_proxy_axes()
    am.arm_line_tools(True)
    am.proxy_pairing()


# 19) A click or a cancelled drag leaves a line as it was, its marks included.
fresh_pair()
item = am._assets_for("main")[am.V_PROPERTY]
item["commands"] = [{"type": "move", "to": {"x": 0.0, "y": 0.0}}]
before = dict(item)
grab = item["points"][len(item["points"]) // 2]
handle(am.V_PROPERTY, "press", grab[0], grab[1], am.DEFORM_PROPERTY)
handle(am.V_PROPERTY, "move", grab[0], grab[1], am.DEFORM_PROPERTY)
handle(am.V_PROPERTY, "release", grab[0], grab[1], am.DEFORM_PROPERTY)
assert am._assets_for("main")[am.V_PROPERTY] == before
handle(am.V_PROPERTY, "press", grab[0], grab[1], am.DEFORM_PROPERTY)
handle(am.V_PROPERTY, "move", grab[0] + 20.0, grab[1], am.DEFORM_PROPERTY)
handle(am.V_PROPERTY, "cancel", grab[0] + 20.0, grab[1], am.DEFORM_PROPERTY)
assert am._assets_for("main")[am.V_PROPERTY] == before
for phases in (("press", "release"), ("press", "move", "cancel")):
    for phase in phases:
        offset = 20.0 if phase in ("move", "cancel") else 0.0
        guide_drag(phase, grab[0] + offset, grab[1])
    assert am._assets_for("main")[am.V_PROPERTY] == before
print("19) a click or a cancel leaves a line as it was")

# 20) Undo in the middle of a drag ends the drag.
fresh_pair()
grab = am._assets_for("main")[am.V_PROPERTY]["points"][10]
handle(am.V_PROPERTY, "press", grab[0], grab[1], am.DEFORM_PROPERTY)
line_tools._history_restored(None, None, {"view": "main"})
assert not line_tools._DRAG
restored = [list(p) for p in am._assets_for("main")[am.V_PROPERTY]["points"]]
handle(am.V_PROPERTY, "move", grab[0] + 30.0, grab[1], am.DEFORM_PROPERTY)
assert am._assets_for("main")[am.V_PROPERTY]["points"] == restored
print("20) undo ends a drag")

# 21) Deforming the texture contour moves the auto axes on the main board, and that
#     board commits it too, or its undo would run ahead of its snapshots.
fresh_pair()
calls, restore = committed()
seam = am._assets_for("child")[am.PROXY_PROPERTY]["points"][0]
handle(am.PROXY_PROPERTY, "press", seam[0], seam[1], am.DEFORM_PROPERTY, "child")
handle(am.PROXY_PROPERTY, "release", seam[0] - 20.0, seam[1], am.DEFORM_PROPERTY, "child")
restore()
assert ("Deform Line", "child") in calls and ("Contour axes", "main") in calls
print("21) the main board commits axes a texture contour moved")

# 22) A contour that pairs while it is being dragged does not keep stations from
#     that moment: the release pairs what the line ended up as.
reset()
texture()
am.arm_line_tools(True)
am._assets_for("child")[am.PROXY_PROPERTY] = {"points": ring(RECT), "width": 3.0}
seam = RECT[0]
handle(am.PROXY_PROPERTY, "press", seam[0], seam[1], am.DEFORM_PROPERTY, "child")
am._assets_for("main")[am.PROXY_PROPERTY] = {"points": ring(KEYSTONE), "width": 3.0}
handle(am.PROXY_PROPERTY, "move", seam[0] - 10.0, seam[1] - 10.0, am.DEFORM_PROPERTY, "child")
am.proxy_pairing()
handle(am.PROXY_PROPERTY, "release", seam[0] - 30.0, seam[1] - 30.0, am.DEFORM_PROPERTY, "child")
final = am._assets_for("child")[am.PROXY_PROPERTY]["points"]
assert max(min(math.dist(s, p) for p in final) for s in am.proxy_pairing()["child"]) < 3.0
print("22) a pairing made mid-drag follows the release")

# 23) Ground ticks and contours change the mapping, so the grid is rebuilt; unticking
#     takes the pins away from both boards at once.
fresh_pair()
am._REFER_RECT.update(child=True, main=True)
am.set_line_ground(am.PROXY_PROPERTY, True)
frozen = am._grid_overlay_items("main")
pushed = {}
original_items = am.overlay_items
am.overlay_items = lambda view: pushed.setdefault(view, sum(
    i["id"].startswith("line_pin:") for i in am._line_mark_items(view))) and [] or []
am.set_line_ground(am.PROXY_PROPERTY, False)
am.overlay_items = original_items
assert pushed == {"main": 0, "child": 0}
assert am._grid_overlay_items("main") is not frozen
frozen = am._grid_overlay_items("main")
am._overlay_removed(None, None, {"overlay": {"id": am.PROXY_PROPERTY}, "view": "main"})
assert am._grid_overlay_items("main") is not frozen
frozen = am._grid_overlay_items("main")
am.run_proxy_contour_tool("main", ring(KEYSTONE))
assert am._grid_overlay_items("main") is not frozen
am._REFER_RECT.update(child=False, main=False)
print("23) ground ticks and contours refresh the grid")

# 24) One matched corner aligns nothing, so it marks nothing either.
drop = [[200.0 * math.cos(t), 200.0 * math.sin(t) * math.sin(t / 2.0)]
        for t in (i * 2.0 * math.pi / 240 for i in range(240))]
paired, why = proxy_shape.match(drop, drop)
assert paired and paired["corners"] == 1 and paired["corner_stations"] == []
print("24) one corner is no corner pin")

# 25) The contour takes presses only under a line tool, and the halo goes with it:
#     any other tool announces itself through the handle event's arm phase.
fresh_pair()
am.choose_line(am.H_PROPERTY)
contour_item = lambda: next(i for i in am._contour_items("main") if i["id"] == am.PROXY_PROPERTY)
assert contour_item()["draggable"] and am._chosen_line_items("main")
line_tools._on_handle({"phase": "arm", "property": ""})
assert not contour_item()["draggable"] and not am._chosen_line_items("main")
line_tools._on_handle({"phase": "arm", "property": am.DEFORM_PROPERTY})
assert contour_item()["draggable"] and am._chosen_line_items("main")
print("25) other tools disarm the contour and the halo")

print("ALL OK")
