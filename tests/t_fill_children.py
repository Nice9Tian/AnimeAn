"""Fill child layers: a fill layer that TRACKS the line layer it was traced
from (round-2 G4). Covers the policy half only - auto-parenting at fill time,
the topology re-trace, the self-echo guard and the "To Independent Layer"
release. The C++ half (AnimeColumn::parentLayerId, its persistence) is
covered by tests/projectio_tests.cpp."""
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))

_stub = types.ModuleType("animean_python")
_stub.ui = types.SimpleNamespace(set_hook_events=lambda events: None)
sys.modules.setdefault("animean_python", _stub)

import python_hooks  # noqa: E402
import fill_tool as ft  # noqa: E402


# --- fakes ------------------------------------------------------------------

def _point(value):
    """Seeds go in as a pair and come back out as a dict, exactly the way the
    C++ binding stores a QPointF and reports it through fill_regions_info."""
    if value is None:
        return {"x": 0.0, "y": 0.0}
    if isinstance(value, dict):
        return {"x": float(value.get("x", 0.0)), "y": float(value.get("y", 0.0))}
    return {"x": float(value[0]), "y": float(value[1])}


class FakeImage:
    def __init__(self):
        self.fills = []   # [{"property","seed","color","source","all","path"}]

    def fill_regions_info(self):
        return [{"index": i, "id": i + 1, "property": f["property"],
                 "seed": f["seed"], "color": f["color"],
                 "source_layer_index": f["source"],
                 "based_on_all_layers": f["all"]}
                for i, f in enumerate(self.fills)]

    def fill_region_contains(self, index, point):
        return bool(self.fills[index].get("contains"))

    def add_fill_region(self, path, color, property="", seed=None,
                        source_layer_index=-1, based_on_all_layers=False):
        self.fills.append({"path": path, "color": color, "property": property,
                           "seed": _point(seed), "source": source_layer_index,
                           "all": based_on_all_layers, "contains": False})
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
    """The layer/parent/fill-region subset fill_tool touches."""

    def __init__(self):
        self.columns = []          # {"id","name","type","locked","visible","parent"}
        self.images = {}           # column id -> FakeImage
        self._frame = 0
        self._layer = -1
        self._next_id = 1
        # Programmable geometry: what a trace against a given scope returns.
        self.trace = {}            # layer_index -> commands or None
        self.bounds = {}           # layer_index -> {"x","y","width","height"} or None
        self.trace_calls = []      # (frame, seed, bounds, layer_index)

    # --- selection ---
    def current_frame(self):
        return self._frame

    def current_layer(self):
        return self._layer

    def set_current_layer(self, index):
        self._layer = index

    # --- layers ---
    def add_layer(self, kind="vector"):
        self.columns.append({"id": self._next_id, "name": f"layer {self._next_id}",
                             "type": kind, "locked": False, "visible": True,
                             "parent": 0})
        self._next_id += 1
        return len(self.columns) - 1

    def add_fill_layer(self):
        return self.add_layer("fill")

    def layer_id_at(self, index):
        if 0 <= index < len(self.columns):
            return self.columns[index]["id"]
        return 0

    def layer_index_for_id(self, lid):
        for index, column in enumerate(self.columns):
            if column["id"] == lid:
                return index
        return -1

    def layer_parent_id(self, index):
        return self.columns[index]["parent"] if 0 <= index < len(self.columns) else 0

    def set_layer_parent_id(self, index, parent_id):
        if not (0 <= index < len(self.columns)):
            return
        if parent_id > 0 and parent_id == self.columns[index]["id"]:
            return
        self.columns[index]["parent"] = parent_id if parent_id > 0 else 0

    def child_layer_indices(self, index):
        pid = self.layer_id_at(index)
        return [i for i, c in enumerate(self.columns) if pid and c["parent"] == pid]

    def get_structure(self):
        return {
            "frame_count": 1,
            "current_frame": self._frame,
            "current_layer": self._layer,
            "layer_count": len(self.columns),
            "layers": [{"index": i, "name": c["name"], "type": c["type"],
                        "locked": c["locked"], "visible": c["visible"],
                        "internal": False, "parent_layer_id": c["parent"]}
                       for i, c in enumerate(self.columns)],
        }

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

    def cell_at(self, _frame, _layer):
        return FakeCell()

    # --- geometry mechanisms ---
    def fill_boundary_bounds(self, _frame, layer_index=-1):
        return self.bounds.get(layer_index, {"x": 0.0, "y": 0.0,
                                             "width": 100.0, "height": 100.0})

    def fill_boundary_path_at(self, frame, seed, bounds, layer_index=-1):
        self.trace_calls.append((frame, tuple(seed), tuple(bounds), layer_index))
        commands = self.trace.get(layer_index, ["default"])
        return None if commands is None else {"commands": commands}


