"""Bezier drape into 3D: _directional_image_3d / _warp_cubic_3d are the
R^3-codomain twins of the 2D handle transport (same eps, same probe set -
the single-midpoint-probe shortcut is exactly what the golden-section probe
exists to defeat), plus the anchor-pinned 3D decimation and the snapshot
metadata (src / anchors) the To 3D export carries for future 2D editing."""
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am
import bezier


def close3(a, b, tol=1e-6):
    return am._dist3(a, b) <= tol


# 1) _directional_image_3d transports a handle to first order: against a
#    smooth analytic lift the error is O(eps * curvature), far under the
#    handle length.
AMP, LAM = 12.0, 90.0


def wavy(p):
    return (p[0], p[1], AMP * math.sin(2.0 * math.pi * p[0] / LAM))


base = (10.0, 5.0)
ctrl = (40.0, 5.0)
img = am._directional_image_3d(wavy, base, ctrl)
length = 30.0
slope = AMP * 2.0 * math.pi / LAM * math.cos(2.0 * math.pi * base[0] / LAM)
exact = (base[0] + length, base[1], wavy(base)[2] + slope * length)
assert am._dist3(img, exact) <= 0.5 * am._JAC_EPS * length, (img, exact)
assert close3(am._directional_image_3d(wavy, base, base), wavy(base))
print("1) 3D handle transport is first-order exact; zero handle maps to base")

# 2) A flat lift keeps a cubic intact: one leaf, exact endpoints, and the
#    leaf's source is the original cubic.
flat = lambda p: (p[0], p[1], 0.0)
cub = ((0.0, 0.0), (30.0, 40.0), (70.0, -40.0), (100.0, 0.0))
leaves = am._warp_cubic_3d(flat, cub)
assert len(leaves) == 1
out, src = leaves[0]
assert src == cub
assert close3(out[0], (0.0, 0.0, 0.0)) and close3(out[3], (100.0, 0.0, 0.0))
assert all(abs(pt[2]) <= 1e-9 for pt in out)
print("2) flat lift: single leaf, exact endpoints, source preserved")

# 3) THE MIDPOINT TRAP: a straight source cubic spanning exactly one full
#    sine period of relief. The fake cubic's error is ~0 at t=0.5 (odd
#    symmetry) - a single midpoint probe would accept the degenerate
#    straight line at depth 0. The inherited probe set must subdivide, and
#    the piecewise result must track the true surface everywhere.
line = bezier.line_cubic((0.0, 0.0), (LAM, 0.0))
fake = (wavy(line[0]),
        am._directional_image_3d(wavy, line[0], line[1]),
        am._directional_image_3d(wavy, line[3], line[2]),
        wavy(line[3]))
mid_gap = am._dist3(am._cubic_point3(fake, 0.5), wavy((LAM * 0.5, 0.0)))
assert mid_gap <= am._CURVE_TOL, mid_gap   # the trap IS armed
leaves = am._warp_cubic_3d(wavy, line)
assert len(leaves) > 1, "one full relief period must force subdivision"
worst = 0.0
for out, src in leaves:
    for k in range(33):
        t = k / 32.0
        truth = wavy(am._cubic_point(src, t))
        worst = max(worst, am._dist3(am._cubic_point3(out, t), truth))
assert worst <= 2.0 * am._CURVE_TOL, worst
print(f"3) full-period relief: subdivided to {len(leaves)} leaves, "
      f"max error {worst:.3f} px (tol {am._CURVE_TOL})")

# 4) Leaf sources partition the original cubic: consecutive leaves chain
#    end-to-start, outer endpoints are the original anchors ("originals
#    are never decimated" - subdividing only ADDS knots).
assert leaves[0][1][0] == line[0]
assert leaves[-1][1][3] == line[3]
for (_, a), (_, b) in zip(leaves, leaves[1:]):
    assert a[3] == b[0]
print("4) leaf sources chain into a partition of the source cubic")

