"""Additional (pink) lines do NOT compose in drawing order any more: every
pair contributes a similarity to ONE blended target gradient field and a
single Poisson solve integrates it (_FlowFieldWarp). This suite pins the
interaction laws of that one global field - order-freedom, disjoint
independence, invertibility, stored-third authority, deletion, and how two
conflicting asks compromise without ever folding the sheet.

Every effect fixture and every probe stays at least 100 px away from BOTH
Third axes: the H/V axes are the field's sacred spine (hard-pinned grid
lines plus a weight-free halo), so a fixture drawn across them would be
measuring the guard, not the law under test (axis sanctity itself is
t_flowfield's case 3/5/6)."""
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
am._ADDITIONAL["falloff"] = "linear"


def fresh_boards():
    am._MAPPING_ASSETS.clear()
    for view in ("child", "main"):
        am._MAPPING_ASSETS[view] = {
            am.H_PROPERTY: {"points": list(H), "width": 3.0},
            am.V_PROPERTY: {"points": list(V), "width": 3.0},
        }


def build(pairs):
    mp, ws = am.build_mapper(H, V, H, V, {}, additional_pairs=pairs)
    assert mp is not None, ws
    return mp


def close(a, b, tol):
    return math.hypot(a[0] - b[0], a[1] - b[1]) <= tol


def spec(points):
    return {"points": [tuple(p) for p in points], "width": 2.5}


def straight(x0, x1, y, n=21):
    return [(x0 + (x1 - x0) * k / (n - 1.0), y) for k in range(n)]


def bow(x0, x1, y, sag, n=21):
    """A parabolic arc over the chord (x0,y)-(x1,y) with the given sagitta."""
    mid, half = 0.5 * (x0 + x1), 0.5 * (x1 - x0)
    return [(x, y + sag * (1.0 - ((x - mid) / half) ** 2))
            for x, _ in straight(x0, x1, y, n)]


def rotated(points, deg):
    """The polyline turned by `deg` about its own midpoint: a pure ROTATION
    ask (tau_main / tau_child = e^(i deg) at every station)."""
    cx = 0.5 * (points[0][0] + points[-1][0])
    cy = 0.5 * (points[0][1] + points[-1][1])
    ca, sa = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(cx + (x - cx) * ca - (y - cy) * sa,
             cy + (x - cx) * sa + (y - cy) * ca) for x, y in points]


def flow_angle(warp, point, tangent=(1.0, 0.0), step=1.0):
    """How far the field turns `tangent` at `point`, in degrees - the drawn
    ask's own currency (the pairs speak similarities, not displacements)."""
    jxx, jxy, jyx, jyy = warp.jacobian(point, step)
    tx, ty = tangent
    ox = jxx * tx + jxy * ty
    oy = jyx * tx + jyy * ty
    return math.degrees(math.atan2(oy * tx - ox * ty, ox * tx + oy * ty))


# 1) ORDER-FREE - the paradigm headline. Two OVERLAPPING curved pairs (bands
#    of radius 80 with their lines 55 px apart, both wholly inside the upper
#    right quadrant) built in both orders. The old model chained the stages,
#    so swapping them moved the map by tens of px; the blended field sums the
#    same per-line contributions into the same right-hand side, so the two
#    solves are bit-identical.
c_low = spec(straight(120.0, 280.0, 120.0))
m_low = spec(bow(120.0, 280.0, 120.0, 15.0))
c_high = spec(straight(120.0, 280.0, 175.0))
m_high = spec(bow(120.0, 280.0, 175.0, -15.0))
mp_ab = build([(c_low, m_low), (c_high, m_high)])
mp_ba = build([(c_high, m_high), (c_low, m_low)])
w_ab, w_ba = mp_ab.warp, mp_ba.warp
assert w_ab is not None and len(w_ab.pairs) == 2, w_ab
assert len(w_ba.pairs) == 2
order_probes = [(200.0, 120.0), (200.0, 175.0), (200.0, 148.0),
                (240.0, 160.0), (140.0, 130.0)]
order_drift = max(math.hypot(*(a - b for a, b in
                               zip(w_ab.apply(p), w_ba.apply(p))))
                  for p in order_probes)
assert order_drift < 1e-9, order_drift
# the two lines really do reshape the ground they share
strongest_shared = max(math.hypot(*w_ab.displacement(p)) for p in order_probes)
assert strongest_shared > 2.0, strongest_shared
print(f"1) order-free: both drawing orders agree to {order_drift:.1e} px at "
      f"{len(order_probes)} off-axis probes in the shared band "
      f"(effect up to {strongest_shared:.1f} px)")

