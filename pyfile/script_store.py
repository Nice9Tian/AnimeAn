"""Small namespaced store over AnimeSceneModel.scriptData.

``scriptData`` is shared by every Python tool.  Treating it as one tool's
private blob makes the next tool that saves state erase all earlier state, so
all new tools update one top-level key and preserve the rest.
"""

import json


def read_all(scene):
    try:
        raw = scene.script_data()
        data = json.loads(raw) if raw else {}
    except (TypeError, ValueError, json.JSONDecodeError):
        return {}
    return data if isinstance(data, dict) else {}


def read(scene, key, default=None):
    value = read_all(scene).get(key, default)
    return value


def write(scene, key, value):
    data = read_all(scene)
    if value is None:
        data.pop(key, None)
    else:
        data[key] = value
    scene.set_script_data(json.dumps(data, ensure_ascii=False, separators=(",", ":")))
