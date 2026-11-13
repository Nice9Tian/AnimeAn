"""A guide line is a gesture, never artwork: drawing one leaves fills whole.

User report 2026-11-13. On the texture board, fill a few cells, then draw the
H and V centre lines through them: the fills came out cut in half along the
axis. The other order - axes first, fill second - was always fine.

Why. A guide tool draws through the pen, so its line sits in the current layer
as an ordinary stroke until auto_mapping._capture_mapping_item takes it out.
fill_tool re-traces the layer's tracked fills on every linefinish, and it ran
BEFORE the capture: python_hooks.set_hook re-appends a hook that registers
again, and the capture registers again on every tool arming and every
tools_json. So the re-trace used the axis as a wall, and nothing traced again
once the axis was gone. The capture now carries
python_hooks.CAPTURE_HOOK_ORDER and runs first.

The same order had a second face: on an independent fill layer the pen is a
region brush (layer_tool_policy), and its conversion took an axis for a brush
stroke - regions were painted in the guide's colour and no axis was made.

Running first is half of it. The hooks behind the capture still get the
ORIGINAL message, and by then cell.layer can name another layer: a guide drawn
outside any unit makes one, whose layer goes in at the front. With the order
alone the re-trace ran for that bystander layer, and for refused gestures too
(a change with no history entry). So the capture also sets
message["captured"], and the re-trace and the region brush leave such a
message alone: a guide re-traces nothing, on any layer (cross-check, same day).

Real modules, real registration, real dispatch; only the scene and the ui are
fakes. Creating a unit for a guide drawn outside one is t_units' business.
"""
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))


# --- fake bindings ----------------------------------------------------------

class _Panel:
    def refresh(self):
        pass


class FakeUi:
    def __init__(self):
        self.commits = []
        self.widget = _Panel()
        self.layer = _Panel()
        self.main = _Panel()
        self.children = _Panel()

    def set_hook_events(self, events):
        pass

    def refresh(self):
        pass

    def refresh_tool_options(self):
        pass

    def set_overlay(self, view, items):
        pass

    def set_draw_color(self, color):
        pass

    def history_commit(self, label, view=""):
        self.commits.append((label, view))


class FakeVectorLogic:
    """Mapping Area finds its region through vectorlogic, not through the fill
    boundary bindings. No region here: the click is refused, which is all the
    area needs to do in this suite."""

    @staticmethod
    def vector_region_path_at(seed, segments, bounds):
        return {"commands": []}


_stub = types.ModuleType("animean_python")
_stub.ui = FakeUi()
sys.modules.setdefault("animean_python", _stub)

# The order initalize.py imports them in: the tools list (and with it
# auto_mapping) before the fill policy and the layer policy.
import python_hooks  # noqa: E402
import extra_tools  # noqa: E402
import auto_mapping as am  # noqa: E402
import fill_tool as ft  # noqa: E402
import layer_tool_policy as ltp  # noqa: E402
import edit_tool as et  # noqa: E402


def _point(value):
    if value is None:
        return {"x": 0.0, "y": 0.0}
    if isinstance(value, dict):
        return {"x": float(value.get("x", 0.0)), "y": float(value.get("y", 0.0))}
    return {"x": float(value[0]), "y": float(value[1])}


class FakeImage:
    def __init__(self):
        self.strokes = []   # stroke dicts, the shape cell_to_dict reports
        self.fills = []

    def fill_regions_info(self):
        return [{"index": i, "id": i + 1, "property": f["property"],
                 "seed": f["seed"], "color": f["color"],
                 "source_layer_index": f["source"],
                 "based_on_all_layers": f["all"]}
                for i, f in enumerate(self.fills)]

    def fill_region_contains(self, index, point):
        return False

    def add_fill_region(self, path, color, property="", seed=None,
                        source_layer_index=-1, based_on_all_layers=False):
        self.fills.append({"path": path, "color": color, "property": property,
                           "seed": _point(seed), "source": source_layer_index,
                           "all": based_on_all_layers})
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

    def remove_fill_area(self, index):
        self.fills.pop(index)
        return True


