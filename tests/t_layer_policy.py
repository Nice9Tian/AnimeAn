"""Layer-selection-driven tool policy (round-2 G6): which Tools page comes up,
which tools are locked, and the pen's second meaning on an independent fill
layer. Policy half only - the C++ mechanisms it drives (fill-paint mode, the
locked-tool chips) are not reachable from here, so the stubs record the calls
the module makes instead."""
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))


# --- fake bindings ----------------------------------------------------------

class FakeWindows:
    def __init__(self):
        self.selected = []       # (name, page)
        self.visible = {"tools": True}

    def list(self):
        return [{"name": "tools", "title": "Tools", "visible": self.visible["tools"],
                 "pages": ["painting", "mapping", "fukusato"], "current": "painting"}]

    def select(self, name, page):
        self.selected.append((name, page))


class FakeUi:
    def __init__(self):
        self.locked = []         # every set_locked_tools push, in order
        self.fill_paint = []     # every set_fill_paint_mode push, in order
        self.commits = []
        self.refreshes = []
        self.windows = FakeWindows()

    def set_hook_events(self, events):
        pass

    def set_locked_tools(self, view, tools):
        self.locked.append((view, tuple(tools)))

    def set_fill_paint_mode(self, view, on):
        self.fill_paint.append((view, bool(on)))

    def refresh(self):
        self.refreshes.append("all")

    def history_commit(self, label, view=""):
        self.commits.append((label, view))


UI = FakeUi()
_stub = types.ModuleType("animean_python")
_stub.ui = UI
sys.modules.setdefault("animean_python", _stub)

import python_hooks  # noqa: E402
import window_manager  # noqa: E402
import layer_tool_policy as ltp  # noqa: E402


def _point(value):
    if isinstance(value, dict):
        return {"x": float(value.get("x", 0.0)), "y": float(value.get("y", 0.0))}
    return {"x": float(value[0]), "y": float(value[1])}


class FakeImage:
    def __init__(self):
        self.fills = []
        self.strokes = []

    # --- fills ---
    def fill_regions_info(self):
        return [{"index": i, "id": i + 1, "property": f["property"], "seed": f["seed"],
                 "color": f["color"], "source_layer_index": f["source"],
                 "based_on_all_layers": f["all"]}
                for i, f in enumerate(self.fills)]

    def fill_region_contains(self, index, point):
        # Programmable geometry: a region contains the samples its "covers"
        # list names, nothing when the list is empty, and EVERYTHING when it
        # is None (the "one region under the whole stroke" case).
        covers = self.fills[index].get("covers", ())
        return covers is None or tuple(point) in covers

    def add_fill_region(self, path, color, property="", seed=None,
                        source_layer_index=-1, based_on_all_layers=False):
        self.fills.append({"path": path, "color": color, "property": property,
                           "seed": _point(seed), "source": source_layer_index,
                           "all": based_on_all_layers, "covers": ()})
        return len(self.fills) - 1

    def set_fill_region(self, index, path=None, color=None, seed=None):
        if not (0 <= index < len(self.fills)):
            return False
        if path is not None:
            self.fills[index]["path"] = path
        if color is not None:
            self.fills[index]["color"] = color
        if seed is not None:
            self.fills[index]["seed"] = _point(seed)
        return True


class FakeScene:
    def __init__(self):
        self.columns = []        # {"id","type","parent","group"}
        self.images = {}
        self._frame = 0
        self._layer = -1
        self._next_id = 1
        self.trace = {}          # sample point -> commands (missing = default)
        self.bounds = {"x": 0.0, "y": 0.0, "width": 100.0, "height": 100.0}
        self.trace_calls = []
        self.removed = []        # (row, layer, index)

    # --- layers ---
    def add_layer(self, kind="vector", parent=0, group=0):
        self.columns.append({"id": self._next_id, "type": kind,
                             "parent": parent, "group": group})
        self._next_id += 1
        return len(self.columns) - 1

    def add_fill_layer(self, parent=0):
        return self.add_layer("fill", parent=parent)

    def layer_id_at(self, index):
        return self.columns[index]["id"] if 0 <= index < len(self.columns) else 0

    def group_id_for_layer(self, index, tag=""):
        if not (0 <= index < len(self.columns)):
            return 0
        return self.columns[index]["group"] if tag == ltp.UNIT_TAG else 0

    def current_frame(self):
        return self._frame

    def current_layer(self):
        return self._layer

    def set_current_layer(self, index):
        self._layer = index

    def get_structure(self):
        return {"frame_count": 1, "current_frame": self._frame,
                "current_layer": self._layer, "layer_count": len(self.columns),
                "layers": [{"index": i, "name": f"layer {i}", "type": c["type"],
                            "locked": False, "visible": True, "internal": False,
                            "parent_layer_id": c["parent"]}
                           for i, c in enumerate(self.columns)]}

    # --- content ---
    def image_at(self, _frame, layer_index, create=False, _kind="vector"):
        lid = self.layer_id_at(layer_index)
        if lid <= 0:
            return None
        if lid not in self.images:
            if not create:
                return None
            self.images[lid] = FakeImage()
        return self.images[lid]

    def cell_to_dict(self, layer_index, frame, to_poly=False, poly_step=4.0):
        image = self.image_at(frame, layer_index, True, "fill")
        return {"image": {"strokes": image.strokes}}

    def remove_stroke(self, row, layer_index, index):
        self.removed.append((row, layer_index, index))
        image = self.image_at(row, layer_index, True, "fill")
        if 0 <= index < len(image.strokes):
            image.strokes.pop(index)
            return True
        return False

    # --- geometry mechanisms ---
    def fill_boundary_bounds(self, _frame, layer_index=-1):
        return self.bounds

    def fill_boundary_path_at(self, frame, seed, bounds, layer_index=-1):
        self.trace_calls.append((frame, tuple(seed), tuple(bounds), layer_index))
        commands = self.trace.get(tuple(seed), ["region"])
        return None if commands is None else {"commands": commands}


