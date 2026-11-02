"""The File menu's flows, driven the way the backend and the page drive them:
C++ forwards the menu item as a "menu" event, the page answers each dialog, and
every file operation goes out through ui.call. Covers the pickers, the question
before a file is replaced, the question before an import is scaled, the message
after a failure, the canvas size at startup and the names a save picker opens on."""
import json
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))


class FakeUi:
    def __init__(self):
        self.published = []
        self.calls = []
        self.replies = {}
        self.window = {"canvasWidth": 1920, "canvasHeight": 1080, "currentFilePath": "", "childFilePath": ""}
        self.later = []

    def set_dialogs(self, text):
        self.published.append(json.loads(text))

    def call(self, method, params):
        self.calls.append((method, dict(params)))
        reply = self.replies.get(method)
        if callable(reply):
            reply = reply(params)
        if isinstance(reply, Exception):
            raise reply
        return reply or {}

    def state(self, key):
        return dict(self.window) if key == "window" else None

    def call_later(self, ms, function):
        self.later.append((ms, function))

    def set_hook_events(self, events):
        pass


UI = FakeUi()
sys.modules["animean_python"] = types.SimpleNamespace(ui=UI)

import dialogs  # noqa: E402
import file_menu  # noqa: E402
import python_hooks  # noqa: E402


def report(name, ok):
    print(f"[{'OK' if ok else 'FAIL'}] {name}")
    return ok


def menu(item):
    """A File item as C++ forwards it; returns the message after the hooks ran."""
    message = {"event": "menu", "menu": "file", "name": item, "view": "main", "checked": False}
    python_hooks.dispatch(message)
    return message


def top():
    return UI.published[-1][-1] if UI.published and UI.published[-1] else None


def answer(action, value=None):
    spec = top()
    python_hooks.dispatch({"event": "dialog", "action": action, "id": spec["id"], "value": value})
    return spec


def reset():
    UI.calls.clear()
    UI.replies.clear()
    for entry in list(dialogs._STACK):
        dialogs.close(entry["spec"]["id"])


def startup():
    results = [report("the canvas size is asked once the event loop runs", len(UI.later) == 1 and UI.later[0][0] == 0)]
    UI.later[0][1]()
    spec = top()
    results.append(report("it asks on the current canvas", spec["kind"] == "canvasSize"
                          and spec["title"] == "New Project" and (spec["width"], spec["height"]) == (1920, 1080)))
    answer("accept", {"width": 800, "height": 600})
    results.append(report("Create makes the new canvas", UI.calls == [("project.new", {"width": 800, "height": 600})]
                          and UI.published[-1] == []))
    reset()
    menu("file.new")
    answer("cancel")
    results.append(report("File > Canvas Size... cancelled keeps the canvas", UI.calls == [] and UI.published[-1] == []))
    return results


def startup_with_a_project():
    """A backend that already holds a project (opened before the page came up)
    is not asked for a blank canvas, and one opened while the question waits
    withdraws it."""
    results = []
    reset()
    UI.later.clear()
    UI.window["currentFilePath"] = "C:/work/girl.anproj"
    file_menu._startup_canvas_size()
    results.append(report("a backend already holding a project is not asked", dialogs.stack() == []))
    UI.window["currentFilePath"] = ""
    file_menu._startup_canvas_size()
    question = top()
    results.append(report("a fresh one is", question["kind"] == "canvasSize"))
    python_hooks.dispatch({"event": "historyrestore", "view": "main"})
    results.append(report("a history reset looks again once the call is done", len(UI.later) == 1))
    UI.later.pop()[1]()
    results.append(report("with no project yet the question stays", dialogs.is_open(question["id"])))
    python_hooks.dispatch({"event": "historyrestore", "view": "main"})
    UI.window["currentFilePath"] = "C:/work/girl.anproj"
    UI.later.pop()[1]()
    results.append(report("a project opened meanwhile withdraws the startup question",
                          not dialogs.is_open(question["id"]) and UI.calls == []))
    python_hooks.dispatch({"event": "historyrestore", "view": "main"})
    results.append(report("and later history resets look no more", UI.later == []))
    UI.window["currentFilePath"] = ""
    return results


