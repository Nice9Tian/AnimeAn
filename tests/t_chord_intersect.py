"""pyfile/chord_intersect.py: the segment-pair ("matrix") inverse of the HV
frame map, checked against auto_mapping._Frame on the rig of
docs/auto_mapping/point_mapping_newton.md §9 (sinusoidal guides, 101 points,
period 300 px, 25x25 probes on a 400x400 canvas).

What must hold:
  * every crossing it reports is a true preimage: hv(hit) lands on the point;
  * where damped Newton converges, Newton's answer is among the crossings, and
    on an unfolded frame it IS the principal one;
  * a straight frame yields exactly the chord-basis decomposition;
  * the numpy matrix path and the pure-python pair loop agree;
  * from_frame reads a _Frame's own origin, so arcs agree with frame.hv;
  * interior cell sides are tested by exact orient2d on the rounded node mesh
    (pinning t / u to 0.0 / 1.0 iff orient2d == 0), and duplicates are merged
    combinatorially (same orient across a shared interior edge or node where
    orient2d == 0, with no distance threshold): every raw report matches exact
    rational (Fraction) cell membership (0 false, 0 lost), and on the folded
    100 px rig the conservation law sum(orient) == w holds next to every fold
    edge, across all 5200 cusp-wedge probes (0 cusp blind-spot errors), and at
    random probes.
"""
from fractions import Fraction
import collections
import math
import os
import random
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am  # noqa: E402
import chord_intersect as ci  # noqa: E402

TOL = 1e-7


def sin_guide(amp, axis, n=101, half=200.0, period=300.0):
    pts = []
    for k in range(n):
        s = -half + 2.0 * half * k / (n - 1)
        bow = amp * math.sin(2.0 * math.pi * s / period)
        pts.append((s, bow) if axis == "h" else (bow, s))
    return pts


def probes(n=25, half=150.0):
    return [(-half + 2.0 * half * a / (n - 1), -half + 2.0 * half * b / (n - 1))
            for a in range(n) for b in range(n)]


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def newton(frame, solver, p):
    seed = solver.chord_seed(p)
    return frame.solve_full(p, seed[0], seed[1])


# ---------------------------------------------------------------- straight frame
frame = am._Frame([(-300.0, 0.0), (300.0, 0.0)], [(0.0, -200.0), (0.0, 200.0)])
solver = ci.ChordIntersect.from_frame(frame)
assert solver.orient0 == 1
for p in probes():
    res = solver.solve(p)
    assert res.count == 1 and res.principal is not None, (p, res.hits)
    seed = solver.chord_seed(p)
    assert abs(res.principal.l_h - seed[0]) < 1e-9 and abs(res.principal.l_v - seed[1]) < 1e-9
    assert dist(frame.hv(*res.arcs), p) < 1e-9
print("straight frame: 625 probes, one crossing each, equals the chord-basis seed")

# ---------------------------------------------------------------- bowed frames
summary = []
for amp in (10.0, 30.0, 60.0):
    frame = am._Frame(sin_guide(amp, "h"), sin_guide(amp, "v"))
    solver = ci.ChordIntersect.from_frame(frame)
    assert abs(solver.h_arc - frame.h_arc) < 1e-12 and abs(solver.v_arc - frame.v_arc) < 1e-12
    pts = probes()
    results = solver.solve_many(pts)
    assert len(results) == len(pts)
    multi = sum(1 for r in results if r.count_on_sheet >= 2)
    none = sum(1 for r in results if r.count_on_sheet == 0)
    newton_ok = newton_in_hits = principal_eq = 0
    for p, res in zip(pts, results):
        for h in res.hits:                                 # every crossing is a real preimage
            assert dist(frame.hv(h.l_h, h.l_v), p) < 1e-6, (amp, p, h)
            assert dist(solver.hv(h.l_h, h.l_v), p) < 1e-6
        l_h, l_v, resid = newton(frame, solver, p)
        if resid < TOL:
            newton_ok += 1
            near = [h for h in res.hits if abs(h.l_h - l_h) < 1e-5 and abs(h.l_v - l_v) < 1e-5]
            assert near, ("Newton's converged answer is missing from the crossings", amp, p, (l_h, l_v), res.hits)
            newton_in_hits += 1
            if res.principal is not None and near[0] is res.principal:
                principal_eq += 1
    if amp <= 30.0:
        # unfolded: one preimage on the drawn guides everywhere, and it is Newton's
        assert multi == 0 and none == 0, (amp, multi, none)
        assert newton_ok == len(pts) and principal_eq == len(pts), (amp, newton_ok, principal_eq)
        assert all(r.principal.on_sheet for r in results)
    summary.append((amp, len(pts), newton_ok, newton_in_hits, principal_eq, multi, none))
for amp, n, nok, inh, peq, multi, none in summary:
    print("bow %3.0f px: %d probes | Newton solved %d, all of them among the crossings (%d), "
          "principal == Newton on %d | points with >=2 preimages on the guides %d, with 0 %d"
          % (amp, n, nok, inh, peq, multi, none))

# ---------------------------------------------------------------- round trip
rng = random.Random(7)
frame = am._Frame(sin_guide(30.0, "h"), sin_guide(30.0, "v"))
solver = ci.ChordIntersect.from_frame(frame)
for _ in range(300):
    l_h = rng.uniform(-solver.h_arc + 1.0, solver.h_cum[-1] - solver.h_arc - 1.0)
    l_v = rng.uniform(-solver.v_arc + 1.0, solver.v_cum[-1] - solver.v_arc - 1.0)
    p = frame.hv(l_h, l_v)
    res = solver.solve(p)
    assert any(abs(h.l_h - l_h) < 1e-6 and abs(h.l_v - l_v) < 1e-6 for h in res.hits), (l_h, l_v, res.hits)
