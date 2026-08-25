"""To Editable Layer: baking a mapping unit's visible output into one plain
vector layer + one tracked fill child layer, and dissolving the unit
(user request 2026-08-25, round 2 / G5).

The fake scene is t_units.py's, extended with the read/write surface the bake
uses: cell_strokes (real images, not the pattern stub), fill regions with
their full argument set, layer parenting and group dissolve.
"""
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am  # noqa: E402


# --- fakes ------------------------------------------------------------------

def points_to_dicts(points):
    """Model-side point format: [{'x': ..., 'y': ...}, ...]."""
    out = []
    for point in points or []:
        if isinstance(point, dict):
            out.append({"x": float(point["x"]), "y": float(point["y"])})
        else:
            out.append({"x": float(point[0]), "y": float(point[1])})
    return out


class FakeStrokeObject:
    def __init__(self, points, color, width, commands=None):
        self.points = points
        self.commands = commands
        self.color = color
        self.width = width
        self.property = ""
        self.pen_style = 1


class FakeImage:
    def __init__(self):
        self.strokes = []
        self.fills = []

    def stroke_count(self):
        return len(self.strokes)

    def fill_count(self):
        return len(self.fills)

    def add_stroke_object(self, obj):
        self.strokes.append(obj)

    def add_fill_region(self, path, color, property="", seed=None,
                        source_layer_index=-1, based_on_all_layers=False):
        self.fills.append({"commands": path, "color": color,
                           "property": property, "seed": seed,
                           "source_layer_index": source_layer_index,
                           "based_on_all_layers": based_on_all_layers})
        return len(self.fills) - 1

    def to_dict(self):
        strokes = []
        for obj in self.strokes:
            entry = {"property": obj.property, "width": obj.width,
                     "pen_style": obj.pen_style, "color": obj.color,
                     "raw_points": points_to_dicts(obj.points)}
            if obj.commands:
                entry["commands"] = obj.commands
            strokes.append(entry)
        fills = []
        for fill in self.fills:
            fills.append({"property": fill["property"], "color": fill["color"],
                          "commands": fill["commands"], "seed": fill["seed"],
                          "source_layer_index": fill["source_layer_index"],
                          "based_on_all_layers": fill["based_on_all_layers"],
                          "bounds": {"x": 0.0, "y": 0.0,
                                     "width": 0.0, "height": 0.0}})
        return {"empty": not (strokes or fills), "strokes": strokes,
                "fills": fills, "stroke_count": len(strokes),
                "fill_count": len(fills)}


class FakeVectorLogic:
    @staticmethod
    def make_stroke_object(points, color, width, _index, _a, _b):
        return FakeStrokeObject(points, color, width)

    @staticmethod
    def make_stroke_object_from_path(commands, flat, color, width, _index):
        return FakeStrokeObject(flat, color, width, commands)


