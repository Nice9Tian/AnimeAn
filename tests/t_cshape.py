"""C-strategy: box detection, deformation vertices, faces, stacking bump."""
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
V_MAIN = [(0.0, -200.0), (-60.0, -100.0), (-80.0, 0.0),
          (-60.0, 100.0), (0.0, 200.0)]


def arc_points(cx, cy, radius, a0_deg, a1_deg, count=65):
    a0 = math.radians(a0_deg)
    a1 = math.radians(a1_deg)
    return [(cx + radius * math.cos(a0 + (a1 - a0) * k / (count - 1)),
             cy + radius * math.sin(a0 + (a1 - a0) * k / (count - 1)))
            for k in range(count)]


def chord_of(points):
    cum = am._cumulative_lengths(points)
    head, tail = points[0], points[-1]
    return [(head[0] + (tail[0] - head[0]) * cum[k] / cum[-1],
             head[1] + (tail[1] - head[1]) * cum[k] / cum[-1])
            for k in range(len(points))]


def densify(corners, per_leg=20):
    out = []
    for a, b in zip(corners, corners[1:]):
        for k in range(per_leg):
            t = k / float(per_leg)
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    out.append(corners[-1])
    return out


def build(pairs, main_v=V):
    am._MAPPING_ASSETS.clear()
    mp, ws = am.build_mapper(H, V, H, main_v, {}, additional_pairs=pairs)
    assert mp is not None, ws
    return mp


# 1) gate stays closed on the endpoint-owned shapes
assert am._c_shape_vertices([(0.0, 0.0), (300.0, 300.0)]) is None
wobbly = [(x, 0.02 * x + 3.0 * math.sin(x * 0.7)) for x in
          [3.0 * k for k in range(101)]]
assert am._c_shape_vertices(wobbly) is None                # tremor only
bow = arc_points(0.0, 0.0, 100.0, 200.0, 340.0)            # plain smile
assert am._c_shape_vertices(bow) is None                   # one interior extreme
rise = ([(3.0 * k, 3.0 * k) for k in range(34)]            # monotone rise with
        + [(102.0 + k, 150.0 - 0.9 * k) for k in range(34)]  # a REAL interior
        + [(136.0 + 3.0 * k, 120.0 + 3.4 * k) for k in range(35)])  # wiggle
assert am._c_shape_vertices(rise) is None    # box edges are still endpoints
closed = arc_points(0.0, 0.0, 100.0, 10.0, 355.0)
assert am._c_shape_vertices(closed) is None                # near-closed
print("1) gate: straight/tremor/bow/wiggly-rise/near-closed all refused")

# 2) the 270-degree C fires at its two turning points
c270 = arc_points(150.0, 0.0, 100.0, 45.0, 315.0)
fracs = am._c_shape_vertices(c270)
assert fracs is not None and len(fracs) == 2, fracs
assert abs(fracs[0] - 1.0 / 6.0) < 0.03, fracs
assert abs(fracs[1] - 5.0 / 6.0) < 0.03, fracs
print("2) 270-degree C: deformation vertices at 1/6 and 5/6")

# 3) S shape: the surviving axis pair gives the two inflection-side turns
s_pts = [(-100.0 + 200.0 * k / 100.0,
          60.0 * math.sin(math.pi * (-1.0 + 2.0 * k / 100.0)))
         for k in range(101)]
fr = am._c_shape_vertices(s_pts)
assert fr is not None and len(fr) == 2, fr
assert 0.15 < fr[0] < 0.40 and 0.60 < fr[1] < 0.85, fr
print("3) S shape: two vertices at the crest and the trough")

# 4) determinism: sub-tremor perturbation never moves the plan
seed = 12345
noisy = []
for x, y in c270:
    seed = (seed * 1103515245 + 12345) % (2 ** 31)
    dx = (seed / 2 ** 31 - 0.5) * 2.0
    seed = (seed * 1103515245 + 12345) % (2 ** 31)
    dy = (seed / 2 ** 31 - 0.5) * 2.0
    noisy.append((x + dx, y + dy))
fn = am._c_shape_vertices(noisy)
assert fn is not None and len(fn) == 2
assert abs(fn[0] - fracs[0]) < 0.02 and abs(fn[1] - fracs[1]) < 0.02, fn
print("4) 1 px noise: same axis, vertices within 2% arc")

