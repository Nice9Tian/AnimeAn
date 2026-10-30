"""Issue #9 measurement: does the alignment T survive into the surface?

Option A: labels only. Main axes = midlines of the drawn quad.
Option B: T as initial shape. Main axes = T(child axes), a rigid + scale frame.
Both go through the REAL build_mapper. Then the contour residual is absorbed by
a Laplace field D on the Third grid (a stand-in for ground lines / boundary
conditions): boundary = hard pin, axes pinned with D = 0 or released.
"""
import cmath
import math
import os
import sys
import types

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am
import proxy_rect as pr


def rect(w, h, cx=0.0, cy=0.0, deg=0.0, scale=1.0):
    r = cmath.exp(1j * math.radians(deg)) * scale
    out = []
    for x, y in ((-w / 2, -h / 2), (w / 2, -h / 2), (w / 2, h / 2), (-w / 2, h / 2)):
        z = complex(x, y) * r + complex(cx, cy)
        out.append((z.real, z.imag))
    return out


W, H_ = 300.0, 200.0
CHILD = rect(W, H_)
CH = [(-W / 2, 0.0), (W / 2, 0.0)]
CV = [(0.0, -H_ / 2), (0.0, H_ / 2)]


def mid(a, b):
    return ((a[0] + b[0]) / 2.0, (a[1] + b[1]) / 2.0)


def boundary_edges(quad, bulge):
    """Four edges as callables on t in [0,1]; bulge = fraction of edge length."""
    edges = []
    for i in range(4):
        p, q = quad[i], quad[(i + 1) % 4]
        dx, dy = q[0] - p[0], q[1] - p[1]
        nx, ny = dy, -dx
        amp = bulge

        def f(t, p=p, dx=dx, dy=dy, nx=nx, ny=ny, amp=amp):
            b = amp * 4.0 * t * (1.0 - t)
            return (p[0] + dx * t + nx * b, p[1] + dy * t + ny * b)
        edges.append(f)
    return edges


def target_grid(quad, n, bulge):
    """Boundary positions on the (n x n) node grid: u along H, v along V."""
    e = boundary_edges(quad, bulge)
    T = np.full((n, n, 2), np.nan)
    ts = np.linspace(0.0, 1.0, n)
    for k, t in enumerate(ts):
        T[0, k] = e[0](t)            # v=-1 edge: corner0 -> corner1
        T[-1, k] = e[2](1.0 - t)     # v=+1 edge: corner3 -> corner2
        T[k, -1] = e[1](t)           # u=+1 edge: corner1 -> corner2
        T[k, 0] = e[3](1.0 - t)      # u=-1 edge: corner0 -> corner3
    return T


def laplace(D_bc, mask):
    """Uniform 5-point Laplace with Dirichlet where mask; CG."""
    D = np.where(mask[..., None], D_bc, 0.0)

    def A(X):
        Y = np.zeros_like(X)
        Y[1:-1, 1:-1] = 4 * X[1:-1, 1:-1] - X[2:, 1:-1] - X[:-2, 1:-1] - X[1:-1, 2:] - X[1:-1, :-2]
        Y[mask] = 0.0
        return Y
    out = D.copy()
    for c in range(2):
        pin = D[..., c].copy()
        pin_only = pin.copy()
        R = np.zeros_like(pin)
        full = np.zeros_like(pin)
        full[1:-1, 1:-1] = (4 * pin_only[1:-1, 1:-1] - pin_only[2:, 1:-1] - pin_only[:-2, 1:-1]
                            - pin_only[1:-1, 2:] - pin_only[1:-1, :-2])
        R = -full
        R[mask] = 0.0
        E = np.zeros_like(pin)
        P = R.copy()
        rs = float(np.sum(R * R))
        rs0 = rs or 1.0
        for _ in range(20000):
            if rs <= 1e-22 * rs0 or rs == 0.0:
                break
            AP = A(P)
            alpha = rs / float(np.sum(P * AP))
            E += alpha * P
            R -= alpha * AP
            rs2 = float(np.sum(R * R))
            P = R + (rs2 / rs) * P
            rs = rs2
        out[..., c] = pin + E
    return out


def fold_stats(F):
    a = F[1:, :-1] - F[:-1, :-1]     # d/dv
    b = F[:-1, 1:] - F[:-1, :-1]     # d/du
    det = b[..., 0] * a[..., 1] - b[..., 1] * a[..., 0]
    ref = np.sign(np.median(det))
    det = det * ref
    area = np.median(np.abs(det))
    return int(np.sum(det <= 0.0)), float(det.min() / area)


