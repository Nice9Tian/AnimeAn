"""The three drawing parameters, and the Draw menu that exposes them.

A stroke is made in two stages, and they are not the same knob:

  1. REALTIME STABILIZATION - the 1 Euro filter smooths the incoming samples
     while the pen is down. It decides how faithful the result is to what the
     hand MEANT, and how much lag the hand feels.
  2. FITTING - once the pen lifts, the samples become chords and cubic
     Beziers. It decides how ECONOMICALLY the shape is described (node count)
     and how eagerly a turn counts as a corner.

Measured over their full ranges the two are near-orthogonal: filter strength
moves fidelity and barely touches node count, fit strength moves node count
and barely touches fidelity. One slider driving both could only ever walk the
diagonal of that square, so combinations a user actually wants - "my hand
shakes, but keep the geometry tight", "my hand is steady, but give me few
nodes to edit" - were unreachable.

The tool panel keeps ONE slider (the stabilizer), because that is the one a
user reaches for mid-drawing. The other two live here, in a settings window
off the Draw menu, where they belong: they are document-shaping preferences,
not per-stroke gestures.
"""

import python_hooks

MENU_NAME = "draw"
SETTINGS_NAME = "draw_settings"
OPTION_HOOK = "draw_settings"

# 50/50/50 reproduces the behaviour that shipped before the split.
_SETTINGS = {
    "stabilizer": 50,
    "simplify": 50,
    "corner": 50,
}


def settings():
    return dict(_SETTINGS)


def stabilizer():
    return _SETTINGS["stabilizer"]


def _apply():
    """Push all three into C++, which owns the algorithms that read them."""
    try:
        import animean_python

        animean_python.ui.set_draw_settings(
            int(_SETTINGS["stabilizer"]),
            int(_SETTINGS["simplify"]),
            int(_SETTINGS["corner"]),
        )
        return True
    except ImportError:
        return False       # embedded module not ready yet
    except Exception as error:
        print(f"[draw_settings] could not apply: {error}")
        return False


def set_value(key, value):
    """Set one parameter (0-100) and push the set. Returns True if changed."""
    if key not in _SETTINGS:
        return False
    try:
        clamped = max(0, min(100, int(value)))
    except (TypeError, ValueError):
        return False
    if _SETTINGS[key] == clamped:
        return False
    _SETTINGS[key] = clamped
    _apply()
    return True


def _slider(name, title, value, row):
    return {
        "name": name,
        "type": "slider",
        "title": title,
        "hook": OPTION_HOOK,
        "min": 0,
        "max": 100,
        "value": int(value),
        "row": row,
        "start_column": 0,
        "end_column": 2,
    }


def _settings_layout():
    """Re-evaluated every time the window opens, so it shows the truth."""
    return {
        "row_spacing": 8,
        "column_spacing": 6,
        "controls": [
            _slider("stabilizer", "Stabilizer  (anti-shake while drawing)",
                    _SETTINGS["stabilizer"], 0),
            _slider("simplify", "Simplify  (fewer nodes, looser fit)",
                    _SETTINGS["simplify"], 1),
            _slider("corner", "Corner sensitivity  (sharper angles)",
                    _SETTINGS["corner"], 2),
        ],
    }


def _menu_items():
    return [
        {
            "name": "draw_setting",
            "title": "Draw Setting...",
            # "settings" is declarative: C++ opens the named window itself
            # rather than calling back into Python while the menu is still up.
            "kind": "settings",
            "settings": SETTINGS_NAME,
        },
    ]


def note_value(key, value):
    """Record a value WITHOUT pushing it back to C++.

    For changes that already came from the UI: the tool panel's Stabilizer
    slider is handled natively by C++ (it emits smoothValueChanged), so
    re-applying here would bounce a rebuild of the panel back at the user
    mid-drag. This only keeps the settings window honest about what the
    slider already did.
    """
    if key not in _SETTINGS:
        return False
    try:
        clamped = max(0, min(100, int(value)))
    except (TypeError, ValueError):
        return False
    changed = _SETTINGS[key] != clamped
    _SETTINGS[key] = clamped
    return changed


def _option_changed(message):
    hook = message.get("hook")
    if hook == "smooth":
        # The tool panel's one drawing slider IS the stabilizer. C++ has
        # already applied it; just remember it so the Draw Setting window
        # opens showing the same number.
        note_value("stabilizer", message.get("value"))
        return
    if hook != OPTION_HOOK:
        return
    name = message.get("name")
    if name not in _SETTINGS:
        return
    if set_value(name, message.get("value")):
        print(f"[draw_settings] {name} -> {_SETTINGS[name]}")


python_hooks.register_menu({
    "name": MENU_NAME,
    "title": "Draw",
    "items": _menu_items,
})
python_hooks.register_settings(SETTINGS_NAME, _settings_layout)
python_hooks.set_hook(_option_changed, option=True)

# The tool panel's Smooth slider is the SAME stabilizer value, so seed C++
# with the defaults at import: without this the fit would run at its own
# struct defaults until the window was opened once.
_apply()
