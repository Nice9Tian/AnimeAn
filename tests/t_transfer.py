"""Transfer tool: the oriented box (rotation), the long-side proportional-scale
fix, the incremental matrix chain and the pointer policy
(user request 2026-08-25: Transfer rotation + Move deletion)."""
import math
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
        self.cursors = []
        self.commits = []
        self.refreshes = 0

    def set_edit_handles(self, view, handles):
        self.handles[view] = handles

    def set_overlay(self, view, items):
        self.overlays[view] = items

    def set_cursor(self, view, name):
        self.cursors.append((view, name))

    def refresh(self):
        self.refreshes += 1

    def history_commit(self, label, view=""):
        self.commits.append((label, view))

    def set_hook_events(self, events):
        pass


UI = FakeUi()
_stub = types.ModuleType("animean_python")
_stub.ui = UI
_stub.get_scene = lambda: []
sys.modules["animean_python"] = _stub

import transfer_tool as tt  # noqa: E402


class FakeImage:
    def __init__(self, bounds=(0.0, 0.0, 100.0, 50.0), raster=False):
        self._bounds = bounds
        self.raster = raster
        self.applied = []

    def bounds(self):
        return self._bounds

    def has_raster(self):
        return self.raster

    def transform(self, m11, m12, m21, m22, dx, dy):
        self.applied.append((m11, m12, m21, m22, dx, dy))


class FakeScene:
    def __init__(self, image):
        self.image = image

    def current_frame(self):
        return 0

    def current_layer(self):
        return 0

    def get_structure(self):
        return {"layers": [{"index": 0, "locked": False, "visible": True}]}

    def image_at(self, frame, layer, create, kind):
        return self.image


def world(image):
    """Install a one-cell scene and a clean session table."""
    scene = FakeScene(image)
    tt._scene_model = lambda view, _scene=scene: _scene
    tt._SESSIONS.clear()
    tt._CURSORS.clear()
    tt._STATE["constrain"] = True
    UI.cursors.clear()
    UI.commits.clear()
    return scene


def session_of(box, angle=0.0, origin=None):
    return {"box": list(box), "angle": angle,
            "origin": origin if origin is not None else tt._box_centre(box)}


def close(a, b, eps=1e-9):
    return abs(a - b) <= eps


def mat_close(a, b, eps=1e-9):
    return all(abs(x - y) <= eps for x, y in zip(a, b))


# 1) the proportional fix: the aspect-locked corner fits INSIDE the mouse rect.
start = [0.0, 0.0, 100.0, 50.0]
drag = tt._drag_record("se", (100.0, 50.0), session_of(start))
box, angle, origin = tt._target_state(drag, (200.0, 75.0), True)
# free scales would be x2.0 / y1.5; the min rule takes 1.5 for both.
assert close(box[2] - box[0], 150.0), box
assert close(box[3] - box[1], 75.0), box
# ... and that is what "fits the rectangle the cursor drew" means:
assert box[2] - box[0] <= 200.0 and box[3] - box[1] <= 75.0
free_box, _, _ = tt._target_state(drag, (200.0, 75.0), False)
assert close(free_box[2], 200.0) and close(free_box[3], 75.0), free_box
# a drag past the anchor still mirrors, and the lock keeps the per-axis sign
flip_box, _, _ = tt._target_state(drag, (-200.0, 75.0), True)
assert close(flip_box[2] - flip_box[0], -150.0), flip_box
assert close(flip_box[3] - flip_box[1], 75.0), flip_box
assert close(angle, 0.0) and origin == tt._box_centre(start)
print("1) corner aspect lock takes the SMALLER magnitude, signs preserved")


# 2) the matrix wheel: a rotation target, and the inverse delta that undoes it.
base = [10.0, 20.0, 110.0, 70.0]
centre = tt._box_centre(base)
upright = (base, 0.0, centre)
turned_state = (base, math.radians(30.0), centre)
total = tt._affine(upright, turned_state)
assert total is not None
# it IS the rotation: the centre is fixed, a corner lands on the turned corner.
assert all(close(a, b, 1e-9) for a, b in zip(tt._mat_apply(total, centre), centre))
corner = (base[0], base[1])
assert all(close(a, b, 1e-9) for a, b in
           zip(tt._mat_apply(total, corner),
               tt._rotate_point(corner, centre, math.radians(30.0))))
# delta = T . T_prev^-1: from "already rotated" back to upright is the inverse.
back = tt._delta_matrix(tt._affine(upright, upright), total)
assert back is not None
assert mat_close(tt._mat_compose(back, total), tt.IDENTITY, 1e-9)
# a target equal to what is already applied has nothing left to do
assert tt._delta_matrix(total, total) is None
print("2) _affine/_delta_matrix roundtrip returns the identity within 1e-9")


