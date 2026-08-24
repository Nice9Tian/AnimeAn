"""Flow-field additional lines (user spec 2026-08-25): lines guide the
texture's FLOW direction; all lines blend into one target gradient field
integrated by a weighted Poisson solve (_FlowFieldWarp). These cases pin
the spec's three stages, the paradigm shifts, and the axis sanctity rule
(user 2026-08-25: no line weight near the H/V axes - they must not move)."""
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am

H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]
ANG = math.radians(25.0)
AXIS_PROBES = [(0.0, 0.0), (0.0, 150.0), (0.0, -100.0), (200.0, 0.0),
               (-250.0, 0.0), (37.7, 0.0), (0.0, 66.6)]


def line_asset(points, line_id=0):
    return {"points": [list(p) for p in points], "width": 3.0,
            "id": line_id, "third": [list(p) for p in points]}


def build(pairs):
    mp, ws = am.build_mapper(H, V, H, V, {}, additional_pairs=pairs)
    assert mp is not None
    return mp


def axis_drift(warp):
    return max(math.hypot(*(c - d for c, d in zip(warp.apply(p), p)))
               for p in AXIS_PROBES)


# 1) PARADIGM: a pure TRANSLATION pair (parallel, equal-length tangents) is
#    INERT - lines guide flow, they no longer carry absolute displacement.
mp = build([(line_asset([(60.0, 50.0), (260.0, 50.0)]),
             line_asset([(90.0, 70.0), (290.0, 70.0)]))])
assert mp.warp is not None and len(mp.warp.pairs) == 1
worst = max(math.hypot(mp(p)[0] - p[0], mp(p)[1] - p[1])
            for p in [(160.0, 50.0), (160.0, 75.0), (300.0, 50.0),
                      (160.0, 0.0)])
assert worst < 0.5, worst
print("1) translation pair is inert: lines guide flow, not displacement")

# 2) FLOW FOLLOWS THE MAIN TANGENT (off-axis fixture): child horizontal at
#    y=120, main rotated 25 deg. The on-line flow lands near the ask (the
#    fit-gain weighting plus secant boost carry it through least-squares
#    dilution); the far field decays; the field stays injective.
cl = line_asset([(60.0, 120.0), (260.0, 120.0)])
mr = line_asset([(60.0, 120.0),
                 (60.0 + 200.0 * math.cos(ANG), 120.0 + 200.0 * math.sin(ANG))])
mp2 = build([(cl, mr)])
w2 = mp2.warp
a = w2.apply((160.0, 120.0))
b = w2.apply((168.0, 120.0))
flow = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
assert 18.0 < flow < 32.0, flow
assert w2._all_positive and w2.fold_loci() == []
assert w2.has_faces is False and w2.face_at((160.0, 120.0)) == 0
print(f"2) on-line flow {flow:.1f} deg (ask 25); field injective, no folds")

# 3) AXIS SANCTITY: even with the rotation band nearby, both axes hold
#    still to sub-0.01 px (hard-pinned grid lines + weight-free halo).
drift = axis_drift(w2)
assert drift < 0.01, drift
print(f"3) axes immovable beside the band (drift {drift:.4f} px)")

# 4) ROUND TRIP: one global field and its inverse are mutual inverses.
rt = max(math.hypot(*(c - d for c, d in zip(w2.unapply(w2.apply(p)), p)))
         for p in [(160.0, 120.0), (200.0, 160.0), (60.0, 90.0),
                   (160.0, -150.0)])
assert rt < 1e-5, rt
print(f"4) unapply(apply(p)) == p (worst {rt:.1e})")

# 5) A line CROSSING the V axis still cannot move it: the drawn bend
#    flattens off-axis, the axes stay put, influence near the axis is
#    surrendered (the guard halo) by design.
bow_x = [(-100.0 + 200.0 * k / 32.0,
          50.0 + 40.0 * math.sin(math.pi * k / 32.0)) for k in range(33)]
mp3 = build([(line_asset(bow_x), line_asset([(-100.0, 50.0), (100.0, 50.0)]))])
w3 = mp3.warp
drift = axis_drift(w3)
assert drift < 0.01, drift
adj = w3.apply((50.0, 88.0))     # off-axis ground beside the bow's apex
assert adj[1] < 80.0, adj        # flattens toward the chord
print(f"5) axis-crossing bow: axes hold ({drift:.4f} px), "
      f"off-axis ground flattens to y={adj[1]:.1f}")