# 5) MAIN-drawn C over its straight chord: the child side is
#    chord-monotone, so the pair keeps ONE verbatim-tracing sweep
#    (cutting it degraded the tracing - review finding), and with no
#    front evidence there are no labels either.
child_item = {"points": chord_of(c270), "width": 2.5}
main_item = {"points": list(c270), "width": 2.5}
mp = build([(child_item, main_item)])
assert mp.warp is not None and len(mp.warp.pairs) == 1, len(mp.warp.pairs)
assert not mp.warp.has_faces
assert any("reads as a C" in n and "no front evidence" in n
           for n in mp.additional_notes), mp.additional_notes
worst = max(math.hypot(*(a - b for a, b in
                         zip(mp.warp.apply(c), m)))
            for c, m in zip(child_item["points"], main_item["points"]))
assert worst < 2.5, worst
print("5) main-drawn C: 1 sweep, verbatim tracing %.2f px, no labels" % worst)

# 6) bowed main V guide -> window labels on the single sweep:
#    the belly (closer to the bow) is convex/front
mp2 = build([(child_item, main_item)], V_MAIN)
assert mp2.warp is not None and mp2.warp.has_faces
assert len(mp2.warp.pairs) == 1
assert any("front side by the bowing main guide" in n
           for n in mp2.additional_notes), mp2.additional_notes
spans = mp2.warp.pairs[0].get("face_spans")
assert spans is not None and list(spans[1]) == [-1, 1, -1], spans
assert mp2.warp.face_at((220.7, 0.0)) == 1      # deep belly window
assert mp2.warp.face_at((220.7, 60.0)) == -1    # arm window
assert mp2.warp.face_at((220.7, -60.0)) == -1   # other arm window
assert mp2.warp.face_at((1200.0, 900.0)) == 0   # outside every band
print("6) bowed main guide: windows [-1,1,-1], face_at signs correct")

# 7) CHILD-drawn C (the bend on the child side): the plan cuts at the
#    deformation vertices - comparable radii, per-piece labels
child_c = {"points": list(c270), "width": 2.5}
main_chord = {"points": chord_of(c270), "width": 2.5}
mp3 = build([(child_c, main_chord)], V_MAIN)
assert mp3.warp is not None and mp3.warp.has_faces
assert len(mp3.warp.pairs) >= 3, len(mp3.warp.pairs)
radii = [p["radius"] for p in mp3.warp.pairs]
assert min(radii) > 25.0, radii        # no sliver cascade (old floor: 5.2)
faces = sorted({p["face"] for p in mp3.warp.pairs})
assert faces == [-1, 1], faces
assert mp3.warp.face_at((50.0, 0.0)) == 1       # the belly of the drawn C
assert mp3.warp.face_at((220.7, -65.0)) == -1   # inside an arm's span
print("7) child-drawn C: %d runs, min radius %.1f, per-piece labels"
      % (len(mp3.warp.pairs), min(radii)))

# 8) the red handle outranks the guide - but only a MOVED handle:
#    the auto-created crossing default is not evidence
am._MAPPING_ASSETS.clear()
am._MAPPING_ASSETS["main"] = {am.NEAREST_PROPERTY: {"arc": (0.0, 0.0)}}
mp4, _ = am.build_mapper(H, V, H, V_MAIN, {},
                         additional_pairs=[(child_c, main_chord)])
assert any("front side by the bowing main guide" in n
           for n in mp4.additional_notes), mp4.additional_notes
am._MAPPING_ASSETS.clear()   # a handle far outside every PIECE's band
am._MAPPING_ASSETS["main"] = {am.NEAREST_PROPERTY: {"arc": (60.0, 170.0)}}
mp4b, _ = am.build_mapper(H, V, H, V_MAIN, {},
                          additional_pairs=[(child_item, main_item)])
assert any("front side by the bowing main guide" in n
           for n in mp4b.additional_notes), mp4b.additional_notes
am._MAPPING_ASSETS.clear()
am._MAPPING_ASSETS["main"] = {am.NEAREST_PROPERTY: {"arc": (230.0, 90.0)}}
mp5, _ = am.build_mapper(H, V, H, V_MAIN, {},
                         additional_pairs=[(child_c, main_chord)])
am._MAPPING_ASSETS.clear()
assert any("front side by the red handle" in n
           for n in mp5.additional_notes), mp5.additional_notes
assert mp5.warp.face_at((220.7, 65.0)) == 1     # the arm the handle sits on
assert mp5.warp.face_at((50.0, 0.0)) == -1      # belly demoted to back
print("8) red handle: crossing default ignored, moved handle overrules")

