"""Before / after probes for the acceptance-band fix (fable_band prototype).

For each module (chord_intersect_orig = live code, chord_intersect = patched) and rig:
  (a) every fold edge on the sheet (vertical + horizontal), at its midpoint, both sides,
      41 distances 1e-12 .. 1e-2 px                                   (page_dedup_check / t_ci (a) design)
  (n) every NON-fold interior edge, same probes but 13 distances         (duplicates / lost roots)
  (b) every interior node (cell corner), 8 directions, 1e-12 .. 1e-4 px (t_ci (b) design, all corners)
      and, separately, the 80-ish cusp corners along the odd sector bisector exactly as t_ci (b)
  (c) 2000 random probes
Counted per probe:
  law      merged hits (solve(p, 'all')) have sum(orient) != w
  false    a raw report (exact repeats removed) whose cell does NOT contain c exactly
  lost     a cell that contains c exactly but is not reported
  odd      odd number of raw reports within 1 arc px of the probed edge midpoint (the PROBLEM.md symptom)
Truth = exact rational membership in the closed cell of the ROUNDED node mesh (sides through
the two rounded nodes; guide-end sides open), computed here with Fractions only, independent
of both modules; candidate cells by a float prefilter with a 1e-3 margin on t, u.
Also: truth sum(orient) over the exact cells == w (the mesh's degree), and patched paths equal
(grid raw == numpy raw == matrix raw on a subsample, bit for bit).
"""
import importlib
import math
import os
import random
import sys
import time
import types
from fractions import Fraction as Fr

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.path.insert(0, os.path.join(ROOT, "docs", "plan", "2026-10-28-格哈希论文新颖性调查", "research_eps_band", "prototype"))
sys.path.insert(0, HERE)
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import numpy as np  # noqa: E402

NAME = sys.argv[1] if len(sys.argv) > 1 else "chord_intersect"
ci = importlib.import_module(NAME)
OUT = open(os.devnull, "w", encoding="utf-8")


def sin_guide(amp, axis, n=101, half=200.0, period=300.0):
    pts = []
    for k in range(n):
        s = -half + 2.0 * half * k / (n - 1)
        bow = amp * math.sin(2.0 * math.pi * s / period)
        pts.append((s, bow) if axis == "h" else (bow, s))
    return pts


S_U = (-200.00, -197.56, -194.77, -191.98, -189.20, -186.00, -182.49, -178.74, -174.74, -170.54, -166.22, -161.76,
       -157.13, -152.43, -147.63, -142.76, -137.82, -132.85, -127.86, -122.84, -117.81, -112.79, -107.78, -102.80,
       -97.88, -93.04, -88.26, -83.54, -78.85, -74.23, -69.64, -65.08, -60.58, -56.08, -51.64, -47.23, -42.82, -38.43,
       -34.09, -29.69, -25.33, -21.02, -16.74, -12.53, -8.37, -4.24, -0.25, 3.65, 7.48, 11.24, 14.90, 18.48, 21.99,
       25.45, 28.84, 32.24, 35.59, 38.99, 42.44, 45.93, 49.48, 53.13, 56.88, 60.79, 64.83, 69.09, 73.46, 77.99, 82.68,
       87.48, 92.36, 97.30, 102.28, 107.30, 112.32, 117.34, 122.36, 127.38, 132.36, 137.31, 142.13, 146.76, 151.18,
       155.37, 159.26, 162.85, 166.15, 169.23, 172.18, 174.99, 177.66, 180.23, 182.72, 185.16, 187.57, 189.92, 191.97,
       193.83, 196.01, 198.04, 200.00)
S_W = (0.00, 4.39, 8.57, 12.74, 16.93, 20.80, 24.41, 27.75, 30.79, 33.56, 36.12, 38.44, 40.40, 42.17, 43.67, 44.92,
       45.86, 46.58, 47.18, 47.52, 47.62, 47.49, 47.09, 46.39, 45.34, 43.99, 42.43, 40.71, 38.90, 36.92, 34.87, 32.74,
       30.51, 28.26, 25.90, 23.50, 21.08, 18.62, 16.09, 13.65, 11.15, 8.57, 5.93, 3.17, 0.35, -2.52, -5.57, -8.74,
       -12.00, -15.34, -18.78, -22.32, -25.91, -29.56, -33.27, -36.97, -40.73, -44.43, -48.08, -51.71, -55.27, -58.72,
       -62.07, -65.23, -68.22, -70.89, -73.38, -75.55, -77.37, -78.84, -80.08, -80.99, -81.65, -81.97, -82.26, -82.46,
       -82.34, -81.96, -81.33, -80.44, -79.04, -77.08, -74.70, -71.92, -68.74, -65.22, -61.43, -57.46, -53.39, -49.22,
       -44.96, -40.64, -36.27, -31.88, -27.46, -23.02, -18.43, -13.76, -9.23, -4.63, 0.00)