def fresh_world():
    scene = FakeScene()
    UI.locked.clear()
    UI.fill_paint.clear()
    UI.commits.clear()
    UI.refreshes.clear()
    UI.windows.selected.clear()
    UI.windows.visible["tools"] = True
    ltp._scene_model = lambda view: scene
    ltp._CONVERT_GUARD["depth"] = 0
    ltp._STATE["fill_paint"] = False
    return scene


def layer_message(layer, view="main"):
    return {"event": "layerchange", "view": view, "tool": "pen", "base_tool": "pen",
            "property": "", "cell": {"row": 0, "layer": layer, "asset": 0, "frame_id": 1},
            "stroke": {}, "position": {"x": 0.0, "y": 0.0}, "delta": {"x": 0.0, "y": 0.0},
            "layer": layer, "layer_id": 0, "previous": -1}


def stroke_points(points):
    return [{"x": float(x), "y": float(y)} for x, y in points]


# 1) AUTOMAPPING member: the Mapping page comes up, nothing is locked and the
#    fill-paint remap stays off (the unit is not a fill layer).
scene = fresh_world()
member = scene.add_layer(group=7)
ltp._layer_changed(layer_message(member))
assert UI.windows.selected == [("tools", "mapping")], UI.windows.selected
assert UI.locked == [("main", ())], UI.locked
assert UI.fill_paint == [("main", False)]

# A unit's FILL member is still a unit member: membership is asked first.
scene = fresh_world()
fill_member = scene.add_fill_layer()
scene.columns[fill_member]["group"] = 7
ltp._layer_changed(layer_message(fill_member))
assert UI.windows.selected == [("tools", "mapping")]
assert UI.fill_paint == [("main", False)], "a unit member armed the region brush"

# 2) ORDINARY VECTOR layer: Painting page, locks cleared explicitly.
scene = fresh_world()
line = scene.add_layer()
ltp._layer_changed(layer_message(line))
assert UI.windows.selected == [("tools", "painting")]
assert UI.locked == [("main", ())], "the vector layer did not clear the locks"
assert UI.fill_paint == [("main", False)]

# 3) TRACKED fill child: the whole drawing set is locked, Arrow is not in the
#    list, and the pen is NOT remapped (its content is derived, not painted).
scene = fresh_world()
line = scene.add_layer()
child = scene.add_fill_layer(parent=scene.layer_id_at(line))
ltp._layer_changed(layer_message(child))
assert UI.locked == [("main", ("pen", "eraser", "fill", "connect", "transfer"))], UI.locked
assert "arrow" not in UI.locked[-1][1], "Arrow must stay available"
assert UI.fill_paint == [("main", False)]

# 4) INDEPENDENT fill layer: only connect is locked and the region brush arms.
scene = fresh_world()
independent = scene.add_fill_layer()
ltp._layer_changed(layer_message(independent))
assert UI.locked == [("main", ("connect",))], UI.locked
assert UI.fill_paint == [("main", True)]
assert ltp._STATE["fill_paint"] is True

# 5) Leaving the fill layer for a vector one clears BOTH: every layerchange
#    pushes the whole verdict, so no lock and no mode survives the move.
line = scene.add_layer()
ltp._layer_changed(layer_message(line))
assert UI.locked[-1] == ("main", ())
assert UI.fill_paint[-1] == ("main", False)
assert ltp._STATE["fill_paint"] is False

