"""Ground lines (spec docs/auto_mapping/ground_line.md, plan
docs/plan/2026-09-30-地线-GroundLine-计划.md): a line that pins a stretch of
pattern where it was drawn, as opposed to an additional line, which only
guides flow. These cases pin the position promise, the axis rules (parallel
axis holds, orthogonal axis yields), the run-on to the domain edge, the
closed contour with its corners, and that nothing changes without them."""
import cmath
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am
import proxy_rect as pr

H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]
RECT = [(-150.0, -100.0), (150.0, -100.0), (150.0, 100.0), (-150.0, 100.0)]
RECT_AXES = ([(-150.0, 0.0), (150.0, 0.0)], [(0.0, -100.0), (0.0, 100.0)])


def line(a, b, n=16):
    return [(a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n) for i in range(n + 1)]


def bowed(a, b, bulge, n=48):
    nx, ny = b[1] - a[1], a[0] - b[0]
    return [(p[0] + nx * bulge * 4.0 * (i / n) * (1.0 - i / n),
             p[1] + ny * bulge * 4.0 * (i / n) * (1.0 - i / n))
            for i, p in enumerate(line(a, b, n))]


def asset(points, line_id=0):
    return {"points": [list(p) for p in points], "width": 3.0, "id": line_id}


def shifted(points, dx, dy):
    return [(p[0] + dx, p[1] + dy) for p in points]


def mapper(grounds=None, additional=None, frame=(H, V, H, V), info=None):
    mp, why = am.build_mapper(*frame, info, additional_pairs=additional,
                              ground_pairs=grounds)
    assert mp is not None, why
    return mp


def ground(child, main):
    return (asset(child), asset(main))


def miss(mp, child, main):
    return max(math.hypot(mp(c)[0] - m[0], mp(c)[1] - m[1]) for c, m in zip(child, main))


def edge_miss(mp, pair):
    """miss() over a ground pair, stations paired by arc fraction as the mapper
    pairs them."""
    c, m = pair
    stations = len(c["points"])
    drawn = am._resample_fractions([tuple(p) for p in m["points"]], stations)
    return miss(mp, [tuple(p) for p in c["points"]], drawn)


def turn(mp, points):
    mapped = [mp(p) for p in points]
    worst = 0.0
    for a, b, c in zip(mapped, mapped[1:], mapped[2:]):
        ux, uy, vx, vy = b[0] - a[0], b[1] - a[1], c[0] - b[0], c[1] - b[1]
        lengths = math.hypot(ux, uy) * math.hypot(vx, vy)
        if lengths > 1e-9:
            cosine = max(-1.0, min(1.0, (ux * vx + uy * vy) / lengths))
            worst = max(worst, math.degrees(math.acos(cosine)))
    return worst


def closed_contour(quad, bulge=0.0):
    g, why = pr.guides(RECT, quad, *RECT_AXES)
    assert g is not None, why
    pairs = []
    for family, sign in (("h", -1.0), ("v", 1.0)):
        for index, (ce, me) in enumerate(zip(g["child"]["edges"][family],
                                             g["main"]["edges"][family])):
            side = sign * (1.0 if index else -1.0)
            pairs.append(ground(line(ce[0], ce[1], 24), bowed(me[0], me[1], side * bulge)))
    frame = (g["child"]["h"], g["child"]["v"], g["main"]["h"], g["main"]["v"])
    return g, pairs, mapper(pairs, frame=frame)


# 1) Without ground lines nothing changes: an empty list is no list, and a
#    mapper asked for neither has no warp at all.
assert mapper().warp is None and mapper(grounds=[]).warp is None
pairs = [(asset(line((60, 50), (260, 90)), 1), asset(line((60, 80), (260, 150)), 1))]
plain = mapper(additional=pairs)
both = mapper(additional=pairs, grounds=[])
for p in [(0.0, 0.0), (100.0, 60.0), (-80.0, 120.0), (250.0, -150.0)]:
    assert plain(p) == both(p)
assert plain.warp.grounds == [] and len(plain.warp.pairs) == 1
print("1) no ground lines, no change")

