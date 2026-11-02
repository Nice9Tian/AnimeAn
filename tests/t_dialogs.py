"""The dialog stack Python owns: what reaches C++ when a dialog opens or closes,
what each kind answers for each thing the page can say, and the two requests
the page makes (a colour swatch, a script's settings window)."""
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
        self.fail = {}
        self.options = {}

    def set_dialogs(self, text):
        self.published.append(json.loads(text))

    def call(self, method, params):
        self.calls.append((method, params))
        if method in self.fail:
            raise RuntimeError(self.fail[method])
        if method in ("tool.option", "settings.option"):
            self.options[(params.get("hook"), params.get("name"))] = params.get("value")
        return {}

    def set_hook_events(self, events):
        pass


UI = FakeUi()
sys.modules["animean_python"] = types.SimpleNamespace(ui=UI)

import dialogs  # noqa: E402
import python_hooks  # noqa: E402


def report(name, ok):
    print(f"[{'OK' if ok else 'FAIL'}] {name}")
    return ok


def page(action, dialog_id=None, value=None, **fields):
    """What the page says, as C++ hands it on; returns the message after the hooks ran."""
    message = {"event": "dialog", "action": action, "id": dialog_id, "value": value}
    message.update(fields)
    python_hooks.dispatch(message)
    return message


def last():
    return UI.published[-1] if UI.published else None


def stack_and_publishing():
    results = []
    answers = []
    first = dialogs.message("Save Project", "Disk full", ("Retry", "Cancel"), answers.append)
    results.append(report("opening publishes the stack", last() == [{
        "id": first, "kind": "message", "modal": True, "title": "Save Project",
        "text": "Disk full", "buttons": ["Retry", "Cancel"]}]))
    results.append(report("a dialog is modal unless asked otherwise", dialogs.is_modal()))
    second = dialogs.show("message", None, modal=False, title="Note", text="x", buttons=["OK"])
    results.append(report("a second dialog goes on top", [d["id"] for d in last()] == [first, second]))
    results.append(report("ids are unique", first != second))
    results.append(report("closing takes it away and publishes", dialogs.close(second) and [d["id"] for d in last()] == [first]))
    results.append(report("closing what is not open is a no-op", not dialogs.close(second)))
    page("button", first, "Retry")
    results.append(report("a button answers with its text and ends the dialog", answers == ["Retry"] and last() == []))
    results.append(report("nothing is modal with the stack empty", not dialogs.is_modal()))
    return results


def answers_by_kind():
    results = []
    got = []
    cases = [
        ("message: closing the window answers None", lambda: dialogs.message("T", "x", ("Yes", "No"), got.append), "closed", None, None),
        ("message: Escape answers None", lambda: dialogs.message("T", "x", ("OK",), got.append), "cancel", None, None),
        ("canvas size: the size", lambda: dialogs.canvas_size(1280, 720, got.append), "accept", {"width": "800", "height": 600}, {"width": 800, "height": 600}),
        ("canvas size: a broken size answers None", lambda: dialogs.canvas_size(1280, 720, got.append), "accept", {"width": "wide"}, None),
        ("canvas size: cancel answers None", lambda: dialogs.canvas_size(1280, 720, got.append), "cancel", None, None),
        ("color: the colour", lambda: dialogs.color("#ff000000", got.append), "accept", "#ff336699", "#ff336699"),
        ("color: an error answers None", lambda: dialogs.color("#ff000000", got.append), "error", "boom", None),
        ("file: the paths", lambda: dialogs.pick_file("open", "Open Project", [], got.append), "accept", ["C:/a.anproj", ""], ["C:/a.anproj"]),
        ("file: cancel answers []", lambda: dialogs.pick_file("open", "Open Project", [], got.append), "cancel", None, []),
    ]
    for name, opener, action, value, expected in cases:
        got.clear()
        dialog_id = opener()
        page(action, dialog_id, value)
        results.append(report(name, got == [expected] and last() == []))
    spec = dialogs.pick_file("save", "Save Project As", [{"name": "AnimeAn Projects", "extensions": ["anproj"]}],
                             got.append, default_name="a.anproj")
    results.append(report("a picker carries mode, filters and default name", last()[0] == {
        "id": spec, "kind": "file", "modal": True, "title": "Save Project As", "mode": "save",
        "filters": [{"name": "AnimeAn Projects", "extensions": ["anproj"]}], "multiple": False,
        "defaultName": "a.anproj"}))
    dialogs.close(spec)
    return results