class FakeCell:
    asset_index = 0
    frame_id = 1


class FakeScene:
    """Layers, strokes, fill regions and scriptData: what a fill click, a
    tracked re-trace, a region-brush conversion and a guide capture touch.

    Geometry is symbolic. A trace answers with the WALLS it was bounded by -
    the properties of the strokes in its scope at that moment - so a region
    traced while a guide stroke was still in the layer says so in its path.
    """

    def __init__(self):
        self.columns = []      # {"id","type","parent","visible"}
        self.images = {}       # column id -> FakeImage
        self._frame = 0
        self._layer = -1
        self._next_id = 1
        self._script = ""
        self.trace_walls = []  # the wall set of every trace, in call order
        # walls -> True when they close a region around the seed.
        self.closed = lambda walls: True

    # --- selection ---
    def current_frame(self):
        return self._frame

    def current_layer(self):
        return self._layer

    def set_current_layer(self, index):
        self._layer = index

    # --- layers ---
    def add_layer(self, kind="vector", front=False):
        column = {"id": self._next_id, "type": kind, "parent": 0, "visible": True}
        self._next_id += 1
        self.images[column["id"]] = FakeImage()
        if front:
            # What _create_mapped_layer does around move_layer(index, 0): every
            # layer moves down one, and the fill regions bounded by one column
            # (remap_fill_source_layers_after_move) and the selection follow.
            self.columns.insert(0, column)
            for image in self.images.values():
                for fill in image.fills:
                    if fill["source"] >= 0:
                        fill["source"] += 1
            if self._layer >= 0:
                self._layer += 1
            return 0
        self.columns.append(column)
        return len(self.columns) - 1

    def add_fill_layer(self):
        return self.add_layer("fill")

    def layer_id_at(self, index):
        return self.columns[index]["id"] if 0 <= index < len(self.columns) else 0

    def layer_index_for_id(self, lid):
        for index, column in enumerate(self.columns):
            if column["id"] == lid:
                return index
        return -1

    def set_layer_parent_id(self, index, parent_id):
        self.columns[index]["parent"] = parent_id if parent_id > 0 else 0

    def group_id_for_layer(self, index, tag=""):
        return 0

    def get_structure(self):
        return {"frame_count": 1, "current_frame": self._frame,
                "current_layer": self._layer, "layer_count": len(self.columns),
                "layers": [{"index": i, "name": f"layer {c['id']}", "type": c["type"],
                            "locked": False, "visible": c["visible"],
                            "internal": False, "parent_layer_id": c["parent"]}
                           for i, c in enumerate(self.columns)]}

    # --- content ---
    def image(self, index):
        return self.images[self.layer_id_at(index)]

    def image_at(self, _frame, layer_index, create=False, _kind="vector"):
        return self.images.get(self.layer_id_at(layer_index))

    def cell_at(self, _frame, _layer):
        return FakeCell()

    def cell_to_dict(self, layer_index, _row, _to_poly=True, _step=4.0):
        image = self.images.get(self.layer_id_at(layer_index))
        strokes = [dict(stroke) for stroke in image.strokes] if image else []
        return {"image": {"strokes": strokes, "fills": []}}

    def add_stroke(self, layer_index, points, prop=""):
        """What C++ has done by the time linefinish is dispatched: the line is
        in the layer. Returns its index, the message's stroke index."""
        dots = [{"x": float(x), "y": float(y)} for x, y in points]
        strokes = self.image(layer_index).strokes
        strokes.append({"property": prop, "width": 3.0,
                        "color": {"r": 0, "g": 0, "b": 255, "a": 255},
                        "polylines": [dots], "raw_points": dots})
        return len(strokes) - 1

    def remove_stroke(self, _row, layer_index, index):
        self.image(layer_index).strokes.pop(index)

    def properties(self, layer_index):
        return [stroke["property"] for stroke in self.image(layer_index).strokes]

    # --- fill boundary mechanisms ---
    def _walls(self, layer_index):
        walls = []
        for index, column in enumerate(self.columns):
            if layer_index >= 0 and index != layer_index:
                continue
            if column["type"] == "fill" or not column["visible"]:
                continue
            walls.extend(self.properties(index))
        return tuple(walls)

    def fill_boundary_bounds(self, _frame, layer_index=-1):
        if not self._walls(layer_index):
            return None
        return {"x": 0.0, "y": 0.0, "width": 400.0, "height": 400.0}

    def fill_boundary_path_at(self, _frame, _seed, _bounds, layer_index=-1):
        walls = self._walls(layer_index)
        self.trace_walls.append(walls)
        if not walls or not self.closed(walls):
            return None
        return {"commands": ["region bounded by", *walls]}

    # --- scriptData ---
    def script_data(self):
        return self._script

    def set_script_data(self, value):
        self._script = value

    def canvas_size(self):
        return (800, 600)


