"""_Frame.solve_full: an unfoldable frame is inverted by chord_intersect, a foldable one stays on the damped
Newton whose branch defines the severing verdict (docs/auto_mapping/topology_severing.md)."""
import math
import os
import random
import sys
import time
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am

H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]


def sine_h(a):
    return [(-300.0 + 6.0 * k, a * math.sin(2 * math.pi * (-300.0 + 6.0 * k) / 300.0)) for k in range(101)]


def sine_v(a):
    return [(a * math.sin(2 * math.pi * (-200.0 + 4.0 * k) / 300.0), -200.0 + 4.0 * k) for k in range(101)]


rng = random.Random(3)
probes = [(rng.uniform(-420.0, 420.0), rng.uniform(-320.0, 320.0)) for _ in range(800)]

# 1) unfoldable polyline frames, both orientations: the locator's root is hv's exact preimage, with no seed
#    worth the name, also far outside the guides on the extensions
for h, v in ((H, V), (sine_h(30), sine_v(30)), (sine_h(30), list(reversed(sine_v(30))))):
    frame = am._Frame(h, v)
    assert not frame.can_fold()
    for p in probes:
        l_h, l_v, residual = frame.solve_full(p, 1e6, -1e6)
        assert residual <= 1e-9, (p, residual)
        q = frame.hv(l_h, l_v)
        assert math.hypot(q[0] - p[0], q[1] - p[1]) <= 1e-9, (p, q)
    assert frame._locator is not None
print("1) unfoldable polyline frames: exact preimage from any guess")

# 2) a point Newton plateaus on is simply solved: the curved 7-degree frame of t_sever #12
tilt = math.radians(7.0)
v7 = [(-500.0 * math.cos(tilt), -500.0 * math.sin(tilt)), (500.0 * math.cos(tilt), 500.0 * math.sin(tilt))]


def _xy(p):
    return {"x": p[0], "y": p[1]}


bow = {"points": am._densify([(-300.0, 0.0), (0.0, 15.0), (300.0, 0.0)]), "width": 3.0,
       "commands": [{"type": "move", "to": _xy((-300.0, 0.0))},
                    {"type": "cubic", "control1": _xy((-100.0, 20.0)), "control2": _xy((100.0, 20.0)),
                     "to": _xy((300.0, 0.0))}]}
frame = am._Frame(bow, v7)
assert not frame.can_fold() and frame.gh.curved
plateaus = 0
for p in [(-600.0, 800.0), (0.0, 800.0), (180.0, 650.0), (400.0, 400.0)]:
    plateaus += frame._descend(p, 0.0, 0.0)[2] > am._SEVER_RESIDUAL
    assert frame.solve_full(p, 0.0, 0.0)[2] <= 1e-7, p
assert plateaus, "the descent must actually stall on this frame"
print("2) curved frame: %d point(s) the descent stalls on are solved to 1e-7" % plateaus)

# 3) foldable frames: solve_full IS the descent, bit for bit, and the locator is never built
hook = [(-300.0, 0.0), (100.0, 0.0)] + [(100.0 + 60.0 * math.sin(math.radians(2.5 * k)),
                                         60.0 * (1.0 - math.cos(math.radians(2.5 * k)))) for k in range(1, 61)]
for h, v in ((hook, V), (sine_h(100), sine_v(100))):
    frame = am._Frame(h, v)
    assert frame.can_fold()
    for p in probes[:200]:
        assert frame.solve_full(p, 10.0, -20.0) == frame._descend(p, 10.0, -20.0)
    assert frame._locator is None
print("3) foldable frames: the descent, bit for bit")

# 4) the gate is per frame: a foldable MAIN frame keeps its Newton beside an unfoldable child
mp, _ws = am.build_mapper(H, V, hook, V, {})
assert not mp.can_fold() and mp.main_frame.can_fold() and not mp.child_frame.can_fold()
print("4) can_fold is each frame's own")
# 5) the gate's sorted lookup is the minimum over every pair of directions
rng = random.Random(8)
seen = {True: 0, False: 0}
for _ in range(300):
    spread = rng.choice((0.3, 0.6, 1.0, 1.5, 2.5))

    def guide(base):
        p = [(0.0, 0.0)]
        for _ in range(rng.randint(1, 30)):
            a = base + rng.uniform(-spread, spread)
            p.append((p[-1][0] + 9.0 * math.cos(a), p[-1][1] + 9.0 * math.sin(a)))
        return p

    frame = am._Frame(guide(0.0), guide(rng.choice((1.5708, -1.5708))))
    hand = frame.hand()

    def unit(g):
        return [((b[0] - a[0]) / 9.0, (b[1] - a[1]) / 9.0) for a, b in zip(g.points, g.points[1:])]

    lowest = min((a[0] * b[1] - a[1] * b[0]) * hand for a in unit(frame.gh) for b in unit(frame.gv))
    if abs(lowest - am._SEVER_GATE_SIN) > 1e-9:
        assert frame.can_fold() == (lowest <= am._SEVER_GATE_SIN), lowest
        seen[frame.can_fold()] += 1
assert seen[True] and seen[False], seen
print("5) can_fold == the brute minimum over all direction pairs (%d folding, %d not)" % (seen[True], seen[False]))

