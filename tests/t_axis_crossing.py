"""Additional lines crossing the H/V axes (user decision 2026-09-30, plan
docs/plan/2026-09-30-附加线跨轴放行施工计划.md): where a line's child
stations cross an axis, that axis releases its TANGENTIAL component on the
half the line crosses, so the ground at the crossing slides along the axis
as the line's flow asks. The axis never changes shape (normal component
pinned), the origin holds, an uncrossed axis holds in full, a voiceless
line releases nothing, and a half-axis whose frame side a line is drawn
out across keeps its pin.
Before the release the pinned band swallowed the slide: a 6 px groove and
~80% of the line's ask on the user's sample (tests/fixtures/
additional_line_axis_notch.anproj, diag tests/diag_additional_axis_notch.py).
"""
import io
import json
import math
import os
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am  # noqa: E402

H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]
BOW = 40.0          # how far the main partner bows toward the parallel axis


def line_asset(points, line_id=0):
    return {"points": [list(p) for p in points], "width": 3.0,
            "id": line_id, "third": [list(p) for p in points]}


def build(pairs):
    mp, _ = am.build_mapper(H, V, H, V, {}, additional_pairs=pairs)
    assert mp is not None and mp.warp is not None
    return mp.warp


def rot_about(points, pivot, deg):
    c, s = math.cos(math.radians(deg)), math.sin(math.radians(deg))
    return [(pivot[0] + c * (p[0] - pivot[0]) - s * (p[1] - pivot[1]),
             pivot[1] + s * (p[0] - pivot[0]) + c * (p[1] - pivot[1]))
            for p in points]


def v_pair(x, lo, hi, n=49):
    """A V-family pair: the child runs straight along x = const over
    y in [lo, hi]; the main partner bows BOW px toward the V axis, most at
    y = 0 when the pair crosses H there (at mid-span otherwise), with
    both ends where the child's are."""
    child, main = [], []
    for k in range(n):
        y = lo + (hi - lo) * k / (n - 1)
        if lo < 0.0 < hi:
            bow = math.cos(0.5 * math.pi * y / (lo if y < 0.0 else hi))
        else:
            bow = math.sin(math.pi * (y - lo) / (hi - lo))
        child.append((x, y))
        main.append((x - BOW * bow, y))
    return child, main


def transpose(points):
    return [(p[1], p[0]) for p in points]


def moved(warp, p):
    q = warp.apply(p)
    return math.hypot(q[0] - p[0], q[1] - p[1])


def groove(profile):
    """How far the axis band (|l| <= 5) sits below the LOWER of its two
    shoulders (25 <= |l| <= 45): a groove is a local depression, so a
    profile that ramps or peaks across the axis has none (<= 0). (The
    diag's shoulder-mean formula assumes a symmetric profile and reads a
    ramp's asymmetry as depth.)"""
    def mean(ls):
        return sum(profile[l] for l in ls) / len(ls)
    return (min(mean(range(-45, -24, 5)), mean(range(25, 46, 5)))
            - mean(range(-5, 6, 5)))


def notch(profile):
    """The deepest single-sample dip in the axis band below the midpoint
    of its 5 px neighbours: a narrow notch that the shoulder comparison
    would average away on a lopsided profile."""
    return max((profile[l - 5] + profile[l + 5]) / 2.0 - profile[l]
               for l in (-5, 0, 5))


def residuals(warp, index=0):
    pair = warp.pairs[index]
    return [math.hypot(*(a - b for a, b in zip(warp.apply(c), m)))
            for c, m in zip(pair["child"], pair["main"])]


def axis_moves(warp, axis, component=None):
    """Worst move of the H ("h") or V ("v") axis over its drawn span,
    total or one component (0 = x, 1 = y)."""
    worst = 0.0
    for s in range(-300 if axis == "h" else -200,
                   301 if axis == "h" else 201, 10):
        p = (float(s), 0.0) if axis == "h" else (0.0, float(s))
        q = warp.apply(p)
        d = (math.hypot(q[0] - p[0], q[1] - p[1]) if component is None
             else abs(q[component] - p[component]))
        worst = max(worst, d)
    return worst


