"""The mapping tools' shared options block + the texture sub-control row.

Round-2 G3: arming a guide tool (midline, h/v center line, mapping area,
additional line) used to give either an empty panel or two pen sliders, so the
auto-mapping run policy - which is GLOBAL state, not auto_mapping_2's property
- could only be reached by arming a different tool. Every mapping tool now
shows the same block, its own extras after it, and the texture board last.

The suite stubs auto_mapping/draw_settings: toolcontrol imports them lazily
inside the branch, which is exactly what makes the panel builder testable
without a scene (and what this file must keep true).
"""
import json
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))


# --- the stubs standing in for the two lazily-imported policy modules -------
class FakeAutoMapping(types.ModuleType):
    """Only the module-level, scene-free state readers toolcontrol calls."""

    def __init__(self):
        super().__init__("auto_mapping")
        self.mode = "bezier"
        self.eps = 0.3
        self.split = True
        self.seal = True
        self.bridge = False
        self.tension = 0.33

    def curve_mode(self):
        return self.mode

    def rdp_eps(self):
        return self.eps

    def fold_split_enabled(self):
        return self.split

    def fold_seal_enabled(self):
        return self.seal

    def bridge_enabled(self):
        return self.bridge

    def bridge_tension(self):
        return self.tension


FAKE = FakeAutoMapping()
sys.modules["auto_mapping"] = FAKE

FAKE_DRAW = types.ModuleType("draw_settings")
FAKE_DRAW.stabilizer = lambda: 42
sys.modules["draw_settings"] = FAKE_DRAW

import toolcontrol  # noqa: E402

MAPPING_TOOLS = ("midline", "h_center_line", "v_center_line",
                 "mapping_area", "additional_line")
PEN_TOOLS = ("midline", "h_center_line", "v_center_line", "additional_line")
BLOCK_NAMES = ["fold_split", "fold_seal", "bridge_topology", "bridge_tension"]


def controls(tool, state=None):
    return toolcontrol.options_for_extra_tool(tool, state or {})["controls"]


def names(tool, state=None):
    return [c["name"] for c in controls(tool, state)]


def rows_are_sane(items):
    """One control per row, starting at 0, no gaps, in ascending order."""
    rows = [c["row"] for c in items]
    return rows == sorted(rows) and rows == list(range(len(rows)))


# 1) every mapping tool carries the whole shared block, in one fixed order
FAKE.mode = "bezier"
for tool in MAPPING_TOOLS + ("auto_mapping_2",):
    got = names(tool)
    assert got[:4] == BLOCK_NAMES, (tool, got)
    # every block control names its hook after itself - which is what lets
    # auto_mapping._tool_option_changed dispatch on message["hook"] alone.
    block = {c["name"]: c for c in controls(tool) if c["name"] in BLOCK_NAMES}
    assert all(block[name]["hook"] == name for name in BLOCK_NAMES), tool
print("1) the shared block opens every mapping tool's panel")


# 2) the conditional rows keep their sibling wiring inside each panel
for tool in MAPPING_TOOLS + ("auto_mapping_2",):
    by_name = {c["name"]: c for c in controls(tool)}
    assert by_name["fold_seal"]["visible_when"] == {
        "name": "fold_split", "values": ["on"]}, tool
    assert by_name["bridge_tension"]["visible_when"] == {
        "name": "bridge_topology", "values": ["on"]}, tool
    # visible_when only resolves against a SIBLING: both watched controls
    # must live in this same panel or the rule silently hides the target.
    assert "fold_split" in by_name and "bridge_topology" in by_name, tool
print("2) fold_seal / bridge_tension watch siblings that are really present")


# 3) per-tool extras come AFTER the block; mapping_area has none
for tool in PEN_TOOLS:
    assert names(tool) == BLOCK_NAMES + ["smooth", "pen_width", "texture_view"], tool
assert names("mapping_area") == BLOCK_NAMES + ["texture_view"]
assert names("auto_mapping_2") == BLOCK_NAMES + ["texture_view"]
print("3) stabilizer/width follow the block; mapping_area and auto_mapping_2 have none")


# 4) the extras are the pen pair the guide tools used to show on their own
state = {"pen_width": 7, "smooth": 11}
for tool in PEN_TOOLS:
    by_name = {c["name"]: c for c in controls(tool, state)}
    smooth, width = by_name["smooth"], by_name["pen_width"]
    assert (smooth["type"], smooth["title"], smooth["hook"]) == \
        ("slider", "Stabilizer", "smooth"), tool
    assert (smooth["min"], smooth["max"], smooth["value"]) == (0, 100, 42), tool
    assert (width["type"], width["title"], width["hook"]) == \
        ("slider", "Width", "pen_width"), tool
    assert (width["min"], width["max"], width["value"]) == (1, 50, 7), tool
print("4) Stabilizer reads draw_settings, Width reads the shell's state")


# 5) the texture board is the last row, full width, and a subwindow control
for tool in MAPPING_TOOLS + ("auto_mapping_2",):
    items = controls(tool)
    last = items[-1]
    assert last["type"] == "subwindow", tool
    assert last["name"] == "texture_view", tool          # the registry key
    assert last["title"] == "Texture", tool
    assert (last["start_column"], last["end_column"]) == (0, 2), tool
    assert last["row"] == max(c["row"] for c in items), tool
    assert rows_are_sane(items), (tool, [c["row"] for c in items])
print("5) texture_view is the last, full-width row of every mapping panel")


