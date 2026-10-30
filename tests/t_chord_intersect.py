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
  * from_frame reads a _Frame's own origin, so arcs agree with frame.hv.
  * duplicates are merged by orientation pairing: near reports of the same
    orient are one root, of opposite orient two roots across a fold crease;
    on the folded 100 px rig the conservation law sum(orient) == w then holds
    next to every fold edge (distance-only merging broke it at all of them),
    at random probes, and fails - detectably - only in the cusp wedges.
"""
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
# _finish merges near-equal reports (a root on a shared edge is accepted by both
# cells) only when they have the same orient: two cells sharing an edge with the
# same orient map their union one-to-one, with opposite orients the edge is a
# fold crease and the two reports are the two roots on either side of it
# (docs/plan/2026-10-28-格哈希论文新颖性调查/2026-10-30-w_orient0奇偶校验命题与Lean证明.md §8).
D = ci._DEDUP


def row(l_h, l_v, orient):
    return (l_h, l_v, 0, 0, 0.5, 0.5, orient, True)


m = ci._merge_duplicates([row(0.0, 0.0, 1), row(0.0, 0.0, 1), row(0.0, 0.0, 1), row(0.0, 0.0, 1)])
assert len(m) == 1, m                                    # one root on a cell node, four reports
m = ci._merge_duplicates([row(0.0, 0.0, 1), row(0.0, 0.0, -1)])
assert sorted(r[6] for r in m) == [-1, 1], m              # a crease pair: both kept
# an opposite-orient report sorting BETWEEN two duplicates: comparing with the
# last kept row only would keep the second duplicate
m = ci._merge_duplicates([row(0.0, 0.0, 1), row(0.2 * D, 0.5 * D, -1), row(0.4 * D, 0.0, 1)])
assert sorted(r[6] for r in m) == [-1, 1], m
m = ci._merge_duplicates([row(0.0, 0.0, 1), row(2.0 * D, 0.0, 1)])
assert len(m) == 2, m                                    # farther than _DEDUP: two roots
m = ci._merge_duplicates([row(0.0, 0.0, 1), row(0.5 * D, 3.0 * D, 1)])
assert len(m) == 2, m                                    # close in l_h only: two roots
print("merge rule: node duplicates merged, crease pairs kept, an interleaved report does not hide a duplicate")


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


frame = am._Frame(sin_guide(100.0, "h"), sin_guide(100.0, "v"))
solver = ci.ChordIntersect.from_frame(frame)
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
# off the corner image: close in, the EPS band adds a fourth report and pairing
# merges the same-orient ones, so sum(orient) misses w - but by exactly one, which
# the conservation check sees; farther out the three roots are more than _DEDUP
# apart and the law holds. It never misses by two. Distance-only merging folds all
# reports into one: the law then often holds by accident with two roots lost,
# and sometimes misses by two.
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
probes_b = off_by_one = old_off_by_two = 0
for i, j, k, dirs in cusps:
    a, b = dirs[k], dirs[(k + 1) % 4]
    na, nb = math.hypot(*a), math.hypot(*b)
    bis = (a[0] / na + b[0] / nb, a[1] / na + b[1] / nb)
    nbis = math.hypot(*bis)
    C = solver.hv(solver.h_cum[i + 1] - solver.h_arc, solver.v_cum[j + 1] - solver.v_arc)
    for e in range(-96, -31):
        d = 10.0 ** (e / 8.0)
        p = (C[0] + d * bis[0] / nbis, C[1] + d * bis[1] / nbis)
        dev = sum(h.orient for h in solver.solve(p, extensions="all").hits) - w
        assert abs(dev) <= 1, (i, j, d, dev)
        if d >= 1e-5:
            assert dev == 0, (i, j, d, dev)          # outside the EPS band and _DEDUP: exact
        probes_b += 1
        off_by_one += dev != 0
        old_off_by_two += abs(sum(x[6] for x in distance_only(raw_rows(solver, p))) - w) == 2
assert off_by_one > 0 and old_off_by_two > 0, (off_by_one, old_off_by_two)
print("bow 100 px, %d cusp corners, %d probes along the odd sectors: sum(orient) misses w by exactly one at %d "
      "(all next to the corner image, all detectable), never by two; distance-only merging missed by two at %d"
      % (len(cusps), probes_b, off_by_one, old_off_by_two))

# (c) random probes on the sheet and around it: the law holds on the merged hits
rng = random.Random(23)
for _ in range(2000):
    p = (rng.uniform(-450.0, 450.0), rng.uniform(-450.0, 450.0))
    res = solver.solve(p, extensions="all")
    assert sum(h.orient for h in res.hits) == w, (p, res.hits)
print("bow 100 px, 2000 random probes: sum(orient) == w on every one")

print("t_chord_intersect: ok")
