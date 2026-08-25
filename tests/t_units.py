"""Mapping units (automapping layers): create, focus-activate, live re-run
in place, duplicate, per-unit settings, and scriptData round-trip
(user request 2026-08-25: layer-manager refactor)."""
import json
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am  # noqa: E402


# --- fakes ------------------------------------------------------------------

class FakeImage:
    def __init__(self):
        self.strokes = []
        self.fills = []

    def stroke_count(self):
        return len(self.strokes)

    def add_stroke_object(self, obj):
        self.strokes.append(obj)

    def add_fill_region(self, commands, color, prop, *_rest):
        self.fills.append((commands, color, prop))


class FakeStrokeObject:
    def __init__(self, points, color, width):
        self.points = points
        self.color = color
        self.width = width
        self.property = ""
        self.pen_style = 1


class FakeVectorLogic:
    @staticmethod
    def make_stroke_object(points, color, width, _index, _a, _b):
        return FakeStrokeObject(points, color, width)

    @staticmethod
    def make_stroke_object_from_path(_commands, flat, color, width, _index):
        return FakeStrokeObject(flat, color, width)


class FakeScene:
    """The layer/group/scriptData subset the unit machinery touches."""

    def __init__(self, name):
        self.name = name
        self.columns = []          # [{"id", "name", "visible", "type"}]
        self.groups = {}           # gid -> {"name","tag","layer_ids":[...]}
        # (row, layer id) -> FakeImage. ROW-AWARE on purpose: the C++ xsheet
        # exposes a column only on the rows where it has a cell, so a fake that
        # answered every row with one image could not tell "the run wrote to
        # the current frame" from "the run wrote to frame 1".
        self.cells = {}
        self.pattern = {}          # layer id -> [stroke dict] (child pattern)
        self._script = ""
        self._next_id = 1
        self._next_gid = 1
        self._frame = 0
        self._layer = -1
        self._asset = -1

    # --- selection ---
    def current_frame(self):
        return self._frame

    def current_layer(self):
        return self._layer

    def current_asset(self):
        return self._asset

    def set_current_frame(self, value):
        self._frame = value

    def set_current_layer(self, value):
        self._layer = value

    def set_current_asset(self, value):
        self._asset = value

    # --- layers ---
    def _unique(self, base):
        names = {c["name"] for c in self.columns}
        if base not in names:
            return base
        n = 1
        while f"{base}{n}" in names:
            n += 1
        return f"{base}{n}"

    def add_layer(self, kind="vector"):
        column = {"id": self._next_id, "name": f"layer {self._next_id}",
                  "visible": True, "type": kind}
        self._next_id += 1
        self.columns.append(column)
        index = len(self.columns) - 1
        self._layer = index
        # AnimeSceneModel::addLayer() exposes the column on m_currentFrame, and
        # rewrites a negative current frame to 0 first (animemodel.cpp:1665).
        row = max(self._frame, 0)
        self._frame = row
        self.cells.setdefault((row, column["id"]), FakeImage())
        return index

    def add_fill_layer(self):
        return self.add_layer("fill")

    def layer_count(self):
        return len(self.columns)

    def layer_name(self, index):
        return self.columns[index]["name"]

    def set_layer_name(self, index, name):
        self.columns[index]["name"] = self._unique(name)

    def set_layer_visible(self, index, visible):
        self.columns[index]["visible"] = bool(visible)

    def layer_visible(self, index):
        return self.columns[index]["visible"]

    def move_layer(self, src, dst):
        if src == dst or not (0 <= src < len(self.columns)):
            return False
        column = self.columns.pop(src)
        self.columns.insert(dst, column)
        return True

    def remap_fill_source_layers_after_move(self, *_a):
        pass

    def remap_fill_source_layers_after_delete(self, *_a):
        pass

    def delete_layer(self, index):
        if not (0 <= index < len(self.columns)):
            return False
        column = self.columns.pop(index)
        for group in self.groups.values():
            group["layer_ids"] = [i for i in group["layer_ids"]
                                  if i != column["id"]]
        for key in [k for k in self.cells if k[1] == column["id"]]:
            self.cells.pop(key)
        if self._layer >= len(self.columns):
            self._layer = len(self.columns) - 1
        return True

    def layer_id_at(self, index):
        if 0 <= index < len(self.columns):
            return self.columns[index]["id"]
        return 0

    def layer_index_for_id(self, lid):
        for index, column in enumerate(self.columns):
            if column["id"] == lid:
                return index
        return -1

    # --- groups (flat: no nesting needed for these tests) ---
    def create_layer_group(self, name, layers=(), groups=(), collapsed=False):
        ids = [self.layer_id_at(i) for i in layers if self.layer_id_at(i)]
        gid = self._next_gid
        self._next_gid += 1
        self.groups[gid] = {"name": name, "tag": "", "layer_ids": ids,
                            "collapsed": bool(collapsed)}
        return gid

    def set_layer_group_name(self, gid, name):
        if gid in self.groups:
            self.groups[gid]["name"] = name
            return True
        return False

    def set_layer_group_collapsed(self, gid, collapsed):
        if gid in self.groups:
            self.groups[gid]["collapsed"] = bool(collapsed)
            return True
        return False

    def layer_group_collapsed(self, gid):
        return self.groups.get(gid, {}).get("collapsed", False)

    def set_layer_group_tag(self, gid, tag):
        if gid in self.groups:
            self.groups[gid]["tag"] = tag
            return True
        return False

    def layer_group_tag(self, gid):
        return self.groups.get(gid, {}).get("tag", "")

    def group_id_for_layer(self, index, tag=""):
        lid = self.layer_id_at(index)
        for gid, group in self.groups.items():
            if lid in group["layer_ids"] and (not tag or group["tag"] == tag):
                return gid
        return 0

    def add_layers_to_group(self, gid, layers):
        group = self.groups.get(gid)
        if group is None:
            return 0
        added = 0
        for index in layers:
            lid = self.layer_id_at(index)
            if lid and lid not in group["layer_ids"]:
                group["layer_ids"].append(lid)
                added += 1
        return added

    def layer_ids_in_group(self, gid):
        return list(self.groups.get(gid, {}).get("layer_ids", []))

    # --- content ---
    def image_at(self, row, layer_index, create=True, _asset_type="vector"):
        lid = self.layer_id_at(layer_index)
        key = (row, lid)
        if key not in self.cells:
            if not create:
                return None
            self.cells[key] = FakeImage()
        return self.cells[key]

    def cell_asset_index(self, row, layer_index):
        lid = self.layer_id_at(layer_index)
        return 0 if (row, lid) in self.cells else -1

    def rows_for_layer(self, lid):
        """Test helper: every row this column is exposed on."""
        return sorted(row for (row, other) in self.cells if other == lid)

    def frame_count(self):
        return max(4, max((row for row, _ in self.cells), default=0) + 1)

    def get_structure(self):
        layers = []
        for index, column in enumerate(self.columns):
            cells = []
            for row in self.rows_for_layer(column["id"]):
                cells.append({"layer_index": index, "frame_index": row,
                              "asset_index": index, "frame_id": 1,
                              "empty": column["id"] not in self.pattern,
                              "stroke_count": len(self.pattern.get(column["id"], [])),
                              "fill_count": 0})
            layers.append({
                "index": index, "name": column["name"],
                "column_name": column["name"], "visible": column["visible"],
                "internal": False, "locked": False, "opacity": 1.0,
                "type": column["type"],
                "cells": cells,
            })
        count = self.frame_count()
        return {"sceneName": f"{self.name}_paint_view", "layers": layers,
                "current_frame": self._frame, "current_layer": self._layer,
                "frame_count": count, "layer_count": len(self.columns),
                "asset_count": 0,
                "frames": [{"index": i, "num": i + 1, "name": str(i + 1)}
                           for i in range(count)],
                "assets": []}

    def cell_to_dict(self, layer_index, _frame, _to_poly=True, _step=4.0):
        lid = self.layer_id_at(layer_index)
        return {"image": {"strokes": list(self.pattern.get(lid, [])),
                          "fills": []}}

    def canvas_size(self):
        return (800, 600)

    # --- scriptData ---
    def script_data(self):
        return self._script

    def set_script_data(self, value):
        self._script = value