# 3) rotation drag: the pointer angle about the pivot, snapped by a modifier.
square = [0.0, 0.0, 100.0, 100.0]
pivot = tt._box_centre(square)          # (50, 50)
rot = tt._drag_record(tt.ROTATE, (150.0, 50.0), session_of(square))
assert rot is not None and rot["pivot"] == pivot and close(rot["start_pointer"], 0.0)


def pointer_at(degrees):
    radians = math.radians(degrees)
    return (pivot[0] + 100.0 * math.cos(radians),
            pivot[1] + 100.0 * math.sin(radians))


_, free_angle, _ = tt._target_state(rot, pointer_at(20.0), False)
assert close(math.degrees(free_angle), 20.0, 1e-9), math.degrees(free_angle)
for pointed, snapped in ((20.0, 15.0), (40.0, 45.0), (-20.0, -15.0), (7.0, 0.0)):
    _, locked, _ = tt._target_state(rot, pointer_at(pointed), True)
    assert close(math.degrees(locked), snapped, 1e-9), (pointed, math.degrees(locked))
# the rect never moves under a rotation; only the frame it is read in does
rot_box, _, rot_origin = tt._target_state(rot, pointer_at(20.0), True)
assert rot_box == square and rot_origin == pivot
# a press sitting on its own pivot has no angle to measure from
assert tt._drag_record(tt.ROTATE, pivot, session_of(square)) is None
print("3) rotation follows the pointer about the pivot; constrain snaps to 15")


# 4) a scale drag on a TURNED session runs in the box's own frame.
turned = session_of([0.0, 0.0, 100.0, 50.0], math.radians(90.0))
grips = tt._grip_positions(tt._state(turned))
# a quarter turn puts the "e" grip below the centre in doc coords (y is down)
assert all(close(a, b, 1e-9) for a, b in zip(grips["e"], (50.0, 75.0))), grips["e"]
edge = tt._drag_record("e", grips["e"], turned)
grown, grown_angle, grown_origin = tt._target_state(edge, (50.0, 125.0), False)
# pushing along doc +y grew the box's LOCAL x span, not its y span
assert close(grown[2] - grown[0], 150.0), grown
assert close(grown[3] - grown[1], 50.0), grown
assert close(grown_angle, math.radians(90.0)) and grown_origin == turned["origin"]
# and in doc space the artwork reaches further down the page than it did
before = max(p[1] for p in tt._box_corners(tt._state(turned)))
after = max(p[1] for p in tt._box_corners((grown, grown_angle, grown_origin)))
assert close(before, 75.0) and close(after, 125.0), (before, after)
print("4) scale on a rotated box works in the local frame")


# 5) a raster cell refuses rotation - no claim, and the pointer does not lie.
raster = FakeImage(raster=True)
world(raster)
tt._handle_event({"base_tool": "transfer", "view": "main", "phase": "arm",
                  "position": {"x": 0.0, "y": 0.0}, "zoom": 1.0})
assert tt._SESSIONS["main"]["box"] == [0.0, 0.0, 100.0, 50.0]
assert tt._SESSIONS["main"]["rotatable"] is False
ring = {"base_tool": "transfer", "view": "main", "phase": "pick",
        "position": {"x": 110.0, "y": 60.0}, "zoom": 1.0}
tt._handle_event(ring)
assert "grab" not in ring, ring
assert tt._SESSIONS["main"]["drag"] is None
assert tt._cursor_name(tt._state(tt._SESSIONS["main"]),
                       (110.0, 60.0), 1.0, False) == "arrow"
# the same cell as vector: the ring claims the gesture and says so
vector = FakeImage()
world(vector)
tt._handle_event({"base_tool": "transfer", "view": "main", "phase": "arm",
                  "position": {"x": 0.0, "y": 0.0}, "zoom": 1.0})
assert tt._SESSIONS["main"]["rotatable"] is True
ring = {"base_tool": "transfer", "view": "main", "phase": "pick",
        "position": {"x": 110.0, "y": 60.0}, "zoom": 1.0}
tt._handle_event(ring)
assert ring.get("grab") == "tf:rot:se", ring.get("grab")
assert tt._SESSIONS["main"]["drag"]["grip"] == tt.ROTATE
assert tt._cursor_name(tt._state(tt._SESSIONS["main"]),
                       (110.0, 60.0), 1.0, True) == "rotate"
