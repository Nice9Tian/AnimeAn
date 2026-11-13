"""Real-time field preview during guide / additional-line / contour drag (issue #46).

Tests:
1. Differential accuracy vs cold-start mapper(p) on Rigs A, B, C (flow field <= 0.5 px,
   front/back sign agreement > 1 cell from det=0, severing seam <= 1 cell from third_of ok).
2. Guard conditions: zero set_field calls when field_display_supported is False,
   Live Re-render (auto_render) is off, or no unit is focused.
3. Cancel behavior: set_field_view receives None and both boards' assets match press-time
   state value-for-value across all 3 line types (axis, additional line, proxy contour).
4. Release ordering: set_field_view(None) happens strictly after _maybe_auto_run (and
   also clears on unmoved release).
5. Zero history_commit and zero scene model writes during drag moves.
6. Throttling gate: moves closer than 8 ms to the last frame schedule call_later, and
   firing the callback flushes the latest position.
7. Single-step frame-budget invariants: dragging an additional line on Rigs B and C, or the
   folded axis on Rig D, stays on warm-capped PCG, cached child/Third state, pre-rasterized
   area LUTs, and inherited region depths without cold solves or exact depth recomputes.
"""
import copy
import math
import os
import statistics
import sys
import time
import types
import unittest
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
for _p in (os.path.join(ROOT, "pyfile"), os.path.join(ROOT, "tests")):
    if _p not in sys.path:
        sys.path.insert(0, _p)

_init_mod = sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
_init_mod.ui = types.SimpleNamespace(
    set_overlay=lambda *a, **k: None,
    history_commit=lambda *a, **k: None,
    refresh=lambda *a, **k: None,
    refresh_tool_options=lambda *a, **k: None,
    set_draw_color=lambda *a, **k: None,
    set_hook_subscriptions=lambda *a, **k: None,
    set_hook_events=lambda *a, **k: None,
)
import auto_mapping as am  # noqa: E402
import gridsolve  # noqa: E402
import line_tools as lt  # noqa: E402
import mapping_preview as mp  # noqa: E402


# ---------------------------------------------------------------------------
# Headless Fake Scene & UI
# ---------------------------------------------------------------------------

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
    def __init__(self, name):
        self.name = name
        self.columns = []
        self.groups = {}
        self.cells = {}
        self.pattern = {}
        self.fills = {}
        self._script = ""
        self._next_id = 1
        self._next_gid = 1
        self._frame = 0
        self._layer = -1
        self._asset = -1
        self.write_count = 0

    def current_frame(self):
        return self._frame

    def current_layer(self):
        return self._layer

    def current_asset(self):
        return self._asset

    def set_current_frame(self, value):
        self._frame = value

    def set_current_layer(self, value):
        self.write_count += 1
        self._layer = value

    def set_current_asset(self, value):
        self._asset = value

    def _unique(self, base):
        names = {c["name"] for c in self.columns}
        if base not in names:
            return base
        n = 1
        while f"{base}{n}" in names:
            n += 1
        return f"{base}{n}"

    def add_layer(self, kind="vector"):
        self.write_count += 1
        column = {"id": self._next_id, "name": f"layer {self._next_id}",
                  "visible": True, "type": kind}
        self._next_id += 1
        self.columns.append(column)
        index = len(self.columns) - 1
        self._layer = index
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
        self.write_count += 1
        self.columns[index]["name"] = self._unique(name)

    def set_layer_visible(self, index, visible):
        self.write_count += 1
        self.columns[index]["visible"] = bool(visible)

    def layer_visible(self, index):
        return self.columns[index]["visible"]

    def move_layer(self, src, dst):
        self.write_count += 1
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
        self.write_count += 1
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

    def create_layer_group(self, name, layers=(), groups=(), collapsed=False):
        self.write_count += 1
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

    def image_at(self, row, layer_index, create=True, _asset_type="vector"):
        lid = self.layer_id_at(layer_index)
        key = (row, lid)
        if key not in self.cells:
            if not create:
                return None
            self.write_count += 1
            self.cells[key] = FakeImage()
        return self.cells[key]

    def cell_asset_index(self, row, layer_index):
        lid = self.layer_id_at(layer_index)
        return 0 if (row, lid) in self.cells else -1

    def rows_for_layer(self, lid):
        return sorted(row for (row, other) in self.cells if other == lid)

    def frame_count(self):
        return max(4, max((row for row, _ in self.cells), default=0) + 1)

    def get_structure(self):
        layers = []
        for index, column in enumerate(self.columns):
            cells = []
            for row in self.rows_for_layer(column["id"]):
                strokes = self.pattern.get(column["id"], [])
                fills = self.fills.get(column["id"], [])
                cells.append({"layer_index": index, "frame_index": row,
                              "asset_index": index, "frame_id": 1,
                              "empty": not strokes and not fills,
                              "stroke_count": len(strokes),
                              "fill_count": len(fills)})
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
                          "fills": list(self.fills.get(lid, []))}}

    def canvas_size(self):
        return (800, 600)

    def script_data(self):
        return self._script

    def set_script_data(self, value):
        self.write_count += 1
        self._script = value


class FakeUi:
    def __init__(self, scenes):
        self.scenes = scenes
        self.supported = True
        self.commits = []
        self.set_field_calls = []
        self.set_field_view_calls = []
        self.call_later_calls = []
        self.events = []

    def field_display_supported(self, view="main"):
        return bool(self.supported)

    def set_field(self, key, width, height, data):
        self.set_field_calls.append((key, int(width), int(height), bytes(data)))
        self.events.append(("set_field", key, int(width), int(height)))

    def set_field_view(self, view, spec):
        spec_copy = copy.deepcopy(spec)
        self.set_field_view_calls.append((view, spec_copy))
        self.events.append(("set_field_view", view, spec_copy))

    def call_later(self, delay_ms, fn):
        self.call_later_calls.append((int(delay_ms), fn))
        self.events.append(("call_later", int(delay_ms)))

    def refresh(self):
        pass

    def refresh_tool_options(self):
        pass

    def set_current(self, frame=None, layer=None, asset=None):
        if layer is not None:
            self.scenes["main"].set_current_layer(layer)

    def history_commit(self, label, view=""):
        self.commits.append((label, view))
        self.events.append(("history_commit", label, view))

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


# ---------------------------------------------------------------------------
# Rig Helpers (reusing Rigs A, B, C from tests/bench_drag_cost.py)
# ---------------------------------------------------------------------------

def _stroke_dict(points, width=3.0, prop=""):
    return {
        "property": prop,
        "width": width,
        "color": {"r": 20, "g": 30, "b": 40, "a": 255},
        "polylines": [[{"x": float(x), "y": float(y)} for x, y in points]],
    }


def _default_pattern_strokes(cx=0.0, cy=0.0):
    """Pattern strokes covering [cx-240, cx+240] x [cy-160, cy+160] on the child board."""
    strokes = []
    # Outer frame & interior cross-hatching so bounding box is well-defined
    strokes.append(_stroke_dict([(cx - 232.0, cy - 152.0), (cx + 232.0, cy - 152.0),
                                 (cx + 232.0, cy + 152.0), (cx - 232.0, cy + 152.0),
                                 (cx - 232.0, cy - 152.0)], width=3.0))
    for y in (-100.0, -40.0, 20.0, 80.0):
        pts = [(cx - 200.0 + 40.0 * k, cy + y + 10.0 * math.sin(k * 0.6)) for k in range(11)]
        strokes.append(_stroke_dict(pts, width=2.5))
    return strokes


