"""Segment-pair ("matrix") inverse of the HV frame map, with no seed and no
iteration.

The frame map is hv(l_h, l_v) = H(l_h) + V(l_v) - O (pyfile/auto_mapping.py,
_Frame.hv): walk l_h along guide H from the crossing O, walk l_v along V,
add the two displacements. Inverting it for a canvas point A means solving

    H(s) = (A + O) - V(t)

The right side is V point-reflected and shifted - another polyline, W.
So A's preimages are exactly the crossings of H with W. A crossing on H's
segment i and W's segment j is cell (i, j) of the chord grid, and the two
segment parameters are the arc lengths. Every (i, j) pair is tested on its
own - the n_h x n_v matrix of pairs - as one sign test per pair, which is
what makes the whole solve a handful of array operations (or one GPU thread
per pair). Where the frame folds the test returns EVERY preimage; the damped
Newton in auto_mapping returns the one branch continuous from its chord seed
(docs/auto_mapping/point_mapping_newton.md), so this module can also count.

Two ways to run the test. solve_many evaluates the whole matrix for a batch
of points (numpy). solve, the per-point path, first LOCATES: cell (i, j) is
the parallelogram H_i-segment + V_j-segment - O, a uniform grid of bins
over the sheet lists the few cells whose parallelogram touches each bin, and
the point tests only its bin's cells (prepare_grid; the straight extensions
past the guide ends get a 1-D index of their own). Same answer as the
matrix, a handful of pair tests per point.

_Frame.solve_full inverts an UNFOLDABLE frame with this module; a foldable
frame stays on the damped Newton, because the severing verdict there is defined
by the branch Newton reaches (docs/auto_mapping/topology_severing.md). Guides
with real curve segments are handled by their flattening: the crossing locates the segment pair, and _Frame._descend
seeded there gives the exact arcs on the curve (locate_then_refine).
"""
import bisect
import math

try:
    import numpy as _np
except Exception:  # the embedded python has numpy; the pure-python path covers the rest
    _np = None

_EPS = 1e-7           # guide ENDS only: the sheet / extension label (on_sheet) uses t, u in [-EPS, 1 + EPS]
_DEDUP = 1e-6         # legacy reference distance (px); _merge_duplicates uses exact combinatorial edge/node matching

# ---- exact cell membership ---------------------------------------------------------
# A cell's INTERIOR sides are no longer tested on the cell's own (t, u) with a
# band of EPS: each side is the line through the two ROUNDED nodes that bound
# it (h[a] + v[b] as floats), and the test is the EXACT sign of orient2d on
# those two nodes and c. The two cells beside a side ask the same question
# about the same three points, so they get the same answer: the cells are the
# closed quads of the rounded node mesh, which tile without gap or overlap
# (a point exactly on a side or node is in every cell around it). Guide-end
# sides stay open (the extension); the sheet / extension label still reads
# t, u against [-EPS, 1 + EPS] at the guide ends only.
#
# Cost: the old float test is kept as a PREFILTER with a proven margin m per
# cell (see _cell): |t_float - t| and the offset between the quad side and the
# parallelogram side are both below m, so t < -m rejects and t > m accepts the
# left side for certain, and only a point within m of a side (m ~ 1e-12 for
# these rigs) pays for the side test, itself Shewchuk's stage-A float filter
# with an exact rational fallback.
from fractions import Fraction as _Fr
_U = 2.0 ** -53
_CCW = (3.0 + 16.0 * _U) * _U         # orient2d stage-A error bound factor (Shewchuk 1997)
_KM = 64.0 * _U                       # prefilter margin factor
EXACT_CALLS = [0]


def _orient_exact(ax, ay, bx, by, cx, cy):
    EXACT_CALLS[0] += 1
    d = (_Fr(ax) - _Fr(cx)) * (_Fr(by) - _Fr(cy)) - (_Fr(ay) - _Fr(cy)) * (_Fr(bx) - _Fr(cx))
    return (d > 0) - (d < 0)


def _orient(ax, ay, bx, by, cx, cy):
    """Exact sign of orient2d(a, b, c) = (a - c) x (b - c): +1 when a, b, c turn
    counter-clockwise, 0 when collinear. The float value decides when it is
    farther from 0 than its proven bound (or both products are exactly 0);
    otherwise exact rational arithmetic."""
    dl = (ax - cx) * (by - cy)
    dr = (ay - cy) * (bx - cx)
    det = dl - dr
    bound = _CCW * (abs(dl) + abs(dr))
    if det > bound:
        return 1
    if -det > bound:
        return -1
    if bound == 0.0:
        return 0
    return _orient_exact(ax, ay, bx, by, cx, cy)


def _cross(ax, ay, bx, by):
    return ax * by - ay * bx


def cumulative_lengths(points):
    out = [0.0]
    for (x0, y0), (x1, y1) in zip(points[:-1], points[1:]):
        out.append(out[-1] + math.hypot(x1 - x0, y1 - y0))
    return out


