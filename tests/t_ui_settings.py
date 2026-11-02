"""The Settings menu Python declares: what it offers given what the page
reported, and what a choice broadcasts for the page to apply."""
import json
import os
import sys
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))


class FakeUi:
    def __init__(self):
        self.broadcasts = []

    def broadcast(self, key, text):
        self.broadcasts.append((key, json.loads(text)))

    def set_hook_events(self, events):
        pass


UI = FakeUi()
sys.modules["animean_python"] = types.SimpleNamespace(ui=UI)

import python_hooks  # noqa: E402
import ui_language  # noqa: E402
import ui_settings  # noqa: E402


def report(name, ok):
    print(f"[{'OK' if ok else 'FAIL'}] {name}")
    return ok


def settings_menu():
    menus = json.loads(python_hooks.menus_json("main"))
    return next((m for m in menus if m["name"] == "Settings"), None)


def radios(menu, group):
    sub = next(item for item in menu["items"] if item["name"] == group)
    return {item["name"]: (item["checked"], item.get("enabled", True)) for item in sub["items"]}


def report_option(name, value, hook=ui_settings.REPORT_HOOK):
    python_hooks.dispatch({"event": "option", "hook": hook, "name": name, "value": value})


def choose(item, menu="Settings"):
    python_hooks.dispatch({"event": "menu", "menu": menu, "name": item})


def main():
    menu = settings_menu()
    results = [report("Settings is a main-bar menu placed after Windows", menu is not None
                      and menu["title"] == "Settings" and menu["place"] == "end")]
    results.append(report("it holds Board Renderer and Language", [i["title"] for i in menu["items"]] == ["Board Renderer", "Language"]))
    results.append(report("WebGL is ticked at first", radios(menu, "renderer") == {
        "renderer.webgl": (True, True), "renderer.canvas2d": (False, True)}))
    results.append(report("languages are listed in their own writing", [i["title"] for i in menu["items"][1]["items"]]
                          == ["English", "简体中文", "日本語"]))

    report_option("renderer", "canvas2d")
    report_option("webgl", False)
    menu = settings_menu()
    results.append(report("the page's report ticks Canvas 2D and greys out WebGL", radios(menu, "renderer") == {
        "renderer.webgl": (False, False), "renderer.canvas2d": (True, True)}))
    choose("renderer.webgl")
    results.append(report("WebGL cannot be chosen where it does not work", UI.broadcasts == []))

    python_hooks.dispatch({"event": "option", "hook": ui_language.OPTION_HOOK, "value": "ja"})
    results.append(report("the language ticks what the page reported", radios(settings_menu(), "language")["language.ja"] == (True, True)))

    report_option("webgl", True)
    choose("renderer.webgl")
    results.append(report("choosing a renderer broadcasts it with the language in force", UI.broadcasts == [
        ("py/preferences", {"renderer": "webgl", "language": "ja"})]))
    results.append(report("the menu waits for the page to report the switch", radios(settings_menu(), "renderer")["renderer.canvas2d"] == (True, True)))
    choose("language.zh-CN")
    results.append(report("choosing a language broadcasts it with the renderer in force", UI.broadcasts[-1] == (
        "py/preferences", {"renderer": "canvas2d", "language": "zh-CN"})))
    count = len(UI.broadcasts)
    choose("language.klingon")
    choose("renderer.webgl", menu="Draw")
    report_option("renderer", "webgl", hook="color")
    results.append(report("unknown items, other menus and other hooks change nothing",
                          len(UI.broadcasts) == count and ui_settings.renderer() == "canvas2d"))
    python_hooks.dispatch({"event": "option", "hook": ui_language.OPTION_HOOK, "value": "en"})
    print(f"PASS {sum(results)}/{len(results)}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
