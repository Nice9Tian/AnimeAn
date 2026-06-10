import python_hooks


def addhange(function):
    return python_hooks.set_hook(function, linefinish=True)


def delhange(function=None):
    python_hooks.del_hook(function)


def linefinish(cell, stroke, stroke_info=None):
    payload = stroke_info if stroke_info is not None else stroke
    message = {
        "event": "linefinish",
        "tool": "pen",
        "cell": cell,
        "stroke": payload,
        "property": payload.get("property") if isinstance(payload, dict) else None,
    }
    return python_hooks.dispatch(message)


addchange = addhange
delchange = delhange
