"""Multiple additional lines COMPOSE in drawing order: addition 2 lives in
addition 1's rendering. Covers ordering, the fixed mis-anchoring, edits of
earlier stages, deletion, and fold consistency on a composite."""
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


# 1) COMPOSITION SEMANTICS: both lines anchor the SAME base child material
#    (y=0). A says it renders at 60; B, drawn later over the result, says
#    it renders at 100. Composed A-then-B: the base point rides A to 60,
#    where B's staged frame sits (B's child coords are pushed through A),
#    then B adds its 40 - lands exactly at 100. Reversed, B first sends it
#    straight to 100 and A (staged THROUGH B) pulls it back to 60.
la = {"points": [(-100.0, 0.0), (100.0, 0.0)], "width": 2.5}
la_m = {"points": [(-100.0, 60.0), (100.0, 60.0)], "width": 2.5}
lb = {"points": [(-100.0, 0.0), (100.0, 0.0)], "width": 2.5}
lb_m = {"points": [(-100.0, 100.0), (100.0, 100.0)], "width": 2.5}
mp_ab = build([(la, la_m), (lb, lb_m)])
w = mp_ab.warp
assert len(w.stages) == 2, len(w.stages)
lifted = w.apply((0.0, 0.0))
assert close(lifted, (0.0, 100.0), 1e-6), lifted  # 60 by A, then +40 by B
mp_ba = build([(lb, lb_m), (la, la_m)])
swapped = mp_ba.warp.apply((0.0, 0.0))
assert close(swapped, (0.0, 60.0), 1e-6), swapped  # 100 by B, then -40 by A
print(f"1) composition: A then B lifts (0,0) -> {tuple(round(v,1) for v in lifted)}, "
      f"B then A -> {tuple(round(v, 1) for v in swapped)} (order matters)")

# 2) round trip through the composite inverse
for probe in [(0.0, 0.0), (30.0, 10.0), (-80.0, 40.0), (0.0, 150.0)]:
    back = w.unapply(w.apply(probe))
    assert close(back, probe, 1e-6), (probe, back)
print("2) composite unapply inverts stage by stage")

# 3) MIS-ANCHORING FIX, end to end through the tool: line 1 is a bow
#    drawn on MAIN (takes effect immediately: the child chord material at
#    y=50 renders bent up to the apex). Line 2 is then drawn on MAIN at
#    y=170, inside line 1's influence. Its child partner must anchor at
#    the child material that RENDERS there - the chain pullback - not at
#    the raw base coordinate 170.
fresh_boards()
bow = [(x * 1.0, 50.0 + 60.0 * (1.0 - (x / 100.0) ** 2))
       for x in range(-100, 101, 5)]
assert am.run_additional_line_tool("main", bow)
mp1, _ = am._mapper_from_assets()
assert mp1.warp is not None and len(mp1.warp.stages) == 1
expected = mp1.warp.unapply((0.0, 135.0))
line2 = [(x * 1.0, 135.0) for x in range(-90, 91, 5)]
assert am.run_additional_line_tool("main", line2)
child_lines = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(child_lines) == 2, len(child_lines)
partner2 = [tuple(p) for p in child_lines[1]["points"]]
mid = min(partner2, key=lambda p: abs(p[0]))
assert abs(mid[1] - expected[1]) < 1.5, (mid, expected)
# significantly pulled back from the base-space ghost at 135 (the exact
# amount shrank a little when the multiscale sweep landed: far ground
# feels the bow's averaged ask, not its apex)
assert 135.0 - mid[1] > 8.0, mid
print(f"3) partner of a line drawn over the warped region anchors at "
      f"child y~{mid[1]:.1f} (pullback {expected[1]:.1f}; base ghost 135)")

# 4) EDITING STAGE 1 re-derives stage 2's basis: redraw the bow higher
#    (apex +90). Stage 2's main stroke stays put - its stored third is
#    frame-based and chain-independent - while its child anchor re-derives
#    through the NEW stage 1 at build time.
bow2 = [(x * 1.0, 50.0 + 90.0 * (1.0 - (x / 100.0) ** 2))
        for x in range(-100, 101, 5)]
assert am.run_additional_line_tool("main", bow2)
main_lines = am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"]
assert len(main_lines) == 2, len(main_lines)  # replaced pair 1, kept pair 2
mp2, _ = am._mapper_from_assets()
assert len(mp2.warp.stages) == 2
t2 = [tuple(t) for t in main_lines[1]["third"]]
assert all(abs(t[1] - 135.0) < 1e-6 for t in t2), t2[:2]
print("4) redraw of stage 1 keeps stage 2's main intent pinned at y=135; "
      "stage 2 re-derives against the new chain at build")

