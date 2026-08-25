"""Palette box: the seed, the scriptData round-trip, and the control dict
(user request 2026-08-25: colour palette control)."""
import json
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import palette_box as pb  # noqa: E402
import tool_colors  # noqa: E402


class FakeScene:
    """The scriptData pair script_store talks to, and nothing else."""

    def __init__(self, raw=""):
        self._script = raw

    def script_data(self):
        return self._script

    def set_script_data(self, value):
        self._script = value


def fresh():
    tool_colors._COLORS.clear()
    return FakeScene()


# 1) a document with no palette of its own opens on the seed
scene = fresh()
assert pb.swatches(scene) == ["#ff000000", "#ffffffff"]
print("1) default seed is black + white")


# 2) the seed picks up whatever the pen and the bucket already hold
scene = fresh()
tool_colors._COLORS["pen"] = (255, 105, 180, 255)
tool_colors._COLORS["fill"] = (0, 0, 0, 255)         # already in the seed
assert pb.swatches(scene) == ["#ff000000", "#ffffffff", "#ffff69b4"]
print("2) seed adopts remembered pen/fill colours without duplicating")


# 3) add / remove round-trip through the real script_store JSON
scene = fresh()
assert pb.add("#ff112233", scene) is True
stored = json.loads(scene.script_data())
assert stored["palette"]["swatches"][-1] == "#ff112233"
assert pb.swatches(scene) == ["#ff000000", "#ffffffff", "#ff112233"]
assert pb.remove("#ff112233", scene) is True
assert pb.swatches(scene) == ["#ff000000", "#ffffffff"]
assert json.loads(scene.script_data())["palette"]["swatches"] == ["#ff000000", "#ffffffff"]
print("3) add/remove persist through scriptData")


# 4) the store is namespaced: another tool's key survives a palette write
scene = FakeScene(json.dumps({"mapping_assets": {"keep": 1}}))
tool_colors._COLORS.clear()
pb.add("#ff445566", scene)
assert json.loads(scene.script_data())["mapping_assets"] == {"keep": 1}
print("4) palette writes preserve other tools' scriptData keys")


# 5) removing a seeded colour sticks (the write is what makes it stick)
scene = fresh()
assert pb.remove("#ffffffff", scene) is True
assert pb.swatches(scene) == ["#ff000000"]
print("5) removing a default colour persists")


# 6) a duplicate add changes nothing, and neither does removing a stranger
scene = fresh()
pb.add("#ff112233", scene)
before = scene.script_data()
assert pb.add("#ff112233", scene) is False
assert pb.add("#FF112233", scene) is False           # case folds to one entry
assert scene.script_data() == before
assert pb.remove("#ff999999", scene) is False
assert scene.script_data() == before
print("6) duplicate add and missing remove are no-ops")


# 7) short hex and rgba tuples normalise to the one stored form
scene = fresh()
assert pb.add("#112233", scene) is True              # opaque by default
assert pb.swatches(scene)[-1] == "#ff112233"
assert pb.add((10, 20, 30, 40), scene) is True
assert pb.swatches(scene)[-1] == "#280a141e"
assert pb.add("not a colour", scene) is False
assert pb.add("#12345", scene) is False
print("7) hex/tuple forms normalise, junk is refused")


# 8) a damaged store falls back to the seed instead of raising
tool_colors._COLORS.clear()
for raw in ('{"palette": {"swatches": "nope"}}',
            '{"palette": ["#ff000000"]}',
            '{"palette": {}}',
            'not json at all',
            '[]'):
    assert pb.swatches(FakeScene(raw)) == ["#ff000000", "#ffffffff"], raw
# a partly damaged list keeps the entries that parse
assert pb.swatches(FakeScene(
    '{"palette": {"swatches": ["#ff000000", 17, "#zzz", "#ff112233"]}}')) == \
    ["#ff000000", "#ff112233"]
print("8) malformed store falls back to defaults")


# 9) an emptied box stays empty - that is not the same as an absent store
scene = fresh()
pb._write(scene, [])
assert pb.swatches(scene) == []
print("9) an emptied box is not re-seeded")