# 2) THE PROMISE: the pattern on the line lands on the drawn line, point for
#    point, where an additional line asking the same only guides flow (a pure
#    translation pair is inert by design).
child = line((60.0, 120.0), (260.0, 120.0))
main = shifted(child, -30.0, 25.0)
mp = mapper([ground(child, main)])
assert miss(mp, child, main) < 0.1
assert miss(mapper(additional=[(asset(child, 1), asset(main, 1))]), child, main) > 30.0
assert mp.warp._all_positive
print("2) a ground line lands where it was drawn")

# 3) Moving it: the PARALLEL axis never moves, the ORTHOGONAL axis yields on
#    the line's half and only there, the origin holds, nothing folds.
for shift in ((0.0, 25.0), (0.0, 50.0), (0.0, 80.0), (0.0, -60.0)):
    mp = mapper([ground(child, shifted(child, *shift))])
    assert mp.warp._all_positive, shift
    for x in (-250.0, -120.0, 120.0, 250.0):
        assert math.hypot(mp((x, 0.0))[0] - x, mp((x, 0.0))[1]) < 1e-6
    assert math.hypot(*mp((0.0, 0.0))) < 1e-6
    assert abs(mp((0.0, 120.0))[1] - 120.0 - shift[1]) < 0.06 * abs(shift[1]) + 0.1
    assert math.hypot(mp((0.0, -120.0))[0], mp((0.0, -120.0))[1] + 120.0) < 1e-6
print("3) parallel axis holds, orthogonal axis yields on the line's half")

# 4) The run-on (user 2026-10-01): beyond both ends the line's move fades to
#    the domain edge without ever reversing, and the tip does not hook.
mp = mapper([ground(child, shifted(child, 0.0, 50.0))])
beyond = [(260.0 + 5.0 * k, 120.0) for k in range(20)]
moves = [mp(p)[1] - 120.0 for p in beyond]
assert moves[0] > 45.0 and all(b <= a + 0.1 for a, b in zip(moves, moves[1:]))
assert turn(mp, beyond) < 6.5       # the fade's own bend, spread to the edge
print("4) the move fades along the tangent, no hook")

# 5) A line that stays put pins the pattern where it is, beside a flow line
#    that would otherwise carry it away (the float of plan decision 9).
flow = [(asset(line((120.0, -120.0), (120.0, 120.0)), 1),
         asset([(p[0] + 40.0 * (1.0 - (p[1] / 120.0) ** 2), p[1])
                for p in line((120.0, -120.0), (120.0, 120.0))], 1))]
stay = line((170.0, 60.0), (280.0, 60.0))
drift = miss(mapper(additional=flow), stay, stay)
held = miss(mapper([ground(stay, stay)], additional=flow), stay, stay)
assert drift > 8.0 and held < 0.1, (drift, held)
print(f"5) a still ground line holds the pattern: {drift:.1f} px -> {held:.2f} px")

# 6) A moved ground line takes the outline with it; a still one leaves the
#    outline constrained.
behind = (160.0, 200.0)
assert mapper([ground(child, shifted(child, 0.0, 50.0))])(behind)[1] > 205.0
assert abs(mapper([ground(stay, stay)])(behind)[1] - 200.0) < 1e-6
print("6) the outline follows a moved line only")

