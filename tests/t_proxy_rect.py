"""Proxy rectangles (issue #9, spec docs/auto_mapping/proxy_rect.md): the corner
labelling of the texture and main rectangles, the rotation weight the user
set, and the main axes carried from the texture axes. Only the frame side:
the rectangle tools and the surface are other issues."""
import cmath
import math
import os
import sys
import types

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am
import proxy_rect as pr


def rect(w, h, cx=0.0, cy=0.0, deg=0.0, scale=1.0):
    turn = cmath.exp(1j * math.radians(deg)) * scale
    out = []
    for x, y in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
        z = complex(x, y) * turn + complex(cx, cy)
        out.append((z.real, z.imag))
    return out


def turned(corners, deg):
    turn = cmath.exp(1j * math.radians(deg))
    return [((complex(*p) * turn).real, (complex(*p) * turn).imag) for p in corners]


def drawing_orders(corners):
    """All 8 ways to walk a quad: 4 starting corners, 2 windings."""
    for start in range(4):
        order = [(start + i) % 4 for i in range(4)]
        yield order
        yield order[::-1]


def assigned(child, main, alignment):
    """child corner -> main corner, independent of list positions."""
    return {child[c]: main[m] for c, m in zip(alignment["child_order"],
                                              alignment["main_order"])}


def aligned(child, main, **kwargs):
    result, reason = pr.align(child, main, **kwargs)
    assert result is not None, reason
    return result


def near(a, b, tol=1e-9):
    return abs(a[0] - b[0]) <= tol and abs(a[1] - b[1]) <= tol


CHILD = rect(300, 200)
KEYSTONE = [(-120, -100), (120, -100), (150, 100), (-150, 100)]
QUAD = [(-170, -60), (90, -130), (210, 120), (-110, 90)]
AXES = ([(-150, 0), (150, 0)], [(0, -100), (0, 100)])      # CHILD's midlines


# 1) The same rectangle on both boards: nothing to rotate, the derived axes
#    agree, and the Auto Mapping identity carries over.
r = aligned(CHILD, CHILD)
assert r["main_order"] == r["child_order"]
assert abs(r["rotation"]) < 1e-12 and abs(r["scale"] - 1.0) < 1e-12
assert abs(r["residual"]) < 1e-12 and abs(r["cost"]) < 1e-12
g, why = pr.guides(CHILD, CHILD, *AXES)
map_point, _ = am.build_mapper(g["child"]["h"], g["child"]["v"], g["main"]["h"], g["main"]["v"])
assert all(near(map_point(p), p, 1e-9) for p in [(0, 0), (120, -70), (-140, 90)])
print("1) same rectangle: identity")

# 2) Translation and scale are free: a similarity with a small turn is
#    recovered exactly, and neither offset nor size can change the labelling.
main = rect(300, 200, 120, -80, 20, 1.3)
r = aligned(CHILD, main)
assert abs(math.degrees(r["rotation"]) - 20.0) < 1e-9
assert abs(r["scale"] - 1.3) < 1e-9 and abs(r["residual"]) < 1e-12
assert near(r["offset"], (120.0, -80.0))
for deg in (50.0, 57.0, 58.0, 59.0, 60.0, 61.0, 70.0):
    base = aligned(CHILD, rect(300, 200, deg=deg))
    for f, dx, dy in ((0.05, 0, 0), (20.0, 0, 0), (1.0, 5000, -3000), (7.0, -900, 40)):
        moved = aligned(CHILD, rect(300, 200, dx, dy, deg, f))
        assert moved["main_order"] == base["main_order"], (deg, f, dx, dy)
print("2) translation and scale cost nothing")

# 3) The result does not depend on how either rectangle was drawn, and no
#    winding ever produces a mirror.
for main in (rect(300, 200, deg=20), rect(300, 200, deg=75), KEYSTONE, QUAD):
    results = set()
    for co in drawing_orders(CHILD):
        for mo in drawing_orders(main):
            c = [CHILD[i] for i in co]
            m = [main[i] for i in mo]
            mapping = assigned(c, m, aligned(c, m))
            results.add(tuple(sorted(mapping.items())))
            g, why = pr.guides(c, m, *AXES)
            info = {}
            am.build_mapper(g["child"]["h"], g["child"]["v"], g["main"]["h"], g["main"]["v"], info)
            assert info["mirrored"] is False
    assert len(results) == 1, results