print("round trip: 300 random arcs -> hv -> crossings recover the arcs")

# ---------------------------------------------------------------- numpy vs pure python
pts = probes(9)
vec = solver.solve_many(pts)
for p, r in zip(pts, vec):
    loop = solver._finish(p, solver._pairs_python(p))
    assert [(h.i, h.j) for h in loop.hits] == [(h.i, h.j) for h in r.hits], (p, loop.hits, r.hits)
    for a, b in zip(loop.hits, r.hits):
        assert abs(a.l_h - b.l_h) < 1e-9 and abs(a.l_v - b.l_v) < 1e-9
print("matrix path and pair loop agree on %d probes" % len(pts))

# ---------------------------------------------------------------- past the guide ends
# hv extends each guide straight past its ends (auto_mapping._point_at_arc), so a
# point beyond the drawn sheet is reached on the extended end segments: the
# crossing lands there with an arc outside the guide, and hv still maps it back.
frame = am._Frame(sin_guide(10.0, "h"), sin_guide(10.0, "v"))
solver = ci.ChordIntersect.from_frame(frame)
res = solver.solve((900.0, 900.0))
assert res.count >= 1 and res.principal is not None, res.hits
h = res.principal
assert h.i == solver.n_h - 1 and h.j == solver.n_v - 1 and h.t > 1.0 and h.u > 1.0, h
assert h.l_h > solver.h_cum[-1] - solver.h_arc and h.l_v > solver.v_cum[-1] - solver.v_arc
assert dist(frame.hv(*res.arcs), (900.0, 900.0)) < 1e-6
print("a point past the guide ends: found on the straight extensions, hv maps it back")

# ---------------------------------------------------------------- the grid vs the matrix
# solve locates by the cell grid (prepare_grid) and tests a handful of cells;
# it must give exactly the matrix's hits (extensions="all") and exactly the
# matrix's principal (the default, extensions="lazy", which skips the
# extensions once the sheet has a same-orientation preimage). Rigs: the §9
# sinusoids up to the folded 100 px one, the user's hand-drawn S stroke
# (digitised 2026-10-26, chord normalised to 400 px, 130 px peak-to-peak
# deviation) at 1.0x and 1.5x, the straight frame, and points far outside
# every sheet, which only the extensions reach.
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


def same_hits(a, b):
    return len(a.hits) == len(b.hits) and all(
        x.i == y.i and x.j == y.j and abs(x.l_h - y.l_h) < 1e-9 and abs(x.l_v - y.l_v) < 1e-9
        for x, y in zip(a.hits, b.hits))


def same_principal(a, b):
    if a.principal is None or b.principal is None:
        return a.principal is None and b.principal is None
    return (a.principal.i == b.principal.i and a.principal.j == b.principal.j
            and abs(a.principal.l_h - b.principal.l_h) < 1e-9 and abs(a.principal.l_v - b.principal.l_v) < 1e-9)


rng = random.Random(11)
rigs = [("straight", [(-300.0, 0.0), (300.0, 0.0)], [(0.0, -200.0), (0.0, 200.0)])]
rigs += [("bow %d px" % amp, sin_guide(amp, "h"), sin_guide(amp, "v")) for amp in (10, 30, 60, 100)]
rigs += [("S stroke %.1fx" % sc, *s_guides(sc)) for sc in (1.0, 1.5)]
for name, H, V in rigs:
    frame = am._Frame(H, V)
    solver = ci.ChordIntersect.from_frame(frame)
    pts = []
    for _ in range(400):      # images of random arcs: at least one preimage each
        l_h = rng.uniform(-solver.h_arc * 0.9, (solver.h_cum[-1] - solver.h_arc) * 0.9)
        l_v = rng.uniform(-solver.v_arc * 0.9, (solver.v_cum[-1] - solver.v_arc) * 0.9)
        pts.append(frame.hv(l_h, l_v))
    pts += probes(9)                                                       # the §9 lattice
    pts += [(rng.uniform(-1500, 1500), rng.uniform(-1500, 1500)) for _ in range(150)]   # far outside
    x0, y0, inv, nbx, nby, _sheet = solver.prepare_grid()
    far = 0
    multi = 0
    for p in pts:
        matrix = solver.solve_matrix(p)
        full = solver.solve(p, extensions="all")
        lazy = solver.solve(p)
        assert same_hits(full, matrix), (name, p, full.hits, matrix.hits)
        assert same_principal(lazy, matrix), (name, p, lazy.principal, matrix.principal)
        # the lazy result keeps every sheet crossing; only extensions may be left out
        assert lazy.count_on_sheet == matrix.count_on_sheet, (name, p)
        if lazy.principal is not None:
            assert lazy.principal is lazy.hits[[k for k, h in enumerate(lazy.hits) if h is lazy.principal][0]]
            assert dist(frame.hv(*lazy.arcs), p) < 1e-6
        cx, cy = p[0] + solver.origin[0], p[1] + solver.origin[1]
        if not (0.0 <= (cx - x0) * inv < nbx and 0.0 <= (cy - y0) * inv < nby):
            far += 1
        multi += matrix.count_on_sheet >= 2
    print("%-14s %d points (%d outside the grid, %d with several sheet preimages): "
          "grid hits == matrix, principal == matrix" % (name + ":", len(pts), far, multi))
