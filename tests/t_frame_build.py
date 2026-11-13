"""_Frame's guide crossing and fold gate.

The crossing walks the segment box trees of pyfile/polyline_boxes.py and must give the old double loop's answer to
the last bit; can_fold is each frame's own, cached on the frame, and equals the minimum over every pair of directions.
Taken from PR #18 (its t_frame_solver sections 4-7, same seeds, so the same 300 frames and 1400 rigs); the solver
that PR also brings is not part of this. Sections 2b and 3b are this split's own: the gate right at its margin,
and nearly parallel segment pairs, where the trees need more than boxes to give the double loop's answer."""
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

# 2) the gate's sorted lookup gives the old closure's verdict: the minimum over every pair of directions
def _brute_gate(frame):
    """build_mapper's can_fold before it moved onto _Frame, verbatim but for the frame argument."""
    _hand_h, _hand_v = frame.directions(0.0, 0.0)
    child_hand = 1.0 if (_hand_h[0] * _hand_v[1] - _hand_h[1] * _hand_v[0]) >= 0.0 else -1.0

    def gate_dirs(curve):
        dirs = []
        for a, b in zip(curve.points, curve.points[1:]):
            length = math.hypot(b[0] - a[0], b[1] - a[1])
            if length > 1e-9:
                dirs.append(((b[0] - a[0]) / length, (b[1] - a[1]) / length))
        eps = 1e-6
        extremes = [0.0, curve.total]
        extremes.extend(getattr(curve, "sharp_arcs", None) or ())
        for arc in extremes:
            for side in (arc - eps, arc + eps):
                dirs.append(curve.dir_at(side))
        return dirs

    lowest = None
    for hx, hy in gate_dirs(frame.gh):
        for vx, vy in gate_dirs(frame.gv):
            value = (hx * vy - hy * vx) * child_hand
            if lowest is None or value < lowest:
                lowest = value
    return lowest is None or lowest <= am._SEVER_GATE_SIN


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
    want = _brute_gate(frame)
    assert frame.can_fold() == want
    seen[want] += 1
assert seen[True] and seen[False], seen
print("2) can_fold == the old closure on 300 frames (%d folding, %d not)" % (seen[True], seen[False]))

# 2b) right at the margin the lookup and the brute minimum can differ by rounding among tied directions, so
#     there the verdict must be the old closure's own: straight guides whose sine is 0.02 to within a few ulps,
#     one of them cut into many near-duplicate segments, and a guide whose chords share one angle and differ
#     by an ulp across the margin (the lookup alone, without the check at the margin, gets that one wrong)
tied = am._Frame([(-10.0, -10.0), (0.0, 0.0), (0.20002739999999997, 9.999369525932584), (0.4000548, 19.99873905186517)],
                 [(0.0, -50.0), (0.0, 50.0)])
assert tied.can_fold() == _brute_gate(tied) is True
margin_seen = {True: 0, False: 0}
for k in range(-40, 41):
    s = am._SEVER_GATE_SIN + k * 2.0 ** -57
    c = math.sqrt(1.0 - s * s)
    for base in (0.0, 0.37):
        h_dir = (math.cos(base), math.sin(base))
        v_dir = (h_dir[0] * c - h_dir[1] * s, h_dir[0] * s + h_dir[1] * c)
        h_line = [(-300.0 * h_dir[0] + 0.5 * i * h_dir[0], -300.0 * h_dir[1] + 0.5 * i * h_dir[1]) for i in range(1201)]
        v_line = [(-200.0 * v_dir[0], -200.0 * v_dir[1]), (200.0 * v_dir[0], 200.0 * v_dir[1])]
        frame = am._Frame(h_line, v_line)
        want = _brute_gate(frame)
        assert frame.can_fold() == want, (k, base)
        margin_seen[want] += 1
assert margin_seen[True] and margin_seen[False], margin_seen
print("2b) at the margin the verdict is the brute minimum's (%d folding, %d not)" % (margin_seen[True], margin_seen[False]))


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