class FakeScene:
    """The layer/group/content subset the unit machinery + bake touch."""

    def __init__(self, name):
        self.name = name
        self.columns = []          # [{"id","name","visible","type","parent_id"}]
        self.groups = {}           # gid -> {"name","tag","layer_ids","collapsed"}
        self.images = {}           # layer id -> FakeImage
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
                  "visible": True, "type": kind, "parent_id": 0}
        self._next_id += 1
        self.columns.append(column)
        index = len(self.columns) - 1
        self._layer = index
        return index

    def add_fill_layer(self):
        return self.add_layer("fill")

    def layer_count(self):
        return len(self.columns)

    def layer_name(self, index):
        return self.columns[index]["name"]

    def layer_type(self, index):
        return self.columns[index]["type"]

    def set_layer_name(self, index, name):
        self.columns[index]["name"] = self._unique(name)

    def set_layer_visible(self, index, visible):
        self.columns[index]["visible"] = bool(visible)

    def layer_visible(self, index):
        return self.columns[index]["visible"]

    def layer_parent_id(self, index):
        return self.columns[index].get("parent_id", 0)

    def set_layer_parent_id(self, index, parent_id):
        self.columns[index]["parent_id"] = int(parent_id)
        return True

    def child_layer_indices(self, index):
        parent_id = self.layer_id_at(index)
        return [i for i, column in enumerate(self.columns)
                if parent_id and column.get("parent_id") == parent_id]

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

    def dissolve_layer_group(self, gid):
        return self.groups.pop(gid, None) is not None

    def layer_ids_in_group(self, gid):
        return list(self.groups.get(gid, {}).get("layer_ids", []))

    def layer_tree(self):
        grouped = set()
        for group in self.groups.values():
            grouped.update(group["layer_ids"])
        tree = []
        for gid, group in self.groups.items():
            tree.append({"group": gid, "name": group["name"],
                         "tag": group["tag"],
                         "collapsed": group["collapsed"],
                         "children": [self.layer_index_for_id(lid)
                                      for lid in group["layer_ids"]]})
        for index, column in enumerate(self.columns):
            if column["id"] not in grouped:
                tree.append(index)
        return tree

    # --- content ---
    def image_at(self, _row, layer_index, _create=True, _asset_type="vector"):
        lid = self.layer_id_at(layer_index)
        return self.images.setdefault(lid, FakeImage())

    def get_structure(self):
        layers = []
        for index, column in enumerate(self.columns):
            layers.append({
                "index": index, "name": column["name"],
                "column_name": column["name"], "visible": column["visible"],
                "internal": False, "locked": False, "opacity": 1.0,
                "type": column["type"],
                "parent_layer_id": column.get("parent_id", 0),
                "cells": [{"layer_index": index, "frame_index": self._frame,
                           "asset_index": index, "frame_id": 1,
                           "empty": column["id"] not in self.pattern,
                           "stroke_count": len(self.pattern.get(column["id"], [])),
                           "fill_count": 0}],
            })
        return {"sceneName": f"{self.name}_paint_view", "layers": layers,
                "current_frame": self._frame, "current_layer": self._layer,
                "frame_count": 1, "layer_count": len(self.columns),
                "asset_count": 0,
                "frames": [{"index": 0, "num": 1, "name": "1"}],
                "assets": []}

    def cell_to_dict(self, layer_index, _frame, _to_poly=True, _step=4.0):
        """The pattern stub the mapping run scans (as in t_units)."""
        lid = self.layer_id_at(layer_index)
        return {"image": {"strokes": list(self.pattern.get(lid, [])),
                          "fills": []}}

    def cell_strokes(self, layer_index, _frame, _to_poly=False, _step=4.0):
        """What the BAKE reads: the layer's real committed image."""
        lid = self.layer_id_at(layer_index)
        image = self.images.get(lid)
        return image.to_dict() if image else FakeImage().to_dict()

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
TRIANGLE = [{"type": "move", "to": {"x": 0.0, "y": 0.0}},
            {"type": "line", "to": {"x": 10.0, "y": 0.0}},
            {"type": "line", "to": {"x": 10.0, "y": 10.0}},
            {"type": "line", "to": {"x": 0.0, "y": 0.0}}]
SQUARE = [{"type": "move", "to": {"x": -8.0, "y": -8.0}},
          {"type": "line", "to": {"x": 8.0, "y": -8.0}},
          {"type": "line", "to": {"x": 8.0, "y": 8.0}},
          {"type": "line", "to": {"x": -8.0, "y": 8.0}},
          {"type": "line", "to": {"x": -8.0, "y": -8.0}}]


def install_guides(uid):
    for view in ("main", "child"):
        assets = am._UNIT_ASSETS[view].setdefault(uid, {})
        assets[am.H_PROPERTY] = {"points": list(H), "width": 3.0}
        assets[am.V_PROPERTY] = {"points": list(V), "width": 3.0}


def run_a_unit():
    """A rendered unit: guides, a child pattern stroke, one mapping run."""
    scenes, fake = fresh_world()
    main, child = scenes["main"], scenes["child"]
    uid = am._create_unit()
    install_guides(uid)
    child.pattern = {child.layer_id_at(child.add_layer()):
                     [stroke([(-100.0, 50.0), (100.0, 50.0)]),
                      stroke([(-80.0, -40.0), (80.0, -40.0)])]}
    assert am._perform_mapping(), "the unit run must succeed"
    return scenes, fake, uid


# 1) MENU: the unit branch offers To Editable Layer next to Duplicate, before
#    the separator that closes the unit-scoped block.
scenes, fake, uid = run_a_unit()
main = scenes["main"]
gid = int(uid)
entries = am._layer_menu_items({"view": "main", "kind": "group",
                                "group": gid, "tag": am.UNIT_TAG})
names = [e.get("name") for e in entries]
assert am.TO_EDITABLE_ACTION in names
assert (names.index(am.DUPLICATE_UNIT_ACTION)
        < names.index(am.TO_EDITABLE_ACTION)
        < names.index("-")), names
title = next(e for e in entries if e.get("name") == am.TO_EDITABLE_ACTION)
assert title["title"] == "To Editable Layer"
# a member LAYER row offers it too (the shared resolver handles both rows)
member_index = main.layer_index_for_id(am._UNIT_META[uid]["primary"])
layer_entries = am._layer_menu_items({"view": "main", "kind": "layer",
                                      "layer": member_index,
                                      "owner_group": gid,
                                      "owner_tag": am.UNIT_TAG})
assert am.TO_EDITABLE_ACTION in [e.get("name") for e in layer_entries]
print("1) menu: To Editable Layer on unit group rows and member rows")