def check_crossing(warp, cross, along):
    """The shared assertions of a straight line crossing an axis at
    `cross`, its partner bowed BOW px toward the other axis: the profile
    along the line through the crossing is continuous with no groove, the
    ground at the crossing slides most of the drawn BOW, the line lands
    near its partner, no folds, the crossed axis keeps its shape, the
    other axis holds, and the field stays invertible."""
    t = 0 if along == "y" else 1          # the crossed axis's direction
    prof = {}
    for l in range(-60, 61, 5):
        p = ((cross[0], float(l)) if along == "y" else (float(l), cross[1]))
        prof[l] = moved(warp, p)
    step = max(abs(prof[l + 5] - prof[l]) for l in range(-45, 45, 5))
    assert step < 1.0, (step, prof)
    depth = groove(prof)
    assert depth < 0.5, (depth, prof)
    assert notch(prof) < 1.0, (notch(prof), prof)
    slide = warp.apply(cross)[t] - cross[t]
    assert slide < -0.75 * BOW, slide
    res = residuals(warp)
    mean_res = sum(res) / len(res)
    assert mean_res < 10.0, mean_res
    assert warp._all_positive and warp.fold_loci() == []
    crossed, other = ("h", "v") if along == "y" else ("v", "h")
    shape = axis_moves(warp, crossed, component=1 - t)
    assert shape < 1e-9, shape
    held = axis_moves(warp, other)
    assert held < 1e-9, held
    rt = max(math.hypot(*(c - d for c, d in zip(warp.unapply(warp.apply(p)),
                                                  p)))
             for p in (cross, (cross[0] + 30.0, cross[1] + 20.0),
                       (cross[0] - 10.0, cross[1] - 25.0),
                       (cross[0] + 3.0, cross[1] + 2.0)))
    assert rt < 1e-5, rt
    return step, depth, slide, mean_res, shape, held


# 1) V-FAMILY LINE CROSSING THE H AXIS (plan a): the child runs along
#    x = 150 across H, the partner bows 40 px toward the V axis, most at
#    the crossing. The crossed H axis slides there; its shape and the V
#    axis do not move.
child_a, main_a = v_pair(150.0, -170.0, 190.0)
w_a = build([(line_asset(child_a), line_asset(main_a))])
step, depth, slide_a, res_a, shape, held = check_crossing(
    w_a, (150.0, 0.0), "y")
print(f"1) V-family line across H: crossing slides {slide_a:.1f} px (drawn "
      f"-{BOW:.0f}), groove {depth:+.2f} px, 5 px steps <= {step:.2f}, "
      f"residual {res_a:.2f} px, no folds; H shape {shape:.0e}, V {held:.0e}")

# 2) MIRROR (plan b): an H-family line crossing the V axis behaves the
#    same way.
w_b = build([(line_asset(transpose(child_a)), line_asset(transpose(main_a)))])
step, depth, slide_b, res_b, shape, held = check_crossing(
    w_b, (0.0, 150.0), "x")
print(f"2) mirror, H-family line across V: crossing slides {slide_b:.1f} px, "
      f"groove {depth:+.2f} px, 5 px steps <= {step:.2f}, residual "
      f"{res_b:.2f} px, no folds; V shape {shape:.0e}, H {held:.0e}")

# 3) CONTROL (plan d): the same bowed pair lying wholly above the H axis
#    crosses nothing, so both axes hold in full - the release happens
#    only where a line crosses.
child_d, main_d = v_pair(150.0, 15.0, 195.0)
w_d = build([(line_asset(child_d), line_asset(main_d))])
held_d = max(axis_moves(w_d, "h"), axis_moves(w_d, "v"))
assert held_d < 0.01, held_d
print(f"3) same pair not crossing: both axes hold ({held_d:.0e} px)")

# 4) A STATION EXACTLY ON THE AXIS still makes a crossing (the symmetric
#    span puts the middle station at y = 0.0 exactly; a sign test on
#    consecutive stations alone would miss it and keep the axis pinned).
child_e, main_e = v_pair(150.0, -180.0, 180.0)
w_e = build([(line_asset(child_e), line_asset(main_e))])
assert any(p[1] == 0.0 for p in w_e.pairs[0]["child"])
slide_e = w_e.apply((150.0, 0.0))[0] - 150.0
assert slide_e < -0.75 * BOW, slide_e
print(f"4) station exactly on the axis still crosses: slide {slide_e:.1f} px")

# 5) A STRETCHED SIDE KEEPS THE PIN: this line crosses V inside the frame
#    but is drawn out across the top edge; the tent is damped to zero on
#    the axes, and a released V filled that dip instead of following the
#    drawn ask (+92 px of slide for +36 px, residual 30 -> 61 px), so the
#    upper half of V stays pinned as before.
child_f = [(-150.0 + 300.0 * k / 32.0, 120.37 + 142.0 * k / 32.0)
           for k in range(33)]
w_f = build([(line_asset(child_f),
              line_asset(rot_about(child_f, child_f[0], 15.0)))])
assert w_f._tent["y"][1][0] > 0.0           # the top side is stretched
held_f = axis_moves(w_f, "v")
assert held_f < 0.01, held_f
assert w_f._all_positive and w_f.fold_loci() == []
print(f"5) line drawn out across the top: V stays pinned ({held_f:.0e} px)")

# 6) A VOICELESS CROSSING LINE RELEASES NOTHING: this H-family line hugs
#    its own H axis and crosses it shallowly, so its keyframe weight never
#    reaches VOICE_MIN (the "(nearly) no influence" note). It asks nothing
#    and must not free H for its neighbours - it let the rotated V-family
#    line beside it slide H 41.6 px.
child_q = [(60.0 + 200.0 * k / 32.0, 1.0 - 2.0 * k / 32.0) for k in range(33)]
child_n = [(60.0, 20.0 + 160.0 * k / 16.0) for k in range(17)]
w_q = build([(line_asset(child_q, 0),
              line_asset(rot_about(child_q, child_q[16], 3.0), 0)),
             (line_asset(child_n, 1),
              line_asset(rot_about(child_n, child_n[0], -30.0), 1))])
