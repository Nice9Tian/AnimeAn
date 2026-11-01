"""Fold notes: a bubble where the mapping turns over. An axis doubling back gets it
at the turn, on that axis's board and nowhere else."""
import math
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, ROOT + r"\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
sys.modules["animean_python"].ui = types.SimpleNamespace(
    set_overlay=lambda *args: None, history_commit=lambda *args: None,
    refresh=lambda: None, refresh_tool_options=lambda: None)
import auto_mapping as am


def line(a, b, n=24):
    return [[a[0] + (b[0] - a[0]) * i / n, a[1] + (b[1] - a[1]) * i / n] for i in range(n + 1)]


def setup(child_v, main_v=None):
    am._MAPPING_ASSETS.clear()
    am._GRID.update(divisions=5, extend=0)
    am._invalidate_grid_cache()
    for view, v in (("child", child_v), ("main", main_v or line((0.0, -100.0), (0.0, 100.0)))):
        assets = am._assets_for(view)
        assets[am.H_PROPERTY] = {"points": line((-150.0, 0.0), (150.0, 0.0)), "width": 3.0}
        assets[am.V_PROPERTY] = {"points": v, "width": 3.0}


def notes(view):
    return [i for i in am.overlay_items(view) if str(i.get("id")).startswith("fold_note:")]


STRAIGHT = line((0.0, -100.0), (0.0, 100.0))
HOOKED = STRAIGHT + [[0.0, 100.0 - 3.0 * i] for i in range(1, 7)]

# 1) Straight axes fold nowhere.
setup(STRAIGHT)
assert notes("main") == [] and notes("child") == []

# 2) An axis whose stroke doubles back at its end: one note, on the board the stroke
#    is on, pointing at the turn itself, saying which axis on which board.
setup(HOOKED)
child_note = notes("child")
assert notes("main") == [] and len(child_note) == 1
label = child_note[0]["label"]
assert "The V axis on the texture board" in label and "doubles back 18 px from its end" in label
assert child_note[0]["points"] == [(0.0, 100.0)]
assert not child_note[0]["removable"]

# 2b) A hook too short for the lattice to see still gets its note.
setup(STRAIGHT + [[0.0, 100.0 - 2.0 * i] for i in range(1, 5)])
assert "doubles back 8 px from its end" in notes("child")[0]["label"]

# 3) Taking the hook away takes the note away, and the same on the main board's
#    axis reports the main board.
setup(STRAIGHT)
assert notes("main") == [] and notes("child") == []
setup(STRAIGHT, HOOKED)
assert notes("child") == [] and "The V axis on the main board" in notes("main")[0]["label"]
assert notes("main")[0]["points"] == [(0.0, 100.0)]

# 4) A round U-turn is a turn back too: past the crossing this V curves round until
#    it heads back up. The note sits where its heading crosses the H axis's.
bend = [[40.0 - 40.0 * math.cos(t * 2.6 / 40), 40.0 * math.sin(t * 2.6 / 40)] for t in range(1, 41)]
setup(STRAIGHT, line((0.0, -100.0), (0.0, 0.0)) + bend)
found = notes("main")
assert notes("child") == [] and len(found) == 1
assert "The V axis on the main board" in found[0]["label"]
assert math.dist(found[0]["points"][0], (40.0, 40.0)) < 4.0

setup(line((0.0, -100.0), (0.0, 0.0)) + bend)
found = notes("child")
assert notes("main") == [] and len(found) == 1
assert "The V axis on the texture board" in found[0]["label"]

# 4b) Neither axis turns back, yet far out they cross each other's heading: nothing
#     to name, so the note says so on the main board, where the fold starts.
tilt = 62.0
right = [[80.0 + 70.0 * i / 20 * math.cos(math.radians(tilt)),
          70.0 * i / 20 * math.sin(math.radians(tilt))] for i in range(1, 21)]