class FakeUi:
    def __init__(self, scenes):
        self.scenes = scenes
        self.commits = []

    def refresh(self):
        pass

    def refresh_tool_options(self):
        pass

    def set_current(self, frame=None, layer=None, asset=None):
        if layer is not None:
            self.scenes["main"].set_current_layer(layer)

    def history_commit(self, label, view=""):
        self.commits.append((label, view))

    def set_overlay(self, *_a):
        pass

    class _Panel:
        @staticmethod
        def refresh():
            pass

    widget = _Panel()
    layer = _Panel()
    main = _Panel()
    children = _Panel()


def fresh_world():
    scenes = {"main": FakeScene("main"), "child": FakeScene("child")}
    fake = types.SimpleNamespace(ui=FakeUi(scenes),
                                 vectorlogic=FakeVectorLogic())
    am._scene_model = lambda view: scenes[view]
    am._animean = lambda: fake
    am._MAPPING_ASSETS.clear()
    am._UNIT_ASSETS.clear()
    am._UNIT_META.clear()
    am._CURVE_MODE["value"] = "polyline"   # fake strokes carry no commands
    am._ACTIVE_UNIT["id"] = None
    am._SETTINGS_TARGET["unit"] = None
    am._RUN_GUARD["depth"] = 0
    am._invalidate_grid_cache()
    return scenes, fake


