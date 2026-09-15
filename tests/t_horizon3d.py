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

# 2) The horizon is NEEDED: without it the tilt sign is a coin that happens
#    to land right below eye level and wrong above it (the weak-perspective
#    blindness, not a regression). With it, both guide heights are right.
for world_y in (-250.0, 250.0):
    for name, zfun in CASES[:2]:
        c, _ = relief_corr(zfun, world_y)
        assert c > 0.9, (name, world_y, c)
blind_above = [relief_corr(zfun, 250.0, horizon_y=None)[0]
               for _name, zfun in CASES[:2]]
assert all(c < 0.0 for c in blind_above), blind_above
print(f"2) tilts above eye level: wrong without a horizon "
      f"({blind_above[0]:+.2f} / {blind_above[1]:+.2f}), right with one")

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


def reconstruct(main_h, main_v, horizon_y):
    mp, note = am.build_mapper(CHILD_H, CHILD_V, main_h, main_v, {})
    assert mp is not None, note
    return am._reconstruct_surface_3d(mp, [FILL], [], grid_target=40,
                                      horizon_y=horizon_y)


def slope_z_over_x(res, keep):
    pts = [(p[0], p[2]) for p, uv in zip(res["vertices"], res["uv"])
           if keep(uv[1])]
    mx = sum(x for x, _z in pts) / len(pts)
    mz = sum(z for _x, z in pts) / len(pts)
    return (sum((x - mx) * (z - mz) for x, z in pts)
            / sum((x - mx) ** 2 for x, _z in pts))


# 5) FOLD PARITY (review finding): the main V guide folds back over a
#    horizontal crease at v = +100, so the layer past it faces away and its
#    relief slope along u is reversed. The horizon verdict is read on the
#    FRONT guide and must reach the back layer with that reversal - forcing
#    one sign down the whole column flattened the fold's parity. After the
#    Poisson solve the reversed back-layer gradient shows as a strongly
#    damped z slope (front 0.54, back 0.17 with or without the horizon);
#    the parity-blind override drove the back layer to the FRONT's slope
#    (0.82 vs 0.84).
main_h, _mv, _truth = curtain(CASES[0][1], -250.0)
cx, cy = main_h[len(main_h) // 2]
main_v = ([(cx, cy - 200.0 + 3.0 * k) for k in range(101)]
          + [(cx, cy + 100.0 - 3.0 * k) for k in range(1, 34)] + [(cx, cy)])
res = reconstruct(main_h, main_v, Y_HZ)
front = slope_z_over_x(res, lambda v: v < 80.0)
back = slope_z_over_x(res, lambda v: v >= 120.0)
assert front > 0.0, front
assert back < 0.5 * front, (front, back)
print(f"5) folded-back layer keeps its reversed gradient under the horizon "
      f"(z slope front {front:+.2f}, back {back:+.2f})")

# 6) A guide that CROSSES the horizon cannot be a world-horizontal line
#    (4 deg camera roll here; also a guide drawn vertically): the horizon
#    abstains and says so, and the result equals the horizon-free one -
#    voting there flipped every column at the crossing into a fake crease.
angle = math.radians(4.0)


def roll(p):
    x, y = p[0] - X0, p[1] - Y_HZ
    return (X0 + x * math.cos(angle) - y * math.sin(angle),
            Y_HZ + x * math.sin(angle) + y * math.cos(angle))


main_h, main_v, _truth = curtain(CASES[2][1], 0.0)
main_h = [roll(p) for p in main_h]
main_v = [roll(p) for p in [(X0, Y_HZ - 200.0), (X0, Y_HZ + 200.0)]]
rolled = reconstruct(main_h, main_v, Y_HZ)
assert rolled["horizon"].startswith("ignored"), rolled["horizon"]
assert rolled["vertices"] == reconstruct(main_h, main_v, None)["vertices"]
vertical_h = [(500.0 + 60.0 * math.cos((-300.0 + 10.0 * k) / 95.0),
               500.0 + 0.8 * (-300.0 + 10.0 * k)) for k in range(61)]
upright = reconstruct(vertical_h, [(700.0, 500.0), (300.0, 500.0)], Y_HZ)
assert upright["horizon"].startswith("ignored"), upright["horizon"]
print("6) guide crossing the horizon / vertical guide: horizon abstains")