# 6) the crossing: the box trees give the double loop's answer to the last bit, the first crossing pair in
#    (i, j) order or else the first closest vertex pair, with the same eps on each segment's own length
def _double_loop(a, b):
    a_cum, b_cum = am._cumulative_lengths(a), am._cumulative_lengths(b)
    eps = 1e-6
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = am._segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -eps <= t <= 1.0 + eps and -eps <= u <= 1.0 + eps:
                point = (a[i][0] + (a[i + 1][0] - a[i][0]) * t, a[i][1] + (a[i + 1][1] - a[i][1]) * t)
                return (point, a_cum[i] + (a_cum[i + 1] - a_cum[i]) * min(max(t, 0.0), 1.0),
                        b_cum[j] + (b_cum[j + 1] - b_cum[j]) * min(max(u, 0.0), 1.0))
    best = None
    for i, pa in enumerate(a):
        for j, pb in enumerate(b):
            d = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
            if best is None or d < best[0]:
                best = (d, i, j)
    _, i, j = best
    return ((a[i][0] + b[j][0]) * 0.5, (a[i][1] + b[j][1]) * 0.5), a_cum[i], b_cum[j]


def _walk(n, step, start, snap):
    p = [start]
    for _ in range(n):
        x, y = p[-1][0] + rng.uniform(-step, step), p[-1][1] + rng.uniform(-step, step)
        p.append((float(round(x)), float(round(y))) if snap else (x, y))
    return p


crossing = fallback = 0
for k in range(1400):
    n, m = rng.randint(1, 35), rng.randint(1, 35)
    kind = k % 7
    if kind == 0:
        a, b = _walk(n, 12.0, (0.0, 0.0), False), _walk(m, 12.0, (5.0, 3.0), False)
    elif kind == 1:                                     # apart: the closest vertices
        a, b = _walk(n, 9.0, (0.0, 0.0), False), _walk(m, 9.0, (900.0, 0.0), False)
    elif kind == 2:                                     # integer: ties, shared vertices, zero-length segments
        a, b = _walk(n, 5.0, (0.0, 0.0), True), _walk(m, 5.0, (3.0, 2.0), True)
    elif kind == 3:                                     # every distance tied
        a, b = [(float(i), 0.0) for i in range(n + 1)], [(float(i), 7.0) for i in range(m + 1)]
    elif kind == 4:                                     # collinear and apart
        a = [(float(i), 2.0 * i) for i in range(n + 1)]
        b = [(float(i) + n + 3.0, 2.0 * (i + n + 3.0)) for i in range(m + 1)]
    elif kind == 5:                                     # a T: one end stops short of the other guide, inside eps
        a = [(-100.0, 0.0), (100.0, 0.0)]
        b = [(rng.uniform(-90.0, 90.0), rng.choice((-1, 1)) * rng.uniform(0.0, 3e-5)),
             (rng.uniform(-90.0, 90.0), rng.uniform(20.0, 90.0))]
    else:                                               # lengths far apart: eps is large on one side only
        a, b = [(-1e6, -3.0), (1e6, 3.0)] + _walk(n, 2.0, (1e6, 3.0), False), _walk(m, 3.0, (0.0, 1.0), False)
    want = _double_loop(a, b)
    assert am._polyline_intersection(a, b) == want, (a, b)
    hits = am._polylines_cross(a, b)
    assert hits == any(-1e-6 <= h[0] <= 1.0 + 1e-6 and -1e-6 <= h[1] <= 1.0 + 1e-6
                       for h in (am._segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
                                 for i in range(len(a) - 1) for j in range(len(b) - 1)) if h)
    crossing += hits
    fallback += not hits
assert crossing and fallback
print("6) crossing == the double loop on 1400 rigs (%d crossing, %d through the closest vertices)" % (crossing, fallback))

# 7) the budget: at 3000 x 3000 the whole of what a frame needs before its answers, crossing, gate, locator, grid,
#    with a point on the sheet and one on the extensions, stays under 200 ms; also where the guides never cross
n = 3000
big_h = [(-6000.0 + 4.0 * k, 25.0 * math.sin(k / n * 6.0)) for k in range(n + 1)]
big_v = [(20.0 * math.sin(k / n * 5.0), -6000.0 + 4.0 * k) for k in range(n + 1)]
best = None
for _ in range(3):
    start = time.perf_counter()
    frame = am._Frame(big_h, big_v)
    for p in ((1.0, 2.0), (7000.0, 3.0)):
        assert frame.solve_full(p, 0.0, 0.0)[2] <= 1e-9
    spent = time.perf_counter() - start
    best = spent if best is None else min(best, spent)
assert not frame.can_fold()
assert best < 0.2, best
apart = None
for _ in range(3):
    start = time.perf_counter()
    far = am._Frame(big_h, [(x + 9000.0, y) for x, y in big_v])
    spent = time.perf_counter() - start
    apart = spent if apart is None else min(apart, spent)
assert apart < 0.2, apart
# the loosest boxes: two spirals winding between each other, near everywhere and never crossing
spiral = [(k * 0.5 * math.cos(k * 0.02), k * 0.5 * math.sin(k * 0.02)) for k in range(1, n + 2)]
start = time.perf_counter()
am._polyline_intersection(spiral, [(-x, -y) for x, y in spiral])
wound = time.perf_counter() - start
assert wound < 0.2, wound
assert not am._polylines_cross(spiral, [(-x, -y) for x, y in spiral])
print("7) 3000 x 3000: crossing to two first solves %.1f ms, guides apart %.1f ms, wound spirals %.1f ms" % (
    best * 1e3, apart * 1e3, wound * 1e3))
print("t_frame_solver: ok")
