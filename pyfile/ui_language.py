"""The language the page is showing, for the text Python puts on screen itself.

Menus, options and status lines stay in English here: the page translates them
where it shows them (webui/src/i18n/). Two kinds of text never reach the page as
text, so theirs has to be chosen here: To 3D writes a page of its own that the
system browser opens, and the notes that point at a fold are drawn on the boards
by the backend. The page reports its language as a tool option; C++ only relays it.
"""

import python_hooks

OPTION_HOOK = "ui_language"

_TO3D_TEXT = {
    "en": {
        "__T_LANG__": "en",
        "__T_SHEET__": "reconstructed sheet",
        "__T_RELIEF__": "Relief scale",
        "__T_FLIP__": "flip relief",
        "__T_WIRE__": "wireframe",
        "__T_CAMERA__": "original camera",
        "__T_GRID__": "Transfer Grid",
        "__T_HELP__": "drag: rotate &middot; wheel: zoom &middot; right-drag: pan",
    },
    "zh-CN": {
        "__T_LANG__": "zh-CN",
        "__T_SHEET__": "重建的纸面",
        "__T_RELIEF__": "浮雕比例",
        "__T_FLIP__": "翻转浮雕",
        "__T_WIRE__": "线框",
        "__T_CAMERA__": "原始相机",
        "__T_GRID__": "Transfer Grid",
        "__T_HELP__": "拖动：旋转 &middot; 滚轮：缩放 &middot; 右键拖动：平移",
    },
    "ja": {
        "__T_LANG__": "ja",
        "__T_SHEET__": "再構成したシート",
        "__T_RELIEF__": "レリーフの倍率",
        "__T_FLIP__": "レリーフを反転",
        "__T_WIRE__": "ワイヤーフレーム",
        "__T_CAMERA__": "元のカメラ",
        "__T_GRID__": "Transfer Grid",
        "__T_HELP__": "ドラッグ：回転 &middot; ホイール：ズーム &middot; 右ドラッグ：移動",
    },
}

# The fold notes, keyed by their English text as the page's catalogs are; {0},
# {1}... are filled in after the lookup. "texture" and "main" name the boards.
_BOARD_TEXT = {
    "zh-CN": {
        "Fold here": "此处折叠",
        "The {0} axis on the {1} board": "{1}上的 {0} 轴线",
        "texture": "纹理板",
        "main": "主画板",
        "doubles back {0} px from its end": "在距端点 {0} px 处折回",
        "Fold: the pattern turns over here": "折叠：花纹在这里翻到背面",
    },
    "ja": {
        "Fold here": "ここで折り返し",
        "The {0} axis on the {1} board": "{1}の {0} 軸線が",
        "texture": "テクスチャボード",
        "main": "メインボード",
        "doubles back {0} px from its end": "端から {0} px で折り返しています",
        "Fold: the pattern turns over here": "折り返し：模様はここで裏返ります",
    },
}

_STATE = {"language": "en"}
_LISTENERS = []


def current():
    """The language the page last reported: "en", "zh-CN" or "ja"."""
    return _STATE["language"]


def to3d_text():
    """The placeholders of the To 3D page and their text in the current language."""
    return _TO3D_TEXT[_STATE["language"]]


def text(source, *values):
    """source, an English key of _BOARD_TEXT, in the current language, with
    {0}, {1}... filled from values."""
    return _BOARD_TEXT.get(_STATE["language"], {}).get(source, source).format(*values)


def on_change(listener):
    """listener() runs after the page switches to another language, so text
    already drawn can be drawn again."""
    _LISTENERS.append(listener)


def _option_changed(message):
    if message.get("hook") != OPTION_HOOK:
        return
    value = str(message.get("value") or "")
    if value not in _TO3D_TEXT or value == _STATE["language"]:
        return
    _STATE["language"] = value
    for listener in list(_LISTENERS):
        try:
            listener()
        except Exception as error:
            print(f"[ui_language] redraw after the language change failed: {error}")


python_hooks.set_hook(_option_changed, option=True)
