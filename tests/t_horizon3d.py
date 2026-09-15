"""To 3D relief SIGN from the horizon (two-point perspective).

Orthographic reading of the mapping Jacobian sees only |grad z|: a sheet
tilting toward the camera and one tilting away draw the same picture, and the
arc-length parametrisation of the guides even smears the foreshortening along
a guide into a uniform speed. A horizon breaks the tie: a receding
world-horizontal line converges on it, so the end nearer the horizon is the
far end. These cases project a curtain (bent about vertical rulings) through a
real perspective camera, draw its H guide on the main board, and require the
reconstructed relief to correlate POSITIVELY with the truth - for guides above
and below the horizon, with and without hand jitter."""
import math
import os
import random
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am

F, DIST, X0, Y_HZ = 1000.0, 1200.0, 500.0, 500.0
CHILD_H = [(-300.0, 0.0), (300.0, 0.0)]
CHILD_V = [(0.0, -200.0), (0.0, 200.0)]
RECT = [(-280.0, -180.0), (280.0, -180.0), (280.0, 180.0), (-280.0, 180.0)]
FILL = {"commands": [{"type": "move", "to": {"x": RECT[0][0], "y": RECT[0][1]}}]
        + [{"type": "line", "to": {"x": p[0], "y": p[1]}}
           for p in RECT[1:] + RECT[:1]],
        "color": {"r": 200, "g": 200, "b": 200, "a": 255}}


def slope(zfun, u):
    return (zfun(u + 1e-3) - zfun(u - 1e-3)) / 2e-3


def curtain(zfun, world_y, samples=241, jitter=0.0):
    """Main guides for a world-horizontal isometric curve z(u) (u = arc
    length, z toward the camera) at height world_y above the eye, plus the
    (image x, true z) samples."""
    rnd = random.Random(7)
    us = [-300.0 + 600.0 * k / (samples - 1) for k in range(samples)]
    xs = [0.0] * samples
    mid = samples // 2
    for k in range(mid + 1, samples):
        dz = slope(zfun, 0.5 * (us[k] + us[k - 1]))
        xs[k] = xs[k - 1] + (us[k] - us[k - 1]) * math.sqrt(1.0 - dz * dz)
    for k in range(mid - 1, -1, -1):
        dz = slope(zfun, 0.5 * (us[k] + us[k + 1]))
        xs[k] = xs[k + 1] - (us[k + 1] - us[k]) * math.sqrt(1.0 - dz * dz)
    main_h, truth = [], []
    for u, x_world in zip(us, xs):
        depth = DIST - zfun(u)
        x = X0 + F * x_world / depth
        y = Y_HZ - F * world_y / depth
        main_h.append((x + rnd.uniform(-jitter, jitter),
                       y + rnd.uniform(-jitter, jitter)))
        truth.append((x, zfun(u)))
    depth0 = DIST - zfun(0.0)
    main_v = [(X0, Y_HZ - F * (world_y + 200.0) / depth0),
              (X0, Y_HZ - F * (world_y - 200.0) / depth0)]
    return main_h, main_v, truth


def corr(a, b):
    ma, mb = sum(a) / len(a), sum(b) / len(b)
    num = sum((x - ma) * (y - mb) for x, y in zip(a, b))
    den = math.sqrt(sum((x - ma) ** 2 for x in a)
                    * sum((y - mb) ** 2 for y in b))
    return num / den if den > 1e-12 else 0.0


def relief_corr(zfun, world_y, jitter=0.0, horizon_y=Y_HZ):
    main_h, main_v, truth = curtain(zfun, world_y, jitter=jitter)
    mp, note = am.build_mapper(CHILD_H, CHILD_V, main_h, main_v, {})
    assert mp is not None, note
    res = am._reconstruct_surface_3d(mp, [FILL], [], grid_target=40,
                                     horizon_y=horizon_y)
    assert res is not None and res["faces"]
    xs = [t[0] for t in truth]
    zs = [t[1] for t in truth]
    rec, tru = [], []
    for vx, _vy, vz in res["vertices"]:
        for k in range(len(xs) - 1):
            if xs[k] <= vx <= xs[k + 1]:
                w = (vx - xs[k]) / ((xs[k + 1] - xs[k]) or 1e-9)
                rec.append(vz)
                tru.append(zs[k] + w * (zs[k + 1] - zs[k]))
                break
    return corr(rec, tru), res


CASES = [
    ("tilt toward +u", lambda u: 0.45 * u),
    ("tilt toward -u", lambda u: -0.45 * u),
    ("sine", lambda u: 35.0 * math.sin(2.0 * math.pi * u / 300.0)),
    ("cosine", lambda u: 35.0 * math.cos(2.0 * math.pi * u / 300.0)),
    ("bump", lambda u: 90.0 * math.exp(-(u / 90.0) ** 2)),
    ("dent", lambda u: -90.0 * math.exp(-(u / 90.0) ** 2)),
    ("asymmetric bump + dent",
     lambda u: 70.0 * math.exp(-((u + 130.0) / 60.0) ** 2)
     - 45.0 * math.exp(-((u - 120.0) / 55.0) ** 2)),
]

# 1) Every case, guide below AND above the horizon, clean and jittered: the
#    relief comes back with the right sign. The floor is not 1.0 because
#    the arc-length guides smear WHERE the foreshortening sits (a bump reads
#    as a tent); the sign is what the horizon decides.
worst = 1.0
for name, zfun in CASES:
    for world_y in (-250.0, 250.0):
        for jitter in (0.0, 0.5):
            c, _res = relief_corr(zfun, world_y, jitter)
            worst = min(worst, c)
            assert c > 0.6, (name, world_y, jitter, c)
print(f"1) horizon recovers the relief sign in every case (worst corr "
      f"{worst:+.3f})")

# 2) The decisive pair: a sheet tilting one way and its mirror draw the same
#    picture up to perspective, with OPPOSITE true relief. Without a horizon
#    both come back with the same sign - one of them necessarily wrong (the
#    weak-perspective blindness, not a regression); with it, both are right.
c_pos, _ = relief_corr(CASES[0][1], -250.0)
c_neg, _ = relief_corr(CASES[1][1], -250.0)
assert c_pos > 0.9 and c_neg > 0.9, (c_pos, c_neg)
blind_pos, _ = relief_corr(CASES[0][1], -250.0, horizon_y=None)
blind_neg, _ = relief_corr(CASES[1][1], -250.0, horizon_y=None)
assert blind_pos * blind_neg > 0.0, (blind_pos, blind_neg)
print(f"2) mirrored tilts: blind without a horizon ({blind_pos:+.2f} / "
      f"{blind_neg:+.2f}), told apart with one ({c_pos:+.2f} / {c_neg:+.2f})")

# 3) A guide AT eye level carries no cue (every receding line is flat
#    there): the horizon abstains and the reconstruction equals the
#    horizon-free one exactly - no fabricated verdict, no crash.
_c, at_eye = relief_corr(CASES[4][1], 0.0)
_c, no_hz = relief_corr(CASES[4][1], 0.0, horizon_y=None)
assert at_eye["vertices"] == no_hz["vertices"]
print("3) guide at eye level: horizon abstains, result unchanged")

# 4) A horizon far off the canvas still decides (the band only gates points
#    ON it): sign depends on the side, not the distance.
c_far, _ = relief_corr(CASES[4][1], -250.0, horizon_y=Y_HZ - 5000.0)
assert c_far > 0.6, c_far
print("4) distant horizon on the same side gives the same verdict")