# the batch path with method="grid", and bins of another size, give the same
frame = am._Frame(sin_guide(60.0, "h"), sin_guide(60.0, "v"))
solver = ci.ChordIntersect.from_frame(frame)
pts = probes(15)
ref = solver.solve_many(pts)
for bin_size in (2.0, 8.0, 25.0):
    solver.prepare_grid(bin_size=bin_size)
    for a, b in zip(solver.solve_many(pts, method="grid", extensions="all"), ref):
        assert same_hits(a, b), (bin_size, a.point)
print("bins of 2, 8 and 25 px and the batch grid path: same hits")

# ---------------------------------------------------------------- orientation pairing
# _finish merges duplicate reports of a root on a shared interior edge or node
# combinatorially: in _test, an interior side pins t / u to 0.0 or 1.0 iff
# exact orient2d == 0 (strictly interior hits stay in (0.0, 1.0)). Two cells
# sharing an edge or node with the SAME orient map their union one-to-one, so
# boundary-pinned reports of the same orient are one root; with OPPOSITE orients
# the shared edge is a fold crease and both reports are kept; two genuine
# same-orient roots in diagonally opposite cells of a cusp wedge have
# 0.0 < t, u < 1.0 and are never merged, however close in (l_h, l_v).
D = ci._DEDUP


def row_cell(l_h, l_v, i, j, t, u, orient):
    return (l_h, l_v, i, j, t, u, orient, True)


# one root on an interior node shared by four cells (0..1 x 0..1), all orient +1
m = ci._merge_duplicates([
    row_cell(10.0, 20.0, 0, 0, 1.0, 1.0, 1),
    row_cell(10.0, 20.0, 1, 0, 0.0, 1.0, 1),
    row_cell(10.0, 20.0, 0, 1, 1.0, 0.0, 1),
    row_cell(10.0, 20.0, 1, 1, 0.0, 0.0, 1),
])
assert len(m) == 1, m
# a shared vertical edge between (0, 0) and (1, 0) with opposite orients (fold crease): both kept
m = ci._merge_duplicates([
    row_cell(10.0, 20.0, 0, 0, 1.0, 0.5, 1),
    row_cell(10.0, 20.0, 1, 0, 0.0, 0.5, -1),
])
assert sorted(r[6] for r in m) == [-1, 1], m
# an opposite-orient report sorting BETWEEN two shared-edge duplicates
m = ci._merge_duplicates([
    row_cell(10.0, 20.0, 0, 0, 1.0, 0.5, 1),
    row_cell(10.0, 20.0 + 0.2 * D, 1, 0, 0.2, 0.5, -1),
    row_cell(10.0, 20.0 + 0.4 * D, 1, 0, 0.0, 0.5, 1),
])
assert sorted(r[6] for r in m) == [-1, 1], m
# cusp-wedge blind spot test: two genuine same-orient roots in diagonally opposite
# cells (0, 0) and (1, 1), closer than _DEDUP in (l_h, l_v) but strictly interior
m = ci._merge_duplicates([
    row_cell(10.0, 20.0, 0, 0, 1.0 - 1e-9, 1.0 - 1e-9, 1),
    row_cell(10.0 + 0.1 * D, 20.0 + 0.1 * D, 1, 1, 1e-9, 1e-9, 1),
])
assert len(m) == 2, m
# shared vertical edge across a culled zero-length H segment (i=0 at t=1.0 and i=2 at t=0.0 with l_h equal)
m = ci._merge_duplicates([
    row_cell(10.0, 20.0, 0, 0, 1.0, 0.5, 1),
    row_cell(10.0, 20.0, 2, 0, 0.0, 0.5, 1),
])
assert len(m) == 1, m
print("merge rule: node duplicates merged, crease pairs kept, cusp-wedge near roots kept, zero-length segment bridged")


def winding(solver):
    """w of the four end directions d1, e1, -d0, -e0 (short arcs)."""
    def unit(a, b):
        dx, dy = b[0] - a[0], b[1] - a[1]
        n = math.hypot(dx, dy)
        return dx / n, dy / n
    d0, d1 = unit(solver.h[0], solver.h[1]), unit(solver.h[-2], solver.h[-1])
    e0, e1 = unit(solver.v[0], solver.v[1]), unit(solver.v[-2], solver.v[-1])
    loop = [d1, e1, (-d0[0], -d0[1]), (-e0[0], -e0[1])]
    tot = sum(math.atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1])
              for a, b in zip(loop, loop[1:] + loop[:1]))
    return int(round(tot / (2.0 * math.pi)))


def raw_rows(solver, p):
    """Every report, as _finish's rows but NOT merged: the grid path with all
    extensions (as fast as the matrix is slow), where an end cell's sheet hit is
    reported twice - by its sheet tuple and its extension tuple - with the same
    (i, j, t, u); those exact repeats are dropped, which leaves the matrix's list."""
    return [solver._finish(p, [one])._rows[0] for one in dict.fromkeys(solver._pairs_grid(p, "all"))]


def distance_only(rows):
    """The merge rule before 2026-10-30: by distance alone, last kept row only."""
    rows = sorted(rows)
    kept = [rows[0]]
    for x in rows[1:]:
        if abs(x[0] - kept[-1][0]) < D and abs(x[1] - kept[-1][1]) < D:
            continue
        kept.append(x)
    return kept


def dedup_distance(rows):
    """The Scheme-C distance-threshold orientation pairing (_DEDUP = 1e-6 px)."""
    rows = sorted(rows)
    kept = []
    for row in rows:
        dup = False
        k = len(kept) - 1
        while k >= 0 and row[0] - kept[k][0] < D:
            prev = kept[k]
            if prev[6] == row[6] and abs(row[1] - prev[1]) < D:
                dup = True
                break
            k -= 1
        if not dup:
            kept.append(row)
    return kept


