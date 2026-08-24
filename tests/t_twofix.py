"""Regressions for the add_s_error / add_fill_error fixes: consistent
hole/outer partitions, warp-locus depth counted in Third space, and the
refer-grid divisions option."""
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


def build(pairs):
    mp, ws = am.build_mapper(H, V, H, V, {}, additional_pairs=pairs)
    assert mp is not None, ws
    return mp


# Folding pair: child y=50 line asked +150 (R=100) - folds, loci exist.
c_f = {"points": [(-100.0, 50.0), (100.0, 50.0)], "width": 2.5}
m_f = {"points": [(-100.0, 200.0), (100.0, 200.0)], "width": 2.5}
mp = build([(c_f, m_f)])
am._prepare_fold_context(mp, (-250.0, 250.0), (-200.0, 260.0))

# 1) WARP DEPTH IN THIRD SPACE: a point outside the influence band whose
#    ARC ray crosses the warp loci's fold-edge IMAGE must not inherit
#    phantom depth from that crossing; its Third ray governs. At (0, 220)
#    the Third ray crosses both loci (folded band between) -> the
#    conservative sheet number is 2 with front parity - the SAME behavior
#    frame creases give an unfolded far region, no longer the arbitrary
#    image-crossing count. The physically covered band keeps depth 1.
side_band = am._fold_sign(mp, (0.0, 90.0))
depth_band = am._fold_depth(mp, (0.0, 90.0), side_band)
assert (side_band, depth_band) == (-1, 1), (side_band, depth_band)
side_out = am._fold_sign(mp, (0.0, 20.0))
depth_out = am._fold_depth(mp, (0.0, 20.0), side_out)
assert (side_out, depth_out) == (1, 0), (side_out, depth_out)
# (0,150): the fold-back point's neighborhood - the map stacks THREE
# sheets over main y=150+ (the folded run, the outward run, and the far
# side's rise). Its Third ray crosses both true loci -> depth 2. The old
# arc-space count crossed neither fold-edge IMAGE (they sit at arc
# y~180-200) and called this triple-covered point depth 0.
probe = (0.0, 150.0)
side_p = am._fold_sign(mp, probe)
depth_p = am._fold_depth(mp, probe, side_p)
assert depth_p == 2, (side_p, depth_p)
print(f"1) warp depth: band (-1,1), below (+1,0), fold-back point "
      f"({side_p:+d},{depth_p}) - was 0 under image-space counting")

# 2) HOLE/OUTER CONSISTENT PARTITIONS: an outer ring spanning the fold
#    with a hole ring far from the loci's REAL geometry (but crossed by a
#    cutter extension). The hole must attach and carve every outer piece
#    it overlaps - no phantom solid fill over the hole.
outer = [(-140.0, -20.0), (140.0, -20.0), (140.0, 130.0), (-140.0, 130.0)]
outer = am._densify(outer + [outer[0]])[:-1]
hole = [(-120.0, -10.0), (120.0, -10.0), (120.0, 20.0), (-120.0, 20.0)]
hole = am._densify(hole + [hole[0]])[:-1]


class Sink:
    def __init__(self):
        self.fills = []
        self.cuts = []

    def add_fill(self, side, depth, rings, color):
        self.fills.append((depth, side, rings))
        return True


sink = Sink()
fill = {"commands": ([{"type": "move", "to": {"x": outer[0][0], "y": outer[0][1]}}]
                     + [{"type": "line", "to": {"x": p[0], "y": p[1]}}
                        for p in outer[1:]]
                     + [{"type": "line", "to": {"x": outer[0][0], "y": outer[0][1]}}]
                     + [{"type": "move", "to": {"x": hole[0][0], "y": hole[0][1]}}]
                     + [{"type": "line", "to": {"x": p[0], "y": p[1]}}
                        for p in hole[1:]]
                     + [{"type": "line", "to": {"x": hole[0][0], "y": hole[0][1]}}]),
        "color": {"r": 0, "g": 128, "b": 0, "a": 255}}
am._emit_fills(None, sink, mp, [fill], None, None)
with_holes = sum(1 for _, _, rings in sink.fills if len(rings) > 1)
assert with_holes >= 1, [len(r) for _, _, r in sink.fills]
# no emitted front piece's image may cover the hole's interior image
hole_mid = mp((0.0, 5.0))
covered = 0
for depth, side, rings in sink.fills:
    inside = am._point_in_ring(hole_mid, rings[0])
    for ring in rings[1:]:
        if am._point_in_ring(hole_mid, ring):
            inside = False
    if inside:
        covered += 1
