"""The menu bar's own menus Python declares (app_menus.py): what each holds,
what it shows checked or greyed for a given backend state, where it sits on
the bar, and what each choice calls. The ids and texts are the ones the C++
builder had before the menus moved to Python."""
import json
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))


class FakeUi:
    def __init__(self):
        self.calls = []
        self.states = {}
        self.fail = {}

    def state(self, key):
        return self.states.get(key)

    def call(self, method, params):
        self.calls.append((method, dict(params)))
        if method in self.fail:
            raise RuntimeError(self.fail[method])
        return {}

    def set_dialogs(self, text):
        pass

    def call_later(self, ms, function):
        pass

    def set_hook_events(self, events):
        pass


UI = FakeUi()
sys.modules["animean_python"] = types.SimpleNamespace(ui=UI)

import app_menus  # noqa: E402
import file_menu  # noqa: E402
import python_hooks  # noqa: E402


def report(name, ok):
    print(f"[{'OK' if ok else 'FAIL'}] {name}")
    return ok


def bar(host):
    return json.loads(python_hooks.menus_json(host))


def menu(host, name):
    return next((m for m in bar(host) if m["name"] == name), None)


def shape(items):
    """id, title, kind and shortcut of each item, submenus nested; references say which menu."""
    out = []
    for item in items:
        row = [item.get("id"), item.get("title"), item.get("kind"), item.get("shortcut", "")]
        if item.get("kind") == "submenu":
            row.append(shape(item["items"]) if "items" in item else ("ref", item.get("host"), item.get("menu")))
        out.append(row)
    return out


def choose(menu_name, item, **fields):
    message = {"event": "menu", "menu": menu_name, "name": item, "view": "main"}
    message.update(fields)
    python_hooks.dispatch(message)
    return message


IMPORTS = lambda prefix: [  # noqa: E731
    [f"{prefix}.raster", "Raster...", "action", ""],
    [f"{prefix}.opentoonz", "OpenToonz Lines...", "action", ""],
    [f"{prefix}.csp", "Clip Studio Paint...", "action", ""],
]
TEXTURE_FILE = [
    ["file.texture.import", "Import", "submenu", "", IMPORTS("file.texture.import")],
    ["file.texture.sep1", None, "separator", ""],
    ["file.texture.open", "Open Texture View...", "action", ""],
    ["file.texture.save", "Save Texture View", "action", ""],
    ["file.texture.saveAs", "Save Texture View As...", "action", ""],
    ["file.texture.exportImage", "Export Texture View Image...", "action", ""],
]
FILE = [
    ["file.new", "Canvas Size...", "action", ""],
    ["file.open", "Open...", "action", "Ctrl+O"],
    ["file.save", "Save", "action", "Ctrl+S"],
    ["file.saveAs", "Save As...", "action", "Ctrl+Shift+S"],
    ["file.sep1", None, "separator", ""],
    ["file.import", "Import", "submenu", "", IMPORTS("file.import")],
    ["file.texture", "Texture", "submenu", "", TEXTURE_FILE + [
        ["file.texture.sep2", None, "separator", ""],
        ["file.texture.setting", "Setting", "submenu", "", ("ref", "child", "child.setting")],
    ]],
]


def declarations():
    UI.states = {
        "history": {"canUndo": True, "canRedo": False},
        "theme": {"mode": "light"},
        "windows": {
            "list": [
                {"name": "paint", "title": "Paint", "visible": True},
                {"name": "tools", "title": "Tools", "visible": True},
                {"name": "python_debug", "title": "Python Debug", "visible": False},
            ],
            "timeline": {"visible": True},
        },
        "textureSettings": {"changeableTimeline": False, "changeableTexture": True, "background": "black"},
    }
    results = [
        report("File holds what the C++ File menu held, ids and shortcuts included", shape(menu("main", "file")["items"]) == FILE),
        report("the texture board's File is File > Texture without its Setting", shape(menu("child", "file.texture")["items"]) == TEXTURE_FILE),
    ]
    edit = menu("main", "edit")["items"]
    results.append(report("Edit: Undo and Redo, greyed by the history state",
                          [(i["id"], i["shortcut"], i["enabled"]) for i in edit] == [("edit.undo", "Ctrl+Z", True), ("edit.redo", "Ctrl+Y", False)]))
    results.append(report("Redo's second key is declared here, not in the page",
                          edit[1].get("extraShortcuts") == ["Ctrl+Shift+Z"] and "extraShortcuts" not in edit[0]))
    theme = menu("main", "theme")["items"]
    results.append(report("Theme: Dark and Light radios, the current mode ticked",
                          [(i["id"], i["kind"], i["checked"]) for i in theme] == [("theme.dark", "radio", False), ("theme.light", "radio", True)]))
    windows = menu("main", "windows")["items"]
    results.append(report("Windows: Texture View, a line, every window but paint, then Timeline", shape(windows) == [
        ["windows.texture_view", "Texture View", "action", ""],
        ["windows.sep1", None, "separator", ""],
        ["windows.tools", "Tools", "check", ""],
        ["windows.python_debug", "Python Debug", "check", ""],
        ["windows.timeline", "Timeline", "check", ""],
    ] and [i.get("checked") for i in windows[2:]] == [True, False, True]))
    setting = menu("child", "child.setting")["items"]
    results.append(report("Setting: the two Changable checks and the background radios follow the backend", shape(setting)[:3] == [
        ["child.setting.changeableTimeline", "Changable Timeline", "check", ""],
        ["child.setting.changeableTexture", "Changable Texture", "check", ""],
        ["child.setting.sep1", None, "separator", ""],
    ] and [setting[0]["checked"], setting[1]["checked"]] == [False, True]
        and [(i["id"], i["checked"]) for i in setting[3]["items"]] == [
            ("child.setting.background.white", False), ("child.setting.background.black", True),
            ("child.setting.background.transparent", False)]))

    python_hooks.register_menu({"name": "View", "title": "View", "host": "child", "items": []})
    setting = menu("child", "child.setting")["items"]
    results.append(report("the texture board's other menus sit in Setting as submenus that refer to them",
                          shape(setting)[4:] == [["child.menu.View", "View", "submenu", "", ("ref", "child", "View")]]))
    UI.states = {}
    results.append(report("with no backend state nothing breaks: nothing ticked, Undo greyed",
                          menu("main", "edit")["items"][0]["enabled"] is False
                          and not any(i.get("checked") for i in menu("main", "theme")["items"])))
    return results