class ExactMeshTruth:
    """Exact closed-cell membership on the rounded node mesh using Fraction only."""

    def __init__(self, sv):
        self.sv = sv
        self.h, self.v = sv.h, sv.v
        self.n_h, self.n_v = sv.n_h, sv.n_v
        self.orient = [[1 if sv._r[i][0] * sv._q[j][1] - sv._r[i][1] * sv._q[j][0] >= 0.0 else -1
                        for j in range(self.n_v)] for i in range(self.n_h)]
        self.ok = [[abs(sv._r[i][0] * (-sv._q[j][1]) - sv._r[i][1] * (-sv._q[j][0]))
                    > 1e-12 * (math.hypot(*sv._r[i]) * math.hypot(*sv._q[j]) + 1e-300)
                    for j in range(self.n_v)] for i in range(self.n_h)]

    @staticmethod
    def ori(ax, ay, bx, by, cxf, cyf):
        d = (Fraction(ax) - cxf) * (Fraction(by) - cyf) - (Fraction(ay) - cyf) * (Fraction(bx) - cxf)
        return (d > 0) - (d < 0)

    def side_signs(self, i, j, cxf, cyf):
        """Exact s * orient2d on (L, R, B, T) interior sides, or None when rejected."""
        h, v, s = self.h, self.v, self.orient[i][j]
        x00, y00 = h[i][0] + v[j][0], h[i][1] + v[j][1]
        x10, y10 = h[i + 1][0] + v[j][0], h[i + 1][1] + v[j][1]
        x01, y01 = h[i][0] + v[j + 1][0], h[i][1] + v[j + 1][1]
        x11, y11 = h[i + 1][0] + v[j + 1][0], h[i + 1][1] + v[j + 1][1]
        oL = s * self.ori(x00, y00, x01, y01, cxf, cyf) if i > 0 else -1
        if oL > 0:
            return None
        oR = s * self.ori(x10, y10, x11, y11, cxf, cyf) if i < self.n_h - 1 else 1
        if oR < 0:
            return None
        oB = s * self.ori(x00, y00, x10, y10, cxf, cyf) if j > 0 else 1
        if oB < 0:
            return None
        oT = s * self.ori(x01, y01, x11, y11, cxf, cyf) if j < self.n_v - 1 else -1
        if oT > 0:
            return None
        return (oL, oR, oB, oT)

    def candidates(self, cx, cy, margin=1e-3):
        out = []
        h, v, r, q = self.h, self.v, self.sv._r, self.sv._q
        last_i, last_j = self.n_h - 1, self.n_v - 1
        for i in range(self.n_h):
            rx, ry = r[i]
            for j in range(self.n_v):
                if not self.ok[i][j]:
                    continue
                qx, qy = q[j]
                den = -rx * qy + ry * qx
                dx, dy = cx - (h[i][0] + v[j][0]), cy - (h[i][1] + v[j][1])
                t = (-dx * qy + dy * qx) / den
                u = (dx * ry - dy * rx) / den
                if (i > 0 and t < -margin) or (i < last_i and t > 1.0 + margin):
                    continue
                if (j > 0 and u < -margin) or (j < last_j and u > 1.0 + margin):
                    continue
                out.append((i, j))
        return out

    def query(self, p):
        """Returns dict {(i, j): (oL, oR, oB, oT)} of all exact cells containing p."""
        cx, cy = p[0] + self.sv.origin[0], p[1] + self.sv.origin[1]
        cxf, cyf = Fraction(cx), Fraction(cy)
        res = {}
        for i, j in self.candidates(cx, cy):
            signs = self.side_signs(i, j, cxf, cyf)
            if signs is not None:
                res[(i, j)] = signs
        return res


frame = am._Frame(sin_guide(100.0, "h"), sin_guide(100.0, "v"))
solver = ci.ChordIntersect.from_frame(frame)
truth = ExactMeshTruth(solver)
w = winding(solver)
assert (solver.orient0, w) == (1, -1), (solver.orient0, w)
r, q = solver._r, solver._q


def orient(i, j):
    return 1 if r[i][0] * q[j][1] - r[i][1] * q[j][0] >= 0.0 else -1


def near_rows(rows, sh, sv):
    return [x for x in rows if abs(x[0] - sh) < 0.5 and abs(x[1] - sv) < 0.5]


# (a) just off every fold edge on the sheet - vertical (cells (i, j) | (i+1, j)) and
# horizontal ((i, j) | (i, j+1)) pairs of opposite orient, probed at the edge's
# midpoint - on the side where both cells cover the point, close enough that the
# two genuine roots lie within _DEDUP of each other
edges = [("v", i, j) for i in range(solver.n_h - 1) for j in range(solver.n_v) if orient(i, j) != orient(i + 1, j)]
edges += [("h", i, j) for i in range(solver.n_h) for j in range(solver.n_v - 1) if orient(i, j) != orient(i, j + 1)]
deltas = [10.0 ** (e / 8.0) for e in range(-80, -39)]          # 1e-10 .. 1e-5 px
probed = old_fail = 0
for kind, i, j in edges:
    if kind == "v":
        sh = solver.h_cum[i + 1] - solver.h_arc
        sv = 0.5 * (solver.v_cum[j] + solver.v_cum[j + 1]) - solver.v_arc
        e = q[j]
    else:
        sh = 0.5 * (solver.h_cum[i] + solver.h_cum[i + 1]) - solver.h_arc
        sv = solver.v_cum[j + 1] - solver.v_arc
        e = r[i]
    P = solver.hv(sh, sv)
    n = math.hypot(*e)
    nx, ny = -e[1] / n, e[0] / n
    hit = None
    for side in (1.0, -1.0):
        for d in deltas:
            p = (P[0] + side * d * nx, P[1] + side * d * ny)
            rows = raw_rows(solver, p)
            near = near_rows(rows, sh, sv)
            if (len(near) == 2 and near[0][6] != near[1][6]
                    and all(0.0 <= x[4] <= 1.0 and 0.0 <= x[5] <= 1.0 for x in near)
                    and abs(near[0][0] - near[1][0]) < D and abs(near[0][1] - near[1][1]) < D):
                hit = (p, rows, near)
        if hit:
            break
    assert hit, ("no probe inside the window next to fold edge", kind, i, j)
    p, rows, near = hit
    probed += 1
    for x in near:                                          # both are genuine preimages
        assert dist(frame.hv(x[0], x[1]), p) < 1e-6, (i, j, x)
    res = solver.solve(p, extensions="all")
    assert sum(h.orient for h in res.hits) == w, (i, j, p, res.hits)
    kept_near = [h for h in res.hits if abs(h.l_h - sh) < 0.5 and abs(h.l_v - sv) < 0.5]
    assert sorted(h.orient for h in kept_near) == [-1, 1], (i, j, kept_near)
    old_fail += sum(x[6] for x in distance_only(rows)) != w
