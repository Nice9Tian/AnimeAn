"""Bounding-box trees over a polyline's segments, for the guide crossing.

A frame needs the first crossing of its two guides and, when they do not
cross, their closest vertex pair (pyfile/auto_mapping.py: _polylines_cross,
_polyline_intersection, _curve_crossing). Both used to be a double loop over
every segment or vertex pair, which is n x m work: nine million pairs for two
3000-point guides. Here each polyline's segment boxes form a binary tree, and
the two trees are walked together so that only pairs whose boxes can meet are
looked at.

The answers are the double loop's to the last bit. box_pairs hands out the
candidate pairs in the double loop's (i, j) order, so the first one that
crosses is the double loop's first; closest_vertices breaks ties the way the
double loop does. Callers keep their own crossing test and tolerance.

Nearly parallel pairs are the exception the boxes cannot see. The crossing
test divides by the cross product of the two segment directions, so when
they are within rounding of parallel the computed parameters are noise and
can land inside the tolerance while the segments lie far apart - the double
loop then reports a crossing that is not there. To give the same answer,
box_pairs never prunes a pair of tree nodes whose directions come within
_PARALLEL_SIN of parallel, whatever their boxes. Farther from parallel the
parameters' rounding error stays under 18 * 2^-53 / sin(angle) of the
segments' lengths (first order), far below the 1e-6 the boxes are grown by
once sin(angle) is above about 2e-9, so a pair whose boxes are apart cannot
pass the test there.

Split out of PR #18, which also builds a segment-pair inverse of the frame on
top of these trees; that solver is not part of this module.
"""
import heapq
import math

_TWO_PI = 2.0 * math.pi
# Below this sine of the angle between two segments, box_pairs keeps the pair
# whatever its boxes; 50 times the bound in the module docstring.
_PARALLEL_SIN = 1e-7
_ARC_MARGIN = 2.0 * math.asin(_PARALLEL_SIN) + 1e-12


def _box_tree(points):
    """Bounding boxes of a polyline's segments as an implicit binary tree. Node
    k has children 2k and 2k + 1, segment i is leaf m + i, and leaves past the
    last segment keep an inverted box that no overlap test passes. Returns
    (lo_x, hi_x, lo_y, hi_y, size, m); size is the box's half perimeter."""
    n = len(points) - 1
    m = 1
    while m < n:
        m *= 2
    inf = math.inf
    lo_x, hi_x = [inf] * (2 * m), [-inf] * (2 * m)
    lo_y, hi_y = [inf] * (2 * m), [-inf] * (2 * m)
    for i in range(n):
        (x0, y0), (x1, y1) = points[i], points[i + 1]
        k = m + i
        lo_x[k], hi_x[k] = (x0, x1) if x0 < x1 else (x1, x0)
        lo_y[k], hi_y[k] = (y0, y1) if y0 < y1 else (y1, y0)
    for k in range(m - 1, 0, -1):
        a, b = 2 * k, 2 * k + 1
        lo_x[k] = lo_x[a] if lo_x[a] < lo_x[b] else lo_x[b]
        hi_x[k] = hi_x[a] if hi_x[a] > hi_x[b] else hi_x[b]
        lo_y[k] = lo_y[a] if lo_y[a] < lo_y[b] else lo_y[b]
        hi_y[k] = hi_y[a] if hi_y[a] > hi_y[b] else hi_y[b]
    size = [hi_x[k] - lo_x[k] + hi_y[k] - lo_y[k] for k in range(2 * m)]
    return lo_x, hi_x, lo_y, hi_y, size, m


def _direction_arcs(points, m):
    """Every node's segment directions as one arc of DOUBLED angles, where a
    segment and its reverse are the same line: (start, length) lists over the
    _box_tree layout. Length -1 marks a node with no direction - the padding
    leaves, and zero-length segments, which the crossing test never accepts.
    A parent's arc covers both children's arcs, so it holds every direction
    below it."""
    start, length = [0.0] * (2 * m), [-1.0] * (2 * m)
    for i in range(len(points) - 1):
        (x0, y0), (x1, y1) = points[i], points[i + 1]
        if x1 != x0 or y1 != y0:
            start[m + i] = (2.0 * math.atan2(y1 - y0, x1 - x0)) % _TWO_PI
            length[m + i] = 0.0
    for k in range(m - 1, 0, -1):
        start[k], length[k] = _cover(start[2 * k], length[2 * k], start[2 * k + 1], length[2 * k + 1])
    return start, length