print("3) drawing order and winding change nothing, never mirrored")

# 4) The user's weight: the pattern keeps its orientation against ANY aspect
#    ratio when the main rectangle is the texture rectangle turned 90 degrees.
assert pr.ROTATION_WEIGHT == 1.0
for aspect in (1.2, 1.5, 2.0, 2.5, 3.0, 4.0, 8.0):
    child = rect(200 * aspect, 200)
    r = aligned(child, rect(200 * aspect, 200, deg=90.0))
    assert abs(r["rotation"]) < 1e-9, aspect
    expected = ((aspect * aspect - 1.0) / (aspect * aspect + 1.0)) ** 2
    assert abs(r["residual"] - expected) < 1e-9, aspect
# A lighter weight does turn a long strip: the weight is what decides.
r = aligned(rect(800, 200), rect(800, 200, deg=90.0), rotation_weight=0.5)
assert abs(abs(math.degrees(r["rotation"])) - 90.0) < 1e-9
print("4) rotation weight 1.0 keeps the orientation at every aspect")

# 5) Tipping angles of a turning rectangle (spec table, weight 1.0), and a
#    rectangle upside down reads as not rotated: it has no top of its own.
for aspect, tip in ((1.0, 45.0), (1.5, 51.00), (2.0, 59.75), (4.0, 78.40)):
    shape = ((aspect * aspect - 1.0) / (aspect * aspect + 1.0)) ** 2
    exact = 45.0 + math.degrees(math.asin(shape / math.sqrt(2.0)))
    assert abs(exact - tip) < 0.01, aspect
    child = rect(200 * aspect, 200)
    before = aligned(child, rect(200 * aspect, 200, deg=exact - 0.01))
    after = aligned(child, rect(200 * aspect, 200, deg=exact + 0.01))
    assert before["main_order"] != after["main_order"], aspect
    assert abs(math.degrees(before["rotation"]) - (exact - 0.01)) < 1e-9, aspect
r = aligned(CHILD, rect(300, 200, deg=180.0))
assert abs(r["rotation"]) < 1e-9
print("5) tipping angles and 180 degrees")

# 6) The cost is the stated formula, and its first term really is the best
#    similarity's relative residual: no (s, theta, t) on a grid beats it.
for main in (rect(300, 200, deg=25), KEYSTONE, QUAD):
    r = aligned(CHILD, main)
    assert abs(r["cost"] - (r["residual"] + 1.0 - math.cos(r["rotation"]))) < 1e-12
    a = [complex(*CHILD[i]) for i in r["child_order"]]
    b = [complex(*main[i]) for i in r["main_order"]]
    ca, cb = sum(a) / 4.0, sum(b) / 4.0
    total = sum(abs(z - cb) ** 2 for z in b)
    fit = cmath.rect(r["scale"], r["rotation"])
    got = sum(abs(fit * (x - ca) + cb - y) ** 2 for x, y in zip(a, b)) / total
    assert abs(got - r["residual"]) < 1e-9
    for ds in range(-3, 4):
        for dt in range(-3, 4):
            w = cmath.rect(r["scale"] * (1.0 + ds * 0.01), r["rotation"] + dt * 0.01)
            trial = sum(abs(w * (x - ca) + cb - y) ** 2 for x, y in zip(a, b)) / total
            assert trial >= r["residual"] - 1e-12
print("6) cost formula and similarity residual")

# 7) An exact tie is settled the same way from every drawing order.
main = rect(200, 200, deg=45.0)
results = set()
for mo in drawing_orders(main):
    m = [main[i] for i in mo]
    r = aligned(rect(200, 200), m)
    assert abs(math.degrees(r["rotation"]) - 45.0) < 1e-9
    results.add(tuple(sorted(assigned(rect(200, 200), m, r).items())))
assert len(results) == 1
print("7) exact tie is deterministic")