assert probed == len(edges) and old_fail == probed, (probed, len(edges), old_fail)
print("bow 100 px, %d fold edges (vertical and horizontal): next to each, two genuine roots within _DEDUP of each other are both kept "
      "and sum(orient) == w = %d; merging by distance alone broke the law at all %d" % (probed, w, old_fail))

# (b) cusp corners: one cell differs from its three neighbours and the corner's
# winding is +-1 (§8.3). Along the odd cell's image sector, from 1e-12 to 1e-4 px
# off the corner image: in the cusp wedge, two genuine same-orient roots and one
# opposite-orient root coexist. With combinatorial deduplication, neither root lies
# on an interior cell boundary (orient2d != 0), so both same-orient roots are kept
# at every distance down to 1e-12 px: off_by_one == 0, every probe matches exact
# Fraction cell membership, and sum(orient) == w everywhere. Under the old _DEDUP
# distance threshold, 3472/5200 probes merged the two same-orient roots (off by 1);
# under distance-only merging, reports collapsed into one (often off by 2).
cusps = []
for i in range(solver.n_h - 1):
    for j in range(solver.n_v - 1):
        o = [orient(i, j), orient(i + 1, j), orient(i + 1, j + 1), orient(i, j + 1)]   # SW SE NE NW
        odd = [k for k in range(4) if o[k] != o[(k + 1) % 4] and o[k] != o[(k + 3) % 4]]
        if len(odd) != 1:
            continue
        dirs = [(-r[i][0], -r[i][1]), (-q[j][0], -q[j][1]), r[i + 1], q[j + 1]]          # W S E N
        tot = sum(math.atan2(a[0] * b[1] - a[1] * b[0], a[0] * b[0] + a[1] * b[1])
                  for a, b in zip(dirs, dirs[1:] + dirs[:1]))
        if abs(round(tot / (2.0 * math.pi))) == 1:
            cusps.append((i, j, odd[0], dirs))
assert cusps, "the folded rig has cusp corners"
probes_b = off_by_one = dedup_off_by_one = old_off_by_two = truth_mism = 0
for i, j, k, dirs in cusps:
    a, b = dirs[k], dirs[(k + 1) % 4]
    na, nb = math.hypot(*a), math.hypot(*b)
    bis = (a[0] / na + b[0] / nb, a[1] / na + b[1] / nb)
    nbis = math.hypot(*bis)
    C = solver.hv(solver.h_cum[i + 1] - solver.h_arc, solver.v_cum[j + 1] - solver.v_arc)
    for e in range(-96, -31):
        d = 10.0 ** (e / 8.0)
        p = (C[0] + d * bis[0] / nbis, C[1] + d * bis[1] / nbis)
        res = solver.solve(p, extensions="all")
        dev = sum(h.orient for h in res.hits) - w
        assert dev == 0, (i, j, d, dev, res.hits)
        rows = raw_rows(solver, p)
        if probes_b % 20 == 0:
            tr = truth.query(p)
            rep_cells = {(x[2], x[3]) for x in rows}
            truth_mism += (rep_cells != set(tr.keys()))
        probes_b += 1
        off_by_one += dev != 0
        dedup_off_by_one += abs(sum(x[6] for x in dedup_distance(rows)) - w) == 1
        old_off_by_two += abs(sum(x[6] for x in distance_only(rows)) - w) == 2
assert off_by_one == 0 and truth_mism == 0 and dedup_off_by_one == 3472 and old_off_by_two > 0, (
    off_by_one, truth_mism, dedup_off_by_one, old_off_by_two)
print("bow 100 px, %d cusp corners, %d probes along the odd sectors: sum(orient) == w at all %d "
      "(off_by_one = %d, Fraction truth mismatches = %d; _DEDUP threshold missed by one at %d, distance-only missed by two at %d)"
      % (len(cusps), probes_b, probes_b, off_by_one, truth_mism, dedup_off_by_one, old_off_by_two))

# (c) random probes on the sheet and around it: the law holds on the merged hits
rng = random.Random(23)
for idx in range(2000):
    p = (rng.uniform(-450.0, 450.0), rng.uniform(-450.0, 450.0))
    res = solver.solve(p, extensions="all")
    assert sum(h.orient for h in res.hits) == w, (p, res.hits)
    if idx < 100:
        tr = truth.query(p)
        rep = {(x[2], x[3]) for x in raw_rows(solver, p)}
        assert rep == set(tr.keys()), (p, rep, set(tr.keys()))
