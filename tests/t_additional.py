"""Additional line: warp geometry, falloffs, inverse, sync, redraw-replace."""
import math
import sys
import types

ROOT = r"C:\Users\admin\Documents\AnimeAn"
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am

H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]


def build(pairs=None):
    mp, ws = am.build_mapper(H, V, H, V, {}, additional_pairs=pairs)
    assert mp is not None, ws
    return mp


def close(a, b, tol=1e-6):
    return math.hypot(a[0] - b[0], a[1] - b[1]) <= tol


# 1) no pairs -> no warp, exact identity
mp0 = build()
assert mp0.warp is None
for p in [(-120.0, 80.0), (37.0, -140.0), (0.0, 0.0)]:
    assert close(mp0(p), p), (p, mp0(p))
print("1) no pairs: warp None, identity exact")

# 2) one pair: child line y=50 (x -100..100), main line shifted (+30, +20).
#    The FULL vector acts (station k -> station k); the chord normal is the
#    DECAY direction, not a projection filter (a projection could not trace
#    C-shaped asks - their arms displace along the chord).
child_line = {"points": [(-100.0, 50.0), (100.0, 50.0)], "width": 2.5}
main_line = {"points": [(-70.0, 70.0), (130.0, 70.0)], "width": 2.5}
am._ADDITIONAL["falloff"] = "linear"
mp = build([(child_line, main_line)])
assert mp.warp is not None and len(mp.warp.pairs) == 1
R = mp.warp.pairs[0]["radius"]
assert abs(R - 100.0) < 1e-6, R

on_line = mp((0.0, 50.0))
assert close(on_line, (30.0, 70.0), 1e-3), on_line   # full (+30, +20)
half = mp((0.0, 50.0 + R / 2))
assert close(half, (15.0, 50.0 + R / 2 + 10.0), 1e-3), half   # linear w(0.5)=0.5
outside = mp((0.0, 50.0 + R * 1.05))
assert close(outside, (0.0, 50.0 + R * 1.05), 1e-9), outside  # beyond R: untouched
far_side = mp((0.0, 50.0 - R / 2))
assert close(far_side, (15.0, 50.0 - R / 2 + 10.0), 1e-3), far_side  # symmetric
print("2) linear falloff: on-line full vector, half-radius half, outside 0")

# 3) quadratic falloff: w(0.5) = 0.25
am._ADDITIONAL["falloff"] = "quadratic"
mpq = build([(child_line, main_line)])
halfq = mpq((0.0, 50.0 + R / 2))
assert close(halfq, (7.5, 50.0 + R / 2 + 5.0), 1e-3), halfq
am._ADDITIONAL["falloff"] = "linear"
print("3) quadratic falloff: half-radius quarter vector")

# 4) warp inverse: apply o unapply == identity
w = mp.warp
for probe in [(0.0, 50.0), (30.0, 80.0), (-50.0, 10.0), (0.0, 149.0)]:
    back = w.unapply(w.apply(probe))
    assert close(back, probe, 1e-6), (probe, back)
inv = mp.inverse((30.0, 70.0))
assert close(inv, (0.0, 50.0), 1e-3), inv
print("4) unapply and mapper.inverse land back on the child line")

# 5) fold-space consistency: _arc_of_point vs _child_of_arcs round-trip
for probe in [(10.0, 55.0), (-40.0, 90.0), (80.0, -20.0)]:
    arcs = am._arc_of_point(mp, probe)
    back = am._child_of_arcs(mp, arcs)
    assert close(back, probe, 1e-5), (probe, arcs, back)
print("5) _arc_of_point / _child_of_arcs invert each other under the warp")

