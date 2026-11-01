"""The language the page is showing, for the one screen Python writes itself.

Menus, options and status lines stay in English here: the page translates them
where it shows them (webui/src/i18n/). To 3D is different: it writes a page of
its own that the system browser opens, so its text has to be chosen here. The
page reports its language as a tool option; C++ only relays it.
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

_STATE = {"language": "en"}


def to3d_text():
    """The placeholders of the To 3D page and their text in the current language."""
    return _TO3D_TEXT[_STATE["language"]]


def _option_changed(message):
    if message.get("hook") != OPTION_HOOK:
        return
    value = str(message.get("value") or "")
    if value in _TO3D_TEXT:
        _STATE["language"] = value


python_hooks.set_hook(_option_changed, option=True)
