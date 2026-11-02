"""The Curve and Round Corner tools (issue #100): Catmull-Rom through clicked
anchors, ending, snapping on, joining and closing lines; rounding a corner by its
control line, the held end staying on the pointer, the snap stops, cancel and
undo. Gestures are driven through the same handle messages C++ sends, on a small
stand-in for the scene model that keeps strokes as the binding reports them."""
import json
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))

COMMITS = []
OVERLAYS = {}


def _pt(p):
    if isinstance(p, dict):
        return (float(p["x"]), float(p["y"]))
    return (float(p[0]), float(p[1]))


def _normalized(commands):
    """Commands as pathCommandsToList reports them: lines stay lines, every
    curve is a cubic, each segment carries its start."""
    out, cur = [], None
    for c in commands:
        kind = c["type"]
        if kind == "move":
            cur = _pt(c["to"])
            out.append({"type": "move", "to": {"x": cur[0], "y": cur[1]}})
        elif kind == "line":
            to = _pt(c["to"])
            out.append({"type": "line", "from": {"x": cur[0], "y": cur[1]}, "to": {"x": to[0], "y": to[1]}})
            cur = to
        elif kind == "cubic":
            to = _pt(c["to"])
            c1, c2 = _pt(c["control1"]), _pt(c["control2"])
            out.append({"type": "cubic", "from": {"x": cur[0], "y": cur[1]},
                        "control1": {"x": c1[0], "y": c1[1]}, "control2": {"x": c2[0], "y": c2[1]},
                        "to": {"x": to[0], "y": to[1]}})
            cur = to
        else:
            raise AssertionError(kind)
    return out


class Stroke(types.SimpleNamespace):
    def as_dict(self):
        return {"commands": self.commands, "raw_points": [{"x": p[0], "y": p[1]} for p in self.points],
                "color": dict(zip("rgba", self.color)), "width": self.width, "id": self.id,
                "property": self.property, "pen_style": self.pen_style}


def make_stroke_object_from_path(commands, points, color=(0, 0, 0, 255), width=3.0, id=0):
    return Stroke(commands=_normalized(commands), points=[_pt(p) for p in points], color=tuple(color),
                  width=float(width), id=int(id), property="", pen_style=1)


class Image:
    def __init__(self):
        self.strokes = []

    def add_stroke_object(self, stroke):
        self.strokes.append(stroke)

    def replace_stroke_with_pieces(self, index, pieces):
        self.strokes[index:index + 1] = list(pieces)
        return len(pieces)


class Scene:
    def __init__(self, layers=1):
        self.layers = [{"index": i, "visible": True, "locked": False, "type": "vector"} for i in range(layers)]
        self.images = {}
        self.data = ""

    def get_structure(self):
        return {"layers": [dict(l) for l in self.layers], "frame_count": 1}

    def image_at(self, row, layer, create=False):
        return self.images.setdefault((row, layer), Image())

    def cell_to_dict(self, layer, row, to_poly=False, step=4.0):
        image = self.images.get((row, layer))
        return {"asset_index": layer, "frame_id": row,
                "image": {"strokes": [s.as_dict() for s in (image.strokes if image else [])]}}

    def remove_stroke(self, row, layer, index):
        del self.images[(row, layer)].strokes[index]

    def script_data(self):
        return self.data

    def current_frame(self):
        return 0

    def layer_id_at(self, layer):
        return f"layer-{layer}"      # no layer here has tracked fills as children

    def set_script_data(self, text):
        self.data = text

    # the snapshot an undo restores
    def snapshot(self):
        return ({k: list(v.strokes) for k, v in self.images.items()}, self.data)

    def restore(self, snap):
        images, data = snap
        self.images = {}
        for k, strokes in images.items():
            self.images[k] = Image()
            self.images[k].strokes = list(strokes)
        self.data = data


api = types.ModuleType("animean_python")
api.ui = types.SimpleNamespace(
    set_overlay=lambda view, items: OVERLAYS.__setitem__(view, items),
    history_commit=lambda label, view: COMMITS.append(label),
    refresh=lambda: None, refresh_tool_options=lambda: None, set_hook_events=lambda *a: None)
api.vectorlogic = types.SimpleNamespace(make_stroke_object_from_path=make_stroke_object_from_path)
api.get_scene = lambda: []
sys.modules["animean_python"] = api

import __main__
import bezier
import curve_tool as ct
import round_corner_tool as rc