print("bow 100 px, 2000 random probes: sum(orient) == w on every one (100 checked against Fraction truth: 0 false, 0 lost)")

# (d) exact Fraction ground truth on both sides of fold edges, non-fold edges, and
# exact-on-lattice points (nodes and edge midpoints with +-1 ulp nudges)
fold_checked = nonfold_checked = lattice_checked = crease_vanished = 0
for kind, i, j in edges[:40]:
    if kind == "v":
        sh = solver.h_cum[i + 1] - solver.h_arc
        sv_ = 0.5 * (solver.v_cum[j] + solver.v_cum[j + 1]) - solver.v_arc
        e = q[j]
    else:
        sh = 0.5 * (solver.h_cum[i] + solver.h_cum[i + 1]) - solver.h_arc
        sv_ = solver.v_cum[j + 1] - solver.v_arc
        e = r[i]
    P = solver.hv(sh, sv_)
    n = math.hypot(*e)
    nx, ny = -e[1] / n, e[0] / n
    for side in (1.0, -1.0):
        for d in (1e-12, 1e-10, 1e-8, 1e-6, 1e-4):
            p = (P[0] + side * d * nx, P[1] + side * d * ny)
            tr = truth.query(p)
            rows = raw_rows(solver, p)
            rep = {(x[2], x[3]) for x in rows}
            assert rep == set(tr.keys()), ("fold edge truth mismatch", kind, i, j, side, d, rep, set(tr.keys()))
            res = solver.solve(p, extensions="all")
            assert sum(h.orient for h in res.hits) == w, (kind, i, j, side, d, res.hits)
            # near the fold edge, either 2 roots of opposite orient (covered side) or 0 (shadow side)
            near = near_rows(rows, sh, sv_)
            assert len(near) in (0, 2), (kind, i, j, side, d, near)
            fold_checked += 1

nf_edges = [("v", i, j) for i in range(solver.n_h - 1) for j in range(solver.n_v) if orient(i, j) == orient(i + 1, j)]
for kind, i, j in random.Random(31).sample(nf_edges, 40):
    sh = solver.h_cum[i + 1] - solver.h_arc
    sv_ = 0.5 * (solver.v_cum[j] + solver.v_cum[j + 1]) - solver.v_arc
    P = solver.hv(sh, sv_)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            x = math.nextafter(P[0], math.inf if dx > 0 else -math.inf) if dx else P[0]
            y = math.nextafter(P[1], math.inf if dy > 0 else -math.inf) if dy else P[1]
            p = (x, y)
            tr = truth.query(p)
            rows = raw_rows(solver, p)
            assert {(r_[2], r_[3]) for r_ in rows} == set(tr.keys()), (i, j, dx, dy)
            # verifying exact boundary pinning: t == 1.0 on (i, j) and t == 0.0 on (i + 1, j) iff orient2d == 0
            for r_ in rows:
                ci_i, ci_j, ct, cu = r_[2], r_[3], r_[4], r_[5]
                oL, oR, oB, oT = tr[(ci_i, ci_j)]
                if ci_i > 0:
                    assert (ct == 0.0) == (oL == 0), (ci_i, ci_j, ct, oL)
                if ci_i < solver.n_h - 1:
                    assert (ct == 1.0) == (oR == 0), (ci_i, ci_j, ct, oR)
                if ci_j > 0:
                    assert (cu == 0.0) == (oB == 0), (ci_i, ci_j, cu, oB)
                if ci_j < solver.n_v - 1:
                    assert (cu == 1.0) == (oT == 0), (ci_i, ci_j, cu, oT)
            res = solver.solve(p, extensions="all")
            assert sum(h.orient for h in res.hits) == w, (i, j, dx, dy, res.hits)
            # on a non-fold edge, even if both cells accept p (orient2d == 0), combinatorial merge yields 1 local root
            near = [h for h in res.hits if abs(h.l_h - sh) < 0.5 and abs(h.l_v - sv_) < 0.5]
            assert len(near) == 1, (i, j, dx, dy, near)
            nonfold_checked += 1

for kind, i, j in edges[:40]:
    if kind == "v":
        sh = solver.h_cum[i + 1] - solver.h_arc
        sv_ = 0.5 * (solver.v_cum[j] + solver.v_cum[j + 1]) - solver.v_arc
    else:
        sh = 0.5 * (solver.h_cum[i] + solver.h_cum[i + 1]) - solver.h_arc
        sv_ = solver.v_cum[j + 1] - solver.v_arc
    P = solver.hv(sh, sv_)
    for dx in (-1, 0, 1):
        for dy in (-1, 0, 1):
            x = math.nextafter(P[0], math.inf if dx > 0 else -math.inf) if dx else P[0]
            y = math.nextafter(P[1], math.inf if dy > 0 else -math.inf) if dy else P[1]
            p = (x, y)
            tr = truth.query(p)
            rows = raw_rows(solver, p)
            assert {(r_[2], r_[3]) for r_ in rows} == set(tr.keys()), (kind, i, j, dx, dy)
            res = solver.solve(p, extensions="all")
            assert sum(h.orient for h in res.hits) == w
            near = [h for h in res.hits if abs(h.l_h - sh) < 1e-4 and abs(h.l_v - sv_) < 1e-4]
            if not near:
                # rounded/nudged onto the 0-layer shadow side: exact Fraction truth also has neither fold cell
                pair = {(i, j), (i + 1, j)} if kind == "v" else {(i, j), (i, j + 1)}
                assert not (pair & set(tr.keys())), (kind, i, j, dx, dy, tr.keys())
                crease_vanished += 1
            else:
                assert sorted(h.orient for h in near) == [-1, 1], (kind, i, j, dx, dy, near)
            lattice_checked += 1