# 6) The TEXTURE board's focus is not this policy's business.
scene = fresh_world()
independent = scene.add_fill_layer()
ltp._layer_changed(layer_message(independent, view="child"))
assert UI.locked == [] and UI.fill_paint == [] and UI.windows.selected == []

# 7) A hidden Tools window is not re-paged (the locks still go out - they are
#    enforcement, not a view).
scene = fresh_world()
line = scene.add_layer()
UI.windows.visible["tools"] = False
ltp._layer_changed(layer_message(line))
assert UI.windows.selected == [], "a hidden Tools window was re-paged"
assert UI.locked == [("main", ())], "the locks were skipped with the window"

# 8) PEN CONVERSION: a stroke across two regions fills both, is removed, vetoes
#    the history entry and commits once under its own label.
scene = fresh_world()
line = scene.add_layer()
target = scene.add_fill_layer()
image = scene.image_at(0, target, True, "fill")
image.strokes.append({"id": 1, "property": "", "width": 3.0,
                      "color": {"r": 10, "g": 20, "b": 30, "a": 255},
                      "raw_points": stroke_points([(0.0, 0.0), (20.0, 0.0)])})
message = {"event": "linefinish", "view": "main", "tool": "pen", "base_tool": "pen",
           "property": "", "cell": {"row": 0, "layer": target, "asset": 0, "frame_id": 1},
           "stroke": {"index": 0}, "position": {}, "delta": {}}
ltp._stroke_finished(message["cell"], message["stroke"], message)
# Five samples along a 20px straight line at a 6px step: 0, 6, 12, 18 and the
# end. Nothing dedups here (this fake's regions cover nothing), so each one
# becomes its own region.
assert len(image.fills) == 5, f"expected one region per sample, got {len(image.fills)}"
assert [f["color"] for f in image.fills] == [{"r": 10, "g": 20, "b": 30, "a": 255}] * 5, \
    "the regions did not take the stroke's colour"
assert all(f["all"] and f["source"] == -1 for f in image.fills), \
    "a fill layer's regions must be bounded by ALL visible layers"
assert scene.removed == [(0, target, 0)], "the stroke was not removed"
assert image.strokes == [], "a stroke survived on a fill layer"
assert message["cancel_history"] is True, "the gesture's own history entry was not vetoed"
assert UI.commits == [("Region Fill", "main")], UI.commits
assert all(call[3] == -1 for call in scene.trace_calls), "a sample was traced against one column"

# The samples are spaced along the stroke, ends included.
seeds = [call[1] for call in scene.trace_calls]
assert seeds[0] == (0.0, 0.0) and seeds[-1] == (20.0, 0.0), seeds

# 9) DEDUP: a stroke that stays inside ONE region fills it once. The region a
#    sample already sits in is recolored, not stacked.
scene = fresh_world()
line = scene.add_layer()
target = scene.add_fill_layer()
image = scene.image_at(0, target, True, "fill")
image.strokes.append({"id": 1, "property": "", "width": 3.0,
                      "color": {"r": 1, "g": 2, "b": 3, "a": 255},
                      "raw_points": stroke_points([(0.0, 0.0), (7.0, 0.0), (14.0, 0.0)])})
original_add = FakeImage.add_fill_region


def add_and_cover(self, path, color, property="", seed=None,
                  source_layer_index=-1, based_on_all_layers=False):
    index = original_add(self, path, color, property, seed,
                         source_layer_index, based_on_all_layers)
    # This fake region covers every sample of the stroke, which is what a real
    # region traced from the first sample would do for the rest of them.
    self.fills[index]["covers"] = None
    return index


FakeImage.add_fill_region = add_and_cover
try:
    message = {"event": "linefinish", "view": "main", "tool": "pen", "base_tool": "pen",
               "property": "", "cell": {"row": 0, "layer": target}, "stroke": {"index": 0},
               "position": {}, "delta": {}}
    ltp._stroke_finished(message["cell"], message["stroke"], message)
finally:
    FakeImage.add_fill_region = original_add
assert len(image.fills) == 1, f"one region was stacked {len(image.fills)} times"
assert UI.commits == [("Region Fill", "main")]

# 10) A stroke that resolved NO region still loses the stroke and still vetoes
#     the history entry - a line must never survive on a fill layer - but there
#     is nothing to commit.
scene = fresh_world()
line = scene.add_layer()
target = scene.add_fill_layer()
image = scene.image_at(0, target, True, "fill")
image.strokes.append({"id": 1, "property": "", "width": 3.0,
                      "color": {"r": 1, "g": 2, "b": 3, "a": 255},
                      "raw_points": stroke_points([(0.0, 0.0), (20.0, 0.0)])})
scene.trace = {(0.0, 0.0): None, (6.0, 0.0): None, (12.0, 0.0): None,
               (18.0, 0.0): None, (20.0, 0.0): None}
