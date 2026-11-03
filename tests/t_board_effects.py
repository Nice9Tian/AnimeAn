"""Board effects (pyfile/board_effects.py): the animation a tool asks for when
its result lands - Fill spreads from the click, Connect draws its line, and
Auto Mapping's output fades in after an axis or contour is drawn. Covers the
policy half: what each tool asks for, and that it asks BEFORE the change is
written (the page keeps the picture it has when the effect arrives). The
drawing half is webui/tests/board_effects.test.ts."""
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))

_stub = types.ModuleType("animean_python")
_stub.ui = types.SimpleNamespace(set_hook_events=lambda events: None)
sys.modules.setdefault("animean_python", _stub)

import board_effects as fx  # noqa: E402
import connect_tool as ct  # noqa: E402
import auto_mapping as am  # noqa: E402


class FakeUi:
    def __init__(self, fail=False):
        self.calls = []
        self.fail = fail

    def call(self, method, params=None):
        if self.fail:
            raise RuntimeError("ui.call needs the running backend.")
        self.calls.append((method, params))
        return {}


def use_ui(ui):
    fx._animean = lambda: types.SimpleNamespace(ui=ui)


# 1) Path commands become the board's flat path (the S op's format).
commands = [
    {"type": "move", "to": {"x": 1, "y": 2}},
    {"type": "line", "from": {"x": 1, "y": 2}, "to": {"x": 3, "y": 4}},
    {"type": "cubic", "from": {"x": 3, "y": 4}, "control1": {"x": 5, "y": 6},
     "control2": {"x": 7, "y": 8}, "to": {"x": 9, "y": 10}},
    {"type": "close"},
]
assert fx.flat_path(commands) == [0, 1.0, 2.0, 1, 3.0, 4.0, 2, 5.0, 6.0, 7.0, 8.0, 9.0, 10.0, 3]
# A run that starts without a move starts at its own `from`.
assert fx.flat_path([{"type": "line", "from": {"x": 0, "y": 0}, "to": {"x": 5, "y": 0}}]) == \
    [0, 0.0, 0.0, 1, 5.0, 0.0]
assert fx.flat_path([]) == []

# 2) The spread reaches the farthest point of the region from the click.
square = fx.flat_path([
    {"type": "move", "to": {"x": 0, "y": 0}},
    {"type": "line", "to": {"x": 10, "y": 0}},
    {"type": "line", "to": {"x": 10, "y": 10}},
    {"type": "line", "to": {"x": 0, "y": 10}},
    {"type": "close"},
])
assert abs(fx.reach((0.0, 0.0), square) - 200 ** 0.5) < 1e-9
assert abs(fx.reach((5.0, 5.0), square) - 50 ** 0.5) < 1e-9

# 3) What each tool asks for.
ui = FakeUi()
use_ui(ui)
assert fx.fill_spread("main", (2.0, 3.0), [
    {"type": "move", "to": {"x": 0, "y": 0}},
    {"type": "line", "to": {"x": 10, "y": 0}},
    {"type": "line", "to": {"x": 10, "y": 10}},
    {"type": "close"},
])
method, params = ui.calls[-1]
assert method == "board.effect"
assert params["view"] == "main" and params["kind"] == "radial"
assert params["duration"] == fx.FILL_MS
assert params["center"] == [2.0, 3.0]
assert params["clip"][0] == 0 and params["clip"][-1] == 3
assert abs(params["radius"] - ((8 ** 2 + 7 ** 2) ** 0.5)) < 1e-9

assert fx.stroke_draw("child", commands[:2], 4.5)
method, params = ui.calls[-1]
assert params["view"] == "child" and params["kind"] == "stroke"
assert params["duration"] == fx.CONNECT_MS
assert params["path"] == [0, 1.0, 2.0, 1, 3.0, 4.0]
assert params["width"] == 4.5 and params["margin"] == fx.STROKE_MARGIN_PX

assert fx.mapping_fade()
method, params = ui.calls[-1]
assert params == {"view": "main", "kind": "fade", "duration": fx.MAPPING_FADE_MS}