def msg(tool_property, phase, x=0.0, y=0.0, *, row=0, layer=0, alt=False, shift=False, handle="", zoom=1.0):
    return {"event": "handle", "view": "main", "tool": "extra", "base_tool": "arrow",
            "property": tool_property, "phase": phase, "handle": handle,
            "position": {"x": x, "y": y}, "cell": {"row": row, "layer": layer},
            "modifiers": {"alt": alt, "shift": shift, "ctrl": False}, "zoom": zoom,
            "color": {"r": 10, "g": 20, "b": 30, "a": 255}}


def curve(phase, x=0.0, y=0.0, **kw):
    ct._on_handle(msg(ct.PROPERTY, phase, x, y, **kw))


def strokes(scene, row=0, layer=0):
    image = scene.images.get((row, layer))
    return image.strokes if image else []


def anchors_of(stroke):
    return [_pt(c["to"]) for c in stroke.commands]


def g1(spans, closed=False):
    worst = 1.0
    joints = len(spans) if closed else len(spans) - 1
    for i in range(joints):
        a, b = spans[i], spans[(i + 1) % len(spans)]
        u = (a[3][0] - a[2][0], a[3][1] - a[2][1])
        v = (b[1][0] - b[0][0], b[1][1] - b[0][1])
        lu, lv = math.hypot(*u), math.hypot(*v)
        if lu > 1e-9 and lv > 1e-9:
            worst = min(worst, (u[0] * v[0] + u[1] * v[1]) / (lu * lv))
    return worst


# 1) Catmull-Rom: through every anchor, G1 everywhere, a straight line stays
#    straight, a new anchor reshapes only the span before it, reversed anchors
#    give the same curve backwards.
import random
rng = random.Random(7)
for closed in (False, True):
    for _ in range(200):
        pts = [(rng.uniform(0, 900), rng.uniform(0, 600)) for _ in range(rng.randint(3, 14))]
        spans = ct.catmull_rom(pts, closed)
        assert len(spans) == (len(pts) if closed else len(pts) - 1)
        for i, s in enumerate(spans):
            assert bezier.dist(s[0], pts[i]) < 1e-9 and bezier.dist(s[3], pts[(i + 1) % len(pts)]) < 1e-9
        assert g1(spans, closed) > 1 - 1e-9
line_pts = [(t * 50.0, t * 30.0) for t in (0, 1, 2.5, 4)]
for s in ct.catmull_rom(line_pts):
    for p in s:
        assert abs(p[0] * 30 - p[1] * 50) < 1e-9
pts = [(rng.uniform(0, 900), rng.uniform(0, 600)) for _ in range(8)]
before, after = ct.catmull_rom(pts[:7]), ct.catmull_rom(pts)
assert [i for i in range(len(before)) if before[i] != after[i]] == [5]
forward, backward = ct.catmull_rom(pts), list(reversed(ct.catmull_rom(list(reversed(pts)))))
assert all(bezier.dist(f[k], b[3 - k]) < 1e-9 for f, b in zip(forward, backward) for k in range(4))
print("1) Catmull-Rom through anchors, G1, local, reversible")

# 2) Drawing: two anchors make the stroke, each further anchor rewrites it in
#    place and commits once; a right click ends the line; a lone anchor never
#    reaches the model; a second click on the last anchor is a double click.
scene = Scene()
__main__.main_model = scene
COMMITS.clear()
curve("arm")
curve("pick", 100, 300)
assert strokes(scene) == [] and COMMITS == []
curve("pick", 200, 150)
assert len(strokes(scene)) == 1 and COMMITS == ["Curve"]
curve("pick", 201, 150)                  # within the double-click gap
assert len(COMMITS) == 1
curve("pick", 320, 260)
curve("pick", 430, 140)
assert len(strokes(scene)) == 1 and len(COMMITS) == 3
assert anchors_of(strokes(scene)[0]) == [(100, 300), (200, 150), (320, 260), (430, 140)]
assert strokes(scene)[0].width == ct._STATE["width"]
curve("hover", 500, 300)
preview = [item for item in OVERLAYS["main"] if item["id"] == "curve:preview"]
assert preview and bezier.dist(preview[0]["points"][-1], (500, 300)) < 1e-6
curve("context", 500, 300)
assert ct._SESSIONS["main"]["active"] is None
assert not [item for item in OVERLAYS["main"] if item["id"] == "curve:preview"]
records = json.loads(scene.data)[ct.STORE_KEY]["lines"]
assert len(records) == 1 and len(records[0]["anchors"]) == 4
curve("pick", 700, 500)
curve("context", 700, 500)               # a lone anchor goes
assert len(strokes(scene)) == 1 and len(COMMITS) == 3
print("2) anchors place, the line ends on the right button")

