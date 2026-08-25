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

# 5) A line CROSSING the V axis still cannot move it, but (user refinement
#    2026-08-25) the halo is void PER HALF-SIDE where the crossing lands:
#    the bow crosses V in the upper half, so right beside the axis there
#    the flow follows the drawn unbending ask instead of being muted; the
#    axis itself stays hard-pinned.
bow_x = [(-100.0 + 200.0 * k / 32.0,
          50.0 + 40.0 * math.sin(math.pi * k / 32.0)) for k in range(33)]
mp3 = build([(line_asset(bow_x), line_asset([(-100.0, 50.0), (100.0, 50.0)]))])
w3 = mp3.warp
drift = axis_drift(w3)
assert drift < 0.01, drift


def hflow(warp, x, y):
    a = warp.apply((x - 4.0, y))
    b = warp.apply((x + 4.0, y))
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


left = hflow(w3, -40.0, 88.0)    # bow tangent ~ +20 deg -> ask ~ -20
right = hflow(w3, 40.0, 88.0)    # bow tangent ~ -20 deg -> ask ~ +20
assert -25.0 < left < -10.0, left
assert 10.0 < right < 25.0, right
assert abs(w3.apply((0.0, 95.0))[0]) < 0.01
print(f"5) axis-crossing bow: axes hold ({drift:.4f} px); crossed upper "
      f"half keeps its voice beside V (flow {left:.1f} / {right:+.1f} deg)")

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

# --- Review fixes (2026-08-25, round 2) ------------------------------------

# 10) THE TENT MUST NOT DISPLACE THE ASK: the same drawn pair, with the
#     frame's V half-height shrunk so the tent overrun grows from 0 to
#     ~200 px, keeps the on-line flow. Pre-fix the weights were measured
#     at the STRETCHED node positions and the ask evaporated (25 -> 1.6
#     deg) with its peak 125 px from the drawn line.
for half, lo_flow in ((600.0, 15.0), (200.0, 15.0)):
    v_short = [(0.0, -half), (0.0, half)]
    mp8, _ = am.build_mapper(H, v_short, H, v_short, {}, additional_pairs=[
        (line_asset([(60.0, 320.0), (260.0, 320.0)]),
         line_asset([(60.0, 320.0),
                     (60.0 + 200.0 * math.cos(ANG),
                      320.0 + 200.0 * math.sin(ANG))]))])
    a = mp8.warp.apply((160.0, 320.0))
    b = mp8.warp.apply((168.0, 320.0))
    flow = math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))
    assert lo_flow < flow < 32.0, (half, flow)
print("10) the ask stays on the drawn line whatever the tent overrun")

# 11) FAR GROUND AND AXES BEYOND THE GRID: the tent fades to zero past the
#     stretched window (a clamped tent translated ground 3000 px out by
#     the full overrun), and the axes hold outside the grid too.
q = mp8((280.0, 3000.0))
assert math.hypot(q[0] - 280.0, q[1] - 3000.0) < 1e-6, q
drift = max(math.hypot(*(c - d for c, d in zip(mp8.warp.apply(p), p)))
            for p in [(0.0, 5000.0), (4000.0, 0.0), (0.0, -3000.0),
                      (0.0, 150.0), (200.0, 0.0)])
assert drift < 0.05, drift
print("11) tent fades out; axes hold in and out of the grid")

# 12) HONEST FOLDS, END TO END: a strong ask pressed against the pinned
#     axis genuinely folds the field - and every consumer must see the
#     SAME story: _all_positive False, loci marched, det_sign flipping,
#     a crossing stroke splitting front/back, the loci registered for
#     the cutters, and unapply still a right inverse inside the band.
ang45 = math.radians(45.0)
mp9, _ = am.build_mapper(H, V, H, V, {}, additional_pairs=[
    (line_asset([(30.0, 35.0), (250.0, 35.0)]),
     line_asset([(30.0, 35.0), (30.0 + 220.0 * math.cos(ang45),
                                35.0 + 220.0 * math.sin(ang45))]))])
