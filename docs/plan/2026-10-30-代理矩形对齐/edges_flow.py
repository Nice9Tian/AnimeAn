"""Issue #9 measurement: the four edges as ADDITIONAL LINES through the real
_FlowFieldWarp. How close do the corners and edges get, what about folds?"""
import cmath
import math
import os
import sys
import types

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am
import proxy_rect as pr

W, H = 300.0, 200.0
CHILD = [(-W / 2, -H / 2), (W / 2, -H / 2), (W / 2, H / 2), (-W / 2, H / 2)]


def asset(points, i):
    return {"points": [list(p) for p in points], "width": 3.0, "id": i}


def densify(edge, n=24):
    return [(edge[0][0] + (edge[1][0] - edge[0][0]) * i / n,
             edge[0][1] + (edge[1][1] - edge[0][1]) * i / n) for i in range(n + 1)]


def measure(mp, g):
    errs, corners = [], []
    for fam in ("h", "v"):
        for ce, me in zip(g["child"]["edges"][fam], g["main"]["edges"][fam]):
            cs, ms = densify(ce), densify(me)
            for k, (c, m) in enumerate(zip(cs, ms)):
                p = mp(c)
                e = math.hypot(p[0] - m[0], p[1] - m[1])
                errs.append(e)
                if k in (0, len(cs) - 1):
                    corners.append(e)
    return sum(errs) / len(errs), max(errs), sum(corners) / len(corners)


def folds(mp, n=60):
    xs = [-W / 2 + W * i / n for i in range(n + 1)]
    ys = [-H / 2 + H * j / n for j in range(n + 1)]
    grid = [[mp((x, y)) for x in xs] for y in ys]
    bad, worst = 0, 1e9
    base = None
    for j in range(n):
        for i in range(n):
            a = grid[j][i]
            b = grid[j][i + 1]
            c = grid[j + 1][i]
            det = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
            worst = min(worst, det)
            bad += det <= 0.0
    return bad, worst


cases = [
    ("rotated 20, scale 1.3, shifted", [((complex(*p) * cmath.rect(1.3, math.radians(20.0))).real + 120.0,
                                         (complex(*p) * cmath.rect(1.3, math.radians(20.0))).imag - 80.0)
                                        for p in CHILD]),
    ("aspect 300x200 -> 200x300", [(-100, -150), (100, -150), (100, 150), (-100, 150)]),
    ("keystone mild", [(-120, -100), (120, -100), (150, 100), (-150, 100)]),
    ("keystone strong", [(-40, -100), (40, -100), (190, 100), (-190, 100)]),
    ("general convex quad", [(-170, -60), (90, -130), (210, 120), (-110, 90)]),
]
print(f"{'case':30s} {'setup':16s} {'edge mean':>9s} {'edge max':>9s} {'corner':>8s} {'folds':>6s} {'min det':>9s}")
for name, quad in cases:
    g, why = pr.guides(CHILD, quad, [(-W / 2, 0.0), (W / 2, 0.0)], [(0.0, -H / 2), (0.0, H / 2)])
    assert g, why
    args = (g["child"]["h"], g["child"]["v"], g["main"]["h"], g["main"]["v"])
    mp0, _ = am.build_mapper(*args)
    assert mp0 is not None
    pairs, i = [], 1
    for fam in ("h", "v"):
        for ce, me in zip(g["child"]["edges"][fam], g["main"]["edges"][fam]):
            pairs.append((asset(densify(ce), i), asset(densify(me), i)))
            i += 1
    mp1, ws = am.build_mapper(*args, additional_pairs=pairs)
    assert mp1 is not None, ws
    for tag, mp in (("axes only", mp0), ("+4 edges flow", mp1)):
        m, x, c = measure(mp, g)
        f, d = folds(mp)
        print(f"{name:30s} {tag:16s} {m:9.2f} {x:9.2f} {c:8.2f} {f:6d} {d:9.2f}")