class _Refreshable:
    def __init__(self, log, name):
        self._log = log
        self._name = name

    def refresh(self):
        self._log.append(self._name)


class FakeUi:
    def __init__(self):
        self.commits = []
        self.refreshes = []
        self.widget = _Refreshable(self.refreshes, "widget")
        self.layer = _Refreshable(self.refreshes, "layer")

    def refresh(self):
        self.refreshes.append("all")

    def history_commit(self, label, view=""):
        self.commits.append((label, view))


def fresh_world():
    scene = FakeScene()
    ui = FakeUi()
    ft._scene_model = lambda view: scene
    ft._animean = lambda: types.SimpleNamespace(ui=ui)
    ft._RETRACE_GUARD["depth"] = 0
    return scene, ui


def fill_message(scope="current", seed=(10.0, 10.0)):
    return {"event": "fillrequest", "view": "main", "tool": "fill",
            "base_tool": "fill", "property": "", "cell": {}, "stroke": {},
            "position": {"x": seed[0], "y": seed[1]},
            "delta": {"x": 0.0, "y": 0.0}, "fill_scope": scope,
            "color": {"r": 1, "g": 2, "b": 3, "a": 255},
            "bounds": {"x": 0.0, "y": 0.0, "width": 100.0, "height": 100.0}}


# 1) AUTO-PARENT: a Current-scope fill that has to create its own fill layer
#    parents that layer to the line layer whose topology bounded it - and
#    leaves the board on the LINE layer, which is where the user is working.
#    Ending on the auto-created child would hand the next click a layer whose
#    drawing tools the layer policy locks.
scene, ui = fresh_world()
line = scene.add_layer()
scene.set_current_layer(line)
ft._fill_request({}, {}, fill_message("current"))
assert scene.columns[1]["type"] == "fill"
assert scene.images[scene.layer_id_at(1)].fills, "the fill did not land on the new layer"
assert scene.current_layer() == line, \
    "a fill click left the board on the auto-created child layer"
assert scene.layer_parent_id(1) == scene.layer_id_at(line), \
    "a current-scope fill did not track its source line layer"
assert scene.child_layer_indices(line) == [1]
assert ui.commits == [("Fill", "main")]

# A second click in the same session still works on the same pair rather than
# spawning another fill layer - the board never left the line layer.
ft._fill_request({}, {}, fill_message("current", seed=(20.0, 20.0)))
assert len(scene.columns) == 2, "a repeated fill stacked a second fill layer"

# 2) SCOPE ALL: no single source to follow, so the new layer stays independent.
scene, ui = fresh_world()
line = scene.add_layer()
scene.set_current_layer(line)
ft._fill_request({}, {}, fill_message("all"))
assert scene.columns[1]["type"] == "fill"
assert scene.layer_parent_id(1) == 0, "a scope-All fill parented its layer"

# 3) A fill layer the user already had keeps its own parenting: a click must
#    never silently re-home an existing layer.
scene, ui = fresh_world()
line = scene.add_layer()
existing = scene.add_fill_layer()
scene.set_current_layer(existing)
ft._fill_request({}, {}, fill_message("current"))
assert scene.current_layer() == existing, "the fill created a second fill layer"
assert scene.layer_parent_id(existing) == 0, "an existing fill layer was re-homed"

# 4) RE-TRACE: a linefinish on the parent re-derives its children's regions
#    from their stored seeds; an unrelated layer's event changes nothing.
scene, ui = fresh_world()
line = scene.add_layer()
child = scene.add_fill_layer()
other = scene.add_layer()
scene.set_layer_parent_id(child, scene.layer_id_at(line))
image = scene.image_at(0, child, True, "fill")
image.add_fill_region(["old"], (1, 2, 3, 255), property="",
                      seed=(5.0, 6.0), source_layer_index=line,
                      based_on_all_layers=False)
