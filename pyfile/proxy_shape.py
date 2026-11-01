"""Free closed proxy shapes: which point of the texture contour is which point
of the main contour, and the main axes that follow from it.

Pure functions, no scene and no UI. Ported from the prototype in
docs/plan/2026-10-31-自由图形代理/ (issue #9), whose numbers are a proposal until
the user signs them off.

The pairing: sample both contours by arc, take the best cyclic shift by shape
(rotation only breaks ties between shape minima, or a closed curve slides along
itself), then let matching corners pin it and keep arc proportion between them.
The main axes are the texture axes carried through a thin plate spline over the
paired contour; the prototype's cross-line reading is not ported yet.
"""
import math

import pydeps

STATIONS = 240
MIN_AREA = 400.0
SYMMETRY_GAP = 0.02         # shape costs this close are a symmetry tie
CORNER_DEG = 45.0
CORNER_WINDOW = 0.05        # fraction of the contour: a hand-drawn corner is round
THIN_EVERY = 3              # contour stations per thin plate centre
AXIS_STEP = 5.0


def _numpy():
    return pydeps.ensure("numpy")


def _sample(np, points, count=STATIONS):
    q = np.vstack([points, points[:1]])
    cum = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(q, axis=0), axis=1))])
    t = np.arange(count) / count * cum[-1]
    return np.stack([np.interp(t, cum, q[:, 0]), np.interp(t, cum, q[:, 1])], axis=1)


def _area(np, p):
    return 0.5 * float(np.sum(p[:, 0] * np.roll(p[:, 1], -1) - np.roll(p[:, 0], -1) * p[:, 1]))


def _crosses_itself(np, p):
    a, b = p, np.roll(p, -1, axis=0)
    d = b - a
    ex = (a[:, 0, None] - a[:, 0]) * d[:, 1] - (a[:, 1, None] - a[:, 1]) * d[:, 0]
    fx = (b[:, 0, None] - a[:, 0]) * d[:, 1] - (b[:, 1, None] - a[:, 1]) * d[:, 0]
    apart = ex * fx < 0
    n = len(p)
    i, j = np.indices((n, n))
    gap = (i - j) % n
    return bool(np.any(apart & apart.T & (gap > 1) & (gap < n - 1)))


def _normalised(np, p):
    z = p[:, 0] + 1j * p[:, 1]
    d = z - z.mean()
    return d / np.sqrt(np.sum(np.abs(d) ** 2))


