"""Arrow tool: a pick that survives the mode switch, and fill regions edited
by their boundary vertices
(user request 2026-08-25: Arrow mode-switch continuity + fill-region handles)."""
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))


# --- fakes ------------------------------------------------------------------

class FakeUi:
    """The slice of animean_python.ui the tool and overlay_stack touch."""

    def __init__(self):
        self.handles = {}
        self.overlays = {}
        self.commits = []
        self.refreshes = 0

    def set_edit_handles(self, view, handles):
        self.handles[view] = [dict(h) for h in handles]

    def set_overlay(self, view, items):
        self.overlays[view] = [dict(i) for i in items]

    def refresh(self):
        self.refreshes += 1

    def history_commit(self, label, view=""):
        self.commits.append((label, view))

    def set_hook_events(self, events):
        pass


class FakeStrokeObject:
    def __init__(self, commands, points, color, width, id):
        self.commands = commands
        self.points = points
        self.color = color
        self.width = width
        self.id = id
        self.property = ""
        self.pen_style = 1


class FakeVectorLogic:
    @staticmethod
    def make_stroke_object(points, color, width, id, *_rest):
        return FakeStrokeObject([], list(points), color, width, id)

    @staticmethod
    def make_stroke_object_from_path(commands, points, color, width, id):
        return FakeStrokeObject(list(commands), list(points), color, width, id)


UI = FakeUi()
_stub = types.ModuleType("animean_python")
_stub.ui = UI
_stub.vectorlogic = FakeVectorLogic
_stub.get_scene = lambda: []
sys.modules["animean_python"] = _stub

import overlay_stack  # noqa: E402
import edit_tool as et  # noqa: E402


def point(x, y):
    return {"x": float(x), "y": float(y)}


class FakeImage:
    """The image_at surface: fill hit-testing, fill rewrites, stroke replace."""

    def __init__(self, strokes, fills):
        self.strokes = strokes
        self.fills = fills
        self.fill_writes = []
        self.stroke_writes = []

    def fill_regions_info(self):
        return [{"index": i, "id": fill["id"]} for i, fill in enumerate(self.fills)]

    def fill_region_contains(self, index, point_dict):
        if not 0 <= index < len(self.fills):
            return False
        xs, ys = [], []
        for command in self.fills[index]["commands"]:
            for key in ("to", "from", "control1", "control2"):
                value = command.get(key)
                if value:
                    xs.append(float(value["x"]))
                    ys.append(float(value["y"]))
        return (min(xs) <= float(point_dict["x"]) <= max(xs)
                and min(ys) <= float(point_dict["y"]) <= max(ys))

    def set_fill_region(self, index, path=None, color=None, seed=None):
        if not 0 <= index < len(self.fills):
            return False
        if path is not None:
            self.fill_writes.append([dict(c) for c in path])
            self.fills[index]["commands"] = [dict(c) for c in path]
        if seed is not None:
            self.fills[index]["seed"] = dict(seed)
        return True

    def replace_stroke_with_pieces(self, index, pieces):
        if not 0 <= index < len(self.strokes):
            return -1
        piece = pieces[0]
        stroke = self.strokes[index]
        stroke["id"] = piece.id
        stroke["commands"] = [dict(c) for c in piece.commands]
        stroke["raw_points"] = [point(p[0], p[1]) for p in piece.points]
        self.stroke_writes.append(index)
        return len(pieces)


