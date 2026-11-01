"""The Refer Rect options in Tool Options: the grid reaches past the axis ends
without moving the lines inside, the block sits on every Mapping tool, and it
edits the focused unit's settings when there are units."""
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
sys.modules["animean_python"].ui = types.SimpleNamespace(
    set_overlay=lambda *args: None, history_commit=lambda *args: None,
    refresh=lambda: None, refresh_tool_options=lambda: None)
import auto_mapping as am
import python_hooks
import toolcontrol


def line(a, b, n=24):
    return [[a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n] for i in range(n + 1)]


def reset():
    am._MAPPING_ASSETS.clear()
    am._UNIT_META.clear()
    am._ACTIVE_UNIT["id"] = None
    am._REFER_RECT.update(child=False, main=False)
    am._GRID.update(divisions=5, extend=0)
    am._invalidate_grid_cache()


def axes(view="child"):
    assets = am._assets_for(view)
    assets[am.H_PROPERTY] = {"points": line((-120.0, 0.0), (120.0, 0.0)), "width": 3.0}
    assets[am.V_PROPERTY] = {"points": line((0.0, -80.0), (0.0, 80.0)), "width": 3.0}


def option(hook, value):
    am._refer_rect_option({"hook": hook, "value": value})


def box(items):
    points = [p for item in items for p in item["points"]]
    return (min(p[0] for p in points), min(p[1] for p in points),
            max(p[0] for p in points), max(p[1] for p in points))


def close(a, b, tol=1e-6):
    return all(abs(x - y) < tol for x, y in zip(a, b))


def controls(tool):
    return {c["name"]: c for c in toolcontrol.options_for_extra_tool(tool)["controls"]}


GRID_NAMES = ("refer_rect", "refer_rect_divisions", "refer_rect_extend")

# 1) The tick shows the grid on both boards; Off is the historical lattice.
reset()
axes()
assert am._grid_overlay_items("child") == []
option("refer_rect", "on")
assert am.refer_rect_enabled("child") and am.refer_rect_enabled("main")
plain = am._grid_overlay_items("child")
assert len(plain) == 10 and len(plain[0]["points"]) == 25
assert close(box(plain), (-120.0, -80.0, 120.0, 80.0))

# 2) One step reaches half a half-axis past each end, the outward lines keep the
#    spacing, and every line of the plain grid is still there, on the same spot.
option("refer_rect_extend", 1)
wider = am._grid_overlay_items("child")
assert len(wider) == 14 and len(wider[0]["points"]) == 37
assert close(box(wider), (-180.0, -120.0, 180.0, 120.0))
for item in plain:
    middle = item["points"][12]
    assert any(any(close(middle, p) for p in other["points"]) for other in wider)

# 3) Raising the density keeps the reach; it adds lines, it does not pull in.
option("refer_rect_divisions", "9")
fine = am._grid_overlay_items("child")
assert len(fine) == 2 * (9 + 4)
assert close(box(fine), (-180.0, -120.0, 180.0, 120.0))

# 4) Out of range clamps, junk changes nothing.
option("refer_rect_extend", 99)
assert am._grid_extend() == am.GRID_EXTEND_MAX
option("refer_rect_extend", "x")
option("refer_rect_divisions", 7)
assert am._grid_extend() == am.GRID_EXTEND_MAX and am._grid_divisions() == 9
assert am.refer_rect_options() == {"shown": True, "divisions": 9, "extend": am.GRID_EXTEND_MAX}
am._refer_rect_option({"hook": "something_else", "value": 1})
assert am._grid_divisions() == 9

# 5) The main board's own frame reaches out the same way while no mapper exists.
reset()
axes("main")
option("refer_rect", "on")
option("refer_rect_extend", 2)
assert close(box(am._grid_overlay_items("main")), (-240.0, -160.0, 240.0, 160.0))

# 6) Every Mapping tool shows the block, values included; the View menu has
#    none of it left.
reset()
option("refer_rect", "on")
option("refer_rect_divisions", 17)
option("refer_rect_extend", 3)
for tool in ("h_center_line", "v_center_line", "mapping_area", "additional_line",
             "free_out_line", "auto_mapping_2", "choice_line", "deform_line"):
    panel = controls(tool)
    assert all(name in panel for name in GRID_NAMES), tool
    assert panel["refer_rect"]["value"] == "on"
    assert panel["refer_rect_divisions"]["value"] == "17"
    assert panel["refer_rect_extend"]["value"] == 3
    assert panel["refer_rect_extend"]["max"] == am.GRID_EXTEND_MAX
    rows = [c["row"] for c in toolcontrol.options_for_extra_tool(tool)["controls"]]
    assert len(rows) == len(set(rows)), tool
assert "refer_rect" not in {c["name"] for c in toolcontrol.options_for_tool("pen")["controls"]}
menus = [m for m in python_hooks._MENUS if m.get("name") == am.VIEW_MENU_NAME]
assert [m["host"] for m in menus] == ["child"]
assert [item["name"] for item in am._view_menu_items()] == [am.OCCLUSION_BUTTON]

# 7) Units: the block edits the focused unit, the document globals stay put, and
#    with no unit focused there is nothing to edit, so nothing shows.
reset()
saved = am._save_units
am._save_units = lambda view: None
am._UNIT_META["u1"] = {"settings": {}}
assert am.refer_rect_options() is None
assert not any(name in controls("choice_line") for name in GRID_NAMES)
am._ACTIVE_UNIT["id"] = "u1"
option("refer_rect", "on")
option("refer_rect_divisions", "33")
option("refer_rect_extend", "2")
assert am._UNIT_META["u1"]["settings"] == {"show_grid": True, "grid_divisions": 33, "grid_extend": 2}
assert am._GRID == {"divisions": 5, "extend": 0} and not any(am._REFER_RECT.values())
assert am.refer_rect_options() == {"shown": True, "divisions": 33, "extend": 2}
am._SETTINGS_TARGET["unit"] = "u1"
am._unit_setting_changed(None, None, {"hook": "unit_setting", "name": "grid_extend", "value": 9})
assert am._UNIT_META["u1"]["settings"]["grid_extend"] == am.GRID_EXTEND_MAX
layout = {c["name"]: c for c in am._unit_settings_layout()["controls"]}
assert layout["grid_extend"]["value"] == am.GRID_EXTEND_MAX
rows = [c["row"] for c in am._unit_settings_layout()["controls"]]
assert len(rows) == len(set(rows))
am._SETTINGS_TARGET["unit"] = None
am._save_units = saved
reset()

print("t_refer_rect: 7 cases ok")
