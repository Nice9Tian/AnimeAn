"""Bezier mode splits each source cubic at the guides' knots before handle
transport (user report 2026-09-25: a straight line under two bent H/V guides
sometimes came out straight - its endpoints followed the warp, but the bend
between them was never sampled). Fuzzed against the true warp (300 random
frames): before the split 45 cases missed by > 1 px, worst 33.6 px on a
child+main bent curve frame; after, one child-fold case remains (3.4 px),
and the main-bent fuzz below drops from 1.17 px to 0.51 px worst."""
import math
import os
import random
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am


def P(p):
    return {"x": p[0], "y": p[1]}


def curve_spec(cubics):
    commands = [{"type": "move", "to": P(cubics[0][0])}]
    flat = [cubics[0][0]]
    for c in cubics:
        commands.append({"type": "cubic", "control1": P(c[1]),
                         "control2": P(c[2]), "to": P(c[3])})
        flat.extend(am._cubic_point(c, k / 32) for k in range(1, 33))
    return {"points": flat, "commands": commands}


def bent_guide(rng, horizontal, kind):
    n = rng.randint(2, 5)
    amp = rng.uniform(40, 160)
    offsets = [0.0] + [rng.uniform(-amp, amp) for _ in range(n - 1)] + [0.0]
    pts = [(-300 + 600 * i / n, o) for i, o in enumerate(offsets)]
    if not horizontal:
        pts = [(o, x) for x, o in pts]
    return pts if kind == "poly" else curve_spec(am._catmull_rom_cubics(pts))


def straight(horizontal):
    return [(-300.0, 0.0), (300.0, 0.0)] if horizontal else [(0.0, -300.0), (0.0, 300.0)]


def deviation(mp, cub, outs):
    """Max distance from the true warped curve to the emitted cubics."""
    dense = [am._cubic_point(o, k / 64) for o in outs for k in range(65)]
    segs = list(zip(dense, dense[1:]))

    def to_seg(p, a, b):
        vx, vy = b[0] - a[0], b[1] - a[1]
        n = vx * vx + vy * vy
        t = 0.0 if n < 1e-12 else max(0.0, min(1.0, ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / n))
        return math.hypot(p[0] - a[0] - t * vx, p[1] - a[1] - t * vy)

    return max(min(to_seg(mp(am._cubic_point(cub, k / 160)), a, b) for a, b in segs)
               for k in range(161))


# 1) A straight line across a two-segment arch: the arch's joint is a knot,
#    so the line must be cut there and its image must follow the arch.
arch_h = curve_spec([((-300, 0), (-200, -120), (-100, -120), (0, -120)),
                     ((0, -120), (100, -120), (200, -120), (300, 0))])
mp, _ = am.build_mapper(straight(True), straight(False), arch_h, straight(False), {})
line = am._line_cubic((-280.0, 150.0), (280.0, 150.0))
params = am._structural_cubic_params(mp, line)
assert len(params) == 1 and abs(params[0] - 0.5) < 0.01, params
outs = am._warp_cubic(mp, line)
assert any(math.isclose(o[3][0], 0.0, abs_tol=0.5) for o in outs[:-1]), \
    "the arch joint must survive as an output anchor"
assert deviation(mp, line, outs) < 1.0
print("1) arch joint splits the line and becomes an output anchor")

# 2) Straight guides have no interior knots: the split is a no-op and the
#    identity map still returns the source cubic untouched.
mp0, _ = am.build_mapper(straight(True), straight(False), straight(True), straight(False), {})
assert am._structural_cubic_params(mp0, line) == []
assert len(am._warp_cubic(mp0, line)) == 1
print("2) straight guides: no split")

# 3) Fuzz: bent MAIN guides (the user's case), curve and polyline alike.
#    Child-bent frames are left out on purpose - they fold, and the real
#    pipeline severs folded ground before any cubic reaches _warp_cubic.
rng = random.Random(7)
worst = 0.0
for trial in range(60):
    kind = rng.choice(["poly", "curve"])
    mh, mv = bent_guide(rng, True, kind), bent_guide(rng, False, kind)
    mp, info = am.build_mapper(straight(True), straight(False), mh, mv, {})
    assert mp is not None, info
    for _ in range(3):
        a = (rng.uniform(-280, 280), rng.uniform(-280, 280))
        b = (rng.uniform(-280, 280), rng.uniform(-280, 280))
        cub = am._line_cubic(a, b)
        worst = max(worst, deviation(mp, cub, am._warp_cubic(mp, cub)))
assert worst < 1.0, worst
print(f"3) fuzz, bent main guides: worst deviation {worst:.2f}px")
print("ALL OK")
