"""Topology severing: the staged Child -> Third -> Main map with NO residual
term, per-point Jacobian validity, and the UV-seam cuts that stand in for 3D
occlusion (user request 2026-08-24)."""
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am

H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]


def close(a, b, tol=1e-6):
    return math.hypot(a[0] - b[0], a[1] - b[1]) <= tol


def hook_h():
    """A child H guide that CURLS: straight to (100, 0), then an arc whose
    tangent turns past vertical at theta=90 deg. Against a straight vertical
    V guide the frame folds exactly where the tangent's x-component flips,
    and the folded sheet's silhouette is the vertical line x = 160 (the arc's
    rightmost point): canvas ground beyond it has NO preimage at all."""
    pts = [(-300.0, 0.0), (100.0, 0.0)]
    for k in range(1, 61):
        theta = math.radians(150.0 * k / 60.0)
        pts.append((100.0 + 60.0 * math.sin(theta),
                    60.0 * (1.0 - math.cos(theta))))
    return pts


def build(child_h, child_v, main_h, main_v):
    mp, ws = am.build_mapper(child_h, child_v, main_h, main_v, {})
    assert mp is not None, ws
    return mp


# 1) STRAIGHT FRAMES: the fold gate stays closed, severing is free, and the
#    residual-free map is still the bit-exact identity (chord seed = lift).
mp0 = build(H, V, H, V)
assert not mp0.can_fold()
for p in [(37.5, -81.25), (-140.0, 60.0), (260.0, 199.0), (0.0, 0.0)]:
    assert close(mp0(p), p, 1e-9), (p, mp0(p))
    l_h, l_v, ok = mp0.third_of(p)
    assert ok
    assert close(mp0.main_of_third((l_h, l_v)), p, 1e-9)
probe = am._densify([(-150.0, 20.0), (250.0, 20.0)])
seams = []
islands = am._sever_source(mp0, probe, seams)
assert len(islands) == 1 and not seams and islands[0] == probe
print("1) straight frames: gate closed, identity bit-exact, one island")

# 2) PURITY: map_point IS the staged composition, literally.
for p in [(11.0, 22.0), (-87.0, -3.0), (150.0, 20.0)]:
    assert mp0(p) == mp0.main_of_third(mp0.coords(p))
print("2) map_point == main_of_third(coords): the residual is gone")

# 3) CURVED IDENTICAL FRAMES: identity holds through the Newton lift alone
#    (no residual to glue it) wherever the frame does not fold.
sine = [(x, 30.0 * math.sin((x + 300.0) / 300.0 * 2.0 * math.pi))
        for x in range(-300, 301, 6)]
mp_sine = build(sine, V, sine, V)
assert not mp_sine.can_fold()   # max slope ~32 deg: nowhere near parallel
worst = 0.0
for gx in range(-4, 5):
    for gy in range(-4, 5):
        p = (gx * 60.0, gy * 40.0)
        worst = max(worst, math.hypot(*(a - b for a, b in zip(mp_sine(p), p))))
assert worst <= 1e-5, worst
print(f"3) identical sine frames: identity via the lift alone (max {worst:.2e} px)")

# 4) HOOK FRAME: validity verdicts around the fold silhouette x = 160.
mp1 = build(hook_h(), V, H, V)
assert mp1.can_fold()
assert mp1.third_of((0.0, 20.0))[2]
assert mp1.third_of((-150.0, 20.0))[2]
assert not mp1.third_of((250.0, 20.0))[2]   # beyond the silhouette: no preimage
assert not mp1.third_of((200.0, 0.0))[2]
print("4) hook frame: gate open, verdicts flip across the silhouette")

# 5) SEVERING a stroke that runs off the sheet: one island, cut ON the
#    silhouette, a seam recorded, and nothing invalid survives.
stroke = am._densify([(-150.0, 20.0), (250.0, 20.0)])
seams = []
islands = am._sever_source(mp1, stroke, seams)
assert len(islands) == 1, len(islands)
island = islands[0]
assert island[0] == stroke[0]
cut = island[-1]
assert len(seams) == 1 and close(seams[0], cut, 1e-9)
assert abs(cut[1] - 20.0) <= 1e-9
assert abs(cut[0] - 160.0) <= 3.0, cut   # the silhouette, +- verdict width
for p in island:
    assert mp1.third_of(p)[2] or close(p, cut, 1e-6)
mapped = am._adaptive_map_polyline(mp1, island)
assert all(math.isfinite(q[0][0]) and math.isfinite(q[0][1]) for q in mapped)
print(f"5) stroke severed at ({cut[0]:.2f}, {cut[1]:.2f}) - "
      f"silhouette x=160, {len(island)} pts survive, 1 seam")

# 6) A stroke ENTIRELY on severed ground vanishes; one entirely on the sheet
#    is untouched.
gone = am._sever_source(mp1, am._densify([(200.0, -50.0), (280.0, 50.0)]), [])
assert gone == [], gone
whole = am._densify([(-250.0, -100.0), (50.0, 100.0)])
kept = am._sever_source(mp1, whole, [])
assert len(kept) == 1 and kept[0] == whole
print("6) fully-shadowed stroke dropped; fully-lit stroke untouched")