def order():
    python_hooks.register_menu({"name": "Auto Mapping", "title": "Auto Mapping", "items": []})
    python_hooks.register_menu({"name": "Settings", "title": "Settings", "order": 100, "items": []})
    python_hooks.register_menu({"name": "Draw", "title": "Draw", "items": []})
    return [
        report("the main bar: File, Edit, script menus as registered, Theme, Windows, Settings",
               [m["name"] for m in bar("main")] == ["file", "edit", "Auto Mapping", "Draw", "theme", "windows", "Settings"]),
        report("the texture bar: File, Setting, then its script menus",
               [m["name"] for m in bar("child")] == ["file.texture", "child.setting", "View"]),
    ]


def failures():
    python_hooks.register_menu({"name": "Broken", "title": "Broken", "items": [{"name": "x", "title": "X", "checked": {True}}]})
    python_hooks.register_menu({"name": "Raises", "title": "Raises", "items": lambda: 1 / 0})
    names = [m["name"] for m in bar("main")]
    python_hooks._MENUS[:] = [m for m in python_hooks._MENUS if m.get("name") not in ("Broken", "Raises")]
    return [report("a menu JSON cannot carry, or one whose items raise, drops out alone",
                   "Broken" not in names and "Raises" not in names
                   and [n for n in names if n in app_menus.MENUS] == ["file", "edit", "theme", "windows"])]


def choices():
    results = []
    ran = []
    real_run = file_menu.run
    file_menu.run = lambda item: ran.append(item) or item in ("file.open", "file.texture.save")
    try:
        opened = choose("file", "file.open")
        texture = choose("file.texture", "file.texture.save", view="child")
        unknown = choose("file", "file.print")
        results.append(report("File items go to file_menu.run, from either bar",
                              ran == ["file.open", "file.texture.save", "file.print"]
                              and opened.get("handled") is True and texture.get("handled") is True))
        results.append(report("an item no flow runs is left unhandled, so C++ reports it", "handled" not in unknown))
    finally:
        file_menu.run = real_run

    UI.states = {"textureSettings": {"changeableTimeline": True, "changeableTexture": False}}
    cases = [
        ("Undo", "edit", "edit.undo", {}, [("history.undo", {})]),
        ("Redo", "edit", "edit.redo", {}, [("history.redo", {})]),
        ("Light", "theme", "theme.light", {"checked": True}, [("theme.set", {"mode": "light"})]),
        ("Dark", "theme", "theme.dark", {}, [("theme.set", {"mode": "dark"})]),
        ("Texture View", "windows", "windows.texture_view", {"checked": True}, [("windows.showTextureView", {})]),
        ("a window ticked from the menu", "windows", "windows.tools", {"checked": False}, [("windows.setVisible", {"name": "tools", "visible": False})]),
        ("a window toggled by a bare trigger", "windows", "windows.python_debug", {}, [("windows.setVisible", {"name": "python_debug"})]),
        ("Timeline", "windows", "windows.timeline", {"checked": True}, [("windows.setVisible", {"name": "timeline", "visible": True})]),
        ("Changable Timeline toggled", "child.setting", "child.setting.changeableTimeline", {}, [("texture.setChangeable", {"timeline": False})]),
        ("Changable Texture ticked", "child.setting", "child.setting.changeableTexture", {"checked": True}, [("texture.setChangeable", {"texture": True})]),
        ("Changable Texture toggled", "child.setting", "child.setting.changeableTexture", {}, [("texture.setChangeable", {"texture": True})]),
        ("a background", "child.setting", "child.setting.background.transparent", {"checked": True}, [("texture.setBackground", {"mode": "transparent"})]),
        ("the same item from File > Texture > Setting", "file", "child.setting.background.white", {}, [("texture.setBackground", {"mode": "white"})]),
    ]
    for label, menu_name, item, fields, expected in cases:
        UI.calls.clear()
        message = choose(menu_name, item, **fields)
        results.append(report(f"{label} calls the generic method", UI.calls == expected and message.get("handled") is True))

    UI.calls.clear()
    other = choose("Auto Mapping", "edit.undo")
    bogus = choose("edit", "edit.cut")
    results.append(report("another menu's item named like ours, or an item we do not have, is not ours",
                          UI.calls == [] and "handled" not in other and "handled" not in bogus))
    UI.fail["theme.set"] = "backend says no"
    try:
        choose("theme", "theme.dark")
        raised = False
    except RuntimeError:
        raised = True
    UI.fail.clear()
    results.append(report("a method the backend refuses raises, so C++ reports the error", raised))
    return results


def main():
    results = [report("the menu hook is registered", python_hooks.has_hooks({"event": "menu"}))]
    results.extend(declarations())
    results.extend(order())
    results.extend(failures())
    results.extend(choices())
    print(f"PASS {sum(results)}/{len(results)}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
