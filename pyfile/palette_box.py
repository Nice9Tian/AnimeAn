"""The saved colour set behind the tool panel's palette.

tool_colors answers "which colour does THIS tool draw with"; this module
answers the sibling question "which colours are on offer at all". Splitting
them keeps each one small: a pick travels down the existing "color" hook and
tool_colors records it exactly as it always did, while adding or removing a
swatch travels on "palette_box" and never touches the draw layer.

The C++ control is a mechanism - a chip, four picking surfaces and a grid of
whatever swatches it was handed. It has no opinion about what the set
contains, how long it lives, or what a fresh document starts with; all three
are decided here.

Scope: the set is stored in the scene's scriptData (script_store key
"palette"), so it travels with the document. That is the only persistence
these modules have, and a palette is a property of the artwork being made
rather than of the machine it is made on.
"""

import python_hooks

STORE_KEY = "palette"
OPTION_HOOK = "palette_box"
VALUE_HOOK = "color"
CONTROL_NAME = "palette"

# Black and white are the two a drawing always needs; the rest of a fresh
# box is whatever the pen and the bucket are already set to, so a document
# opens holding the colours in use rather than a stranger's defaults.
_DEFAULT_SWATCHES = ("#ff000000", "#ffffffff")

_FALLBACK_COLOR = "#ff000000"


def _animean():
    import animean_python

    return animean_python


def _scene(required=True):
    """The MAIN scene: the palette belongs to the document, not to a board.

    Same lookup visibility_tool and auto_mapping use - the injected global
    first, the registered scene list second.
    """
    try:
        import __main__

        model = getattr(__main__, "main_model", None)
        if model is not None:
            return model

        for info in _animean().get_scene():
            if info.get("sceneName") == "main_paint_view":
                return info["scene"]
    except Exception:
        if required:
            raise
        return None
    if required:
        raise RuntimeError("scene for view 'main' is not registered")
    return None


def _normalize(value):
    """Anything a caller might hold -> '#aarrggbb', or None.

    Accepts the C++ control's '#AARRGGBB', a plain '#RRGGBB', and the
    (r, g, b, a) tuples tool_colors caches.
    """
    if isinstance(value, (tuple, list)) and len(value) >= 3:
        try:
            parts = [int(v) for v in value[:4]]
        except (TypeError, ValueError):
            return None
        while len(parts) < 4:
            parts.append(255)
        r, g, b, a = (max(0, min(255, v)) for v in parts[:4])
        return f"#{a:02x}{r:02x}{g:02x}{b:02x}"
    if isinstance(value, dict):
        try:
            return _normalize((value["r"], value["g"], value["b"], value.get("a", 255)))
        except (KeyError, TypeError):
            return None
    text = str(value or "").strip().lower()
    if not text.startswith("#"):
        return None
    digits = text[1:]
    if len(digits) == 6:
        digits = "ff" + digits
    if len(digits) != 8:
        return None
    try:
        int(digits, 16)
    except ValueError:
        return None
    return "#" + digits


def _default_swatches():
    values = []
    for entry in _DEFAULT_SWATCHES:
        text = _normalize(entry)
        if text and text not in values:
            values.append(text)
    try:
        import tool_colors

        for key in ("pen", "fill"):
            text = _normalize(tool_colors.color_for(key))
            if text and text not in values:
                values.append(text)
    except Exception:
        pass
    return values


def _stored(scene):
    """The saved list, or None when there is nothing usable to read.

    None is not the same as an empty box: a damaged or absent store has to
    fall back to the seed, while a box the user emptied stays empty.
    """
    if scene is None:
        return None
    try:
        import script_store

        data = script_store.read(scene, STORE_KEY, None)
    except Exception:
        return None
    if not isinstance(data, dict):
        return None
    raw = data.get("swatches")
    if not isinstance(raw, (list, tuple)):
        return None
    values = []
    for entry in raw:
        text = _normalize(entry)
        if text and text not in values:
            values.append(text)
    return values


def _write(scene, values):
    import script_store

    script_store.write(scene, STORE_KEY, {"swatches": list(values)})


def swatches(scene=None):
    """The colours the box offers right now."""
    if scene is None:
        scene = _scene(required=False)
    stored = _stored(scene)
    if stored is None:
        return _default_swatches()
    return stored


def add(color, scene=None):
    """Save one colour. A duplicate is a no-op, not an error."""
    text = _normalize(color)
    if not text:
        return False
    if scene is None:
        scene = _scene()
    values = swatches(scene)
    if text in values:
        return False
    _write(scene, values + [text])
    return True


def remove(color, scene=None):
    """Drop one colour. Removing what is not there is harmless."""
    text = _normalize(color)
    if not text:
        return False
    if scene is None:
        scene = _scene()
    values = swatches(scene)
    if text not in values:
        return False
    # Writes even when the list came from the seed: without that, removing a
    # default would be undone by the next read.
    _write(scene, [entry for entry in values if entry != text])
    return True


def current_color(key="pen"):
    """The colour the chip opens on."""
    try:
        import tool_colors

        text = _normalize(tool_colors.color_for(key))
        if text:
            return text
    except Exception:
        pass
    return _FALLBACK_COLOR


def control(state=None, row=0):
    """The palette control for the tool options grid.

    The value hook stays "color" so the pick reaches the draw layer through
    the path that already exists (C++ arms Fill or Pen, then paints, and
    tool_colors records it). Only the box edits need this module.
    """
    return {
        "name": CONTROL_NAME,
        "type": "palette",
        "hook": VALUE_HOOK,
        "value": current_color(),
        "swatches": list(swatches()),
        "row": row,
        "start_column": 0,
        "end_column": 2,
    }


def _refresh():
    try:
        _animean().ui.refresh_tool_options()
    except Exception:
        pass


def _option_changed(message):
    if message.get("hook") != OPTION_HOOK:
        # Hook "color" is the drawing colour itself: C++ applies it and
        # tool_colors records it against the armed tool. The box has nothing
        # to add, and acting here would fight both.
        return
    verb, _, value = str(message.get("value") or "").partition(":")
    try:
        if verb == "add":
            changed = add(value)
        elif verb == "remove":
            changed = remove(value)
        else:
            return
    except Exception as error:
        print(f"[palette_box] {verb} failed: {error}")
        return
    if changed:
        _refresh()


python_hooks.set_hook(_option_changed, option=True)