def stroke(points, width=3.0, prop=""):
    return {"property": prop, "width": width,
            "color": {"r": 10, "g": 20, "b": 30, "a": 255},
            "polylines": [[{"x": x, "y": y} for x, y in points]]}


H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]


def install_guides(uid):
    for view in ("main", "child"):
        assets = am._UNIT_ASSETS[view].setdefault(uid, {})
        assets[am.H_PROPERTY] = {"points": list(H), "width": 3.0}
        assets[am.V_PROPERTY] = {"points": list(V), "width": 3.0}


# 1) CREATE: a tagged group + one focusable primary member; the unit
#    activates immediately and its config persists to scriptData.
scenes, fake = fresh_world()
scenes["child"].pattern = {}
uid = am._create_unit()
assert uid is not None and uid in am._UNIT_META
main = scenes["main"]
gid = int(uid)
assert main.layer_group_tag(gid) == am.UNIT_TAG
assert main.groups[gid]["name"] == am.UNIT_LAYER_TITLE
assert main.layer_group_collapsed(gid)   # one collapsed row in the panel
assert am._ACTIVE_UNIT["id"] == uid
primary = am._UNIT_META[uid]["primary"]
assert main.layer_index_for_id(primary) == main.current_layer()
stored = json.loads(main.script_data())[am.UNIT_STORE_KEY]
assert uid in stored["units"]
print("1) unit created: tagged group, focused primary, persisted config")

# 2) FOCUS GATING: overlays exist only while the unit has focus.
install_guides(uid)
items = am.overlay_items("main")
assert any(item["id"] == am.H_PROPERTY for item in items)
am._activate_unit(None)
assert am.overlay_items("main") == []
am._activate_unit(uid)
assert any(item["id"] == am.V_PROPERTY for item in items)
print("2) overlays follow unit focus: shown on enter, hidden on leave")

# 3) LAYERCHANGE hook drives activation.
outside = main.add_layer()
main.set_current_layer(outside)
am._layer_focus_event({"view": "main", "layer": outside, "event": "layerchange"})
assert am._ACTIVE_UNIT["id"] is None
inside = main.layer_index_for_id(primary)
main.set_current_layer(inside)
am._layer_focus_event({"view": "main", "layer": inside, "event": "layerchange"})
assert am._ACTIVE_UNIT["id"] == uid
am._layer_focus_event({"view": "child", "layer": 0, "event": "layerchange"})
assert am._ACTIVE_UNIT["id"] == uid   # child focus never deactivates
print("3) layerchange activates/deactivates; child focus is ignored")

# 4) RUN IN PLACE: the run's layers join the unit group, the old primary is
#    replaced, and focus survives on the new front layer.
scenes["child"].pattern = {scenes["child"].layer_id_at(scenes["child"].add_layer()):
                           [stroke([(-100.0, 50.0), (100.0, 50.0)])]}