class FakeScene:
    """One vector layer of strokes over one fill layer of regions."""

    def __init__(self, images):
        self.images = images
        self.layers = [
            {"index": 0, "visible": True, "locked": False, "type": "vector"},
            {"index": 1, "visible": True, "locked": False, "type": "fill"},
        ]

    def current_frame(self):
        return 0

    def get_structure(self):
        return {"layers": self.layers}

    def image_at(self, frame, layer, create=False, kind="vector"):
        return self.images.get(layer)

    def cell_to_dict(self, layer, frame, to_poly, poly_step):
        image = self.images.get(layer)
        strokes = []
        for stroke in (image.strokes if image else []):
            data = {"id": stroke["id"], "property": stroke["property"],
                    "width": stroke["width"], "pen_style": stroke["pen_style"],
                    "color": dict(stroke["color"]),
                    "raw_points": [dict(p) for p in stroke["raw_points"]]}
            if to_poly:
                # The C++ cell dict carries EITHER polylines or commands.
                elements = et._elements(stroke["commands"])
                flat = (et._flatten_elements(elements) if elements
                        else [(p["x"], p["y"]) for p in stroke["raw_points"]])
                data["polylines"] = [[point(x, y) for x, y in flat]]
            else:
                data["commands"] = [dict(c) for c in stroke["commands"]]
            strokes.append(data)
        fills = [dict(fill, commands=[dict(c) for c in fill["commands"]])
                 for fill in (image.fills if image else [])]
        return {"image": {"strokes": strokes, "fills": fills}}


BLACK = {"r": 0, "g": 0, "b": 0, "a": 255}

# A two-cubic arc through (50, 27.5) and a raw polyline well below it.
CHAIN_COMMANDS = [
    {"type": "move", "to": point(0, 50)},
    {"type": "cubic", "from": point(0, 50), "control1": point(30, 20),
     "control2": point(70, 20), "to": point(100, 50)},
    {"type": "cubic", "from": point(100, 50), "control1": point(130, 80),
     "control2": point(170, 80), "to": point(200, 50)},
]
RAW_POINTS = [point(x, 200 + (x % 40) * 0.25) for x in range(0, 200, 5)]

# A ring with one curved side: its closing line lands back on the move point.
FILL_COMMANDS = [
    {"type": "move", "to": point(10, 10)},
    {"type": "line", "from": point(10, 10), "to": point(90, 10)},
    {"type": "cubic", "from": point(90, 10), "control1": point(110, 40),
     "control2": point(110, 60), "to": point(90, 90)},
    {"type": "line", "from": point(90, 90), "to": point(10, 90)},
    {"type": "line", "from": point(10, 90), "to": point(10, 10)},
]


def world(mode="default"):
    """A fresh scene, a fresh session table, a fresh view."""
    strokes = [
        {"id": 11, "property": "", "width": 4.0, "pen_style": 1, "color": dict(BLACK),
         "commands": [dict(c) for c in CHAIN_COMMANDS],
         "raw_points": [point(x, y) for x, y in
                        et._flatten_elements(et._elements(CHAIN_COMMANDS))]},
        {"id": 12, "property": "", "width": 4.0, "pen_style": 1, "color": dict(BLACK),
         "commands": [], "raw_points": [dict(p) for p in RAW_POINTS]},
    ]
    fills = [{"id": 21, "property": "", "seed": point(50, 50), "color": dict(BLACK),
              "commands": [dict(c) for c in FILL_COMMANDS]}]
    scene = FakeScene({0: FakeImage(strokes, []), 1: FakeImage([], fills)})
    et._scene_model = lambda view, _scene=scene: _scene
    et._SESSIONS.clear()
    et._STATE["mode"] = mode
    et._STATE["last_zoom"] = 1.0
    overlay_stack._LAYERS.clear()
    UI.handles.clear()
    UI.overlays.clear()
    UI.commits.clear()
    return scene


def event(phase, x=0.0, y=0.0, handle="", **extra):
    message = {"base_tool": "arrow", "view": "main", "phase": phase,
               "position": point(x, y), "zoom": 1.0, "handle": handle,
               "modifiers": {}}
    message.update(extra)
    et._handle_event(message)
    return message


def switch(mode):
    et._option_changed({"hook": "edit_mode", "name": "edit_mode", "value": mode})


def handle_ids():
    return [h["id"] for h in UI.handles.get("main", [])]