# 3) Snapping on: the tail resumes the same line, the head resumes it backwards,
#    Alt keeps the click free.
curve("hover", 436, 146)
target = ct._SESSIONS["main"]["target"]
assert target and target["kind"] == "resume" and target["end"] == "tail"
curve("pick", 436, 146)
assert ct._SESSIONS["main"]["active"]["anchors"][-1] == (430, 140)
curve("pick", 520, 250)
assert len(strokes(scene)) == 1
assert anchors_of(strokes(scene)[0])[-1] == (520, 250) and len(anchors_of(strokes(scene)[0])) == 5
curve("context")
curve("hover", 104, 296)
assert ct._SESSIONS["main"]["target"]["end"] == "head"
curve("pick", 104, 296)
curve("pick", 40, 200)
chain = anchors_of(strokes(scene)[0])
assert chain[0] == (520, 250) and chain[-1] == (40, 200) and len(chain) == 6
curve("context")
curve("hover", 42, 202, alt=True)
assert ct._SESSIONS["main"]["target"] is None
curve("pick", 42, 202, alt=True)
assert ct._SESSIONS["main"]["active"]["uid"] is None      # a new line, not the old one
curve("context")
print("3) ends snap: tail and head resume the same line, Alt is free")

# 4) Joining and closing: drawing into another line's end makes one line; a
#    line snapped to its own first anchor closes.
curve("pick", 600, 100)
curve("pick", 700, 60)
curve("context")
assert len(strokes(scene)) == 2
curve("pick", 800, 200)
curve("pick", 760, 120)
curve("hover", 703, 63)
assert ct._SESSIONS["main"]["target"]["kind"] == "merge"
curve("pick", 703, 63)
assert len(strokes(scene)) == 2 and COMMITS[-1] == "Join Curves"
joined = [s for s in strokes(scene) if len(anchors_of(s)) == 4][0]
assert anchors_of(joined) == [(800, 200), (760, 120), (700, 60), (600, 100)]
curve("pick", 300, 500)
curve("pick", 400, 450)
curve("pick", 380, 560)
curve("hover", 303, 503)
assert ct._SESSIONS["main"]["target"]["kind"] == "close"
curve("pick", 303, 503)
closed_stroke = strokes(scene)[-1]
assert anchors_of(closed_stroke)[0] == anchors_of(closed_stroke)[-1] and COMMITS[-1] == "Close Curve"
curve("hover", 303, 503)
assert ct._SESSIONS["main"]["target"] is None             # a closed line has no ends
print("4) lines join into one, a line closes on its first anchor")

# 5) Undo while drawing takes the last anchor back and drawing goes on from the
#    one before; a line erased by something else stops offering its ends.
curve("pick", 100, 650)
curve("pick", 200, 600)
snap = scene.snapshot()
curve("pick", 300, 680)
scene.restore(snap)
ct._on_history({"event": "historyrestore", "view": "main"})
assert ct._SESSIONS["main"]["active"]["anchors"][-1] == (200, 600)
curve("pick", 260, 700)
last = strokes(scene)[-1]
assert anchors_of(last) == [(100, 650), (200, 600), (260, 700)]
curve("context")
del strokes(scene)[-1]
curve("hover", 262, 702)
assert ct._SESSIONS["main"]["target"] is None
print("5) undo keeps drawing from the restored line; an erased line is forgotten")

# 5b) Review fixes: undo after resuming from the head keeps drawing from the
#     head; undoing the second anchor leaves the first placed; a carried-on
#     line keeps its colour; a handle moved by another tool (anchors kept)
#     means the line is no longer offered.
scene = Scene()
__main__.main_model = scene
curve("pick", 100, 100)
curve("pick", 200, 50)
curve("pick", 300, 100)
curve("context")
strokes(scene)[0].color = (200, 0, 0, 255)            # drawn red earlier
curve("hover", 102, 102)
curve("pick", 102, 102)                                # resume at the head (100, 100)
snap = scene.snapshot()
curve("pick", 40, 160)
assert strokes(scene)[0].color == (200, 0, 0, 255)
scene.restore(snap)
ct._on_history({"event": "historyrestore", "view": "main"})
assert ct._SESSIONS["main"]["active"]["anchors"][-1] == (100, 100)
curve("pick", 60, 200)
assert anchors_of(strokes(scene)[0]) == [(300, 100), (200, 50), (100, 100), (60, 200)]
curve("context")
curve("pick", 500, 500)
snap = scene.snapshot()
curve("pick", 600, 450)
scene.restore(snap)
ct._on_history({"event": "historyrestore", "view": "main"})
assert ct._SESSIONS["main"]["active"]["anchors"] == [(500, 500)]
curve("context")
edited = strokes(scene)[0]
cmd = next(c for c in edited.commands if c["type"] == "cubic")
cmd["control1"] = {"x": 150.0, "y": 20.0}
curve("hover", 62, 202)
assert ct._SESSIONS["main"]["target"] is None
print("5b) undo direction, lone first anchor, colour kept, edited lines dropped")


