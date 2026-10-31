"""Proxy rectangles: which corner of the texture rectangle is which corner of
the main rectangle, and the main H/V axes and edge pairs that follow from it.

Spec: docs/auto_mapping/proxy_rect.md (tier 1, the rotation weight included).

The boards share no coordinates that mean anything to the pattern, so both
quads are centred and scaled to unit size before they are compared:
translation and scale cost nothing by construction. Of the four cyclic
labellings the one with the lowest cost wins:

    Z_k  = sum conj(a_i) b_(i+k)          a, b normalised, |Z_k| <= 1
    E_k  = (1 - |Z_k|^2) + ROTATION_WEIGHT * (1 - cos(arg Z_k))

The first term is the relative residual of the best similarity for that
labelling, the second what it costs to rotate the pattern that far. The shape
term alone cannot tell a rectangle from itself turned 180 degrees, so that
choice belongs to the rotation term.

No mirror is ever computed (user 2026-10-30): the pattern shows its back only
by folding. The winding a rectangle happens to be drawn in carries no intent,
so both quads are put in the same winding first.

The labelling is the only thing the surface needs: the mapper reads the axes,
and the main axes come from the labelled corners. rotation / scale / offset are
reported for diagnostics. Nothing is stored; callers recompute on every edit,
so a main rectangle turned past the tipping angle changes labelling (user
2026-10-30).
"""
import cmath
import math

ROTATION_WEIGHT = 1.0
_DEGENERATE_AREA = 1e-6     # |area| / perimeter^2 below this is not a quad
_TINY_CORRELATION = 1e-9    # below this arg Z is noise
_COST_TIE = 1e-9            # exact ties must not depend on the drawing order
_SKEW_TOLERANCE = 1e-6      # corner mismatch / edge lengths of a true rectangle
_AXIS_SAMPLES = 32          # samples per texture axis segment on the main quad


def _signed_area(quad):
    return 0.5 * sum(quad[i].real * quad[(i + 1) % 4].imag
                     - quad[(i + 1) % 4].real * quad[i].imag for i in range(4))


def _cross(a, b):
    return a.real * b.imag - a.imag * b.real


def _segments_cross(a1, a2, b1, b2):
    return (_cross(a2 - a1, b1 - a1) * _cross(a2 - a1, b2 - a1) < 0.0
            and _cross(b2 - b1, a1 - b1) * _cross(b2 - b1, a2 - b1) < 0.0)


def _unusable(quad):
    """Why this quad cannot be a proxy rectangle, or "" when it can."""
    if not all(cmath.isfinite(z) for z in quad):
        return "is degenerate"
    if any(quad[i] == quad[(i + 1) % 4] for i in range(4)):
        return "is degenerate"
    if (_segments_cross(quad[0], quad[1], quad[2], quad[3])
            or _segments_cross(quad[1], quad[2], quad[3], quad[0])):
        return "crosses itself"
    perimeter = sum(abs(quad[(i + 1) % 4] - quad[i]) for i in range(4))
    if abs(_signed_area(quad)) <= _DEGENERATE_AREA * perimeter * perimeter:
        return "is degenerate"
    return ""


def _reading_order(quad):
    """Positive winding, starting at the corner whose outgoing edge points
    most along +x. Both boards go through this, so the drawing order is gone
    before any arithmetic: the same quad gives the same floats however it
    was drawn, ties included. Edges that agree on direction (collinear ones)
    fall to the corner's position."""
    order = [0, 1, 2, 3] if _signed_area(quad) > 0.0 else [3, 2, 1, 0]

    def along_x(start):
        corner = quad[order[start]]
        edge = quad[order[(start + 1) % 4]] - corner
        return (round(edge.real / abs(edge), 9), round(edge.imag / abs(edge), 9),
                corner.real, corner.imag)

    first = max(range(4), key=along_x)
    return [order[(first + i) % 4] for i in range(4)]


def _normalised(quad):
    centre = sum(quad) / 4.0
    size = math.sqrt(sum(abs(z - centre) ** 2 for z in quad))
    return [(z - centre) / size for z in quad], centre, size