# 7) BEZIER mode: the cubic splitter cuts on the same silhouette.
cub = ((-150.0, 20.0), (-20.0, 20.0), (120.0, 20.0), (250.0, 20.0))
seams = []
runs = am._sever_cubics_by_child_fold(mp1, [cub], seams)
assert len(runs) == 1 and len(seams) == 1
tail = runs[0][-1][3]
assert abs(tail[0] - 160.0) <= 3.0 and abs(tail[1] - 20.0) <= 0.5, tail
assert close(seams[0], tail, 1e-9)
print(f"7) bezier island ends at ({tail[0]:.2f}, {tail[1]:.2f}) on the seam")

# 8) SEAM LOCI: the child-frame fold curves exist and sit where the windowed
#    tangents really turn parallel.
loci = am._sever_loci(mp1)
assert loci, "hook frame must trace at least one seam locus"
child = mp1.child_frame
worst_det = 0.0
for curve in loci:
    for l_h, l_v in curve[:: max(1, len(curve) // 8)]:
        t_h, t_v = child.directions(l_h, l_v)
        worst_det = max(worst_det, abs(t_h[0] * t_v[1] - t_h[1] * t_v[0]))
assert worst_det <= 5e-2, worst_det
assert am._sever_loci(mp1) is loci   # memoized
print(f"8) {len(loci)} seam locus/loci traced, |det| <= {worst_det:.1e} on them")

# 9) FILLS: a ring straddling the silhouette is cut there and only lit
#    pieces survive; a ring wholly in shadow yields nothing - even though no
#    seam crosses its bbox.
ring = [(100.0, -30.0), (250.0, -30.0), (250.0, 30.0), (100.0, 30.0)]
pieces = am._split_ring_by_fold(mp1, am._densify(ring + [ring[0]])[:-1])
assert pieces, "the lit part of the ring must survive"
for piece, side, rep in pieces:
    assert mp1.third_of(rep)[2]
    for p in piece:
        assert p[0] <= 165.0, p   # nothing survives beyond the silhouette
shadow = [(200.0, -25.0), (250.0, -25.0), (250.0, 25.0), (200.0, 25.0)]
assert am._split_ring_by_fold(mp1, am._densify(shadow + [shadow[0]])[:-1]) == []
lit = [(-120.0, -40.0), (60.0, -40.0), (60.0, 40.0), (-120.0, 40.0)]
lit_pieces = am._split_ring_by_fold(mp1, am._densify(lit + [lit[0]])[:-1])
assert len(lit_pieces) == 1 and lit_pieces[0][1] == am._MappedOutput.FRONT
print(f"9) fills: straddling ring -> {len(pieces)} lit piece(s), "
      "shadowed ring dropped, lit ring whole")

# 10) MAIN-frame folds are a different phenomenon and still work: a main
#     guide with a sharp corner splits runs front/back exactly as before.
bent_main_v = [(0.0, -200.0), (0.0, 100.0), (-190.0, 40.0)]
mp2 = build(H, V, H, bent_main_v)
assert not mp2.can_fold()   # child side is straight: nothing severs
runs = am._split_by_fold(mp2, am._densify([(-100.0, -150.0), (-100.0, 150.0)]))
sides = [side for _run, side in runs]
assert -1 in sides and 1 in sides, sides
print(f"10) main-fold split still live (runs: {sides}), severing untouched")

# --- Review fixes (2026-08-25) --------------------------------------------

# 11) A severed island's endpoints sit on VALID ground: the bisected cut
#     snaps to the True side of the bracket, and a verdict change falling
#     on a source vertex is bisected ACROSS the vertex instead of stopping
#     there with a branch-jumped lift.
ZIG_H = [(-300.0, 0.0), (120.0, 0.0), (0.0, 100.0), (400.0, 100.0)]
MAIN_H = [(-300.0, 0.0), (300.0, 0.0)]
mp3 = build(ZIG_H, V, MAIN_H, V)
node = (118.0, 30.0)   # a source vertex just before the verdict boundary
poly = am._densify([(-200.0, 30.0), node]) + am._densify([node, (350.0, 30.0)])[1:]
for pts in (am._densify([(-200.0, 30.0), (350.0, 30.0)]), poly):
    cut_islands = am._sever_source(mp3, pts, [])
    assert len(cut_islands) == 2, len(cut_islands)
    for island in cut_islands:
        assert mp3.third_of(island[0])[2], island[0]
        assert mp3.third_of(island[-1])[2], island[-1]
print("11) sever cuts land on the valid side; vertex changes bisect across")

# 12) An UNFOLDABLE frame keeps the identity even where the chord-seed
#     Newton plateaus (near-parallel guides, ground far outside the frame):
#     retry from the iterate, then the exact residual correction - never
#     the raw stalled iterate (measured 113 px drift before the fix).
bow = [(-300.0 + 600.0 * k / 50.0, 15.0 * math.sin(math.pi * k / 50.0))
       for k in range(51)]
tilt = math.radians(6.0)
v6 = [(-math.sin(tilt) * 500.0, -math.cos(tilt) * 500.0),
      (math.sin(tilt) * 500.0, math.cos(tilt) * 500.0)]
mp4 = build(bow, v6, bow, v6)
assert not mp4.can_fold()
for y in (625.0, 650.0, 700.0, 800.0):
    q = mp4((180.0, y))
    assert math.hypot(q[0] - 180.0, q[1] - y) <= 1e-6, (y, q)
print("12) unfoldable-frame plateau absorbed: identity holds off-frame")

# 13) Fills and strokes agree on where the pattern ends: the sever cutters
#     are marched from the SAME verdict field the strokes consult, so a
#     straddling ring's cut edges sit on the stroke seams - including the
#     divergence edge, which no Third locus projects onto (the zig re-open
#     at canvas x~274 while the nearest locus images onto the x=0 line).
stroke_seams = []
am._sever_source(mp3, am._densify([(-50.0, 30.0), (350.0, 30.0)]),
                 stroke_seams)
seam_x = sorted(p[0] for p in stroke_seams)
assert len(seam_x) == 2, seam_x
ring = [(-50.0, -20.0), (350.0, -20.0), (350.0, 80.0), (-50.0, 80.0)]
zpieces = am._sever_ring(mp3, am._densify(ring + [ring[0]])[:-1])
assert len(zpieces) >= 2, len(zpieces)
edges = sorted(x for piece in zpieces
               for x in (min(p[0] for p in piece), max(p[0] for p in piece))
               if -40.0 < x < 340.0)
assert len(edges) >= 2, edges
assert abs(edges[0] - seam_x[0]) <= 3.0, (edges, seam_x)
assert abs(edges[-1] - seam_x[1]) <= 3.0, (edges, seam_x)
print(f"13) fill cut edges [{edges[0]:.1f}, {edges[-1]:.1f}] sit on the "
      f"stroke seams [{seam_x[0]:.1f}, {seam_x[1]:.1f}]")

# 14) A donut whose hole dips into severed ground KEEPS its hole: the piece
#     verdict votes over boundary samples (one interior probe painted the
#     cut-out solid / deleted whole donuts on a speckle), and the hole
#     piece attaches through spread boundary candidates (a single edge
#     midpoint on the shared cut chord detached the crescent).


def _circle(cx, cy, r, n=90):
    return [(cx + r * math.cos(2.0 * math.pi * k / n),
             cy + r * math.sin(2.0 * math.pi * k / n)) for k in range(n)]


def _ring_commands(rings):
    commands = []
    for ring_pts in rings:
        commands.append({"type": "move",
                         "to": {"x": ring_pts[0][0], "y": ring_pts[0][1]}})
        for p in ring_pts[1:] + [ring_pts[0]]:
            commands.append({"type": "line", "to": {"x": p[0], "y": p[1]}})
    return commands


class _FillOut:
    def __init__(self):
        self.cuts = []
        self.fills = []

    def add_fill(self, side, depth, mapped, shade):
        if not mapped:
            return False
        self.fills.append((side, depth, mapped, shade))
        return True


fill = {"commands": _ring_commands([_circle(300.0, 0.0, 95.0),
                                    _circle(255.0, 0.0, 25.0)]),
        "color": {"r": 200, "g": 60, "b": 60, "a": 255}}
fout = _FillOut()
assert am._emit_fills(None, fout, mp3, [fill], None, None) >= 1


def _covered(point):
    total = 0
    for _side, _depth, mapped, _shade in fout.fills:
        hits = sum(1 for ring_pts in mapped
                   if am._point_in_ring(point, ring_pts))
        total += hits % 2
    return total


assert _covered(mp3((277.5, 0.0))) == 0    # the hole's surviving crescent
assert _covered(mp3((370.0, 0.0))) >= 1    # the outer's lit band
print("14) severed donut keeps its hole; lit band still paints")

# 15) "To 3D" collects only computable ground: the stroke drape severs like
#     the 2D emitters (pre-fix it lifted the whole stroke with bare coords,
#     piling severed points onto fabricated Third positions).
seen_islands = []
orig_sever = am._sever_source


def _sever_spy(map_point, piece, seams=None):
    result = orig_sever(map_point, piece, seams)
    seen_islands.extend(result)
    return result


am._sever_source = _sever_spy
try:
    res3d = am._reconstruct_surface_3d(
        mp1, [], [{"polylines": [[{"x": -150.0, "y": 20.0},
                                  {"x": 250.0, "y": 20.0}]],
                   "width": 3.0}], grid_target=16)
finally:
    am._sever_source = orig_sever
assert res3d is not None
assert seen_islands, "the 3D drape must sever its strokes"
for island in seen_islands:
    for p in island:
        assert p[0] <= 165.0, p   # nothing past the silhouette is lifted
print("15) 3D reconstruction drapes severed islands only")

print("t_sever: ALL OK")
