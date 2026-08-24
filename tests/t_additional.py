"""Additional (pink) line TOOL contracts under the flow-field model.

The warp was rewritten (_FlowFieldWarp, user spec 2026-08-25): a pink pair
no longer carries an absolute displacement, it states a per-station
similarity J = tau_main / tau_child that steers the texture's FLOW, and one
fit-weighted Poisson solve integrates every line's ask into a single field.
What that changes for the cases below:

  * a pure TRANSLATION pair is INERT by design, so every fixture that must
    actually deform is BOWED, never merely shifted;
  * effects have smooth harmonic tails instead of a hard zero at R - far
    ground decays to sub-pixel, it does not snap to 1e-9;
  * drawing order is irrelevant and the field stays orientation-preserving,
    so there are no intentional warp folds left to assert;
  * the H and V axes are SACRED: no line weight inside the AXIS_GUARD halo,
    the straddling grid lines are hard-pinned, and the tent damps near the
    perpendicular axis. A fixture straddling an axis is muted on purpose, so
    every fixture and probe here that measures an EFFECT is kept clear of
    both axes (the standard bow lives at y = 120..160, x = 50..250).

t_flowfield.py pins the field's own machinery (translation inertness, flow
direction, axis sanctity, the tent, order-independence, the falloff option,
build cost). This file pins what the TOOL promises: pair management, the
partner sync, redraw-vs-append, orphans, guide edits, legacy assets.
"""
import copy
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


def build(pairs=None):
    mp, ws = am.build_mapper(H, V, H, V, {}, additional_pairs=pairs)
    assert mp is not None, ws
    return mp


def item(points, width=2.5):
    return {"points": [tuple(p) for p in points], "width": width}


def dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def close(a, b, tol=1e-6):
    return dist(a, b) <= tol


def moved(mp, p):
    return dist(mp(p), p)


def bow(x_lo, x_hi, y0, sag, count=33):
    """A parabolic bow of `sag` px over the chord (x_lo, y0) -> (x_hi, y0)."""
    return [(x_lo + (x_hi - x_lo) * k / (count - 1.0),
             y0 + sag * (1.0 - (2.0 * k / (count - 1.0) - 1.0) ** 2))
            for k in range(count)]


def reset_assets(h=H, v=V):
    am._MAPPING_ASSETS.clear()
    for view in ("child", "main"):
        am._MAPPING_ASSETS[view] = {
            am.H_PROPERTY: {"points": list(h), "width": 3.0},
            am.V_PROPERTY: {"points": list(v), "width": 3.0},
        }


def lines(view):
    return am._MAPPING_ASSETS[view][am.ADDITIONAL_PROPERTY]["lines"]