def handle_at(handle_id):
    for handle in UI.handles.get("main", []):
        if handle["id"] == handle_id:
            return (handle["x"], handle["y"])
    return None


def overlay_ids():
    return [item["id"] for item in UI.overlays.get("main", [])]


# 1) default mode picks a stroke: an outline, no handles, and it claims the drag.
scene = world()
picked = event("pick", 50.0, 28.0)
session = et._SESSIONS["main"]
assert session["kind"] == "stroke" and session["layer"] == 0 and session["index"] == 0
assert picked.get("grab") == "default:body"
assert handle_ids() == []
assert overlay_ids() == ["edit_outline0"], overlay_ids()
print("1) default pick outlines the stroke and claims the gesture")


# 2) THE COMPLAINT: switching modes must not throw the pick away.
switch("debug")
assert et._SESSIONS.get("main") is session, "the mode switch dropped the pick"
assert handle_ids() == ["e0:p", "e1:p", "e2:p"], handle_ids()
assert handle_at("e1:p") == (100.0, 50.0), handle_at("e1:p")
assert "edit_outline0" in overlay_ids(), overlay_ids()

switch("artist")
assert et._SESSIONS.get("main") is session
assert [i for i in handle_ids() if i.startswith("a")] == ["a0", "a1", "a2"], handle_ids()
assert [i for i in handle_ids() if i.startswith("h")], handle_ids()
assert "edit_arm" in overlay_ids() and "edit_outline0" in overlay_ids(), overlay_ids()

switch("default")
assert et._SESSIONS.get("main") is session
assert handle_ids() == [] and overlay_ids() == ["edit_outline0"]
print("2) default -> debug -> artist -> default keeps the same picked stroke")


# 3) the carried session is really editable - no re-pick before the first drag.
world()
event("pick", 50.0, 28.0)
switch("debug")
event("press", 100.0, 50.0, handle="e1:p")
event("move", 100.0, 70.0, handle="e1:p")
mid_drag = UI.overlays["main"][0]
assert mid_drag["id"] == "edit_outline0", mid_drag["id"]
# the highlight follows the ink instead of ghosting the pre-drag shape
assert any(abs(p[0] - 100.0) < 1e-6 and abs(p[1] - 70.0) < 1e-6
           for p in mid_drag["points"]), mid_drag["points"][:4]
event("release", 100.0, 70.0, handle="e1:p")
assert UI.commits == [("Edit Stroke", "main")], UI.commits
assert handle_at("e1:p") == (100.0, 70.0), handle_at("e1:p")
print("3) a handle drag right after the switch edits that stroke, one commit")


# 4) artist mode on a RAW polyline runs the fit (no stored chain to read back).
world("artist")
event("pick", 0.0, 200.0)
session = et._SESSIONS["main"]
assert session["kind"] == "stroke" and session["index"] == 1
anchors = [i for i in handle_ids() if i.startswith("a")]
assert len(anchors) >= 2 and session["chain"] is not None, handle_ids()
print(f"4) artist fit on a raw polyline: {len(anchors)} anchors")


# 5) a fill region picked in DEBUG grows one handle per stored boundary vertex.
scene = world("debug")
event("pick", 30.0, 80.0)
session = et._SESSIONS["main"]
assert session["kind"] == "fill" and session["layer"] == 1 and session["index"] == 0
assert handle_ids() == ["f0", "f1", "f2", "f3"], handle_ids()
assert [handle_at(i) for i in handle_ids()] == [(10.0, 10.0), (90.0, 10.0),
                                                (90.0, 90.0), (10.0, 90.0)]
outline = UI.overlays["main"][0]
assert outline["id"] == "edit_outline0" and outline.get("closed") is True
print("5) fill pick in debug: 4 vertex handles (the closing line shares one)")