# 3b) nearly parallel pairs: rounding alone can put the double loop's parameters inside the tolerance while the
#     boxes lie apart, so these must not be pruned. The literal cases are inputs a box test without the
#     direction guard got wrong (the first: two collinear 300 px segments 300 px apart, which the double loop
#     reports as crossing); then a seeded batch of near-collinear pairs like them.
NEAR_PARALLEL = [
    ([(-97.04842870086989, -81.34744476323282), (74.06577326562098, 156.82064537657033)],
     [(245.17997523211184, 394.9887355163734), (416.2941771986026, 633.1568256561764)]),
    ([(0.7109772381731951, 0.8974067919575055), (-14.794872728354592, 9.42341149903724)],
     [(8.202060884550242, -3.2216201925674675), (0.7112740433622283, 0.8972435914593557), (-2.054478436949292, 0.7339561132727819)]),
    ([(-0.5792216389789748, 0.29131342309384856), (-5.613146107535671, -21.108049482166116)],
     [(2.3443286209553253, 12.71941268737756), (-0.5789120449384801, 0.29262951658457126), (1.2954823247767442, 2.0469705675488683)]),
    ([(22528.58394539554, -70166.00287914566), (-1891933.668328654, 2189634.3341676737), (-1598740.068270247, 2183208.925844843)],
     [(-1892169.1581279067, 2189912.302525154), (-4303286.776207619, 5035956.615762824), (-4572360.474528049, 5276951.806905031)]),
    ([(-252720.20374301408, 632512.9207853873), (-265340.5888772723, 612750.7354079654), (-267510.24449461396, 615192.3192381753), (-264843.67687061033, 615066.3956120289), (-264417.0845943021, 613153.6199814897)],
     [(-233917.97196918452, 661955.2227335148), (-252719.5728462613, 632513.908702828)]),
    ([(283769.50820314925, 736.6088043932906), (242234.1196178972, 22271.966927215177), (243639.28972968555, 24369.0823611096), (240974.12005285433, 25157.66795101656)],
     [(242224.43027046605, 22276.990681031228), (229034.7117552513, 29115.62446872981), (229175.90957745217, 27329.536321800875), (227675.11723683664, 29589.27969727934)]),
    ([(-0.8916902150065946, 0.2344975787679635), (2.042319989131192, -10.057617861118416), (-0.7716431804432746, -7.34114981949889), (2.043263850270617, -5.583058580411912), (3.1823413489565624, -5.551258350389172), (3.230049405998087, -8.013707756268172)],
     [(-10.461701873391707, 33.80481870792216), (-0.8934988855195906, 0.24084215305956422), (0.1990598498749817, -2.5296477588162283), (-1.8557558458521393, -4.285939489701967)]),
]


def _crosses(a, b):
    return any(-1e-6 <= h[0] <= 1.0 + 1e-6 and -1e-6 <= h[1] <= 1.0 + 1e-6
               for h in (am._segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
                         for i in range(len(a) - 1) for j in range(len(b) - 1)) if h)


for a, b in NEAR_PARALLEL:
    assert am._polyline_intersection(a, b) == _double_loop(a, b), (a, b)
    assert am._polylines_cross(a, b) == _crosses(a, b), (a, b)
near = random.Random(13)
for _ in range(4000):
    ang, tilt = near.uniform(-math.pi, math.pi), near.choice((1e-13, 1e-12, 1e-11, 1e-10, 1e-8))
    scale = near.choice((1.0, 1e3, 1e5))
    la, lb = near.uniform(0.5, 50.0) * scale, near.uniform(0.5, 50.0) * scale
    d, e = (math.cos(ang), math.sin(ang)), (math.cos(ang + tilt), math.sin(ang + tilt))
    a0 = (near.uniform(-1.0, 1.0) * scale, near.uniform(-1.0, 1.0) * scale)
    a = [a0, (a0[0] + la * d[0], a0[1] + la * d[1])]
    end = near.choice((0.0, 1.0)) + near.uniform(-3e-4, 3e-4)
    cx, cy = a0[0] + end * la * d[0], a0[1] + end * la * d[1]
    s0 = near.uniform(-1.2, 0.2) * lb
    b = [(cx + s0 * e[0], cy + s0 * e[1]), (cx + (s0 + lb) * e[0], cy + (s0 + lb) * e[1])]
    assert am._polyline_intersection(a, b) == _double_loop(a, b), (a, b)
    assert am._polylines_cross(a, b) == _crosses(a, b), (a, b)
print("3b) nearly parallel pairs == the double loop (%d literal, 4000 seeded)" % len(NEAR_PARALLEL))

# 3c) eps is a fraction of each segment's own length, so beside a 2,000,000 px segment it reaches 2 px past
#     the ends: short segments crossing the long one's line within a few px of its end, inside and outside
#     that window, where only the boxes' slack keeps the pair
edge = random.Random(17)
inside = outside = 0
for _ in range(3000):
    long_seg = [(-1e6, -3.0), (1e6, 3.0)]
    end_x = 1e6 + edge.uniform(-4.0, 4.0)
    cut_y = 3.0 + (end_x - 1e6) * 3e-6
    tilt = edge.uniform(0.2, 2.9)
    half = edge.uniform(0.5, 20.0)
    lift = edge.uniform(-1.5, 1.5) * half
    short = [(end_x - half * math.cos(tilt), cut_y + lift - half * math.sin(tilt)),
             (end_x + half * math.cos(tilt), cut_y + lift + half * math.sin(tilt))]
    for a, b in ((long_seg, short), (short, long_seg)):
        assert am._polyline_intersection(a, b) == _double_loop(a, b), (a, b)
        assert am._polylines_cross(a, b) == _crosses(a, b), (a, b)
    if _crosses(long_seg, short):
        if end_x > 1e6:
            inside += 1
    else:
        outside += 1