# Nothing to draw: no call at all.
before = len(ui.calls)
assert not fx.fill_spread("main", (0, 0), [])
assert not fx.stroke_draw("main", [], 3)
assert len(ui.calls) == before

# 4) Never at the cost of the tool: no backend, or geometry that cannot be
#    read, is just no animation.
use_ui(FakeUi(fail=True))
assert fx.mapping_fade() is False
assert fx.fill_spread("main", (0, 0), ["not a command"]) is False
assert fx.stroke_draw("main", [{"type": "line"}], 3) is False
use_ui(ui)

# 5) Connect asks for the line drawing BEFORE the stroke is added, with the
#    stroke's own path and width.
order = []


class FakeImage:
    def add_stroke_object(self, stroke):
        order.append(("add", stroke))


class FakeScene:
    def image_at(self, frame, layer, create):
        return FakeImage()

    def cell_to_dict(self, layer, frame, raw, step):
        return {"image": {"strokes": []}}


ct._layer_writable = lambda scene, layer: True
ct._push_handles = lambda view, session: None
ct._commit = lambda label, view: order.append(("commit", label))
line_commands = commands[:2]
ct._stroke_parts = lambda scene, frame, a, b: (line_commands, [(1, 2), (3, 4)], (0, 0, 0, 255), 6.0)
ct._animean = lambda: types.SimpleNamespace(
    vectorlogic=types.SimpleNamespace(make_stroke_object_from_path=lambda *a: "stroke"))
saved_draw = fx.stroke_draw
fx.stroke_draw = lambda view, cmds, width: order.append(("effect", view, cmds, width))
try:
    session = {"first": {"point": (1, 2)}}
    ct._create_connection("main", session, FakeScene(), 0, 1, {"point": (3, 4)})
finally:
    fx.stroke_draw = saved_draw
assert order[0] == ("effect", "main", line_commands, 6.0), order
assert order[1] == ("add", "stroke"), order
assert session["pending"]["stroke_index"] == 0

# 6) Auto Mapping fades in only when a run actually mapped something (after
#    the run, still inside the hook), and only when the caller asks (a
#    finished axis or contour); drags re-render without it.
runs = []
fades = []
mapped = {"ok": True}
saved = (am._run, am._refresh_proxy_axes, am._unit_settings, dict(am._ACTIVE_UNIT), fx.mapping_fade)
am._run = lambda: runs.append(len(fades)) or mapped["ok"]
am._refresh_proxy_axes = lambda: False
fx.mapping_fade = lambda view="main": fades.append(view)
try:
    am._ACTIVE_UNIT["id"] = "u1"
    am._unit_settings = lambda uid=None: {"auto_render": True}
    am._maybe_auto_run(fade=True)
    assert fades == ["main"] and runs == [0], (fades, runs)   # ran, then faded
    mapped["ok"] = False
    am._maybe_auto_run(fade=True)
    assert fades == ["main"] and len(runs) == 2               # nothing mapped: no fade
    mapped["ok"] = True
    runs.pop()
    am._maybe_auto_run()
    assert fades == ["main"] and len(runs) == 2               # a drag: no fade
    am._unit_settings = lambda uid=None: {"auto_render": False}
    am._maybe_auto_run(fade=True)
    assert fades == ["main"] and len(runs) == 2               # nothing ran: no fade
    am._ACTIVE_UNIT["id"] = None
    am._unit_settings = lambda uid=None: {"auto_render": True}
    am._maybe_auto_run(fade=True)
    assert fades == ["main"] and len(runs) == 2               # no unit in focus
finally:
    am._run, am._refresh_proxy_axes, am._unit_settings = saved[0], saved[1], saved[2]
    am._ACTIVE_UNIT.clear()
    am._ACTIVE_UNIT.update(saved[3])
    fx.mapping_fade = saved[4]

# The two "finished drawing" paths are the ones that ask for the fade.
import inspect  # noqa: E402
assert "_maybe_auto_run(fade=True)" in inspect.getsource(am.run_center_line_tool)
assert "_maybe_auto_run(fade=True)" in inspect.getsource(am.run_proxy_contour_tool)

print("t_board_effects: ok")