w9 = mp9.warp
assert not w9._all_positive
loci9 = w9.fold_loci()
assert loci9, "a folding field must march its loci"
assert loci9 is not w9.fold_loci()          # fresh copies per call
band = [(x, y) for x in (35.0, 45.0, 60.0) for y in (8.0, 11.0)]
assert any(w9.det_sign(p) == -1 for p in band), \
    [w9.det_sign(p) for p in band]
runs9 = am._split_by_fold(mp9, am._densify([(30.0, -40.0), (30.0, 70.0)]))
sides9 = {side for _r, side in runs9}
assert sides9 == {1, -1}, sides9
am._crease_curves(mp9, (-250.0, 250.0), (-350.0, 350.0), stitch=False)
assert getattr(mp9, "warp_curve_child", None), \
    "warp loci must register their child geometry for the cutters"
ri = 0.0
for p in band:
    z = w9.apply(p)
    back = w9.apply(w9.unapply(z))
    ri = max(ri, math.hypot(back[0] - z[0], back[1] - z[1]))
assert ri < 0.05, ri
print(f"12) honest fold: {len(loci9)} locus, det_sign flips, stroke "
      f"splits {sorted(sides9)}, loci registered, right-inverse {ri:.1e}")

# 13) SILENT NO-OPS SPEAK: a neutral pair and a voiceless line (drawn ON
#     its own family's axis, where its keyframe weight is zero) both
#     leave a note for the user instead of looking broken.
mp10 = build([(line_asset([(60.0, 120.0), (260.0, 120.0)]),
               line_asset([(90.0, 150.0), (290.0, 150.0)]))])
assert any("neutral" in n for n in mp10.warp.notes), mp10.warp.notes
mp11 = build([(line_asset([(60.0, 0.0), (260.0, 0.0)]),
               line_asset([(60.0, 0.0),
                           (60.0 + 200.0 * math.cos(math.radians(15.0)),
                            200.0 * math.sin(math.radians(15.0)))]))])
assert mp11.warp is None or any("no influence" in n
                                for n in mp11.warp.notes), \
    None if mp11.warp is None else mp11.warp.notes
print("13) neutral and on-axis (voiceless) lines leave notes")

# 14) NEAR-CLOSED LINES align by endpoint proximity (their chord's sign is
#     pen-lift noise): reversing the partner's point order changes nothing.
loop_c = [(150.0 + 40.0 * math.cos(2 * math.pi * k / 24.0),
           120.0 + 40.0 * math.sin(2 * math.pi * k / 24.0))
          for k in range(24)]
loop_m = [(150.0 + 46.0 * math.cos(2 * math.pi * k / 24.0),
           120.0 + 34.0 * math.sin(2 * math.pi * k / 24.0))
          for k in range(24)]
mp12 = build([(line_asset(loop_c), line_asset(loop_m))])
mp13 = build([(line_asset(loop_c), line_asset(list(reversed(loop_m))))])
drift = max(math.hypot(*(c - d for c, d in zip(mp12(p), mp13(p))))
            for p in [(150.0, 120.0), (190.0, 120.0), (150.0, 160.0)])
assert drift < 1.0, drift
print("14) near-closed pair alignment is point-order-proof")

# 15) MAPPER CACHE: same content -> same object; ANY mapping-shaping edit
#     (guide points, line geometry, stored third, falloff) -> rebuild.
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": [list(p) for p in H], "width": 3.0},
        am.V_PROPERTY: {"points": [list(p) for p in V], "width": 3.0},
        am.ADDITIONAL_PROPERTY: {"lines": [
            {"points": [[60.0, 120.0], [260.0, 120.0]], "width": 3.0,
             "id": 0, "third": [[60.0, 120.0], [260.0, 120.0]]}
            if view == "child" else
            {"points": [[60.0, 120.0], [242.0, 205.0]], "width": 3.0,
             "id": 0, "third": [[60.0, 120.0], [242.0, 205.0]]}]},
    }
am._MAPPER_CACHE["key"] = None
m_a, _ = am._current_mapper()
m_b, _ = am._current_mapper()
assert m_a is not None and m_b is m_a
am._MAPPING_ASSETS["child"][am.H_PROPERTY]["points"][0][1] = 4.0
m_c, _ = am._current_mapper()
assert m_c is not m_a                      # guide edit rebuilds
am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"][0][
    "third"][1][1] = 190.0