GUIDES = (am.H_PROPERTY, am.V_PROPERTY, am.MAPPING_AREA_PROPERTY,
          am.ADDITIONAL_PROPERTY, am.PROXY_PROPERTY)


def fresh_world():
    scenes = {"main": FakeScene(), "child": FakeScene()}
    ui = FakeUi()
    fake = types.SimpleNamespace(ui=ui, vectorlogic=FakeVectorLogic())
    for module in (am, ft, ltp):
        module._scene_model = lambda view, _scenes=scenes: _scenes[view]
        module._animean = lambda _fake=fake: _fake
    ft._RETRACE_GUARD["depth"] = 0
    ltp._CONVERT_GUARD["depth"] = 0
    ft.board_effects.fill_spread = lambda view, seed, path: None
    am._MAPPING_ASSETS.clear()
    am._UNIT_ASSETS.clear()
    am._UNIT_META.clear()
    am._ACTIVE_UNIT["id"] = None
    am._RUN_GUARD["depth"] = 0
    am._invalidate_grid_cache()
    # No unit comes into being here: the guide goes on into the scratch set,
    # as it does on a build that cannot group layers.
    am._create_unit = lambda *args, **kwargs: None
    return scenes, ui


def fill_click(view, seed):
    """A Fill click, through the hooks, the way C++ offers it."""
    message = {"event": "fillrequest", "view": view, "tool": "fill",
               "base_tool": "fill", "property": "", "cell": {}, "stroke": {},
               "position": {"x": seed[0], "y": seed[1]},
               "delta": {"x": 0.0, "y": 0.0}, "fill_scope": "current",
               "color": {"r": 255, "g": 179, "b": 0, "a": 255},
               "bounds": {"x": 0.0, "y": 0.0, "width": 400.0, "height": 400.0}}
    python_hooks.dispatch(message)
    assert message.get("handled"), "the fill policy did not take the click"


def draw(scene, view, layer, points, prop="", tool="pen"):
    """A finished line: C++ has put the stroke in the layer, then dispatches."""
    index = scene.add_stroke(layer, points, prop)
    message = {"event": "linefinish", "view": view, "tool": tool,
               "base_tool": "pen", "property": prop,
               "cell": {"row": 0, "layer": layer, "asset": 0, "frame_id": 1},
               "stroke": {"index": index, "point_count": len(points)},
               "position": {"x": 0.0, "y": 0.0}, "delta": {"x": 0.0, "y": 0.0}}
    python_hooks.dispatch(message)
    return message


def linefinish_order(prop, tool="extra"):
    """The hooks a linefinish reaches, in the order dispatch calls them."""
    message = {"event": "linefinish", "tool": tool, "property": prop}
    return [hook["function"] for hook in python_hooks._HOOKS
            if python_hooks._matches(hook, message)]


def historyrestore_order():
    """The hooks a historyrestore reaches, in dispatch order."""
    message = {"event": "historyrestore", "view": "main"}
    return [hook["function"] for hook in python_hooks._HOOKS
            if python_hooks._matches(hook, message)]


H_LINE = [(40.0, 140.0), (200.0, 140.0), (360.0, 140.0)]
V_LINE = [(240.0, 40.0), (240.0, 200.0), (240.0, 360.0)]
GRID_LINE = [(80.0, 100.0), (420.0, 100.0)]


