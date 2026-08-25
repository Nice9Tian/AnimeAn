"""Onion-skin ghost guides (round-3 R3-4).

An onion ghost is a rendering of the LAYER STACK, so the H/V axes and the
additional lines - which auto_mapping draws through ui.set_overlay, not into
layers - never appear in one. The "onion" hook event hands the ghost set to
the policy side, which draws the OTHER frames' unit axes itself, tinted like
the ghosts they belong to.
"""
import json
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))


# --- fake bindings ----------------------------------------------------------

class FakeUi:
    def __init__(self):
        self.overlays = {}       # view -> the last full item list pushed

    def set_hook_events(self, events):
        self.events = list(events)

    def set_overlay(self, view, items):
        self.overlays[view] = list(items)

    def refresh(self):
        pass

    def refresh_tool_options(self):
        pass

    def history_commit(self, label, view=""):
        pass


UI = FakeUi()
_stub = types.ModuleType("animean_python")
_stub.ui = UI
sys.modules.setdefault("animean_python", _stub)

import overlay_stack  # noqa: E402
import python_hooks  # noqa: E402
import auto_mapping as am  # noqa: E402


class FakeScene:
    """Columns with stable ids and a sparse (row, layer) cell set."""

    def __init__(self):
        self.columns = []            # [layer id]
        self.cells = set()           # {(row, layer index)}

    def add_layer_with_id(self, lid, rows=()):
        self.columns.append(lid)
        index = len(self.columns) - 1
        for row in rows:
            self.cells.add((row, index))
        return index

    def layer_index_for_id(self, lid):
        return self.columns.index(lid) if lid in self.columns else -1

    def cell_asset_index(self, row, layer_index):
        return 0 if (row, layer_index) in self.cells else -1


H = [(-300.0, 0.0), (300.0, 0.0)]
V = [(0.0, -200.0), (0.0, 200.0)]


def install_unit(scene, uid, lid, rows, extra_line=None):
    """A unit whose single member layer has cells on `rows`."""
    scene.add_layer_with_id(lid, rows)
    am._UNIT_META[uid] = {
        "settings": dict(am._UNIT_SETTING_DEFAULTS),
        "primary": lid,
        "members": {str(lid): {"role": "front", "depth": 0}},
    }
    assets = am._UNIT_ASSETS.setdefault("main", {}).setdefault(uid, {})
    assets[am.H_PROPERTY] = {"points": list(H), "width": 3.0}
    assets[am.V_PROPERTY] = {"points": list(V), "width": 3.0}
    if extra_line:
        assets[am.ADDITIONAL_PROPERTY] = {"lines": [{"points": list(extra_line)}]}


def fresh_world():
    scene = FakeScene()
    am._scene_model = lambda view: scene
    am._animean = lambda: _stub
    am._UNIT_META.clear()
    am._UNIT_ASSETS.clear()
    am._MAPPING_ASSETS.clear()
    am._ONION.clear()
    am._ONION_PUSHED.clear()
    am._ACTIVE_UNIT["id"] = None
    overlay_stack._LAYERS.clear()
    UI.overlays.clear()
    return scene


def ghosts():
    return overlay_stack._LAYERS.get("main", {}).get(am.ONION_OVERLAY_OWNER, [])


def onion(**kwargs):
    message = {"view": "main", "enabled": True, "guides": True,
               "frames": [0, 2], "current": 1}
    message.update(kwargs)
    am._onion_event(message)


# 1) HOOK VOCABULARY: "onion" is a subscribable event, so C++ can gate the
#    dispatch on animeanHookEventSubscribed("onion").
python_hooks.del_hook()
python_hooks.set_hook(am._onion_event, onion=True)
assert "onion" in UI.events, UI.events
assert python_hooks.has_hooks({"event": "onion", "view": "main"})
python_hooks.del_hook()
print("1) python_hooks.set_hook accepts onion=True and pushes the subscription")