# 2) BAKE: two layers with the whole visible output, in PAINT order.
#    Setup: the run's front member plus two registered back members (this
#    pattern folds nowhere, so the depth stack is built here), a plain layer
#    above the unit (so placement has to move the pair to a non-zero slot), a
#    hand-dragged layer inside the group (which must survive), a hidden member
#    (excluded from the result) and injected fill regions.
def add_member(role, depth, points, color, pen_style, fill_commands):
    """Register one more output layer with the unit, as a run would."""
    index = main.add_layer()
    main.set_layer_name(index, f"mapped layer depth {depth}")
    obj = FakeStrokeObject(points, color, 2.0)
    obj.property = am.BACK_PROPERTY
    obj.pen_style = pen_style
    image = main.image_at(0, index, True)
    image.add_stroke_object(obj)
    image.add_fill_region(fill_commands, color, am.BACK_PROPERTY, None, -1, True)
    lid = main.layer_id_at(index)
    am._UNIT_META[uid]["members"][str(lid)] = {"role": role, "depth": depth}
    main.add_layers_to_group(gid, [index])
    return index


back_id = main.layer_id_at(
    add_member("back", 1, [(-20.0, 10.0), (20.0, 10.0)],
               {"r": 104, "g": 112, "b": 140, "a": 255}, 2, SQUARE))
deep_id = main.layer_id_at(
    add_member("back", 2, [(-30.0, 20.0), (30.0, 20.0)],
               {"r": 60, "g": 60, "b": 60, "a": 255}, 1, SQUARE))
top = main.add_layer()
main.set_layer_name(top, "background")
main.move_layer(top, 0)
adopted = main.add_layer()
main.set_layer_name(adopted, "hand dragged")
adopted_id = main.layer_id_at(adopted)
main.add_layers_to_group(gid, [adopted])

member_ids = [int(lid) for lid in am._UNIT_META[uid]["members"]]
member_indices = sorted(main.layer_index_for_id(lid) for lid in member_ids)
assert len(member_indices) == 3
hidden_id = deep_id                         # the deepest member goes dark
hidden_index = main.layer_index_for_id(hidden_id)
assert hidden_index == member_indices[-1], "deepest member = highest index"
assert main.layer_index_for_id(back_id) == member_indices[-2]
main.set_layer_visible(hidden_index, False)
main.image_at(0, member_indices[0], True).add_fill_region(
    TRIANGLE, {"r": 90, "g": 80, "b": 70, "a": 255}, am.MAPPED_PROPERTY,
    None, -1, True)

# what a faithful flattening must produce, computed BEFORE the surgery:
# deepest member first, front member last - the renderer's own column loop.
expected_strokes = []
expected_fills = []
for index in sorted(member_indices, reverse=True):
    if not main.layer_visible(index):
        continue
    image = main.cell_strokes(index, 0, False, 4.0)
    expected_strokes.extend(image["strokes"])
    expected_fills.extend(image["fills"])
assert expected_strokes and expected_fills
hidden_points = [s["raw_points"] for s
                 in main.cell_strokes(hidden_index, 0, False, 4.0)["strokes"]]
assert hidden_points, "the hidden member must actually hold artwork"
unit_top = member_indices[0]
fake.ui.commits.clear()

# the live re-render must not fire while the layers are in flux: assert the
# run guard is up at the moment content is written.
guard_seen = []
real_bake_stroke = am._bake_stroke


def probe_bake_stroke(animean, image, item):
    guard_seen.append(am._RUN_GUARD["depth"])
    return real_bake_stroke(animean, image, item)


am._bake_stroke = probe_bake_stroke
try:
    am._layer_menu_action({"action": am.TO_EDITABLE_ACTION, "view": "main",
                           "group": gid, "group_name": "", "layer": -1,
                           "members": list(member_indices)})
finally:
    am._bake_stroke = real_bake_stroke
assert guard_seen and min(guard_seen) >= 1, guard_seen
assert am._RUN_GUARD["depth"] == 0

lines_index = next(i for i in range(main.layer_count())
                   if main.layer_name(i).endswith(" lines"))
fill_index = next(i for i in range(main.layer_count())
                  if main.layer_name(i).endswith(" fill"))
# the group row's name (via layer_tree, since the menu message carried none)
assert main.layer_name(lines_index) == f"{am.UNIT_LAYER_TITLE} lines"
assert main.layer_name(fill_index) == f"{am.UNIT_LAYER_TITLE} fill"
assert main.layer_type(lines_index) == "vector"
assert main.layer_type(fill_index) == "fill"