m_d, _ = am._current_mapper()
assert m_d is not m_c                      # stored-third edit rebuilds
am._MAPPING_ASSETS.clear()
am._MAPPER_CACHE["key"] = None
am._MAPPER_CACHE["mapper"] = None
print("15) mapper cache: content-keyed reuse, edits rebuild")

# 16) FAMILY GEODESICS (user restatement 2026-08-25): an additional line
#     is a drawn iso-line. A V-family line (vertical stroke) keeps its
#     voice beside the ORTHOGONAL H axis - crossing it or not - because
#     orthogonal families never interact; its own weight instead dies on
#     its PARALLEL V axis (the ramp toward the origin O).  The axes stay
#     immovable throughout.
cx = line_asset([(120.0, -80.0), (120.0, 80.0)])
cm = line_asset([(120.0, -80.0),
                 (120.0 - 160.0 * math.sin(ANG),
                  -80.0 + 160.0 * math.cos(ANG))])
w_cross = build([(cx, cm)]).warp


def vflow(warp, x, y):
    a = warp.apply((x, y - 4.0))
    b = warp.apply((x, y + 4.0))
    return math.degrees(math.atan2(b[0] - a[0], b[1] - a[1]))


# Ask: -25 deg off vertical.  Probes sit just outside the few-cell
# numerical boundary layer beside the pinned H rows (the layer holds a
# ~1 px harmonic recoil, not the ask).
tilt = vflow(w_cross, 120.0, 30.0)
assert -35.0 < tilt < -12.0, tilt           # voice kept beside H
# A NON-crossing V-family line keeps it just the same (H never mutes it).
nx_ = line_asset([(120.0, 20.0), (120.0, 180.0)])
nm_ = line_asset([(120.0, 20.0),
                  (120.0 - 160.0 * math.sin(ANG),
                   20.0 + 160.0 * math.cos(ANG))])
w_nocross = build([(nx_, nm_)]).warp
tilt_nc = vflow(w_nocross, 120.0, 40.0)
assert -35.0 < tilt_nc < -12.0, tilt_nc
# The ramp toward the parallel V axis: the tilt fades monotonically from
# the line to the axis and (nearly) vanishes beside it.
t_line = abs(vflow(w_cross, 120.0, 45.0))
t_mid = abs(vflow(w_cross, 55.0, 45.0))
t_axis = abs(vflow(w_cross, 12.0, 45.0))
assert t_axis < t_mid < t_line, (t_axis, t_mid, t_line)
assert t_axis < 0.35 * t_line, (t_axis, t_line)
assert axis_drift(w_cross) < 0.01 and axis_drift(w_nocross) < 0.01
assert w_cross._all_positive and w_cross.fold_loci() == []
print(f"16) V-family line: voice beside H kept ({tilt:.1f} deg crossing, "
      f"{tilt_nc:.1f} not crossing); ramp to its own axis "
      f"{t_line:.1f} -> {t_mid:.1f} -> {t_axis:.1f} deg; axes hold")

# 17) CONSTRAIN OUTLINE (user 2026-08-25, default ON): with a strictly
#     interior pair the window outline keeps its SHAPE (component pins -
#     ground may slide ALONG an edge); unchecked, the field is free and
#     the outline bends.  The flag is part of the mapper fingerprint.
ci = line_asset([(60.0, 60.0), (220.0, 60.0)])
mi = line_asset([(60.0, 60.0),
                 (60.0 + 160.0 * math.cos(ANG), 60.0 + 160.0 * math.sin(ANG))])
OUTLINE_PTS = [(300.0, 120.0), (300.0, -150.0), (-300.0, 80.0),
               (150.0, 200.0), (-200.0, -200.0), (300.0, 200.0)]


def shape_dev(warp, p):
    q = warp.apply(p)
    devs = []
    if abs(abs(p[0]) - 300.0) < 1e-9:
        devs.append(abs(q[0] - p[0]))
    if abs(abs(p[1]) - 200.0) < 1e-9:
        devs.append(abs(q[1] - p[1]))
    return max(devs)