# 6) STAGE I - TENT: a line beyond the frame edge stretches the space; the
#    axes STILL hold (the tent's cross-axis damping - the spec's separable
#    formula slid V-axis ground 19 px); the tent inverse is exact outside
#    the solved grid.
hi_c = line_asset([(-100.0, 250.0), (100.0, 250.0)])
hi_m = line_asset([(-100.0, 250.0),
                   (-100.0 + 200.0 * math.cos(ANG),
                    250.0 + 200.0 * math.sin(ANG))])
mp4 = build([(hi_c, hi_m)])
w4 = mp4.warp
assert w4._tent["y"][1][0] > 0.0        # the top edge translated outward
drift = axis_drift(w4)
assert drift < 0.01, drift
outside = (0.0, -5000.0)
back = w4.unapply(w4.apply(outside))
assert math.hypot(back[0] - outside[0], back[1] - outside[1]) < 1e-6
rt = max(math.hypot(*(c - d for c, d in zip(w4.unapply(w4.apply(p)), p)))
         for p in [(50.0, 180.0), (0.0, 250.0), (-140.0, 240.0)])
assert rt < 1e-5, rt
print(f"6) tent stretch t_top={w4._tent['y'][1][0]:.1f} px; axes hold "
      f"({drift:.4f} px); tent inverse exact")

# 7) CONFLICT + ORDER: two lines asking opposite rotations over crossing
#    ground resolve to one injective field, independent of drawing order.
lineA = (line_asset([(60.0, 60.0), (220.0, 60.0)], 0),
         line_asset([(60.0, 60.0),
                     (60.0 + 160.0 * math.cos(math.radians(20.0)),
                      60.0 + 160.0 * math.sin(math.radians(20.0)))], 0))
lineB = (line_asset([(140.0, -20.0), (140.0, 140.0)], 1),
         line_asset([(140.0, -20.0),
                     (140.0 + 160.0 * math.sin(math.radians(-20.0)),
                      -20.0 + 160.0 * math.cos(math.radians(20.0)))], 1))
mp5 = build([lineA, lineB])
mp6 = build([lineB, lineA])
assert mp5.warp._all_positive and mp5.warp.fold_loci() == []
drift = max(math.hypot(*(c - d
                         for c, d in zip(mp5.warp.apply(p), mp6.warp.apply(p))))
            for p in [(140.0, 60.0), (180.0, 80.0), (100.0, 20.0),
                      (140.0, 120.0)])
assert drift < 1e-9, drift
print("7) conflicting asks stay injective; drawing order is irrelevant")

# 8) FALLOFF OPTION: quadratic concentrates the band - the off-line
#    effect at ~0.6R is smaller than under linear.
bow2 = [(20.0 + 200.0 * k / 32.0,
         50.0 + 40.0 * math.sin(math.pi * k / 32.0)) for k in range(33)]
chord2 = [(20.0, 50.0), (220.0, 50.0)]
probe = (120.0, 130.0)
effects = {}
for falloff in ("linear", "quadratic"):
    saved = am._ADDITIONAL["falloff"]
    am._ADDITIONAL["falloff"] = falloff
    try:
        mp7 = build([(line_asset(bow2), line_asset(chord2))])
    finally:
        am._ADDITIONAL["falloff"] = saved
    q = mp7.warp.apply(probe)
    effects[falloff] = math.hypot(q[0] - probe[0], q[1] - probe[1])
assert effects["quadratic"] < effects["linear"], effects
print(f"8) falloff shapes the band: quadratic {effects['quadratic']:.1f} px "
      f"< linear {effects['linear']:.1f} px at 0.6R")

# 9) BUILD COST: the whole solve must stay interactive (guide drags
#    rebuild the mapper).
import time
t0 = time.perf_counter()
build([(line_asset(bow2), line_asset(chord2))])
ms = (time.perf_counter() - t0) * 1000.0
assert ms < 2000.0, ms
print(f"9) mapper with a flow line builds in {ms:.0f} ms")

print("t_flowfield: ALL OK")