# 6) dragging a vertex rewrites the path once, controls in tow, one commit.
fill_image = scene.images[1]
event("press", 90.0, 10.0, handle="f1")
event("move", 120.0, 20.0, handle="f1")
assert len(fill_image.fill_writes) == 1, fill_image.fill_writes
written = fill_image.fill_writes[-1]
assert written[1]["to"] == point(120, 20), written[1]
# the cubic that leaves this vertex keeps its shape: control1 rode along
assert written[2]["control1"] == point(140, 50), written[2]
assert written[2]["to"] == point(90, 90), written[2]
event("release", 120.0, 20.0, handle="f1")
# the release re-applies the same position: the path is unchanged, so nothing
# is written a second time, but the gesture still commits once.
assert len(fill_image.fill_writes) == 1, fill_image.fill_writes
assert UI.commits == [("Edit Fill", "main")], UI.commits
assert handle_at("f1") == (120.0, 20.0), handle_at("f1")
print("6) a fill vertex drag rewrites the path once and commits 'Edit Fill'")


# 7) a press-and-release that never moved must not touch the model.
UI.commits.clear()
event("press", 120.0, 20.0, handle="f1")
event("release", 120.0, 20.0, handle="f1")
assert len(fill_image.fill_writes) == 1 and UI.commits == [], (
    fill_image.fill_writes, UI.commits)
print("7) a no-op click on a fill handle writes nothing and commits nothing")


# 8) artist shows the same vertex set, and the pick survives here too.
switch("artist")
assert et._SESSIONS["main"]["kind"] == "fill"
assert handle_ids() == ["f0", "f1", "f2", "f3"], handle_ids()
assert "edit_outline0" in overlay_ids()
print("8) artist shows a region the same way debug does, pick intact")


# 9) hit-test priority is unchanged: the ink wins over the region under it.
world("debug")
event("pick", 50.0, 28.0)      # inside the fill's bounds AND on the stroke
assert et._SESSIONS["main"]["kind"] == "stroke", et._SESSIONS["main"]["kind"]
print("9) strokes still out-rank fills in the hit test")


# 10) default translate still works for both kinds, and keeps the selection.
scene = world()
event("pick", 30.0, 80.0)
assert et._SESSIONS["main"]["kind"] == "fill"
event("move", 40.0, 80.0, handle="default:body")
event("release", 40.0, 80.0, handle="default:body")
assert UI.commits == [("Move Fill", "main")], UI.commits
assert scene.images[1].fills[0]["commands"][0]["to"] == point(20, 10)
assert "main" in et._SESSIONS

scene = world()
event("pick", 50.0, 28.0)
event("move", 55.0, 28.0, handle="default:body")
event("release", 55.0, 28.0, handle="default:body")
assert UI.commits == [("Move Stroke", "main")], UI.commits
assert scene.images[0].strokes[0]["commands"][0]["to"] == point(5, 50)
assert "main" in et._SESSIONS and overlay_ids() == ["edit_outline0"]
print("10) default translate of a fill and of a stroke, selection kept")


# 11) a mode switch with nothing picked behaves as it always did.
world()
et._SESSIONS.clear()
switch("debug")
switch("artist")
switch("default")
assert et._SESSIONS == {}, et._SESSIONS
assert "main" not in UI.handles and "main" not in UI.overlays
print("11) mode switches with an empty session stay empty")


# 12) undo/redo and the tool leaving still end the session the same way.
scene = world("debug")
event("pick", 30.0, 80.0)
assert "main" in et._SESSIONS
et._history_restored({"view": "main"})
assert et._SESSIONS["main"]["kind"] == "fill" and handle_ids() == ["f0", "f1", "f2", "f3"]
scene.images[1].fills[0]["id"] = 99          # the restore brought a DIFFERENT region
et._history_restored({"view": "main"})
assert et._SESSIONS == {} and UI.handles["main"] == []
world("artist")
event("pick", 50.0, 28.0)
event("cancel")
assert et._SESSIONS == {} and UI.handles["main"] == []
print("12) historyrestore revalidates by identity; cancel clears")

print("t_edit_modes: ALL OK")