# 8) Axes: midlines bisect each other in any quad, so there is no per-side
#    scale kink, and the residual left for the edges is exactly the quad's
#    twist d: zero at edge midpoints, |d| at the corners.
for main in (KEYSTONE, QUAD, rect(200, 300, 40, 10)):
    g, why = pr.guides(CHILD, main, *AXES)
    info = {}
    map_point, _ = am.build_mapper(g["child"]["h"], g["child"]["v"],
                                   g["main"]["h"], g["main"]["v"], info)
    assert abs(info["h_scale_mismatch"] - 1.0) < 1e-9
    assert abs(info["v_scale_mismatch"] - 1.0) < 1e-9
    for family in ("h", "v"):
        for ce, me in zip(g["child"]["edges"][family], g["main"]["edges"][family]):
            mid_child = ((ce[0][0] + ce[1][0]) * 0.5, (ce[0][1] + ce[1][1]) * 0.5)
            mid_main = ((me[0][0] + me[1][0]) * 0.5, (me[0][1] + me[1][1]) * 0.5)
            assert near(map_point(mid_child), mid_main, 1e-6)
    q = [complex(*p) for p in (g["main"]["edges"]["h"][0] + g["main"]["edges"]["h"][1][::-1])]
    twist = abs(q[0] - q[1] + q[2] - q[3]) / 4.0
    for ce, me in ((g["child"]["edges"]["h"][0], g["main"]["edges"]["h"][0]),
                   (g["child"]["edges"]["h"][1], g["main"]["edges"]["h"][1])):
        for c, m in zip(ce, me):
            p = map_point(c)
            assert abs(math.hypot(p[0] - m[0], p[1] - m[1]) - twist) < 1e-6
print("8) midline axes: no scale kink, corner residual is the quad's twist")

# 9) Edges of equal index start and end on corresponding corners, also where
#    a skewed quad's edge leans against its axis.
for main in (turned(KEYSTONE, 20.0), [(0, 0), (100, 0), (180, 67), (182, 79)]):
    g, why = pr.guides(CHILD, main, *AXES)
    assert why == ""
    corner_of = assigned(CHILD, [(float(x), float(y)) for x, y in main], g["alignment"])
    for family in ("h", "v"):
        for ce, me in zip(g["child"]["edges"][family], g["main"]["edges"][family]):
            assert corner_of[ce[0]] == me[0] and corner_of[ce[1]] == me[1]
print("9) edges pair corner to corner")

# 10) Nothing is stored: the same input gives the same answer, and a rectangle
#     turned past the tipping angle changes labelling on the next call.
a = aligned(CHILD, rect(300, 200, deg=40.0))
b = aligned(CHILD, rect(300, 200, deg=40.0))
c = aligned(CHILD, rect(300, 200, deg=70.0))
assert a == b and a["main_order"] != c["main_order"]
print("10) stateless: recomputed on every edit")

# 11) What cannot be a proxy rectangle is refused with a reason.
for child, main, word in ((CHILD[:3], CHILD, "4 corners"),
                          (CHILD, [(0, 0), (1, 1), (2, 2), (3, 3)], "degenerate"),
                          (CHILD, [(0, 0), (0, 0), (0, 0), (0, 0)], "degenerate"),
                          (CHILD, [(0, 0), (0, 0), (100, 0), (0, 100)], "degenerate"),
                          ([(0, 0), (0, 0), (100, 0), (0, 100)], CHILD, "degenerate"),
                          (CHILD, [(0, 0), (math.nan, 0), (100, 0), (0, 100)], "degenerate"),
                          (CHILD, [(0, 0), (100, 100), (100, 0), (0, 100)], "crosses"),
                          ([(0, 0), (100, 100), (100, 0), (0, 100)], CHILD, "crosses")):
    result, reason = pr.align(child, main)
    assert result is None and word in reason, reason
    result, reason = pr.guides(child, main, *AXES)
    assert result is None and word in reason, reason
result, reason = pr.guides(KEYSTONE, CHILD, *AXES)
assert result is None and "parallelogram" in reason, reason
print("11) unusable quads are refused")

