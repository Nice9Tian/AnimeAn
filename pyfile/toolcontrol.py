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
    elif tool in ("auto_mapping", "auto_mapping_2"):
        # Debug aid: overlays the mapping's 3x3 anchor grid in both views so
        # a wrong mapping is visible at a glance.
        try:
            import auto_mapping
            current = "on" if auto_mapping.refer_rect_enabled() else "off"
        except Exception:
            current = "off"
        controls = [
            {
                "name": "refer_rect",
                "type": "list",
                "title": "Refer Rect",
                "hook": "refer_rect",
                "value": current,
                "row": 0,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "Off", "value": "off"},
                    {"title": "On", "value": "on"},
                ],
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