down = [[70.0 * i / 20 * math.sin(math.radians(tilt)),
         60.0 + 70.0 * i / 20 * math.cos(math.radians(tilt))] for i in range(1, 21)]
setup(STRAIGHT)
am._assets_for("main")[am.H_PROPERTY]["points"] = line((-150.0, 0.0), (80.0, 0.0)) + right
am._assets_for("main")[am.V_PROPERTY]["points"] = line((0.0, -100.0), (0.0, 60.0)) + down
am._invalidate_grid_cache()
found = notes("main")
assert notes("child") == [] and len(found) == 1
assert found[0]["label"] == "Fold: the pattern turns over here"

# 5) Reaching past the axis ends widens the band, it does not add a second note,
#    and the regions are cached until the assets change.
setup(HOOKED)
am._GRID["extend"] = 1
am._invalidate_grid_cache()
assert len(notes("child")) == 1 and notes("child")[0]["points"] == [(0.0, 100.0)]
regions = am._fold_regions()
assert am._fold_regions() is regions
am._invalidate_grid_cache()
assert am._fold_regions() is not regions

# 6) A real stroke is a curve, whose arc table is per cubic and not per flattened
#    point: the same hook, drawn as cubics, is found and named the same way.


def P(p):
    return {"x": p[0], "y": p[1]}


def curve_spec(cubics):
    commands = [{"type": "move", "to": P(cubics[0][0])}]
    flat = [cubics[0][0]]
    for c in cubics:
        commands.append({"type": "cubic", "control1": P(c[1]), "control2": P(c[2]), "to": P(c[3])})
        flat.extend(am._cubic_point(c, k / 32) for k in range(1, 33))
    return {"points": flat, "commands": commands, "width": 3.0}


setup(STRAIGHT)
am._assets_for("child")[am.V_PROPERTY] = curve_spec([
    ((0.0, -100.0), (6.0, -40.0), (-6.0, 40.0), (0.0, 100.0)),
    ((0.0, 100.0), (0.0, 94.0), (0.0, 88.0), (0.0, 82.0))])
am._assets_for("child")[am.H_PROPERTY] = curve_spec([
    ((-150.0, 0.0), (-50.0, 8.0), (50.0, -8.0), (150.0, 0.0))])
am._invalidate_grid_cache()
assert am._current_mapper()[0] is not None
found = am._fold_regions()
assert len(found) == 1 and "The V axis on the texture board" in found[0]["text"]
assert "doubles back 18 px from its end" in found[0]["text"]
assert found[0]["view"] == "child" and math.dist(found[0]["point"], (0.0, 100.0)) < 0.01

# 7) Deleting an axis the contours own: for a moment there is no mapping, then the
#    contours put the axis back. Nothing may stay cached from that moment.
setup(HOOKED)
am._REFER_RECT.update(child=True, main=True)
rect = [(-150.0, -100.0), (150.0, -100.0), (150.0, 100.0), (-150.0, 100.0)]
ring = [[a[0] + (b[0] - a[0]) * i / 40, a[1] + (b[1] - a[1]) * i / 40]
        for a, b in zip(rect, rect[1:] + rect[:1]) for i in range(40)]
for view in ("child", "main"):
    am._assets_for(view)[am.PROXY_PROPERTY] = {"points": [list(p) for p in ring], "width": 3.0}
am._assets_for("main").pop(am.V_PROPERTY)
am._assets_for("main").pop(am.H_PROPERTY)
assert am._refresh_proxy_axes()
am._invalidate_grid_cache()
assert len(notes("child")) == 1 and am._grid_overlay_items("main")
am._overlay_removed(None, None, {"overlay": {"id": am.V_PROPERTY}, "view": "main"})
assert am._assets_for("main")[am.V_PROPERTY]["auto"]
assert len(notes("child")) == 1 and notes("main") == [] and am._grid_overlay_items("main")
am._REFER_RECT.update(child=False, main=False)

print("t_fold_notes: 7 cases ok")