# 12) Inputs where a tie used to leak the drawing order: costs a hair apart,
#     equal costs at equal angles, collinear neighbouring edges. Every pair of
#     drawing orders must give one assignment and one frame.
for child, main in (
        ([(239.92253847990526, 153.78450000310312), (383.19605482332287, 293.32911478487256),
          (243.65144004155343, 436.60263112829017), (100.37792369813582, 297.05801634652073)],
         [(613.2955989808116, 38.950083465508214), (623.2734879398395, 795.7220209062979),
          (-133.4984495009502, 805.6999098653257), (-143.47633845997802, 48.92797242453605)]),
        ([(-100, -100), (100, -500), (100, 500), (-100, 100)],
         [(-300, 0), (0, -100), (-100, 0), (0, 100)]),
        ([(0, 0), (100, 0), (200, 0), (100, 100)], rect(300, 200, deg=20))):
    assignments, frames = set(), set()
    for co in drawing_orders(child):
        for mo in drawing_orders(main):
            c = [child[i] for i in co]
            m = [main[i] for i in mo]
            assignments.add(tuple(sorted(assigned(c, m, aligned(c, m)).items())))
            g, why = pr.guides(c, m, *AXES)
            if g is not None:
                frames.add(tuple(tuple(g["main"][f]) for f in ("h", "v")))
    assert len(assignments) == 1 and len(frames) <= 1, (assignments, frames)
print("12) ties never see the drawing order")

# 13) Small integer corners cannot overflow on their way to the axes.
g, why = pr.guides(np.array(rect(200, 200, 100, 100), dtype=np.uint8), CHILD,
                   [(0, 100), (200, 100)], [(100, 0), (100, 200)])
assert why == "" and near(g["main"]["h"][-1], (150.0, 0.0))
print("13) axes are computed in floats")

# 14) The texture rectangle only marks what maps (user 2026-10-30): whatever it
#     does, the texture axes come back untouched, and it is the mapping that
#     changes. Corners land on corners even with the axes far off its centre.
main = rect(300, 200, 40, -30, 20, 1.2)
for child in (rect(150, 100, 30, -20), rect(90, 260, -60, 40), rect(300, 200, 500, 300)):
    g, why = pr.guides(child, main, *AXES)
    assert why == ""
    assert g["child"]["h"] == [(-150.0, 0.0), (150.0, 0.0)]
    assert g["child"]["v"] == [(0.0, -100.0), (0.0, 100.0)]
    map_point, _ = am.build_mapper(g["child"]["h"], g["child"]["v"], g["main"]["h"], g["main"]["v"])
    assert map_point is not None
    for family in ("h", "v"):
        for ce, me in zip(g["child"]["edges"][family], g["main"]["edges"][family]):
            assert near(map_point(ce[0]), me[0], 1e-6) and near(map_point(ce[1]), me[1], 1e-6)
print("14) the rectangle never moves the texture axes")

# 15) The carried main axes: a point of a texture axis keeps its place in the
#     rectangle, parallel axes stay straight with even sides, and a slanted
#     axis still maps end to end on a main quad that bends it.
g, why = pr.guides(CHILD, KEYSTONE, *AXES)
assert all(abs(p[1]) < 1e-9 for p in g["main"]["h"]) and near(g["main"]["h"][0], (-135.0, 0.0))
slanted = ([(-150, -40), (150, 40)], [(30, -100), (-30, 100)])
g, why = pr.guides(CHILD, KEYSTONE, *slanted)
assert len(g["main"]["h"]) == pr._AXIS_SAMPLES + 1
assert near(g["main"]["h"][0], ((1 - 0.3) * -120.0 + 0.3 * -150.0, -40.0), 1e-9)
map_point, _ = am.build_mapper(g["child"]["h"], g["child"]["v"], g["main"]["h"], g["main"]["v"])
assert near(map_point(slanted[0][1]), g["main"]["h"][-1], 1e-6)
assert near(map_point(slanted[1][0]), g["main"]["v"][0], 1e-6)
print("15) axes carried by place in the rectangle")

print("ALL OK")