def s_guides(scale):
    return ([(u, scale * w) for u, w in zip(S_U, S_W)], [(scale * w, u) for u, w in zip(S_U, S_W)])


def winding(sv):
    def unit(a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        n = math.hypot(dx, dy)
        return dx / n, dy / n
    d0, d1 = unit(sv.h[0], sv.h[1]), unit(sv.h[-2], sv.h[-1])
    e0, e1 = unit(sv.v[0], sv.v[1]), unit(sv.v[-2], sv.v[-1])
    loop = [d1, e1, (-d0[0], -d0[1]), (-e0[0], -e0[1])]
    tot = sum(math.atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1]) for a, b in zip(loop, loop[1:] + loop[:1]))
    return int(round(tot / (2.0 * math.pi)))


class Truth:
    """Exact closed-cell membership on the rounded node mesh, Fractions only."""

    def __init__(self, sv):
        self.sv = sv
        h, v = sv.h, sv.v
        self.n_h, self.n_v = sv.n_h, sv.n_v
        H, V = np.asarray(h), np.asarray(v)
        N = H[:, None, :] + V[None, :, :]
        self.NX, self.NY = N[..., 0], N[..., 1]
        R = H[1:] - H[:-1]
        Q = V[1:] - V[:-1]
        self.R, self.Q = R, Q
        self.D = R[:, None, 0] * Q[None, :, 1] - R[:, None, 1] * Q[None, :, 0]
        self.orient = np.where(self.D >= 0, 1, -1)
        rl = np.hypot(R[:, 0], R[:, 1])[:, None]
        ql = np.hypot(Q[:, 0], Q[:, 1])[None, :]
        self.ok = np.abs(self.D) > 1e-12 * (rl * ql + 1e-300)

    def candidates(self, cx, cy, margin=1e-3):
        dx = cx - self.NX[:-1, :-1]
        dy = cy - self.NY[:-1, :-1]
        t = (dx * self.Q[None, :, 1] - dy * self.Q[None, :, 0]) / self.D
        u = (self.R[:, None, 0] * dy - self.R[:, None, 1] * dx) / self.D
        lo_t = np.full(self.n_h, -margin)
        hi_t = np.full(self.n_h, 1 + margin)
        lo_u = np.full(self.n_v, -margin)
        hi_u = np.full(self.n_v, 1 + margin)
        lo_t[0], hi_t[-1], lo_u[0], hi_u[-1] = -np.inf, np.inf, -np.inf, np.inf
        m = self.ok & (t >= lo_t[:, None]) & (t <= hi_t[:, None]) & (u >= lo_u[None, :]) & (u <= hi_u[None, :])
        return list(zip(*[a.tolist() for a in np.nonzero(m)]))

    @staticmethod
    def ori(ax, ay, bx, by, cx, cy):
        d = (Fr(ax) - cx) * (Fr(by) - cy) - (Fr(ay) - cy) * (Fr(bx) - cx)
        return (d > 0) - (d < 0)

    def contains(self, i, j, cx, cy):
        X, Y, s = self.NX, self.NY, int(self.orient[i, j])
        cxf, cyf = Fr(cx), Fr(cy)
        if i > 0 and s * self.ori(X[i, j], Y[i, j], X[i, j + 1], Y[i, j + 1], cxf, cyf) > 0:
            return False
        if i < self.n_h - 1 and s * self.ori(X[i + 1, j], Y[i + 1, j], X[i + 1, j + 1], Y[i + 1, j + 1], cxf, cyf) < 0:
            return False
        if j > 0 and s * self.ori(X[i, j], Y[i, j], X[i + 1, j], Y[i + 1, j], cxf, cyf) < 0:
            return False
        if j < self.n_v - 1 and s * self.ori(X[i, j + 1], Y[i, j + 1], X[i + 1, j + 1], Y[i + 1, j + 1], cxf, cyf) > 0:
            return False
        return True

    def cells(self, p):
        cx, cy = p[0] + self.sv.origin[0], p[1] + self.sv.origin[1]
        return {(i, j) for i, j in self.candidates(cx, cy) if self.contains(i, j, cx, cy)}


