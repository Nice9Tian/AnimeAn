"""The menu bar's own menus: File, Edit, Theme and Windows on the main window,
File and Setting on the texture board.

Which items a menu has, what it shows checked or greyed, and where it sits on
the bar are Python's (developer_guide.md, "分工"). C++ builds what
python_hooks hands it and reports a choice as a "menu" event; behind each item
is a File flow (file_menu.py) or a generic backend method. The item ids are
the ones the page and the tests have always used (file.open, edit.undo,
windows.timeline, child.setting.background.black, ...).

State is read when a menu is built, so checks follow the backend:
  Edit     history          canUndo, canRedo
  Theme    theme            mode
  Windows  windows          list (name, title, visible), timeline
  Setting  textureSettings  changeableTimeline, changeableTexture, background
"""

import file_menu
import python_hooks

FILE = "file"
EDIT = "edit"
THEME = "theme"
WINDOWS = "windows"
TEXTURE_FILE = "file.texture"
TEXTURE_SETTING = "child.setting"
MENUS = (FILE, EDIT, THEME, WINDOWS, TEXTURE_FILE, TEXTURE_SETTING)

# Script menus sit at the default order (50), between Edit and Theme; Settings
# (ui_settings.py) is 100.
ORDER = {FILE: 10, EDIT: 20, THEME: 80, WINDOWS: 90, TEXTURE_FILE: 10, TEXTURE_SETTING: 20}

THEMES = (("dark", "Dark"), ("light", "Light"))
BACKGROUNDS = (("white", "White"), ("black", "Black"), ("transparent", "Transparent"))
# Windows that the Windows menu does not list: the paint window is the boards.
UNLISTED_WINDOWS = ("paint",)


def _ui():
    try:
        import animean_python

        return animean_python.ui
    except Exception:
        return None


def _state(key):
    ui = _ui()
    try:
        value = ui.state(key) if ui is not None and hasattr(ui, "state") else None
    except Exception as error:
        print(f"[app_menus] reading '{key}' failed: {error}")
        value = None
    return value if isinstance(value, dict) else {}


def _call(method, params=None):
    ui = _ui()
    if ui is None or not hasattr(ui, "call"):
        raise RuntimeError("the backend is not running")
    return ui.call(method, dict(params or {}))


def _item(item_id, title, kind="action", **fields):
    entry = {"id": item_id, "name": item_id, "title": title, "kind": kind}
    entry.update(fields)
    return entry


def _separator(item_id):
    return {"id": item_id, "name": item_id, "kind": "separator"}


def _submenu(item_id, title, items=None, **fields):
    entry = {"id": item_id, "name": item_id, "title": title, "kind": "submenu"}
    if items is not None:
        entry["items"] = items
    entry.update(fields)
    return entry


def _imports(prefix):
    return [
        _item(f"{prefix}.raster", "Raster..."),
        _item(f"{prefix}.opentoonz", "OpenToonz Lines..."),
        _item(f"{prefix}.csp", "Clip Studio Paint..."),
    ]


def _texture_file_items():
    return [
        _submenu("file.texture.import", "Import", _imports("file.texture.import")),
        _separator("file.texture.sep1"),
        _item("file.texture.open", "Open Texture View..."),
        _item("file.texture.save", "Save Texture View"),
        _item("file.texture.saveAs", "Save Texture View As..."),
        _item("file.texture.exportImage", "Export Texture View Image..."),
    ]


def _file_items():
    # File > Texture is the texture board's File menu, plus its Setting menu.
    texture = _texture_file_items() + [
        _separator("file.texture.sep2"),
        _submenu("file.texture.setting", "Setting", menu=TEXTURE_SETTING, host="child"),
    ]
    return [
        _item("file.new", "Canvas Size..."),
        _item("file.open", "Open...", shortcut="Ctrl+O"),
        _item("file.save", "Save", shortcut="Ctrl+S"),
        _item("file.saveAs", "Save As...", shortcut="Ctrl+Shift+S"),
        _separator("file.sep1"),
        _submenu("file.import", "Import", _imports("file.import")),
        _submenu("file.texture", "Texture", texture),
    ]