def kink(F, n):
    """Largest second difference across the axes (px per cell): a crease."""
    c = n // 2
    return float(max(np.abs(F[c + 1, :] - 2 * F[c, :] + F[c - 1, :]).max(),
                     np.abs(F[:, c + 1] - 2 * F[:, c] + F[:, c - 1]).max()))


def run(name, quad, bulge=0.0, n=97):
    labels = pr.align(CHILD, quad)[0]["main_order"]
    q = [quad[i] for i in labels]
    z = [complex(*p) for p in q]
    a = [complex(*p) for p in CHILD]
    Z = sum((a[i]).conjugate() * (z[i] - sum(z) / 4) for i in range(4)) / sum(abs(x) ** 2 for x in a)
    c = sum(z) / 4

    def T(p):
        w = Z * complex(*p) + c
        return (w.real, w.imag)
    frames = {
        "A": ([mid(q[3], q[0]), mid(q[1], q[2])], [mid(q[0], q[1]), mid(q[3], q[2])]),
        "B": ([T(p) for p in CH], [T(p) for p in CV]),
    }
    target = target_grid(q, n, bulge)
    us = np.linspace(-W / 2, W / 2, n)
    vs = np.linspace(-H_ / 2, H_ / 2, n)
    c_i = n // 2
    res = {}
    for key, (mh, mv) in frames.items():
        mp, ws = am.build_mapper(CH, CV, mh, mv, {})
        assert mp is not None, ws
        base = np.array([[mp((float(u), float(v))) for u in us] for v in vs], dtype=float)
        bnd = np.zeros((n, n), bool)
        bnd[0, :] = bnd[-1, :] = bnd[:, 0] = bnd[:, -1] = True
        D_bc = np.where(bnd[..., None], target - np.nan_to_num(base * bnd[..., None]), 0.0)
        contour = np.linalg.norm((base - np.nan_to_num(target))[bnd], axis=1)
        axis_mask = bnd.copy()
        axis_mask[c_i, :] = True
        axis_mask[:, c_i] = True
        pinned = base + laplace(D_bc, axis_mask)
        free = base + laplace(D_bc, bnd)
        res[key] = (contour.mean(), contour.max(), pinned, free)
    ref = res["A"][3]
    print(f"\n--- {name}  labels={labels}")
    for key in "AB":
        cm, cx, pinned, free = res[key]
        print(f"  {key}: contour residual to carry  mean {cm:7.2f} max {cx:7.2f} px")
        for tag, F in (("axes pinned", pinned), ("axes free  ", free)):
            f, md = fold_stats(F)
            print(f"     {tag}: folded cells {f:5d}  min det {md:7.3f}  crease {kink(F, n):6.2f}  "
                  f"vs A-free max {np.linalg.norm(F - ref, axis=2).max():8.4f} px")
    # bilinear patch (straight edges): the exact answer for the A-field?
    uu, vv = np.meshgrid(np.linspace(0, 1, n), np.linspace(0, 1, n))
    Q = np.array(q)
    bil = ((1 - uu) * (1 - vv))[..., None] * Q[0] + (uu * (1 - vv))[..., None] * Q[1] \
        + (uu * vv)[..., None] * Q[2] + ((1 - uu) * vv)[..., None] * Q[3]
    if bulge == 0.0:
        print(f"  bilinear patch vs A pinned: max {np.linalg.norm(res['A'][2] - bil, axis=2).max():.2e} px; "
              f"vs B free: {np.linalg.norm(res['B'][3] - bil, axis=2).max():.2e} px")


cases = [
    ("same rectangle", rect(W, H_)),
    ("rotated 20, scale 1.3, shifted", rect(W, H_, 120, -80, 20, 1.3)),
    ("aspect change 300x200 -> 200x300", rect(200, 300, 40, 10)),
    ("keystone mild", [(-120, -100), (120, -100), (150, 100), (-150, 100)]),
    ("keystone strong", [(-40, -100), (40, -100), (190, 100), (-190, 100)]),
    ("general convex quad", [(-170, -60), (90, -130), (210, 120), (-110, 90)]),
    ("big displacement + keystone + rot", [(z.real + 400, z.imag - 300) for z in
                                           (complex(*p) * cmath.exp(0.5j) * 1.5 for p in
                                            [(-90, -100), (90, -100), (170, 100), (-170, 100)])]),
]
for name, q in cases:
    run(name, q)
for name, q in (("keystone mild, edges bulged 8%", cases[3][1]), ("general quad, edges bulged 8%", cases[5][1])):
    run(name, q, bulge=0.08)