def answers_that_go_wrong():
    results = []
    got = []
    dialog_id = dialogs.message("T", "x", ("OK",), got.append)
    stray = page("button", "d999", "OK")
    results.append(report("an answer for an unknown id is ignored", got == [] and len(last()) == 1))
    results.append(report("and left unhandled, so C++ drops that id", "handled" not in stray))
    bogus = page("bogus", dialog_id, "OK")
    results.append(report("an unknown action is ignored", got == [] and len(last()) == 1))
    results.append(report("but the dialog is still Python's, so C++ keeps it", bogus.get("handled") is True))
    taken = page("button", dialog_id, "OK")
    results.append(report("an answer Python takes is handled", taken.get("handled") is True))
    late = page("button", dialog_id, "OK")
    results.append(report("a second answer to the same dialog is ignored", got == ["OK"] and "handled" not in late))

    chain = []

    def next_question(answer):
        chain.append(answer)
        dialogs.message("Save Project As", "replace?", ("Yes", "No"), chain.append)

    first = dialogs.pick_file("save", "Save Project As", [], next_question)
    page("accept", first, ["C:/a.anproj"])
    results.append(report("an answer can put the next dialog up", chain == [["C:/a.anproj"]]
                          and [d["title"] for d in last()] == ["Save Project As"] and last()[0]["kind"] == "message"))
    page("button", last()[0]["id"], "Yes")
    results.append(report("and that one answers in turn", chain == [["C:/a.anproj"], "Yes"] and last() == []))

    def broken(_answer):
        raise ValueError("handler bug")

    dialog_id = dialogs.message("T", "x", ("OK",), broken)
    page("button", dialog_id, "OK")
    results.append(report("a handler that raises still leaves the dialog closed", last() == []))
    return results


def requests():
    results = []
    UI.calls.clear()
    asked = page("request", kind="color", value="#ff112233", hook="axis_color", name="axis_color",
                 row=2, startColumn=0, endColumn=1, menu="line_settings")
    spec = last()[-1]
    results.append(report("a colour request opens a colour dialog on the control's colour",
                          spec["kind"] == "color" and spec["value"] == "#ff112233" and spec["title"] == "Select Color"
                          and asked.get("handled") is True))
    page("accept", spec["id"], "#ff445566")
    results.append(report("the chosen colour goes out as that control's option", UI.calls == [("settings.option", {
        "hook": "axis_color", "name": "axis_color", "row": 2, "startColumn": 0, "endColumn": 1,
        "menu": "line_settings", "type": "color", "value": "#ff445566"})]))
    UI.calls.clear()
    page("request", kind="color", value="#ff112233", hook="pen", method="project.open")
    page("accept", last()[-1]["id"], "#ff010203")
    results.append(report("Python, not the page, picks the method: a tool's colour is a tool option",
                          [method for method, _ in UI.calls] == ["tool.option"]))
    UI.calls.clear()
    page("request", kind="color", value="#ff112233", hook="pen")
    page("cancel", last()[-1]["id"])
    results.append(report("a cancelled colour sends nothing", UI.calls == [] and last() == []))

    python_hooks.register_settings("t_dialogs_window", {"controls": [{"type": "check", "name": "x"}]})
    opened = page("request", kind="settings", name="t_dialogs_window", title="Window Settings")
    spec = last()[-1]
    results.append(report("a settings request opens the registered layout", spec["kind"] == "settings"
                          and spec["name"] == "t_dialogs_window" and spec["title"] == "Window Settings"
                          and spec["layout"] == {"controls": [{"type": "check", "name": "x"}]}
                          and opened.get("handled") is True))
    page("cancel", spec["id"])
    count = len(UI.published)
    missing = page("request", kind="settings", name="nothing_registered")
    results.append(report("a settings request with no layout opens nothing, and C++ reports it",
                          len(UI.published) == count and "handled" not in missing))
    unknown = page("request", kind="unknown_kind")
    results.append(report("an unknown request opens nothing, and C++ reports it",
                          len(UI.published) == count and "handled" not in unknown))
    return results


def settings_window_redraws_its_colour():
    """A colour picked from a settings window shows in that window's swatch."""
    results = []
    UI.options.clear()

    def layout():
        value = UI.options.get(("line_colors", "h_axis"), "#ff000000")
        return {"controls": [{"type": "color", "name": "h_axis", "hook": "line_colors", "value": value}]}

    python_hooks.register_settings("t_dialogs_colors", layout)
    page("request", kind="settings", name="t_dialogs_colors", title="Line Display Settings")
    window = last()[-1]
    page("request", kind="color", value="#ff000000", hook="line_colors", name="h_axis", menu="t_dialogs_colors")
    picker = last()[-1]
    results.append(report("the colour dialog opens over the settings window",
                          [d["id"] for d in last()] == [window["id"], picker["id"]]))
    page("accept", picker["id"], "#ff123456")
    shown = last()
    results.append(report("the settings window is sent again with the chosen colour",
                          [d["id"] for d in shown] == [window["id"]]
                          and shown[0]["layout"]["controls"][0]["value"] == "#ff123456"))
    UI.fail["settings.option"] = "backend says no"
    page("request", kind="color", value="#ff123456", hook="line_colors", name="h_axis", menu="t_dialogs_colors")
    page("accept", last()[-1]["id"], "#ff654321")
    results.append(report("a colour the backend refused is not drawn",
                          last()[0]["layout"]["controls"][0]["value"] == "#ff123456"))
    UI.fail.clear()
    page("cancel", window["id"])
    return results


def main():
    results = [report("the dialog hook is registered", python_hooks.has_hooks({"event": "dialog"}))]
    results.extend(stack_and_publishing())
    results.extend(answers_by_kind())
    results.extend(answers_that_go_wrong())
    results.extend(requests())
    results.extend(settings_window_redraws_its_colour())
    print(f"PASS {sum(results)}/{len(results)}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