def _edit_items():
    history = _state("history")
    return [
        _item("edit.undo", "Undo", shortcut="Ctrl+Z", enabled=bool(history.get("canUndo"))),
        _item("edit.redo", "Redo", shortcut="Ctrl+Y", enabled=bool(history.get("canRedo"))),
    ]


def _theme_items():
    mode = _state("theme").get("mode")
    return [_item(f"theme.{name}", title, "radio", checked=mode == name) for name, title in THEMES]


def _windows_items():
    windows = _state("windows")
    items = [_item("windows.texture_view", "Texture View"), _separator("windows.sep1")]
    for info in windows.get("list") or []:
        name = str(info.get("name") or "")
        if not name or name in UNLISTED_WINDOWS:
            continue
        items.append(_item(f"windows.{name}", info.get("title") or name, "check", checked=bool(info.get("visible"))))
    timeline = windows.get("timeline") or {}
    items.append(_item("windows.timeline", "Timeline", "check", checked=bool(timeline.get("visible"))))
    return items


def _texture_setting_items():
    settings = _state("textureSettings")
    background = settings.get("background")
    items = [
        _item("child.setting.changeableTimeline", "Changable Timeline", "check",
              checked=bool(settings.get("changeableTimeline"))),
        _item("child.setting.changeableTexture", "Changable Texture", "check",
              checked=bool(settings.get("changeableTexture"))),
        _separator("child.setting.sep1"),
        _submenu("child.setting.background", "Background", [
            _item(f"child.setting.background.{mode}", title, "radio", checked=background == mode)
            for mode, title in BACKGROUNDS
        ]),
    ]
    # The texture board's other menus (auto_mapping's View) are here as well.
    for menu in python_hooks.registered_menus("child"):
        name = menu.get("name")
        if name and name not in MENUS:
            items.append(_submenu(f"child.menu.{name}", menu.get("title") or name, menu=name, host="child"))
    return items


def _toggle(message, current):
    # A choice from the menu says what the check becomes; a bare trigger toggles.
    return bool(message["checked"]) if "checked" in message else not current


def _run(name, message):
    """One item of these menus. False when the item is not one of them."""
    if name.startswith("file."):
        return file_menu.run(name)
    if name in ("edit.undo", "edit.redo"):
        _call("history.undo" if name == "edit.undo" else "history.redo")
        return True
    if name in (f"theme.{mode}" for mode, _ in THEMES):
        _call("theme.set", {"mode": name[len("theme."):]})
        return True
    if name == "windows.texture_view":
        _call("windows.showTextureView")
        return True
    if name.startswith("windows."):
        params = {"name": name[len("windows."):]}
        if "checked" in message:
            params["visible"] = bool(message["checked"])
        _call("windows.setVisible", params)
        return True
    if name in ("child.setting.changeableTimeline", "child.setting.changeableTexture"):
        which = "timeline" if name.endswith("Timeline") else "texture"
        current = _state("textureSettings").get("changeableTimeline" if which == "timeline" else "changeableTexture")
        _call("texture.setChangeable", {which: _toggle(message, bool(current))})
        return True
    if name in (f"child.setting.background.{mode}" for mode, _ in BACKGROUNDS):
        _call("texture.setBackground", {"mode": name[len("child.setting.background."):]})
        return True
    return False


def _on_menu(message):
    # An item no flow here knows is left unhandled, and C++ reports it.
    if message.get("menu") in MENUS and _run(str(message.get("name") or ""), message):
        message["handled"] = True


python_hooks.register_menu({"name": FILE, "title": "File", "order": ORDER[FILE], "items": _file_items})
python_hooks.register_menu({"name": EDIT, "title": "Edit", "order": ORDER[EDIT], "items": _edit_items})
python_hooks.register_menu({"name": THEME, "title": "Theme", "order": ORDER[THEME], "items": _theme_items})
python_hooks.register_menu({"name": WINDOWS, "title": "Windows", "order": ORDER[WINDOWS], "items": _windows_items})
python_hooks.register_menu({"name": TEXTURE_FILE, "title": "File", "host": "child",
                            "order": ORDER[TEXTURE_FILE], "items": _texture_file_items})
python_hooks.register_menu({"name": TEXTURE_SETTING, "title": "Setting", "host": "child",
                            "order": ORDER[TEXTURE_SETTING], "items": _texture_setting_items})
python_hooks.set_hook(_on_menu, menu=True)
