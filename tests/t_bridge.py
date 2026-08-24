"""Bezier Bridge (补全拓扑): the tool-menu checkbox + tension slider, and the
Third-space cubic P0=A, P1=A+k*vA, P2=B-k*vB, P3=B spanning severed gaps
(user request 2026-08-24)."""
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am
import toolcontrol

V = [(0.0, -200.0), (0.0, 200.0)]
MAIN_H = [(-300.0, 0.0), (300.0, 0.0)]
# Zigzag child H: the middle segment retreats LEFT, so its cells fold
# (det < 0 band) and a horizontal stroke dives into severed ground and
# RE-EMERGES - exactly the bridgeable shape.
ZIG_H = [(-300.0, 0.0), (120.0, 0.0), (0.0, 100.0), (400.0, 100.0)]


def close(a, b, tol=1e-9):
    return math.hypot(a[0] - b[0], a[1] - b[1]) <= tol


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


class FakeOut:
    """Just enough of _MappedOutput for the emitters under test."""

    def __init__(self):
        self.cuts, self.seams, self.bridges = [], [], []
        self.polylines, self.curved = [], []

    def add_polyline(self, side, points, color, width, depth=None):
        if len(points) < 2:
            return False
        self.polylines.append((side, points, depth))
        return True

    def add_curved(self, side, commands, flat, color, width, depth=None):
        if len(flat) < 2:
            return False
        self.curved.append((side, flat, depth))
        return True


mp, _ws = am.build_mapper(ZIG_H, V, MAIN_H, V, {})
assert mp is not None
stroke_pts = am._densify([(-200.0, 30.0), (350.0, 30.0)])
islands = am._sever_source(mp, stroke_pts, [])
assert len(islands) == 2, len(islands)

# 1) THE FORMULA: P0/P3 are the lifted cuts, the handles are k = tension*|AB|
#    long along the islands' Third trends.
bridge = am._bridge_third_cubic(mp, islands[0], islands[1])
assert bridge is not None
p0, p1, p2, p3 = bridge
A = mp.coords(islands[0][-1])
B = mp.coords(islands[1][0])
assert close(p0, A) and close(p3, B)
span = dist(A, B)
k = am.bridge_tension() * span
assert abs(dist(p1, p0) - k) <= 1e-9 * max(1.0, k)
assert abs(dist(p2, p3) - k) <= 1e-9 * max(1.0, k)
chord = ((B[0] - A[0]) / span, (B[1] - A[1]) / span)
v_a = ((p1[0] - p0[0]) / k, (p1[1] - p0[1]) / k)
v_b = ((p3[0] - p2[0]) / k, (p3[1] - p2[1]) / k)
assert v_a[0] * chord[0] + v_a[1] * chord[1] > 0.5   # departs toward B
assert v_b[0] * chord[0] + v_b[1] * chord[1] > 0.5   # arrives from A
print(f"1) bridge over gap |AB|={span:.1f} (Third px): P0/P3 = lifted cuts, "
      f"|P1-P0| = |P2-P3| = k = {k:.2f}")

# 2) THE SLIDER: k scales with the tension setting.
am._BRIDGE["tension"] = 0.5
p0b, p1b, _p2b, _p3b = am._bridge_third_cubic(mp, islands[0], islands[1])
assert abs(dist(p1b, p0b) - 0.5 * span) <= 1e-9 * span
am._BRIDGE["tension"] = 0.33
print("2) tension slider drives k (0.5 x |AB| measured as set)")

# 3) COLLINEAR ISLANDS on an identity frame: the trends line up with the
#    chord, the cubic degenerates to the straight join, and the projection
#    reproduces it exactly (identity mapping).
mp0, _ = am.build_mapper(MAIN_H, V, MAIN_H, V, {})
ia = [(-100.0, 0.0), (-75.0, 0.0), (-50.0, 0.0)]
ib = [(50.0, 0.0), (75.0, 0.0), (100.0, 0.0)]
straight = am._bridge_third_cubic(mp0, ia, ib)
flagged = am._project_third_cubic(mp0, straight)
assert close(flagged[0][0], (-50.0, 0.0), 1e-6)
assert close(flagged[-1][0], (50.0, 0.0), 1e-6)
worst = max(abs(pt[1]) for pt, _anchor in flagged)
assert worst <= 1e-9, worst
print(f"3) collinear islands: bridge = the straight join (max |y| {worst:.1e})")

# 4) CONTINUITY: the projected bridge starts exactly at the mapped A-island
#    end and lands exactly on the mapped B-island start.
flagged = am._project_third_cubic(mp, bridge)
assert close(flagged[0][0], mp(islands[0][-1]), 1e-9)
assert close(flagged[-1][0], mp(islands[1][0]), 1e-9)
print("4) projected bridge is C0 with both islands (exact endpoint match)")

# 5) EMITTERS: off -> two islands, no bridge; on -> one extra segment whose
#    ends butt the islands', in both sampled modes.
stroke = {"polylines": [[{"x": x, "y": y} for x, y in
                         [(-200.0, 30.0), (350.0, 30.0)]]]}
for mode_name, emit, bucket in (
        ("polyline", am._emit_polyline_mode, "polylines"),
        ("spline", am._emit_spline_mode, "curved")):
    am._BRIDGE["enabled"] = False
    out = FakeOut()
    emit(None, out, stroke, mp, None, None, (0, 0, 0, 255), 3.0)
    base = len(getattr(out, bucket))
    assert not out.bridges
    am._BRIDGE["enabled"] = True
    out = FakeOut()
    emit(None, out, stroke, mp, None, None, (0, 0, 0, 255), 3.0)
    assert len(out.bridges) == 1, (mode_name, len(out.bridges))
    assert len(getattr(out, bucket)) == base + 1
    am._BRIDGE["enabled"] = False
    print(f"5) {mode_name} mode: bridging off {base} segment(s), "
          f"on {base}+1 with 1 bridge")