# 7) CLOSED CONTOUR: four edges on a keystone are hit to a tenth of a pixel,
#    the inside is the Coons patch of those edges, smooth and unfolded, the
#    midline axes stay where they are, and the ground outside the corners
#    does not fold either.
for quad, limit in (([(-120, -100), (120, -100), (150, 100), (-150, 100)], 0.4),
                    ([(-105, -125), (125, -130), (195, 130), (-215, 130)], 0.8),
                    ([(-40, -100), (40, -100), (190, 100), (-190, 100)], 0.6),
                    ([(-170, -60), (90, -130), (210, 120), (-110, 90)], 0.5)):
    g, pairs, mp = closed_contour(quad)
    assert mp.warp._all_positive, quad
    assert max(edge_miss(mp, pair) for pair in pairs) < 0.15
    edges = g["main"]["edges"]["h"]
    q = [complex(*p) for p in (edges[0][0], edges[0][1], edges[1][1], edges[1][0])]
    for i in range(1, 12):
        for j in range(1, 12):
            u, v = i / 12.0, j / 12.0
            z = ((1 - u) * (1 - v) * q[0] + u * (1 - v) * q[1]
                 + u * v * q[2] + (1 - u) * v * q[3])
            p = mp((-150.0 + 300.0 * u, -100.0 + 200.0 * v))
            assert abs(complex(*p) - z) < limit, (quad, u, v)
    for x in (-120.0, -60.0, 60.0, 120.0):
        assert turn(mp, line((x, -80.0), (x, 80.0), 32)) < 2.0
    for y in (-60.0, 60.0):
        assert turn(mp, line((-130.0, y), (130.0, y), 48)) < 2.0
    assert miss(mp, [(0.0, 0.0)], [g["main"]["h"][len(g["main"]["h"]) // 2]]) < 1e-6
print("7) a closed contour gives the Coons patch of its edges")

# 8) Bowed edges move their crossings, so the axes let go instead of creasing
#    where they meet the edge; the fold-free promise holds.
quad = [(-120, -100), (120, -100), (150, 100), (-150, 100)]
g, pairs, mp = closed_contour(quad, bulge=0.08)
assert mp.warp._all_positive
assert max(edge_miss(mp, pair) for pair in pairs) < 0.2
assert turn(mp, line((-130.0, 0.0), (130.0, 0.0), 48)) < 1.0
assert turn(mp, line((0.0, -90.0), (0.0, 90.0), 32)) < 1.0
assert math.hypot(mp((0.0, -100.0))[0] - g["main"]["v"][0][0],
                  mp((0.0, -100.0))[1] - g["main"]["v"][0][1]) > 10.0
print("8) bowed edges release the axes")

# 9) A partner drawn backwards is the same line, and stations pair by canvas
#    arc fraction whatever the two sides' point counts are.
mp = mapper([ground(child, list(reversed(shifted(child, 0.0, 40.0))))])
assert miss(mp, child, shifted(child, 0.0, 40.0)) < 0.1
sparse = [shifted(child, 0.0, 40.0)[0], shifted(child, 0.0, 40.0)[-1]]
mp = mapper([ground(child, sparse)])
assert miss(mp, child, shifted(child, 0.0, 40.0)) < 0.1
print("9) partner direction and point count do not matter")

# 10) Ground outranks additional lines near it, and they still shape the rest.
cross = line((180.0, -150.0), (180.0, 150.0))
flow = [(asset(cross, 1), asset([(p[0] + 30.0 * (1.0 - (p[1] / 150.0) ** 2), p[1])
                                 for p in cross], 1))]
pin = line((100.0, 40.0), (260.0, 40.0))
mp = mapper([ground(pin, pin)], additional=flow)
free = mapper(additional=flow)
assert miss(mp, pin, pin) < 0.1
bend = mp((180.0, -70.0))[0] - mp((180.0, -140.0))[0]
free_bend = free((180.0, -70.0))[0] - free((180.0, -140.0))[0]
assert abs(bend - free_bend) < 1.0, (bend, free_bend)
print("10) ground lines hold position, additional lines keep the flow's shape")

# 11) Crossing its own parallel axis cannot be honoured without folding: the
#     line still lands, and the mapper says so instead of hiding it.
info = {}
mp = mapper([ground(line((60.0, 40.0), (260.0, 40.0)),
                    line((60.0, -40.0), (260.0, -40.0)))], info=info)
assert not mp.warp._all_positive
assert any("fold" in note for note in mp.warp.notes)
print("11) crossing the parallel axis folds and says so")

# 12) The inverse stays consistent with the forward map on a closed contour.
g, pairs, mp = closed_contour([(-105, -125), (125, -130), (195, 130), (-215, 130)])
warp = mp.warp
for p in [(-120.0, -80.0), (0.0, 40.0), (130.0, 90.0), (75.0, -30.0), (-40.0, 70.0)]:
    back = warp.unapply(warp.apply(p))
    assert math.hypot(back[0] - p[0], back[1] - p[1]) < 1e-3, p
print("12) apply and unapply agree")

# 13) A ground line outside the frame window still works and needs no tent.
far = line((340.0, 120.0), (460.0, 120.0))
mp = mapper([ground(far, shifted(far, 10.0, 30.0))])
assert miss(mp, far, shifted(far, 10.0, 30.0)) < 0.2
assert mp.warp._tent["x"][1][0] == 0.0 and mp.warp._tent["y"][1][0] == 0.0
print("13) a line outside the window needs no tent")

# 14) Direction is judged in Third: a main frame turned past 90 degrees does
#     not make an unmoved partner read as drawn backwards.
turn_135 = cmath.exp(1j * math.radians(135.0))


def turned(points):
    return [((complex(*p) * turn_135).real, (complex(*p) * turn_135).imag) for p in points]


mp = mapper([ground(child, turned(child))], frame=(H, V, turned(H), turned(V)))
assert miss(mp, child, turned(child)) < 0.1 and mp.warp._all_positive
print("14) a turned main frame is not a reversed partner")

# 15) A contour that does not surround the origin moves rigidly without
#     folding: its tails cross the axes, and the axes they cross give way.
for box, move in (((60, 60, 260, 160), (0, 30)), ((60, 60, 260, 160), (25, 25)),
                  ((60, 60, 260, 160), (0, -30)), ((-150, 40, 150, 180), (0, 30)),
                  ((40, 40, 120, 120), (10, 10))):
    a, b, c, d = box
    q = [(a, b), (c, b), (c, d), (a, d)]
    pairs = [ground(line(q[i], q[(i + 1) % 4], 24),
                    shifted(line(q[i], q[(i + 1) % 4], 24), *move)) for i in range(4)]
    mp = mapper(pairs)
    assert mp.warp._all_positive, box
    assert max(edge_miss(mp, pair) for pair in pairs) < 0.1, box
print("15) off-centre contours move without folding")

# 16) Another ground line stops a tail, and the tail fades to THAT line's move.
left, right = line((-260.0, 120.0), (-80.0, 120.0)), line((80.0, 120.0), (260.0, 120.0))
for up, other in ((40.0, 0.0), (40.0, -40.0), (40.0, 40.0)):
    mp = mapper([ground(left, shifted(left, 0.0, up)), ground(right, shifted(right, 0.0, other))])
    assert mp.warp._all_positive, (up, other)
    assert miss(mp, left, shifted(left, 0.0, up)) < 0.1
    assert miss(mp, right, shifted(right, 0.0, other)) < 0.1
print("16) ground lines stop each other's tails")

# 17) No dead zone: a small move across the orthogonal axis lands too.
wide = line((-100.0, 120.0), (100.0, 120.0))
for dy in (0.3, 2.0, 4.05, 4.2):
    mp = mapper([ground(wide, shifted(wide, 0.0, dy))])
    assert abs(mp((0.0, 120.0))[1] - 120.0 - dy) < 0.05, dy
print("17) small moves land")

# 18) A line about a cell long is not a closed loop.
for x, span in ((100.0, 0.5), (100.0, 6.0), (40.0, 6.0)):
    tiny = line((x, 120.0), (x + span, 120.0), 4)
    assert mapper([ground(tiny, shifted(tiny, 0.0, 50.0))]).warp._all_positive, (x, span)
print("18) short lines do not fold")

# 19) One moved crossing frees the whole inside of a contour around the origin:
#     three bowed edges do not leave a half-axis creasing at a pinned origin.
quad = [(-120, -100), (120, -100), (150, 100), (-150, 100)]
g, why = pr.guides(RECT, quad, *RECT_AXES)
pairs = []
for family, sign in (("h", -1.0), ("v", 1.0)):
    for index, (ce, me) in enumerate(zip(g["child"]["edges"][family], g["main"]["edges"][family])):
        bulge = 0.0 if (family, index) == ("v", 1) else sign * (1.0 if index else -1.0) * 0.08
        pairs.append(ground(line(ce[0], ce[1], 24), bowed(me[0], me[1], bulge)))
mp = mapper(pairs, frame=(g["child"]["h"], g["child"]["v"], g["main"]["h"], g["main"]["v"]))
assert mp.warp._all_positive
assert turn(mp, line((-130.0, 0.0), (130.0, 0.0), 48)) < 0.5
assert turn(mp, line((0.0, -90.0), (0.0, 90.0), 32)) < 0.5
print("19) a contour frees its whole inside")

# 20) A tail heading for its parallel axis fades out there instead of ramming
#     it, and a still ground line does not take the blame for someone's fold.
slope = line((60.0, 100.0), (260.0, 30.0))
assert mapper([ground(slope, shifted(slope, 0.0, -15.0))]).warp._all_positive
print("20) tails fade at the parallel axis")

print("ALL OK")
