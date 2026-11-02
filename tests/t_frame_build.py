"""_Frame's guide crossing and fold gate.

The crossing walks the segment box trees of pyfile/polyline_boxes.py and must give the old double loop's answer to
the last bit; can_fold is each frame's own, cached on the frame, and equals the minimum over every pair of directions.
Taken from PR #18 (its t_frame_solver sections 4-7, same seeds, so the same 300 frames and 1400 rigs); the solver
that PR also brings is not part of this."""
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
hook = [(-300.0, 0.0), (100.0, 0.0)] + [(100.0 + 60.0 * math.sin(math.radians(2.5 * k)),
                                         60.0 * (1.0 - math.cos(math.radians(2.5 * k)))) for k in range(1, 61)]

# 1) the gate is per frame: a foldable MAIN frame beside an unfoldable child, each cached on its own frame
mp, _ws = am.build_mapper(H, V, hook, V, {})
assert not mp.can_fold() and mp.main_frame.can_fold() and not mp.child_frame.can_fold()
assert mp.child_frame._fold_gate is False and mp.main_frame._fold_gate is True
print("1) can_fold is each frame's own, cached on the frame")

# 2) the gate's sorted lookup is the minimum over every pair of directions
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
print("2) can_fold == the brute minimum over all direction pairs (%d folding, %d not)" % (seen[True], seen[False]))


# 3) the crossing: the box trees give the double loop's answer to the last bit, the first crossing pair in
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
print("3) crossing == the double loop on 1400 rigs (%d crossing, %d through the closest vertices)" % (crossing, fallback))


# 4) curve guides: _curve_crossing seeds on the flattenings with the same box trees, so its seed - and the Newton
#    refinement from it - is the double loop's to the last bit, crossing or not. The reference is the function as
#    it was before the box trees.
def _double_loop_curves(gh, gv):
    a, b = gh.points, gv.points
    seed = None
    eps = 1e-6
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = am._segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -eps <= t <= 1.0 + eps and -eps <= u <= 1.0 + eps:
                t = min(max(t, 0.0), 1.0)
                u = min(max(u, 0.0), 1.0)
                seed = (gh.flat_arcs[i] + (gh.flat_arcs[i + 1] - gh.flat_arcs[i]) * t,
                        gv.flat_arcs[j] + (gv.flat_arcs[j + 1] - gv.flat_arcs[j]) * u)
                break
        if seed is not None:
            break
    if seed is None:
        best = None
        for i, pa in enumerate(a):
            for j, pb in enumerate(b):
                distance = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
                if best is None or distance < best[0]:
                    best = (distance, i, j)
        _, i, j = best
        point = ((a[i][0] + b[j][0]) * 0.5, (a[i][1] + b[j][1]) * 0.5)
        return point, gh.flat_arcs[i], gv.flat_arcs[j]

    arc_h, arc_v = seed
    for _ in range(12):
        ph = gh.point_at(arc_h)
        pv = gv.point_at(arc_v)
        fx, fy = ph[0] - pv[0], ph[1] - pv[1]
        if math.hypot(fx, fy) <= 1e-10:
            break
        hx, hy = gh.dir_at(arc_h)
        vx, vy = gv.dir_at(arc_v)
        det = vx * hy - hx * vy
        if abs(det) < 1e-12:
            break
        step_h = (fx * vy - fy * vx) / det
        step_v = (fx * hy - fy * hx) / det
        if abs(step_h) > gh.total or abs(step_v) > gv.total:
            break
        arc_h = min(max(arc_h + step_h, 0.0), gh.total)
        arc_v = min(max(arc_v + step_v, 0.0), gv.total)
    return gh.point_at(arc_h), arc_h, arc_v


def _xy(p):
    return {"x": p[0], "y": p[1]}


def _curve_spec(start, step, wobble, count, horizontal):
    """A guide of `count` cubics marching along x (or y), its control points pulled off the line by up to wobble."""
    def at(s, off):
        return (s, off) if horizontal else (off, s)
    s = start
    points = [at(s, rng.uniform(-wobble, wobble))]
    commands = [{"type": "move", "to": _xy(points[0])}]
    for _ in range(count):
        c1, c2, end = (at(s + step * f, rng.uniform(-wobble, wobble)) for f in (0.33, 0.66, 1.0))
        commands.append({"type": "cubic", "control1": _xy(c1), "control2": _xy(c2), "to": _xy(end)})
        points.append(end)
        s += step
    return {"points": am._densify(points), "width": 3.0, "commands": commands}


crossed = apart = 0
for k in range(120):
    count_h, count_v = rng.randint(1, 4), rng.randint(1, 4)
    gh = am._Curve.of(_curve_spec(-300.0, 600.0 / count_h, rng.uniform(5.0, 120.0), count_h, True))
    shift = 0.0 if k % 3 else 900.0                     # every third pair never crosses: the closest vertices
    v_spec = _curve_spec(-200.0, 400.0 / count_v, rng.uniform(5.0, 120.0), count_v, False)
    if shift:
        v_spec = {**v_spec, "points": [(x + shift, y) for x, y in v_spec["points"]],
                  "commands": [{key: ({"x": val["x"] + shift, "y": val["y"]} if isinstance(val, dict) else val)
                                for key, val in command.items()} for command in v_spec["commands"]]}
    gv = am._Curve.of(v_spec)
    assert gh.curved and gv.curved
    got = am._curve_crossing(gh, gv)
    assert got == _double_loop_curves(gh, gv), (k, got)
    if am._polylines_cross(gh.points, gv.points):
        crossed += 1
    else:
        apart += 1
assert crossed and apart
print("4) curve crossing == the double loop on 120 curve pairs (%d crossing, %d apart)" % (crossed, apart))

# 5) the budget: at 3000 x 3000 the crossing, the gate and a first solve stay well under half a second; also
#    where the guides never cross, and for the loosest boxes. The double loop took over a second here.
n = 3000
big_h = [(-6000.0 + 4.0 * k, 25.0 * math.sin(k / n * 6.0)) for k in range(n + 1)]
big_v = [(20.0 * math.sin(k / n * 5.0), -6000.0 + 4.0 * k) for k in range(n + 1)]
best = None
for _ in range(3):
    start = time.perf_counter()
    frame = am._Frame(big_h, big_v)
    frame.can_fold()
    assert frame.solve_full((1.0, 2.0), 0.0, 0.0)[2] <= 1e-9
    spent = time.perf_counter() - start
    best = spent if best is None else min(best, spent)
assert not frame.can_fold()
assert best < 0.5, best
far = None
for _ in range(3):
    start = time.perf_counter()
    am._Frame(big_h, [(x + 9000.0, y) for x, y in big_v])
    spent = time.perf_counter() - start
    far = spent if far is None else min(far, spent)
assert far < 0.5, far
# the loosest boxes: two spirals winding between each other, near everywhere and never crossing
spiral = [(k * 0.5 * math.cos(k * 0.02), k * 0.5 * math.sin(k * 0.02)) for k in range(1, n + 2)]
start = time.perf_counter()
am._polyline_intersection(spiral, [(-x, -y) for x, y in spiral])
wound = time.perf_counter() - start
assert wound < 0.5, wound
assert not am._polylines_cross(spiral, [(-x, -y) for x, y in spiral])
print("5) 3000 x 3000: crossing, gate and a first solve %.1f ms, guides apart %.1f ms, wound spirals %.1f ms" % (
    best * 1e3, far * 1e3, wound * 1e3))
print("t_frame_build: ok")