# 1) THE ORDER, as the app registers it. The tools list is built (C++ asks for
#    it at startup and that registers the capture), then tools are armed, and
#    other modules register again whenever they like. Through all of it:
#    - on a guide's linefinish (tool="extra"), auto_mapping's capture is first
#      and the pen-only region brush is not matched at all;
#    - on a native pen linefinish (tool="pen"), layer_tool_policy's region
#      brush runs ahead of default-order observers like fill_tool;
#    - on historyrestore, edit_tool runs AFTER auto_mapping even when mapping
#      tools re-register auto_mapping's hooks.
fresh_world()
extra_tools.tools_json()
for prop in GUIDES:
    order = linefinish_order(prop)
    assert order[0] is am._capture_mapping_item, \
        f"{prop}: the capture runs after {order[0].__module__}.{order[0].__qualname__}"
    assert ft._topology_changed in order
    assert ltp._stroke_finished not in order, \
        "region brush matched a non-pen guide stroke"
pen_order = linefinish_order("", tool="pen")
assert pen_order[0] is ltp._stroke_finished, pen_order
assert pen_order.index(ltp._stroke_finished) < pen_order.index(ft._topology_changed)
restore_order = historyrestore_order()
assert restore_order.index(am._history_restored) < restore_order.index(et._history_restored)
for arm in (am.activate_center_line_tool, am.activate_mapping_area_tool,
            am.activate_additional_line_tool, am.activate_proxy_contour_tool):
    arm()
    ft.register_hooks()        # an observer registering again goes last...
    ltp.register_hooks()       # ...while capture-ordered hooks stay ahead
    order = linefinish_order(am.H_PROPERTY)
    assert order[0] is am._capture_mapping_item, "re-arming moved the capture"
    assert ltp._stroke_finished not in order
    pen_order = linefinish_order("", tool="pen")
    assert pen_order[0] is ltp._stroke_finished, \
        "re-registering observers moved the region brush behind them"
    assert pen_order.index(ltp._stroke_finished) < pen_order.index(ft._topology_changed)
    extra_tools.tools_json()   # re-registers auto_mapping._history_restored at default 50
    assert linefinish_order(am.H_PROPERTY)[0] is am._capture_mapping_item
    restore_order = historyrestore_order()
    assert restore_order.index(am._history_restored) < restore_order.index(et._history_restored), \
        "re-arming a mapping tool moved auto_mapping._history_restored after edit_tool._history_restored"
print("1) capture hooks and edit_tool's restore hook keep their explicit order across re-registrations")

# 2) set_hook's order: lower first, ties in the order of registration, a
#    function registered again goes last among ITS order, and hooks without an
#    order keep the plain append they always had.
calls = []


def _early(message):
    calls.append("early")


def _plain_a(message):
    calls.append("a")


def _plain_b(message):
    calls.append("b")


def _late(message):
    calls.append("late")


try:
    python_hooks.set_hook(_plain_a, pad=True)
    python_hooks.set_hook(_late, pad=True, order=90)
    python_hooks.set_hook(_early, pad=True, order=python_hooks.CAPTURE_HOOK_ORDER)
    python_hooks.set_hook(_plain_b, pad=True)
    python_hooks.dispatch({"event": "pad"})
    assert calls == ["early", "a", "b", "late"], calls
    python_hooks.set_hook(_plain_a, pad=True)       # again: last of the defaults
    python_hooks.set_hook(_early, pad=True, order=python_hooks.CAPTURE_HOOK_ORDER)
    calls.clear()
    python_hooks.dispatch({"event": "pad"})
    assert calls == ["early", "b", "a", "late"], calls
    assert python_hooks.DEFAULT_HOOK_ORDER > python_hooks.CAPTURE_HOOK_ORDER
    # A bool is not an order (True would sort as 1): it falls back to the default.
    python_hooks.set_hook(_early, pad=True, order=True)
    calls.clear()
    python_hooks.dispatch({"event": "pad"})
    assert calls == ["b", "a", "early", "late"], calls
    # Nor is a NaN: it compares false with everything, so it would sit
    # wherever it fell and every hook registered after it would be placed
    # against a table that is no longer sorted.
    python_hooks.set_hook(_plain_b, pad=True, order=float("nan"))
    python_hooks.set_hook(_early, pad=True, order=python_hooks.CAPTURE_HOOK_ORDER)
    orders = [hook["order"] for hook in python_hooks._HOOKS if "pad" in hook["events"]]
    assert orders == sorted(orders) and all(order == order for order in orders), orders
    calls.clear()
    python_hooks.dispatch({"event": "pad"})
    assert calls == ["early", "a", "b", "late"], calls
