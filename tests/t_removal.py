"""Removal sync: deleting an additional line on EITHER board must remove the
pair on both, through the exact overlay-removal entry the app uses."""
import sys
import types

ROOT = r"C:\Users\admin\Documents\AnimeAn"
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am

H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]


def fresh_boards():
    am._MAPPING_ASSETS.clear()
    for view in ("child", "main"):
        am._MAPPING_ASSETS[view] = {
            am.H_PROPERTY: {"points": list(H), "width": 3.0},
            am.V_PROPERTY: {"points": list(V), "width": 3.0},
        }


def lines_of(view):
    return (am._MAPPING_ASSETS[view].get(am.ADDITIONAL_PROPERTY) or {}).get("lines") or []


for click_view in ("main", "child"):
    fresh_boards()
    am.run_additional_line_tool("child", [(-100.0, 50.0), (100.0, 50.0)])
    am.run_additional_line_tool("main", [(-100.0, -120.0), (100.0, -120.0)])
    assert len(lines_of("child")) == 2 and len(lines_of("main")) == 2
    # overlay ids as the app renders them on the clicked board
    items = [i for i in am.overlay_items(click_view)
             if str(i.get("id", "")).startswith(am.ADDITIONAL_PROPERTY + ":")]
    assert len(items) == 2, items
    target = items[0]["id"]
    am._overlay_removed({}, {}, {"overlay": {"id": target}, "view": click_view})
    assert len(lines_of("child")) == 1, (click_view, lines_of("child"))
    assert len(lines_of("main")) == 1, (click_view, lines_of("main"))
    # the SAME pair must be gone on both sides
    cid = am._line_id(lines_of("child")[0], 0)
    mid = am._line_id(lines_of("main")[0], 0)
    assert cid == mid, (cid, mid)
    print(f"x on {click_view}: pair removed on both boards, survivor id {cid}")

# legacy mismatched ids: the pair's halves carry DIFFERENT ids (created by
# older builds). Removal by id misses the partner - the geometric fallback
# must still take it out.
fresh_boards()
am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY] = {"lines": [
    {"points": [(-100.0, 50.0), (100.0, 50.0)], "width": 2.5, "id": 3},
]}
am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY] = {"lines": [
    {"points": [(-100.0, 50.0), (100.0, 50.0)], "width": 2.5, "id": 7},
]}
am._overlay_removed({}, {}, {"overlay": {"id": f"{am.ADDITIONAL_PROPERTY}:7"},
                             "view": "main"})
assert not lines_of("main"), lines_of("main")
assert not lines_of("child"), lines_of("child")
print("mismatched-id pair: geometric fallback removed the partner too")

# review R1: deleting a GENUINE orphan must not touch a healthy pair
fresh_boards()
am.run_additional_line_tool("child", [(-100.0, 100.0), (100.0, 100.0)])  # healthy pair id 0
am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"].append(
    {"points": [(-100.0, 110.0), (100.0, 110.0)], "width": 2.5, "id": 42})  # orphan
am._overlay_removed({}, {}, {"overlay": {"id": f"{am.ADDITIONAL_PROPERTY}:42"},
                             "view": "child"})
assert len(lines_of("child")) == 1 and am._line_id(lines_of("child")[0], 0) == 0
assert len(lines_of("main")) == 1 and am._line_id(lines_of("main")[0], 0) == 0
print("orphan removal leaves the healthy pair intact on both boards")

# review R2: mismatched-id pair with a real BEND still heals (chord endpoints
# coincide even though the bent line is far from its chord partner)
fresh_boards()
bow = [(x, 50.0 + 60.0 * (1.0 - (x / 100.0) ** 2)) for x in range(-100, 101, 10)]
am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY] = {"lines": [
    {"points": [tuple(p) for p in bow], "width": 2.5, "id": 7}]}
am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY] = {"lines": [
    {"points": [(-100.0, 50.0), (100.0, 50.0)], "width": 2.5, "id": 3}]}  # the chord
am._overlay_removed({}, {}, {"overlay": {"id": f"{am.ADDITIONAL_PROPERTY}:7"},
                             "view": "main"})
assert not lines_of("main"), lines_of("main")
assert not lines_of("child"), lines_of("child")
print("bent mismatched-id pair heals via chord-endpoint matching")

# review R2b: a bent mismatched half near an UNRELATED pair must not take the
# wrong line - the paired candidate is ineligible, and with no eligible
# match the fallback gives up loudly instead of guessing.
fresh_boards()
am.run_additional_line_tool("child", [(-100.0, 40.0), (100.0, 40.0)])   # healthy pair id 0
bow_hi = [(x, 90.0 + 60.0 * (1.0 - (x / 100.0) ** 2)) for x in range(-100, 101, 10)]
am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"].append(
    {"points": [tuple(p) for p in bow_hi], "width": 2.5, "id": 9})
am._MAPPING_ASSETS["child"][am.ADDITIONAL_PROPERTY]["lines"].append(
    {"points": [(-100.0, 300.0), (100.0, 300.0)], "width": 2.5, "id": 5})  # far stray
am._overlay_removed({}, {}, {"overlay": {"id": f"{am.ADDITIONAL_PROPERTY}:9"},
                             "view": "main"})
assert len(lines_of("main")) == 1 and am._line_id(lines_of("main")[0], 0) == 0
child_ids = sorted(am._line_id(l, i) for i, l in enumerate(lines_of("child")))
assert child_ids == [0, 5], child_ids   # healthy pair AND the far stray survive
print("no eligible chord match: fallback gives up, nothing unrelated deleted")

# refer rect on MAIN renders the warped mapping
fresh_boards()
bow_main = [(x, 50.0 + 30.0 * (1.0 - (x / 100.0) ** 2))
            for x in range(-100, 101, 10)]
am.run_additional_line_tool("main", bow_main)
am._REFER_RECT["main"] = True
am._REFER_RECT["child"] = True
am._invalidate_grid_cache()
main_grid = am._grid_overlay_items("main")
child_grid = am._grid_overlay_items("child")
assert main_grid and child_grid
# identity boards + a warp: the main grid must DIFFER from the child grid
# near the line, and match it far away.
flat_c = [p for item in child_grid for p in item["points"]]
flat_m = [p for item in main_grid for p in item["points"]]
assert len(flat_c) == len(flat_m)
import math as _m
diffs = [_m.hypot(a[0] - b[0], a[1] - b[1]) for a, b in zip(flat_c, flat_m)]
assert max(diffs) > 5.0, max(diffs)          # the warp bends the main grid
far = [d for (a, _), d in zip(flat_c, diffs) if abs(a) > 500.0]
assert not far or max(far) < 1e-6            # far from the line: identical
am._REFER_RECT["main"] = False
am._REFER_RECT["child"] = False
print(f"refer rect: main grid bends with the warp (max {max(diffs):.1f} px), "
      "far field untouched")

print("t_removal: ALL OK")