def open_and_save():
    results = []
    reset()
    menu("file.open")
    spec = top()
    results.append(report("Open picks an .anproj", spec["kind"] == "file" and spec["mode"] == "open"
                          and spec["title"] == "Open Project" and spec["filters"] == file_menu.PROJECT))
    UI.replies["project.open"] = RuntimeError("Not an AnimeAn project")
    answer("accept", ["C:/broken.anproj"])
    failure = top()
    results.append(report("a failed open says so under Open Project", UI.calls == [("project.open", {"path": "C:/broken.anproj"})]
                          and failure["kind"] == "message" and failure["title"] == "Open Project"
                          and failure["text"] == "Not an AnimeAn project" and failure["buttons"] == ["OK"]))
    answer("button", "OK")
    results.append(report("and the message closes", UI.published[-1] == []))

    reset()
    menu("file.open")
    answer("cancel")
    results.append(report("a cancelled picker opens nothing", UI.calls == []))

    reset()
    UI.window["currentFilePath"] = ""
    menu("file.save")
    spec = top()
    results.append(report("Save without a file asks where", spec["kind"] == "file" and spec["mode"] == "save"
                          and spec["title"] == "Save Project As" and spec["defaultName"] == ""))
    UI.replies["project.saveAs"] = lambda p: {"needsConfirmOverwrite": False, "fileName": "a.anproj"} if p.get("probe") else {}
    answer("accept", ["C:/a"])
    results.append(report("a new name saves without asking", UI.calls == [
        ("project.saveAs", {"path": "C:/a", "probe": True}), ("project.saveAs", {"path": "C:/a", "overwrite": True})]))

    reset()
    UI.window["currentFilePath"] = "C:/work/old.json"
    menu("file.saveAs")
    results.append(report("Save As opens on the file's name in the new format", top()["defaultName"] == "old.anproj"))
    UI.replies["project.saveAs"] = lambda p: {"needsConfirmOverwrite": True, "fileName": "a.anproj"} if p.get("probe") else {}
    answer("accept", ["C:/a"])
    question = top()
    results.append(report("an existing file is asked about first", question["kind"] == "message"
                          and question["title"] == "Save Project As"
                          and question["text"] == "a.anproj already exists.\nDo you want to replace it?"
                          and question["buttons"] == ["Yes", "No"] and len(UI.calls) == 1))
    answer("button", "No")
    results.append(report("No leaves it alone", len(UI.calls) == 1 and UI.published[-1] == []))

    reset()
    UI.replies["project.saveAs"] = lambda p: {"needsConfirmOverwrite": True, "fileName": "a.anproj"} if p.get("probe") else {}
    menu("file.saveAs")
    answer("accept", ["C:/a"])
    answer("closed")
    results.append(report("closing the question leaves it alone too", len(UI.calls) == 1))

    reset()
    UI.replies["project.saveAs"] = lambda p: {"needsConfirmOverwrite": True, "fileName": "a.anproj"} if p.get("probe") else RuntimeError("Disk full")
    menu("file.saveAs")
    answer("accept", ["C:/a"])
    answer("button", "Yes")
    failure = top()
    results.append(report("Yes replaces it, and a failure says so under Save Project",
                          UI.calls[-1] == ("project.saveAs", {"path": "C:/a", "overwrite": True})
                          and failure["title"] == "Save Project" and failure["text"] == "Disk full"))

    reset()
    UI.window["currentFilePath"] = "C:/work/a.anproj"
    UI.replies["project.save"] = RuntimeError("Read-only")
    menu("file.save")
    results.append(report("Save with an .anproj saves in place", UI.calls == [("project.save", {})]
                          and top()["title"] == "Save Project" and top()["text"] == "Read-only"))
    reset()
    UI.window["currentFilePath"] = ""
    return results


def imports():
    results = []
    reset()
    menu("file.import.raster")
    spec = top()
    results.append(report("Import Raster picks images", spec["title"] == "Import Raster" and spec["filters"] == file_menu.IMAGES))
    answer("accept", ["C:/a.png", "C:/b.png"])
    results.append(report("and imports them onto the main board", UI.calls == [("file.import", {"kind": "raster", "paths": ["C:/a.png", "C:/b.png"]})]))

    reset()
    UI.replies["file.import"] = lambda p: ({"needsScale": True, "sourceBounds": {"x": 0, "y": -2.0, "width": 4000.0, "height": 3000},
                                            "canvasWidth": 1920, "canvasHeight": 1080.0} if p.get("probe") else {})
    menu("file.texture.import.opentoonz")
    answer("accept", ["C:/a.pli"])
    question = top()
    results.append(report("a vector import that does not fit asks first", question["title"] == "Import OpenToonz Lines"
                          and question["text"] == file_menu.FIT_TEXT.format("0", "-2", "4000", "3000", "1920", "1080")
                          and UI.calls == [("file.import", {"kind": "opentoonz", "paths": ["C:/a.pli"], "view": "child", "probe": True})]))
    answer("button", "Yes")
    results.append(report("Yes scales it to the texture board", UI.calls[-1] == ("file.import", {
        "kind": "opentoonz", "paths": ["C:/a.pli"], "view": "child", "fitToCanvas": True})))

    reset()
    UI.replies["file.import"] = lambda p: ({"needsScale": True, "sourceBounds": {}} if p.get("probe") else {})
    menu("file.import.csp")
    answer("accept", ["C:/a.clip"])
    answer("closed")
    results.append(report("closing the question imports at its own size", UI.calls[-1] == ("file.import", {
        "kind": "csp", "paths": ["C:/a.clip"], "fitToCanvas": False})))

    reset()
    UI.replies["file.import"] = lambda p: ({"needsScale": False} if p.get("probe") else RuntimeError("No vector strokes"))
    menu("file.import.csp")
    answer("accept", ["C:/a.clip"])
    results.append(report("a vector import that fits goes straight in, and its failure says so",
                          UI.calls[-1] == ("file.import", {"kind": "csp", "paths": ["C:/a.clip"], "fitToCanvas": False})
                          and top()["title"] == "Import Clip Studio Paint" and top()["text"] == "No vector strokes"))
    reset()
    return results


