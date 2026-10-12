"""Recompute the numbers the paper quotes, from the code in this repository.

    py paper/verify/verify_claims.py            # prints and writes verify_results.json

Nothing here is copied from docs/auto_mapping/*.md: each value is measured on
pyfile/auto_mapping.py as it stands (headless: the C++ binding is stubbed the
way tests/t_*.py do it). The paper cites only what this script prints.
"""
import json
import math
import os
import platform
import random
import subprocess
import sys
import time
import types

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am  # noqa: E402

OUT = {}


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


# ---- an arc-length evaluator written here, independent of the repository ----
def cum(points):
    c = [0.0]
    for a, b in zip(points, points[1:]):
        c.append(c[-1] + dist(a, b))
    return c


def at_arc(points, c, s):
    """Point at arc length s; linear extension past either end."""
    if s <= 0.0:
        d = dist(points[0], points[1])
        u = ((points[1][0] - points[0][0]) / d, (points[1][1] - points[0][1]) / d)
        return (points[0][0] + s * u[0], points[0][1] + s * u[1])
    if s >= c[-1]:
        d = dist(points[-2], points[-1])
        u = ((points[-1][0] - points[-2][0]) / d, (points[-1][1] - points[-2][1]) / d)
        e = s - c[-1]
        return (points[-1][0] + e * u[0], points[-1][1] + e * u[1])
    lo, hi = 0, len(c) - 1
    while hi - lo > 1:
        mid = (lo + hi) // 2
        if c[mid] <= s:
            lo = mid
        else:
            hi = mid
    f = (s - c[lo]) / (c[hi] - c[lo])
    a, b = points[lo], points[hi]
    return (a[0] + f * (b[0] - a[0]), a[1] + f * (b[1] - a[1]))


def crossing(h, v):
    """Intersection of two polylines: point, arc on h, arc on v (first hit)."""
    ch, cv = cum(h), cum(v)
    for i in range(len(h) - 1):
        for j in range(len(v) - 1):
            p, r = h[i], (h[i + 1][0] - h[i][0], h[i + 1][1] - h[i][1])
            q, s = v[j], (v[j + 1][0] - v[j][0], v[j + 1][1] - v[j][1])
            den = r[0] * s[1] - r[1] * s[0]
            if abs(den) < 1e-12:
                continue
            t = ((q[0] - p[0]) * s[1] - (q[1] - p[1]) * s[0]) / den
            u = ((q[0] - p[0]) * r[1] - (q[1] - p[1]) * r[0]) / den
            if 0.0 <= t <= 1.0 and 0.0 <= u <= 1.0:
                o = (p[0] + t * r[0], p[1] + t * r[1])
                return o, ch[i] + t * (ch[i + 1] - ch[i]), cv[j] + u * (cv[j + 1] - cv[j])
    raise ValueError("axes do not cross")


def build(ch, cv, mh, mv, pairs=None):
    info = {}
    mp, ws = am.build_mapper(ch, cv, mh, mv, info, additional_pairs=pairs)
    assert mp is not None, ws
    return mp, ws, info


def sine(amp, period, x0=-300.0, x1=300.0, n=201):
    return [(x0 + (x1 - x0) * k / (n - 1),
             amp * math.sin((x1 - x0) * k / (n - 1) / period * 2.0 * math.pi))
            for k in range(n)]


def vsine(amp, period, y0=-200.0, y1=200.0, n=201):
    return [(amp * math.sin((y1 - y0) * k / (n - 1) / period * 2.0 * math.pi),
             y0 + (y1 - y0) * k / (n - 1)) for k in range(n)]


H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]
rng = random.Random(20261013)

# ---- 1. Coons degeneration (mathematical identity, independent evaluator) ----
mh, mv = sine(45.0, 260.0), vsine(15.0, 400.0)
cmh, cmv = cum(mh), cum(mv)
o, sx, tx = crossing(mh, mv)


def sweep(s, t):
    a, b = at_arc(mh, cmh, s), at_arc(mv, cmv, t)
    return (a[0] + b[0] - o[0], a[1] + b[1] - o[1])


s0, s1, t0, t1 = 20.0, cmh[-1] - 20.0, 15.0, cmv[-1] - 15.0
worst = 0.0
for _ in range(4000):
    s, t = rng.uniform(s0, s1), rng.uniform(t0, t1)
    u, v = (s - s0) / (s1 - s0), (t - t0) / (t1 - t0)
    cb, ct = sweep(s, t0), sweep(s, t1)
    cl, cr = sweep(s0, t), sweep(s1, t)
    p00, p10, p01, p11 = sweep(s0, t0), sweep(s1, t0), sweep(s0, t1), sweep(s1, t1)
    coons = tuple((1 - v) * cb[k] + v * ct[k] + (1 - u) * cl[k] + u * cr[k]
                  - ((1 - u) * (1 - v) * p00[k] + u * (1 - v) * p10[k]
                     + (1 - u) * v * p01[k] + u * v * p11[k]) for k in (0, 1))
    worst = max(worst, dist(coons, sweep(s, t)))