# 10) the control dict is what ToolOptPanel's palette factory reads
tool_colors._COLORS.clear()
tool_colors._COLORS["pen"] = (0, 0, 255, 255)
control = pb.control({}, 0)
assert control["type"] == "palette"
assert control["name"] == "palette"
assert control["hook"] == "color"                     # C++ applies the pick
assert control["value"] == "#ff0000ff"
assert control["row"] == 0
assert (control["start_column"], control["end_column"]) == (0, 2)
assert isinstance(control["swatches"], list)
assert all(isinstance(entry, str) and entry.startswith("#") for entry in control["swatches"])
tool_colors._COLORS.clear()
assert pb.control({}, 3)["value"] == "#ff000000"      # no memory -> black
assert pb.control({}, 3)["row"] == 3
print("10) control() reports type/hook/value/swatches/placement")


# 10b) the seed follows the TOOL's colour slot. The pen and the bucket keep
# separate colours, and the seeded value is what the chip, the hex readout and
# every picker open on - seeding the pen on the Fill panel would show, and
# commit, a colour the bucket is not painting with.
tool_colors._COLORS.clear()
tool_colors._COLORS["pen"] = (255, 0, 0, 255)
tool_colors._COLORS["fill"] = (0, 0, 255, 255)
assert pb.control({}, 0)["value"] == "#ffff0000"              # default slot: pen
assert pb.control({}, 0, "pen")["value"] == "#ffff0000"
assert pb.control({}, 0, "fill")["value"] == "#ff0000ff"
print("10b) control() seeds from the tool's own colour slot")


# 11) toolcontrol's Pen and Fill rows carry the palette, nobody else does
import toolcontrol  # noqa: E402

for tool in ("pen", "fill"):
    controls = toolcontrol.options_for_tool(tool, {})["controls"]
    assert controls[0]["type"] == "palette", tool
    assert controls[0]["row"] == 0, tool
    rows = [c["row"] for c in controls]
    assert rows == sorted(rows) and len(set(rows)) == len(rows), tool
for tool in ("eraser", "arrow", "connect", "transfer"):
    types_seen = {c["type"] for c in toolcontrol.options_for_tool(tool, {})["controls"]}
    assert "palette" not in types_seen, tool
# ... and each of the two panels opens on its OWN colour, not the pen's twice.
assert toolcontrol.options_for_tool("pen", {})["controls"][0]["value"] == "#ffff0000"
assert toolcontrol.options_for_tool("fill", {})["controls"][0]["value"] == "#ff0000ff"
print("11) palette sits on Pen and Fill only, one control per row")


# 12) the option hook: "palette_box" acts, "color" is left to tool_colors
scene = fresh()
pb.add("#ff112233", scene)
calls = []
real_scene = pb._scene
real_refresh = pb._refresh
pb._scene = lambda required=True: scene
pb._refresh = lambda: calls.append(1)
try:
    pb._option_changed({"hook": "color", "value": "#ffaabbcc"})
    assert pb.swatches(scene) == ["#ff000000", "#ffffffff", "#ff112233"]
    assert not calls
    pb._option_changed({"hook": "palette_box", "value": "add:#ffaabbcc"})
    assert pb.swatches(scene)[-1] == "#ffaabbcc"
    assert len(calls) == 1
    pb._option_changed({"hook": "palette_box", "value": "remove:#ffaabbcc"})
    assert "#ffaabbcc" not in pb.swatches(scene)
    assert len(calls) == 2
    # a repeat of either changes nothing, so the panel is not rebuilt for it
    pb._option_changed({"hook": "palette_box", "value": "remove:#ffaabbcc"})
    pb._option_changed({"hook": "palette_box", "value": "wobble:#ffaabbcc"})
    assert len(calls) == 2
finally:
    pb._scene = real_scene
    pb._refresh = real_refresh

print("12) palette_box hook adds/removes and refreshes only on a change")

print("t_palette: ALL OK")