assert am._ADDITIONAL["constrain_outline"] is True      # the default
w_con = build([(ci, mi)]).warp
dev_on = max(shape_dev(w_con, p) for p in OUTLINE_PTS)
assert dev_on < 0.01, dev_on
assert axis_drift(w_con) < 0.01
am._ADDITIONAL["constrain_outline"] = False
w_free = build([(ci, mi)]).warp
dev_off = max(shape_dev(w_free, p) for p in OUTLINE_PTS)
assert dev_off > 5.0, dev_off
am._ADDITIONAL["constrain_outline"] = True
print(f"17) constrain outline: ON holds shape ({dev_on:.4f} px), "
      f"OFF lets it bend ({dev_off:.1f} px)")

# 18) RELEASED SIDE: a pair drawn OUT across the top edge releases that
#     side (it follows the tent + flow); the other edges keep their
#     lines - side edges stay x = const (ground may slide along them,
#     the corner rides up the pinned edge to meet the released top), the
#     bottom stays y = const; no folds, exact round trip.
out_c = line_asset([(-100.0, 250.0), (100.0, 250.0)])
out_m = line_asset([(-100.0, 250.0),
                    (-100.0 + 200.0 * math.cos(ANG),
                     250.0 + 200.0 * math.sin(ANG))])
w_rel = build([(out_c, out_m)]).warp
lifted = max(w_rel.apply((x, 200.0))[1] - 200.0 for x in (100.0, 150.0))
assert lifted > 20.0, lifted
for p in [(300.0, 150.0), (300.0, 200.0), (-300.0, 180.0)]:
    assert abs(w_rel.apply(p)[0] - p[0]) < 0.01, p
for p in [(150.0, -200.0), (-100.0, -200.0)]:
    assert abs(w_rel.apply(p)[1] - p[1]) < 0.01, p
assert axis_drift(w_rel) < 0.01
assert w_rel._all_positive and w_rel.fold_loci() == []
rt = max(math.hypot(*(c - d for c, d in zip(w_rel.unapply(w_rel.apply(p)), p)))
         for p in [(50.0, 180.0), (0.0, 250.0), (295.0, 195.0),
                   (299.0, 199.0)])
assert rt < 1e-5, rt
print(f"18) drawn-out side released (top lifts {lifted:.1f} px), other "
      f"edges hold their lines; round trip exact")

# 19) FAMILY SEPARATION AND OUTWARD HOLD: an untouched V-family line laid
#     across an H-family band changes NOTHING (orthogonal families never
#     interact; neutral lines are out of the field entirely), while a
#     TUNED V-family line and the H line each keep their own family's
#     ask.  Beyond the outermost line its influence is HELD (until the
#     released window edge), not cut at a radius.
hbase = [(60.0 + 200.0 * k / 16.0, 120.0) for k in range(17)]
hmain = [(60.0 + (p[0] - 60.0) * math.cos(ANG)
          - (p[1] - 120.0) * math.sin(ANG),
          120.0 + (p[0] - 60.0) * math.sin(ANG)
          + (p[1] - 120.0) * math.cos(ANG)) for p in hbase]
vneutral = [(150.0, -20.0 + 180.0 * k / 16.0) for k in range(17)]
mp_solo = build([(line_asset(hbase, 0), line_asset(hmain, 0))])
mp_dual = build([(line_asset(hbase, 0), line_asset(hmain, 0)),
                 (line_asset(vneutral, 1), line_asset(vneutral, 1))])
change = max(math.hypot(*(c - d for c, d in zip(mp_dual(p), mp_solo(p))))
             for p in [(160.0, 120.0), (150.0, 60.0), (100.0, 100.0),
                       (200.0, 30.0)])
assert change < 1e-9, change
w_solo = mp_solo.warp


def hflow(warp, x, y):
    a = warp.apply((x - 4.0, y))
    b = warp.apply((x + 4.0, y))
    return math.degrees(math.atan2(b[1] - a[1], b[0] - a[0]))


held = hflow(w_solo, 160.0, 190.0)     # far above the line, same x-span
on_line = hflow(w_solo, 160.0, 120.0)
assert 12.0 < held, (held, on_line)    # held outward, not radius-cut
assert 18.0 < on_line < 32.0, on_line
print(f"19) neutral orthogonal line inert ({change:.1e} px); outward hold "
      f"{held:.1f} deg at 70 px above (on-line {on_line:.1f} deg)")

print("t_flowfield: ALL OK")
