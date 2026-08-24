"""Additional lines that FOLD: C-shape fidelity, occlusion parity, warp
crease loci, cut anchoring - the add_occlusion report's scenarios."""
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


def fresh_boards():
    am._MAPPING_ASSETS.clear()
    for view in ("child", "main"):
        am._MAPPING_ASSETS[view] = {
            am.H_PROPERTY: {"points": list(H), "width": 3.0},
            am.V_PROPERTY: {"points": list(V), "width": 3.0},
        }


# 1) C-SHAPE FIDELITY: a C drawn on main (arc/chord ~2.3) must be traced by
#    the mapped image of its chord partner, not smoothed into a bump.
fresh_boards()
c_line = []
for k in range(25):
    a = math.pi * (0.5 - k / 24.0 * 1.6)     # 144-degree arc, opening left
    c_line.append((60.0 + 80.0 * math.cos(a), 60.0 + 80.0 * math.sin(a)))
arc = am._cumulative_lengths(c_line)[-1]
chord = math.hypot(c_line[-1][0] - c_line[0][0], c_line[-1][1] - c_line[0][1])
print(f"C line: arc {arc:.0f} chord {chord:.0f} ratio {arc / chord:.2f}")
ok = am.run_additional_line_tool("main", c_line)
assert ok
partner = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"][0]["points"]
mp_c, _ = am._mapper_from_assets()
assert mp_c.warp is not None and mp_c.warp.pairs
dense = []
cumP = am._cumulative_lengths([tuple(p) for p in partner])
for k in range(97):
    dense.append(am._point_at_arc([tuple(p) for p in partner], cumP,
                                  cumP[-1] * k / 96.0))
image = [mp_c(p) for p in dense]
worst = max(min(math.hypot(q[0] - c[0], q[1] - c[1]) for q in image)
            for c in c_line)
print(f"1) C fidelity: every drawn point within {worst:.1f} px of the mapped chord")
assert worst < 12.0, worst

# 2) OCCLUSION: a straight pair asking 1.5x the fold threshold must FOLD -
#    parity flips inside the band, warp loci exist, depth stacks.
fresh_boards()
child_line = {"points": [(-100.0, 50.0), (100.0, 50.0)], "width": 2.5}
main_line = {"points": [(-100.0, 200.0), (100.0, 200.0)], "width": 2.5}
am._ADDITIONAL["falloff"] = "linear"
mp_f = build([(child_line, main_line)])
w = mp_f.warp
assert w is not None
R = w.pairs[0]["radius"]
peak = max(math.hypot(dx, dy) for dx, dy in
           zip(w.pairs[0]["delta_x"], w.pairs[0]["delta_y"]))
print(f"2) fold ask: delta {peak:.0f} vs threshold R {R:.0f}")
assert peak > R, "test setup must exceed the fold threshold"
loci = w.fold_loci()
assert len(loci) >= 2, len(loci)
# parity: inside the folded band (between line and influence edge) the face
# flips; outside it does not.
assert am._fold_sign(mp_f, (0.0, 90.0)) == -1      # inside band
assert am._fold_sign(mp_f, (0.0, 20.0)) == 1       # below the line
assert am._fold_sign(mp_f, (0.0, 50.0 + R * 1.1)) == 1   # beyond the edge
# depth context: the folded band stacks at depth 1
am._prepare_fold_context(mp_f, (-250.0, 250.0), (-180.0, 260.0))
side_band = am._fold_sign(mp_f, (0.0, 90.0))
depth_band = am._fold_depth(mp_f, (0.0, 90.0), side_band)
side_out = am._fold_sign(mp_f, (0.0, 20.0))
depth_out = am._fold_depth(mp_f, (0.0, 20.0), side_out)
print(f"   band depth {depth_band} (side {side_band:+d}), "
      f"outside depth {depth_out} (side {side_out:+d})")
assert depth_out == 0 and depth_band >= 1

# 3) CUTS anchor on the warp loci: a vertical stroke through the band splits
#    into front/back runs whose cuts land on the injected cutters.
stroke = [(0.0, -40.0 + 4.0 * k) for k in range(80)]
runs = am._split_by_fold(mp_f, stroke)
sides = [s for _, s in runs]
print(f"   stroke runs: {sides}")
assert -1 in sides and sides.count(1) >= 2, sides
cutters = am._child_cutters(mp_f)
raw = mp_f.child_cutters_raw
assert raw, "no cutters injected for the warp fold"
worst_cut = 0.0
for k in range(len(runs) - 1):
    cut = runs[k][0][-1]
    d = min(am._polyline_arc_of(cut, c)[0] for c in raw)
    worst_cut = max(worst_cut, d)
print(f"   worst cut-to-cutter distance {worst_cut:.2f} px")
assert worst_cut < 6.0, worst_cut

# 4) orientation matches the numeric determinant everywhere on a probe grid
def det_at(mp_, p, eps=0.5):
    ax, bx = mp_((p[0] + eps, p[1])), mp_((p[0] - eps, p[1]))
    ay, by = mp_((p[0], p[1] + eps)), mp_((p[0], p[1] - eps))
    return (((ax[0] - bx[0]) * (ay[1] - by[1])
             - (ay[0] - by[0]) * (ax[1] - bx[1])) / (4 * eps * eps))


bad = 0
for x in range(-140, 141, 20):
    for y in range(-40, 181, 10):
        p = (float(x), float(y))
        d = det_at(mp_f, p)
        if abs(d) < 0.05:
            continue  # too close to a locus for the finite difference
        if (d > 0) != (am._fold_sign(mp_f, p) == 1):
            bad += 1
print(f"4) orientation vs numeric det: {bad} disagreements")
assert bad == 0, bad

print("t_addfold: ALL OK")