# 2) DISJOINT INDEPENDENCE: two curved pairs whose bands never meet (equal
#    radii, one per far quadrant, ~500 px apart). Each line's effect ON
#    ITSELF with both present must match its solo effect - the global solve
#    must not leak one line's ask into the other's band.
c_left = spec(straight(-280.0, -130.0, 150.0))
m_left = spec(bow(-280.0, -130.0, 150.0, 20.0))
c_right = spec(straight(130.0, 280.0, -150.0))
m_right = spec(bow(130.0, 280.0, -150.0, -20.0))
mp_two = build([(c_left, m_left), (c_right, m_right)])
mp_solo_l = build([(c_left, m_left)])
mp_solo_r = build([(c_right, m_right)])
w_two = mp_two.warp
gap_abs = gap_rel = 0.0
strongest = 0.0
for solo, line in ((mp_solo_l.warp, c_left), (mp_solo_r.warp, c_right)):
    for p in line["points"][2:-2]:
        both = w_two.displacement(p)
        alone = solo.displacement(p)
        magnitude = math.hypot(*alone)
        strongest = max(strongest, magnitude)
        delta = math.hypot(both[0] - alone[0], both[1] - alone[1])
        gap_abs = max(gap_abs, delta)
        if magnitude > 1.0:
            gap_rel = max(gap_rel, delta / magnitude)
assert strongest > 5.0, strongest      # the probes must be a real effect
assert gap_abs < 2.0, gap_abs
assert gap_rel < 0.10, gap_rel
print(f"2) disjoint independence: on-line effect up to {strongest:.1f} px, "
      f"companion changes it by {gap_abs:.2e} px ({gap_rel * 100:.4f}%)")

# 3) The single global field is invertible: unapply o apply is the identity,
#    inside both bands of the overlapping pair of case 1 and out on open
#    ground (no folds to make the inverse branch-ambiguous - see case 6).
rt_worst = 0.0
rt_probes = [(200.0, 120.0), (200.0, 175.0), (200.0, 148.0),
             (250.0, 190.0), (-200.0, -150.0)]
for probe in rt_probes:
    back = w_ab.unapply(w_ab.apply(probe))
    rt_worst = max(rt_worst, math.hypot(back[0] - probe[0], back[1] - probe[1]))
    assert close(back, probe, 1e-5), (probe, back)
print(f"3) unapply inverts the blended field (worst {rt_worst:.1e} px over "
      f"{len(rt_probes)} probes, three of them inside both bands)")

# 4) STORED-THIRD AUTHORITY: a pair's Third polyline is written when it is
#    drawn and nothing else may move it. Editing pair 1's geometry (redraw
#    the bow 20 px taller) must leave pair 2's stored coordinates - on BOTH
#    boards - byte-for-byte where they were. Under the chained model pair 2
#    was re-derived through the new pair 1 at build time; the flow field has
#    no chain to re-derive against, and a branch-ambiguous re-solve was
#    measured to fabricate a 278 px delta on a pair the user never touched.
fresh_boards()
assert am.run_additional_line_tool("main", bow(130.0, 280.0, 120.0, 40.0))
mp_stage1, _ = am._mapper_from_assets()
assert mp_stage1.warp is not None and len(mp_stage1.warp.pairs) == 1
assert am.run_additional_line_tool("main", straight(-280.0, -130.0, 150.0, 19))
child_lines = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]
main_lines = am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(child_lines) == 2 and len(main_lines) == 2
main_third_before = [tuple(t) for t in main_lines[1]["third"]]
child_third_before = [tuple(t) for t in child_lines[1]["third"]]
assert am.run_additional_line_tool("main", bow(130.0, 280.0, 120.0, 60.0))
main_lines = am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"]
child_lines = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(main_lines) == 2 and len(child_lines) == 2  # replaced 1, kept 2
main_third_after = [tuple(t) for t in main_lines[1]["third"]]
child_third_after = [tuple(t) for t in child_lines[1]["third"]]
pinned = max(math.hypot(a[0] - b[0], a[1] - b[1])
             for a, b in zip(main_third_before + child_third_before,
                             main_third_after + child_third_after))
assert pinned == 0.0, pinned
assert all(abs(t[1] - 150.0) < 1e-6 for t in main_third_after), \
    main_third_after[:2]
mp_edited, _ = am._mapper_from_assets()
assert len(mp_edited.warp.pairs) == 2
print(f"4) stored-third authority: editing pair 1 moves pair 2's stored "
      f"coordinates by {pinned:.1f} px; its main intent stays pinned at y=150")

