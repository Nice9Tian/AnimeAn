import json


def _color_controls():
    return [
        {
            "name": "color",
            "type": "button",
            "title": "Black",
            "hook": "color",
            "value": "black",
            "row": 0,
            "column": 0,
            "state": {"color": "black"},
        },
        {
            "name": "color",
            "type": "button",
            "title": "Blue",
            "hook": "color",
            "value": "blue",
            "row": 0,
            "column": 1,
            "state": {"color": "blue"},
        },
        {
            "name": "color",
            "type": "button",
            "title": "Green",
            "hook": "color",
            "value": "green",
            "row": 0,
            "column": 2,
            "state": {"color": "green"},
        },
    ]


def _slider(name, title, hook, minimum, maximum, value, row):
    return {
        "name": name,
        "type": "slider",
        "title": title,
        "hook": hook,
        "min": minimum,
        "max": maximum,
        "value": value,
        "row": row,
        "start_column": 0,
        "end_column": 2,
    }


def options_for_tool(tool, state=None):
    state = state or {}
    tool = str(tool).lower()

    if tool == "fill":
        controls = [
            *_color_controls(),
            {
                "name": "fill_scope",
                "type": "list",
                "title": "Fill Scope",
                "hook": "fill_scope",
                "value": state.get("fill_scope", "current"),
                "row": 1,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "ALL", "value": "all"},
                    {"title": "Current", "value": "current"},
                ],
            },
        ]
    elif tool in ("eraser", "delete_line", "deleteline"):
        controls = [
            {
                "name": "eraser_mode",
                "type": "list",
                "title": "Eraser Mode",
                "hook": "eraser_mode",
                "value": "line" if tool in ("delete_line", "deleteline") else "area",
                "row": 0,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "LineMode", "value": "line"},
                    {"title": "AreaMode", "value": "area"},
                ],
            },
        ]
    elif tool in ("move", "arrow"):
        controls = []
    else:
        controls = [
            *_color_controls(),
            _slider("smooth", "Smooth", "smooth", 0, 100, int(state.get("smooth", 50)), 1),
            _slider("pen_width", "Width", "pen_width", 1, 50, int(state.get("pen_width", 5)), 2),
        ]

    return {
        "row_spacing": 8,
        "column_spacing": 6,
        "controls": controls,
    }


def options_for_tool_json(tool, state=None):
    return json.dumps(options_for_tool(tool, state), ensure_ascii=False)


def options_for_extra_tool(tool, state=None):
    """Tool options for ExtraTools (script tools). Unknown tools get none."""
    state = state or {}
    tool = str(tool).lower()

    if tool in ("h_center_line", "v_center_line"):
        # Center lines are pen strokes, so they honour the same smoothing and
        # width parameters as the pen tool.
        controls = [
            _slider("smooth", "Smooth", "smooth", 0, 100, int(state.get("smooth", 50)), 0),
            _slider("pen_width", "Width", "pen_width", 1, 50, int(state.get("pen_width", 5)), 1),
        ]
    elif tool == "auto_mapping_2":
        # Curve Mode picks how the mapped strokes' geometry is rebuilt after the
        # (non-linear) warp; RDP is the decimation tolerance for the samples
        # inserted between original points (0.1px units; originals are never
        # decimated); Refer Rect overlays the mapping's 3x3 anchor grid in both
        # views so a wrong mapping is visible at a glance.
        try:
            import auto_mapping
            mode = auto_mapping.curve_mode()
            rdp_tenths = int(round(auto_mapping.rdp_eps() * 10))
            refer = "on" if auto_mapping.refer_rect_enabled() else "off"
            split = "on" if auto_mapping.fold_split_enabled() else "off"
            seal = "on" if auto_mapping.fold_seal_enabled() else "off"
            shade = max(0, min(100, int(round((auto_mapping.fold_back_color()[0] - 20) / 2.0))))
        except Exception:
            mode = "spline"
            rdp_tenths = 3
            refer = "off"
            split = "on"
            seal = "on"
            shade = 45
        controls = [
            {
                "name": "curve_mode",
                "type": "list",
                "title": "Curve Mode",
                "hook": "curve_mode",
                "value": mode,
                "row": 0,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "Spline", "value": "spline"},
                    {"title": "Bezier", "value": "bezier"},
                    {"title": "Polyline", "value": "polyline"},
                ],
            },
            {
                # RDP decimation exists in the sampled modes (spline/polyline):
                # hide the label and slider on bezier so nobody assumes the
                # handle-transport route uses it too.
                **_slider("rdp_eps", "RDP (x0.1px)", "rdp_eps", 1, 20, rdp_tenths, 1),
                "visible_when": {"name": "curve_mode", "values": ["spline", "polyline"]},
            },
            {
                "name": "refer_rect",
                "type": "check",
                "title": "Refer Rect",
                "hook": "refer_rect",
                "value": refer,
                "row": 2,
                "start_column": 0,
                "end_column": 2,
            },
            # Where the map turns orientation-reversing (a fold past ~135 deg
            # or a U-turn) the pattern is mirrored - that is the BACK of the
            # fold. Split sends it to its own layer with a lining colour;
            # Crease draws the fold line so hiding the back still reads.
            {
                "name": "fold_split",
                "type": "check",
                "title": "Front/Back Split",
                "hook": "fold_split",
                "value": split,
                "row": 3,
                "start_column": 0,
                "end_column": 2,
            },
            {
                "name": "fold_seal",
                "type": "check",
                "title": "Crease Line",
                "hook": "fold_seal",
                "value": seal,
                "row": 4,
                "start_column": 0,
                "end_column": 2,
                "visible_when": {"name": "fold_split", "values": ["on"]},
            },
            {
                **_slider("back_shade", "Lining Shade", "back_shade", 0, 100, shade, 5),
                "visible_when": {"name": "fold_split", "values": ["on"]},
            },
        ]
    else:
        controls = []

    return {
        "row_spacing": 8,
        "column_spacing": 6,
        "controls": controls,
    }


def options_for_extra_tool_json(tool, state=None):
    return json.dumps(options_for_extra_tool(tool, state), ensure_ascii=False)
