"""Diagnostic (not a test): reproduce the additional-line groove at an axis
crossing on the user's 2026-09-29 sample.

A V-family additional line crosses the H axis. The axis rows are hard-pinned
in both components and the line's weight is smoothstepped to zero over four
cells beside the orthogonal axis, so the displacement the line asks for
collapses to 0 in the band and a groove appears where the pattern (or the
refer-rect grid) crosses it. Record: docs/plan/2026-09-29-附加线跨轴凹槽.md.

Run:  py tests\\diag_additional_axis_notch.py
"""
import io
import json
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am  # noqa: E402

FIXTURE = os.path.join(ROOT, "tests", "fixtures", "additional_line_axis_notch.anproj")


def load_case():
    doc = json.load(io.open(FIXTURE, encoding="utf-8"))
    case = {}
    for view, key in (("mainView", "main"), ("textureView", "child")):
        unit = json.loads(doc[view]["scriptData"])["mapping_units"]["units"]["1"]
        strokes = []
        for asset in doc[view]["assets"]:
            for frame in asset["frames"]:
                for entry in frame["image"]["strokes"]:
                    stroke = entry["stroke"]
                    strokes.append({"property": stroke.get("property"),
                                    "points": [(p["x"], p["y"]) for p in stroke["points"]]})
        case[key] = {"assets": unit["assets"], "strokes": strokes}
    return case


def densify(pts, step=2.0):
    out = [tuple(pts[0])]
    for a, b in zip(pts, pts[1:]):
        n = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / step))
        for k in range(1, n + 1):
            t = k / n
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out


def bar(d):
    return "#" * int(d * 4)


def main():
    case = load_case()
    am._ACTIVE_UNIT["id"] = None
    am._MAPPING_ASSETS.clear()
    am._MAPPING_ASSETS["child"] = case["child"]["assets"]
    am._MAPPING_ASSETS["main"] = case["main"]["assets"]
    full, _ = am._mapper_from_assets(additional=True)   # with the additional line
    base, _ = am._mapper_from_assets(additional=False)  # axes only
    warp = full.warp
    grid = warp._grid
    print(f"grid {grid['nx']}x{grid['ny']}, cell {grid['du']:.1f} x {grid['dv']:.1f} Third px")

    # Where the additional line crosses the child H axis (l_v changes sign).
    line = case["child"]["assets"]["additional_line"]["lines"][0]["points"]
    prev = None
    crossing = None
    for p in densify(line, 2.0):
        c = full.coords(p)
        if c is None:
            prev = None
            continue
        if prev is not None and prev[1] * c[1] < 0:
            crossing = c
            break
        prev = c
    print("additional line crosses the H axis at Third", tuple(round(v, 1) for v in crossing))

    # Displacement (with line - without line) along the grid iso-line through
    # the crossing. main_of_third() applies the warp and the arc scaling
    # itself, so it takes raw Third coordinates.
    print("\n|with line - without line| along the iso-line l_h = const through the crossing (main px):")
    rows = []
    for i in range(-60, 61, 5):
        third = (crossing[0], float(i))
        f = full.main_of_third(third)
        b = base.main_of_third(third)
        d = math.hypot(f[0] - b[0], f[1] - b[1])
        rows.append((i, d))
        print(f"  l_v {i:4d}  {d:5.2f}  {bar(d)}")
    shoulders = [d for i, d in rows if 25 <= abs(i) <= 45]
    at_axis = [d for i, d in rows if abs(i) <= 5]
    depth = sum(shoulders) / len(shoulders) - sum(at_axis) / len(at_axis)
    print(f"\ngroove depth ~{depth:.2f} main px "
          f"(shoulders {sum(shoulders) / len(shoulders):.2f}, axis {sum(at_axis) / len(at_axis):.2f})")

    # The warp itself, in Third px, on the same iso-line.
    print("\nwarp displacement |U(p) - p| in Third px on the same iso-line:")
    for i in range(-60, 61, 10):
        third = (crossing[0], float(i))
        u = warp.apply(third)
        print(f"  l_v {i:4d}  {math.hypot(u[0] - third[0], u[1] - third[1]):5.2f}")

    # The same dip on the pattern strokes that cross the band, through the
    # real pipeline: full(p) vs base(p) on child points.
    print("\npattern strokes crossing the H axis band: displacement by l_v (main px):")
    for k, stroke in enumerate(case["child"]["strokes"]):
        samples = []
        for p in densify(stroke["points"], 2.0):
            c = full.coords(p)
            if c is None or abs(c[1]) > 60 or abs(c[0] - crossing[0]) > 60:
                continue  # only the pass near the crossing
            f, b = full(p), base(p)
            if f and b:
                samples.append((c[1], math.hypot(f[0] - b[0], f[1] - b[1])))
        if not samples or min(lv for lv, _ in samples) > -5 or max(lv for lv, _ in samples) < 5:
            continue
        samples.sort()
        print(f"  stroke {k}:")
        last = None
        for lv, d in samples:
            if last is None or abs(lv - last) >= 8:
                print(f"    l_v {lv:6.1f}  {d:5.2f}  {bar(d)}")
                last = lv
    return 0


if __name__ == "__main__":
    sys.exit(main())
