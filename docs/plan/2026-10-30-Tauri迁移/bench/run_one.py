"""One suite or micro-op in a fresh interpreter; same file for CPython and Pyodide.

run_one(root, name) -> JSON string. Bytecode caching is off in both runtimes so
the repository stays untouched and both compile from source.
triangle / mapbox_earcut are forced absent so both walk the same fallback path.
"""
import contextlib
import io
import json
import math
import runpy
import sys
import time
import types


def _prepare(root):
    sys.dont_write_bytecode = True
    for sub in ("tests", "pythonbind", "pyfile"):
        sys.path.insert(0, root + "/" + sub)
    sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
    import pydeps
    real = pydeps.ensure

    def ensure(name, pip_name=None):
        if name in ("triangle", "mapbox_earcut"):
            return None
        return real(name, pip_name)

    pydeps.ensure = ensure


def _suite(root, name):
    sink = io.StringIO()
    status = "ok"
    start = time.perf_counter()
    try:
        with contextlib.redirect_stdout(sink), contextlib.redirect_stderr(sink):
            runpy.run_path(root + "/tests/" + name + ".py", run_name="__main__")
    except SystemExit as error:
        status = "ok" if not error.code else f"exit {error.code}"
    except BaseException as error:
        status = f"{type(error).__name__}: {str(error)[:200]}"
    return {"seconds": time.perf_counter() - start, "status": status}


def _sin_guide(amp, axis, n=101, half=200.0, period=300.0):
    pts = []
    for k in range(n):
        s = -half + 2.0 * half * k / (n - 1)
        bow = amp * math.sin(2.0 * math.pi * s / period)
        pts.append((s, bow) if axis == "h" else (bow, s))
    return pts


def _asset(points, line_id=0):
    return {"points": [list(p) for p in points], "width": 3.0,
            "id": line_id, "third": [list(p) for p in points]}


def _ops(root):
    """The steps a user waits on, on the rig of tests/t_chord_intersect.py
    (sinusoidal guides, 101 points) with a rotated additional line so the
    flow-field grid really solves."""
    out = {}
    t = time.perf_counter()
    import auto_mapping as am
    import chord_intersect as ci
    out["import_auto_mapping_ms"] = (time.perf_counter() - t) * 1e3

    child_h, child_v = _sin_guide(30.0, "h"), _sin_guide(30.0, "v")
    main_h, main_v = _sin_guide(60.0, "h"), _sin_guide(45.0, "v")
    pts = [(-150.0 + 300.0 * a / 24, -150.0 + 300.0 * b / 24)
           for a in range(25) for b in range(25)]

    t = time.perf_counter()
    mp, _ = am.build_mapper(child_h, child_v, main_h, main_v, {})
    out["build_mapper_plain_ms"] = (time.perf_counter() - t) * 1e3
    if mp is None:
        raise RuntimeError("plain mapper failed")
    t = time.perf_counter()
    for p in pts:
        mp(p)
    out["map_625_points_ms"] = (time.perf_counter() - t) * 1e3

    ang = math.radians(25.0)
    src = [(60.0 + 3.0 * i, 50.0 + 20.0 * math.sin(i * 0.1)) for i in range(60)]
    dst = [(x * math.cos(ang) - y * math.sin(ang),
            x * math.sin(ang) + y * math.cos(ang)) for x, y in src]
    t = time.perf_counter()
    mp2, _ = am.build_mapper(child_h, child_v, main_h, main_v, {},
                             additional_pairs=[(_asset(src), _asset(dst))])
    out["build_mapper_additional_ms"] = (time.perf_counter() - t) * 1e3
    if mp2 is None:
        raise RuntimeError("additional mapper failed")
    out["warp_grid"] = bool(mp2.warp is not None and mp2.warp._grid is not None)
    t = time.perf_counter()
    for p in pts:
        mp2(p)
    out["map_625_points_warped_ms"] = (time.perf_counter() - t) * 1e3

    frame = am._Frame(child_h, child_v)
    solver = ci.ChordIntersect.from_frame(frame)
    t = time.perf_counter()
    for p in pts:
        solver.solve(p)
    out["chord_solve_625_ms"] = (time.perf_counter() - t) * 1e3

    import numpy as np
    a = np.random.default_rng(1).standard_normal((1500, 2))
    t = time.perf_counter()
    for _ in range(40):
        d = a[:, None, :] - a[None, ::4, :]
        (d * d).sum(axis=2).min(axis=1)
    out["numpy_broadcast_40x_ms"] = (time.perf_counter() - t) * 1e3
    return out


def run_one(root, name):
    _prepare(root)
    payload = {"ops": _ops(root)} if name == "__ops__" else _suite(root, name)
    payload["name"] = name
    payload["python"] = sys.version.split()[0]
    return json.dumps(payload)


if __name__ == "__main__":
    print("RESULT " + run_one(sys.argv[1], sys.argv[2]))