print("5) raster refuses rotation: no grab claimed, pointer says arrow")


# 6) the pointer policy: grips by direction, body, ring, nothing - and it
#    follows the box's angle.
upright_state = ([0.0, 0.0, 100.0, 50.0], 0.0, (50.0, 25.0))
for grip, name in (("n", "size_v"), ("s", "size_v"), ("e", "size_h"),
                   ("w", "size_h"), ("nw", "size_fdiag"), ("se", "size_fdiag"),
                   ("ne", "size_bdiag"), ("sw", "size_bdiag")):
    pos = tt._grip_positions(upright_state)[grip]
    assert tt._cursor_name(upright_state, pos, 1.0, True) == name, (grip, name)
assert tt._cursor_name(upright_state, (50.0, 25.0), 1.0, True) == "size_all"
assert tt._cursor_name(upright_state, (110.0, 60.0), 1.0, True) == "rotate"
assert tt._cursor_name(upright_state, (400.0, 400.0), 1.0, True) == ""
# zoomed in, the same screen-px reach covers less artwork (viewscale)
assert tt._cursor_name(upright_state, (118.0, 50.0), 1.0, True) == "rotate"
assert tt._cursor_name(upright_state, (118.0, 50.0), 4.0, True) == ""
quarter = ([0.0, 0.0, 100.0, 50.0], math.radians(90.0), (50.0, 25.0))
assert tt._cursor_name(quarter, tt._grip_positions(quarter)["e"], 1.0, True) == "size_v"
assert tt._cursor_name(quarter, tt._grip_positions(quarter)["n"], 1.0, True) == "size_h"
print("6) pointer policy: grip direction, body, ring, and it turns with the box")


# 7) the pointer is named only when it CHANGES (hover arrives every 33 ms).
world(FakeImage())
tt._set_cursor("main", "size_all")
tt._set_cursor("main", "size_all")
tt._set_cursor("main", "rotate")
assert UI.cursors == [("main", "size_all"), ("main", "rotate")], UI.cursors
# through the hook: a hover with no session clears, an armed one classifies,
# and a repeat of the same region says nothing more.
world(FakeImage())


def hover(x, y):
    tt._handle_event({"base_tool": "transfer", "view": "main", "phase": "hover",
                      "position": {"x": x, "y": y}, "zoom": 1.0})


hover(500.0, 500.0)
assert UI.cursors == [("main", "")], UI.cursors
tt._handle_event({"base_tool": "transfer", "view": "main", "phase": "arm",
                  "position": {"x": 0.0, "y": 0.0}, "zoom": 1.0})
hover(50.0, 25.0)
hover(51.0, 25.0)
hover(110.0, 60.0)
assert UI.cursors == [("main", ""), ("main", "size_all"), ("main", "rotate")], UI.cursors
print("7) set_cursor pushes on state change only, hover routes through it")


# 8) one drag end to end: incremental delta, one commit, no identity apply.
image = FakeImage()
world(image)
common = {"base_tool": "transfer", "view": "main", "zoom": 1.0,
          "modifiers": {"constrain": False}}
tt._handle_event({**common, "phase": "arm", "position": {"x": 0.0, "y": 0.0}})
tt._handle_event({**common, "phase": "press", "handle": "tf:se",
                  "position": {"x": 100.0, "y": 50.0}})
tt._handle_event({**common, "phase": "move", "handle": "tf:se",
                  "position": {"x": 200.0, "y": 100.0}})
assert len(image.applied) == 1, image.applied
assert mat_close(image.applied[0], (2.0, 0.0, 0.0, 2.0, 0.0, 0.0)), image.applied[0]
tt._handle_event({**common, "phase": "release", "handle": "tf:se",
                  "position": {"x": 200.0, "y": 100.0}})
# the release re-sends the same target: the delta is the identity, so nothing
# is applied a second time, but the gesture still commits once.
assert len(image.applied) == 1, image.applied
assert UI.commits == [("Transfer", "main")], UI.commits
# a click that transformed nothing must not burn a history entry
UI.commits.clear()
picked = {**common, "phase": "pick", "position": {"x": 50.0, "y": 25.0}}
tt._handle_event(picked)
assert picked.get("grab") == "tf:body"
tt._handle_event({**common, "phase": "release", "handle": "tf:body",
                  "position": {"x": 50.0, "y": 25.0}})
assert len(image.applied) == 1 and UI.commits == [], (image.applied, UI.commits)
print("8) one drag: incremental delta, one commit, no-op click commits nothing")

print("t_transfer: ALL OK")