OUT["coons_vs_sweep_max_px"] = worst
print("1  Coons patch vs H(s)+V(t)-O, 4000 samples: max %.3e px" % worst)

# ---- 2. the implementation is that formula (straight child frame) ----
mp, ws, info = build(H, V, mh, mv)
ch_len, cv_len = 600.0, 400.0
worst = 0.0
for _ in range(5000):
    p = (rng.uniform(-290.0, 290.0), rng.uniform(-190.0, 190.0))
    lh, lv = p[0], p[1]                     # child arcs from the crossing (0, 0)
    sig_h = (sx / 300.0) if lh < 0 else ((cmh[-1] - sx) / 300.0)
    sig_v = (tx / 200.0) if lv < 0 else ((cmv[-1] - tx) / 200.0)
    worst = max(worst, dist(mp(p), sweep(sx + sig_h * lh, tx + sig_v * lv)))
OUT["implementation_vs_formula_max_px"] = worst
print("2  map_point vs the formula, 5000 samples: max %.3e px" % worst)

# ---- 3. identity: the same curved axes on both boards ----
ident = {}
for amp in (0.0, 10.0, 30.0, 60.0):
    ax = sine(amp, 300.0) if amp else H
    m3, _, _ = build(ax, V, ax, V)
    w = 0.0
    for gx in range(-10, 11):
        for gy in range(-10, 11):
            p = (gx * 26.0, gy * 16.0)
            w = max(w, dist(m3(p), p))
    ident[str(int(amp))] = w
    print("3  identity, sine amplitude %3d: max |Phi(p)-p| = %.3e px (fold gate %s)"
          % (amp, w, m3.can_fold()))
OUT["identity_max_px_by_amplitude"] = ident

# ---- 4. affine case: four straight axes, equal side ratios ----
mh4 = [(100.0, 50.0), (520.0, 190.0)]
mv4 = [(350.0, -90.0), (270.0, 330.0)]
m4, _, _ = build(H, V, mh4, mv4)
a0, a1, a2 = m4((0.0, 0.0)), m4((1.0, 0.0)), m4((0.0, 1.0))
w = 0.0
for _ in range(2000):
    p = (rng.uniform(-290.0, 290.0), rng.uniform(-190.0, 190.0))
    q = (a0[0] + p[0] * (a1[0] - a0[0]) + p[1] * (a2[0] - a0[0]),
         a0[1] + p[0] * (a1[1] - a0[1]) + p[1] * (a2[1] - a0[1]))
    w = max(w, dist(m4(p), q))
OUT["affine_case_max_px"] = w
print("4  straight axes with the crossing at both midpoints: deviation from one affine map %.3e px" % w)

# ---- 5. end points land on end points, the crossing on the crossing ----
m5, _, _ = build(H, V, mh, mv)
ends = max(dist(m5(H[0]), mh[0]), dist(m5(H[1]), mh[-1]),
           dist(m5(V[0]), mv[0]), dist(m5(V[1]), mv[-1]), dist(m5((0.0, 0.0)), o))
OUT["endpoint_anchor_max_px"] = ends
print("5  axis end points and crossing: max error %.3e px" % ends)

# ---- 6. det J does not depend on the distance from the spine ----
R = 300.0
arc = [(R * math.sin(a), R * (1.0 - math.cos(a)))
       for a in [math.radians(-50.0 + 100.0 * k / 200) for k in range(201)]]
arc_len = cum(arc)[-1]
child_h = [(-arc_len / 2.0, 0.0), (arc_len / 2.0, 0.0)]
child_v = [(0.0, -400.0), (0.0, 400.0)]
main_v = [(0.0, -400.0), (0.0, 400.0)]
m6, _, _ = build(child_h, child_v, arc, main_v)
dets = []
e = 0.5
for d in range(-400, 401, 50):
    for x in (-120.0, -40.0, 40.0, 120.0):          # off the crossing, inside one side
        p = (x, float(d))
        if abs(d) == 400:
            p = (x, d * 0.99)
        fx = [(m6((p[0] + e, p[1]))[k] - m6((p[0] - e, p[1]))[k]) / (2 * e) for k in (0, 1)]
        fy = [(m6((p[0], p[1] + e))[k] - m6((p[0], p[1] - e))[k]) / (2 * e) for k in (0, 1)]
        dets.append((d, x, fx[0] * fy[1] - fx[1] * fy[0]))