finally:
    for function in (_early, _plain_a, _plain_b, _late):
        python_hooks.del_hook(function)
print("2) set_hook order: lower first, ties by registration, default unchanged")

# 3) THE REPORT. Texture board: a grid cell is filled, then the H and the V
#    centre line are drawn through it. The fill is never traced against an
#    axis, so it keeps the region it had; each axis ends up a guide, out of
#    the layer, and its stroke commit is not vetoed.
scenes, ui = fresh_world()
child = scenes["child"]
line = child.add_layer()
child.set_current_layer(line)
for _ in range(4):
    assert not draw(child, "child", line, GRID_LINE).get("cancel_history")
fill_click("child", (150.0, 150.0))
fill_layer = child.layer_index_for_id(
    next(c["id"] for c in child.columns if c["type"] == "fill"))
assert child.columns[fill_layer]["parent"] == child.layer_id_at(line), \
    "the fill does not track the line layer: this test would prove nothing"
assert child.current_layer() == line
region = child.image(fill_layer).fills[0]
whole = list(region["path"])
assert whole == ["region bounded by", "", "", "", ""]

for prop, points in ((am.H_PROPERTY, H_LINE), (am.V_PROPERTY, V_LINE)):
    child.trace_walls.clear()
    message = draw(child, "child", line, points, prop, tool="extra")
    assert region["path"] == whole, f"the fill was re-traced against the {prop}: {region['path']}"
    assert message.get("captured"), "the capture did not say the stroke is gone"
    assert child.trace_walls == [], \
        f"a guide is no line-art change, yet fills were traced: {child.trace_walls}"
    assert child.properties(line) == ["", "", "", ""], "the guide stroke stayed in the layer"
    assert am._assets_for("child")[prop]["points"] == points, "the guide was not installed"
    assert not message.get("cancel_history"), "the guide's own commit was vetoed"
print("3) fill first, axes second: the fill keeps its region, both axes are guides")

# 3b) ...and tracking is as alive as before: a real pen line through the cell
#     does re-trace it, and so does erasing that line again.
pen_message = draw(child, "child", line, [(80.0, 160.0), (220.0, 160.0)])
assert region["path"] == ["region bounded by", "", "", "", "", ""], region["path"]
assert not pen_message.get("cancel_history")
child.remove_stroke(0, line, 4)
python_hooks.dispatch({"event": "erasefinish", "view": "child", "tool": "eraser",
                       "property": "", "cell": {"row": 0, "layer": line},
                       "stroke": {}})
assert region["path"] == whole
print("3b) a pen line still re-traces the fill it crosses")

# 4) THE OTHER GUIDE TOOLS go through the same capture: Additional Line and
#    Free Out Line strokes, and the Mapping Area click, never reach a trace
#    either. (Whether the tool then accepts the line is its own business -
#    an additional line needs both boards' axes - the stroke is gone either way.)
for prop, points in ((am.ADDITIONAL_PROPERTY, [(120.0, 120.0), (180.0, 190.0)]),
                     (am.PROXY_PROPERTY, [(90.0, 90.0), (310.0, 90.0), (310.0, 310.0), (90.0, 310.0)]),
                     (am.MAPPING_AREA_PROPERTY, [(150.0, 150.0)])):
    child.trace_walls.clear()
    message = draw(child, "child", line, points, prop, tool="extra")
    assert region["path"] == whole, f"{prop}: the fill changed to {region['path']}"
    assert message.get("captured") and child.trace_walls == [], \
        f"{prop}: fills were traced for a guide gesture: {child.trace_walls}"
    assert child.properties(line) == ["", "", "", ""], f"the {prop} stroke stayed in the layer"