def segment_intersection(p, p2, q, q2):
    """(t, u) with p + t*(p2-p) == q + u*(q2-q), or None when parallel."""
    rx, ry = p2[0] - p[0], p2[1] - p[1]
    sx, sy = q2[0] - q[0], q2[1] - q[1]
    den = rx * sy - ry * sx
    if abs(den) <= 1e-12 * (math.hypot(rx, ry) * math.hypot(sx, sy) + 1e-300):
        return None
    dx, dy = q[0] - p[0], q[1] - p[1]
    return (dx * sy - dy * sx) / den, (dx * ry - dy * rx) / den


def polyline_crossing(a, b):
    """Where polylines a and b cross: (point, arc_on_a, arc_on_b, i, j).

    The same rule as auto_mapping._polyline_intersection: the first crossing
    segment pair wins; when nothing crosses, the closest vertex pair's midpoint.
    """
    a_cum, b_cum = cumulative_lengths(a), cumulative_lengths(b)
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -_EPS <= t <= 1.0 + _EPS and -_EPS <= u <= 1.0 + _EPS:
                t, u = min(max(t, 0.0), 1.0), min(max(u, 0.0), 1.0)
                point = (a[i][0] + (a[i + 1][0] - a[i][0]) * t, a[i][1] + (a[i + 1][1] - a[i][1]) * t)
                return (point, a_cum[i] + (a_cum[i + 1] - a_cum[i]) * t,
                        b_cum[j] + (b_cum[j + 1] - b_cum[j]) * u, i, j)
    best = None
    for i, pa in enumerate(a):
        for j, pb in enumerate(b):
            d = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
            if best is None or d < best[0]:
                best = (d, i, j)
    _, i, j = best
    point = ((a[i][0] + b[j][0]) * 0.5, (a[i][1] + b[j][1]) * 0.5)
    return point, a_cum[i], b_cum[j], min(i, len(a) - 2), min(j, len(b) - 2)


class Hit:
    """One preimage: cell (i, j), segment parameters, arcs from the origin,
    the local orientation sign (+1 / -1) of that cell, and whether it lies on
    the drawn guides (both parameters inside their segments) or on the straight
    extension hv adds past a guide's ends."""
    __slots__ = ("i", "j", "t", "u", "l_h", "l_v", "orient", "on_sheet")

    def __init__(self, i, j, t, u, l_h, l_v, orient, on_sheet):
        self.i, self.j, self.t, self.u = i, j, t, u
        self.l_h, self.l_v, self.orient, self.on_sheet = l_h, l_v, orient, on_sheet

    def arcs(self):
        return self.l_h, self.l_v

    def __repr__(self):
        return "Hit(cell=(%d, %d), arcs=(%.6f, %.6f), orient=%+d%s)" % (
            self.i, self.j, self.l_h, self.l_v, self.orient, "" if self.on_sheet else ", extension")


class Result:
    """All preimages of one point. `principal` is the one the live mapper's
    verdict would keep - same orientation as the origin crossing, on the drawn
    guides rather than their extension when there is a choice - and, among
    several, the one nearest the chord-basis seed (Newton's own starting
    branch); None when no preimage has the right orientation, which is the
    severing verdict of docs/auto_mapping/topology_severing.md."""
    __slots__ = ("point", "_rows", "_hits", "_principal", "_index")

    # a row is (l_h, l_v, i, j, t, u, orient, on_sheet); Hit objects are built
    # only when asked for, which keeps the per-point cost at the pair test
    def __init__(self, point, rows, index):
        self.point, self._rows, self._hits, self._index = point, rows, None, index
        self._principal = None if index is None else Hit(*_hit_args(rows[index]))

    @property
    def hits(self):
        if self._hits is None:
            self._hits = [Hit(*_hit_args(r)) for r in self._rows]
            if self._index is not None:
                self._hits[self._index] = self._principal
        return self._hits

    @property
    def principal(self):
        return self._principal

    @property
    def count(self):
        return len(self._rows)

    @property
    def count_on_sheet(self):
        return sum(1 for r in self._rows if r[7])

    @property
    def arcs(self):
        return None if self._principal is None else self._principal.arcs()


def _merge_duplicates(rows):
    """One preimage reported by several cells -> one row (combinatorial pairing).

    Under exact cell membership on the rounded node mesh, two same-orientation
    cells can both accept the same canvas point if and only if the point lies on
    a shared interior edge or corner node (exact orient2d == 0). In _test, an
    interior side pins t or u to 0.0 / 1.0 if and only if orient2d == 0 (points
    strictly inside the cell keep 0.0 < t, u < 1.0). Thus two same-orient rows
    are duplicates of one root iff they are the same cell or meet across a shared
    interior edge/node where both sides have orient2d == 0 (including across a
    culled zero-length segment, where cumulative arc lengths match). Opposite-
    orient rows across a fold crease are two roots coming together and are both
    kept; two genuine same-orient roots in diagonally opposite cells of a cusp
    or double-crease wedge (c != corner node) have 0.0 < t, u < 1.0 and are
    never merged, eliminating the _DEDUP cusp blind spot regardless of proximity.
    Rows are sorted by (l_h, l_v) on return.
    """
    rows.sort()
    kept = []
    for row in rows:
        l_h, l_v, i, j, t, u, s, _ = row
        dup = False
        for prev in kept:
            if prev[6] != s:
                continue
            pl_h, pl_v, pi, pj, pt, pu, _, _ = prev
            same_h = (i == pi
                      or (pi < i and pt == 1.0 and t == 0.0 and (i - pi == 1 or l_h == pl_h))
                      or (i < pi and pt == 0.0 and t == 1.0 and (pi - i == 1 or l_h == pl_h)))
            if not same_h:
                continue
            same_v = (j == pj
                      or (pj < j and pu == 1.0 and u == 0.0 and (j - pj == 1 or l_v == pl_v))
                      or (j < pj and pu == 0.0 and u == 1.0 and (pj - j == 1 or l_v == pl_v)))
            if same_v:
                dup = True
                break
        if not dup:
            kept.append(row)
    return kept


