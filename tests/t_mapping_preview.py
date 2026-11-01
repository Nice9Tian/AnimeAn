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
6. Throttling gate: rapid moves skipped by max(8ms, 0.5*t) schedule call_later, and
   firing the callback flushes the latest position.
7. Single-step update latency: dragging an additional line on Rigs B and C has median
   cost <= 33 ms.
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


def _default_pattern_strokes():
    """Pattern strokes covering [-240, 240] x [-160, 160] on the child board."""
    strokes = []
    # Outer frame & interior cross-hatching so bounding box is well-defined
    strokes.append(_stroke_dict([(-232.0, -152.0), (232.0, -152.0),
                                 (232.0, 152.0), (-232.0, 152.0),
                                 (-232.0, -152.0)], width=3.0))
    for y in (-100.0, -40.0, 20.0, 80.0):
        pts = [(-200.0 + 40.0 * k, y + 10.0 * math.sin(k * 0.6)) for k in range(11)]
        strokes.append(_stroke_dict(pts, width=2.5))
    return strokes


def _build_rig_assets(rig_name):
    """Build (child_assets, main_assets) matching tests/bench_drag_cost.py Rigs A, B, C."""
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
    child.pattern[c_lid] = list(pattern_strokes if pattern_strokes is not None else _default_pattern_strokes())

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
        """Rapid moves skipped by max(8ms, 0.5*t) schedule call_later once and flush the latest state."""
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
        self.assertEqual(len(fake.ui.call_later_calls), 0)

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
        self.assertEqual(len(fake.ui.call_later_calls), 1)

        # Fire the scheduled callback after advancing clock past gate
        delay_ms, callback = fake.ui.call_later_calls[0]
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
        """Median single-step update cost dragging an additional line on Rigs B and C is <= 33 ms."""
        for rig_name in ("B", "C"):
            _scenes, fake, _uid = fresh_rig_world(rig_name)
            clock_val = [0.0]

            # Use real perf_counter plus a virtual offset so the throttle gate never skips
            # while _compute_and_push still measures real wall-clock deltas!
            offset = [0.0]
            mp._clock = lambda: time.perf_counter() + offset[0]

            handle = f"{am.ADDITIONAL_PROPERTY}:1"
            am._guide_drag_event({
                "view": "main", "handle": handle, "phase": "press",
                "position": {"x": 0.0, "y": 72.0},
            })

            step_durations_ms = []
            sub_timings = []
            for step in range(1, 16):
                offset[0] += 0.060  # advance past throttle gate (>8ms) while keeping depth low-freq
                t0 = time.perf_counter()
                am._guide_drag_event({
                    "view": "main", "handle": handle, "phase": "move",
                    "position": {"x": step * 0.35, "y": 72.0 + step * 0.25},
                })
                dt_ms = (time.perf_counter() - t0) * 1000.0
                step_durations_ms.append(dt_ms)
                sub_timings.append(mp.last_timings())

            median_ms = statistics.median(step_durations_ms)
            med_sub = {
                k: statistics.median([st.get(k, 0.0) * 1000.0 for st in sub_timings])
                for k in ("build_mapper", "third_of_batch", "main_of_third_batch",
                          "det2", "depth_table", "pack_and_upload", "total")
            }
            depth_runs_ms = [st["depth_table"] * 1000.0 for st in sub_timings if st.get("depth_table", 0.0) > 0.0]
            print(
                f"[timing] Rig {rig_name} (15 steps): median={median_ms:.2f}ms "
                f"(build_mapper={med_sub['build_mapper']:.2f}ms, "
                f"third_of_batch={med_sub['third_of_batch']:.2f}ms, "
                f"main_of_third_batch={med_sub['main_of_third_batch']:.2f}ms, "
                f"det2={med_sub['det2']:.2f}ms, "
                f"pack_and_upload={med_sub['pack_and_upload']:.2f}ms, "
                f"depth_table_when_run={statistics.median(depth_runs_ms) if depth_runs_ms else 0.0:.2f}ms)"
            )
            self.assertLessEqual(
                median_ms, 33.0,
                f"Rig {rig_name}: median update cost {median_ms:.2f} ms exceeds 33 ms budget",
            )
            mp.end()

    def test_8_mapping_area_400_vertices_under_2ms(self):
        """A 400-vertex main Mapping Area on a 129x129 grid takes <= 2 ms per frame."""
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

        offset = [0.0]
        mp._clock = lambda: time.perf_counter() + offset[0]
        am._guide_drag_event({
            "view": "main", "handle": am.H_PROPERTY, "phase": "press",
            "position": {"x": 0.0, "y": 0.0},
        })
        self.assertEqual((mp._SESSION["W"], mp._SESSION["H"]), (129, 129))

        area_ms_list = []
        for step in range(1, 8):
            offset[0] += 0.050
            am._guide_drag_event({
                "view": "main", "handle": am.H_PROPERTY, "phase": "move",
                "position": {"x": step * 1.5, "y": step * 1.0},
            })
            area_ms_list.append(mp.last_timings().get("main_area", 0.0) * 1000.0)

        med_area_ms = statistics.median(area_ms_list)
        self.assertLessEqual(
            med_area_ms, 2.0,
            f"400-vertex main_area lookup on 129x129 took {med_area_ms:.3f} ms (> 2.0 ms)",
        )
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


if __name__ == "__main__":
    unittest.main()