# The area click found no region here and was REFUSED: its commit is vetoed,
# so a trace in its wake would have changed fills with no history entry to
# hold the change. Refused or accepted, the stroke is gone and nothing follows.
assert prop == am.MAPPING_AREA_PROPERTY and message.get("cancel_history")
print("4) additional line, contour and area click: out of the layer, nothing traced")

# 5) WHAT A SECOND TRACE COULD NOT HAVE MENDED. A shape was filled while it
#    was closed, then opened again (the closing line erased): its seed no
#    longer resolves, and tracking keeps the region as drawn. An axis across
#    the opening closes it for as long as the stroke sits in the layer - a
#    trace in that moment REPLACES the kept region with the axis-bounded one,
#    and no later trace brings the old one back, because the seed is open
#    again. So the capture has to come before the first trace, not after it.
scenes, ui = fresh_world()
child = scenes["child"]
line = child.add_layer()
child.set_current_layer(line)
child.closed = lambda walls: "lid" in walls or am.H_PROPERTY in walls
draw(child, "child", line, GRID_LINE, "cup")
draw(child, "child", line, GRID_LINE, "lid")
fill_click("child", (150.0, 150.0))
fill_layer = child.layer_index_for_id(
    next(c["id"] for c in child.columns if c["type"] == "fill"))
region = child.image(fill_layer).fills[0]
kept = list(region["path"])
assert kept == ["region bounded by", "cup", "lid"]
child.remove_stroke(0, line, 1)                       # erase the lid
python_hooks.dispatch({"event": "erasefinish", "view": "child", "tool": "eraser",
                       "property": "", "cell": {"row": 0, "layer": line},
                       "stroke": {}})
assert region["path"] == kept, "an open seed must keep its region"
child.trace_walls.clear()
draw(child, "child", line, H_LINE, am.H_PROPERTY, tool="extra")
assert region["path"] == kept, f"the axis closed the open shape: {region['path']}"
assert child.trace_walls == []
print("5) an axis across an open shape does not re-close and overwrite its fill")

# 6) MAIN BOARD, an independent fill layer is current. There the pen is a
#    region brush: its finished stroke becomes fills of the regions it
#    crossed. An axis is not a brush stroke - it becomes the axis, paints
#    nothing and commits no "Region Fill".
scenes, ui = fresh_world()
main = scenes["main"]
line = main.add_layer()
main.set_current_layer(line)
for _ in range(4):
    draw(main, "main", line, GRID_LINE)
brush_layer = main.add_fill_layer()                    # New Fill Layer: no parent
main.set_current_layer(brush_layer)
message = draw(main, "main", brush_layer, H_LINE, am.H_PROPERTY, tool="extra")
assert (am._assets_for("main").get(am.H_PROPERTY) or {}).get("points") == H_LINE, \
    "an axis drawn on an independent fill layer made no axis"
assert main.image(brush_layer).fills == [], \
    f"the axis painted region fills: {main.image(brush_layer).fills}"
assert main.properties(brush_layer) == []
assert ("Region Fill", "main") not in ui.commits
assert not message.get("cancel_history")
# The region brush itself is untouched: a pen stroke there still paints, runs
# ahead of default-order observers, and marks message["captured"] = True once
# the brush stroke is removed from the fill layer (while leaving ordinary line
# strokes on vector layers uncaptured).
observer_seen = []


def _brush_observer(message):
    layer_idx = message["cell"]["layer"]
    observer_seen.append((
        bool(message.get("captured")),
        len(main.image(layer_idx).strokes),
    ))


try:
    python_hooks.set_hook(_brush_observer, linefinish=True)
    brushed = draw(main, "main", brush_layer, [(120.0, 150.0), (180.0, 150.0)])
    normal_line = draw(main, "main", line, [(90.0, 110.0), (190.0, 110.0)])
finally:
    python_hooks._HOOKS[:] = [h for h in python_hooks._HOOKS
                              if h["function"] is not _brush_observer]