before_ids = set(main.layer_ids_in_group(gid))
ok = am._perform_mapping()
assert ok, "the unit run must succeed"
after_ids = set(main.layer_ids_in_group(gid))
meta = am._UNIT_META[uid]
assert meta["primary"] != primary          # replaced, not appended
assert str(meta["primary"]) in meta["members"]
assert meta["members"][str(meta["primary"])]["role"] == "front"
assert not (before_ids & after_ids), "old members must be retired"
assert main.layer_index_for_id(meta["primary"]) == main.current_layer()
count_after_first = main.layer_count()
print(f"4) run installed {len(after_ids)} member(s) in place, focus kept")

# 5) RE-RUN stays in place: layer count stable, members swapped again.
first_members = set(meta["members"])
ok = am._perform_mapping()
assert ok
assert main.layer_count() == count_after_first
assert set(am._UNIT_META[uid]["members"]) != first_members
assert int(uid) in main.groups and main.layer_group_tag(gid) == am.UNIT_TAG
print("5) re-run replaces members in place (no layer-count growth)")

# 6) AUTO-RUN gate: guard + auto_render setting.
runs = []
real_run = am._run
am._run = lambda: runs.append(1)
try:
    am._maybe_auto_run()
    assert runs, "focused unit with auto_render on must re-run"
    runs.clear()
    am._UNIT_META[uid]["settings"]["auto_render"] = False
    am._maybe_auto_run()
    assert not runs
    am._UNIT_META[uid]["settings"]["auto_render"] = True
    am._RUN_GUARD["depth"] = 1
    am._maybe_auto_run()
    assert not runs
    am._RUN_GUARD["depth"] = 0
    am._activate_unit(None)
    am._maybe_auto_run()
    assert not runs
    am._activate_unit(uid)
finally:
    am._run = real_run
print("6) auto-run respects focus, guard and the Live Re-render toggle")

# 7) SETTINGS: the Advanced Settings window edits the stashed target unit;
#    front/back/crease checkboxes drive member-layer visibility.
am._SETTINGS_TARGET["unit"] = uid
layout = am._unit_settings_layout()
names = [c["name"] for c in layout["controls"]]
for expected in ("show_h", "show_v", "show_grid", "front_visible",
                 "back_visible", "seal_visible", "auto_render"):
    assert expected in names, expected
am._unit_setting_changed(None, None,
                         {"hook": "unit_setting", "name": "front_visible",
                          "value": "off"})
front_index = main.layer_index_for_id(am._UNIT_META[uid]["primary"])
assert main.layer_visible(front_index) is False
am._unit_setting_changed(None, None,
                         {"hook": "unit_setting", "name": "front_visible",
                          "value": "on"})
assert main.layer_visible(front_index) is True
am._unit_setting_changed(None, None,
                         {"hook": "unit_setting", "name": "show_h",
                          "value": "off"})
assert not any(item["id"] == am.H_PROPERTY
               for item in am.overlay_items("main"))
assert any(item["id"] == am.V_PROPERTY
           for item in am.overlay_items("main"))
am._unit_setting_changed(None, None,
                         {"hook": "unit_setting", "name": "show_h",
                          "value": "on"})
print("7) advanced settings: member visibility + per-component overlays")

# 8) MENU PROVIDER: unit rows offer settings + duplicate, every context
#    offers the typed-layer creation entries, and the target unit is stashed.
am._SETTINGS_TARGET["unit"] = None
entries = am._layer_menu_items({"view": "main", "kind": "group",
                                "group": gid, "tag": am.UNIT_TAG})
names = [e.get("name") for e in entries]
assert "unit_settings" in names and am.DUPLICATE_UNIT_ACTION in names
assert am.NEW_UNIT_ACTION in names and am.NEW_LINE_LAYER_ACTION in names
assert am._SETTINGS_TARGET["unit"] == uid
panel_entries = am._layer_menu_items({"view": "child", "kind": "panel"})
panel_names = [e.get("name") for e in panel_entries]
assert am.NEW_LINE_LAYER_ACTION in panel_names
assert am.NEW_UNIT_ACTION not in panel_names   # units live on main
settings_entry = next(e for e in entries if e.get("name") == "unit_settings")
assert settings_entry["kind"] == "settings"
assert settings_entry["settings"] == am.UNIT_SETTINGS_NAME
print("8) layer menu: unit entries + creation entries, target stashed")