scene.trace[line] = ["retraced"]
ft._topology_changed({"event": "linefinish", "view": "main",
                      "cell": {"row": 0, "layer": line}})
assert image.fills[0]["path"] == ["retraced"], "a tracked region was not re-derived"
assert scene.trace_calls[-1][1] == (5.0, 6.0), "the stored seed was not reused"
assert scene.trace_calls[-1][3] == line, "the region was traced against the wrong scope"
assert ui.refreshes == ["widget"], "the re-trace did not refresh the canvas"
assert ui.commits == [], "the re-trace pushed a history entry of its own"

scene.trace[line] = ["should-not-apply"]
scene.trace[other] = ["should-not-apply"]
ft._topology_changed({"event": "linefinish", "view": "main",
                      "cell": {"row": 0, "layer": other}})
assert image.fills[0]["path"] == ["retraced"], \
    "an unrelated layer's edit re-derived a tracked region"

# 4b) An all-layers region inside a tracked layer follows its OWN stored
#     scope, not the parent link.
scene, ui = fresh_world()
line = scene.add_layer()
child = scene.add_fill_layer()
scene.set_layer_parent_id(child, scene.layer_id_at(line))
image = scene.image_at(0, child, True, "fill")
image.add_fill_region(["old"], (1, 2, 3, 255), seed=(7.0, 8.0),
                      source_layer_index=-1, based_on_all_layers=True)
scene.trace[-1] = ["all-layers"]
ft._topology_changed({"event": "linefinish", "view": "main",
                      "cell": {"row": 0, "layer": line}})
assert image.fills[0]["path"] == ["all-layers"]
assert scene.trace_calls[-1][3] == -1, "an all-layers region was traced against one column"

# 5) A seed that no longer resolves KEEPS its artwork; so does a frame whose
#    walls are gone entirely.
scene, ui = fresh_world()
line = scene.add_layer()
child = scene.add_fill_layer()
scene.set_layer_parent_id(child, scene.layer_id_at(line))
image = scene.image_at(0, child, True, "fill")
image.add_fill_region(["old"], (1, 2, 3, 255), seed=(5.0, 6.0),
                      source_layer_index=line, based_on_all_layers=False)
scene.trace[line] = None
ft._topology_changed({"event": "linefinish", "view": "main",
                      "cell": {"row": 0, "layer": line}})
assert image.fills[0]["path"] == ["old"], "an unresolvable seed erased the artwork"
assert ui.refreshes == [], "nothing changed, but the canvas was refreshed anyway"

scene.trace[line] = ["would-apply"]
scene.bounds[line] = None
ft._topology_changed({"event": "linefinish", "view": "main",
                      "cell": {"row": 0, "layer": line}})
assert image.fills[0]["path"] == ["old"], "a wall-less frame re-derived a region"

# 6) SELF-ECHO GUARD: a re-trace that re-enters while one is running is a
#    no-op rather than a recursion.
scene, ui = fresh_world()
line = scene.add_layer()
child = scene.add_fill_layer()
scene.set_layer_parent_id(child, scene.layer_id_at(line))
image = scene.image_at(0, child, True, "fill")
image.add_fill_region(["old"], (1, 2, 3, 255), seed=(5.0, 6.0),
                      source_layer_index=line, based_on_all_layers=False)
scene.trace[line] = ["retraced"]
ft._RETRACE_GUARD["depth"] = 1
ft._topology_changed({"event": "linefinish", "view": "main",
                      "cell": {"row": 0, "layer": line}})
assert image.fills[0]["path"] == ["old"], "the guard did not stop a re-entrant pass"
assert scene.trace_calls == [], "a guarded pass still traced"
ft._RETRACE_GUARD["depth"] = 0
ft._topology_changed({"event": "linefinish", "view": "main",
                      "cell": {"row": 0, "layer": line}})
assert image.fills[0]["path"] == ["retraced"], "the guard stayed armed"