# 2) PAST/AHEAD: a unit on frame 0 ghosts RED, a unit on frame 2 ghosts GREEN,
#    while the playhead sits on frame 1.
scene = fresh_world()
install_unit(scene, "1", 11, rows=(0,))
install_unit(scene, "3", 33, rows=(2,), extra_line=[(10.0, 10.0), (90.0, 90.0)])
onion()
items = ghosts()
by_id = {item["id"]: item for item in items}
past_h = by_id[f"{am.ONION_GUIDE_ID}:1:{am.H_PROPERTY}"]
past_v = by_id[f"{am.ONION_GUIDE_ID}:1:{am.V_PROPERTY}"]
ahead_h = by_id[f"{am.ONION_GUIDE_ID}:3:{am.H_PROPERTY}"]
assert past_h["color"] == am.ONION_PAST_COLOR == (179, 57, 47, 150), past_h["color"]
assert past_v["color"] == am.ONION_PAST_COLOR
assert ahead_h["color"] == am.ONION_AHEAD_COLOR == (47, 122, 79, 150)
assert past_h["points"] == H and past_v["points"] == V
# The frame-2 unit's additional line ghosts too, in the same tint.
additional = [item for item in items
              if am.ADDITIONAL_PROPERTY in item["id"]]
assert len(additional) == 1 and additional[0]["color"] == am.ONION_AHEAD_COLOR
# Non-interactive, and thinner than the live axis it mirrors.
assert all(item["removable"] is False and item["draggable"] is False
           for item in items)
assert past_h["width"] < 3.0
# The ghosts ride their OWN owner slot: the live overlay is untouched.
assert am.ONION_OVERLAY_OWNER in overlay_stack._LAYERS["main"]
assert UI.overlays["main"] == items
print(f"2) {len(items)} ghost item(s): red for frame 0, green for frame 2")

# 3) GUIDE LINE OFF clears the slot; so does turning onion off entirely.
onion(guides=False)
assert ghosts() == [] and am.ONION_OVERLAY_OWNER not in overlay_stack._LAYERS["main"]
onion()
assert ghosts(), "re-enabling Guide Line must bring the ghosts back"
onion(enabled=False)
assert ghosts() == []
print("3) guides off / onion off clear the owner slot")

# 4) A unit that lives on the CURRENT frame is not ghosted - its axes are the
#    live overlay's job.
scene = fresh_world()
install_unit(scene, "1", 11, rows=(1,))
onion()
assert ghosts() == [], ghosts()
# ... and a unit exposed on BOTH the current frame and a ghost frame stays out
# of the ghost set rather than being drawn twice.
scene = fresh_world()
install_unit(scene, "1", 11, rows=(0, 1))
onion()
assert ghosts() == []
print("4) units on the current frame are never ghosted")

# 5) Only ENABLED frames count: a unit parked on a frame nobody asked to ghost
#    contributes nothing.
scene = fresh_world()
install_unit(scene, "1", 11, rows=(4,))
onion()
assert ghosts() == []
onion(frames=[0, 2, 4])
assert len(ghosts()) == 2, ghosts()
assert all(item["color"] == am.ONION_AHEAD_COLOR for item in ghosts())
print("5) only frames in the ghost set produce ghosts")

# 6) LEGACY documents (no units at all): the axes are one scene-global,
#    frame-invariant set, so ghosting them would redraw the live lines on top
#    of themselves. Nothing is pushed.
scene = fresh_world()
am._MAPPING_ASSETS["main"] = {am.H_PROPERTY: {"points": list(H), "width": 3.0},
                              am.V_PROPERTY: {"points": list(V), "width": 3.0}}
onion()
assert ghosts() == []
print("6) legacy no-unit documents ghost nothing (frame-invariant axes)")

