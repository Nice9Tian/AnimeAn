"""Per-point cost, original vs patched, interleaved inside each pass (one process per rig)."""
import math
import os
import random
import sys
import time
import types

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.path.insert(0, os.path.join(ROOT, "docs", "plan", "2026-10-28-格哈希论文新颖性调查", "research_eps_band", "prototype"))
sys.path.insert(0, HERE)
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am  # noqa: E402
import chord_intersect as new  # noqa: E402
try:
    import chord_intersect_orig as old  # noqa: E402
except ModuleNotFoundError:
    import chord_intersect_C as old  # noqa: E402


def sin_guide(amp, axis, n=101, half=200.0, period=300.0):
    return [((-half + 2 * half * k / (n - 1)), amp * math.sin(2 * math.pi * (-half + 2 * half * k / (n - 1)) / period))
            if axis == "h" else (amp * math.sin(2 * math.pi * (-half + 2 * half * k / (n - 1)) / period), (-half + 2 * half * k / (n - 1)))
            for k in range(n)]


amp = float(sys.argv[1]) if len(sys.argv) > 1 else 100.0
frame = am._Frame(sin_guide(amp, "h"), sin_guide(amp, "v"))
A, B = old.ChordIntersect.from_frame(frame), new.ChordIntersect.from_frame(frame)
A.prepare_grid()
B.prepare_grid()
rng = random.Random(3)
pts = []
for _ in range(4000):
    l_h = rng.uniform(-A.h_arc * 0.9, (A.h_cum[-1] - A.h_arc) * 0.9)
    l_v = rng.uniform(-A.v_arc * 0.9, (A.v_cum[-1] - A.v_arc) * 0.9)
    pts.append(frame.hv(l_h, l_v))
batch = pts[:400]
res = {k: [] for k in ("grid_old", "grid_new", "gridall_old", "gridall_new", "np_old", "np_new", "mat_old", "mat_new")}
A.solve_many(batch[:2])
B.solve_many(batch[:2])
for rep in range(5):
    for name, sv in (("old", A), ("new", B)):
        t0 = time.perf_counter()
        for p in pts:
            sv.solve(p)
        res["grid_" + name].append((time.perf_counter() - t0) / len(pts) * 1e6)
        t0 = time.perf_counter()
        for p in pts:
            sv.solve(p, extensions="all")
        res["gridall_" + name].append((time.perf_counter() - t0) / len(pts) * 1e6)
        t0 = time.perf_counter()
        sv.solve_many(batch)
        res["np_" + name].append((time.perf_counter() - t0) / len(batch) * 1e6)
        t0 = time.perf_counter()
        for p in pts[:20]:
            sv.solve_matrix(p)
        res["mat_" + name].append((time.perf_counter() - t0) / 20 * 1e6)
cands = 0
for p in pts:
    cx, cy = p[0] + B.origin[0], p[1] + B.origin[1]
    x0, y0, inv, nbx, nby, sheet = B._grid
    fx, fy = (cx - x0) * inv, (cy - y0) * inv
    if 0 <= fx < nbx and 0 <= fy < nby:
        cands += len(sheet[int(fy) * nbx + int(fx)])
print("bow %g px, %d points (median of 5 interleaved passes), us per point:" % (amp, len(pts)))
for k in ("grid", "gridall", "np", "mat"):
    o = sorted(res[k + "_old"])[2]
    n = sorted(res[k + "_new"])[2]
    print("  %-8s old %9.1f   new %9.1f   new/old %.2f" % (k, o, n, n / o))
print("  sheet candidates per point (mean): %.1f; exact fallbacks taken: %d" % (cands / len(pts), new.EXACT_CALLS[0]))
