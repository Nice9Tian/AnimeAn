"""Regressions for the add_s_error / add_fill_error fixes, re-based on the
flow-field model: consistent hole/outer partitions over a MAIN-FRAME fold,
concentric-ring nesting, and the refer-grid divisions option.

The warp itself no longer folds (the additional lines are a Poisson-
integrated flow field: conflicts resolve by stress minimisation, never by
doubling the sheet back), so the old warp-fold depth cases are gone with the
phenomenon they measured. What survives is the frame-fold half of the same
machinery - creases from the MAIN guides - plus the new coexistence rule:
a pink line and a folded main frame must live on one mapper without either
inventing a warp fold."""
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
# The bent main V of t_sever's case 10: the child side stays straight (nothing
# severs) while the main frame creases - the main-frame fold this suite needs.
BENT_MAIN_V = [(0.0, -200.0), (0.0, 100.0), (-190.0, 40.0)]
am._ADDITIONAL["falloff"] = "linear"


def build(main_v=None, pairs=None):
    mp, ws = am.build_mapper(H, V, H, main_v or V, {}, additional_pairs=pairs)
    assert mp is not None, ws
    return mp


class Sink:
    """_emit_fills' output surface: add_fill(side, depth, rings, color)."""

    def __init__(self):
        self.fills = []
        self.cuts = []

    def add_fill(self, side, depth, rings, color):
        self.fills.append((side, depth, rings))
        return True


def ring_cmds(rings):
    cmds = []
    for ring in rings:
        cmds.append({"type": "move", "to": {"x": ring[0][0], "y": ring[0][1]}})
        for p in ring[1:]:
            cmds.append({"type": "line", "to": {"x": p[0], "y": p[1]}})
        cmds.append({"type": "line", "to": {"x": ring[0][0], "y": ring[0][1]}})
    return cmds


def square(cx, cy, r):
    return [(cx - r, cy - r), (cx + r, cy - r), (cx + r, cy + r), (cx - r, cy + r)]


def rect(x0, y0, x1, y1):
    return [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]


def straight(x0, x1, y, n=21):
    return [(x0 + (x1 - x0) * k / (n - 1.0), y) for k in range(n)]


def bow(x0, x1, y, sag, n=21):
    """A parabolic arc over the chord (x0,y)-(x1,y) with the given sagitta."""
    mid, half = 0.5 * (x0 + x1), 0.5 * (x1 - x0)
    return [(x, y + sag * (1.0 - ((x - mid) / half) ** 2))
            for x, _ in straight(x0, x1, y, n)]


def line_asset(points, line_id=0):
    """A drawn pair side with AUTHORITATIVE Third coordinates - the contract
    every stored line carries (build_mapper never re-derives them)."""
    return {"points": [tuple(p) for p in points], "width": 2.5,
            "id": line_id, "third": [tuple(p) for p in points]}


# 1) GRID DIVISIONS: option changes iso-line count and sample density,
#    default reproduces the historical 5x5/25 lattice.
am._MAPPING_ASSETS.clear()
for view in ("child", "main"):
    am._MAPPING_ASSETS[view] = {
        am.H_PROPERTY: {"points": list(H), "width": 3.0},
        am.V_PROPERTY: {"points": list(V), "width": 3.0},
    }
am._REFER_RECT["child"] = True
am._invalidate_grid_cache()
items5 = am._grid_overlay_items("child")
assert len(items5) == 10 and len(items5[0]["points"]) == 25, \
    (len(items5), len(items5[0]["points"]))
am._GRID["divisions"] = 9
am._invalidate_grid_cache()
items9 = am._grid_overlay_items("child")
assert len(items9) == 18 and len(items9[0]["points"]) == 49, \
    (len(items9), len(items9[0]["points"]))
am._GRID["divisions"] = 5
am._invalidate_grid_cache()
am._REFER_RECT["child"] = False
print("1) grid divisions: 5 -> 10 iso-lines x 25 pts (historical), "
      "9 -> 18 x 49")

# 2) CONCENTRIC HOLES: a centred donut must emit ONE region with its hole
#    carved (the centroid-based nesting probe classified BOTH rings as
#    holes and silently dropped the whole fill).
mp2 = build()
sink2 = Sink()
am._emit_fills(None, sink2, mp2,
               [{"commands": ring_cmds([square(0, 0, 100), square(0, 0, 60)]),
                 "color": {"r": 0, "g": 128, "b": 0, "a": 255}}],
               None, None)
assert len(sink2.fills) == 1 and len(sink2.fills[0][2]) == 2, \
    [(s, d, len(r)) for s, d, r in sink2.fills]
inside = am._point_in_ring(mp2((0.0, 0.0)), sink2.fills[0][2][0])
for ring in sink2.fills[0][2][1:]:
    if am._point_in_ring(mp2((0.0, 0.0)), ring):
        inside = False
assert not inside
print("2) centred donut: 1 region, 2 rings, centre carved "
      "(was silently dropped)")

# 3) NESTED ISLAND (4 concentric rings: outer, hole, island, island-hole):
#    the island's hole must attach to the ISLAND, and the innermost hole
#    interior must be empty while the island band paints.
sink3 = Sink()
am._emit_fills(None, sink3, mp2,
               [{"commands": ring_cmds([square(0, 0, 100), square(0, 0, 70),
                                        square(0, 0, 45), square(0, 0, 20)]),
                 "color": {"r": 0, "g": 128, "b": 0, "a": 255}}],
               None, None)