print("Fraction truth checks: %d fold-edge probes, %d non-fold lattice probes, %d fold-crease lattice probes "
      "(%d on 0-layer side verified absent in Fraction truth): 0 false, 0 lost, exact boundary pinning verified"
      % (fold_checked, nonfold_checked, lattice_checked, crease_vanished))

# (e) zero-length segment & bitwise agreement across grid, numpy, and matrix paths
H_zero = [(-100.0, 0.0), (0.0, 0.0), (0.0, 0.0), (100.0, 0.0)]   # segment 1 has zero length
V_zero = [(0.0, -100.0), (0.0, 0.0), (0.0, 100.0)]
sv_zero = ci.ChordIntersect(H_zero, V_zero)
tr_zero = ExactMeshTruth(sv_zero)
for p in [(0.0, 25.0), (0.0, 0.0), (-20.0, 0.0), (35.0, -40.0)]:
    tr_z = tr_zero.query(p)
    raw_g = list(dict.fromkeys(sv_zero._pairs_grid(p, "all")))
    raw_m = sv_zero._pairs_python(p)
    raw_n = sv_zero._pairs_numpy([p])[0] if (sv_zero._prepare_numpy() or True) else []
    assert raw_g == raw_m == raw_n, (p, raw_g, raw_m, raw_n)
    assert {(x[0], x[1]) for x in raw_g} == set(tr_z.keys()), (p, raw_g, tr_z)
    res_z = sv_zero.solve(p, extensions="all")
    assert res_z.count == 1 and res_z.principal is not None and dist(sv_zero.hv(*res_z.arcs), p) < 1e-9, (p, res_z.hits)
print("zero-length segment & path identity: grid, numpy, and matrix raw tuples agree bit-for-bit and bridge zero-length segments")

# ---------------------------------------------------------------- exact degeneracies, integer rigs
# Integer guides and integer / half-integer points: every node is exactly representable
# and points land exactly on cell edges and nodes all the time, also on the extensions.
# Truth = Fraction enumeration of closed cells, guide ends open, one root per distinct
# parameter point. Regression for _extension_candidates: a point whose family coordinate
# k is exactly the family's kmin / kmax used to look only in `below` / `above` and lost
# the cells meeting there (the matrix never did).
from fractions import Fraction as _Fr


def _corner_degree(h, v, a, b):
    """Local degree at interior corner (a, b): signed angle of the four neighbour nodes / 2 pi."""
    ring = [(h[a + 1][0] - h[a][0], h[a + 1][1] - h[a][1]), (v[b + 1][0] - v[b][0], v[b + 1][1] - v[b][1]),
            (h[a - 1][0] - h[a][0], h[a - 1][1] - h[a][1]), (v[b - 1][0] - v[b][0], v[b - 1][1] - v[b][1])]
    angle = sum(math.atan2(p[0] * q[1] - p[1] * q[0], p[0] * q[0] + p[1] * q[1]) for p, q in zip(ring, ring[1:] + ring[:1]))
    return round(angle / (2.0 * math.pi))


def _exact_roots(h, v, c):
    n_h, n_v = len(h) - 1, len(v) - 1
    rr = [(h[i + 1][0] - h[i][0], h[i + 1][1] - h[i][1]) for i in range(n_h)]
    qq = [(v[j + 1][0] - v[j][0], v[j + 1][1] - v[j][1]) for j in range(n_v)]

    def key(k, t):
        return ("node", k + (1 if t == 1 else 0)) if t == 0 or t == 1 else ("arc", k, t)
    out = collections.Counter()
    seen = set()
    for i in range(n_h):
        for j in range(n_v):
            den = rr[i][0] * (-qq[j][1]) - rr[i][1] * (-qq[j][0])
            dx, dy = c[0] - (h[i][0] + v[j][0]), c[1] - (h[i][1] + v[j][1])
            t = _Fr(dx * (-qq[j][1]) - dy * (-qq[j][0])) / den
            u = _Fr(dx * rr[i][1] - dy * rr[i][0]) / den
            if (t < 0 and i != 0) or (t > 1 and i != n_h - 1) or (u < 0 and j != 0) or (u > 1 and j != n_v - 1):
                continue
            o = 1 if -den >= 0 else -1
            k = (key(i, t), key(j, u), o)
            if k not in seen:
                seen.add(k)
                out[o] += 1
    # exactly on a cusp corner the two orientations are one point of degree W
    for hk, vk, o in seen:
        if hk[0] == "node" == vk[0] and 0 < hk[1] < n_h and 0 < vk[1] < n_v and (hk, vk, -o) in seen:
            w = _corner_degree(h, v, hk[1], vk[1])
            if w and o != w:
                out[o] -= 1
    return out


# the case that exposed the bug: H's first-segment family, k exactly at V's extreme node
sv_ext = ci.ChordIntersect([(5, 1), (3, 6), (9, 3)], [(-5, -1), (-11, -7), (-17, -3), (-15, -9), (-15, -5), (-18, -5), (-13, -11)],
                           (0.0, 0.0), 0.0, 0.0)