# 7) RELEASE: the menu offers "To Independent Layer" only on a tracked row,
#    and taking it cuts the link while leaving every region drawn.
scene, ui = fresh_world()
line = scene.add_layer()
child = scene.add_fill_layer()
scene.set_layer_parent_id(child, scene.layer_id_at(line))
image = scene.image_at(0, child, True, "fill")
image.add_fill_region(["kept"], (1, 2, 3, 255), seed=(5.0, 6.0),
                      source_layer_index=line, based_on_all_layers=False)

assert ft._fill_menu_items({"kind": "layer", "layer": line,
                            "parent_layer_id": 0, "child_count": 1}) == []
assert ft._fill_menu_items({"kind": "group", "group": 1,
                            "parent_layer_id": 0}) == []
entries = ft._fill_menu_items({"kind": "layer", "layer": child,
                               "parent_layer_id": scene.layer_id_at(line),
                               "child_count": 0})
assert [e["name"] for e in entries] == [ft.TO_INDEPENDENT_ACTION]
assert entries[0]["title"] == "To Independent Layer"

ft._fill_menu_action({"event": "layermenu", "view": "main",
                      "action": ft.TO_INDEPENDENT_ACTION, "layer": child,
                      "group": 0, "members": []})
assert scene.layer_parent_id(child) == 0, "the parent link survived the release"
assert image.fills[0]["path"] == ["kept"], "the release dropped the artwork"
assert ui.commits == [("To Independent Layer", "main")]
assert "layer" in ui.refreshes, "the panel was not refreshed after the release"

# A released layer no longer follows the line layer.
scene.trace[line] = ["retraced"]
ft._topology_changed({"event": "linefinish", "view": "main",
                      "cell": {"row": 0, "layer": line}})
assert image.fills[0]["path"] == ["kept"], "an independent layer still tracked"

# An action this module does not own is ignored (other providers share the
# layermenu event).
ui.commits.clear()
ft._fill_menu_action({"event": "layermenu", "view": "main",
                      "action": "new_line_layer", "layer": line})
assert ui.commits == [], "fill_tool answered another module's menu action"

# 7b) The release re-pushes the TOOL POLICY when the released row is the one
#     the board is on: the current layer never moves, so no layerchange will
#     be dispatched and the tracked-child locks would otherwise stand.
scene, ui = fresh_world()
line = scene.add_layer()
child = scene.add_fill_layer()
scene.set_layer_parent_id(child, scene.layer_id_at(line))
scene.set_current_layer(child)
policy_calls = []
stub_policy = types.ModuleType("layer_tool_policy")
stub_policy.reevaluate = lambda view=None: policy_calls.append(view)
saved_policy = sys.modules.get("layer_tool_policy")
sys.modules["layer_tool_policy"] = stub_policy
try:
    ft._fill_menu_action({"event": "layermenu", "view": "main",
                          "action": ft.TO_INDEPENDENT_ACTION, "layer": child})
    assert policy_calls == ["main"], policy_calls

    # A row that is NOT the current layer leaves the policy alone: the board
    # is elsewhere and its verdict is still the right one.
    policy_calls.clear()
    other = scene.add_fill_layer()
    scene.set_layer_parent_id(other, scene.layer_id_at(line))
    ft._fill_menu_action({"event": "layermenu", "view": "main",
                          "action": ft.TO_INDEPENDENT_ACTION, "layer": other})
    assert scene.layer_parent_id(other) == 0, "the release skipped a non-current row"
    assert policy_calls == [], "a non-current row re-pushed the policy"
finally:
    if saved_policy is None:
        sys.modules.pop("layer_tool_policy", None)
    else:
        sys.modules["layer_tool_policy"] = saved_policy

# 8) The hooks are actually registered for the events the policy needs.
events = set()
for hook in python_hooks._HOOKS:
    if hook["function"] is ft._topology_changed:
        events = hook["events"]
assert events == set(ft.TOPOLOGY_EVENTS), \
    f"topology subscriptions are {events}, expected {set(ft.TOPOLOGY_EVENTS)}"
assert any(h["function"] is ft._fill_request and "fillrequest" in h["events"]
           for h in python_hooks._HOOKS)
assert ft._fill_menu_items in python_hooks._MENU_PROVIDERS

print("t_fill_children: ok")