# 9) DUPLICATE: config copied into a fresh unit, nothing shared, auto-run.
runs = []
real_run = am._run
am._run = lambda: runs.append(1)
try:
    dup = am._duplicate_unit(uid)
finally:
    am._run = real_run
assert dup and dup != uid and dup in am._UNIT_META
assert runs, "duplicating re-renders the copy"
assert am._ACTIVE_UNIT["id"] == dup
src_assets = am._UNIT_ASSETS["main"][uid]
dup_assets = am._UNIT_ASSETS["main"][dup]
assert dup_assets[am.H_PROPERTY]["points"] == src_assets[am.H_PROPERTY]["points"]
assert dup_assets[am.H_PROPERTY] is not src_assets[am.H_PROPERTY]
dup_assets[am.H_PROPERTY]["points"][0] = (-999.0, 0.0)
assert src_assets[am.H_PROPERTY]["points"][0] != (-999.0, 0.0)
print("9) duplicate: independent config copy, re-rendered, focus moved")

# 10) ROUND-TRIP: units survive scriptData reload; the active unit follows
#     the restored current layer.
saved_meta = json.loads(json.dumps(am._UNIT_META))
am._UNIT_META.clear()
am._UNIT_ASSETS["main"].clear()
am._load_units("main", scenes["main"])
assert set(am._UNIT_META) == set(saved_meta)
for check_uid in saved_meta:
    assert am._UNIT_META[check_uid]["primary"] == saved_meta[check_uid]["primary"]
assert am._ACTIVE_UNIT["id"] == dup   # current layer sits in the duplicate
print("10) scriptData round-trip restores units and re-derives focus")

# 11) LEGACY MODE: with no units, the old scratch assets + always-on
#     overlays still work (the pre-unit workflow and every t_* suite).
scenes, fake = fresh_world()
am._MAPPING_ASSETS["main"] = {am.H_PROPERTY: {"points": list(H), "width": 3.0},
                              am.V_PROPERTY: {"points": list(V), "width": 3.0}}
assert am._assets_for("main") is am._MAPPING_ASSETS["main"]
items = am.overlay_items("main")
assert any(item["id"] == am.H_PROPERTY for item in items)
print("11) legacy mode intact: scratch assets, always-on overlays")

# 12) CONVERT: a pre-refactor nested "Auto Mapping" group becomes a unit -
#     snapshots feed the config, members classify by name, snapshot layers
#     die, and the group collapses into the one-row look.
scenes, fake = fresh_world()
main = scenes["main"]


def _legacy_stroke(points, prop, width=3.0, color=(0, 0, 255)):
    return {"property": prop, "width": width,
            "color": {"r": color[0], "g": color[1], "b": color[2], "a": 255},
            "polylines": [[{"x": x, "y": y} for x, y in points]]}


legacy_members = []
for name, prop, pts in (
        ("mapped layer", "", [(0.0, 0.0), (10.0, 0.0)]),
        ("mapped layer back", "", [(0.0, 5.0), (10.0, 5.0)]),
        ("mapped layer crease", "", [(0.0, 9.0), (10.0, 9.0)]),
        ("H axis", am.H_GUIDE_LAYER_PROPERTY, H),
        ("V axis", am.V_GUIDE_LAYER_PROPERTY, V)):
    index = main.add_layer()
    main.set_layer_name(index, name)
    legacy_members.append(index)
    if prop:
        main.pattern[main.layer_id_at(index)] = [_legacy_stroke(pts, prop)]
legacy_gid = main.create_layer_group("Auto Mapping", legacy_members, [], False)

entries = am._layer_menu_items({"view": "main", "kind": "group",
                                "group": legacy_gid, "tag": ""})