# 9) _fold_depth: the parity bump direction follows the face label
mp2.depth_curves = [[(30.0, -10000.0), (30.0, 10000.0)]]
mp2.depth_anchor = (0.0, 0.0)
mp2.warp_curve_third = {}
mp2.depth_anchor_third = None
FRONT = am._MappedOutput.FRONT
d_convex = am._fold_depth(mp2, (220.7, 0.0), FRONT)
d_concave = am._fold_depth(mp2, (220.7, 60.0), FRONT)
assert d_convex == 0, d_convex     # face +1: resolves TOWARD the viewer
assert d_concave == 2, d_concave   # face -1: the old +1 bump stands
assert d_convex % 2 == d_concave % 2 == 0    # parity preserved either way
print("9) parity bump: convex 0, concave 2, parity intact")

# 10) direction invariance: redrawing one side backwards changes nothing
asym = []
for a, b in zip([(-200.0, 0.0), (-160.0, 100.0), (-120.0, -100.0)],
                [(-160.0, 100.0), (-120.0, -100.0), (200.0, -50.0)]):
    asym += [(a[0] + (b[0] - a[0]) * k / 24.0,
              a[1] + (b[1] - a[1]) * k / 24.0) for k in range(24)]
asym.append((200.0, -50.0))
probes = [(x, y) for x in range(-240, 260, 60) for y in range(-140, 160, 60)]


def drift(pairs_a, pairs_b, main_v=V):
    ma = build(pairs_a, main_v)
    mb = build(pairs_b, main_v)
    return max(math.hypot(*(p - q for p, q in zip(ma(pt), mb(pt))))
               for pt in probes)

# (a) main-drawn asymmetric C, child chord redrawn right-to-left
fwd = {"points": chord_of(asym), "width": 2.5}
rev = {"points": list(reversed(chord_of(asym))), "width": 2.5}
d_a = drift([(fwd, {"points": list(asym), "width": 2.5})],
            [(rev, {"points": list(asym), "width": 2.5})])
assert d_a < 0.5, d_a
# (b) child-drawn C, main chord redrawn right-to-left (the cut path)
d_b = drift([({"points": list(asym), "width": 2.5}, fwd)],
            [({"points": list(asym), "width": 2.5}, rev)])
assert d_b < 0.5, d_b
print("10) reversed redraws: drift %.3f / %.3f px (legacy contract holds)"
      % (d_a, d_b))

# 11) face stability under reversal: 4-piece zigzag (even piece count)
zig = densify([(-200.0, 0.0), (-100.0, 60.0), (0.0, -60.0),
               (100.0, 60.0), (200.0, 0.0)])
zc_f = {"points": chord_of(zig), "width": 2.5}
zc_r = {"points": list(reversed(chord_of(zig))), "width": 2.5}
zm = {"points": list(zig), "width": 2.5}
mza = build([(zc_f, zm)], V_MAIN)
mzb = build([(zc_r, zm)], V_MAIN)
zig_probes = [(-150.0, 30.0), (-50.0, -30.0), (50.0, -30.0), (150.0, 30.0)]
la = [mza.warp.face_at(p) for p in zig_probes]
lb = [mzb.warp.face_at(p) for p in zig_probes]
assert la == lb, (la, lb)
assert sorted(set(la)) == [-1, 1], la
print("11) zigzag faces stable under reversed child: %s" % la)

# 12) escape hatch: grouping stays, labels vanish
am._ADDITIONAL["face_stacking"] = False
mp6 = build([(child_c, main_chord)], V_MAIN)
am._ADDITIONAL["face_stacking"] = True
assert mp6.warp is not None and len(mp6.warp.pairs) >= 3
assert not mp6.warp.has_faces
print("12) face_stacking off: same cuts, no labels")

# 13) legacy 2-tuples and planless composition stay intact
w_legacy = am._AdditionalWarp([(chord_of(c270), list(c270))])
assert w_legacy.pairs and not w_legacy.has_faces
straight_child = {"points": [(-100.0, -150.0), (100.0, -150.0)], "width": 2.5}
straight_main = {"points": [(-100.0, -120.0), (100.0, -120.0)], "width": 2.5}
mp7 = build([(child_c, main_chord), (straight_child, straight_main)])
assert mp7.warp is not None
stage2 = mp7.warp.stages[1]
assert all(seg["face"] == 0 for seg in stage2)
print("13) legacy tuples fine; a straight second line stays unlabelled")

print("t_cshape: ALL OK")
