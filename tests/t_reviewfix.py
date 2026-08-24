"""Regression tests for the six adversarial-review findings on the
vector/fold rework: taper tear, phantom loci, depth-cap discard, per-band
duplicate tracing, rank-chain welds, stale seal registry, memo/support."""
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


def c_arc(radius, deg0, deg1, count, cx=0.0, cy=0.0):
    pts = []
    for k in range(count):
        a = math.radians(deg0 + (deg1 - deg0) * k / (count - 1))
        pts.append((cx + radius * math.cos(a), cy + radius * math.sin(a)))
    return pts


# 1) TAPER TEAR: multi-segment C, displacement must be continuous - probe
#    dense steps along many chords and bound the worst adjacent jump.
child = c_arc(120.0, -125.0, 125.0, 120)
main = [(x + 40.0, y + 15.0) for x, y in child]
w = am._AdditionalWarp([(child, main)], "linear", 0.5)
assert len(w.pairs) >= 2, "scenario must segment"
worst_jump = 0.0
for row in range(-15, 16, 3):
    prev = None
    for k in range(801):
        p = (-200.0 + 400.0 * k / 800.0, row * 12.0)
        d = w.displacement(p)
        if prev is not None:
            worst_jump = max(worst_jump,
                             math.hypot(d[0] - prev[0], d[1] - prev[1]))
        prev = d
print(f"1) taper tear: worst adjacent displacement jump {worst_jump:.3f} px "
      f"(0.5 px steps)")
assert worst_jump < 2.0, worst_jump

# 2) PHANTOM LOCI: gentle ask (max gradient ~0.33) must yield ZERO loci.
gentle_main = [(x + 3.0, y + 1.0) for x, y in child]
wg = am._AdditionalWarp([(child, gentle_main)], "linear", 0.5)
loci = wg.fold_loci()
print(f"2) gentle non-folding ask: {len(loci)} fold loci (want 0)")
assert not loci, len(loci)

# 3) DEPTH CAP: a 340-degree hook must leave NO dead stations.
hook = c_arc(120.0, -170.0, 170.0, 120)
hook_main = [(x + 12.0, y + 7.0) for x, y in hook]
wh = am._AdditionalWarp([(hook, hook_main)], "linear", 0.5)
dead = sum(1 for p in hook
           if math.hypot(*wh.displacement(p)) < 1e-9)
print(f"3) 340-degree hook: {len(wh.pairs)} segments, {dead}/{len(hook)} "
      f"dead stations (want 0)")
assert dead == 0, dead

# 4) DUPLICATE TRACING: two parallel lines, overlapping bands, one shared
#    fold boundary - traced crossings on a column must match the field.
la = [(-100.0 + 200.0 * k / 39.0, -15.0) for k in range(40)]
lb = [(-100.0 + 200.0 * k / 39.0, 15.0) for k in range(40)]
wp = am._AdditionalWarp([(la, [(x, y + 150.0) for x, y in la]),
                         (lb, [(x, y + 150.0) for x, y in lb])],
                        "linear", 0.5)
loci = wp.fold_loci()


def crossings_at(x, curves):
    count = 0
    for curve in curves:
        for a, b in zip(curve, curve[1:]):
            if (a[0] - x) * (b[0] - x) <= 0.0 and a[0] != b[0]:
                count += 1
    return count


def field_flips(x):
    flips = 0
    prev = None
    y = -300.0
    while y <= 400.0:
        s = wp.det_sign((x, y))
        if prev is not None and s != prev:
            flips += 1
        prev = s
        y += 0.05
    return flips


bad_cols = []
for x in (-140.3, -120.3, -60.3, 3.7, 60.3, 120.3, 140.3):
    t, f = crossings_at(x, loci), field_flips(x)
    if t != f:
        bad_cols.append((x, t, f))
print(f"4) parallel-line duplicate check: {len(loci)} loci, "
      f"column mismatches {bad_cols} (want [])")
assert not bad_cols, bad_cols

# 5) MEMOIZATION: same geometry, distinct list objects per call.
first, second = wp.fold_loci(), wp.fold_loci()
assert first == second
assert all(id(a) != id(b) for a, b in zip(first, second))
print("5) fold_loci memoized: equal geometry, distinct objects")

# 6) SUPPORT EARLY-OUT: det_sign must agree with the raw jacobian sign
#    everywhere, including the band fringe.
mismatch = 0
for gx in range(-30, 31, 2):
    for gy in range(-30, 31, 2):
        p = (gx * 10.0, gy * 10.0)
        a, b, c, d = wp.jacobian(p)
        raw = 1 if a * d - b * c >= 0.0 else -1
        if wp.det_sign(p) != raw:
            mismatch += 1
print(f"6) det_sign early-out vs raw jacobian: {mismatch} mismatches")
assert mismatch == 0, mismatch

# 7) SEAL REGISTRY LIVENESS: after a full crease pass, every warp locus
#    that _emit_seals iterates must resolve in warp_curve_child.
mp = build([({"points": [(-100.0, 50.0), (100.0, 50.0)], "width": 2.5},
             {"points": [(-100.0, 200.0), (100.0, 200.0)], "width": 2.5})])
am._prepare_fold_context(mp, (-250.0, 250.0), (-180.0, 260.0))
curves = am._crease_curves(mp, (-180.0, 260.0), (-250.0, 250.0),
                           corner_spans=((-250.0, 250.0), (-180.0, 260.0)))
registry = getattr(mp, "warp_curve_child", {}) or {}
warp_hits = sum(1 for c in curves if id(c) in registry)
print(f"7) stitched pass: {len(curves)} curves, {warp_hits} resolve in the "
      f"registry (want >= 1)")
assert warp_hits >= 1, warp_hits

print("t_reviewfix: ALL OK")