# 5) DELETION of stage 1: remaining line still works as the only stage
fresh_boards()
assert am.run_additional_line_tool("child", [(-100.0, 50.0), (100.0, 50.0)])
assert am.run_additional_line_tool("main", [(-70.0, 70.0), (130.0, 70.0)])
assert am.run_additional_line_tool("child", [(-100.0, -150.0), (100.0, -150.0)])
lines0 = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"]
first_id = am._line_id(lines0[0], 0)
am._overlay_removed(None, None, {
    "view": "child",
    "overlay": {"id": f"{am.ADDITIONAL_PROPERTY}:{first_id}"}})
mp3, _ = am._mapper_from_assets()
assert mp3.warp is not None and len(mp3.warp.stages) == 1
assert close(mp3((0.0, -150.0)), (0.0, -150.0), 1e-6)  # neutral marker
print("5) deleting stage 1 leaves the survivor as a clean single stage")

# 6) DISJOINT lines behave like the old independent sum
fresh_boards()
c1 = {"points": [(-250.0, 50.0), (-120.0, 50.0)], "width": 2.5}
m1 = {"points": [(-250.0, 80.0), (-120.0, 80.0)], "width": 2.5}
c2 = {"points": [(120.0, -50.0), (250.0, -50.0)], "width": 2.5}
m2 = {"points": [(120.0, -90.0), (250.0, -90.0)], "width": 2.5}
mp_d = build([(c1, m1), (c2, m2)])
assert close(mp_d((-185.0, 50.0)), (-185.0, 80.0), 1e-6)
assert close(mp_d((185.0, -50.0)), (185.0, -90.0), 1e-6)
print("6) disjoint lines: each acts alone, exactly as before")

# 7) FOLD CONSISTENCY on a composite: stage 1 folds (ask 150 > R 100),
#    stage 2 rides on top nearby; orientation must match the numeric det
#    of the COMPOSITE everywhere, and loci must cover every sign change.
f1 = {"points": [(-100.0, 40.0), (100.0, 40.0)], "width": 2.5}
f1_m = {"points": [(-100.0, 190.0), (100.0, 190.0)], "width": 2.5}
f2 = {"points": [(-80.0, 220.0), (80.0, 220.0)], "width": 2.5}
f2_m = {"points": [(-50.0, 260.0), (110.0, 260.0)], "width": 2.5}
mp_f = build([(f1, f1_m), (f2, f2_m)])
wf = mp_f.warp
assert len(wf.stages) == 2


def det_at(p, eps=0.5):
    ax, bx = wf.apply((p[0] + eps, p[1])), wf.apply((p[0] - eps, p[1]))
    ay, by = wf.apply((p[0], p[1] + eps)), wf.apply((p[0], p[1] - eps))
    return (((ax[0] - bx[0]) * (ay[1] - by[1])
             - (ay[0] - by[0]) * (ax[1] - bx[1])) / (4 * eps * eps))


bad = folded = 0
for x in range(-140, 141, 20):
    for y in range(-20, 261, 10):
        p = (float(x), float(y))
        d = det_at(p)
        if abs(d) < 0.05:
            continue
        if (d > 0) != (wf.det_sign(p) == 1):
            bad += 1
        if d < 0:
            folded += 1
assert bad == 0, bad
assert folded > 0
loci = wf.fold_loci()
assert loci


def crossings_at(x):
    n = 0
    for curve in loci:
        for a, b in zip(curve, curve[1:]):
            if (a[0] - x) * (b[0] - x) <= 0.0 and a[0] != b[0]:
                n += 1
    return n


bad_cols = []
for x in (-90.3, -40.3, 0.7, 40.3, 90.3):
    flips = 0
    prev = None
    y = -50.0
    while y <= 320.0:
        s = wf.det_sign((x, y))
        if prev is not None and s != prev:
            flips += 1
        prev = s
        y += 0.05
    t = crossings_at(x)
    if t != flips:
        bad_cols.append((x, t, flips))
assert not bad_cols, bad_cols
print(f"7) composite fold: {folded} folded probes, orientation vs det 0 "
      f"disagreements, {len(loci)} loci matching the field on every column")