def _best_shift(np, a, b):
    n = len(a)
    spread = max(2, n // 30)
    z = np.fft.ifft(np.conj(np.fft.fft(a)) * np.fft.fft(b))
    shape = 1.0 - np.abs(z) ** 2
    rotation = 1.0 - np.cos(np.angle(z))
    minima = []
    for k in range(n):
        if all(shape[k] <= shape[(k + d) % n] for d in range(-spread, spread + 1) if d):
            if not minima or k - minima[-1] > spread:
                minima.append(k)
    best = min(shape[k] for k in minima)
    return min((k for k in minima if shape[k] <= best + SYMMETRY_GAP), key=lambda k: rotation[k])


def _corners(np, p):
    n = len(p)
    w = max(2, int(round(CORNER_WINDOW * n)))
    forward = np.roll(p, -w, axis=0) - p
    back = p - np.roll(p, w, axis=0)
    turn = np.abs(np.arctan2(back[:, 0] * forward[:, 1] - back[:, 1] * forward[:, 0],
                             np.sum(back * forward, axis=1)))
    out = []
    for i in range(n):
        if turn[i] >= math.radians(CORNER_DEG) and all(
                turn[i] >= turn[(i + d) % n] for d in range(-w, w + 1)):
            if not out or i - out[-1] > w:
                out.append(i)
    if len(out) > 1 and out[0] + n - out[-1] <= w:
        out.pop()
    return out


def _matched_corners(np, ca, cb, seed, n):
    """Corner pairs by arc along the seed: the heaviest set that keeps the
    order, so a corner one side missed costs one pair, not a shift."""
    along = np.unwrap(seed * 2 * np.pi / n) * n / (2 * np.pi)
    tol = 0.5 * n / max(len(ca), len(cb), 1)
    items = []
    for i in ca:
        for j in cb:
            gap = (j - seed[i] + n / 2) % n - n / 2
            if abs(gap) < tol:
                items.append((i, along[i] + gap, (i, j, 1.0 - abs(gap) / tol)))
    items.sort(key=lambda t: (t[0], t[1]))
    score, back = [], []
    for k, (i, u, pair) in enumerate(items):
        top, via = 0.0, -1
        for m in range(k):
            if items[m][0] < i and items[m][1] < u and score[m] > top:
                top, via = score[m], m
        score.append(top + pair[2])
        back.append(via)
    k = int(np.argmax(score)) if score else -1
    chain = []
    while k >= 0:
        chain.append(items[k][2])
        k = back[k]
    return chain[::-1]


def _between(np, anchors, n):
    out = np.zeros(n)
    for q in range(len(anchors)):
        (i0, j0), (i1, j1) = anchors[q][:2], anchors[(q + 1) % len(anchors)][:2]
        li, lj = (i1 - i0) % n or n, (j1 - j0) % n or n
        for s in range(li):
            out[(i0 + s) % n] = j0 + lj * s / li
    return out % n


def match(child_points, main_points):
    """Pair two closed contours: (result, "") or (None, reason).

    result["child"] and result["main"] are STATIONS points, equal index means
    the same point of the pattern, both in positive winding. The main contour
    may cross itself (the sheet folds there); the texture contour may not.
    """
    np = _numpy()
    if np is None:
        return None, "numpy is not available"
    child = np.array(child_points, float)[:, :2]
    main = np.array(main_points, float)[:, :2]
    if len(child) < 3 or len(main) < 3:
        return None, "a contour needs at least 3 points"
    a = _sample(np, child)
    b = _sample(np, main)
    if _crosses_itself(np, a):
        return None, "the texture contour crosses itself"
    folded = _crosses_itself(np, b)
    if abs(_area(np, a)) < MIN_AREA or (abs(_area(np, b)) < MIN_AREA and not folded):
        return None, "the contour is too small"
    if _area(np, a) < 0.0:
        a = a[::-1]
    if _area(np, b) < 0.0:
        b = b[::-1]
    n = len(a)
    shift = _best_shift(np, _normalised(np, a), _normalised(np, b))
    seed = ((np.arange(n) + shift) % n).astype(float)
    kept = _matched_corners(np, _corners(np, a), _corners(np, b), seed, n)
    p = _between(np, kept, n) if len(kept) >= 2 else seed
    j = np.floor(p).astype(int) % n
    f = (p - np.floor(p))[:, None]
    paired = b[j] * (1.0 - f) + b[(j + 1) % n] * f
    return {"child": a.tolist(), "main": paired.tolist(), "folded": folded,
            "corners": len(kept), "corner_stations": [int(pair[0]) for pair in kept]}, ""


def corner_points(points):
    """The corners of one closed contour, before it has a partner."""
    np = _numpy()
    if np is None or len(points) < 3:
        return []
    a = _sample(np, np.array(points, float)[:, :2])
    return [a[i].tolist() for i in _corners(np, a)]


def _inside(np, points, ring):
    a, b = ring, np.roll(ring, -1, axis=0)
    x, y = points[:, 0, None], points[:, 1, None]
    spans = (a[:, 1] > y) != (b[:, 1] > y)
    denom = np.where(b[:, 1] == a[:, 1], 1e-12, b[:, 1] - a[:, 1])
    meet = a[:, 0] + (y - a[:, 1]) * (b[:, 0] - a[:, 0]) / denom
    return (np.sum(spans & (x < meet), axis=1) % 2) == 1


def _thin_plate(np, a, b, points):
    c, m = a[::THIN_EVERY], b[::THIN_EVERY]
    n = len(c)

    def kernel(d2):
        return 0.5 * d2 * np.log(d2 + 1e-12)

    k = kernel(np.sum((c[:, None] - c[None]) ** 2, axis=2))
    p = np.c_[np.ones(n), c]
    system = np.block([[k, p], [p.T, np.zeros((3, 3))]])
    coef = np.linalg.solve(system, np.r_[m, np.zeros((3, 2))])
    return (kernel(np.sum((points[:, None] - c[None]) ** 2, axis=2)) @ coef[:n]
            + np.c_[np.ones(len(points)), points] @ coef[n:])


def _similarity(np, a, b):
    za, zb = a[:, 0] + 1j * a[:, 1], b[:, 0] + 1j * b[:, 1]
    ca, cb = za.mean(), zb.mean()
    w = np.sum(np.conj(za - ca) * (zb - cb)) / np.sum(np.abs(za - ca) ** 2)

    def carry(points):
        z = w * (points[:, 0] + 1j * points[:, 1] - ca) + cb
        return np.stack([z.real, z.imag], axis=1)

    return carry, abs(w)


def _stations(np, polyline):
    p = np.array(polyline, float)[:, :2]
    cum = np.concatenate([[0.0], np.cumsum(np.linalg.norm(np.diff(p, axis=0), axis=1))])
    t = np.linspace(0.0, cum[-1], max(2, int(cum[-1] / AXIS_STEP) + 1))
    return np.stack([np.interp(t, cum, p[:, 0]), np.interp(t, cum, p[:, 1])], axis=1)


def carried_axis(paired, axis):
    """A texture axis on the main board: read off the contour inside it, carried
    on along its end tangents outside; the best similarity if it never enters.
    Returns the main polyline, or None without numpy."""
    np = _numpy()
    if np is None:
        return None
    a, b = np.array(paired["child"]), np.array(paired["main"])
    stations = _stations(np, axis)
    carry, scale = _similarity(np, a, b)
    inside = np.flatnonzero(_inside(np, stations, a))
    if len(inside) < 2:
        return carry(stations).tolist()
    first, last = int(inside[0]), int(inside[-1])
    line = carry(stations)
    line[first:last + 1] = _thin_plate(np, a, b, stations[first:last + 1])
    head = line[first] - line[min(first + 3, last)]
    tail = line[last] - line[max(last - 3, first)]
    head /= max(np.linalg.norm(head), 1e-9)
    tail /= max(np.linalg.norm(tail), 1e-9)
    spacing = scale * float(np.linalg.norm(stations[1] - stations[0]))
    line[:first] = line[first] + head * (spacing * (first - np.arange(first)))[:, None]
    line[last + 1:] = line[last] + tail * (spacing * np.arange(1, len(line) - last))[:, None]
    return line.tolist()