# 5) DELETION: two disjoint curved pairs drawn through the tool, then one is
#    removed by overlay id. The survivor's field must be exactly what it was
#    (it never depended on the other line), and the deleted line's ground
#    must relax back to the identity.
fresh_boards()
assert am.run_additional_line_tool("child", bow(-280.0, -130.0, 150.0, 20.0))
assert am.run_additional_line_tool("child", bow(130.0, 280.0, -150.0, -20.0))
lines0 = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(lines0) == 2, len(lines0)
mp_before, _ = am._mapper_from_assets()
assert len(mp_before.warp.pairs) == 2
survivor = bow(130.0, 280.0, -150.0, -20.0, 9)[1:-1]
effect_before = [mp_before.warp.displacement(p) for p in survivor]
am._overlay_removed(None, None, {
    "view": "child",
    "overlay": {"id": f"{am.ADDITIONAL_PROPERTY}:{am._line_id(lines0[0], 0)}"}})
mp_after, _ = am._mapper_from_assets()
assert mp_after.warp is not None and len(mp_after.warp.pairs) == 1
effect_after = [mp_after.warp.displacement(p) for p in survivor]
kept = max(math.hypot(a[0] - b[0], a[1] - b[1])
           for a, b in zip(effect_before, effect_after))
assert max(math.hypot(*d) for d in effect_after) > 5.0   # still a real effect
assert kept < 1.0, kept
ground = max(math.hypot(*mp_after.warp.displacement(p))
             for p in [(-205.0, 150.0), (-205.0, 170.0), (-280.0, 150.0),
                       (-130.0, 190.0), (-250.0, -150.0)])
assert ground < 0.05, ground
print(f"5) deletion: survivor's on-line effect moves {kept:.2e} px, the "
      f"removed line's ground relaxes to identity ({ground:.2e} px)")

# 6) CONFLICT COMPROMISE - the spec's conflict-resolution claim. Two pairs
#    ask OPPOSITE rotations (+10 / -10 deg) over off-axis ground their bands
#    share (lines 60 px apart, radius 70, and the rotated mains still clear
#    both axes by 100 px). The old model let the later line win outright; the
#    blended field minimises internal stress instead:
#      * the sheet never doubles back (det > 0 everywhere, no fold loci),
#      * and the settlement sits BETWEEN the two asks - each line still
#        turns the flow its own way, but by less than it would alone.
X_LO, X_HI, ASK = 130.0, 270.0, 10.0
Y_LOW, Y_HIGH = 115.0, 175.0
low = straight(X_LO, X_HI, Y_LOW)
high = straight(X_LO, X_HI, Y_HIGH)
pair_low = (spec(low), spec(rotated(low, ASK)))
pair_high = (spec(high), spec(rotated(high, -ASK)))
w_conflict = build([pair_low, pair_high]).warp
w_low_solo = build([pair_low]).warp
w_high_solo = build([pair_high]).warp
assert w_conflict.fold_loci() == [], w_conflict.fold_loci()
negative = [(x, y)
            for i in range(7) for j in range(7)
            for x in [X_LO + 10.0 + (X_HI - X_LO - 20.0) * i / 6.0]
            for y in [Y_LOW + (Y_HIGH - Y_LOW) * j / 6.0]
            if w_conflict.det_sign((x, y)) != 1]
assert not negative, negative
stations = [X_LO + (X_HI - X_LO) * k / 8.0 for k in range(1, 8)]
got_low = [flow_angle(w_conflict, (x, Y_LOW)) for x in stations]
got_high = [flow_angle(w_conflict, (x, Y_HIGH)) for x in stations]
solo_low = [flow_angle(w_low_solo, (x, Y_LOW)) for x in stations]
solo_high = [flow_angle(w_high_solo, (x, Y_HIGH)) for x in stations]
for got, alone in zip(got_low, solo_low):
    assert 0.0 < got < alone, (got, alone)          # own direction, less of it
for got, alone in zip(got_high, solo_high):
    assert alone < got < 0.0, (got, alone)
mean_low = sum(got_low) / len(got_low)
mean_high = sum(got_high) / len(got_high)
mean_solo_low = sum(solo_low) / len(solo_low)
mean_solo_high = sum(solo_high) / len(solo_high)
assert 0.10 < mean_low / mean_solo_low < 0.85, mean_low / mean_solo_low
assert 0.10 < mean_high / mean_solo_high < 0.85, mean_high / mean_solo_high
print(f"6) conflict: +{ASK:.0f}/-{ASK:.0f} deg over shared ground settle at "
      f"{mean_low:+.2f} deg (alone {mean_solo_low:+.2f}) and "
      f"{mean_high:+.2f} deg (alone {mean_solo_high:+.2f}); "
      "det > 0 on 49 band probes, no fold loci")

print("t_compose: ALL OK")