by_x = {}
for d, x, v in dets:
    by_x.setdefault(x, []).append(v)
spread = max(max(v) - min(v) for v in by_x.values())
OUT["detJ_spread_over_offaxis"] = spread
OUT["detJ_range"] = [min(v for _, _, v in dets), max(v for _, _, v in dets)]
print("6  arc spine R=300, off-axis -400..400: det J in [%.6f, %.6f], spread along the off-axis direction %.3e"
      % (OUT["detJ_range"][0], OUT["detJ_range"][1], spread))

# ---- 7. mirror: reversing one main axis ----
_, _, i_same = build(H, V, mh, mv)
_, _, i_rev = build(H, V, list(reversed(mh)), mv)
OUT["mirrored_flag"] = {"same": bool(i_same.get("mirrored")), "main_h_reversed": bool(i_rev.get("mirrored"))}
print("7  mirrored flag: same direction %s, main H reversed %s"
      % (i_same.get("mirrored"), i_rev.get("mirrored")))

# ---- 8. severing at a fold of the texture-side frame ----
hook = [(-300.0, 0.0), (100.0, 0.0)]
for k in range(1, 61):
    th = math.radians(150.0 * k / 60.0)
    hook.append((100.0 + 60.0 * math.sin(th), 60.0 * (1.0 - math.cos(th))))
m8, _, _ = build(hook, V, H, V)
probe = am._densify([(-150.0, 20.0), (250.0, 20.0)])
seams = []
islands = am._sever_source(m8, probe, seams)
cut_x = max(p[0] for isl in islands for p in isl)
OUT["sever"] = {"can_fold": bool(m8.can_fold()), "islands": len(islands), "seams": len(seams), "cut_x": cut_x}
print("8  hook axis (fold silhouette x=160): gate %s, %d island(s), %d seam(s), stroke ends at x=%.2f"
      % (m8.can_fold(), len(islands), len(seams), cut_x))
m8s, _, _ = build(H, V, H, V)
seams0 = []
isl0 = am._sever_source(m8s, probe, seams0)
OUT["sever_straight"] = {"can_fold": bool(m8s.can_fold()), "islands": len(isl0), "seams": len(seams0)}
print("   straight frame: gate %s, %d island, %d seams" % (m8s.can_fold(), len(isl0), len(seams0)))

# ---- 9. curve reconstruction: spline output against the true image ----
def true_image(mp_, a, b, n=4000):
    return [mp_((a[0] + (b[0] - a[0]) * k / n, a[1] + (b[1] - a[1]) * k / n)) for k in range(n + 1)]


def cubic_points(cubics, per=40):
    pts = []
    for p0, c1, c2, p3 in cubics:
        for k in range(per + 1):
            t = k / per
            mt = 1 - t
            pts.append((mt ** 3 * p0[0] + 3 * mt * mt * t * c1[0] + 3 * mt * t * t * c2[0] + t ** 3 * p3[0],
                        mt ** 3 * p0[1] + 3 * mt * mt * t * c1[1] + 3 * mt * t * t * c2[1] + t ** 3 * p3[1]))
    return pts


def seg_dist(p, a, b):
    vx, vy = b[0] - a[0], b[1] - a[1]
    L = vx * vx + vy * vy
    t = 0.0 if L == 0 else max(0.0, min(1.0, ((p[0] - a[0]) * vx + (p[1] - a[1]) * vy) / L))
    return math.hypot(p[0] - a[0] - t * vx, p[1] - a[1] - t * vy)


def to_polyline(p, poly):
    return min(seg_dist(p, a, b) for a, b in zip(poly, poly[1:]))


recon = {}
src_a, src_b = (20.0 - 200.0, 170.0 - 200.0), (380.0 - 200.0, 230.0 - 200.0)
Hc = [(-200.0, 0.0), (200.0, 0.0)]
Vc = [(0.0, -200.0), (0.0, 200.0)]
for name, amp in (("mild", 15.0), ("strong", 45.0), ("extreme", 80.0)):
    mhw = sine(amp, 260.0, -200.0, 200.0, 101)
    mvw = vsine(15.0, 400.0, -200.0, 200.0, 101)
    m9, _, _ = build(Hc, Vc, mhw, mvw)
    truth = true_image(m9, src_a, src_b)
    source = am._densify([src_a, src_b])
    flagged = am._adaptive_map_polyline(m9, source)
    knots = am._decimate_between_anchors(flagged, 0.3)
    spline = cubic_points(am._catmull_rom_cubics(knots))
    chord = [m9(src_a), m9(src_b)]
    err_spline = max(to_polyline(p, truth) for p in spline)
    err_poly = max(to_polyline(p, knots) for p in truth[::8])
    bend = max(seg_dist(p, chord[0], chord[1]) for p in truth[::8])
    recon[name] = {"image_bend_px": bend, "spline_err_px": err_spline,
                   "polyline_err_px": err_poly, "anchors": len(source), "knots": len(knots)}
    print("9  %-7s main axis: image leaves its chord by %.1f px; spline output %.3f px from the true image "
          "(%d source vertices -> %d knots)" % (name, bend, err_spline, len(source), len(knots)))