# 8) REVIEW F1: chain-rule det_sign. A steep stage 1 (gradient ~12) next
#    to a small stage 2: the base-space composite difference straddled the
#    small band and reported phantom folds; the chain-rule form must agree
#    with a fine-step converged determinant everywhere probed.
g1 = {"points": [(-10.0, 0.0), (10.0, 0.0)], "width": 2.5}
g1_m = {"points": [(-10.0, 200.0), (10.0, 200.0)], "width": 2.5}
g2 = {"points": [(-6.0, 3.0), (6.0, 3.0)], "width": 2.5}
g2_m = {"points": [(-6.0, 9.0), (6.0, 9.0)], "width": 2.5}
mp_g = build([(g1, g1_m), (g2, g2_m)])
wg = mp_g.warp


def fine_det(p, eps=0.004):
    ax, bx = wg.apply((p[0] + eps, p[1])), wg.apply((p[0] - eps, p[1]))
    ay, by = wg.apply((p[0], p[1] + eps)), wg.apply((p[0], p[1] - eps))
    return (((ax[0] - bx[0]) * (ay[1] - by[1])
             - (ay[0] - by[0]) * (ax[1] - bx[1])) / (4 * eps * eps))


mismatch = total = 0
for gx in range(-24, 25, 2):
    for gy in range(-8, 17, 1):
        p = (gx * 0.5, gy * 0.5)
        d = fine_det(p)
        if abs(d) < 0.1:
            continue
        total += 1
        if (d > 0) != (wg.det_sign(p) == 1):
            mismatch += 1
print(f"8) chain-rule det_sign vs converged determinant: "
      f"{mismatch}/{total} mismatches")
assert mismatch == 0, mismatch

# 9) REVIEW F2: a STRAIGHT main-drawn line over a standing FOLD band must
#    stay (near) neutral - the pullback finds a true preimage instead of
#    returning the target and encoding the residual as a fake ask.
fresh_boards()
c_f = {"points": [(-100.0, 40.0), (100.0, 40.0)], "width": 2.5}
m_f = {"points": [(-100.0, 190.0), (100.0, 190.0)], "width": 2.5}
am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY] = {
    "lines": [{"points": c_f["points"], "width": 2.5, "id": 0,
               "third": [list(p) for p in c_f["points"]]}]}
am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY] = {
    "lines": [{"points": m_f["points"], "width": 2.5, "id": 0,
               "third": [list(p) for p in m_f["points"]]}]}
assert am.run_additional_line_tool("main", [(-80.0, 100.0), (80.0, 100.0)])
mp_n, _ = am._mapper_from_assets()
assert len(mp_n.warp.stages) == 2
peak2 = mp_n.warp.stages[1][0]["peak"]
print(f"9) straight main line over a fold band: stage-2 peak {peak2:.3f} px "
      "(a non-preimage pullback once encoded the full fold residual)")
assert peak2 < 1.0, peak2

# 10) REVIEW F4/F6: DISJOINT lines pay no pad - their loci match a
#     single-line trace exactly, and a short later line's locus survives
#     two big earlier pushes.
p1 = {"points": [(-600.0, -100.0), (-400.0, -100.0)], "width": 2.5}
p1_m = {"points": [(-600.0, 50.0), (-400.0, 50.0)], "width": 2.5}
p2 = {"points": [(300.0, -8.0), (320.0, -8.0)], "width": 2.5}
p2_m = {"points": [(300.0, 12.0), (320.0, 12.0)], "width": 2.5}
mp_short = build([(p1, p1_m), (p2, p2_m)])
pads = [seg["prefix_pad"] for seg in mp_short.warp.stages[1]]
assert all(p == 0.0 for p in pads), pads  # disjoint: gate keeps pad 0
loci_pair = [c for c in mp_short.warp.fold_loci()
             if any(q[0] > 250.0 for q in c)]
mp_solo = build([(p2, p2_m)])
loci_solo = mp_solo.warp.fold_loci()
assert len(loci_pair) == len(loci_solo) and loci_solo, \
    (len(loci_pair), len(loci_solo))
n_pair = sum(len(c) for c in loci_pair)
n_solo = sum(len(c) for c in loci_solo)
assert n_pair == n_solo, (n_pair, n_solo)
print(f"10) short line after a big disjoint push: pad 0, "
      f"{len(loci_solo)} loci / {n_solo} pts, identical to the solo trace")

print("t_compose: ALL OK")