# 5) _rdp_indices3d: decimates like _rdp_polyline3d, but a protected index
#    is pinned even when it deviates nothing.
poly = [(float(k), 0.0, 0.0) for k in range(11)]
poly[5] = (5.0, 4.0, 0.0)
kept = am._rdp_indices3d(poly, 0.25)
assert [poly[i] for i in kept] == am._rdp_polyline3d(poly, 0.25)
straight = [(float(k), 0.0, 0.0) for k in range(11)]
assert am._rdp_indices3d(straight, 0.25) == [0, 10]
assert am._rdp_indices3d(straight, 0.25, protect=(7,)) == [0, 7, 10]
print("5) 3D decimation matches _rdp_polyline3d and pins protected anchors")

# 6) End to end through a real mapper: a commands stroke drapes as leaves
#    with snapshot metadata - src carried through, every original anchor
#    tagged with its chain ordinal at the right 3D position, ribbon edges
#    straddling the centerline at the stroke's width.
H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]
mp, _ws = am.build_mapper(H, V, H, V, {})
assert mp is not None
ANCHORS_2D = [(-120.0, 30.0), (0.0, -20.0), (140.0, 40.0)]
stroke = {
    "width": 6.0,
    "color": {"r": 10, "g": 20, "b": 30, "a": 255},
    "src": (2, 7),
    "commands": [
        {"type": "move", "to": {"x": ANCHORS_2D[0][0], "y": ANCHORS_2D[0][1]}},
        {"type": "cubic",
         "control1": {"x": -80.0, "y": 60.0},
         "control2": {"x": -40.0, "y": -50.0},
         "to": {"x": ANCHORS_2D[1][0], "y": ANCHORS_2D[1][1]}},
        {"type": "cubic",
         "control1": {"x": 50.0, "y": 10.0},
         "control2": {"x": 90.0, "y": 80.0},
         "to": {"x": ANCHORS_2D[2][0], "y": ANCHORS_2D[2][1]}},
    ]}
res = am._reconstruct_surface_3d(mp, [], [stroke], grid_target=16)
assert res is not None and len(res["strokes"]) == 1
draped = res["strokes"][0]
assert draped["src"] == (2, 7)
assert draped["width"] == 6.0
assert draped["color"][:3] == [10, 20, 30]
marks = dict((ordinal, index) for index, ordinal in draped["anchors"])
assert sorted(marks) == [0, 1, 2], draped["anchors"]
assert marks[0] == 0 and marks[2] == len(draped["points"]) - 1
for ordinal, anchor in enumerate(ANCHORS_2D):
    x, y, _z = draped["points"][marks[ordinal]]
    # identity guides: the drape's x/y is the anchor itself
    assert math.hypot(x - anchor[0], y - anchor[1]) <= 0.5, (ordinal, x, y)
left, right = draped["ribbon"]
assert len(left) == len(right) == len(draped["points"])
for pt, lp, rp in zip(draped["points"], left, right):
    gap = am._dist3(lp, rp)
    assert abs(gap - 6.0) <= 0.75, gap
    assert am._dist3(pt, lp) <= 4.0 and am._dist3(pt, rp) <= 4.0
print("6) real-mapper drape: metadata, anchors, and width ribbon all land")

# 7) The polyline fallback (no commands) still severs and still emits the
#    same stroke record shape - anchors empty, decimation applied.
plain = {"polylines": [[{"x": -150.0, "y": 20.0}, {"x": 250.0, "y": 20.0}]],
         "width": 3.0}
res = am._reconstruct_surface_3d(mp, [], [plain], grid_target=16)
assert res is not None and len(res["strokes"]) == 1
fallback = res["strokes"][0]
assert fallback["anchors"] == [] and fallback["src"] is None
assert len(fallback["points"]) >= 2
assert fallback["ribbon"] is not None
print("7) polyline fallback keeps the record shape (no anchors, has ribbon)")

print("t_warp3d: all cases passed")