OUT["reconstruction"] = recon

# ---- 10. additional line: flow field ----
def line_asset(points, line_id=0):
    return {"points": [list(p) for p in points], "width": 3.0, "id": line_id,
            "third": [list(p) for p in points]}


ANG = math.radians(25.0)
cl = line_asset([(60.0, 120.0), (260.0, 120.0)])
mr = line_asset([(60.0, 120.0), (60.0 + 200.0 * math.cos(ANG), 120.0 + 200.0 * math.sin(ANG))])
t0_ = time.perf_counter()
m10, _, _ = build(H, V, H, V, pairs=[(cl, mr)])
build_ms = (time.perf_counter() - t0_) * 1000.0
w = m10.warp
a_, b_ = w.apply((160.0, 120.0)), w.apply((168.0, 120.0))
flow = math.degrees(math.atan2(b_[1] - a_[1], b_[0] - a_[0]))
probes = [(0.0, 0.0), (0.0, 150.0), (0.0, -100.0), (200.0, 0.0), (-250.0, 0.0), (37.7, 0.0), (0.0, 66.6)]
drift = max(dist(w.apply(p), p) for p in probes)
rt = max(dist(w.unapply(w.apply(p)), p) for p in [(160.0, 120.0), (200.0, 160.0), (60.0, 90.0), (160.0, -150.0)])
times = []
for _ in range(5):
    am._MAPPER_CACHE.clear() if hasattr(am, "_MAPPER_CACHE") else None
    t1_ = time.perf_counter()
    am._FlowFieldWarp  # the class the build below instantiates
    build(H, V, H, V, pairs=[(line_asset(cl["points"], 0), line_asset(mr["points"], 0))])
    times.append((time.perf_counter() - t1_) * 1000.0)
OUT["flow_field"] = {"ask_deg": 25.0, "flow_on_line_deg": flow, "axis_drift_px": drift,
                     "round_trip_px": rt, "no_fold": bool(w._all_positive and w.fold_loci() == []),
                     "first_build_ms": build_ms, "rebuild_ms_median": sorted(times)[len(times) // 2]}
print("10 additional line asking 25 deg: flow on the line %.1f deg, axis drift %.2e px, round trip %.1e px, "
      "folds: %s" % (flow, drift, rt, "none" if OUT["flow_field"]["no_fold"] else "present"))
print("   mapper build with one line: first %.0f ms, median of 5 rebuilds %.0f ms"
      % (build_ms, OUT["flow_field"]["rebuild_ms_median"]))

# pure translation pair is inert
m11, _, _ = build(H, V, H, V, pairs=[(line_asset([(60.0, 50.0), (260.0, 50.0)]),
                                      line_asset([(90.0, 70.0), (290.0, 70.0)]))])
inert = max(dist(m11(p), p) for p in [(160.0, 50.0), (160.0, 75.0), (300.0, 50.0), (160.0, 0.0)])
OUT["translation_pair_inert_px"] = inert
print("   a pure translation pair moves the map by at most %.3f px" % inert)

# ---- 11. per-point cost ----
def per_point(mp_, n=20000):
    pts = [(rng.uniform(-250.0, 250.0), rng.uniform(-150.0, 150.0)) for _ in range(n)]
    t = time.perf_counter()
    for p in pts:
        mp_(p)
    return (time.perf_counter() - t) / n * 1e6


m_str, _, _ = build(H, V, mh, mv)
m_cur, _, _ = build(sine(30.0, 300.0), V, mh, mv)
OUT["per_point_us"] = {"straight_child": per_point(m_str), "sine_child": per_point(m_cur)}
print("11 one mapped point: %.1f us (straight texture axes), %.1f us (curved texture axes)"
      % (OUT["per_point_us"]["straight_child"], OUT["per_point_us"]["sine_child"]))

# ---- environment ----
try:
    commit = subprocess.run(["git", "-C", ROOT, "rev-parse", "--short", "HEAD"],
                            capture_output=True, text=True).stdout.strip()
except OSError:
    commit = ""
OUT["environment"] = {"commit": commit, "python": platform.python_version(),
                      "machine": platform.processor(), "system": platform.platform()}
print("env:", OUT["environment"])
with open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "verify_results.json"), "w",
          encoding="utf-8") as fh:
    json.dump(OUT, fh, indent=1, ensure_ascii=False)