# 6) rdp_eps tracks the menu-bar calculation mode: emitted only when sampled
for mode, expected in (("bezier", False), ("polyline", True), ("spline", True)):
    FAKE.mode = mode
    for tool in MAPPING_TOOLS + ("auto_mapping_2",):
        items = controls(tool)
        got = [c["name"] for c in items]
        assert ("rdp_eps" in got) is expected, (tool, mode, got)
        if expected:
            slider = items[0]
            assert slider["name"] == "rdp_eps" and slider["row"] == 0, (tool, mode)
            assert (slider["min"], slider["max"]) == (1, 20), (tool, mode)
        # and no gap is left where the slider is not
        assert rows_are_sane(items), (tool, mode)
FAKE.mode = "polyline"
FAKE.eps = 1.7
assert controls("auto_mapping_2")[0]["value"] == 17
FAKE.eps = 0.3
FAKE.mode = "bezier"
print("6) rdp_eps appears with the sampled modes only, rows close behind it")


# 7) auto_mapping_2's own panel is byte-for-byte what it was, plus the new row
FAKE.mode = "bezier"
FAKE.split, FAKE.seal, FAKE.bridge, FAKE.tension = True, False, True, 0.5
legacy = controls("auto_mapping_2")[:-1]
assert legacy == [
    {"name": "fold_split", "type": "check", "title": "Front/Back Split",
     "hook": "fold_split", "value": "on", "row": 0,
     "start_column": 0, "end_column": 2},
    {"name": "fold_seal", "type": "check", "title": "Crease Line",
     "hook": "fold_seal", "value": "off", "row": 1,
     "start_column": 0, "end_column": 2,
     "visible_when": {"name": "fold_split", "values": ["on"]}},
    {"name": "bridge_topology", "type": "check", "title": "补全拓扑",
     "hook": "bridge_topology", "value": "on", "row": 2,
     "start_column": 0, "end_column": 2},
    {"name": "bridge_tension", "type": "slider", "title": "Bridge k (% |AB|)",
     "hook": "bridge_tension", "min": 5, "max": 100, "value": 50, "row": 3,
     "start_column": 0, "end_column": 2,
     "visible_when": {"name": "bridge_topology", "values": ["on"]}},
], legacy
# the guide tools show that same block verbatim - same values, same rows
assert controls("mapping_area")[:-1] == legacy
assert controls("h_center_line")[:4] == legacy
FAKE.split, FAKE.seal, FAKE.bridge, FAKE.tension = True, True, False, 0.33
print("7) auto_mapping_2's block is unchanged and the guide tools mirror it")


# 8) each call builds fresh dicts: two panels must not share mutable controls
a = controls("midline")
b = controls("mapping_area")
assert a[0] is not b[0]
a[0]["value"] = "tampered"
assert controls("midline")[0]["value"] == "on"
print("8) panels are rebuilt, never handing out shared control dicts")


# 9) the block survives auto_mapping being unimportable (fallback defaults)
saved = sys.modules["auto_mapping"]
sys.modules["auto_mapping"] = None            # makes `import auto_mapping` raise
try:
    for tool in MAPPING_TOOLS + ("auto_mapping_2",):
        by_name = {c["name"]: c for c in controls(tool)}
        assert sorted(by_name) == sorted(BLOCK_NAMES + ["texture_view"]
                                         + (["smooth", "pen_width"]
                                            if tool in PEN_TOOLS else [])), tool
        assert by_name["fold_split"]["value"] == "on", tool
        assert by_name["fold_seal"]["value"] == "on", tool
        assert by_name["bridge_topology"]["value"] == "off", tool
        assert by_name["bridge_tension"]["value"] == 33, tool
        assert "rdp_eps" not in by_name, tool
finally:
    sys.modules["auto_mapping"] = saved
print("9) an unavailable auto_mapping still yields the block, on defaults")


# 10) tools outside the mapping family are untouched
for tool in ("fukusato_line", "fukusato_cut"):
    assert names(tool) == ["smooth", "pen_width"], tool
assert names("fukusato_guide_mapping") == [
    "fk_variant", "fk_alpha", "fk_beta", "fk_grid", "fk_samples"]
for tool in ("fukusato_line", "fukusato_cut", "fukusato_guide_mapping", "nonsense"):
    seen = {c["type"] for c in controls(tool)}
    assert "subwindow" not in seen, tool
assert controls("nonsense") == []
print("10) fukusato tools and unknown tools are unchanged")


# 11) the JSON the shell actually receives carries all of it
payload = json.loads(toolcontrol.options_for_extra_tool_json("additional_line", {}))
assert payload["row_spacing"] == 8 and payload["column_spacing"] == 6
assert [c["name"] for c in payload["controls"]] == \
    BLOCK_NAMES + ["smooth", "pen_width", "texture_view"]
assert any(c["title"] == "补全拓扑" for c in payload["controls"])   # ensure_ascii=False
print("11) options_for_extra_tool_json round-trips the whole panel")


# 12) the helper's start_row offset shifts the block without reshaping it
block, nxt = toolcontrol._automapping_controls(0)
shifted, shifted_next = toolcontrol._automapping_controls(5)
assert nxt == 4 and shifted_next == 9
assert [c["row"] for c in block] == [0, 1, 2, 3]
assert [c["row"] for c in shifted] == [5, 6, 7, 8]
assert [c["name"] for c in block] == [c["name"] for c in shifted] == BLOCK_NAMES
FAKE.mode = "spline"
sampled, sampled_next = toolcontrol._automapping_controls(2)
assert [c["row"] for c in sampled] == [2, 3, 4, 5, 6] and sampled_next == 7
FAKE.mode = "bezier"
print("12) _automapping_controls(start_row) offsets rows and reports the next")

print("t_toolcontrol: ALL OK")
