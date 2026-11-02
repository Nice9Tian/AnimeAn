"""The dialogs that are open, which one is on top, and whether it is modal.

This is program-level interface state, so it lives in Python (developer_guide.md,
"分工"): a module opens a dialog here and gets the user's answer back as a call.
C++ broadcasts the stack as the "dialogs" state and knows none of the kinds; the
page draws each dialog in a window of its own and blocks the other windows while
a modal one is open. A window closed from Windows, or a page that fails, comes
back as "closed" or "error"; C++ has already dropped that dialog from the state
by then, and a stale stack sent later cannot bring it back.

Kinds and their fields. The text is English and the page translates it.
  message     title, text, buttons            answer: the button, or None
  canvasSize  title, width, height            answer: {"width", "height"}, or None
  color       title, value, accept            answer: "#AARRGGBB", or None
  settings    title, name, layout             answer: None (the window just closes)
  file        title, mode, filters, multiple, defaultName
                                              answer: the paths, [] when cancelled
"""

import itertools
import json

import python_hooks

# Bottom to top. Each entry: {"spec": what the page sees, "on_result": callable}.
_STACK = []
_IDS = itertools.count(1)

# What a page may say about a dialog. Every one of them ends it.
ACTIONS = ("button", "accept", "cancel", "closed", "error")


def _ui():
    try:
        import animean_python

        return animean_python.ui
    except Exception:
        return None


def call(method, params=None):
    """A backend method, as the page would call it. Raises RuntimeError with the
    backend's message when the call fails."""
    ui = _ui()
    if ui is None or not hasattr(ui, "call"):
        raise RuntimeError("the backend is not running")
    return ui.call(method, dict(params or {}))


def stack():
    """The open dialogs, bottom to top, as the page sees them."""
    return [dict(entry["spec"]) for entry in _STACK]


def is_modal():
    return any(entry["spec"].get("modal", True) for entry in _STACK)


def is_open(dialog_id):
    return any(entry["spec"]["id"] == dialog_id for entry in _STACK)


def _publish():
    ui = _ui()
    publish = getattr(ui, "set_dialogs", None) if ui is not None else None
    if publish is not None:
        publish(json.dumps(stack()))


def show(kind, on_result=None, *, modal=True, **fields):
    """Put a dialog on top and return its id."""
    dialog_id = f"d{next(_IDS)}"
    spec = {"id": dialog_id, "kind": kind, "modal": bool(modal)}
    spec.update(fields)
    _STACK.append({"spec": spec, "on_result": on_result})
    _publish()
    return dialog_id


def close(dialog_id):
    """Take a dialog away without an answer. False when it is not open."""
    kept = [entry for entry in _STACK if entry["spec"]["id"] != dialog_id]
    if len(kept) == len(_STACK):
        return False
    _STACK[:] = kept
    _publish()
    return True


def message(title, text, buttons=("OK",), on_result=None):
    """Enter picks the first button; Escape and closing the window answer None."""
    return show("message", on_result, title=title, text=text, buttons=[str(b) for b in buttons])


def canvas_size(width, height, on_result, title="New Project"):
    return show("canvasSize", on_result, title=title, width=int(width), height=int(height))


def color(value, on_result, title="Select Color", accept="OK"):
    return show("color", on_result, title=title, value=str(value), accept=accept)


def settings(name, title, layout):
    return show("settings", None, title=title or name, name=name, layout=layout)


def pick_file(mode, title, filters, on_result, multiple=False, default_name=""):
    """A native picker. The system makes it modal to the main window."""
    return show(
        "file",
        on_result,
        title=title,
        mode=mode,
        filters=[dict(f) for f in filters],
        multiple=bool(multiple),
        defaultName=default_name,
    )


def _answer(kind, action, value):
    if action not in ("button", "accept"):
        return [] if kind == "file" else None
    if kind == "message":
        return None if value is None else str(value)
    if kind == "canvasSize":
        try:
            return {"width": int(value["width"]), "height": int(value["height"])}
        except (KeyError, TypeError, ValueError):
            return None
    if kind == "color":
        return str(value) if value else None
    if kind == "file":
        return [str(path) for path in (value or []) if path]
    return None


def _find(dialog_id):
    for entry in _STACK:
        if entry["spec"]["id"] == dialog_id:
            return entry
    return None


def _deliver(entry, action, value):
    spec = entry["spec"]
    # One answer per dialog. It is closed before the answer is handed on, so the
    # handler can put the next dialog up (the overwrite question after a picker).
    close(spec["id"])
    handler = entry.get("on_result")
    if handler is None:
        return
    try:
        handler(_answer(spec["kind"], action, value))
    except Exception as error:
        print(f"[dialogs] the answer to '{spec.get('title', spec['kind'])}' failed: {error}")


def _request_color(message):
    """A colour control was clicked: pick, then send the colour as that control would."""
    method = message.get("method") or "tool.option"
    params = {
        key: message[key]
        for key in ("hook", "name", "row", "startColumn", "endColumn", "menu")
        if message.get(key) is not None
    }

    def chosen(text):
        if not text:
            return
        try:
            call(method, {**params, "type": "color", "value": text})
        except Exception as error:
            print(f"[dialogs] sending the colour to '{params.get('hook')}' failed: {error}")

    color(message.get("value") or "#ff000000", chosen, title=message.get("title") or "Select Color")


def _request_settings(message):
    name = str(message.get("name") or "")
    try:
        layout = json.loads(python_hooks.settings_layout_json(name))
    except Exception as error:
        print(f"[dialogs] settings '{name}' failed: {error}")
        return
    if not layout:
        print(f"[dialogs] no settings registered for '{name}'")
        return
    settings(name, message.get("title") or name, layout)


_REQUESTS = {"color": _request_color, "settings": _request_settings}


def _on_dialog(message):
    action = message.get("action")
    if action == "request":
        handler = _REQUESTS.get(message.get("kind"))
        if handler is not None:
            handler(message)
        return
    if action not in ACTIONS:
        return
    entry = _find(message.get("id"))
    if entry is not None:
        _deliver(entry, action, message.get("value"))


python_hooks.set_hook(_on_dialog, dialog=True)