assert any(e.get("name") == am.CONVERT_UNIT_ACTION for e in entries)
uid12 = am._convert_group_to_unit(main, legacy_gid, list(legacy_members))
assert uid12 == str(legacy_gid) and uid12 in am._UNIT_META
assert main.layer_group_tag(legacy_gid) == am.UNIT_TAG
assert main.layer_group_collapsed(legacy_gid)
assert main.groups[legacy_gid]["name"] == am.UNIT_LAYER_TITLE
assets12 = am._UNIT_ASSETS["main"][uid12]
assert assets12[am.H_PROPERTY]["points"] == [tuple(p) for p in H]
assert assets12[am.V_PROPERTY]["points"] == [tuple(p) for p in V]
names_left = [main.layer_name(i) for i in range(main.layer_count())]
assert not any(n.startswith("H axis") or n.startswith("V axis")
               for n in names_left)
roles12 = {info["role"] for info in am._UNIT_META[uid12]["members"].values()}
assert roles12 == {"front", "back", "seal"}
assert am._ACTIVE_UNIT["id"] == uid12
print("12) legacy group converted: config adopted, snapshots retired")

# 13) HEAL: when the unit's group was deleted (pruned), the next run
#     re-houses the unit in a fresh tagged group instead of leaking loose
#     layers forever.
scenes, fake = fresh_world()
main = scenes["main"]
uid13 = am._create_unit()
install_guides(uid13)
scenes["child"].pattern = {scenes["child"].layer_id_at(scenes["child"].add_layer()):
                           [stroke([(-100.0, 40.0), (100.0, 40.0)])]}
del main.groups[int(uid13)]          # the panel's Delete Group
assert am._perform_mapping()
assert uid13 not in am._UNIT_META    # migrated away from the dead id
healed = am._ACTIVE_UNIT["id"]
assert healed and healed in am._UNIT_META
assert main.layer_group_tag(int(healed)) == am.UNIT_TAG
assert set(main.layer_ids_in_group(int(healed))) ==     {int(k) for k in am._UNIT_META[healed]["members"]}
print("13) deleted unit group healed into a fresh tagged group on re-run")

# 14) LOAD PRUNE: units whose tagged group is gone do not survive a
#     scriptData reload - unit mode unlatches when the last one dies.
am._save_units("main")
del main.groups[int(healed)]
am._UNIT_META.clear(); am._UNIT_ASSETS["main"].clear()
am._load_units("main", main)
assert healed not in am._UNIT_META
assert not am._UNIT_META               # last unit dead -> legacy mode again
assert am._ACTIVE_UNIT["id"] is None
print("14) reload prunes dead units; legacy mode returns")

# 15) CAPTURE ORDER: the implicit unit auto-create must not shift the layer
#     index out from under the stroke being captured.
scenes, fake = fresh_world()
main = scenes["main"]
child = scenes["child"]
seed_uid = am._create_unit()           # unit mode on...
am._activate_unit(None)                # ...but nothing focused
removed = []
child_layer = child.add_layer()
child_lid = child.layer_id_at(child_layer)
guide_stroke = stroke(H, prop=am.H_PROPERTY)
child.pattern[child_lid] = [guide_stroke]
child.remove_stroke = lambda row, layer, index, _lid=child_lid: removed.append(
    (row, layer, index, child.layer_id_at(layer)))
am._capture_mapping_item({"row": 0, "layer": child_layer, "asset": 0,
                          "frame_id": 1},
                         {"index": 0},
                         {"property": am.H_PROPERTY, "view": "child",
                          "event": "linefinish", "tool": "extra"})
assert removed and removed[0][3] == child_lid   # removed from the RIGHT layer
new_uid = am._ACTIVE_UNIT["id"]
assert new_uid and new_uid != seed_uid          # auto-created a fresh unit
assets15 = am._UNIT_ASSETS["child"][new_uid]
assert assets15[am.H_PROPERTY]["points"] == [tuple(p) for p in H]
print("15) capture reads/removes the stroke before the auto-created unit shifts indices")

# 16) UNITS BY DEFAULT: guides drawn before any unit existed migrate into
#     the implicitly created unit (capture and run-button flows).
scenes, fake = fresh_world()
main = scenes["main"]
child = scenes["child"]
am._MAPPING_ASSETS["main"] = {am.H_PROPERTY: {"points": list(H), "width": 3.0},
                              am.V_PROPERTY: {"points": list(V), "width": 3.0}}