def _cover(s1, l1, s2, l2):
    """The shorter of the two arcs that start at one arc and reach round the
    other; the whole circle when that is all there is."""
    if l1 < 0.0:
        return s2, l2
    if l2 < 0.0:
        return s1, l1
    from1 = max(l1, (s2 - s1) % _TWO_PI + l2)
    from2 = max(l2, (s1 - s2) % _TWO_PI + l1)
    s, l = (s1, from1) if from1 <= from2 else (s2, from2)
    return (0.0, _TWO_PI) if l >= _TWO_PI else (s, l)


def _arcs_apart(s1, l1, s2, l2):
    """True when every direction of one arc is more than _PARALLEL_SIN from
    parallel to every direction of the other."""
    if l1 < 0.0 or l2 < 0.0:
        return True
    if l1 >= _TWO_PI or l2 >= _TWO_PI:
        return False
    ahead, behind = (s2 - s1) % _TWO_PI, (s1 - s2) % _TWO_PI
    if ahead <= l1 or behind <= l2:
        return False
    return min(ahead - l1, behind - l2) >= _ARC_MARGIN


def box_pairs(a, b, eps):
    """(i, j) of the segment pairs of polylines a and b that can cross, in the
    order of the double loop over i then j, so the first one that does is the
    double loop's. eps is the crossing test's own tolerance: a segment counts
    eps of its length past either end, and the boxes grow to match. A pair of
    nodes is dropped only when its boxes are apart AND no pair below it is
    nearly parallel (module docstring). Splitting a's tree all the way before
    b's is what keeps the order."""
    ax0, ax1, ay0, ay1, a_size, ma = _box_tree(a)
    bx0, bx1, by0, by1, b_size, mb = _box_tree(b)
    a_dir, a_turn = _direction_arcs(a, ma)
    b_dir, b_turn = _direction_arcs(b, mb)
    slack = 2.0 * eps * (max(a_size[ma:ma + len(a) - 1], default=0.0)
                         + max(b_size[mb:mb + len(b) - 1], default=0.0))
    stack = [(1, 1)]
    pop, push = stack.pop, stack.append
    while stack:
        p, q = pop()
        if ((ax0[p] > bx1[q] + slack or bx0[q] > ax1[p] + slack
                or ay0[p] > by1[q] + slack or by0[q] > ay1[p] + slack)
                and _arcs_apart(a_dir[p], a_turn[p], b_dir[q], b_turn[q])):
            continue
        if p < ma:
            push((2 * p + 1, q))
            push((2 * p, q))
        elif q < mb:
            push((p, 2 * q + 1))
            push((p, 2 * q))
        else:
            yield p - ma, q - mb


def closest_vertices(a, b):
    """(i, j) of the closest vertex pair of a and b, and among equals the first
    in the order of the double loop: best-first over the box trees, a pair of
    stretches waits on the gap between its boxes. The gap is shaded down a
    hair so that rounding can never end the search before a tie is seen.
    Fewer than two points on a side take the double loop itself, so even an
    empty polyline fails the way it always did."""
    if len(a) < 2 or len(b) < 2:
        best = None
        for i, p in enumerate(a):
            for j, q in enumerate(b):
                distance = math.hypot(p[0] - q[0], p[1] - q[1])
                if best is None or distance < best[0]:
                    best = (distance, i, j)
        _, i, j = best
        return i, j
    ax0, ax1, ay0, ay1, a_size, ma = _box_tree(a)
    bx0, bx1, by0, by1, b_size, mb = _box_tree(b)

    def gap(p, q):
        return math.hypot(max(ax0[p] - bx1[q], bx0[q] - ax1[p], 0.0),
                          max(ay0[p] - by1[q], by0[q] - ay1[p], 0.0)) * (1.0 - 1e-12)

    heap = [(gap(1, 1), 1, 1)]
    best = None
    while heap:
        lower, p, q = heapq.heappop(heap)
        if best is not None and lower > best[0]:
            break
        if p >= ma and q >= mb:
            for i in (p - ma, p - ma + 1):
                for j in (q - mb, q - mb + 1):
                    d = math.hypot(a[i][0] - b[j][0], a[i][1] - b[j][1])
                    if best is None or (d, i, j) < best:
                        best = (d, i, j)
            continue
        if p < ma and (q >= mb or a_size[p] >= b_size[q]):
            children = ((2 * p, q), (2 * p + 1, q))
        else:
            children = ((p, 2 * q), (p, 2 * q + 1))
        for u, w in children:
            if ax0[u] != math.inf and bx0[w] != math.inf:
                heapq.heappush(heap, (gap(u, w), u, w))
    return best[1], best[2]
