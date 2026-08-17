"""THE shared home of exact cubic-bezier math on plain (x, y) tuples.

This module is the Python mirror of algorithm/beziersplit.h - the one wheel
for de Casteljau splitting, evaluation, derivatives and degree elevation.
Do not re-derive any of these formulas in a tool module; import them from
here, and leave a short comment at the call site pointing back to this file.
(The C++ side must be used for QPainterPath-level work - parsing, arc
tables, locate, slice; this mirror exists because the tool modules work in
bare float tuples inside tight loops, where a per-call binding round-trip
would cost more than the math.)

Every function is EXACT: a sub-cubic returned here is the original curve
restricted to the requested range, never an approximation. The arithmetic
is kept expression-for-expression identical to the code it replaced (the
guide-arc tables in auto_mapping pin a sub-1e-3 px parameterization
contract, so evaluation and derivatives must not drift even in the last
bit).

Cubics are 4-sequences (p0, c1, c2, p3) of (x, y) tuples; functions accept
any indexable and return lists, which callers may mutate.
"""

import math


def lerp(a, b, t):
    """Linear interpolation between two points."""
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def eval_cubic(cub, t):
    """de Casteljau evaluation of a cubic Bezier (p0, c1, c2, p3) at t."""
    p0, c1, c2, p3 = cub
    a = lerp(p0, c1, t)
    b = lerp(c1, c2, t)
    c = lerp(c2, p3, t)
    d = lerp(a, b, t)
    e = lerp(b, c, t)
    return lerp(d, e, t)


def cubic_derivative(cub, t):
    """First derivative r'(t) of a cubic (the hodograph, Bernstein form)."""
    p0, c1, c2, p3 = cub
    mt = 1.0 - t
    dx = (3.0 * mt * mt * (c1[0] - p0[0]) + 6.0 * mt * t * (c2[0] - c1[0])
          + 3.0 * t * t * (p3[0] - c2[0]))
    dy = (3.0 * mt * mt * (c1[1] - p0[1]) + 6.0 * mt * t * (c2[1] - c1[1])
          + 3.0 * t * t * (p3[1] - c2[1]))
    return (dx, dy)


def split_cubic(cub, t=0.5):
    """One de Casteljau split: the same curve as (left, right) at t."""
    p0, c1, c2, p3 = cub
    a = lerp(p0, c1, t)
    b = lerp(c1, c2, t)
    c = lerp(c2, p3, t)
    d = lerp(a, b, t)
    e = lerp(b, c, t)
    f = lerp(d, e, t)
    return [p0, a, d, f], [f, e, c, p3]


def cubic_segment(cub, t0, t1):
    """Sub-cubic covering the parameter range [t0, t1] of `cub`.

    Split order (left at t1 first, then right at t0/t1, with the 1e-12
    denominator floor) is kept from the implementation this replaced, so
    long-standing results do not shift by even a bit.
    """
    if t1 < 1.0:
        cub = split_cubic(cub, t1)[0]
    if t0 > 0.0:
        denom = t1 if t1 > 1e-12 else 1e-12
        cub = split_cubic(cub, t0 / denom)[1]
    return list(cub)


def line_cubic(p0, p3):
    """A straight segment as a cubic, so all output shares one type."""
    return [p0, lerp(p0, p3, 1.0 / 3.0), lerp(p0, p3, 2.0 / 3.0), p3]


def quad_cubic(p0, ctrl, p3):
    """Exact quadratic-to-cubic degree elevation."""
    c1 = (p0[0] + 2.0 / 3.0 * (ctrl[0] - p0[0]), p0[1] + 2.0 / 3.0 * (ctrl[1] - p0[1]))
    c2 = (p3[0] + 2.0 / 3.0 * (ctrl[0] - p3[0]), p3[1] + 2.0 / 3.0 * (ctrl[1] - p3[1]))
    return [p0, c1, c2, p3]


def hull_length(cub):
    """Control-hull length |p0 c1| + |c1 c2| + |c2 p3|: the standard upper
    bound on a cubic's arc length, driving flattening density. The step
    count a caller derives from it (floor, cap, rounding) is that caller's
    policy and deliberately stays with the caller."""
    p0, c1, c2, p3 = cub
    return dist(p0, c1) + dist(c1, c2) + dist(c2, p3)