def raw_cells(sv, p):
    return list(dict.fromkeys(sv._pairs_grid(p, "all")))


class Count:
    def __init__(self):
        self.probes = self.law = self.false = self.lost = self.odd = self.truth_law = 0
        self.false_probes = self.lost_probes = 0
        self.raw_law = 0

    def line(self):
        return ("probes %6d | law %5d | probes with false roots %5d (false reports %5d) | with lost roots %4d (%4d) | "
                "odd-near %5d | truth-sum != w %d | raw-sum != w %d"
                % (self.probes, self.law, self.false_probes, self.false, self.lost_probes, self.lost, self.odd,
                   self.truth_law, self.raw_law))


def run_probe(sv, truth, w, p, cnt, tag, near=None):
    cnt.probes += 1
    raw = raw_cells(sv, p)
    rep = {(i, j) for i, j, _, _ in raw}
    tr = truth.cells(p)
    false = rep - tr
    lost = tr - rep
    cnt.false += len(false)
    cnt.lost += len(lost)
    cnt.false_probes += bool(false)
    cnt.lost_probes += bool(lost)
    tsum = sum(int(truth.orient[c]) for c in tr)
    cnt.truth_law += tsum != w
    rsum = sum(int(truth.orient[c]) for c in rep)
    cnt.raw_law += rsum != w
    res = sv.solve(p, extensions="all")
    msum = sum(h.orient for h in res.hits)
    bad = msum != w
    cnt.law += bad
    if near is not None:
        sh, sv_ = near
        rows = [sv._finish(p, [one])._rows[0] for one in raw]
        k = sum(1 for x in rows if abs(x[0] - sh) < 1.0 and abs(x[1] - sv_) < 1.0)
        cnt.odd += k % 2
    if bad or false or lost:
        OUT.write("%s p=(%.17g, %.17g) merged_sum=%d w=%d false=%s lost=%s truth=%s\n"
                  % (tag, p[0], p[1], msum, w, sorted(false), sorted(lost), sorted(tr)))