def _hit_args(row):
    l_h, l_v, i, j, t, u, orient, on_sheet = row
    return i, j, t, u, l_h, l_v, orient, on_sheet


class ChordIntersect:
    def __init__(self, h_points, v_points, origin=None, h_arc=None, v_arc=None,
                 h_cum=None, v_cum=None, orient0=None):
        self.h = [(float(p[0]), float(p[1])) for p in h_points]
        self.v = [(float(p[0]), float(p[1])) for p in v_points]
        if len(self.h) < 2 or len(self.v) < 2:
            raise ValueError("each guide needs at least two points")
        # h_cum / v_cum: the arc position of each vertex when the points are the
        # flattening of a curve; the chord lengths otherwise
        self.h_cum = cumulative_lengths(self.h) if h_cum is None else list(h_cum)
        self.v_cum = cumulative_lengths(self.v) if v_cum is None else list(v_cum)
        if origin is None:
            origin, h_arc, v_arc, _, _ = polyline_crossing(self.h, self.v)
        self.origin = (float(origin[0]), float(origin[1]))
        self.h_arc, self.v_arc = float(h_arc), float(v_arc)
        self.n_h, self.n_v = len(self.h) - 1, len(self.v) - 1
        self._r = [(x1 - x0, y1 - y0) for (x0, y0), (x1, y1) in zip(self.h[:-1], self.h[1:])]
        self._q = [(x1 - x0, y1 - y0) for (x0, y0), (x1, y1) in zip(self.v[:-1], self.v[1:])]
        i0 = self._segment_of(self.h_cum, self.h_arc)
        j0 = self._segment_of(self.v_cum, self.v_arc)
        o = _cross(*self._r[i0], *self._q[j0])
        # orient0 given: the caller's handedness (windowed, so a jittered guide
        # cannot flip it at the crossing's own segment pair)
        self.orient0 = (1 if o >= 0.0 else -1) if orient0 is None else orient0
        # chord basis (build_mapper's seed), used only to pick among several preimages
        self._eh = ((self.h[-1][0] - self.h[0][0]) * 0.5, (self.h[-1][1] - self.h[0][1]) * 0.5)
        self._ev = ((self.v[-1][0] - self.v[0][0]) * 0.5, (self.v[-1][1] - self.v[0][1]) * 0.5)
        self._seed_det = _cross(*self._eh, *self._ev)
        self._np_ready = False
        self._grid = None

    @classmethod
    def from_frame(cls, frame):
        """From an auto_mapping._Frame: its flattening points with their true
        arc positions, origin, origin arcs and handedness, so (l_h, l_v) here
        mean what they mean in frame.hv (exactly on polyline guides, within
        the flattening on curves, which is what the refine step is for)."""
        return cls(frame.h, frame.v, frame.origin, frame.h_arc, frame.v_arc,
                   frame.gh.flat_arcs, frame.gv.flat_arcs, frame.hand())

    # ---------------------------------------------------------------- forward
    @staticmethod
    def _segment_of(cum, arc):
        return min(len(cum) - 2, max(0, bisect.bisect_right(cum, arc) - 1))

    @staticmethod
    def _point_at(points, cum, arc):
        """Position at an arc from the start; past either end the end segment
        continues straight, exactly as auto_mapping._point_at_arc does."""
        i = min(len(cum) - 2, max(0, bisect.bisect_right(cum, arc) - 1))
        span = cum[i + 1] - cum[i]
        a = (arc - cum[i]) / span if span > 0.0 else 0.0
        return (points[i][0] + (points[i + 1][0] - points[i][0]) * a,
                points[i][1] + (points[i + 1][1] - points[i][1]) * a)

    def hv(self, l_h, l_v):
        """H(l_h) + V(l_v) - O on the polylines, as _Frame.hv does it."""
        ph = self._point_at(self.h, self.h_cum, self.h_arc + l_h)
        pv = self._point_at(self.v, self.v_cum, self.v_arc + l_v)
        return ph[0] + pv[0] - self.origin[0], ph[1] + pv[1] - self.origin[1]

    def chord_seed(self, point):
        """build_mapper's chord-basis guess (auto_mapping.py :3037-3038), or None."""
        if abs(self._seed_det) < 1e-12:
            return None
        dx, dy = point[0] - self.origin[0], point[1] - self.origin[1]
        eh, ev = self._eh, self._ev
        return ((dx * ev[1] - dy * ev[0]) / self._seed_det * math.hypot(*eh),
                (eh[0] * dy - eh[1] * dx) / self._seed_det * math.hypot(*ev))

    # ---------------------------------------------------------------- inverse
    def solve(self, point, extensions="lazy"):
        """The preimages of one canvas point, by the cell grid: the point's
        bin names the handful of cells whose parallelogram can contain it, and
        only those get the segment-pair test (see prepare_grid).

        extensions="lazy" (default): the cells' parts on the drawn guides are
        tested first, and the straight extensions past the guide ends only
        when that found no same-orientation preimage. The principal is the
        one solve_matrix gives in every case (it prefers the sheet anyway);
        `hits` leaves out the extension crossings when the sheet answered.
        extensions="all": every crossing, identical to solve_matrix.
        """
        point = (float(point[0]), float(point[1]))
        return self._finish(point, self._pairs_grid(point, extensions))

    def solve_matrix(self, point):
        """Every preimage of one canvas point by the full segment-pair loop:
        the reference the grid is checked against."""
        point = (float(point[0]), float(point[1]))
        return self._finish(point, self._pairs_python(point))

    def solve_many(self, points, chunk=None, method="matrix", extensions="lazy"):
        """Every preimage of each point. method="matrix": the n_h x n_v pair
        matrix evaluated for a chunk of points at once (numpy; the pair loop
        without it). method="grid": the per-point grid of solve."""
        points = [(float(p[0]), float(p[1])) for p in points]
        if method == "grid":
            return [self._finish(p, self._pairs_grid(p, extensions)) for p in points]
        if _np is None or not points:
            return [self._finish(p, self._pairs_python(p)) for p in points]
        if not self._np_ready:
            self._prepare_numpy()
        if chunk is None:
            chunk = max(1, int(2_000_000 // max(1, self.n_h * self.n_v)))
        out = []
        for start in range(0, len(points), chunk):
            block = points[start:start + chunk]
            raw = self._pairs_numpy(block)
            out.extend(self._finish(p, raw[k]) for k, p in enumerate(block))
        return out

    def locate_then_refine(self, frame, point):
        """For a frame whose guides carry real curve segments: the crossing on
        the flattening locates the segment pair, frame._descend refines the
        arcs on the curve from there. Returns (l_h, l_v, residual) or None."""
        res = self.solve(point)
        if res.principal is None:
            return None
        l_h, l_v = res.principal.arcs()
        return frame._descend(point, l_h, l_v)

    # ---------------------------------------------------------------- the grid
    def prepare_grid(self, bin_size=None, pad=None):
        """Index the cells by position, so that one point tests only the
        cells that can contain it.

        Cell (i, j) is the point set (segment i of H) + (segment j of V) - O,
        the Minkowski sum of the two segments: a parallelogram with the node
        H_i + V_j - O at one corner, and A lies in it exactly when the pair
        test of the matrix says so. Two indexes:

        * the sheet: a uniform grid of square bins over the bounding box of
          all the parallelograms plus `pad` px around it; each bin lists every
          cell whose parallelogram touches it (by bounding box, a superset).
          A point inside the grid tests its bin's list - a handful of cells
          instead of n_h * n_v.
        * the extensions: past a guide's end hv continues straight, so the
          end cells are strips along the four end rays (cones at the corners,
          where both guides run out). Across such a ray the coordinate
          k = cross(ray, c - end) is linear in the OTHER guide's parameter, so
          each strip is an interval of k and a point's candidates are a 1-D
          lookup (_extension_candidates). A point outside the grid can only
          be on an extension; a point inside is checked there only when the
          sheet gave no same-orientation preimage (solve, extensions="lazy"),
          because the principal prefers the sheet anyway.

        bin_size defaults to the median segment length (about one cell per
        bin), pad to two bins. Returns the grid and keeps it; solve builds it
        on first use. Cost: one pass over the n_h * n_v cells, about 25 ms
        for 100 x 100 in pure Python, which is what _Frame already spends on
        the crossing.
        """
        h, v, r, q = self.h, self.v, self._r, self._q
        n_h, n_v = self.n_h, self.n_v
        lengths = sorted([math.hypot(*s) for s in r] + [math.hypot(*s) for s in q])
        med = lengths[len(lengths) // 2] or 1.0
        b = float(bin_size) if bin_size else med
        inv = 1.0 / b
        if pad is None:
            pad = 2.0 * b
        # the grid lives in the c = A + O coordinates the pair test uses, where
        # the parallelograms' corners are the nodes H_i + V_j
        hx = [p[0] for p in h]
        hy = [p[1] for p in h]
        vx = [p[0] for p in v]
        vy = [p[1] for p in v]
        x0, y0 = min(hx) + min(vx) - pad, min(hy) + min(vy) - pad
        nbx = max(1, int(math.ceil((max(hx) + max(vx) + pad - x0) * inv)))
        nby = max(1, int(math.ceil((max(hy) + max(vy) + pad - y0) * inv)))
        sheet = [[] for _ in range(nbx * nby)]
        t_lo, t_hi = self._bounds(n_h)
        u_lo, u_hi = self._bounds(n_v)
        last_i, last_j = n_h - 1, n_v - 1
        eps = 1e-6
        cells = {}
        for i, (rx, ry) in enumerate(r):
            rl = math.hypot(rx, ry)
            for j, (qx, qy) in enumerate(q):
                sx, sy = -qx, -qy
                den = rx * sy - ry * sx
                if abs(den) <= 1e-12 * (rl * math.hypot(sx, sy) + 1e-300):
                    continue
                nx, ny = h[i][0] + v[j][0], h[i][1] + v[j][1]
                cand = self._cell(i, j, False)
                cells[(i, j)] = cand
                # the part on the sheet: t, u in [0, 1] whatever the cell, so
                # an end cell gets a second tuple with the bounds pinned
                if not (0 < i < last_i and 0 < j < last_j):
                    cand = self._cell(i, j, True)
                xs = (nx, nx + rx, nx + qx, nx + rx + qx)
                ys = (ny, ny + ry, ny + qy, ny + ry + qy)
                cx0 = max(0, int((min(xs) - x0) * inv - eps))
                cx1 = min(nbx - 1, int((max(xs) - x0) * inv + eps))
                cy0 = max(0, int((min(ys) - y0) * inv - eps))
                cy1 = min(nby - 1, int((max(ys) - y0) * inv + eps))
                for by in range(cy0, cy1 + 1):
                    row = by * nbx
                    for bx in range(cx0, cx1 + 1):
                        sheet[row + bx].append(cand)
        families = []
        for i in sorted({0, last_i}):
            families.append(self._family(cells, h[i], r[i], v, [(i, j) for j in range(n_v)], u_lo, u_hi, False))
        for j in sorted({0, last_j}):
            inner = [(i, j) for i in range(1, last_i)]      # the corner cells are in the H families
            if inner:
                families.append(self._family(cells, v[j], q[j], h, inner, t_lo, t_hi, True))
        self._grid = (x0, y0, inv, nbx, nby, sheet)
        self._families = families
        return self._grid

    @staticmethod
    def _family(cells, end, ray, other, pairs, lo, hi, along_h):
        """The strip of cells along one guide end's ray, indexed by the
        coordinate k = cross(ray, c - end) across the ray: inside cell (i, j)
        k runs linearly between its values at the two ends of the OTHER
        guide's segment, to +-infinity on a corner cell, so a point's cells
        are a 1-D lookup. Returns (end, ray, kmin, 1/width, n, bins, below,
        above): bins over [kmin, kmax] of the other guide's vertices, and the
        cells reaching below / above that range."""
        ex, ey = end
        dx, dy = ray
        ks = [dx * p[1] - dy * p[0] for p in other]
        kmin, kmax = min(ks), max(ks)
        steps = sorted(abs(k1 - k0) for k0, k1 in zip(ks[:-1], ks[1:]))
        width = steps[len(steps) // 2] or 1.0
        n = max(1, int(math.ceil((kmax - kmin) / width))) if kmax > kmin else 1
        winv = n / (kmax - kmin) if kmax > kmin else 0.0
        bins = [[] for _ in range(n)]
        below, above = [], []
        for pair in pairs:
            cand = cells.get(pair)
            if cand is None:
                continue
            m = pair[0] if along_h else pair[1]          # index along `other`
            k0, k1 = ks[m], ks[m + 1]
            ends = []
            for w in (lo[m], hi[m]):
                if w == -math.inf:
                    ends.append(-math.inf if k1 > k0 else math.inf)
                elif w == math.inf:
                    ends.append(math.inf if k1 > k0 else -math.inf)
                else:
                    ends.append((1.0 - w) * k0 + w * k1)
            klo, khi = min(ends), max(ends)
            if klo == -math.inf:
                below.append(cand)
            if khi == math.inf:
                above.append(cand)
            c0 = max(0, int((max(klo, kmin) - kmin) * winv - 1e-7))
            c1 = min(n - 1, int((min(khi, kmax) - kmin) * winv + 1e-7))
            for c in range(c0, c1 + 1):
                bins[c].append(cand)
        # a point's k can equal kmin / kmax (a corner of the strip family) or
        # round past them, so the lookup must not skip the end bins there
        tol = 1e-12 * (abs(kmin) + abs(kmax) + (kmax - kmin))
        return (ex, ey, dx, dy, kmin, kmax, tol, winv, n, bins, below, above)

    def _extension_candidates(self, cx, cy):
        out = []
        for ex, ey, dx, dy, kmin, kmax, tol, winv, n, bins, below, above in self._families:
            k = dx * (cy - ey) - dy * (cx - ex)
            if k < kmin:
                out.extend(below)
                if k >= kmin - tol:
                    out.extend(bins[0])
            else:
                idx = int((k - kmin) * winv)
                if idx < n:
                    out.extend(bins[idx])
                else:
                    out.extend(above)
                    if k <= kmax + tol:
                        out.extend(bins[n - 1])
        return out

    def _pairs_grid(self, point, extensions="lazy"):
        g = self._grid
        if g is None:
            g = self.prepare_grid()
        x0, y0, inv, nbx, nby, sheet = g
        cx, cy = point[0] + self.origin[0], point[1] + self.origin[1]
        fx, fy = (cx - x0) * inv, (cy - y0) * inv
        raw = []
        if 0.0 <= fx < nbx and 0.0 <= fy < nby:
            if self._test(cx, cy, sheet[int(fy) * nbx + int(fx)], raw) and extensions == "lazy":
                return raw
        self._test(cx, cy, self._extension_candidates(cx, cy), raw)
        return raw

    def _test(self, cx, cy, cands, raw):
        """The cell test on each candidate, appending the hits to raw; True
        when one of them has the origin's orientation. A hit is a point in the
        closed quad of the rounded nodes (interior sides exact, see the module
        header), within the guide-end bounds t_lo..u_hi of the tuple. Every
        path (grid, matrix, numpy) ends in this function, so they agree to
        the last bit; (t, u) are the float values of the old pair test."""
        orient0 = self.orient0
        same = False
        for i, j, x00, y00, sx, sy, rx, ry, den, tmin, tmax, umin, umax, mb, ma, s, side in cands:
            dx, dy = cx - x00, cy - y00
            t = (dx * sy - dy * sx) / den
            if t < tmin or t > tmax:          # guide-end bounds and, for a bounded tuple, the margin
                continue
            u = (dx * ry - dy * rx) / den
            if u < umin or u > umax:
                continue
            m = ma
            if mb:
                # unbounded (extension) tuple: the margin grows with |c - N00|
                x10, y10, x01, y01, x11, y11, hasL, hasR, hasB, hasT = side
                m = ma + mb * (abs(dx) + abs(dy))
                if (hasL and t < -m) or (hasR and t > 1.0 + m) or (hasB and u < -m) or (hasT and u > 1.0 + m):
                    continue
            if not (m < t < 1.0 - m and m < u < 1.0 - m):
                # within m of a side: the exact side-line test decides, and pins
                # t / u to 0.0 or 1.0 on an interior side iff _orient == 0
                x10, y10, x01, y01, x11, y11, hasL, hasR, hasB, hasT = side
                if hasL and t <= m:
                    o = s * _orient(x00, y00, x01, y01, cx, cy)
                    if o > 0:
                        continue
                    t = 0.0 if o == 0 else (t if t > 0.0 else 1e-300)
                if hasR and t >= 1.0 - m:
                    o = s * _orient(x10, y10, x11, y11, cx, cy)
                    if o < 0:
                        continue
                    t = 1.0 if o == 0 else (t if t < 1.0 else 1.0 - _U)
                if hasB and u <= m:
                    o = s * _orient(x00, y00, x10, y10, cx, cy)
                    if o < 0:
                        continue
                    u = 0.0 if o == 0 else (u if u > 0.0 else 1e-300)
                if hasT and u >= 1.0 - m:
                    o = s * _orient(x01, y01, x11, y11, cx, cy)
                    if o > 0:
                        continue
                    u = 1.0 if o == 0 else (u if u < 1.0 else 1.0 - _U)
            raw.append((i, j, t, u))
            if s == orient0:
                same = True
        return same

    def _node_max(self):
        nm = getattr(self, "_nmax", None)
        if nm is None:
            nm = self._nmax = (max(max(abs(p[0]), abs(p[1])) for p in self.h)
                               + max(max(abs(p[0]), abs(p[1])) for p in self.v))
        return nm

    def _cell(self, i, j, sheet):
        """The cell tuple _test reads, or None for a degenerate (parallel)
        pair. Nodes Nkl = h[i + k] + v[j + l] (the same float sums for every
        cell that shares them). sheet=True: the grid's sheet tuple, whose
        guide-end sides are pinned to [-EPS, 1 + EPS] in t / u (what on_sheet
        means). Prefilter margin m = ma + mb * (|dx| + |dy|) in parameter
        units; mb = 0 for a bounded tuple (interior cell, or a sheet tuple),
        whose points lie within |r| + |q| of N00."""
        h, v = self.h, self.v
        rx, ry = self._r[i]
        qx, qy = self._q[j]
        sx, sy = -qx, -qy
        den = rx * sy - ry * sx
        rl, ql = math.hypot(rx, ry), math.hypot(qx, qy)
        if abs(den) <= 1e-12 * (rl * ql + 1e-300):
            return None
        last_i, last_j = self.n_h - 1, self.n_v - 1
        s = 1 if rx * qy - ry * qx >= 0.0 else -1
        inf = math.inf
        t_lo = -_EPS if (sheet and i == 0) else -inf
        t_hi = 1.0 + _EPS if (sheet and i == last_i) else inf
        u_lo = -_EPS if (sheet and j == 0) else -inf
        u_hi = 1.0 + _EPS if (sheet and j == last_j) else inf
        span = rl + ql
        scale = self._node_max() + 2.0 * span
        ad = abs(den) * (1.0 - 1e-3)
        bounded = sheet or (0 < i < last_i and 0 < j < last_j)
        ma = _KM * scale * span * 2.0 / ad
        mb = 0.0 if bounded else _KM * scale / ad
        hasL, hasR, hasB, hasT = i > 0, i < last_i, j > 0, j < last_j
        tmin = (-ma if hasL else -inf) if mb == 0.0 else t_lo
        tmin = t_lo if not hasL else tmin
        tmax = (1.0 + ma if hasR else inf) if mb == 0.0 else t_hi
        tmax = t_hi if not hasR else tmax
        umin = (-ma if hasB else -inf) if mb == 0.0 else u_lo
        umin = u_lo if not hasB else umin
        umax = (1.0 + ma if hasT else inf) if mb == 0.0 else u_hi
        umax = u_hi if not hasT else umax
        return (i, j, h[i][0] + v[j][0], h[i][1] + v[j][1], sx, sy, rx, ry, den, tmin, tmax, umin, umax, mb, ma, s,
                (h[i + 1][0] + v[j][0], h[i + 1][1] + v[j][1], h[i][0] + v[j + 1][0], h[i][1] + v[j + 1][1],
                 h[i + 1][0] + v[j + 1][0], h[i + 1][1] + v[j + 1][1], hasL, hasR, hasB, hasT))

    # ---------------------------------------------------------------- the matrix
    def _bounds(self, n):
        """Parameter range per segment: [0, 1] inside, open-ended on the first
        and last segments because hv extends the guides straight past their
        ends (auto_mapping._point_at_arc)."""
        lo = [-_EPS] * n
        hi = [1.0 + _EPS] * n
        lo[0], hi[-1] = -math.inf, math.inf
        return lo, hi

    def _cells_all(self):
        cells = getattr(self, "_cell_list", None)
        if cells is None:
            cells = self._cell_list = [c for c in (self._cell(i, j, False)
                                                   for i in range(self.n_h) for j in range(self.n_v)) if c]
        return cells

    def _pairs_python(self, point):
        cx, cy = point[0] + self.origin[0], point[1] + self.origin[1]
        raw = []
        self._test(cx, cy, self._cells_all(), raw)
        return raw

    def _prepare_numpy(self):
        """Arrays for a vectorised PREFILTER that keeps a superset of the
        cells _test's own prefilter keeps (same float t, u; margins doubled),
        so running _test on the kept cells gives exactly the matrix answer.
        Interior cells (bounded) use the constant margin ma; the O(n) end
        cells carry the |dx| + |dy| term and are filtered in a small block."""
        np = _np
        n_h, n_v = self.n_h, self.n_v
        cells = self._cells_all()
        self._np_tup = {(c[0], c[1]): c for c in cells}
        inner = [c for c in cells if c[13] == 0.0]
        outer = [c for c in cells if c[13] != 0.0]

        def cols(group):
            A = np.array([[c[2], c[3], c[4], c[5], c[6], c[7], c[8], c[14], c[13]] for c in group] or
                         np.zeros((0, 9)), float).reshape(-1, 9)
            return {k: A[:, n] for n, k in enumerate(("x00", "y00", "sx", "sy", "rx", "ry", "den", "ma", "mb"))}

        ci_, co = cols(inner), cols(outer)
        ci_["lo"] = -2.0 * ci_["ma"]
        ci_["hi"] = 1.0 + 2.0 * ci_["ma"]
        self._np_inner, self._np_outer = ci_, co
        self._np_inner_ij = [(c[0], c[1]) for c in inner]
        self._np_outer_ij = [(c[0], c[1]) for c in outer]
        inf = np.inf
        self._np_outer_open = (np.array([inf if c[0] == 0 else 1.0 for c in outer]),
                               np.array([inf if c[0] == n_h - 1 else 1.0 for c in outer]),
                               np.array([inf if c[1] == 0 else 1.0 for c in outer]),
                               np.array([inf if c[1] == n_v - 1 else 1.0 for c in outer]))
        self._np_ready = True

    def _pairs_numpy(self, points):
        np = _np
        pts = [(p[0] + self.origin[0], p[1] + self.origin[1]) for p in points]
        c = np.asarray(pts, float)
        cx, cy = c[:, 0, None], c[:, 1, None]
        per = [[] for _ in points]
        C = self._np_inner
        if len(self._np_inner_ij):
            dx = cx - C["x00"][None]
            dy = cy - C["y00"][None]
            t = (dx * C["sy"][None] - dy * C["sx"][None]) / C["den"][None]
            u = (dx * C["ry"][None] - dy * C["rx"][None]) / C["den"][None]
            keep = (t >= C["lo"][None]) & (t <= C["hi"][None]) & (u >= C["lo"][None]) & (u <= C["hi"][None])
            ij = self._np_inner_ij
            for k, n in zip(*[a.tolist() for a in np.nonzero(keep)]):
                per[k].append(ij[n])
        C = self._np_outer
        if len(self._np_outer_ij):
            dx = cx - C["x00"][None]
            dy = cy - C["y00"][None]
            m2 = 2.0 * (C["ma"][None] + C["mb"][None] * (np.abs(dx) + np.abs(dy)))
            t = (dx * C["sy"][None] - dy * C["sx"][None]) / C["den"][None]
            u = (dx * C["ry"][None] - dy * C["rx"][None]) / C["den"][None]
            oL, oR, oB, oT = self._np_outer_open
            keep = (t >= -oL * m2) & (t <= 1.0 + oR * m2) & (u >= -oB * m2) & (u <= 1.0 + oT * m2)
            ij = self._np_outer_ij
            for k, n in zip(*[a.tolist() for a in np.nonzero(keep)]):
                per[k].append(ij[n])
        tup = self._np_tup
        raw = [[] for _ in points]
        for k, cand in enumerate(per):
            if cand:
                cand.sort()                                  # matrix order (i, j)
                self._test(pts[k][0], pts[k][1], [tup[x] for x in cand], raw[k])
        return raw

    # ---------------------------------------------------------------- bookkeeping
    def _corner_winding(self, a, b):
        """Local degree of the frame map at interior corner node (a, b): the
        winding number of the four neighbouring nodes around it, by exact
        orient2d crossings on the rounded mesh. +1 or -1 at a regular or cusp
        corner, 0 where a crease runs through the corner. None when the ring
        has no answer: a neighbour missing (only zero-length segments beyond)
        or two neighbours collinear with the node (a culled parallel cell)."""
        h, v = self.h, self.v

        def step(points, k, d):
            n = k + d
            while 0 <= n < len(points) and points[n] == points[k]:
                n += d
            return n if 0 <= n < len(points) else None

        east, west = step(h, a, 1), step(h, a, -1)
        north, south = step(v, b, 1), step(v, b, -1)
        if None in (east, west, north, south):
            return None
        ha, vb = h[a], v[b]
        cx, cy = ha[0] + vb[0], ha[1] + vb[1]
        ring = ((h[east][0] + vb[0], h[east][1] + vb[1]), (ha[0] + v[north][0], ha[1] + v[north][1]),
                (h[west][0] + vb[0], h[west][1] + vb[1]), (ha[0] + v[south][0], ha[1] + v[south][1]))
        winding = 0
        for k in range(4):
            ax, ay = ring[k]
            bx, by = ring[(k + 1) % 4]
            o = _orient(ax, ay, bx, by, cx, cy)
            if o == 0:
                return None
            if ay <= cy < by and o > 0:
                winding += 1
            elif by <= cy < ay and o < 0:
                winding -= 1
        return winding

    def _settle_corners(self, rows):
        """A point exactly on a corner node, reported with both orientations.
        Merging leaves one row per orientation, the pair a crease point has,
        and that pair is right where a crease runs through the corner (W == 0,
        degree 0). At a cusp (W == +-1) the three roots around the corner have
        collapsed into one point of degree W: only the row of that orientation
        stays, so the orient sum keeps the conservation law on this critical
        value too. Rows with t and u both pinned are the only ones that can
        be here, and they are rare, so this costs a scan of 0.0 / 1.0 tests."""
        last_i, last_j = self.n_h - 1, self.n_v - 1
        groups = None
        for row in rows:
            t, u = row[4], row[5]
            if (t == 0.0 or t == 1.0) and (u == 0.0 or u == 1.0):
                groups = groups or {}
                groups.setdefault((row[0], row[1]), []).append(row)
        if groups is None:
            return rows
        drop = set()
        for group in groups.values():
            if len({row[6] for row in group}) < 2:
                continue
            a = group[0][2] + (1 if group[0][4] == 1.0 else 0)
            b = group[0][3] + (1 if group[0][5] == 1.0 else 0)
            if not (0 < a <= last_i and 0 < b <= last_j):
                continue
            winding = self._corner_winding(a, b)
            if winding:
                keep = 1 if winding > 0 else -1
                drop.update(id(row) for row in group if row[6] != keep)
        return [row for row in rows if id(row) not in drop] if drop else rows

    def _finish(self, point, raw):
        h_cum, v_cum, r, q = self.h_cum, self.v_cum, self._r, self._q
        last_i, last_j = self.n_h - 1, self.n_v - 1
        rows = []
        for i, j, t, u in raw:
            # only the guide-end sides can leave the sheet; an interior side is
            # decided by the exact side-line test in _test, which already pins
            # t / u to 0.0 or 1.0 iff _orient == 0 and keeps strictly interior
            # hits inside (0.0, 1.0)
            on_sheet = ((i > 0 or t >= -_EPS) and (i < last_i or t <= 1.0 + _EPS)
                        and (j > 0 or u >= -_EPS) and (j < last_j or u <= 1.0 + _EPS))
            l_h = (h_cum[i + 1] if t == 1.0 else h_cum[i] + t * (h_cum[i + 1] - h_cum[i])) - self.h_arc
            l_v = (v_cum[j + 1] if u == 1.0 else v_cum[j] + u * (v_cum[j + 1] - v_cum[j])) - self.v_arc
            ri, qj = r[i], q[j]
            o = ri[0] * qj[1] - ri[1] * qj[0]
            rows.append((l_h, l_v, i, j, t, u, 1 if o >= 0.0 else -1, on_sheet))
        if len(rows) > 1:
            rows = self._settle_corners(_merge_duplicates(rows))
        orient0 = self.orient0
        same = [k for k, row in enumerate(rows) if row[6] == orient0]
        index = None
        if same:
            if len(same) > 1:
                sheet = [k for k in same if rows[k][7]]
                if sheet:
                    same = sheet
            if len(same) == 1:
                index = same[0]
            else:
                seed = self.chord_seed(point)
                if seed is None:
                    index = same[0]
                else:
                    s_h, s_v = seed
                    index = min(same, key=lambda k: (rows[k][0] - s_h) ** 2 + (rows[k][1] - s_v) ** 2)
        return Result(point, rows, index)