assert any("no influence" in n for n in w_q.notes), w_q.notes
held_q = axis_moves(w_q, "h")
assert held_q < 0.01, held_q
print(f"6) voiceless crossing line: H holds ({held_q:.0e} px)")

# 7) FLOAT (known trade-off, plan risk): a NON-crossing line rotated about
#    one end shares the released right half of H with a crossing line
#    that asks almost nothing (0.5 deg about the crossing). The
#    non-crossing band's integration constant used to ride on that half's
#    tangential pin, so the half now slides under it: 9.2 px under an
#    H-family line (t_flowfield case 2's), 31.3 px under a V-family one,
#    whose integration runs straight down to H. The bounds catch growth.
child_z = [(150.0, -170.0 + 360.0 * k / 48.0) for k in range(49)]
main_z = rot_about(child_z, (150.0, 0.0), 0.5)
w_z = build([(line_asset(child_z, 0), line_asset(main_z, 0))])


def float_under(child_g, deg, bound):
    w_zg = build([(line_asset(child_z, 0), line_asset(main_z, 0)),
                  (line_asset(child_g, 1),
                   line_asset(rot_about(child_g, child_g[0], deg), 1))])
    drift = max(abs(w_zg.apply((float(s), 0.0))[0]
                    - w_z.apply((float(s), 0.0))[0])
                for s in range(0, 301, 10))
    assert drift < bound, drift
    assert w_zg._all_positive and w_zg.fold_loci() == []
    return drift


float_h = float_under([(60.0 + 200.0 * k / 16.0, 120.0) for k in range(17)],
                      25.0, 12.0)
float_v = float_under([(60.0, 20.0 + 160.0 * k / 16.0) for k in range(17)],
                      -30.0, 40.0)
print(f"7) non-crossing bands slide the released half of H {float_h:.1f} px "
      f"(H family) / {float_v:.1f} px (V family) - known trade-off, bounded")

# 8) THE USER'S SAMPLE (plan c): a V-family line crossing the H axis.
#    Measured through the real pipeline in main-board px, like the diag.
FIXTURE = os.path.join(HERE, "fixtures", "additional_line_axis_notch.anproj")
doc = json.load(io.open(FIXTURE, encoding="utf-8"))
am._ACTIVE_UNIT["id"] = None
am._MAPPING_ASSETS.clear()
for view, key in (("mainView", "main"), ("textureView", "child")):
    unit = json.loads(doc[view]["scriptData"])["mapping_units"]["units"]["1"]
    am._MAPPING_ASSETS[key] = unit["assets"]
full, _ = am._mapper_from_assets(additional=True)
base, _ = am._mapper_from_assets(additional=False)
w_s = full.warp
drawn = am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"][0][
    "points"]
crossing = None
prev = None
for a, b in zip(drawn, drawn[1:]):
    n = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / 2.0))
    for k in range(n + 1):
        c = full.coords((a[0] + (b[0] - a[0]) * k / n,
                         a[1] + (b[1] - a[1]) * k / n))
        if c is not None and prev is not None and prev[1] * c[1] < 0.0:
            crossing = crossing or c
        prev = c
assert crossing is not None
prof = {}
for l in range(-60, 61, 5):
    f = full.main_of_third((crossing[0], float(l)))
    g = base.main_of_third((crossing[0], float(l)))
    prof[l] = math.hypot(f[0] - g[0], f[1] - g[1])
depth_s = groove(prof)
assert depth_s < 0.5, (depth_s, prof)
notch_s = notch(prof)
assert notch_s < 1.0, (notch_s, prof)             # measured 0.44
res_s = residuals(w_s)
mean_s = sum(res_s) / len(res_s)
assert mean_s < 10.0, mean_s
pair = w_s.pairs[0]
at = min(range(len(pair["child"])), key=lambda k: abs(pair["child"][k][1]))
ask = math.hypot(*(m - c for m, c in zip(pair["main"][at],
                                          pair["child"][at])))
assert res_s[at] < 0.2 * ask, (res_s[at], ask)
assert w_s._all_positive and w_s.fold_loci() == []
shape_s = axis_moves(w_s, "h", component=1)
assert shape_s < 1e-9, shape_s
held_s = axis_moves(w_s, "v")
assert held_s < 1e-9, held_s
am._MAPPING_ASSETS.clear()
print(f"8) user sample: groove {depth_s:+.2f} px, crossing delivers "
      f"{ask - res_s[at]:.1f} of {ask:.1f} px, residual mean {mean_s:.2f} / "
      f"max {max(res_s):.2f} px, no folds; H shape {shape_s:.0e}, "
      f"V {held_s:.0e}")

print("t_axis_crossing: ALL OK")
