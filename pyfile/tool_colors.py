"""Per-tool drawing colour: the cache and the policy.

Every tool remembers the colour it was last used with, and gets that colour
back when it is armed again. Without this there is ONE drawing colour for the
whole app, and a script tool that paints itself (the Mapping Area's blue, an
Additional Line's pink) silently repaints the pen and the fill bucket - the
reported "switching back to the brush gives me the Area Fill colour".

C++ stays a mechanism: it applies a colour (ui.set_draw_color) and announces
that a tool was armed (the "handle" event, phase "arm", carrying the base
tool and the stroke property). Which colour that tool should get is decided
here.

The identity of a tool, for colour purposes, is its stroke PROPERTY when it
has one (each script tool owns a property) and its base tool name otherwise -
so the Pen, the Fill bucket, the Mapping Area and the Additional Line each
keep their own colour, while every plain-pen gesture shares one.
"""

import python_hooks

# tool key -> (r, g, b, a)
_COLORS = {}
# The armed tool, as last announced. Colour picks and script-set colours are
# recorded against this.
_CURRENT = {"key": "pen"}


def _animean():
    import animean_python
    return animean_python


def _key(tool, property_value):
    prop = (property_value or "").strip()
    if prop:
        return f"property:{prop}"
    return (tool or "pen").strip() or "pen"


def current_key():
    return _CURRENT["key"]


def _parse_color(value):
    """Accepts {"r","g","b","a"}, (r,g,b[,a]), '#aarrggbb', '#rrggbb' or a
    colour name."""
    if isinstance(value, dict):
        try:
            return (int(value["r"]), int(value["g"]), int(value["b"]),
                    int(value.get("a", 255)))
        except (KeyError, TypeError, ValueError):
            return None
    if isinstance(value, (tuple, list)) and len(value) >= 3:
        parts = [int(v) for v in value[:4]]
        while len(parts) < 4:
            parts.append(255)
        return tuple(parts)
    text = str(value or "").strip()
    if not text:
        return None
    if text.startswith("#"):
        digits = text[1:]
        try:
            if len(digits) == 8:
                a, r, g, b = (int(digits[i:i + 2], 16) for i in (0, 2, 4, 6))
                return (r, g, b, a)
            if len(digits) == 6:
                r, g, b = (int(digits[i:i + 2], 16) for i in (0, 2, 4))
                return (r, g, b, 255)
        except ValueError:
            return None
        return None
    named = {
        "black": (0, 0, 0, 255),
        "white": (255, 255, 255, 255),
        "red": (255, 0, 0, 255),
        "green": (0, 128, 0, 255),
        "blue": (0, 0, 255, 255),
        "yellow": (255, 255, 0, 255),
    }
    return named.get(text.lower())


def apply(color, key=None):
    """Draw with `color` from now on, and remember it for the armed tool.

    Script tools call this instead of ui.set_draw_color: setting a colour
    without recording it would leave the cache describing a colour the view
    is not using, and the next arm would fight the tool that just painted.
    """
    parsed = _parse_color(color)
    if parsed is None:
        return False
    slot = key or _CURRENT["key"]
    _COLORS[slot] = parsed
    try:
        _animean().ui.set_draw_color(parsed)
    except Exception:
        return False
    return True


def remember(color, key=None):
    """Record a colour for a tool WITHOUT applying it to the view."""
    parsed = _parse_color(color)
    if parsed is None:
        return False
    _COLORS[key or _CURRENT["key"]] = parsed
    return True


def color_for(key):
    return _COLORS.get(key)


def _armed(message):
    """A tool was armed: hand it back its own colour."""
    key = _key(message.get("base_tool"), message.get("property"))
    if key == _CURRENT["key"]:
        return
    outgoing = _CURRENT["key"]
    _CURRENT["key"] = key
    # The colour in force belongs to the tool being LEFT. Recording it here
    # is what stops a script tool's colour from sticking: without a baseline
    # the pen had no remembered colour of its own to be handed back, so it
    # simply kept painting in the Mapping Area's blue.
    current = _parse_color(message.get("color"))
    if current is not None and outgoing not in _COLORS:
        _COLORS[outgoing] = current
    remembered = _COLORS.get(key)
    if remembered is None:
        # First time this tool is armed: it inherits what is in force, and
        # that inheritance is recorded so the NEXT tool cannot take it away.
        if current is not None:
            _COLORS[key] = current
        return
    try:
        _animean().ui.set_draw_color(remembered)
    except Exception:
        pass


def _color_picked(message):
    """The colour swatch in the tool options.

    A pick ARMS a tool as well as colouring it: the C++ handler keeps the
    Fill bucket armed when it was armed, and arms the Pen otherwise. The
    colour therefore belongs to that tool, never to whichever script tool
    happened to be up when the swatch was clicked.
    """
    if message.get("hook") != "color":
        return
    parsed = _parse_color(message.get("value"))
    if parsed is None:
        return
    key = "fill" if (message.get("base_tool") or "") == "fill" else "pen"
    _COLORS[key] = parsed
    _CURRENT["key"] = key


def _handle_event(message):
    if message.get("phase") == "arm":
        _armed(message)


python_hooks.set_hook(_handle_event, handle=True)
python_hooks.set_hook(_color_picked, option=True)