baked_lines = main.cell_strokes(lines_index, 0, False, 4.0)
baked_fills = main.cell_strokes(fill_index, 0, False, 4.0)
assert baked_lines["stroke_count"] == len(expected_strokes)
assert baked_lines["fill_count"] == 0          # fills live in the child layer
assert baked_fills["fill_count"] == len(expected_fills)
assert baked_fills["stroke_count"] == 0
assert ([s["raw_points"] for s in baked_lines["strokes"]]
        == [s["raw_points"] for s in expected_strokes]), "paint order lost"
assert all(s["property"] == "" for s in baked_lines["strokes"]), \
    "unit-internal properties must not survive the bake"
assert all(s["color"] == e["color"] and s["width"] == e["width"]
           and s["pen_style"] == e["pen_style"]
           for s, e in zip(baked_lines["strokes"], expected_strokes))
for points in hidden_points:
    assert points not in [s["raw_points"] for s in baked_lines["strokes"]], \
        "a hidden member is not part of the visible result"
assert ([f["commands"] for f in baked_fills["fills"]]
        == [f["commands"] for f in expected_fills]), "fill paint order lost"
assert all(f["property"] == "" and f["based_on_all_layers"]
           and f["source_layer_index"] == -1
           for f in baked_fills["fills"])
for baked_fill in baked_fills["fills"]:
    # Baked fills are TRACKED (the layer is a child), so each one must carry
    # a seed that actually resolves inside its own region.
    seed = baked_fill["seed"]
    ring = am._path_commands_to_polygons(baked_fill["commands"])[0]
    assert seed and am._point_in_ring((float(seed[0]), float(seed[1])), ring)
print("2) bake: lines + fills flattened in paint order, hidden member skipped")

# 3) STRUCTURE: the fill layer is a tracked child of the line layer and the
#    pair sits where the unit row was; every member layer is gone, the
#    hand-dragged layer and the layer above the unit are not.
assert main.layer_parent_id(fill_index) == main.layer_id_at(lines_index)
assert main.child_layer_indices(lines_index) == [fill_index]
assert fill_index == lines_index + 1, "the fill row nests under the line row"
assert lines_index == unit_top, "the pair must land where the unit row was"
assert main.layer_name(0) == "background"
for lid in member_ids:
    assert main.layer_index_for_id(lid) == -1, "member layers must be gone"
assert main.layer_index_for_id(hidden_id) == -1
assert main.layer_index_for_id(adopted_id) >= 0, \
    "a layer the user dragged in by hand survives the dissolve"
assert gid not in main.groups, "the unit group is dissolved"
print("3) structure: child fill layer, unit slot kept, members retired")

# 4) STATE: the unit's meta and BOTH asset stores are purged, focus lands on
#    the baked line layer, and one history entry per scene carries the bake.
assert uid not in am._UNIT_META
assert uid not in am._UNIT_ASSETS["main"] and uid not in am._UNIT_ASSETS["child"]
assert am._ACTIVE_UNIT["id"] is None
assert am._SETTINGS_TARGET["unit"] != uid
assert main.current_layer() == lines_index
assert fake.ui.commits.count(("To Editable Layer", "main")) == 1
assert fake.ui.commits.count(("To Editable Layer", "child")) == 1
assert all(label == "To Editable Layer" for label, _ in fake.ui.commits)
# the purge is persisted, not just in-memory
am._UNIT_META.clear()
am._UNIT_ASSETS["main"].clear()
am._load_units("main", main)
assert uid not in am._UNIT_META and not am._UNIT_META
print("4) state: unit meta/assets purged on both stores, one commit per scene")

# 5) EMPTY UNIT: nothing to bake -> nothing created, unit untouched.
scenes, fake = fresh_world()
main = scenes["main"]
empty_uid = am._create_unit()
install_guides(empty_uid)
before = main.layer_count()
fake.ui.commits.clear()
assert am._bake_unit_to_layers(main, empty_uid) is None
assert main.layer_count() == before
assert empty_uid in am._UNIT_META
assert int(empty_uid) in main.groups
assert not fake.ui.commits
assert am._RUN_GUARD["depth"] == 0
print("5) empty unit: refused, nothing created, the unit survives")

# 6) NOT A UNIT: a plain group id is refused (the resolver's tag gate).
scenes, fake = fresh_world()
main = scenes["main"]
plain = main.add_layer()
plain_gid = main.create_layer_group("just a group", [plain], [], False)
am._layer_menu_action({"action": am.TO_EDITABLE_ACTION, "view": "main",
                       "group": plain_gid, "group_name": "just a group",
                       "layer": -1, "members": [plain]})
assert main.layer_count() == 1 and plain_gid in main.groups
assert not fake.ui.commits
print("6) non-unit group: the action is a no-op")

print("t_unit_bake: ALL OK")