# 6) BEZIER-MODE PLUMBING: cubic islands bridge through the island trend
#    probes at their facing ends (the probes span whole islands now, so a
#    sub-pixel end sliver cannot set the trend).
cub_a = ((-200.0, 30.0), (-100.0, 30.0), (0.0, 30.0), tuple(islands[0][-1]))
cub_b = (tuple(islands[1][0]), (300.0, 30.0), (320.0, 30.0), (350.0, 30.0))
pairs = [(am._cubic_tail_polyline([cub_a]), am._cubic_head_polyline([cub_b]))]
am._BRIDGE["enabled"] = True
out = FakeOut()
added = am._emit_bridges(out, mp, pairs, None, (0, 0, 0, 255), 3.0,
                         curved=True, eps=am.rdp_eps())
am._BRIDGE["enabled"] = False
assert added == 1 and len(out.bridges) == 1
b0 = out.bridges[0]
assert close(b0[0], mp.coords(cub_a[3]), 1e-6)
assert close(b0[3], mp.coords(cub_b[0]), 1e-6)
print("6) bezier islands bridge via their end-trend probes")

# 7) TOOL OPTIONS: the hooks flip the state (with clamping), and the panel
#    declares the checkbox plus a slider that only shows while it is on.
am._tool_option_changed(None, None, {"hook": "bridge_topology", "value": "on"})
assert am.bridge_enabled()
am._tool_option_changed(None, None, {"hook": "bridge_tension", "value": 50})
assert abs(am.bridge_tension() - 0.5) < 1e-12
am._tool_option_changed(None, None, {"hook": "bridge_tension", "value": 400})
assert abs(am.bridge_tension() - 1.0) < 1e-12   # clamped high
am._tool_option_changed(None, None, {"hook": "bridge_tension", "value": 1})
assert abs(am.bridge_tension() - 0.05) < 1e-12  # clamped low
am._tool_option_changed(None, None, {"hook": "bridge_topology", "value": "off"})
am._tool_option_changed(None, None, {"hook": "bridge_tension", "value": 33})
assert not am.bridge_enabled() and abs(am.bridge_tension() - 0.33) < 1e-12
panel = toolcontrol.options_for_extra_tool("auto_mapping_2")
by_name = {c["name"]: c for c in panel["controls"]}
assert by_name["bridge_topology"]["type"] == "check"
assert by_name["bridge_topology"]["title"] == "补全拓扑"
slider = by_name["bridge_tension"]
assert slider["min"] == 5 and slider["max"] == 100 and slider["value"] == 33
assert slider["visible_when"] == {"name": "bridge_topology", "values": ["on"]}
print("7) checkbox + tension slider declared; hooks flip and clamp the state")

# 8) NO-GAP SAFETY: a single island emits nothing extra even with bridging
#    on, and same-point cuts return None instead of a degenerate cubic.
am._BRIDGE["enabled"] = True
out = FakeOut()
am._emit_polyline_mode(None, out, {"polylines": [[{"x": -200.0, "y": 30.0},
                                                  {"x": 100.0, "y": 30.0}]]},
                       mp, None, None, (0, 0, 0, 255), 3.0)
assert not out.bridges
assert am._bridge_third_cubic(mp0, ia, [ia[-1], (0.0, 50.0)]) is None
am._BRIDGE["enabled"] = False
print("8) one island -> no bridge; coincident cuts -> no degenerate cubic")

# --- Review fixes (2026-08-25) --------------------------------------------

# 9) SIDE STABILITY: side and depth are probed one step INSIDE island A
#    (_island_end_anchor), never at the cut itself - the cut sits on
#    det J = 0, where the verdict was a rounding coin flip (26% of
#    sub-pixel perturbations emitted a BACK bridge between FRONT islands).
probe = am._island_end_anchor(islands[0], at_end=True)
assert mp.third_of(probe)[2], probe
assert am._fold_sign(mp, probe) == am._split_by_fold(mp, islands[0])[-1][1]
for k in range(20):
    y = 5.0 + 85.0 * k / 19.0
    ip = am._sever_source(mp, am._densify([(-200.0, y), (350.0, y)]), [])
    if len(ip) < 2:
        continue
    p_k = am._island_end_anchor(ip[0], at_end=True)
    assert am._fold_sign(mp, p_k) == am._split_by_fold(mp, ip[0])[-1][1], y
print("9) bridge side matches island A across the sweep (no coin flip)")

# 10) COUNTING: a bridge clipped into two visible stretches by the mapping
#     area is still ONE bridge in out.bridges (the summary's count); each
#     clipped piece still counts as an emitted item.
area = [[(-400.0, -100.0), (150.0, -100.0), (150.0, 60.0), (-400.0, 60.0)],
        [(200.0, -100.0), (500.0, -100.0), (500.0, 60.0), (200.0, 60.0)]]
am._BRIDGE["enabled"] = True
out = FakeOut()
added = am._emit_bridges(out, mp, [(islands[0], islands[1])], area,
                         (0, 0, 0, 255), 3.0, curved=False, eps=am.rdp_eps())
am._BRIDGE["enabled"] = False
assert added == 2, added
assert len(out.bridges) == 1, len(out.bridges)
print("10) clipped bridge: 2 emitted pieces, 1 recorded bridge")

print("t_bridge: ALL OK")