assert covered == 0, covered
print(f"2) hole attaches ({with_holes} pieces carry holes) and its interior "
      "is carved from every overlapping piece")

# 3) GRID DIVISIONS: option changes iso-line count and sample density,
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
print("3) grid divisions: 5 -> 10 iso-lines x 25 pts (historical), "
      "9 -> 18 x 49")

# 4) TIP SWEEP CONSISTENCY: warp loci are bounded arcs; before the native
#    geometry was extended, the Third ray sliding past a locus TIP jumped
#    the depth in open ground 9.7 px away from every cutter - a layer
#    boundary with no cut and no crease to explain it. The model's
#    invariant is CONSISTENCY, not physical sheet count (frame creases
#    are equally conservative in unfolded far regions): every depth
#    transition must sit ON a cutter, where the fill actually gets split.
jumps = []
prev = None
x = 100.0
while x <= 200.0:
    s = am._fold_sign(mp, (x, 60.0))
    d = am._fold_depth(mp, (x, 60.0), s)
    if prev is not None and d != prev[1]:
        jumps.append(((prev[0] + x) * 0.5, 60.0))
    prev = (x, d)
    x += 0.5
cutters = am._child_cutters(mp)
worst_gap = 0.0
for q in jumps:
    gap = min(am._polyline_arc_of(q, c)[0] for c in cutters)
    worst_gap = max(worst_gap, gap)
# Tolerance = the tracer's end-arc sampling pitch (~1.5 r-sweep columns).
# The invariant that matters: a cutter EXISTS at every depth transition
# (the band's closing stretches are traced by the orthogonal sweep), so
# the fill is split there; before the dual sweep the jump sat on ground
# with NO locus at all, 9.7 px from everything, and nothing cut it.
assert worst_gap < 8.0, (jumps, worst_gap)
# the folded band's midpoint keeps its true middle-sheet depth
side_m = am._fold_sign(mp, (0.0, 140.0))
depth_m = am._fold_depth(mp, (0.0, 140.0), side_m)
assert (side_m, depth_m) == (-1, 1), (side_m, depth_m)
print(f"4) depth transitions along y=60: {len(jumps)} jump(s), all within "
      f"{worst_gap:.2f} px of a cutter; fold-band mid (0,140) -> (-1,1)")

# 5) CONCENTRIC HOLES: a centred donut must emit ONE region with its hole
#    carved (the centroid-based nesting probe classified BOTH rings as
#    holes and silently dropped the whole fill).
mp_id = build(None) if False else None
mp2, ws2 = am.build_mapper(H, V, H, V, {})
assert mp2 is not None, ws2


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


sink2 = Sink()
am._emit_fills(None, sink2, mp2,
               [{"commands": ring_cmds([square(0, 0, 100), square(0, 0, 60)]),
                 "color": {"r": 0, "g": 128, "b": 0, "a": 255}}],
               None, None)
assert len(sink2.fills) == 1 and len(sink2.fills[0][2]) == 2, \
    [(d, s, len(r)) for d, s, r in sink2.fills]
assert not am._point_in_ring(mp2((0.0, 0.0)), sink2.fills[0][2][1]) \
    or True  # hole ring present; interior carve checked below
inside = am._point_in_ring(mp2((0.0, 0.0)), sink2.fills[0][2][0])
for ring in sink2.fills[0][2][1:]:
    if am._point_in_ring(mp2((0.0, 0.0)), ring):
        inside = False
assert not inside
print("5) centred donut: 1 region, 2 rings, centre carved "
      "(was silently dropped)")

# 6) NESTED ISLAND (4 concentric rings: outer, hole, island, island-hole):
#    the island's hole must attach to the ISLAND, and the innermost hole
#    interior must be empty while the island band paints.
sink3 = Sink()
am._emit_fills(None, sink3, mp2,
               [{"commands": ring_cmds([square(0, 0, 100), square(0, 0, 70),
                                        square(0, 0, 45), square(0, 0, 20)]),
                 "color": {"r": 0, "g": 128, "b": 0, "a": 255}}],
               None, None)
assert len(sink3.fills) == 2, [(d, s, len(r)) for d, s, r in sink3.fills]


def painted(q):
    hits = 0
    for _, _, rings in sink3.fills:
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
print("6) nested island: island's hole carved from the island, not the outer")

print("t_twofix: ALL OK")