assert main.image(brush_layer).fills, "the pen stopped painting regions on a fill layer"
assert main.properties(brush_layer) == []
assert brushed.get("captured") is True and brushed.get("cancel_history") is True
assert not normal_line.get("captured"), "ordinary vector-layer stroke was marked captured"
assert observer_seen == [(True, 0), (False, 5)], \
    f"default-order observer saw wrong (captured, stroke_count): {observer_seen}"
assert ("Region Fill", "main") in ui.commits
# And a captured message is never read as a brush stroke, whatever its indices
# name by then: here they name a stroke that IS on the fill layer.
stray = main.add_stroke(brush_layer, [(120.0, 150.0), (180.0, 150.0)])
painted = len(main.image(brush_layer).fills)
ltp._stroke_finished({"row": 0, "layer": brush_layer}, {"index": stray},
                     {"event": "linefinish", "view": "main", "captured": True})
assert main.properties(brush_layer) == [""], "a captured message was converted as a brush stroke"
assert len(main.image(brush_layer).fills) == painted
print("6) on an independent fill layer an axis is an axis; the pen still paints regions")

# 7) THE CAPTURE MOVES LAYERS, AND THE MESSAGE DOES NOT FOLLOW. A guide drawn
#    on the main board outside any unit makes one, whose layer goes in at the
#    front: cell.layer in the message then names the layer that used to sit
#    one above. Here that is line layer A, a bystander: the axis is drawn on
#    B. A's tracked fill layer holds an all-layers region that a line on B
#    (not its parent) has since crossed - out of date by design, and none of
#    an axis's business. Found by cross-check on the real backend: the
#    re-trace ran for A and cut that region along B's line.
scenes, ui = fresh_world()
main = scenes["main"]
layer_a = main.add_layer()
layer_b = main.add_layer()
id_a, id_b = main.layer_id_at(layer_a), main.layer_id_at(layer_b)
main.set_current_layer(layer_a)
for _ in range(4):
    draw(main, "main", layer_a, GRID_LINE, "a")
fill_click("main", (150.0, 150.0))                     # Current: follows A
fill_layer = main.layer_index_for_id(
    next(c["id"] for c in main.columns if c["type"] == "fill"))
assert main.columns[fill_layer]["parent"] == id_a
main.set_current_layer(fill_layer)
fill_click("main", (250.0, 150.0))                     # on the fill layer: All
current_region, all_region = main.image(fill_layer).fills
assert (current_region["all"], all_region["all"]) == (False, True)
main.set_current_layer(layer_b)
draw(main, "main", layer_b, GRID_LINE, "b")            # B is nobody's parent
assert "b" not in all_region["path"], "this test needs a region that is out of date"
as_drawn = [list(current_region["path"]), list(all_region["path"])]


def _unit_layer_goes_in_front(*args, **kwargs):
    main.add_layer(front=True)


am._create_unit = _unit_layer_goes_in_front
main.trace_walls.clear()
message = draw(main, "main", layer_b, H_LINE, am.H_PROPERTY, tool="extra")
assert len(main.columns) == 4 and main.columns[0]["type"] == "vector"
assert main.layer_id_at(message["cell"]["layer"]) == id_a, \
    "the message was meant to name the bystander by now: this test would prove nothing"
assert message.get("captured") and not message.get("cancel_history")
assert main.trace_walls == [], f"the axis re-traced a bystander's fills: {main.trace_walls}"
assert [current_region["path"], all_region["path"]] == as_drawn
assert am.H_PROPERTY not in main.properties(main.layer_index_for_id(id_b))
assert am._assets_for("main")[am.H_PROPERTY]["points"] == H_LINE
print("7) an axis on another layer re-traces nobody's fills, though the indices moved")

# 7b) ...and A's fills still follow A, at its new index: a pen line there
#     re-traces both regions, each against its own scope.
draw(main, "main", main.layer_index_for_id(id_a), GRID_LINE, "a")
assert current_region["path"] == ["region bounded by", "a", "a", "a", "a", "a"], current_region["path"]
assert all_region["path"] == ["region bounded by", "a", "a", "a", "a", "a", "b"], all_region["path"]
print("7b) the fills still follow their own line layer after the move")

print("t_guide_capture_order: ok")