# --- Round Corner -------------------------------------------------------------

def rnd(phase, x=0.0, y=0.0, **kw):
    message = msg(rc.PROPERTY, phase, x, y, **kw)
    rc._on_handle(message)
    return message


def polyline(points):
    commands = [{"type": "move", "to": points[0]}]
    commands += [{"type": "line", "to": p} for p in points[1:]]
    return make_stroke_object_from_path(commands, points, (0, 0, 0, 255), 4.0, 7)


def knob_of(scene, row=0, layer=0, joint=0):
    """Where the held end of a corner's control line is drawn: from the drag in
    progress, else from what the document remembers."""
    drag = (rc._SESSIONS.get("main") or {}).get("drag")
    if drag is not None:
        line, rounds = drag["line"], drag["rounds"]
    else:
        line = [l for l in rc._lines(scene, row) if joint in l["rounds"]][0]
        rounds = line["rounds"]
    corner = rc.rounded(line["base"], line["closed"], rounds)["corners"][joint]
    s = rounds[joint]["s"]
    half = ((corner["cuts"][1][0] - corner["cuts"][0][0]) / 2 * s, (corner["cuts"][1][1] - corner["cuts"][0][1]) / 2 * s)
    return (corner["V"][0] + half[0], corner["V"][1] + half[1]), line


# 6) Paths and corners: lines, quads and closures read as cubics; a corner is a
#    tangent break, a smooth joint is not.
segs, closed = rc.segments_of(_normalized([{"type": "move", "to": (0, 0)}, {"type": "line", "to": (100, 0)},
                                           {"type": "line", "to": (100, 100)}, {"type": "line", "to": (0, 0)}]))
assert len(segs) == 3 and closed and rc.corner_joints(segs, closed) == [0, 1, 2]
smooth = ct.catmull_rom([(0, 0), (100, 50), (200, 0)])
assert rc.corner_joints(smooth, False) == []
assert rc.segments_of([{"type": "move", "to": {"x": 0, "y": 0}}, {"type": "move", "to": {"x": 1, "y": 1}}]) is None
print("6) segments and corners")

# 7) The control line: on straight legs the held end lands exactly on the
#    pointer, a level pull on a symmetric peak gives equal legs, either end
#    of the line gives the same rounding; curved legs reach the pointer too.
peak = [bezier.line_cubic((0, 100), (100, 0)), bezier.line_cubic((100, 0), (200, 100))]
view = rc.rounded(peak, False, {})
c = view["corners"][0]
legs = rc.legs_for(c, (130.0, 0.0))
assert abs(legs["L1"] - legs["L2"]) < 1e-6 and legs["s"] == 1
assert bezier.dist(rc._knob(c, legs["L1"], legs["L2"], 1), (130.0, 0.0)) < 0.05
other = rc.legs_for(c, (70.0, 0.0))
assert abs(other["L1"] - legs["L1"]) < 1e-6 and other["s"] == -1
for target in [(125, -8), (140, 10), (118, 5)]:
    l = rc.legs_for(c, target)
    assert bezier.dist(rc._knob(c, l["L1"], l["L2"], l["s"]), target) < 0.05
shifted = rc.legs_for(c, (125.0, -8.0), shift=True)
assert abs(shifted["L1"] - shifted["L2"]) < 1e-9
arc = ct.catmull_rom([(0, 200), (60, 60), (200, 0)])
bent = [arc[0], arc[1], bezier.line_cubic((200, 0), (320, 120))]
cb = rc.rounded(bent, False, {})["corners"][1]
for L1, L2 in [(20, 20), (60, 15), (10, 70), (90, 90)]:
    for s in (1, -1):
        target = rc._knob(cb, L1, L2, s)      # a place the curved legs can reach
        l = rc.legs_for(cb, target)
        assert bezier.dist(rc._knob(cb, l["L1"], l["L2"], l["s"]), target) < 0.1, (L1, L2, s)
far = rc.legs_for(c, (100000.0, 0.0))
assert far["L1"] <= c["avail1"] + 1e-9 and far["L2"] <= c["avail2"] + 1e-9
print("7) the held end of the control line sits on the pointer")