assert len(sink3.fills) == 2, [(s, d, len(r)) for s, d, r in sink3.fills]


def painted(q):
    hits = 0
    for _side, _depth, rings in sink3.fills:
        inside = am._point_in_ring(q, rings[0])
        for ring in rings[1:]:
            if am._point_in_ring(q, ring):
                inside = False
        if inside:
            hits += 1
    return hits


assert painted(mp2((0.0, 90.0))) == 1    # outer band
assert painted(mp2((0.0, 55.0))) == 0    # hole band
assert painted(mp2((0.0, 30.0))) == 1    # island band
assert painted(mp2((0.0, 0.0))) == 0     # island's hole
print("3) nested island: island's hole carved from the island, not the outer")

# 4) HOLE/OUTER CONSISTENT PARTITIONS OVER A MAIN-FRAME FOLD. The bent main
#    V creases the sheet along child y ~ 66.8; a donut straddling that crease
#    has BOTH its rings cut by it. Outer and hole must be split against the
#    SAME cutters (the fill's own bbox gate), or a hole piece straddles two
#    outer pieces, attaches to only one, and the other paints solid over the
#    hole. Assertions: some emitted piece still carries a hole ring, the fold
#    really did cut (front AND back pieces exist), and the hole's interior
#    image is covered by NO front piece on either side of the crease.
mp_fold = build(BENT_MAIN_V)
assert not mp_fold.can_fold()      # child side straight: nothing severs
am._prepare_fold_context(mp_fold, (-250.0, 250.0), (-200.0, 200.0))
crease_y = 66.8
assert am._fold_sign(mp_fold, (0.0, crease_y - 20.0)) == 1
assert am._fold_sign(mp_fold, (0.0, crease_y + 20.0)) == -1
outer = rect(-140.0, -20.0, 140.0, 130.0)
hole = rect(-120.0, 40.0, 120.0, 100.0)     # straddles the crease
sink4 = Sink()
am._emit_fills(None, sink4, mp_fold,
               [{"commands": ring_cmds([outer, hole]),
                 "color": {"r": 0, "g": 128, "b": 0, "a": 255}}],
               None, None)
sides4 = sorted(side for side, _d, _r in sink4.fills)
assert sides4 == [-1, 1], [(s, d, len(r)) for s, d, r in sink4.fills]
with_holes = sum(1 for _s, _d, rings in sink4.fills if len(rings) > 1)
assert with_holes >= 1, [len(r) for _s, _d, r in sink4.fills]
covered = 0
for probe in ((0.0, 50.0), (0.0, 70.0), (0.0, 95.0), (-90.0, 55.0),
              (90.0, 90.0)):
    image = mp_fold(probe)
    for side, _depth, rings in sink4.fills:
        if side != am._MappedOutput.FRONT:
            continue
        inside = am._point_in_ring(image, rings[0])
        for ring in rings[1:]:
            if am._point_in_ring(image, ring):
                inside = False
        if inside:
            covered += 1
assert covered == 0, covered
print(f"4) main-fold donut: {len(sink4.fills)} pieces (front+back), "
      f"{with_holes} carry holes; the hole's interior is covered by "
      f"{covered} front pieces on either side of the crease")

# 5) COEXISTENCE: a curved additional pair (off-axis, well clear of the H/V
#    axes the field holds sacred) on the SAME mapper as the folded main
#    frame. The two mechanisms are independent - the frame crease still
#    splits a crossing stroke front/back, while the flow field stays
#    orientation-preserving: under the flow model occlusion comes from the
#    frames, and the warp has no folds of its own to contribute.
child_line = line_asset(straight(130.0, 280.0, 130.0))
main_line = line_asset(bow(130.0, 280.0, 130.0, 20.0))
mp5 = build(BENT_MAIN_V, [(child_line, main_line)])
warp = mp5.warp
assert warp is not None and len(warp.pairs) == 1
effect = max(math.hypot(*warp.displacement((x, 130.0)))
             for x in (160.0, 200.0, 250.0))
assert effect > 5.0, effect                       # the line really speaks
assert warp.fold_loci() == [], warp.fold_loci()   # ... and never folds
assert warp._all_positive
axis_drift = max(math.hypot(*(a - b for a, b in zip(warp.apply(p), p)))
                 for p in [(0.0, 0.0), (0.0, 150.0), (200.0, 0.0),
                           (-250.0, 0.0), (0.0, -100.0)])
assert axis_drift < 0.01, axis_drift
crossing = am._densify([(200.0, -150.0), (200.0, 150.0)])
sides5 = [side for _run, side in am._split_by_fold(mp5, crossing)]
assert -1 in sides5 and 1 in sides5, sides5
far = [side for _run, side in
       am._split_by_fold(mp5, am._densify([(-100.0, -150.0), (-100.0, 150.0)]))]
assert -1 in far and 1 in far, far
print(f"5) pink line ({effect:.1f} px on-line, no fold loci, axes still to "
      f"{axis_drift:.0e} px) coexists with the folded main frame: crossing "
      f"strokes still split {sides5} / {far}")

print("t_twofix: ALL OK")