def _build_rig_assets(rig_name):
    """Build (child_assets, main_assets) matching tests/bench_drag_cost.py Rigs A, B, C, D."""
    if rig_name in ("D", "D3"):
        child_h = [[240.0, 360.0], [1040.0, 360.0]]
        child_v = [[640.0, 90.0], [640.0, 630.0]]
        main_v = [[640.0, 90.0], [640.0, 630.0]]
        main_h = [
            [
                240.0 + 800.0 * (k / 60.0) + 170.0 * math.sin((k / 60.0) * math.pi * 3.0 + 0.6),
                360.0 + 90.0 * math.sin((k / 60.0) * math.pi * 3.0),
            ]
            for k in range(61)
        ]
        child_lines = [{
            "id": 1,
            "width": 2.5,
            "points": [[330.0 + 16.0 * k, 230.0] for k in range(41)],
        }]
        main_lines = [{
            "id": 1,
            "width": 2.5,
            "points": [
                [330.0 + 16.0 * k, 210.0 + 30.0 * math.sin(k / 40.0 * math.pi)]
                for k in range(41)
            ],
        }]
        return (
            {
                am.H_PROPERTY: {"points": [list(p) for p in child_h], "width": 3.0},
                am.V_PROPERTY: {"points": [list(p) for p in child_v], "width": 3.0},
                am.ADDITIONAL_PROPERTY: {"lines": child_lines},
            },
            {
                am.H_PROPERTY: {"points": [list(p) for p in main_h], "width": 3.0},
                am.V_PROPERTY: {"points": [list(p) for p in main_v], "width": 3.0},
                am.ADDITIONAL_PROPERTY: {"lines": main_lines},
            },
        )

    if rig_name == "A":
        child_h = [(-300.0, 0.0), (300.0, 0.0)]
        child_v = [(0.0, -200.0), (0.0, 200.0)]
        main_h = [(-300.0, 0.0), (300.0, 0.0)]
        main_v = [(0.0, -200.0), (0.0, 200.0)]
        return (
            {
                am.H_PROPERTY: {"points": [list(p) for p in child_h], "width": 3.0},
                am.V_PROPERTY: {"points": [list(p) for p in child_v], "width": 3.0},
            },
            {
                am.H_PROPERTY: {"points": [list(p) for p in main_h], "width": 3.0},
                am.V_PROPERTY: {"points": [list(p) for p in main_v], "width": 3.0},
            },
        )

    n_seg = 24
    if rig_name == "B":
        child_h = [
            (-300.0 + 600.0 * i / n_seg,
             22.0 * math.sin(math.pi * (-300.0 + 600.0 * i / n_seg) / 300.0))
            for i in range(n_seg + 1)
        ]
    else:
        child_h = [
            (-300.0 + 500.0 * i / 20.0,
             22.0 * math.sin(math.pi * (-300.0 + 500.0 * i / 20.0) / 300.0))
            for i in range(21)
        ]
        bx, by = child_h[-1]
        for k in range(1, 7):
            ang = (k / 6.0) * (math.pi * 0.68)
            child_h.append((bx + 45.0 * math.sin(ang),
                            by + 45.0 * (1.0 - math.cos(ang))))

    child_v = [
        (18.0 * math.sin(math.pi * (-200.0 + 400.0 * i / n_seg) / 200.0),
         -200.0 + 400.0 * i / n_seg)
        for i in range(n_seg + 1)
    ]
    main_h = [
        (-310.0 + 620.0 * i / n_seg,
         32.0 * math.sin(0.8 * math.pi * (-310.0 + 620.0 * i / n_seg) / 310.0)
         + 10.0 * ((-310.0 + 620.0 * i / n_seg) / 310.0) ** 2)
        for i in range(n_seg + 1)
    ]
    main_v = [
        (-24.0 * math.sin(0.9 * math.pi * (-210.0 + 420.0 * i / n_seg) / 210.0)
         + 8.0 * ((-210.0 + 420.0 * i / n_seg) / 210.0) ** 2,
         -210.0 + 420.0 * i / n_seg)
        for i in range(n_seg + 1)
    ]

    n_line = 20
    child_lines = [
        {
            "id": 1,
            "width": 2.5,
            "points": [
                [-160.0 + 320.0 * i / n_line,
                 65.0 + 6.0 * math.sin(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        },
        {
            "id": 2,
            "width": 2.5,
            "points": [
                [-150.0 + 300.0 * i / n_line,
                 -70.0 - 5.0 * math.cos(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        },
        {
            "id": 3,
            "width": 2.5,
            "points": [
                [85.0 + 6.0 * math.sin(math.pi * i / n_line),
                 -120.0 + 240.0 * i / n_line]
                for i in range(n_line + 1)
            ],
        },
    ]
    main_lines = [
        {
            "id": 1,
            "width": 2.5,
            "points": [
                [-160.0 + 320.0 * i / n_line,
                 72.0 + 28.0 * (i / n_line - 0.5) + 10.0 * math.sin(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        },
        {
            "id": 2,
            "width": 2.5,
            "points": [
                [-150.0 + 300.0 * i / n_line,
                 -78.0 - 24.0 * (i / n_line - 0.5) - 8.0 * math.cos(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        },
        {
            "id": 3,
            "width": 2.5,
            "points": [
                [92.0 - 22.0 * (i / n_line - 0.5) + 9.0 * math.sin(math.pi * i / n_line),
                 -120.0 + 240.0 * i / n_line]
                for i in range(n_line + 1)
            ],
        },
    ]

    if rig_name == "C":
        child_lines.append({
            "id": 4,
            "width": 2.5,
            am.GROUND_FLAG: True,
            "points": [
                [-100.0 + 200.0 * i / n_line,
                 -125.0 + 3.0 * math.sin(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        })
        main_lines.append({
            "id": 4,
            "width": 2.5,
            am.GROUND_FLAG: True,
            "points": [
                [-100.0 + 200.0 * i / n_line,
                 -129.0 + 4.0 * math.sin(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        })

    return (
        {
            am.H_PROPERTY: {"points": [list(p) for p in child_h], "width": 3.0},
            am.V_PROPERTY: {"points": [list(p) for p in child_v], "width": 3.0},
            am.ADDITIONAL_PROPERTY: {"lines": child_lines},
        },
        {
            am.H_PROPERTY: {"points": [list(p) for p in main_h], "width": 3.0},
            am.V_PROPERTY: {"points": [list(p) for p in main_v], "width": 3.0},
            am.ADDITIONAL_PROPERTY: {"lines": main_lines},
        },
    )


def fresh_rig_world(rig_name="B", pattern_strokes=None):
    scenes = {"main": FakeScene("main"), "child": FakeScene("child")}
    fake = types.SimpleNamespace(ui=FakeUi(scenes), vectorlogic=FakeVectorLogic())
    sys.modules["animean_python"].ui = fake.ui
    sys.modules["animean_python"].vectorlogic = fake.vectorlogic
    am._scene_model = lambda view: scenes[view]
    am._animean = lambda: fake
    lt._animean = lambda: fake
    mp._clock = time.perf_counter
    mp.cancel(restore_assets=False)

    am._MAPPING_ASSETS.clear()
    am._UNIT_ASSETS.clear()
    am._UNIT_ASSETS.update(main={}, child={})
    am._UNIT_META.clear()
    am._GUIDE_DRAG.clear()
    lt._DRAG.clear()
    am._CURVE_MODE["value"] = "polyline"
    am._ACTIVE_UNIT["id"] = None
    am._SETTINGS_TARGET["unit"] = None
    am._RUN_GUARD["depth"] = 0
    am._FOLD.update(split=True, seal=True)
    am._invalidate_grid_cache()
    am._MAPPER_CACHE["key"] = None
    am._MAPPER_CACHE["mapper"] = None
    am._PROXY_PAIRING["key"] = None
    am._PROXY_PAIRING["value"] = None

    # Populate child board pattern layer
    child = scenes["child"]
    c_layer = child.add_layer("vector")
    c_lid = child.layer_id_at(c_layer)
    if pattern_strokes is not None:
        child.pattern[c_lid] = list(pattern_strokes)
    elif rig_name == "D":
        child.pattern[c_lid] = _default_pattern_strokes(640.0, 360.0)
    elif rig_name == "D3":
        child.pattern[c_lid] = [
            _stroke_dict([(268.0, 208.0), (872.0, 208.0),
                          (872.0, 512.0), (268.0, 512.0),
                          (268.0, 208.0)], width=3.0),
        ]
    else:
        child.pattern[c_lid] = _default_pattern_strokes()

    # Create and focus a mapping unit on main board
    uid = am._create_unit()
    ch_assets, mn_assets = _build_rig_assets(rig_name)
    am._UNIT_ASSETS["child"][uid] = ch_assets
    am._UNIT_ASSETS["main"][uid] = mn_assets

    fake.ui.commits.clear()
    fake.ui.set_field_calls.clear()
    fake.ui.set_field_view_calls.clear()
    fake.ui.call_later_calls.clear()
    fake.ui.events.clear()
    return scenes, fake, uid


def _decode_last_field(ui):
    key, W, H, raw = ui.set_field_calls[-1]
    spec = next(s for _v, s in reversed(ui.set_field_view_calls) if s is not None)
    rect = tuple(float(v) for v in spec["rect"])
    arr = np.frombuffer(raw, dtype="<f4").reshape(H, W, 4)
    return key, W, H, rect, arr


def _build_cold_mapper_from_current_assets():
    ch = copy.deepcopy(am._assets_for("child"))
    mn = copy.deepcopy(am._assets_for("main"))
    for assets in (ch, mn):
        for line in (assets.get(am.ADDITIONAL_PROPERTY) or {}).get("lines") or []:
            line.pop("third", None)
    pairs = []
    ch_lines = {line["id"]: line for line in (ch.get(am.ADDITIONAL_PROPERTY) or {}).get("lines") or []}
    mn_lines = {line["id"]: line for line in (mn.get(am.ADDITIONAL_PROPERTY) or {}).get("lines") or []}
    grounds = []
    for k in sorted(ch_lines):
        if k not in mn_lines:
            continue
        cl, ml = ch_lines[k], mn_lines[k]
        if cl.get(am.GROUND_FLAG) or ml.get(am.GROUND_FLAG):
            grounds.append(({"points": cl["points"]}, {"points": ml["points"]}))
        else:
            pairs.append((cl, ml))
    mapper, _ws = am.build_mapper(
        ch[am.H_PROPERTY], ch[am.V_PROPERTY],
        mn[am.H_PROPERTY], mn[am.V_PROPERTY],
        additional_pairs=pairs or None,
        ground_pairs=grounds or None,
        warm=None,
    )
    return mapper


# ---------------------------------------------------------------------------
# Test Suite
# ---------------------------------------------------------------------------

def _grid_gap(items, other):
    """How far the worst point of `items` lies from any point of `other`."""
    a = np.array([p for item in items for p in item["points"]], dtype=float)
    b = np.array([p for item in other for p in item["points"]], dtype=float)
    return max(float(np.min(np.hypot(*(b - p).T))) for p in a)


def moves_later(fake):
    """The gate's deferred moves, without the settle passes."""
    return [call for call in fake.ui.call_later_calls if call[0] != mp.SETTLE_DELAY_MS]


class TestMappingPreview(unittest.TestCase):

    def tearDown(self):
        mp._clock = time.perf_counter
        mp.cancel(restore_assets=False)

    def test_1_differential_accuracy_rigs_A_B_C(self):
        """Flow field <= 0.5 px, front/back sign agreement > 1 cell from det=0, severing <= 1 cell."""
        for rig_name in ("A", "B", "C"):
            scenes, fake, _uid = fresh_rig_world(rig_name)
            clock_val = [100.0]
            mp._clock = lambda: clock_val[0]

            handle = am.H_PROPERTY if rig_name == "A" else f"{am.ADDITIONAL_PROPERTY}:1"
            origin = (0.0, 0.0)
            am._guide_drag_event({
                "view": "main", "handle": handle, "phase": "press",
                "position": {"x": origin[0], "y": origin[1]},
            })
            for step in range(1, 6):
                clock_val[0] += 0.25  # bypass throttle and allow depth refresh
                am._guide_drag_event({
                    "view": "main", "handle": handle, "phase": "move",
                    "position": {"x": step * 1.4, "y": step * 0.9},
                })

            self.assertGreaterEqual(len(fake.ui.set_field_calls), 5)
            _key, W, H, rect, arr = _decode_last_field(fake.ui)
            x0, y0, rw, rh = rect
            du = rw / (W - 1)
            dv = rh / (H - 1)

            cold_mapper = _build_cold_mapper_from_current_assets()
            self.assertIsNotNone(cold_mapper)

            max_flow_err = 0.0
            valid_count = 0
            for r in range(H):
                v = y0 + r * dv
                for c in range(W):
                    u = x0 + c * du
                    dx, dy, tag, zero = arr[r, c]
                    self.assertEqual(float(zero), 0.0)
                    self.assertFalse(math.isnan(float(dx)) or math.isnan(float(dy)))
                    if tag > 0.0:
                        cold_pt = cold_mapper((u, v))
                        self.assertIsNotNone(cold_pt)
                        pred_x = u + float(dx)
                        pred_y = v + float(dy)
                        err = math.hypot(pred_x - cold_pt[0], pred_y - cold_pt[1])
                        if err > max_flow_err:
                            max_flow_err = err
                        valid_count += 1

            self.assertGreater(valid_count, 500)
            self.assertLessEqual(
                max_flow_err, 0.5,
                f"Rig {rig_name}: max flow error {max_flow_err:.4f} px exceeds 0.5 px",
            )
            mp.end()

    def test_2_front_back_and_severing_differential(self):
        """Folded Z-rig front/back sign and eccentric rig severing boundary vs cold mapper."""
        # Part A: Folded Z-rig (with fold_reference +1 and -1)
        z_strokes = [
            _stroke_dict([(-232.0, -72.0), (232.0, -72.0),
                          (232.0, 72.0), (-232.0, 72.0),
                          (-232.0, -72.0)], width=3.0),
        ]
        for mirror_main in (False, True):
            scenes, fake, uid = fresh_rig_world("A", pattern_strokes=z_strokes)
            clock_val = [10.0]
            mp._clock = lambda: time.perf_counter() + clock_val[0]

            n_seg = 40
            ch_h = [(-300.0 + 600.0 * i / n_seg, 0.0) for i in range(n_seg + 1)]
            ch_v = [(0.0, -200.0 + 400.0 * i / n_seg) for i in range(n_seg + 1)]
            s_sign = -1.0 if mirror_main else 1.0
            mn_h = []
            for i in range(n_seg + 1):
                s = -1.0 + 2.0 * i / n_seg
                x = s_sign * (220.0 * s - 160.0 * math.sin(math.pi * s))
                y = 20.0 * math.sin(0.5 * math.pi * s)
                mn_h.append([x, y])
            mn_v = [[0.0, -200.0 + 400.0 * i / n_seg] for i in range(n_seg + 1)]

            am._UNIT_ASSETS["child"][uid] = {
                am.H_PROPERTY: {"points": [list(p) for p in ch_h], "width": 3.0},
                am.V_PROPERTY: {"points": [list(p) for p in ch_v], "width": 3.0},
            }
            am._UNIT_ASSETS["main"][uid] = {
                am.H_PROPERTY: {"points": mn_h, "width": 3.0},
                am.V_PROPERTY: {"points": mn_v, "width": 3.0},
            }

            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": "press",
                "position": {"x": 0.0, "y": 0.0},
            })
            clock_val[0] += 0.25
            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": "move",
                "position": {"x": 2.0, "y": 1.0},
            })
            if not mirror_main:
                z_st = mp.last_timings()
                print(
                    f"[timing] Folded Z-rig: total={z_st.get('total', 0.0)*1000.0:.2f}ms, "
                    f"depth_table={z_st.get('depth_table', 0.0)*1000.0:.2f}ms"
                )

            _key, W, H, rect, arr = _decode_last_field(fake.ui)
            x0, y0, rw, rh = rect
            du, dv = rw / (W - 1), rh / (H - 1)
            cold_mapper = _build_cold_mapper_from_current_assets()

            cold_signs = np.zeros((H, W), dtype=np.int32)
            for r in range(H):
                v = y0 + r * dv
                for c in range(W):
                    u = x0 + c * du
                    cold_signs[r, c] = am._fold_sign(cold_mapper, (u, v))

            mismatches = 0
            back_nodes = 0
            for r in range(1, H - 1):
                for c in range(1, W - 1):
                    tag = float(arr[r, c, 2])
                    if tag <= 0.0:
                        continue
                    nb = cold_signs[r - 1:r + 2, c - 1:c + 2]
                    if np.all(nb == nb[1, 1]):
                        pred_depth = int(round(tag)) - 1
                        pred_sign = 1 if (pred_depth % 2 == 0) else -1
                        if pred_sign < 0:
                            back_nodes += 1
                        if pred_sign != int(nb[1, 1]):
                            mismatches += 1
            self.assertGreater(back_nodes, 20)
            self.assertEqual(mismatches, 0, f"mirror={mirror_main}: {mismatches} fold sign mismatches")
            mp.end()

        # Part B: Severing seam (child axis with hooked end so third_of ok is False on part of grid)
        scenes, fake, uid = fresh_rig_world("C")
        clock_val = [20.0]
        mp._clock = lambda: clock_val[0]
        # Make child H curl sharply so the right side of the pattern is severed (third_of ok == False)
        ch_h = [(-300.0 + 380.0 * i / 20.0, 0.0) for i in range(21)]
        bx, by = ch_h[-1]
        for k in range(1, 9):
            ang = (k / 8.0) * (math.pi * 0.85)
            ch_h.append([bx + 35.0 * math.sin(ang), by + 35.0 * (1.0 - math.cos(ang))])
        am._UNIT_ASSETS["child"][uid][am.H_PROPERTY]["points"] = [list(p) for p in ch_h]

        am._guide_drag_event({
            "view": "main", "handle": f"{am.ADDITIONAL_PROPERTY}:1", "phase": "press",
            "position": {"x": 0.0, "y": 0.0},
        })
        clock_val[0] += 0.25
        am._guide_drag_event({
            "view": "main", "handle": f"{am.ADDITIONAL_PROPERTY}:1", "phase": "move",
            "position": {"x": 1.5, "y": -1.0},
        })

        _key, W, H, rect, arr = _decode_last_field(fake.ui)
        x0, y0, rw, rh = rect
        du, dv = rw / (W - 1), rh / (H - 1)
        cold_mapper = _build_cold_mapper_from_current_assets()

        cold_ok = np.zeros((H, W), dtype=bool)
        for r in range(H):
            v = y0 + r * dv
            for c in range(W):
                u = x0 + c * du
                _lh, _lv, ok = cold_mapper.third_of((u, v))
                cold_ok[r, c] = bool(ok)

        pred_ok = arr[:, :, 2] > 0.0
        self.assertTrue(np.any(~cold_ok), "Expected severed region in cold_ok")
        self.assertTrue(np.any(cold_ok), "Expected valid region in cold_ok")

        # Every node where pred_ok != cold_ok must lie within 1 grid cell of the cold_ok seam
        seam_violations = 0
        for r in range(H):
            r0, r1 = max(0, r - 1), min(H, r + 2)
            for c in range(W):
                if bool(pred_ok[r, c]) != bool(cold_ok[r, c]):
                    c0, c1 = max(0, c - 1), min(W, c + 2)
                    patch = cold_ok[r0:r1, c0:c1]
                    if np.all(patch == cold_ok[r, c]):
                        seam_violations += 1
        self.assertEqual(seam_violations, 0, f"Severing seam differed by >1 cell at {seam_violations} nodes")
        mp.end()

    def test_3_disabled_conditions_produce_zero_set_field(self):
        """No set_field when field_display_supported is False, auto_render is False, or no unit focus."""
        # Case 1: field_display_supported is False
        _scenes, fake, _uid = fresh_rig_world("A")
        fake.ui.supported = False
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "press",
                              "position": {"x": 0.0, "y": 0.0}})
        self.assertIsNone(mp._SESSION.get("snapshot"))
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "move",
                              "position": {"x": 5.0, "y": 3.0}})
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "cancel",
                              "position": {"x": 5.0, "y": 3.0}})
        self.assertEqual(len(fake.ui.set_field_calls), 0)
        self.assertEqual(len(fake.ui.set_field_view_calls), 0)

        # Case 2: Unit Live Re-render (auto_render) is False
        _scenes, fake, uid = fresh_rig_world("A")
        am._UNIT_META[uid].setdefault("settings", {})["auto_render"] = False
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "press",
                              "position": {"x": 0.0, "y": 0.0}})
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "move",
                              "position": {"x": 5.0, "y": 3.0}})
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "release",
                              "position": {"x": 5.0, "y": 3.0}})
        self.assertEqual(len(fake.ui.set_field_calls), 0)

        # Case 3: No unit focused (_ACTIVE_UNIT["id"] is None)
        _scenes, fake, _uid = fresh_rig_world("A")
        am._ACTIVE_UNIT["id"] = None
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "press",
                              "position": {"x": 0.0, "y": 0.0}})
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "move",
                              "position": {"x": 5.0, "y": 3.0}})
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "release",
                              "position": {"x": 5.0, "y": 3.0}})
        self.assertEqual(len(fake.ui.set_field_calls), 0)

    def test_4_cancel_restores_assets_and_clears_field_view_all_line_types(self):
        """Cancel sends set_field_view('main', None) and restores assets identically for all 3 line types."""
        # 1) Axis drag via _guide_drag_event
        _scenes, fake, _uid = fresh_rig_world("B")
        am._assets_for("main")[am.H_PROPERTY]["commands"] = [{"cmd": "M", "x": -310.0, "y": 0.0}]
        before_child = copy.deepcopy(am._assets_for("child"))
        before_main = copy.deepcopy(am._assets_for("main"))
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "press",
                              "position": {"x": 0.0, "y": 0.0}})
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "move",
                              "position": {"x": 12.0, "y": -8.0}})
        self.assertGreater(len(fake.ui.set_field_calls), 0)
        am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": "cancel",
                              "position": {"x": 12.0, "y": -8.0}})
        self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
        self.assertEqual(am._assets_for("child"), before_child)
        self.assertEqual(am._assets_for("main"), before_main)

        # 2) Additional line drag via _guide_drag_event
        _scenes, fake, _uid = fresh_rig_world("B")
        am._assets_for("main")[am.ADDITIONAL_PROPERTY]["lines"][0]["third"] = [(0.0, 0.0), (10.0, 10.0)]
        before_child = copy.deepcopy(am._assets_for("child"))
        before_main = copy.deepcopy(am._assets_for("main"))
        am._guide_drag_event({"view": "main", "handle": f"{am.ADDITIONAL_PROPERTY}:1", "phase": "press",
                              "position": {"x": 0.0, "y": 72.0}})
        am._guide_drag_event({"view": "main", "handle": f"{am.ADDITIONAL_PROPERTY}:1", "phase": "move",
                              "position": {"x": 6.0, "y": 80.0}})
        self.assertGreater(len(fake.ui.set_field_calls), 0)
        am._guide_drag_event({"view": "main", "handle": f"{am.ADDITIONAL_PROPERTY}:1", "phase": "cancel",
                              "position": {"x": 6.0, "y": 80.0}})
        self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
        self.assertEqual(am._assets_for("child"), before_child)
        self.assertEqual(am._assets_for("main"), before_main)

        # 3) Proxy contour drag via line_tools Deform (with contour-carried auto main axes)
        _scenes, fake, _uid = fresh_rig_world("A")
        am._assets_for("main").pop(am.H_PROPERTY, None)
        am._assets_for("main").pop(am.V_PROPERTY, None)
        ring_child = [(-220.0, -140.0), (220.0, -140.0), (220.0, 140.0), (-220.0, 140.0)]
        ring_main = [(-230.0, -135.0), (215.0, -145.0), (225.0, 145.0), (-215.0, 135.0)]
        am.run_proxy_contour_tool("child", ring_child, width=3.0)
        am.run_proxy_contour_tool("main", ring_main, width=3.0)
        self.assertTrue(am._assets_for("main")[am.H_PROPERTY].get("auto"))
        fake.ui.set_field_calls.clear()
        fake.ui.set_field_view_calls.clear()

        before_child = copy.deepcopy(am._assets_for("child"))
        before_main = copy.deepcopy(am._assets_for("main"))

        lt._on_handle({
            "view": "main", "property": am.DEFORM_PROPERTY, "handle": am.PROXY_PROPERTY,
            "phase": "press", "position": {"x": 215.0, "y": 0.0},
        })
        lt._on_handle({
            "view": "main", "property": am.DEFORM_PROPERTY, "handle": am.PROXY_PROPERTY,
            "phase": "move", "position": {"x": 235.0, "y": 15.0},
        })
        self.assertGreater(len(fake.ui.set_field_calls), 0)
        lt._on_handle({
            "view": "main", "property": am.DEFORM_PROPERTY, "handle": am.PROXY_PROPERTY,
            "phase": "cancel", "position": {"x": 235.0, "y": 15.0},
        })
        self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
        self.assertEqual(am._assets_for("child"), before_child)
        self.assertEqual(am._assets_for("main"), before_main)
        # 4) Axis and additional line drag via line_tools Deform + child board drag
        for handle in (am.V_PROPERTY, f"{am.ADDITIONAL_PROPERTY}:2"):
            for view_name in ("main", "child"):
                _scenes, fake, _uid = fresh_rig_world("B")
                before_child = copy.deepcopy(am._assets_for("child"))
                before_main = copy.deepcopy(am._assets_for("main"))
                pts = am.line_item(view_name, handle)["points"]
                mid = pts[len(pts) // 2]
                lt._on_handle({
                    "view": view_name, "property": am.DEFORM_PROPERTY, "handle": handle,
                    "phase": "press", "position": {"x": mid[0], "y": mid[1]},
                })
                lt._on_handle({
                    "view": view_name, "property": am.DEFORM_PROPERTY, "handle": handle,
                    "phase": "move", "position": {"x": mid[0] + 9.0, "y": mid[1] - 7.0},
                })
                self.assertGreater(len(fake.ui.set_field_calls), 0)
                lt._on_handle({
                    "view": view_name, "property": am.DEFORM_PROPERTY, "handle": handle,
                    "phase": "cancel", "position": {"x": mid[0] + 9.0, "y": mid[1] - 7.0},
                })
                self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
                self.assertEqual(am._assets_for("child"), before_child)
                self.assertEqual(am._assets_for("main"), before_main)

    def test_5_release_clears_after_maybe_auto_run_and_no_writes_during_drag(self):
        """During drag: 0 history_commit and 0 scene writes. On release: set_field_view(None) after _maybe_auto_run."""
        scenes, fake, _uid = fresh_rig_world("B")
        orig_maybe_auto_run = am._maybe_auto_run

        def tracked_maybe_auto_run():
            fake.ui.events.append(("maybe_auto_run",))
            return orig_maybe_auto_run()

        am._maybe_auto_run = tracked_maybe_auto_run
        try:
            # Record baseline scene state before drag
            main_writes_before = scenes["main"].write_count
            child_writes_before = scenes["child"].write_count
            fake.ui.commits.clear()
            fake.ui.events.clear()

            am._guide_drag_event({
                "view": "main", "handle": f"{am.ADDITIONAL_PROPERTY}:1", "phase": "press",
                "position": {"x": 0.0, "y": 72.0},
            })
            clock_val = [50.0]
            mp._clock = lambda: clock_val[0]
            for step in range(1, 5):
                clock_val[0] += 0.05
                am._guide_drag_event({
                    "view": "main", "handle": f"{am.ADDITIONAL_PROPERTY}:1", "phase": "move",
                    "position": {"x": step * 2.0, "y": 72.0 + step * 1.5},
                })

            # Verify no history_commit and no scene model writes during drag
            self.assertEqual(len(fake.ui.commits), 0)
            self.assertEqual(scenes["main"].write_count, main_writes_before)
            self.assertEqual(scenes["child"].write_count, child_writes_before)

            # Release and check ordering: maybe_auto_run strictly before ("set_field_view", "main", None)
            am._guide_drag_event({
                "view": "main", "handle": f"{am.ADDITIONAL_PROPERTY}:1", "phase": "release",
                "position": {"x": 8.0, "y": 78.0},
            })
            idx_auto_run = next(i for i, ev in enumerate(fake.ui.events) if ev[0] == "maybe_auto_run")
            idx_clear = next(
                i for i, ev in enumerate(fake.ui.events)
                if ev[0] == "set_field_view" and ev[1] == "main" and ev[2] is None
            )
            self.assertLess(idx_auto_run, idx_clear, "set_field_view(None) must happen AFTER _maybe_auto_run")

            # Also verify unmoved release clears field view
            fake.ui.events.clear()
            fake.ui.set_field_view_calls.clear()
            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": "press",
                "position": {"x": 0.0, "y": 0.0},
            })
            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": "release",
                "position": {"x": 0.0, "y": 0.0},
            })
            self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
        finally:
            am._maybe_auto_run = orig_maybe_auto_run

    def test_6_throttling_gate_flushes_last_skipped_move_via_call_later(self):
        """Moves inside the 8 ms gate schedule call_later once and flush the latest state."""
        _scenes, fake, _uid = fresh_rig_world("A")
        clock_val = [1000.0]
        mp._clock = lambda: clock_val[0]

        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "press",
            "position": {"x": 0.0, "y": 0.0},
        })
        # Move 1 executes immediately
        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 2.0, "y": 1.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        self.assertEqual(moves_later(fake), [])

        # Move 2 and Move 3 arrive 1ms and 2ms later (inside the >=8ms gate)
        clock_val[0] += 0.001
        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 6.0, "y": 3.0},
        })
        clock_val[0] += 0.001
        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 14.0, "y": 9.0},
        })
        # Both moves were throttled, and only one timer was armed
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        self.assertEqual(len(moves_later(fake)), 1)

        # Fire the scheduled callback after advancing clock past gate
        delay_ms, callback = moves_later(fake)[0]
        self.assertGreaterEqual(delay_ms, 1)
        clock_val[0] += 0.050
        callback()

        self.assertEqual(len(fake.ui.set_field_calls), 2)
        # Verify the flushed field reflects Move 3's vertical shift (+9.0 at the H/V crossing)
        _key, _W, _H, _rect, arr = _decode_last_field(fake.ui)
        center_dy = float(arr[arr.shape[0] // 2, arr.shape[1] // 2, 1])
        self.assertAlmostEqual(center_dy, 9.0, delta=0.5)
        mp.end()

    def test_0_single_update_cost_rigs_B_and_C(self):
        """Dragging an additional line on Rigs B and C keeps H/V _Frames and child Third lift
        cached and caps every PCG solve at WARM_MAX_ITER (no cold solves)."""
        warm_cap = am._FlowFieldWarp.WARM_MAX_ITER
        orig_pcg = gridsolve.pcg
        orig_frame = am._Frame

        for rig_name in ("B", "C"):
            _scenes, fake, _uid = fresh_rig_world(rig_name)
            offset = [0.0]
            mp._clock = lambda: time.perf_counter() + offset[0]

            pcg_calls = []
            frames_built = [0]

            def spy_pcg(*args, **kwargs):
                sol, iters = orig_pcg(*args, **kwargs)
                pcg_calls.append((kwargs.get("max_iter"), iters))
                return sol, iters

            class SpyFrame(orig_frame):
                def __init__(self, *args, **kwargs):
                    frames_built[0] += 1
                    super().__init__(*args, **kwargs)

            handle = f"{am.ADDITIONAL_PROPERTY}:1"
            gridsolve.pcg = spy_pcg
            am._Frame = SpyFrame
            try:
                am._guide_drag_event({
                    "view": "main", "handle": handle, "phase": "press",
                    "position": {"x": 0.0, "y": 72.0},
                })
                cached_third = mp._SESSION["third_cache"].get("value")
                self.assertIsNotNone(cached_third)
                frames_built[0] = 0

                step_durations_ms = []
                step_pcg_iters = []
                seen_caps = set()
                max_single_call_iters = 0
                for step in range(1, 16):
                    offset[0] += 0.060  # advance past throttle gate (>8ms)
                    pcg_calls.clear()
                    t0 = time.perf_counter()
                    am._guide_drag_event({
                        "view": "main", "handle": handle, "phase": "move",
                        "position": {"x": step * 0.35, "y": 72.0 + step * 0.25},
                    })
                    dt_ms = (time.perf_counter() - t0) * 1000.0
                    step_durations_ms.append(dt_ms)
                    step_pcg_iters.append(sum(it for _, it in pcg_calls))
                    seen_caps.update(cap for cap, _ in pcg_calls)
                    max_single_call_iters = max(
                        max_single_call_iters,
                        max((it for _, it in pcg_calls), default=0),
                    )
                    self.assertIs(
                        mp._SESSION["third_cache"].get("value"), cached_third,
                        f"Rig {rig_name} step {step}: third_of_batch recomputed during main drag",
                    )
                    if step == 1:
                        # At most 1 overlay guide frame on step 1, then 0 for all remaining moves.
                        self.assertLessEqual(frames_built[0], 1)
                        frames_built[0] = 0
            finally:
                gridsolve.pcg = orig_pcg
                am._Frame = orig_frame

            self.assertEqual(len(fake.ui.set_field_calls), 15)
            self.assertEqual(frames_built[0], 0, f"Rig {rig_name}: H/V _Frame rebuilt on steps 2..15")
            self.assertEqual(seen_caps, {warm_cap}, f"Rig {rig_name}: unexpected PCG max_iter caps {seen_caps}")
            self.assertLessEqual(max_single_call_iters, warm_cap)
            med_iters = statistics.median(step_pcg_iters)
            max_iters = max(step_pcg_iters)
            self.assertLessEqual(med_iters, 2 * warm_cap)
            self.assertLessEqual(max_iters, 4 * warm_cap)
            median_ms = statistics.median(step_durations_ms)
            print(
                f"[budget] Rig {rig_name} (15 steps): pcg_iters median={med_iters:.0f} max={max_iters} "
                f"(cap={warm_cap}), frames_rebuilt_steps_2_15=0, clock_median={median_ms:.2f}ms"
            )
            mp.end()

    def test_8_mapping_area_400_vertices_pre_rasterized_lut(self):
        """A 400-vertex main Mapping Area rasterizes its LUT once on press and uses O(1)
        grid lookup (no polygon rasterization) on every move frame."""
        square_strokes = [
            _stroke_dict([(-200.0, -200.0), (200.0, -200.0),
                          (200.0, 200.0), (-200.0, 200.0),
                          (-200.0, -200.0)], width=3.0),
        ]
        _scenes, fake, _uid = fresh_rig_world("A", pattern_strokes=square_strokes)
        # Build a 400-vertex star/wavy polygon around the origin with radius ~120 px
        n_verts = 400
        poly400 = [
            [
                (120.0 + 10.0 * math.sin(6.0 * (2.0 * math.pi * k / n_verts)))
                * math.cos(2.0 * math.pi * k / n_verts),
                (120.0 + 10.0 * math.sin(6.0 * (2.0 * math.pi * k / n_verts)))
                * math.sin(2.0 * math.pi * k / n_verts),
            ]
            for k in range(n_verts)
        ]
        am._assets_for("main")[am.MAPPING_AREA_PROPERTY] = {"polygons": [poly400]}

        orig_rasterize = mp._rasterize_area_lut
        orig_lookup = mp._lookup_area_lut
        rasterize_calls = [0]
        lookup_calls = [0]

        def spy_rasterize(*args, **kwargs):
            rasterize_calls[0] += 1
            return orig_rasterize(*args, **kwargs)

        def spy_lookup(*args, **kwargs):
            lookup_calls[0] += 1
            return orig_lookup(*args, **kwargs)

        offset = [0.0]
        mp._clock = lambda: time.perf_counter() + offset[0]
        mp._rasterize_area_lut = spy_rasterize
        mp._lookup_area_lut = spy_lookup
        try:
            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": "press",
                "position": {"x": 0.0, "y": 0.0},
            })
            self.assertEqual((mp._SESSION["W"], mp._SESSION["H"]), (129, 129))
            self.assertEqual(rasterize_calls[0], 1, "main Mapping Area LUT should rasterize once on press")
            rasterize_calls[0] = 0
            lookup_calls[0] = 0

            for step in range(1, 8):
                offset[0] += 0.050
                am._guide_drag_event({
                    "view": "main", "handle": am.H_PROPERTY, "phase": "move",
                    "position": {"x": step * 1.5, "y": step * 1.0},
                })
        finally:
            mp._rasterize_area_lut = orig_rasterize
            mp._lookup_area_lut = orig_lookup

        self.assertEqual(rasterize_calls[0], 0, "move frames must not re-rasterize Mapping Area polygons")
        self.assertEqual(lookup_calls[0], 7, "each move frame should perform one LUT lookup")
        _key, W, H, _rect, arr = _decode_last_field(fake.ui)
        # Center is inside the 120px-radius polygon; corners (-200, -200) are outside
        self.assertGreater(float(arr[H // 2, W // 2, 2]), 0.0)
        self.assertEqual(float(arr[0, 0, 2]), 0.0)
        self.assertEqual(float(arr[H - 1, W - 1, 2]), 0.0)
        mp.end()

    def test_9_mid_drag_frame_or_unit_switch_clears_and_stops_preview(self):
        """Switching main frame or active unit mid-drag clears display and stops previewing."""
        # Case A: Main board frame changes while dragging on child board
        scenes, fake, _uid = fresh_rig_world("A")
        clock_val = [100.0]
        mp._clock = lambda: clock_val[0]
        am._guide_drag_event({
            "view": "child", "handle": am.H_PROPERTY, "phase": "press",
            "position": {"x": 0.0, "y": 0.0},
        })
        clock_val[0] += 0.05
        am._guide_drag_event({
            "view": "child", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 2.0, "y": 1.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        self.assertIsNotNone(fake.ui.set_field_view_calls[-1][1])

        # Switch main board frame mid-drag
        scenes["main"].set_current_frame(2)
        clock_val[0] += 0.05
        am._guide_drag_event({
            "view": "child", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 4.0, "y": 2.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
        # Subsequent move still does not preview
        clock_val[0] += 0.05
        am._guide_drag_event({
            "view": "child", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 6.0, "y": 3.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        mp.end()

        # Case B: Active unit changes mid-drag
        _scenes, fake, _uid = fresh_rig_world("A")
        clock_val = [200.0]
        mp._clock = lambda: clock_val[0]
        am._guide_drag_event({
            "view": "child", "handle": am.H_PROPERTY, "phase": "press",
            "position": {"x": 0.0, "y": 0.0},
        })
        clock_val[0] += 0.05
        am._guide_drag_event({
            "view": "child", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 2.0, "y": 1.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        am._UNIT_ASSETS["child"]["other_unit"] = copy.deepcopy(am._UNIT_ASSETS["child"][_uid])
        am._ACTIVE_UNIT["id"] = "other_unit"
        clock_val[0] += 0.05
        am._guide_drag_event({
            "view": "child", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 4.0, "y": 2.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
        mp.end()

    def test_11_click_keeps_the_line_and_ends_the_preview(self):
        """A click is press, a zero-delta move (mouseReleaseEvent always sends one), release:
        the line keeps its curve and marks, with the preview on, and no history is written."""
        cases = (
            (am._guide_drag_event, "main", am.H_PROPERTY, am.H_PROPERTY),
            (am._guide_drag_event, "child", am.V_PROPERTY, am.V_PROPERTY),
            (am._guide_drag_event, "main", f"{am.ADDITIONAL_PROPERTY}:1", None),
            (lt._on_handle, "main", am.V_PROPERTY, am.V_PROPERTY),
            (lt._on_handle, "main", f"{am.ADDITIONAL_PROPERTY}:2", None),
        )
        for send, view, handle, axis in cases:
            _scenes, fake, _uid = fresh_rig_world("B")
            item = am.line_item(view, handle)
            if axis:
                item["commands"] = [{"type": "move", "to": {"x": 0.0, "y": 0.0}}]
                item["auto"] = True
                item["source"] = 7
            else:
                item["third"] = [(0.0, 0.0), (10.0, 10.0)]
            before_child = copy.deepcopy(am._assets_for("child"))
            before_main = copy.deepcopy(am._assets_for("main"))
            commits = len(fake.ui.commits)
            grab = item["points"][len(item["points"]) // 2]
            message = {"view": view, "handle": handle,
                       "position": {"x": grab[0], "y": grab[1]}}
            if send is lt._on_handle:
                message["property"] = am.DEFORM_PROPERTY
            for phase in ("press", "move", "release"):
                send(dict(message, phase=phase))
            self.assertEqual(am._assets_for("child"), before_child, handle)
            self.assertEqual(am._assets_for("main"), before_main, handle)
            self.assertEqual(len(fake.ui.commits), commits, handle)
            self.assertEqual(fake.ui.set_field_calls, [], handle)
            self.assertFalse(mp._SESSION.get("active"), handle)

    def test_16_stopped_hand_settles_and_a_new_move_cancels_the_settle(self):
        """A frame out of solver budget is shown; polishing waits for the hand to stop."""
        _scenes, fake, _uid = fresh_rig_world("C")
        pts = am._assets_for("child")[am.H_PROPERTY]["points"]
        mid = pts[len(pts) // 2]
        offset = [0.0]
        mp._clock = lambda: time.perf_counter() + offset[0]

        def drag(phase, step=0):
            offset[0] += 0.06
            am._guide_drag_event({
                "view": "child", "handle": am.H_PROPERTY, "phase": phase,
                "position": {"x": mid[0] + 2.4 * step, "y": mid[1] - 6.4 * step},
            })

        drag("press")
        warm = mp._SESSION["warm"]
        for step in range(1, 8):
            drag("move", step)
        self.assertTrue(warm.get("unsettled"), "6.8 px per frame should outrun the budget")
        stale = fake.ui.call_later_calls[-1]
        self.assertEqual(stale[0], mp.SETTLE_DELAY_MS)

        drag("move", 8)
        frames = len(fake.ui.set_field_calls)
        stale[1]()
        self.assertEqual(len(fake.ui.set_field_calls), frames, "a move must cancel the old settle")

        for _ in range(mp.SETTLE_PASSES):
            if not warm.get("unsettled"):
                break
            fake.ui.call_later_calls[-1][1]()
        self.assertFalse(warm.get("unsettled"))
        self.assertGreater(len(fake.ui.set_field_calls), frames)

        ca, ma = am._assets_for("child"), am._assets_for("main")
        cold, _ = am.build_mapper(ca[am.H_PROPERTY], ca[am.V_PROPERTY], ma[am.H_PROPERTY],
                                  ma[am.V_PROPERTY], additional_pairs=am._additional_pairs(),
                                  ground_pairs=am._ground_pairs())
        last = np.frombuffer(fake.ui.set_field_calls[-1][3], dtype="<f4")
        W, H = fake.ui.set_field_calls[-1][1], fake.ui.set_field_calls[-1][2]
        field = last.reshape(H, W, 4)
        x0, y0, w, h = mp._SESSION["rect"]
        worst = 0.0
        for j in range(4, H, 12):
            for i in range(4, W, 16):
                if field[j, i, 2] <= 0.0:
                    continue
                p = (x0 + w * i / (W - 1), y0 + h * j / (H - 1))
                q = cold(p)
                if q is not None:
                    worst = max(worst, math.hypot(p[0] + field[j, i, 0] - q[0],
                                                  p[1] + field[j, i, 1] - q[1]))
        print(f"[settle] field against the cold mapper after settling: {worst:.3f} px")
        self.assertLessEqual(worst, 0.5)
        mp.end()

    def test_17_stopped_hand_gets_one_exact_pass_and_only_one(self):
        """Layer order is re-judged and the stations searched in full once per stop."""
        _scenes, fake, _uid = fresh_rig_world("D")
        pts = am._assets_for("main")[am.H_PROPERTY]["points"]
        mid = pts[len(pts) // 2]
        offset = [0.0]
        mp._clock = lambda: time.perf_counter() + offset[0]
        calls = [0]
        orig = am._prepare_fold_context

        def spy(*args, **kwargs):
            calls[0] += 1
            return orig(*args, **kwargs)

        def drag(phase, step=0):
            offset[0] += 0.06
            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": phase,
                "position": {"x": mid[0], "y": mid[1] - 0.6 * step},
            })

        am._prepare_fold_context = spy
        try:
            drag("press")
            for step in range(1, 4):
                drag("move", step)
            calls[0] = 0
            warm = mp._SESSION["warm"]
            fired = 0
            while fired < mp.SETTLE_PASSES and len(fake.ui.call_later_calls):
                before = len(fake.ui.call_later_calls)
                fake.ui.call_later_calls[-1][1]()
                fired += 1
                if len(fake.ui.call_later_calls) == before:
                    break
            self.assertEqual(calls[0], 1, "one exact layer order per stop")
            self.assertTrue(warm.get("settling"))
            frames = len(fake.ui.set_field_calls)
            self.assertFalse(warm.get("unsettled"))
            offset[0] += 1.0     # the exact pass was long: past its update gap
            drag("move", 4)
            self.assertFalse(warm.get("settling"), "a new move goes back to the budget")
            self.assertEqual(len(fake.ui.set_field_calls), frames + 1)
            mp.end()
        finally:
            am._prepare_fold_context = orig

    def test_18_settle_keeps_away_from_a_moving_hand_and_a_changed_context(self):
        _scenes, fake, _uid = fresh_rig_world("B")
        offset = [0.0]
        mp._clock = lambda: offset[0]

        def drag(phase, y=0.0):
            am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": phase,
                                  "position": {"x": 0.0, "y": y}})

        drag("press")
        offset[0] += 1.0
        drag("move", 1.0)
        armed = fake.ui.call_later_calls[-1]
        self.assertEqual(armed[0], mp.SETTLE_DELAY_MS)
        # A move the gate skips still takes the token.
        offset[0] += 0.001
        drag("move", 2.0)
        frames = len(fake.ui.set_field_calls)
        armed[1]()
        self.assertEqual(len(fake.ui.set_field_calls), frames)

        offset[0] += 1.0
        drag("move", 3.0)
        armed = fake.ui.call_later_calls[-1]
        am._ACTIVE_UNIT["id"] = None
        armed[1]()
        self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
        self.assertFalse(mp._SESSION.get("active"))

    def _drag_with_grid(self, rig, view, handle, grid=True):
        _scenes, fake, uid = fresh_rig_world(rig)
        am._UNIT_META[uid].setdefault("settings", {})["show_grid"] = grid
        points = am.line_item(view, handle)["points"]
        grab = points[len(points) // 2]
        offset = [0.0]
        mp._clock = lambda: time.perf_counter() + offset[0]

        def send(phase, step=0):
            offset[0] += 0.06
            am._guide_drag_event({
                "view": view, "handle": handle, "phase": phase,
                "position": {"x": grab[0] + 2.0 * step, "y": grab[1] - 3.0 * step},
            })

        before = am._grid_overlay_items("main")
        send("press")
        for step in range(1, 6):
            send("move", step)
        for _ in range(mp.SETTLE_PASSES):
            if not fake.ui.call_later_calls:
                break
            count = len(fake.ui.call_later_calls)
            fake.ui.call_later_calls[-1][1]()
            if len(fake.ui.call_later_calls) == count:
                break
        return send, before

    def test_19_refer_grid_follows_the_drag(self):
        """The grid is drawn from the frame's mapper; settled, it sits on the exact one."""
        cases = (
            ("B", "main", f"{am.ADDITIONAL_PROPERTY}:1"),
            ("C", "child", am.H_PROPERTY),
            ("D", "main", am.H_PROPERTY),
        )
        for rig, view, handle in cases:
            send, before = self._drag_with_grid(rig, view, handle)
            live = am._GRID_CACHE["main"]
            self.assertIs(live, mp._SESSION["grid_items"]["main"], rig)
            self.assertGreater(_grid_gap(live, before), 1.0, f"{rig}: the grid did not move")
            live_child = am._GRID_CACHE["child"] if view == "child" else None

            am._invalidate_grid_cache()
            exact = am._grid_overlay_items("main")
            gap = _grid_gap(live, exact)
            print(f"[grid] rig {rig} {view}: {gap:.3f} px off the exact grid")
            self.assertLessEqual(gap, 0.5, rig)
            if live_child is not None:
                self.assertLessEqual(_grid_gap(live_child, am._grid_overlay_items("child")), 0.01)

            am._GRID_CACHE["main"] = live
            send("release", 5)
            self.assertIsNot(am._GRID_CACHE["main"], live, f"{rig}: the release keeps the exact grid")
            self.assertNotIn("grid_items", mp._SESSION)

    def test_20_cancel_puts_the_grid_back(self):
        send, before = self._drag_with_grid("C", "child", am.V_PROPERTY)
        self.assertGreater(_grid_gap(am._GRID_CACHE["main"], before), 1.0)
        send("cancel")
        self.assertLessEqual(_grid_gap(am._grid_overlay_items("main"), before), 1e-6)

    def test_21_no_grid_work_with_the_grid_off(self):
        send, _before = self._drag_with_grid("C", "child", am.H_PROPERTY, grid=False)
        self.assertEqual(mp._LAST_TIMINGS["grid"], 0.0)
        self.assertNotIn("grid_items", mp._SESSION)
        send("release", 5)

    def test_22_scope_widths_match_the_release(self):
        """The field already scales the outline: the scope must not scale it twice."""
        for factor in (1.0, 0.5, 1.6):
            _scenes, fake, _uid = fresh_rig_world("B")
            main = am._assets_for("main")
            pts = main[am.H_PROPERTY]["points"]
            cx = sum(p[0] for p in pts) / len(pts)
            cy = sum(p[1] for p in pts) / len(pts)
            lines = [main[am.H_PROPERTY], main[am.V_PROPERTY],
                     *main.get(am.ADDITIONAL_PROPERTY, {}).get("lines", [])]
            for line in lines:
                line["points"] = [[cx + (x - cx) * factor, cy + (y - cy) * factor]
                                  for x, y in line["points"]]
                line.pop("third", None)
            ca = am._assets_for("child")
            _cold, release_scale = am.build_mapper(
                ca[am.H_PROPERTY], ca[am.V_PROPERTY], main[am.H_PROPERTY], main[am.V_PROPERTY],
                additional_pairs=am._additional_pairs(), ground_pairs=am._ground_pairs())
            grab = ca[am.H_PROPERTY]["points"][len(ca[am.H_PROPERTY]["points"]) // 2]
            for phase, step in (("press", 0), ("move", 1)):
                am._guide_drag_event({"view": "child", "handle": am.H_PROPERTY, "phase": phase,
                                      "position": {"x": grab[0], "y": grab[1] - 2.0 * step}})
            scope = fake.ui.set_field_view_calls[-1][1]["width_scale"]
            print(f"[width] main x{factor}: release x{release_scale:.3f}, scope x{scope:.3f}")
            self.assertAlmostEqual(scope, 1.0, delta=0.1, msg=f"x{factor}")
            mp.cancel()

    def test_23_a_move_after_a_slow_frame_is_not_held_back(self):
        """C++ lets a drag move through once per 33 ms already; only the 8 ms
        floor may hold one back, not a share of the last frame's cost."""
        _scenes, fake, _uid = fresh_rig_world("A")
        clock_val = [1000.0]
        mp._clock = lambda: clock_val[0]
        raw_build = am.build_mapper

        def slow_build(*args, **kwargs):
            clock_val[0] += 0.040   # every frame takes 40 ms
            return raw_build(*args, **kwargs)

        am.build_mapper = slow_build
        self.addCleanup(setattr, am, "build_mapper", raw_build)

        def send(phase, x, y):
            am._guide_drag_event({"view": "main", "handle": am.H_PROPERTY, "phase": phase,
                                  "position": {"x": x, "y": y}})

        send("press", 0.0, 0.0)
        send("move", 2.0, 1.0)
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        clock_val[0] += 0.010
        send("move", 4.0, 2.0)
        self.assertEqual(len(fake.ui.set_field_calls), 2, "the move waited for half a frame")
        self.assertEqual(moves_later(fake), [])
        mp.end()

    def test_24_guides_that_did_not_move_keep_their_frames(self):
        """Building a frame of curved guides takes milliseconds: a drag of
        anything but a guide builds none, a texture-axis drag only its own."""
        built = []
        raw_frame = am._Frame

        class Counting(raw_frame):
            def __init__(self, h_spec, v_spec):
                built.append(1)
                super().__init__(h_spec, v_spec)

        am._Frame = Counting
        self.addCleanup(setattr, am, "_Frame", raw_frame)
        cases = (
            ("B", "main", f"{am.ADDITIONAL_PROPERTY}:1", 0),
            ("C", "child", am.H_PROPERTY, 1),
        )
        for rig, view, handle, per_frame in cases:
            _scenes, fake, _uid = fresh_rig_world(rig)
            points = am.line_item(view, handle)["points"]
            grab = points[len(points) // 2]
            offset = [0.0]
            mp._clock = lambda: time.perf_counter() + offset[0]

            def send(phase, step=0):
                offset[0] += 0.06
                am._guide_drag_event({
                    "view": view, "handle": handle, "phase": phase,
                    "position": {"x": grab[0] + 1.5 * step, "y": grab[1] - 2.0 * step},
                })

            send("press")
            am._main_guide_frame()   # the overlay builds its own once per guide set
            built.clear()
            frames = len(fake.ui.set_field_calls)
            for step in range(1, 5):
                send("move", step)
            frames = len(fake.ui.set_field_calls) - frames
            self.assertEqual(frames, 4, rig)
            self.assertEqual(len(built), per_frame * frames, f"{rig} {view}: frames rebuilt")
            mp.cancel()

    def test_10_mid_drag_invalid_axes_clears_and_recovers(self):
        """When axes stop crossing mid-drag, preview clears; when crossing again, preview recovers."""
        _scenes, fake, _uid = fresh_rig_world("A")
        clock_val = [300.0]
        mp._clock = lambda: clock_val[0]
        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "press",
            "position": {"x": 0.0, "y": 0.0},
        })
        # Move 1: valid (y = 10 crosses V axis [-200, 200])
        clock_val[0] += 0.05
        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 0.0, "y": 10.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        self.assertEqual(len(fake.ui.set_field_view_calls), 1)
        self.assertIsNotNone(fake.ui.set_field_view_calls[-1][1])

        # Move 2: drag H axis above V axis's top end (y = 260 > 200) so axes no longer cross
        clock_val[0] += 0.05
        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 0.0, "y": 260.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 1)
        self.assertEqual(fake.ui.set_field_view_calls[-1], ("main", None))
        self.assertIsNone(mp._SESSION.get("last_passes"))

        # Move 3: drag H axis back inside V axis range (y = 20) -> preview recovers
        clock_val[0] += 0.05
        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "move",
            "position": {"x": 0.0, "y": 20.0},
        })
        self.assertEqual(len(fake.ui.set_field_calls), 2)
        self.assertIsNotNone(fake.ui.set_field_view_calls[-1][1])
        mp.end()

    def _assert_frame_depth_matches_fold_depth(self, ui, orig_prepare, min_agreement=0.99):
        _key, W, H, rect, arr = _decode_last_field(ui)
        x0, y0, rw, rh = rect
        du, dv = rw / (W - 1), rh / (H - 1)

        # Convert (H, W, 4) buffer back to (W, H) grid indexing matching px, py
        dx = arr[:, :, 0].T.astype(np.float64)
        dy = arr[:, :, 1].T.astype(np.float64)
        tag = arr[:, :, 2].T.astype(np.float64)
        valid = tag > 0.0
        pred_depth = np.rint(tag).astype(np.int32) - 1

        gx = np.linspace(x0, x0 + rw, W, dtype=np.float64)
        gy = np.linspace(y0, y0 + rh, H, dtype=np.float64)
        px, py = np.meshgrid(gx, gy, indexing="ij")

        specs = mp._effective_specs_and_pairs(am, mp._SESSION)
        self.assertIsNotNone(specs)
        ch_h, ch_v, mn_h, mn_v, add_pairs, gnd_pairs = specs
        ref_warm = copy.deepcopy(mp._SESSION["warm"])
        ref_mapper, _ws = am.build_mapper(
            ch_h, ch_v, mn_h, mn_v,
            additional_pairs=add_pairs, ground_pairs=gnd_pairs, warm=ref_warm,
        )
        self.assertIsNotNone(ref_mapper)

        import gridsolve
        det = gridsolve.det2(
            (px + dx) * float(ref_mapper.fold_reference),
            py + dy,
            du,
            dv,
        )
        is_front = det >= 0.0

        # 1. Parity and front/back must agree 100% on ALL valid nodes
        valid_parity_ok = ((pred_depth[valid] % 2) == 0) == is_front[valid]
        self.assertTrue(
            bool(np.all(valid_parity_ok)),
            "Depth parity and is_front disagreed on valid nodes",
        )

        # 2. Prepare reference fold context outside preview and evaluate direct _fold_depth
        l_h, l_v, _ok = ref_mapper.third_of_batch(px, py)
        v_lh = l_h[valid]
        v_lv = l_v[valid]
        warp = getattr(ref_mapper, "warp", None)
        if warp is not None:
            w_h, w_v = warp.apply_batch(v_lh, v_lv)
        else:
            w_h, w_v = v_lh, v_lv
        arc_h = np.where(w_h >= 0.0, w_h * ref_mapper.h_scales[1], w_h * ref_mapper.h_scales[0])
        arc_v = np.where(w_v >= 0.0, w_v * ref_mapper.v_scales[1], w_v * ref_mapper.v_scales[0])
        h_range = (float(np.min(arc_h)), float(np.max(arc_h)))
        v_range = (float(np.min(arc_v)), float(np.max(arc_v)))
        orig_prepare(ref_mapper, h_range, v_range)

        front_const = am._MappedOutput.FRONT
        back_const = am._MappedOutput.BACK
        interior_total = 0
        interior_match = 0
        seen_depths = set()

        for i in range(1, W - 1):
            for j in range(1, H - 1):
                if not valid[i, j]:
                    continue
                nb_valid = valid[i - 1:i + 2, j - 1:j + 2]
                if not np.all(nb_valid):
                    continue
                nb_front = is_front[i - 1:i + 2, j - 1:j + 2]
                if not np.all(nb_front == nb_front[1, 1]):
                    continue
                pt = (float(px[i, j]), float(py[i, j]))
                side = front_const if is_front[i, j] else back_const
                ref_d = int(am._fold_depth(ref_mapper, pt, side))
                pred_d = int(pred_depth[i, j])
                seen_depths.add(pred_d)
                self.assertEqual(
                    pred_d % 2, ref_d % 2,
                    f"Parity mismatch at ({i}, {j}): pred={pred_d}, ref={ref_d}",
                )
                interior_total += 1
                if pred_d == ref_d:
                    interior_match += 1

        self.assertGreater(interior_total, 200)
        agreement = interior_match / float(interior_total)
        self.assertGreaterEqual(
            agreement, min_agreement,
            f"Interior depth agreement {agreement:.4f} < {min_agreement:.4f} "
            f"({interior_match}/{interior_total})",
        )
        return agreement, seen_depths

    def test_12_zero_prepare_fold_context_calls_during_drag(self):
        """_prepare_fold_context is called once in begin() and 0 times during drag updates."""
        _scenes, fake, _uid = fresh_rig_world("D")
        clock_val = [100.0]
        mp._clock = lambda: clock_val[0]

        orig_prepare = am._prepare_fold_context
        call_count = [0]

        def counted_prepare(*args, **kwargs):
            call_count[0] += 1
            return orig_prepare(*args, **kwargs)

        am._prepare_fold_context = counted_prepare
        try:
            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": "press",
                "position": {"x": 640.0, "y": 360.0},
            })
            self.assertEqual(call_count[0], 1, "Expected 1 _prepare_fold_context call in begin()")

            call_count[0] = 0
            for step in range(1, 15):
                clock_val[0] += 0.30  # even well past 200ms, never calls _prepare_fold_context
                am._guide_drag_event({
                    "view": "main", "handle": am.H_PROPERTY, "phase": "move",
                    "position": {"x": 640.0, "y": 360.0 - step * 0.6},
                })
            self.assertEqual(len(fake.ui.set_field_calls), 14)
            self.assertEqual(call_count[0], 0, "Expected 0 _prepare_fold_context calls during drag")
            mp.end()
        finally:
            am._prepare_fold_context = orig_prepare

    def test_13_continuous_drag_depth_accuracy_rig_D_and_3layer(self):
        """Continuous drag on Rig D and a 3-layer rig matches direct _fold_depth >=99% (>1 cell from crease)."""
        orig_prepare = am._prepare_fold_context
        for rig_name, expected_layers in (("D", {0, 1}), ("D3", {0, 1, 2})):
            _scenes, fake, _uid = fresh_rig_world(rig_name)
            clock_val = [200.0]
            mp._clock = lambda: clock_val[0]

            drag_prepare_calls = [0]

            def spy_prepare(*args, **kwargs):
                drag_prepare_calls[0] += 1
                return orig_prepare(*args, **kwargs)

            am._prepare_fold_context = spy_prepare
            try:
                am._guide_drag_event({
                    "view": "main", "handle": am.H_PROPERTY, "phase": "press",
                    "position": {"x": 640.0, "y": 360.0},
                })
                self.assertEqual(drag_prepare_calls[0], 1)
                drag_prepare_calls[0] = 0

                for step in range(1, 6):
                    clock_val[0] += 0.25
                    am._guide_drag_event({
                        "view": "main", "handle": am.H_PROPERTY, "phase": "move",
                        "position": {"x": 640.0 + step * 0.4, "y": 360.0 - step * 0.6},
                    })
                    self.assertEqual(drag_prepare_calls[0], 0)
                    agreement, seen_depths = self._assert_frame_depth_matches_fold_depth(
                        fake.ui, orig_prepare, min_agreement=0.99,
                    )
                    self.assertTrue(
                        expected_layers.issubset(seen_depths),
                        f"Rig {rig_name} step {step}: expected {expected_layers} in {seen_depths}",
                    )
                mp.end()
            finally:
                am._prepare_fold_context = orig_prepare

    def test_14_fold_appear_and_disappear_mid_drag(self):
        """Mid-drag fold appearance (0 -> 0,1,2) and disappearance (0,1,2 -> 0) match _fold_depth >=99%."""
        orig_prepare = am._prepare_fold_context
        n_seg = 40
        ch_h = [(-300.0 + 600.0 * i / n_seg, 0.0) for i in range(n_seg + 1)]
        ch_v = [(-140.0, -200.0 + 400.0 * i / n_seg) for i in range(n_seg + 1)]
        mn_v = [[-140.0, -200.0 + 400.0 * i / n_seg] for i in range(n_seg + 1)]

        def make_main_h(amp):
            pts = []
            for i in range(n_seg + 1):
                s = -1.0 + 2.0 * i / n_seg
                u = s - 0.25
                # Localized S-wave around s = 0.25 (to the right of the crossing at x = -140)
                env = math.exp(-6.0 * u * u)
                x = 300.0 * s - amp * math.sin(2.0 * math.pi * u) * env
                y = 12.0 * math.sin(0.5 * math.pi * s)
                pts.append([x, y])
            return pts

        z_strokes = [
            _stroke_dict([(-232.0, -72.0), (232.0, -72.0),
                          (232.0, 72.0), (-232.0, 72.0),
                          (-232.0, -72.0)], width=3.0),
        ]

        # Case A: New fold appears mid-drag (amp goes from 20 [unfolded] -> 140 [3-layer folded])
        _scenes, fake, uid = fresh_rig_world("A", pattern_strokes=z_strokes)
        clock_val = [300.0]
        mp._clock = lambda: clock_val[0]
        am._UNIT_ASSETS["child"][uid] = {
            am.H_PROPERTY: {"points": [list(p) for p in ch_h], "width": 3.0},
            am.V_PROPERTY: {"points": [list(p) for p in ch_v], "width": 3.0},
        }
        am._UNIT_ASSETS["main"][uid] = {
            am.H_PROPERTY: {"points": make_main_h(20.0), "width": 3.0},
            am.V_PROPERTY: {"points": copy.deepcopy(mn_v), "width": 3.0},
        }

        drag_prepare_calls = [0]

        def spy_prepare(*args, **kwargs):
            drag_prepare_calls[0] += 1
            return orig_prepare(*args, **kwargs)

        am._prepare_fold_context = spy_prepare
        try:
            mp.begin("main", am.H_PROPERTY)
            self.assertEqual(drag_prepare_calls[0], 1)
            drag_prepare_calls[0] = 0

            seen_by_step = []
            for amp in (25.0, 65.0, 95.0, 125.0, 145.0):
                clock_val[0] += 0.25
                am._UNIT_ASSETS["main"][uid][am.H_PROPERTY]["points"] = make_main_h(amp)
                mp.update()
                self.assertEqual(drag_prepare_calls[0], 0)
                _ag, seen = self._assert_frame_depth_matches_fold_depth(
                    fake.ui, orig_prepare, min_agreement=0.99,
                )
                seen_by_step.append(seen)

            self.assertEqual(seen_by_step[0], {0}, "First step should be unfolded {0}")
            self.assertEqual(seen_by_step[-1], {0, 1, 2}, "Last step should have 3 layers {0, 1, 2}")
            mp.end()

            # Case B: Fold disappears mid-drag (amp goes from 145 [3-layer folded] -> 20 [unfolded])
            _scenes, fake, uid = fresh_rig_world("A", pattern_strokes=z_strokes)
            mp._clock = lambda: clock_val[0]
            am._UNIT_ASSETS["child"][uid] = {
                am.H_PROPERTY: {"points": [list(p) for p in ch_h], "width": 3.0},
                am.V_PROPERTY: {"points": [list(p) for p in ch_v], "width": 3.0},
            }
            am._UNIT_ASSETS["main"][uid] = {
                am.H_PROPERTY: {"points": make_main_h(145.0), "width": 3.0},
                am.V_PROPERTY: {"points": copy.deepcopy(mn_v), "width": 3.0},
            }

            drag_prepare_calls[0] = 0
            mp.begin("main", am.H_PROPERTY)
            self.assertEqual(drag_prepare_calls[0], 1)
            drag_prepare_calls[0] = 0

            seen_disappear = []
            for amp in (140.0, 115.0, 85.0, 45.0, 20.0):
                clock_val[0] += 0.25
                am._UNIT_ASSETS["main"][uid][am.H_PROPERTY]["points"] = make_main_h(amp)
                mp.update()
                self.assertEqual(drag_prepare_calls[0], 0)
                _ag, seen = self._assert_frame_depth_matches_fold_depth(
                    fake.ui, orig_prepare, min_agreement=0.99,
                )
                seen_disappear.append(seen)

            self.assertEqual(seen_disappear[0], {0, 1, 2}, "First step should have 3 layers {0, 1, 2}")
            self.assertEqual(seen_disappear[-1], {0}, "Last step should unfold back to {0}")
            mp.end()
        finally:
            am._prepare_fold_context = orig_prepare

    def test_15_rig_D_40_steps_no_cold_solve_or_exact_depth(self):
        """Across a 40-step drag on folded Rig D, no move frame triggers _prepare_fold_context,
        exact _update_coarse_depth_table, child _Frame rebuilds, third_of_batch recomputes,
        or uncapped cold PCG solves."""
        _scenes, fake, _uid = fresh_rig_world("D")
        offset = [0.0]
        mp._clock = lambda: time.perf_counter() + offset[0]

        warm_cap = am._FlowFieldWarp.WARM_MAX_ITER
        orig_prepare = am._prepare_fold_context
        orig_coarse = mp._update_coarse_depth_table
        orig_inherit = mp._inherit_region_depths
        orig_pcg = gridsolve.pcg
        drag_prepare_calls = [0]
        coarse_depth_calls = [0]
        inherit_depth_calls = [0]
        pcg_calls = []

        def spy_prepare(*args, **kwargs):
            drag_prepare_calls[0] += 1
            return orig_prepare(*args, **kwargs)

        def spy_coarse(*args, **kwargs):
            coarse_depth_calls[0] += 1
            return orig_coarse(*args, **kwargs)

        def spy_inherit(*args, **kwargs):
            inherit_depth_calls[0] += 1
            return orig_inherit(*args, **kwargs)

        def spy_pcg(*args, **kwargs):
            sol, iters = orig_pcg(*args, **kwargs)
            pcg_calls.append((kwargs.get("max_iter"), iters))
            return sol, iters

        am._prepare_fold_context = spy_prepare
        mp._update_coarse_depth_table = spy_coarse
        mp._inherit_region_depths = spy_inherit
        gridsolve.pcg = spy_pcg
        try:
            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": "press",
                "position": {"x": 640.0, "y": 360.0},
            })
            self.assertEqual(drag_prepare_calls[0], 1)
            self.assertEqual(coarse_depth_calls[0], 1)
            drag_prepare_calls[0] = 0
            coarse_depth_calls[0] = 0
            inherit_depth_calls[0] = 0

            cached_third = mp._SESSION["third_cache"].get("value")
            cached_child_frame = mp._SESSION["warm"]["frames"].get("child")[1]
            self.assertIsNotNone(cached_third)
            self.assertIsNotNone(cached_child_frame)

            step_durations_ms = []
            step_pcg_iters = []
            seen_caps = set()
            max_single_call_iters = 0
            for step in range(1, 41):
                offset[0] += 0.060  # advance past throttle gate every step
                pcg_calls.clear()
                t0 = time.perf_counter()
                am._guide_drag_event({
                    "view": "main", "handle": am.H_PROPERTY, "phase": "move",
                    "position": {"x": 640.0, "y": 360.0 - step * 0.6},
                })
                dt_ms = (time.perf_counter() - t0) * 1000.0
                step_durations_ms.append(dt_ms)
                step_pcg_iters.append(sum(it for _, it in pcg_calls))
                seen_caps.update(cap for cap, _ in pcg_calls)
                max_single_call_iters = max(
                    max_single_call_iters,
                    max((it for _, it in pcg_calls), default=0),
                )
                self.assertIs(mp._SESSION["third_cache"].get("value"), cached_third)
                self.assertIs(mp._SESSION["warm"]["frames"].get("child")[1], cached_child_frame)

            self.assertEqual(len(fake.ui.set_field_calls), 40)
            self.assertEqual(drag_prepare_calls[0], 0)
            self.assertEqual(coarse_depth_calls[0], 0)
            self.assertEqual(inherit_depth_calls[0], 40)
            self.assertEqual(seen_caps, {warm_cap})
            self.assertLessEqual(max_single_call_iters, warm_cap)
            med_iters = statistics.median(step_pcg_iters)
            max_iters = max(step_pcg_iters)
            self.assertLessEqual(med_iters, 4 * warm_cap)
            self.assertLessEqual(max_iters, 8 * warm_cap)

            max_ms = max(step_durations_ms)
            median_ms = statistics.median(step_durations_ms)
            print(
                f"[budget] Rig D (40 steps): pcg_iters median={med_iters:.0f} max={max_iters} "
                f"(cap={warm_cap}), exact_depth=0, inherited_depth=40, "
                f"clock median={median_ms:.2f}ms max={max_ms:.2f}ms"
            )
            mp.end()
        finally:
            am._prepare_fold_context = orig_prepare
            mp._update_coarse_depth_table = orig_coarse
            mp._inherit_region_depths = orig_inherit
            gridsolve.pcg = orig_pcg


if __name__ == "__main__":
    unittest.main()