def rig_probes(name, H, V, n_random=2000, seed=23):
    import auto_mapping as am
    frame = am._Frame(H, V)
    sv = ci.ChordIntersect.from_frame(frame)
    sv.prepare_grid()
    truth = Truth(sv)
    w = winding(sv)
    r, q = sv._r, sv._q
    orient = lambda i, j: int(truth.orient[i, j])
    print("\n=== %s (%s): n_h=%d n_v=%d orient0=%+d w=%d" % (name, NAME, sv.n_h, sv.n_v, sv.orient0, w))
    t0 = time.perf_counter()
    # (a) fold edges / (n) non-fold edges
    fold, nonfold = Count(), Count()
    edges = [("v", i, j) for i in range(sv.n_h - 1) for j in range(sv.n_v)]
    edges += [("h", i, j) for i in range(sv.n_h) for j in range(sv.n_v - 1)]
    d_fold = [10.0 ** (e / 4.0) for e in range(-48, -7)]           # 1e-12 .. 1e-2
    d_non = [10.0 ** (e / 1.0) for e in range(-12, -1)] + [3e-8, 3e-7]
    rng = random.Random(5)
    nf_edges = [e for e in edges if orient(e[1], e[2]) == (orient(e[1] + 1, e[2]) if e[0] == "v" else orient(e[1], e[2] + 1))]
    nf_edges = rng.sample(nf_edges, min(len(nf_edges), 400))
    f_edges = [e for e in edges if e not in set(nf_edges) and orient(e[1], e[2]) != (orient(e[1] + 1, e[2]) if e[0] == "v" else orient(e[1], e[2] + 1))]
    for group, cnt, ds in ((f_edges, fold, d_fold), (nf_edges, nonfold, d_non)):
        for kind, i, j in group:
            if kind == "v":
                sh = sv.h_cum[i + 1] - sv.h_arc
                svv = 0.5 * (sv.v_cum[j] + sv.v_cum[j + 1]) - sv.v_arc
                e = q[j]
            else:
                sh = 0.5 * (sv.h_cum[i] + sv.h_cum[i + 1]) - sv.h_arc
                svv = sv.v_cum[j + 1] - sv.v_arc
                e = r[i]
            P = sv.hv(sh, svv)
            n = math.hypot(*e)
            nx, ny = -e[1] / n, e[0] / n
            for side in (1.0, -1.0):
                for d in ds:
                    p = (P[0] + side * d * nx, P[1] + side * d * ny)
                    run_probe(sv, truth, w, p, cnt, "edge %s %d %d side %+d d %.1e" % (kind, i, j, side, d),
                              (sh, svv) if (cnt is fold and d <= 1e-3) else None)
    print("(a) fold edges %4d      : %s" % (len(f_edges), fold.line()))
    print("(n) non-fold edges %4d  : %s" % (len(nf_edges), nonfold.line()))
    # (b) corners: every interior node, 8 directions
    corner = Count()
    ds = [10.0 ** (e / 2.0) for e in range(-24, -7)]               # 1e-12 .. 1e-4
    nodes = [(i, j) for i in range(1, sv.n_h) for j in range(1, sv.n_v)]
    nodes = random.Random(9).sample(nodes, min(len(nodes), 300))
    for i, j in nodes:
        C = sv.hv(sv.h_cum[i] - sv.h_arc, sv.v_cum[j] - sv.v_arc)
        for k in range(8):
            a = 2 * math.pi * (k + 0.37) / 8
            for d in ds:
                p = (C[0] + d * math.cos(a), C[1] + d * math.sin(a))
                run_probe(sv, truth, w, p, corner, "node %d %d dir %d d %.1e" % (i, j, k, d))
    print("(b) corners %4d x 8 dirs: %s" % (len(nodes), corner.line()))
    # (b') cusp corners as t_chord_intersect (b)
    cusp = Count()
    cusps = []
    for i in range(sv.n_h - 1):
        for j in range(sv.n_v - 1):
            o = [orient(i, j), orient(i + 1, j), orient(i + 1, j + 1), orient(i, j + 1)]
            odd = [k for k in range(4) if o[k] != o[(k + 1) % 4] and o[k] != o[(k + 3) % 4]]
            if len(odd) != 1:
                continue
            dirs = [(-r[i][0], -r[i][1]), (-q[j][0], -q[j][1]), r[i + 1], q[j + 1]]
            tot = sum(math.atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1]) for a, b in zip(dirs, dirs[1:] + dirs[:1]))
            if abs(round(tot / (2.0 * math.pi))) == 1:
                cusps.append((i, j, odd[0], dirs))
    for i, j, k, dirs in cusps:
        a, b = dirs[k], dirs[(k + 1) % 4]
        na, nb = math.hypot(*a), math.hypot(*b)
        bis = (a[0] / na + b[0] / nb, a[1] / na + b[1] / nb)
        nbis = math.hypot(*bis)
        C = sv.hv(sv.h_cum[i + 1] - sv.h_arc, sv.v_cum[j + 1] - sv.v_arc)
        for e in range(-96, -31):
            d = 10.0 ** (e / 8.0)
            p = (C[0] + d * bis[0] / nbis, C[1] + d * bis[1] / nbis)
            run_probe(sv, truth, w, p, cusp, "cusp %d %d d %.1e" % (i, j, d))
    print("(b') cusp corners %4d   : %s" % (len(cusps), cusp.line()))
    # (c) random
    rnd = Count()
    rr = random.Random(seed)
    pts = [(rr.uniform(-450.0, 450.0), rr.uniform(-450.0, 450.0)) for _ in range(n_random)]
    for p in pts:
        run_probe(sv, truth, w, p, rnd, "random")
    print("(c) random               : %s" % rnd.line())
    # path equality on the patched module: grid raw == numpy raw (all probes of (c) + some edge probes), matrix on a subsample
    mism = 0
    allpts = pts[:500]
    if not sv._np_ready:
        sv._prepare_numpy()
    vec = sv._pairs_numpy(allpts)
    for k, p in enumerate(allpts):
        g = sorted(raw_cells(sv, p))
        n_ = sorted(set(vec[k]))
        if g != n_:
            mism += 1
        if k < 60:
            m_ = sorted(set(sv._pairs_python(p)))
            if m_ != g:
                mism += 1
    print("paths: grid raw vs numpy raw (500 probes) and vs matrix raw (60): %d mismatches (bitwise tuples)" % mism)
    print("time %.1f s" % (time.perf_counter() - t0))
    return sv


if __name__ == "__main__":
    which = sys.argv[2] if len(sys.argv) > 2 else "bow100"
    rigs = {"bow100": (sin_guide(100.0, "h"), sin_guide(100.0, "v")),
            "bow60": (sin_guide(60.0, "h"), sin_guide(60.0, "v")),
            "S1.5": s_guides(1.5)}
    for key in which.split(","):
        rig_probes(key, *rigs[key])
    if hasattr(ci, "EXACT_CALLS"):
        print("exact fallbacks taken: %d" % ci.EXACT_CALLS[0])
    OUT.close()