# The standard fixture: a 40 px bow over a 200 px chord, centred near
# (150, 120) - clear of both axes, so nothing here is guard-muted. Its
# partner is that chord.
SAG = 40.0
CHORD_Y = 120.0
BOW = bow(50.0, 250.0, CHORD_Y, SAG)
CHORD = [(p[0], CHORD_Y) for p in BOW]
APEX = BOW[len(BOW) // 2]                       # (150, 160)
BOW_ARC = am._cumulative_lengths(BOW)[-1]
BOW_CHORD = dist(BOW[0], BOW[-1])

am._ADDITIONAL["falloff"] = "linear"


# 1) no pairs -> no warp at all, exact identity.
mp0 = build()
assert mp0.warp is None
for p in [(-120.0, 150.0), (137.0, -140.0), (220.0, 130.0)]:
    assert close(mp0(p), p), (p, mp0(p))
print("1) no pairs: warp None, identity exact at 3 probes")

# 2) BOWED child line vs its straight chord partner. The flow ask is real
#    (the tangents differ station by station), so the drawn apex flattens
#    onto the chord; the tails are harmonic rather than clipped at R, so
#    ground beyond 2R has decayed to sub-pixel WITHOUT being exactly zero.
mp = build([(item(BOW), item(CHORD))])
assert mp.warp is not None and len(mp.warp.pairs) == 1
R = mp.warp.pairs[0]["radius"]
assert abs(R - 0.5 * max(BOW_CHORD, BOW_ARC)) < 0.5, R
image = mp(APEX)
flattened = (APEX[1] - image[1]) / SAG
assert flattened >= 0.30, (image, flattened)          # measured 1.01
assert image[1] >= CHORD_Y - 0.35 * SAG, image        # no runaway overshoot
assert abs(image[0] - APEX[0]) < 10.0, image          # measured 1.7 px
far = [(-250.0, 150.0), (-300.0, 150.0), (150.0, -150.0), (-200.0, -150.0)]
for p in far:
    assert am._polyline_arc_of(p, BOW)[0] >= 2.0 * R, p
far_worst = max(moved(mp, p) for p in far)
assert far_worst < 1.5, far_worst                     # measured 0.18
tail = moved(mp, (-250.0, 150.0))                     # 2.7 R off, same side
assert 0.01 < tail < 1.5, tail                        # measured 0.18: a
#    DECAYING tail, not the old hard cut-off at R (which read 1e-9 here).
# The public surface the rest of the pipeline binds to:
assert mp.warp.has_faces is False and mp.warp.face_at(APEX) == 0
assert mp.warp.det_sign(APEX) == 1 and mp.warp.fold_loci() == []
print(f"2) bowed pair: R={R:.1f} (= 0.5*arc), apex {APEX[1]:.0f} -> "
      f"{image[1]:.1f} ({100.0 * flattened:.0f}% onto the chord); ground at "
      f"2R moves {far_worst:.2f} px (tail {tail:.2f}, not zero); no folds")

# 3) the single global field and its inverse are mutual inverses, and the
#    mapper's own inverse lands an on-line image back on the line.
warp = mp.warp
round_trip = max(dist(warp.unapply(warp.apply(p)), p) for p in
                 [APEX, (150.0, 190.0), (60.0, 130.0), (-150.0, -120.0)])
assert round_trip < 1e-5, round_trip                  # measured 5.1e-10
back = mp.inverse(mp(APEX))
assert close(back, APEX, 1e-3), back
print(f"3) unapply(apply(p)) == p (worst {round_trip:.1e}); mapper.inverse "
      "lands the drawn line's image back on it")

# 4) fold-space consistency: _arc_of_point vs _child_of_arcs round-trip
#    through the warped mapping.
arc_worst = 0.0
for probe in [(150.0, 150.0), (200.0, 130.0), (-150.0, 140.0)]:
    arcs = am._arc_of_point(mp, probe)
    arc_worst = max(arc_worst, dist(am._child_of_arcs(mp, arcs), probe))
assert arc_worst < 1e-4, arc_worst                    # measured 8.9e-10
print(f"4) _arc_of_point / _child_of_arcs invert each other under the warp "
      f"(worst {arc_worst:.1e})")

# 5) TOOL ENTRY: drawing a CURVED line on the child board makes one line on
#    each board, the main partner being that line's STRAIGHTENED CHORD in
#    rendering space; a redraw near it replaces, a distant draw appends.
reset_assets()
assert am.run_additional_line_tool("child", BOW)
assert len(lines("child")) == 1 and len(lines("main")) == 1
partner = lines("main")[0]["points"]
chord_gap = max(am._polyline_arc_of(p, CHORD)[0] for p in partner)
assert chord_gap < 1.5, chord_gap                     # measured 0.00
assert am.run_additional_line_tool("child", bow(55.0, 255.0, 122.0, 36.0))
assert len(lines("child")) == 1 and len(lines("main")) == 1, "redraw appended"
assert am.run_additional_line_tool("child", [(-260.0, -150.0), (-60.0, -150.0)])
assert len(lines("child")) == 2 and len(lines("main")) == 2, "far draw replaced"
print(f"5) curved child draw: partner is the chord ({chord_gap:.2f} px off), "
      "a redraw replaces it, a distant draw appends")

# 6) drawing on MAIN syncs the child partner through the mapping's inverse;
#    with identity boards that partner is the drawn line's chord.
reset_assets()
assert am.run_additional_line_tool("main", BOW)
assert len(lines("child")) == 1 and len(lines("main")) == 1
synced = lines("child")[0]["points"]
sync_gap = max(am._polyline_arc_of(p, CHORD)[0] for p in synced)
assert sync_gap < 1.5, sync_gap                       # measured 0.00
print(f"6) main-side draw syncs a child partner on the chord "
      f"({sync_gap:.2f} px off)")

# 7) an UNTOUCHED pair (the drawn line and its partner are the same line,
#    J = I) asks for nothing: it is NEUTRAL and stays out of the field
#    entirely - alone it is the exact identity, and appended beside a
#    standing warp it leaves that warp's field bit-alone, even when its
#    own band would cross the tuned one (pre-fix a J = I line laid across
#    the bow's band bent it by 64.6 px through the fit-gain weighting).
untouched = [(-280.0, -150.0), (-100.0, -150.0)]
solo = build([(item(untouched), item(untouched))])
assert solo.warp is not None and len(solo.warp.pairs) == 1
probes7 = [APEX, (150.0, 150.0), (220.0, 130.0)]
solo_worst = max(moved(solo, p) for p in probes7 + [(-190.0, -150.0)])
assert solo_worst < 1e-6, solo_worst                  # measured 5.7e-14
standing = build([(item(BOW), item(CHORD))])
appended = build([(item(BOW), item(CHORD)), (item(untouched), item(untouched))])
append_worst = max(dist(appended(p), standing(p)) for p in probes7)
assert append_worst < 0.1, append_worst               # measured 4.8e-06
# The hard half of the contract: a straight untouched pair whose band
# CROSSES the tuned bow must still change nothing (the neutral skip).
across = [(50.0, 20.0), (250.0, 20.0)]
crossed = build([(item(BOW), item(CHORD)), (item(across), item(across))])
cross_worst = max(dist(crossed(p), standing(p)) for p in probes7)
assert cross_worst < 0.1, cross_worst                 # pre-fix 64.6 px
print(f"7) an untouched pair is inert: identity alone ({solo_worst:.1e} px), "
      f"beside ({append_worst:.1e} px) and across ({cross_worst:.1e} px) "
      f"a standing warp")

# 8) ORPHAN half-pairs (a one-board undo) contribute NOTHING - only
#    complete pairs build the warp, and the lost pair's deformation goes
#    with it.
reset_assets()
am.run_additional_line_tool("child", bow(-260.0, -60.0, -150.0, 30.0))
am.run_additional_line_tool("child", BOW)
am.run_additional_line_tool("child", bow(-260.0, -60.0, 150.0, 30.0))
assert len(lines("child")) == 3 and len(lines("main")) == 3
mp_full, _ = am._mapper_from_assets()
assert len(mp_full.warp.pairs) == 3
full_pull = moved(mp_full, APEX)
assert full_pull > 10.0, full_pull                    # measured 40 px
del lines("child")[1]                                 # a lost undo half
pairs = am._additional_pairs()
assert pairs is not None and len(pairs) == 2, pairs
ids = sorted(am._line_id(c, -1) for c, m in pairs)
assert ids == [0, 2], ids                             # never re-matched
mp_desync, _ = am._mapper_from_assets()
assert len(mp_desync.warp.pairs) == 2
widowed = moved(mp_desync, APEX)
assert widowed < 1.0, widowed                         # measured 0.14
print(f"8) orphaned half-pairs are ignored; the widowed line stops deforming "
      f"({full_pull:.0f} px -> {widowed:.2f} px at its apex)")

# 9) a centre-guide edit retires every stored "third" on BOTH boards.
reset_assets()
am.run_additional_line_tool("child", BOW)
assert "third" in lines("child")[0] and "third" in lines("main")[0]
am.run_center_line_tool("child", am.H_PROPERTY,
                        [(-300.0, -40.0), (300.0, -40.0)])
for view in ("child", "main"):
    for line in lines(view):
        assert "third" not in line, (view, sorted(line))
print("9) guide edit drops stale thirds on both boards")

# 10) proximity rule: a short line drawn far from a long standing one
#     APPENDS instead of silently redrawing it.
WIDE_H = [(-1200.0, 0.0), (1200.0, 0.0)]
reset_assets(WIDE_H, V)
am.run_additional_line_tool("child", [(-1000.0, 200.0), (1000.0, 200.0)])
am.run_additional_line_tool("child", [(-60.0, -120.0), (60.0, -120.0)])
assert len(lines("child")) == 2 and len(lines("main")) == 2, len(lines("child"))
print("10) a 120 px line 320 px away appends instead of replacing a 2000 px "
      "line")

# 11) redrawing the SAME shape right-to-left is one line, not two, the
#     stored geometry is the shape that was drawn, and the field itself is
#     parameterization-direction invariant (same pair, both sides sampled
#     backwards -> the same mapping), and _prepare's chord-dot alignment
#     makes the TOOL-LEVEL redraw a no-op too: a child line redrawn
#     backwards against its untouched forwards partner reads as the same
#     ask, not a 180 degree one (pre-fix 70.9 px of drift).
reset_assets()
wavy = [(50.0, 120.0), (90.0, 144.0), (130.0, 152.0), (170.0, 142.0),
        (210.0, 128.0), (250.0, 120.0)]
am.run_additional_line_tool("child", wavy)
probes11 = [(90.0, 144.0), (130.0, 152.0), (210.0, 128.0), (150.0, 110.0)]
mp_fwd, _ = am._mapper_from_assets()
before11 = [mp_fwd(p) for p in probes11]
main11 = [tuple(p) for p in lines("main")[0]["points"]]
am.run_additional_line_tool("child", list(reversed(wavy)))
assert len(lines("child")) == 1 and len(lines("main")) == 1
assert max(am._polyline_arc_of(p, wavy)[0]
           for p in lines("child")[0]["points"]) < 1e-6
assert max(am._polyline_arc_of(p, main11)[0]
           for p in lines("main")[0]["points"]) < 1e-6   # partner untouched
mp_rev, _ = am._mapper_from_assets()
drift11 = max(dist(mp_rev(p), q) for p, q in zip(probes11, before11))
assert drift11 < 1.5, drift11                         # pre-fix 70.9 px
aligned = build([(item(list(reversed(wavy))), item(list(reversed(main11))))])
aligned_drift = max(dist(aligned(p), q) for p, q in zip(probes11, before11))
assert aligned_drift < 1.5, aligned_drift             # measured 0.53
print(f"11) reversed redraw is a no-op: {drift11:.2f} px at the tool level, "
      f"{aligned_drift:.2f} px with both sides sampled backwards")

# 12) legacy id-less assets get positional ids stamped by the load path;
#     removing one pair keeps the survivor working.
reset_assets()
legacy = {
    "child": [item([(-280.0, -150.0), (-100.0, -150.0)]), item(BOW)],
    "main": [item([(-280.0, -150.0), (-100.0, -150.0)]), item(CHORD)],
}
for view, stored in legacy.items():
    asset = {"lines": copy.deepcopy(stored)}
    for index, line in enumerate(asset["lines"]):
        assert "id" not in line
        line.setdefault("id", index)   # what the load path stamps
    am._MAPPING_ASSETS[view][am.ADDITIONAL_PROPERTY] = asset
assert len(am._additional_pairs()) == 2
mp_legacy, _ = am._mapper_from_assets()
legacy_flat = (APEX[1] - mp_legacy(APEX)[1]) / SAG
assert legacy_flat >= 0.30, legacy_flat
am._overlay_removed({}, {}, {"overlay": {"id": f"{am.ADDITIONAL_PROPERTY}:0"},
                             "view": "child"})
survivors = am._additional_pairs()
assert survivors is not None and len(survivors) == 1
assert sorted(am._line_id(c, -1) for c, m in survivors) == [1]
mp_after, _ = am._mapper_from_assets()
after_flat = (APEX[1] - mp_after(APEX)[1]) / SAG
assert after_flat >= 0.30, after_flat
assert abs(after_flat - legacy_flat) < 0.05, (legacy_flat, after_flat)
print(f"12) legacy ids stamped; deleting one pair keeps the survivor "
      f"({100.0 * after_flat:.0f}% flattening, unchanged)")

print("t_additional: ALL OK")