# 7) A GHOSTED UNIT'S OWN Advanced Settings decide what ghosts. Hiding a
#    component is a statement about the component, not about which frame the
#    playhead sits on, so a unit with H Axis switched off must not get its H
#    axis back as a ghost - while its V axis and additional lines still do.
scene = fresh_world()
install_unit(scene, "1", 11, rows=(0,), extra_line=[(10.0, 10.0), (90.0, 90.0)])
am._UNIT_META["1"]["settings"]["show_h"] = False
onion()
ids = {item["id"] for item in ghosts()}
assert f"{am.ONION_GUIDE_ID}:1:{am.H_PROPERTY}" not in ids, ids
assert f"{am.ONION_GUIDE_ID}:1:{am.V_PROPERTY}" in ids, ids
assert any(am.ADDITIONAL_PROPERTY in item for item in ids), ids
# The gate reads the GHOSTED unit, not the active one: a second unit with the
# axis still on keeps its H ghost even while unit 1 is the active one.
install_unit(scene, "3", 33, rows=(2,))
am._ACTIVE_UNIT["id"] = "1"
onion()
ids = {item["id"] for item in ghosts()}
assert f"{am.ONION_GUIDE_ID}:1:{am.H_PROPERTY}" not in ids, ids
assert f"{am.ONION_GUIDE_ID}:3:{am.H_PROPERTY}" in ids, ids
# Every component off leaves that unit out of the push entirely.
am._UNIT_META["3"]["settings"].update(show_h=False, show_v=False,
                                      show_additional=False)
am._UNIT_META["1"]["settings"].update(show_v=False, show_additional=False)
onion()
assert ghosts() == [], ghosts()
print("7) a ghosted unit's own show_h/show_v/show_additional gate its ghosts")

# 8) The unit render path re-pushes the ghosts: _push_overlay is the one
#    refresh point every run/install/delete already goes through.
scene = fresh_world()
install_unit(scene, "1", 11, rows=(0,))
onion()
count = len(ghosts())
assert count
overlay_stack._LAYERS["main"].pop(am.ONION_OVERLAY_OWNER)
am._push_overlay("main")
assert len(ghosts()) == count, "a unit re-render must restore the ghost guides"
print("8) _push_overlay re-pushes the ghost slot")

# 9) The message carries the OTHER ghost gates too ("lines", "fills" for the
#    transport's LINE/FILL toggles, "am_layers" for AM LAYER). All three gate
#    layer-stack content, which no overlay is, so the ghost guides must come
#    out identical either way - and a handler that read them positionally, or
#    refused unknown keys, would drift the moment C++ gained one.
scene = fresh_world()
install_unit(scene, "1", 11, rows=(0,), extra_line=[(10.0, 10.0), (90.0, 90.0)])
onion()
baseline = [dict(item) for item in ghosts()]
assert baseline
onion(lines=False, fills=False, am_layers=False)
assert ghosts() == baseline, ghosts()
onion(lines=True, fills=True, am_layers=True, unknown_future_key=1)
assert ghosts() == baseline, ghosts()
print("9) lines/fills/am_layers (and any unknown key) leave the ghost guides alone")

# 10) MECHANISM / POLICY: the AM LAYER gate is C++'s to apply, but the WORD
#     "automapping" is ours. Importing auto_mapping registers the layer-group
#     tag the gate keys on, exactly as it registers the guide properties, and
#     C++ reads both back as JSON at startup.
assert json.loads(python_hooks.onion_layer_tag_json()) == am.UNIT_TAG
assert am.UNIT_TAG, "an empty tag would gate nothing"
guide_properties = json.loads(python_hooks.onion_guide_properties_json())
assert am.H_PROPERTY in guide_properties, guide_properties
# A fresh registry gates nothing: a session with no auto-mapping module keeps
# ghosting every layer whatever AM LAYER says.
saved = python_hooks._ONION_LAYER_TAG
python_hooks.register_onion_layer_tag(None)
assert json.loads(python_hooks.onion_layer_tag_json()) == ""
python_hooks.register_onion_layer_tag(saved)
print("10) auto_mapping registers the onion layer tag; C++ never spells it")

print("t_onion_guides: ok")