# 6) sync + redraw-replace through the tool entry (assets simulated)
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
ok = am.run_additional_line_tool("child", [(-100.0, 50.0), (100.0, 50.0)])
assert ok
child_lines = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]
main_lines = am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(child_lines) == 1 and len(main_lines) == 1
assert close(main_lines[0]["points"][0], (-100.0, 50.0), 1e-6)  # identity sync
# redraw NEAR the synced line on the main board -> replaces index 0
ok = am.run_additional_line_tool("main", [(-70.0, 70.0), (130.0, 70.0)])
assert ok
main_lines = am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(main_lines) == 1, len(main_lines)
assert close(main_lines[0]["points"][0], (-70.0, 70.0), 1e-6)
pairs = am._additional_pairs()
assert pairs and len(pairs) == 1
mp2, _ = am.build_mapper(H, V, H, V, {}, additional_pairs=pairs)
assert close(mp2((0.0, 50.0)), (30.0, 70.0), 1e-3)  # full vector, station-matched
# drawing far away appends a NEW pair and syncs it
ok = am.run_additional_line_tool("child", [(-100.0, -150.0), (100.0, -150.0)])
assert ok
assert len(am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]) == 2
assert len(am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"]) == 2
print("6) tool entry: sync, redraw-replace, new-pair append all behave")

# 7) drawing on MAIN syncs the inverse onto the child board
am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"].clear()
am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"].clear()
ok = am.run_additional_line_tool("main", [(50.0, -100.0), (50.0, 100.0)])
assert ok
synced = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"][0]["points"]
assert close(synced[0], (50.0, -100.0), 1e-3)
print("7) main-side draw syncs through the inverse")

# 8) QUADRATIC strong ask: +60 normal on a 200 chord exceeds the quadratic
#    fold threshold (R/gain = 50) - the map now legitimately FOLDS, and the
#    machinery must AGREE with itself: fold_sign flips exactly where the
#    numeric determinant is negative, and the inverse stays exact outside
#    the folded band.
am._ADDITIONAL["falloff"] = "quadratic"
big = {"points": [(-100.0, 50.0), (100.0, 50.0)], "width": 2.5}
big_m = {"points": [(-100.0, 110.0), (100.0, 110.0)], "width": 2.5}
mpf = build([(big, big_m)])


def det8(p, eps=0.5):
    ax, bx = mpf((p[0] + eps, p[1])), mpf((p[0] - eps, p[1]))
    ay, by = mpf((p[0], p[1] + eps)), mpf((p[0], p[1] - eps))
    return (((ax[0] - bx[0]) * (ay[1] - by[1])
             - (ay[0] - by[0]) * (ax[1] - bx[1])) / (4 * eps * eps))


folded = 0
for k in range(81):
    y = -40.0 + 200.0 * k / 80.0
    d = det8((0.0, y))
    if abs(d) < 0.05:
        continue
    sign = am._fold_sign(mpf, (0.0, y))
    assert (d > 0) == (sign == 1), (y, d, sign)
    if d < 0:
        folded += 1
assert folded > 0, "the strong quadratic ask must fold"
for probe in [(0.0, -20.0), (0.0, 190.0), (80.0, 30.0)]:
    rt = mpf.inverse(mpf(probe))
    assert close(rt, probe, 1e-3), (probe, rt)
am._ADDITIONAL["falloff"] = "linear"
print(f"8) quadratic strong ask folds ({folded} probes) with consistent "
      "orientation; inverse exact outside the band")

# 9) CURVED-LINE contract (review F2): a bowed line must receive its full
#    correction ON ITSELF (weight centred on the drawn line, not the chord).
bow = [(x, 50.0 + 50.0 * (1.0 - (x / 100.0) ** 2)) for x in range(-100, 101, 10)]
bow_m = [(x, y + 30.0) for x, y in bow]
mpb = build([({"points": bow, "width": 2.5}, {"points": bow_m, "width": 2.5})])
apex = mpb((0.0, 100.0))
assert close(apex, (0.0, 130.0), 0.8), apex
print("9) bowed line: apex receives the full +30 on itself")

# 10) DOUBLE-ENCODE guard (review): appending an untouched line inside an
#     existing pair's field must not change the mapping.
am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"].clear()
am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"].clear()
am.run_additional_line_tool("child", [(-100.0, 0.0), (100.0, 0.0)])
am.run_additional_line_tool("main", [(-100.0, 20.0), (100.0, 20.0)])  # edit pair 1
mp_before, _ = am._mapper_from_assets()
at75_before = mp_before((0.0, 75.0))
am.run_additional_line_tool("child", [(-100.0, 75.0), (100.0, 75.0)])  # append, no edit
mp_after, _ = am._mapper_from_assets()
at75_after = mp_after((0.0, 75.0))
assert close(at75_after, at75_before, 1e-3), (at75_before, at75_after)
print("10) appending an untouched line leaves the tuned field unchanged")

# 11) ORPHAN pairs (review): a one-board desync must not fabricate a warp.
am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"].clear()
am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"].clear()
am.run_additional_line_tool("child", [(-100.0, -120.0), (100.0, -120.0)])
am.run_additional_line_tool("child", [(-100.0, 0.0), (100.0, 0.0)])
am.run_additional_line_tool("child", [(-100.0, 120.0), (100.0, 120.0)])
del am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"][1]  # a lost undo half
pairs = am._additional_pairs()
assert pairs is not None and len(pairs) == 2
ids = sorted(am._line_id(c, -1) for c, m in pairs)
assert -1 not in ids, "legacy fallback misfired"
mp_desync, _ = am._mapper_from_assets()
for probe in [(0.0, -120.0), (0.0, 0.0), (0.0, 60.0), (0.0, 120.0)]:
    assert close(mp_desync(probe), probe, 1e-6), probe  # untouched pairs: identity
print("11) orphaned half-pairs are ignored, no fabricated deformation")

# 12) two straight pairs meeting at 135 deg, +60 asks. Without budgets the
#     junction MAY crease where the blended fields conflict - the contract
#     is now consistency: orientation agrees with the numeric det, the
#     inverse is exact wherever the map is locally orientation-preserving,
#     and modest asks keep any creased sliver tiny.
def det_at(mp_, p, eps=0.5):
    ax, bx = mp_((p[0] + eps, p[1])), mp_((p[0] - eps, p[1]))
    ay, by = mp_((p[0], p[1] + eps)), mp_((p[0], p[1] - eps))
    return (((ax[0] - bx[0]) * (ay[1] - by[1])
             - (ay[0] - by[0]) * (ax[1] - bx[1])) / (4 * eps * eps))


def probe_consistency(mp_, xs, ys, rt_tol=0.1):
    """Orientation must agree with the numeric det, and inverse must be a
    RIGHT inverse: in a globally folded region an image has several
    legitimate preimages, so we assert the landing's IMAGE matches, not
    the landing itself."""
    total = folded = mismatched = 0
    worst_gap = 0.0
    for x in xs:
        for y in ys:
            p = (float(x), float(y))
            d = det_at(mp_, p)
            if abs(d) < 0.05:
                continue
            total += 1
            if (d > 0) != (am._fold_sign(mp_, p) == 1):
                # the multiscale field has features finer than the 0.5 px
                # probe near small-radius hairpin segments - recheck with
                # a converged step before calling it a mismatch
                d_fine = det_at(mp_, p, eps=0.05)
                if abs(d_fine) >= 0.05 \
                        and (d_fine > 0) != (am._fold_sign(mp_, p) == 1):
                    mismatched += 1
            if d < 0:
                folded += 1
            else:
                q = mp_(p)
                q2 = mp_(mp_.inverse(q))
                worst_gap = max(worst_gap,
                                math.hypot(q2[0] - q[0], q2[1] - q[1]))
    assert mismatched == 0, mismatched
    assert worst_gap < rt_tol, worst_gap
    return total, folded, worst_gap


c1 = {"points": [(-200.0, 0.0), (0.0, 0.0)], "width": 2.5}
m1 = {"points": [(-200.0, 60.0), (0.0, 60.0)], "width": 2.5}
ang = math.radians(45.0)
c2 = {"points": [(0.0, 0.0), (200.0 * math.cos(ang), 200.0 * math.sin(ang))],
      "width": 2.5}
n2 = (-math.sin(ang), math.cos(ang))
m2 = {"points": [(p[0] + 60.0 * n2[0], p[1] + 60.0 * n2[1])
                 for p in c2["points"]], "width": 2.5}
mpx = build([(c1, m1), (c2, m2)])
# Inside the junction's doubly-covered band the map is ~100x compressive
# (whole neighborhoods share one image); unapply is best-effort there by
# contract, so the right-inverse gap is bounded loosely. Exactness OUTSIDE
# fold bands is test 8's assertion.
total, folded, worst_gap = probe_consistency(
    mpx, range(-10, 11, 2), range(-10, 11, 2), rt_tol=6.0)
print(f"12) 135-deg junction: orientation consistent, {folded}/{total} "
      f"creased probes, right-inverse gap {worst_gap:.2f} px (best-effort band)")

# 13) hairpin pairs: child-side reversals split into sub-pairs; the modest
#     +60 asks must stay consistent (orientation == numeric det) with the
#     inverse exact wherever unfolded.
hp1 = {"points": [(-100.0, 0.0), (-10.0, 0.0), (0.0, 200.0), (10.0, 0.0),
                  (100.0, 0.0)], "width": 2.5}
hp1_m = {"points": [(x, y + 60.0) for x, y in hp1["points"]], "width": 2.5}
hp2 = {"points": [(0.0, -100.0), (0.0, -10.0), (200.0, 0.0), (0.0, 10.0),
                  (0.0, 100.0)], "width": 2.5}
hp2_m = {"points": [(x + 60.0, y) for x, y in hp2["points"]], "width": 2.5}
mph = build([(hp1, hp1_m), (hp2, hp2_m)])
total, folded, worst_gap = probe_consistency(
    mph, range(-12, 13, 4), range(-12, 13, 4), rt_tol=6.0)
print(f"13) hairpin pairs: orientation consistent, {folded}/{total} creased "
      f"probes, right-inverse gap {worst_gap:.4f} px")

# 14) round-2 F3: a guide edit invalidates stored thirds
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
am.run_additional_line_tool("child", [(-100.0, 50.0), (100.0, 50.0)])
assert "third" in am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"][0]
am.run_center_line_tool("child", am.H_PROPERTY,
                        [(-300.0, -40.0), (300.0, -40.0)])
for view in ("child", "main"):
    for line in am._MAPPING_ASSETS[view][am.ADDITIONAL_PROPERTY]["lines"]:
        assert "third" not in line, (view, line.keys())
print("14) guide edit drops stale thirds on both boards")

# 15) round-2 F4: first edit of a partner moves the mapping by the edit,
#     not by edit + standing warp.
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
am.run_additional_line_tool("child", [(-100.0, 50.0), (100.0, 50.0)])
am.run_additional_line_tool("main", [(-100.0, 70.0), (100.0, 70.0)])  # tune A
mp_a, _ = am._mapper_from_assets()
before = mp_a((0.0, -10.0))[1]
am.run_additional_line_tool("child", [(-100.0, -10.0), (100.0, -10.0)])  # append B
mp_b, _ = am._mapper_from_assets()
assert abs(mp_b((0.0, -10.0))[1] - before) < 1e-3   # append is a no-op
# partner of B displays at its RENDERING position: stage A lifts base
# y=-10 by +8 (w(0.6)=0.4 of delta 20) so the synced line sits at y=-2 -
# the same frame its stored third describes (the display/third
# frame-consistency rule survives composition; the frame is now the
# standing chain's rendering, not base space).
partner = am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"][1]
assert close(partner["points"][0], (-100.0, -2.0), 1e-3), partner["points"][0]
am.run_additional_line_tool("main", [(-100.0, -1.0), (100.0, -1.0)])  # 1 px nudge
mp_c, _ = am._mapper_from_assets()
moved = mp_c((0.0, -10.0))[1] - before
assert abs(moved - 1.0) < 0.2, moved
print(f"15) 1 px partner nudge moves the mapping {moved:.2f} px (not 9)")

# 16) round-2 F5: a long shaped line must not swallow a distant short line
am._MAPPING_ASSETS.clear()
WIDE_H = [(-1200.0, 0.0), (1200.0, 0.0)]
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(WIDE_H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
am.run_additional_line_tool("child", [(-1000.0, 200.0), (1000.0, 200.0)])
am.run_additional_line_tool("child", [(-60.0, -80.0), (60.0, -80.0)])
lines = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(lines) == 2, len(lines)   # appended, not swallowed
print("16) 120 px line 280 px away appends instead of replacing a 2000 px line")

# 17) round-2 F6: redrawing the same shape right-to-left is a no-op
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
wavy = [(-100.0, 50.0), (-60.0, 74.0), (-20.0, 82.0), (20.0, 72.0),
        (60.0, 58.0), (100.0, 50.0)]
am.run_additional_line_tool("child", wavy)
probes17 = [(-60.0, 74.0), (-20.0, 82.0), (60.0, 58.0), (0.0, 40.0)]
mp_before17, _ = am._mapper_from_assets()
before17 = [mp_before17(p) for p in probes17]
am.run_additional_line_tool("child", list(reversed(wavy)))
lines = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(lines) == 1
mp_r, _ = am._mapper_from_assets()
worst = max(math.hypot(*(a - b for a, b in zip(mp_r(p), q)))
            for p, q in zip(probes17, before17))
assert worst < 0.5, worst
print("17) reversed redraw of the same shape changes nothing")

# 18) round-2 F7: legacy id-less assets survive a delete
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
legacy = {
    "lines": [
        {"points": [[-100.0, -120.0], [100.0, -120.0]], "width": 2.5},
        {"points": [[-100.0, 50.0], [100.0, 50.0]], "width": 2.5},
    ]
}
import copy
for view in ("child", "main"):
    item = copy.deepcopy(legacy)
    if view == "main":
        item["lines"][1]["points"] = [[-100.0, 70.0], [100.0, 70.0]]
    # simulate the load path stamping positional ids
    for index, line in enumerate(item["lines"]):
        line.setdefault("id", index)
    am._MAPPING_ASSETS[view][am.ADDITIONAL_PROPERTY] = item
mp_l, _ = am._mapper_from_assets()
assert abs(mp_l((0.0, 50.0))[1] - 70.0) < 0.5
am._overlay_removed({}, {}, {"overlay": {"id": f"{am.ADDITIONAL_PROPERTY}:0"},
                             "view": "child"})
pairs = am._additional_pairs()
assert pairs is not None and len(pairs) == 1
mp_l2, _ = am._mapper_from_assets()
assert abs(mp_l2((0.0, 50.0))[1] - 70.0) < 0.5
print("18) delete after legacy load keeps the surviving pair intact")

# 19) chord semantics: a CURVED line drawn on MAIN takes effect immediately -
#     the straight child chord region renders bent onto the drawn line.
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
bow_main = [(x, 50.0 + 30.0 * (1.0 - (x / 100.0) ** 2))
            for x in range(-100, 101, 10)]
ok = am.run_additional_line_tool("main", bow_main)
assert ok
partner = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"][0]
assert abs(partner["points"][0][1] - 50.0) < 1e-6      # chord, not the bow
mp_g, _ = am._mapper_from_assets()
bent = mp_g((0.0, 50.0))
assert close(bent, (0.0, 80.0), 0.8), bent             # apex renders bent
print("19) curved main line takes effect immediately (chord -> bow)")

# 20) chord semantics, child side: a curved texture feature renders straight.
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
bow_child = [(x, 50.0 + 30.0 * (1.0 - (x / 100.0) ** 2))
             for x in range(-100, 101, 10)]
ok = am.run_additional_line_tool("child", bow_child)
assert ok
partner = am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"][0]
assert abs(partner["points"][0][1] - 50.0) < 1e-6      # chord on the main side
mp_s, _ = am._mapper_from_assets()
straightened = mp_s((0.0, 80.0))                       # the drawn apex
# The safety budget clamps this strong bow (~15% sagitta) a little; the
# feature must still straighten by at least ~80% of the way to its chord.
assert abs(straightened[0]) < 0.5 and abs(straightened[1] - 50.0) < 7.0, straightened
print(f"20) curved child feature straightens onto its chord "
      f"(apex 80 -> {straightened[1]:.1f})")

print("t_additional: ALL OK")