def texture():
    results = []
    reset()
    UI.window["childFilePath"] = "C:/t/skin.textureview"
    menu("file.texture.save")
    results.append(report("Save Texture View with a file saves in place", UI.calls == [("file.texture", {"kind": "save"})]
                          and UI.published[-1] == []))
    reset()
    UI.window["childFilePath"] = "C:/t/skin.json"
    menu("file.texture.save")
    spec = top()
    results.append(report("without one it asks where, on the file's name", spec["title"] == "Save Texture View As"
                          and spec["defaultName"] == "skin.textureview"))
    UI.replies["file.texture"] = lambda p: {"needsConfirmOverwrite": True, "fileName": "skin.textureview"} if p.get("probe") else {}
    answer("accept", ["C:/t/skin"])
    answer("button", "Yes")
    results.append(report("and asks before replacing", UI.calls == [
        ("file.texture", {"kind": "saveAs", "path": "C:/t/skin", "probe": True}),
        ("file.texture", {"kind": "saveAs", "path": "C:/t/skin", "overwrite": True})]))

    reset()
    UI.window["childFilePath"] = ""
    menu("file.texture.exportImage")
    spec = top()
    results.append(report("Export opens on texture.png", spec["defaultName"] == "texture.png" and spec["filters"] == file_menu.PNG))
    UI.replies["file.texture"] = RuntimeError("The texture view could not be captured.")
    answer("accept", ["C:/out.png"])
    results.append(report("and exports without asking, a failure under its own title",
                          UI.calls == [("file.texture", {"kind": "exportImage", "path": "C:/out.png"})]
                          and top()["title"] == "Export Texture View Image"))
    reset()
    menu("file.texture.open")
    results.append(report("Open Texture View picks a .textureview with no default name",
                          top()["mode"] == "open" and top()["defaultName"] == "" and top()["filters"] == file_menu.TEXTURE))
    answer("accept", ["C:/t/a.textureview"])
    results.append(report("and opens it", UI.calls == [("file.texture", {"kind": "open", "path": "C:/t/a.textureview"})]))
    reset()
    return results


def names_and_other_menus():
    name = file_menu.default_save_name
    results = [
        report("project names", name("project", {"currentFilePath": "C:\\w\\a.anproj"}) == "a.anproj"
               and name("project", {"currentFilePath": "C:/w/a"}) == "a.anproj" and name("project", {}) == ""),
        report("texture names", name("saveAs", {"childFilePath": "a.json"}) == "a.textureview"
               and name("saveAs", {}) == "texture.textureview"),
        report("export names", name("exportImage", {"childFilePath": "C:/t/skin.textureview"}) == "skin.png"
               and name("exportImage", {"childFilePath": ".hidden"}) == ".hidden.png"),
    ]
    reset()
    other = {"event": "menu", "menu": "Draw", "name": "file.open"}
    python_hooks.dispatch(other)
    results.append(report("another menu's item named like a File item is not ours",
                          (UI.published[-1] == [] if UI.published else True) and "handled" not in other))
    results.append(report("an unknown File item is left alone", file_menu.run("file.print") is False))
    results.append(report("and left unhandled, so C++ reports it", "handled" not in menu("file.print")))
    results.append(report("a File item a flow runs is handled", menu("file.open").get("handled") is True))
    reset()
    return results


def main():
    results = []
    results.extend(startup())
    results.extend(startup_with_a_project())
    results.extend(open_and_save())
    results.extend(imports())
    results.extend(texture())
    results.extend(names_and_other_menus())
    print(f"PASS {sum(results)}/{len(results)}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