# 8) The gesture on a real stroke: drag rounds it in place (id, colour, width
#    kept), every join G1, one commit on release; a plain click changes nothing.
scene = Scene()
__main__.main_model = scene
scene.image_at(0, 0).add_stroke_object(polyline([(100, 400), (250, 150), (400, 400), (550, 150), (700, 400)]))
original = strokes(scene)[0].commands
COMMITS.clear()
rnd("arm")
rnd("hover", 252, 152)
assert rc._SESSIONS["main"]["target"]["s"] == 0
m = rnd("pick", 252, 152)
assert m.get("grab") == "round:drag"
rnd("release", 252, 152, handle="round:drag")
assert strokes(scene)[0].commands == original and COMMITS == []
m = rnd("pick", 250, 150)
for k in range(1, 9):
    rnd("move", 250 + 5 * k, 150, handle="round:drag")
rnd("release", 290, 150, handle="round:drag")
s = strokes(scene)[0]
assert COMMITS == ["Round Corner"] and s.id == 7 and s.width == 4.0
segs, closed = rc.segments_of(s.commands)
assert len(segs) == 5 and g1(segs[:3]) > 1 - 1e-6
knob, line = knob_of(scene)
assert bezier.dist(knob, (290, 150)) < 0.1
print("8) a drag rounds the corner in place, one commit")

# 9) The rounded corner is grabbed again by its control line without a jump;
#    dragging it back to where it started snaps back exactly; to the tip
#    snaps the corner sharp; a right click cancels; undo forgets the drag.
rnd("hover", *knob)
assert rc._SESSIONS["main"]["target"]["s"] == 1
rnd("pick", *knob)
rnd("move", knob[0] + 3, knob[1] - 1, handle="round:drag")
moved, _ = knob_of(scene)
assert bezier.dist(moved, (knob[0] + 3, knob[1] - 1)) < 0.1
rnd("move", knob[0] + 60, knob[1] + 40, handle="round:drag")
rnd("move", knob[0] + 4, knob[1] + 3, handle="round:drag")
assert rc._SESSIONS["main"]["drag"]["snap"]["kind"] == "start"
before_commits = len(COMMITS)
rnd("release", knob[0] + 4, knob[1] + 3, handle="round:drag")
assert len(COMMITS) == before_commits
again, _ = knob_of(scene)
assert bezier.dist(again, knob) < 1e-6
rnd("hover", *knob)
rnd("pick", *knob)
rnd("move", knob[0] - 30, knob[1], handle="round:drag")
rnd("move", 252, 151, handle="round:drag")
assert rc._SESSIONS["main"]["drag"]["snap"]["kind"] == "tip"
rnd("release", 252, 151, handle="round:drag")
assert len(rc.segments_of(strokes(scene)[0].commands)[0]) == 4
assert rc._lines(scene, 0)[0]["rounds"] == {}
rnd("hover", 400, 400)
rnd("pick", 400, 400)
rnd("move", 400, 330, handle="round:drag")
assert len(rc.segments_of(strokes(scene)[0].commands)[0]) == 5
rnd("context", 400, 330, handle="round:drag")
assert len(rc.segments_of(strokes(scene)[0].commands)[0]) == 4
rnd("release", 400, 330, handle="round:drag")
print("9) regrab, start and tip snaps, right-click cancel")

# 9b) A rounded stroke whose handle another tool moved (ends kept) is not
#     treated as rounded any more: its fillet cannot be re-adjusted from the
#     stale record, which would undo that edit.
rnd("hover", 550, 150)
rnd("pick", 550, 150)
for k in range(1, 6):
    rnd("move", 550 + 6 * k, 150, handle="round:drag")
rnd("release", 580, 150, handle="round:drag")
assert 2 in rc._lines(scene, 0)[0]["rounds"]       # the corner at (550, 150)
fillet_cmd = [c for c in strokes(scene)[0].commands if c["type"] == "cubic"][3]
fillet_cmd["control1"] = {"x": fillet_cmd["control1"]["x"] + 9.0, "y": fillet_cmd["control1"]["y"]}
assert rc._lines(scene, 0)[0]["rounds"] == {}
print("9b) a stroke edited elsewhere is no longer re-adjusted from its record")

# 10) Strokes another tool owns (a property) are never offered.
guide = polyline([(100, 600), (200, 500), (300, 600)])
guide.property = "h_center_line"
scene.image_at(0, 0).add_stroke_object(guide)
rc._model_changed({})
rnd("hover", 200, 500)
assert rc._SESSIONS["main"]["target"] is None
print("10) other tools' lines are left alone")

print("t_curve_round_tools: all passed")