def align(child_corners, main_corners, rotation_weight=ROTATION_WEIGHT):
    """Label the two rectangles' corners: (result, "") or (None, reason).

    child_corners[result["child_order"][i]] corresponds to
    main_corners[result["main_order"][i]]. The winding and starting corner of
    either input change nothing.
    """
    if len(child_corners) != 4 or len(main_corners) != 4:
        return None, "a proxy rectangle needs exactly 4 corners"
    child = [complex(p[0], p[1]) for p in child_corners]
    main = [complex(p[0], p[1]) for p in main_corners]
    for name, quad in (("child", child), ("main", main)):
        reason = _unusable(quad)
        if reason:
            return None, f"the {name} rectangle {reason}"

    child_order = _reading_order(child)
    main_order = _reading_order(main)
    a, child_centre, child_size = _normalised([child[i] for i in child_order])
    b, main_centre, main_size = _normalised([main[i] for i in main_order])

    candidates = []
    for shift in range(4):
        z = sum(a[i].conjugate() * b[(i + shift) % 4] for i in range(4))
        angle = cmath.phase(z) if abs(z) > _TINY_CORRELATION else math.pi
        residual = 1.0 - abs(z) ** 2
        cost = residual + rotation_weight * (1.0 - math.cos(angle))
        candidates.append((cost, angle, shift, abs(z), residual))
    lowest = min(c[0] for c in candidates)
    # Exact ties (a square turned 45 degrees) fall to the positive angle.
    cost, angle, shift, correlation, residual = max(
        (c for c in candidates if c[0] <= lowest + _COST_TIE), key=lambda c: c[1])

    return {
        "child_order": child_order,
        "main_order": [main_order[(i + shift) % 4] for i in range(4)],
        "rotation": angle,
        "scale": correlation * main_size / child_size,
        "offset": (main_centre.real - child_centre.real,
                   main_centre.imag - child_centre.imag),
        "residual": residual,
        "cost": cost,
    }, ""


def _carried_axis(axis, c, m):
    """A texture axis carried onto the main quad: each point keeps its place
    relative to the texture rectangle c and is put back bilinearly into m. A
    line parallel to the rectangle's edges stays straight and keeps its
    proportions; any other bends where m is not a parallelogram, so every
    segment is sampled. Outside the rectangle the same formula extrapolates."""
    e = complex(*c[1]) - complex(*c[0])
    f = complex(*c[3]) - complex(*c[0])
    q = [complex(*p) for p in m]
    area = _cross(e, f)
    out = []
    for a, b in zip(axis, axis[1:]):
        for k in range(_AXIS_SAMPLES):
            p = complex(*a) + (complex(*b) - complex(*a)) * (k / _AXIS_SAMPLES) - complex(*c[0])
            out.append(_bilinear(q, _cross(p, f) / area, _cross(e, p) / area))
    p = complex(*axis[-1]) - complex(*c[0])
    out.append(_bilinear(q, _cross(p, f) / area, _cross(e, p) / area))
    return out


def _bilinear(q, u, v):
    z = ((1.0 - u) * (1.0 - v) * q[0] + u * (1.0 - v) * q[1]
         + u * v * q[2] + (1.0 - u) * v * q[3])
    return (z.real, z.imag)


def _edges(q):
    """The four edges by family: the H side runs q0 -> q1 and q3 -> q2, the V
    side q0 -> q3 and q1 -> q2, so equal indices on two boards start and end
    on corresponding corners."""
    return {"h": [[q[0], q[1]], [q[3], q[2]]],
            "v": [[q[0], q[3]], [q[1], q[2]]]}


def guides(child_corners, main_corners, child_h, child_v, rotation_weight=ROTATION_WEIGHT):
    """The main axes and the edge pairs: (result, "") or (None, reason).

    child_h and child_v are the texture board's own axes. The rectangle there
    only marks what maps, so moving it never moves them (user 2026-10-30);
    only the main axes are computed. result["child"] and result["main"] each
    hold "h" and "v" (axis point lists, the guide specs build_mapper takes)
    and "edges" (the same per family, each edge the partner of the same index
    on the other board).
    """
    alignment, reason = align(child_corners, main_corners, rotation_weight)
    if alignment is None:
        return None, reason
    c = [(float(child_corners[i][0]), float(child_corners[i][1]))
         for i in alignment["child_order"]]
    m = [(float(main_corners[i][0]), float(main_corners[i][1]))
         for i in alignment["main_order"]]
    corners = [complex(*p) for p in c]
    twist = abs(corners[0] - corners[1] + corners[2] - corners[3])
    if twist > _SKEW_TOLERANCE * (abs(corners[1] - corners[0]) + abs(corners[3] - corners[0])):
        return None, "the child rectangle is not a parallelogram"
    h = [(float(p[0]), float(p[1])) for p in child_h]
    v = [(float(p[0]), float(p[1])) for p in child_v]
    return {
        "alignment": alignment,
        "child": {"h": h, "v": v, "edges": _edges(c)},
        "main": {"h": _carried_axis(h, c, m), "v": _carried_axis(v, c, m), "edges": _edges(m)},
    }, ""
