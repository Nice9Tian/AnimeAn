import json


def _color_options():
    return [
        {
            "title": "Black",
            "value": "black",
            "state": {"color": "black"},
        },
        {
            "title": "Blue",
            "value": "blue",
            "state": {"color": "blue"},
        },
        {
            "title": "Green",
            "value": "green",
            "state": {"color": "green"},
        },
    ]


def _color_control():
    return {
        "name": "color",
        "type": "button_row",
        "title": "Color",
        "hook": "color",
        "options": _color_options(),
    }


def _slider(name, title, hook, minimum, maximum, value):
    return {
        "name": name,
        "type": "slider",
        "title": title,
        "hook": hook,
        "min": minimum,
        "max": maximum,
        "value": value,
    }


def options_for_tool(tool, state=None):
    state = state or {}
    tool = str(tool).lower()

    if tool == "fill":
        return [
            _color_control(),
            {
                "name": "fill_scope",
                "type": "list",
                "title": "Fill Scope",
                "hook": "fill_scope",
                "value": state.get("fill_scope", "current"),
                "height": 62,
                "options": [
                    {"title": "ALL", "value": "all"},
                    {"title": "Current", "value": "current"},
                ],
            },
        ]

    if tool in ("eraser", "delete_line", "deleteline"):
        return [
            {
                "name": "eraser_mode",
                "type": "list",
                "title": "Eraser Mode",
                "hook": "eraser_mode",
                "value": "line" if tool in ("delete_line", "deleteline") else "area",
                "height": 62,
                "options": [
                    {"title": "LineMode", "value": "line"},
                    {"title": "AreaMode", "value": "area"},
                ],
            },
        ]

    if tool == "move":
        return []

    return [
        _color_control(),
        _slider("smooth", "Smooth", "smooth", 0, 100, int(state.get("smooth", 50))),
        _slider("pen_width", "Width", "pen_width", 1, 50, int(state.get("pen_width", 5))),
    ]


def options_for_tool_json(tool, state=None):
    return json.dumps(options_for_tool(tool, state), ensure_ascii=False)