runs16 = []
real_run16 = am._run
am._run = lambda: runs16.append(1)
try:
    am._auto_mapping_button(None, None, {})
finally:
    am._run = real_run16
uid16 = am._ACTIVE_UNIT["id"]
assert uid16 and uid16 in am._UNIT_META      # button created a unit
assert runs16
adopted16 = am._UNIT_ASSETS["main"][uid16]
assert adopted16[am.H_PROPERTY]["points"] == [tuple(p) for p in H]
assert not am._MAPPING_ASSETS["main"]        # scratch adopted, not copied
# a guide capture with no unit at all also creates one:
scenes, fake = fresh_world()
child = scenes["child"]
cl = child.add_layer()
child.pattern[child.layer_id_at(cl)] = [stroke(H, prop=am.H_PROPERTY)]
child.remove_stroke = lambda *a: None
am._capture_mapping_item({"row": 0, "layer": cl, "asset": 0, "frame_id": 1},
                         {"index": 0},
                         {"property": am.H_PROPERTY, "view": "child",
                          "event": "linefinish", "tool": "extra"})
assert am._ACTIVE_UNIT["id"] is not None
print("16) units by default: button and capture auto-create + adopt scratch")

# 17) FRAME ANCHORING (R3-5 regression): a run invoked on frame 3 (row 2) puts
#     BOTH halves of every output layer on row 2 - the cell add_layer() exposes
#     the column on AND the cell flush() writes the artwork into. The old code
#     let those come from two sources (`row` was passed to
#     _create_mapped_layer and then ignored, so the column was exposed wherever
#     the selection sat), which is how output ended up on frame 1.
scenes, fake = fresh_world()
main = scenes["main"]
child = scenes["child"]
main.set_current_frame(2)
uid17 = am._create_unit()
install_guides(uid17)
child.pattern = {child.layer_id_at(child.add_layer()):
                 [stroke([(-100.0, 50.0), (100.0, 50.0)])]}
assert main.current_frame() == 2, "creating a unit must not move the frame"
assert am._perform_mapping()
assert main.current_frame() == 2, "a run must not move the frame either"
members17 = am._UNIT_META[uid17]["members"]
assert members17
for lid in members17:
    rows = main.rows_for_layer(int(lid))
    assert rows == [2], f"member {lid} exposed on {rows}, expected [2]"
    image = main.cells[(2, int(lid))]
    assert image.strokes or image.fills, f"member {lid} has no artwork on row 2"
assert not any(row == 0 for (row, _lid) in main.cells), \
    "nothing from this run may land on frame 1"

# The contract underneath it, exercised where the two sources DISAGREE: the
# model's selection sits on row 0 while the caller asks for row 2. The column
# must be exposed on the row that was ASKED FOR, and the caller's selection
# must come back untouched.
main.set_current_frame(0)
main.set_current_layer(-1)
made = am._create_mapped_layer(main, 2, "anchored layer")
assert made >= 0
made_id = main.layer_id_at(made)
assert main.rows_for_layer(made_id) == [2], main.rows_for_layer(made_id)
assert main.current_frame() == 0, "the user's frame selection must be restored"
assert main.image_at(2, made, False) is not None
assert main.image_at(0, made, False) is None, \
    "the column must not also be exposed on frame 1"

# ... and the same run started with NO frame selected (current_frame -1, which
#     the model rewrites to 0) still puts the column and its artwork together.
scenes, fake = fresh_world()
main = scenes["main"]
child = scenes["child"]
main.set_current_frame(-1)
uid17b = am._create_unit()
install_guides(uid17b)
child.pattern = {child.layer_id_at(child.add_layer()):
                 [stroke([(-100.0, 50.0), (100.0, 50.0)])]}
assert am._perform_mapping()
for lid in am._UNIT_META[uid17b]["members"]:
    assert main.rows_for_layer(int(lid)) == [0]
    assert main.cells[(0, int(lid))].strokes
print("17) run output lands on the frame it was invoked on (row 2, not row 0)")

print("t_units: ALL OK")