assert sv_ext.solve((-11.0, -9.0), extensions="all").count == 2 == sv_ext.solve_matrix((-11.0, -9.0)).count
rng = random.Random(29)
rigs_done = pts_done = nodes_done = 0
while rigs_done < 40:
    def _guide(n):
        p = [(rng.randint(-6, 6), rng.randint(-6, 6))]
        for _ in range(n):
            p.append((p[-1][0] + rng.randint(-6, 6), p[-1][1] + rng.randint(-6, 6)))
        return p
    H, V = _guide(rng.randint(2, 7)), _guide(rng.randint(2, 7))
    rs = [(b[0] - a[0], b[1] - a[1]) for a, b in zip(H, H[1:])]
    qs = [(b[0] - a[0], b[1] - a[1]) for a, b in zip(V, V[1:])]
    if any(a[0] * b[1] - a[1] * b[0] == 0 for a in rs for b in qs):
        continue                               # parallel cells are culled: the mesh has holes by design
    rigs_done += 1
    sv = ci.ChordIntersect(H, V, (0.0, 0.0), 0.0, 0.0)
    law = sum(hit.orient for hit in sv.solve((1234.5678, -987.6543), extensions="all").hits)
    xs = [a[0] + b[0] for a in H for b in V]
    ys = [a[1] + b[1] for a in H for b in V]
    for p in {(a[0] + b[0] + 0.0, a[1] + b[1] + 0.0) for a in H for b in V}:     # every node, cusps included
        want = _exact_roots(H, V, p)
        got = collections.Counter(hit.orient for hit in sv.solve_matrix(p).hits)
        assert got == want and sum(got.elements()) == law, (H, V, p, want, got, law)
        nodes_done += 1
    for x2 in range(2 * min(xs) - 4, 2 * max(xs) + 5, 3):
        for y2 in range(2 * min(ys) - 4, 2 * max(ys) + 5, 3):
            want = _exact_roots(H, V, (_Fr(x2, 2), _Fr(y2, 2)))
            p = (x2 / 2.0, y2 / 2.0)
            for res in (sv.solve(p, extensions="all"), sv.solve_matrix(p)):
                got = collections.Counter(hit.orient for hit in res.hits)
                assert got == want and sum(got.elements()) == law, (H, V, p, want, res.hits)
            pts_done += 1
print("exact degeneracies: %d integer rigs, %d points, %d nodes, hits == Fraction enumeration, orient sum == law" % (
    rigs_done, pts_done, nodes_done))

# a corner with one odd cell is a cusp only when its link winds (W = +-1): there the two rows collapse to the
# one of orientation W; at W = 0 a crease runs through the corner and the pair stays. A zero-length segment at
# the corner changes neither.
H_c = [(-4, 0), (0, -6), (-5, -4), (-10, -5), (-7, -11)]
V_c = [(-3, -6), (-8, -6), (-8, -11), (-11, -16), (-9, -16), (-15, -13), (-20, -16)]
sv_c = ci.ChordIntersect(H_c, V_c, (0.0, 0.0), 0.0, 0.0)
assert _corner_degree(H_c, V_c, 1, 5) == -1 and _corner_degree(H_c, V_c, 1, 4) == 0
rows_c = sv_c.solve((-15.0, -19.0), extensions="all").hits
assert sorted(h.orient for h in rows_c) == [-1, -1, 1], rows_c       # the cusp node: its +1 row is gone
assert sorted(h.orient for h in sv_c.solve((-9.0, -22.0), extensions="all").hits) == [-1, -1, 1]   # W = 0: pair kept
sv_z = ci.ChordIntersect(H_c[:2] + H_c[1:], V_c, (0.0, 0.0), 0.0, 0.0)
assert sorted(h.orient for h in sv_z.solve((-15.0, -19.0), extensions="all").hits) == [-1, -1, 1]

# ---------------------------------------------------------------- the lazy grid
# Bin lists are made on first use by walking the two box trees: nothing per cell
# up front, and on guides that make loose boxes (rotated, jittered, folded, with
# segment lengths far apart) a bin must still hold every cell the matrix accepts.
rng = random.Random(41)


def _rotated(points, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(x * c - y * s, x * s + y * c) for x, y in points]


rigs_lazy = []
for deg, fold, jitter in ((0.0, 30.0, 0.0), (45.0, 30.0, 0.0), (20.0, 200.0, 0.0), (33.0, 30.0, 1.5), (71.0, 200.0, 1.5)):
    H_l = [(-600.0 + 6.0 * k + rng.uniform(-jitter, jitter), fold * math.sin(k * 0.08) + rng.uniform(-jitter, jitter)) for k in range(201)]
    V_l = [(fold * math.sin(k * 0.07) + rng.uniform(-jitter, jitter), -450.0 + 6.0 * k + rng.uniform(-jitter, jitter)) for k in range(151)]
    rigs_lazy.append((_rotated(H_l, deg), _rotated(V_l, deg)))
rigs_lazy.append(([(-500.0, 0.0), (-499.0, 1.0), (-498.0, 0.0), (500.0, 40.0)], [(0.0, -500.0), (1.0, -499.0), (40.0, 500.0)]))
for H_l, V_l in rigs_lazy:
    sv_l = ci.ChordIntersect(H_l, V_l)
    sv_l.prepare_grid()
    assert not sv_l._cell_tuples and not sv_l._grid[5] and sv_l._families is None
    pts = [(rng.uniform(-900.0, 900.0), rng.uniform(-900.0, 900.0)) for _ in range(500)]
    for a, b in zip(sv_l.solve_many(pts, method="grid", extensions="all"), sv_l.solve_many(pts)):
        assert same_hits(a, b), (a.point, a.hits, b.hits)
    assert len(sv_l._grid[5]) <= len(pts)                  # a list only for a bin a point asked for
print("lazy grid: %d rigs, 500 points each, hits == matrix, cell tuples only for the bins touched" % len(rigs_lazy))

print("t_chord_intersect: ok")
