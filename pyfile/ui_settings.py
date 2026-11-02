"""The Settings menu: the board renderer and the interface language.

Both are per-machine choices the page keeps in its own storage (localStorage),
not in the document. The page reports what it has and whether WebGL works;
this module decides what the menu offers and what a choice changes, and
broadcasts the choice as the "py/preferences" state for the page to apply
(developer_guide.md, "分工": which items a menu has is Python's).
"""

import json

import python_hooks
import ui_language

MENU = "Settings"
REPORT_HOOK = "ui_renderer"
RENDERERS = (("webgl", "WebGL"), ("canvas2d", "Canvas 2D"))
# Each language is listed in its own writing, whatever the interface is showing.
LANGUAGES = (("en", "English"), ("zh-CN", "简体中文"), ("ja", "日本語"))

_STATE = {"renderer": "webgl", "webgl": True}
_CHOICES = {"count": 0}


def renderer():
    return _STATE["renderer"]


def _items():
    renderer_items = [
        {
            "name": f"renderer.{kind}",
            "title": title,
            "kind": "radio",
            "checked": kind == _STATE["renderer"],
            "enabled": kind != "webgl" or _STATE["webgl"],
        }
        for kind, title in RENDERERS
    ]
    language_items = [
        {"name": f"language.{code}", "title": title, "kind": "radio", "checked": code == ui_language.current()}
        for code, title in LANGUAGES
    ]
    return [
        {"name": "renderer", "title": "Board Renderer", "kind": "submenu", "items": renderer_items},
        {"name": "language", "title": "Language", "kind": "submenu", "items": language_items},
    ]


def _publish(preferences):
    # The count tells the page this is a new choice, not one it applied already.
    _CHOICES["count"] += 1
    try:
        import animean_python

        animean_python.ui.broadcast("py/preferences", json.dumps({**preferences, "choice": _CHOICES["count"]}))
    except Exception as error:
        print(f"[ui_settings] the choice did not reach the page: {error}")


def choose(item):
    """A Settings menu item: "renderer.<kind>" or "language.<code>"."""
    group, _, value = str(item or "").partition(".")
    if group == "renderer" and value in dict(RENDERERS):
        if value == "webgl" and not _STATE["webgl"]:
            return False
        _publish({"renderer": value, "language": ui_language.current()})
        return True
    if group == "language" and value in dict(LANGUAGES):
        _publish({"renderer": _STATE["renderer"], "language": value})
        return True
    return False


def _on_menu(message):
    if message.get("menu") == MENU:
        choose(message.get("name"))


def _on_report(message):
    # The page says what it is drawing with, at startup and after every switch.
    if message.get("hook") != REPORT_HOOK:
        return
    name, value = message.get("name"), message.get("value")
    if name == "renderer" and value in dict(RENDERERS):
        _STATE["renderer"] = value
    elif name == "webgl":
        _STATE["webgl"] = value not in (False, 0, "false", "0", "")


python_hooks.register_menu({"name": MENU, "title": "Settings", "place": "end", "items": _items})
python_hooks.set_hook(_on_menu, menu=True)
python_hooks.set_hook(_on_report, option=True)