assert inside and outside, (inside, outside)
print("3c) crossings past a very long segment's end == the double loop (%d inside its eps, %d not crossing)" % (
    inside, outside))

# an empty polyline fails exactly as the double loop did; a single point is the closest-vertex case
for a, b in (([], [(0.0, 0.0), (1.0, 1.0)]), ([(0.0, 0.0), (1.0, 1.0)], [])):
    try:
        am._polyline_intersection(a, b)
    except TypeError:
        pass
    else:
        raise AssertionError("an empty polyline must raise TypeError, as before")
    assert am._polylines_cross(a, b) is False
lone = [(3.0, 4.0)]
assert am._polyline_intersection(lone, [(0.0, 0.0), (1.0, 1.0), (5.0, 5.0)]) == _double_loop(lone, [(0.0, 0.0), (1.0, 1.0), (5.0, 5.0)])
print("3d) empty and single-point polylines behave as the double loop did")


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

# 5) pruning invariants on 3000 x 3000 polylines (9,000,000 segment pairs):
#    box_pairs prunes crossing and apart guides to O(1) candidates and even
#    worst-case intertwined spirals to O(n) candidates, without relying on
#    load-sensitive wall-clock limits.
import polyline_boxes as pb

n = 3000
big_h = [(-6000.0 + 4.0 * k, 25.0 * math.sin(k / n * 6.0)) for k in range(n + 1)]
big_v = [(20.0 * math.sin(k / n * 5.0), -6000.0 + 4.0 * k) for k in range(n + 1)]
far_v = [(x + 9000.0, y) for x, y in big_v]
spiral = [(k * 0.5 * math.cos(k * 0.02), k * 0.5 * math.sin(k * 0.02)) for k in range(1, n + 2)]
spiral_opp = [(-x, -y) for x, y in spiral]

cross_candidates = sum(1 for _ in pb.box_pairs(big_h, big_v, 1e-9))
far_candidates = sum(1 for _ in pb.box_pairs(big_h, far_v, 1e-9))
spiral_candidates = sum(1 for _ in pb.box_pairs(spiral, spiral_opp, 1e-9))
assert cross_candidates < 50, cross_candidates
assert far_candidates == 0, far_candidates
assert spiral_candidates <= 2 * n, spiral_candidates

# The production paths (_Frame, _polyline_intersection, _polylines_cross) must still go through the trees: count
# their exact segment tests. Going back to the double loop makes these millions and fails here, as the old
# wall-clock limits did.
_orig_seg = am._segment_intersection
_orig_box_pairs = pb.box_pairs
_seg_tests = [0]
_tree_walks = [0]


def _counting_seg(*args):
    _seg_tests[0] += 1
    return _orig_seg(*args)


def _counting_box_pairs(*args):
    _tree_walks[0] += 1
    return _orig_box_pairs(*args)


def _exact_tests(fn):
    """fn's result and how many exact segment tests it ran; fn must walk the box trees."""
    _seg_tests[0] = _tree_walks[0] = 0
    am._segment_intersection, pb.box_pairs = _counting_seg, _counting_box_pairs
    try:
        start = time.perf_counter()
        out = fn()
        spent = time.perf_counter() - start
    finally:
        am._segment_intersection, pb.box_pairs = _orig_seg, _orig_box_pairs
    assert _tree_walks[0] >= 1, "the crossing no longer walks polyline_boxes.box_pairs"
    return out, _seg_tests[0], spent


def _cross_frame():
    frame = am._Frame(big_h, big_v)
    assert not frame.can_fold()
    assert frame.solve_full((1.0, 2.0), 0.0, 0.0)[2] <= 1e-9
    return frame


_, cross_tests, best = _exact_tests(_cross_frame)
_, far_tests, far = _exact_tests(lambda: am._Frame(big_h, far_v))
_, wound_tests, wound = _exact_tests(lambda: am._polyline_intersection(spiral, spiral_opp))
wound_cross, wound_cross_tests, _ = _exact_tests(lambda: am._polylines_cross(spiral, spiral_opp))
assert not wound_cross
assert cross_tests <= 2 * max(cross_candidates, 1), cross_tests
assert far_tests == 0, far_tests
assert wound_tests <= 2 * n and wound_cross_tests <= 2 * n, (wound_tests, wound_cross_tests)
print(
    "5) 3000 x 3000 pruning: candidates cross=%d, apart=%d, spirals=%d / %d; exact segment tests in _Frame %d, "
    "apart %d, spirals %d (%.1f ms, %.1f ms, %.1f ms)"
    % (cross_candidates, far_candidates, spiral_candidates, n * n, cross_tests, far_tests, wound_tests,
       best * 1e3, far * 1e3, wound * 1e3)
)
print("t_frame_build: ok")