message = {"event": "linefinish", "view": "main", "tool": "pen", "base_tool": "pen",
           "property": "", "cell": {"row": 0, "layer": target}, "stroke": {"index": 0},
           "position": {}, "delta": {}}
ltp._stroke_finished(message["cell"], message["stroke"], message)
assert image.fills == [], "an open-area stroke produced a region"
assert scene.removed == [(0, target, 0)], "the stroke survived a zero-region pass"
assert message["cancel_history"] is True
assert UI.commits == [], "a zero-region pass pushed a history entry"

# 11) A stroke on an ordinary VECTOR layer is left completely alone, and so is
#     one on a TRACKED child (its tools are locked, but a stale mode must not
#     eat a stroke that got through anyway).
scene = fresh_world()
line = scene.add_layer()
image = scene.image_at(0, line, True, "vector")
image.strokes.append({"id": 1, "property": "", "width": 3.0,
                      "color": {"r": 0, "g": 0, "b": 0, "a": 255},
                      "raw_points": stroke_points([(0.0, 0.0), (20.0, 0.0)])})
message = {"event": "linefinish", "view": "main", "cell": {"row": 0, "layer": line},
           "stroke": {"index": 0}, "property": ""}
ltp._stroke_finished(message["cell"], message["stroke"], message)
assert scene.removed == [] and image.fills == []
assert "cancel_history" not in message, "an ordinary stroke was vetoed"

scene = fresh_world()
line = scene.add_layer()
child = scene.add_fill_layer(parent=scene.layer_id_at(line))
image = scene.image_at(0, child, True, "fill")
image.strokes.append({"id": 1, "property": "", "width": 3.0,
                      "color": {"r": 0, "g": 0, "b": 0, "a": 255},
                      "raw_points": stroke_points([(0.0, 0.0), (20.0, 0.0)])})
message = {"event": "linefinish", "view": "main", "cell": {"row": 0, "layer": child},
           "stroke": {"index": 0}, "property": ""}
ltp._stroke_finished(message["cell"], message["stroke"], message)
assert image.fills == [], "a tracked child was region-painted"
assert scene.removed == []

# 12) RE-ENTRANCY: a conversion that re-enters while one is running is a no-op.
scene = fresh_world()
target = scene.add_fill_layer()
image = scene.image_at(0, target, True, "fill")
image.strokes.append({"id": 1, "property": "", "width": 3.0,
                      "color": {"r": 1, "g": 2, "b": 3, "a": 255},
                      "raw_points": stroke_points([(0.0, 0.0), (20.0, 0.0)])})
ltp._CONVERT_GUARD["depth"] = 1
message = {"event": "linefinish", "view": "main", "cell": {"row": 0, "layer": target},
           "stroke": {"index": 0}, "property": ""}
ltp._stroke_finished(message["cell"], message["stroke"], message)
assert scene.removed == [] and image.fills == [], "the guard did not stop a re-entrant pass"
assert "cancel_history" not in message
ltp._CONVERT_GUARD["depth"] = 0
ltp._stroke_finished(message["cell"], message["stroke"], message)
assert scene.removed == [(0, target, 0)], "the guard stayed armed"

# 13) A build without the new bindings degrades quietly instead of taking the
#     layerchange down with it.
scene = fresh_world()
line = scene.add_layer()
missing = types.SimpleNamespace(ui=types.SimpleNamespace(
    windows=UI.windows, refresh=UI.refresh, history_commit=UI.history_commit))
real_animean = ltp._animean
ltp._animean = lambda: missing
try:
    ltp._layer_changed(layer_message(line))
finally:
    ltp._animean = real_animean
assert UI.windows.selected == [("tools", "painting")], \
    "a missing lock binding cost the page switch"

# 14) The sampler keeps both ends and spaces the middle.
samples = ltp._sample_points([(0.0, 0.0), (3.0, 0.0), (9.0, 0.0), (10.0, 0.0)], 6.0)
assert samples[0] == (0.0, 0.0) and samples[-1] == (10.0, 0.0)
# The middle sample is INTERPOLATED at 6px of arc length, not one of the
# captured points - which is what makes a two-point straight line work.
assert (6.0, 0.0) in samples, samples
assert ltp._sample_points([], 6.0) == []
assert ltp._sample_points([(4.0, 4.0)], 6.0) == [(4.0, 4.0)]

# 15) The hooks are registered for the events the policy needs.
assert any(h["function"] is ltp._layer_changed and h["events"] == {"layerchange"}
           for h in python_hooks._HOOKS)
assert any(h["function"] is ltp._stroke_finished and h["events"] == {"linefinish"}
           for h in python_hooks._HOOKS)

print("t_layer_policy: ok")
