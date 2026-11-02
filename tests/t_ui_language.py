"""The To 3D page follows the page's language: the option reaches Python, every
placeholder of the template has text in every language, and an exported page
carries the text of the language in force and no leftover placeholder. The fold
notes, drawn on the boards by the backend, follow it too, and the notes on show
are drawn again when it changes."""
import os
import re
import sys
import tempfile
import types

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))

import auto_mapping as am  # noqa: E402
import python_hooks  # noqa: E402
import ui_language  # noqa: E402

PLACEHOLDER = re.compile(r"__T_[A-Z]+__")
SURFACE = {
    "vertices": [(0.0, 0.0, 0.0), (1.0, 0.0, 0.0), (0.0, 1.0, 0.0)],
    "faces": [(0, 1, 2)],
    "colors": [(255, 255, 255)],
    "strokes": [],
}


def report(name, ok):
    print(f"[{'OK' if ok else 'FAIL'}] {name}")
    return ok


def send(value):
    python_hooks.dispatch({"event": "option", "hook": ui_language.OPTION_HOOK, "value": value})


def exported():
    with tempfile.TemporaryDirectory() as folder:
        path = os.path.join(folder, "to3d.html")
        am._export_three_html(SURFACE, path)
        with open(path, encoding="utf-8") as handle:
            return handle.read()


def fold_notes():
    slots = lambda text: sorted(re.findall(r"\{\d\}", text))
    say = ui_language.text
    results = []
    tables = ui_language._BOARD_TEXT
    results.append(report("every language has every fold-note key", len({frozenset(t) for t in tables.values()}) == 1))
    results.append(report("and the same {0} slots as the English", all(
        slots(text) == slots(source) for table in tables.values() for source, text in table.items())))

    note = lambda: say("The {0} axis on the {1} board", "V", say("texture"))
    results.append(report("English notes read as before", note() == "The V axis on the texture board"
                          and say("doubles back {0} px from its end", "18") == "doubles back 18 px from its end"))

    pushed = []
    original = am._push_overlay
    am._push_overlay = pushed.append
    try:
        am._FOLD_NOTES["regions"] = [{"view": "main", "point": (0.0, 0.0), "text": say("Fold here")}]
        send("ja")
        results.append(report("Japanese notes", note() == "テクスチャボードの V 軸線が" and say("Fold here") == "ここで折り返し"))
        results.append(report("the notes on show are drawn again", am._FOLD_NOTES["regions"] is None
                              and sorted(pushed) == ["child", "main"]))
        send("ja")
        results.append(report("the same language draws nothing again", len(pushed) == 2))
        send("zh-CN")
        results.append(report("Chinese notes, nothing on show to draw", note() == "纹理板上的 V 轴线" and len(pushed) == 2))
    finally:
        am._push_overlay = original
        send("en")
        am._FOLD_NOTES["regions"] = None
    return results


def main():
    used = set(PLACEHOLDER.findall(am._TO3D_TEMPLATE))
    results = [report("the template has placeholders", bool(used))]
    for language, table in ui_language._TO3D_TEXT.items():
        results.append(report(f"{language} fills exactly the template's placeholders", set(table) == used))

    language_of = lambda: ui_language.to3d_text()["__T_LANG__"]
    results.append(report("English is the start", language_of() == "en"))
    html = exported()
    results.append(report("the English page reads as before", 'lang="en"' in html and "Relief scale" in html))
    results.append(report("the English page has no placeholder left", not PLACEHOLDER.search(html)))

    send("ja")
    results.append(report("the option switches the text", language_of() == "ja"))
    html = exported()
    results.append(report("the page is written in the language in force", 'lang="ja"' in html and "レリーフの倍率" in html))
    results.append(report("and has no placeholder left", not PLACEHOLDER.search(html)))

    send("klingon")
    results.append(report("an unknown language is ignored", language_of() == "ja"))
    python_hooks.dispatch({"event": "option", "hook": "color", "value": "zh-CN"})
    results.append(report("another option's hook is ignored", language_of() == "ja"))
    send("zh-CN")
    results.append(report("Chinese works too", "浮雕比例" in exported()))
    send("en")

    results.extend(fold_notes())
    print(f"PASS {sum(results)}/{len(results)}")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
