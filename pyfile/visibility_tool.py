"""Layer visibility policy: the Python side of the panel checkbox.

Event flow (the pattern every UI action is migrating to):

    layer panel checkbox -> C++ sends a "visibility" hook event
        -> this module decides what the toggle means
        -> applies it through the bindings (scene.set_layer_visible)
        -> sets message["handled"] = True
    C++ only falls back to a direct model write when no hook handled the
    event (builds without Python).

Today the policy is "apply exactly what the user asked", but rules like
"solo mode", "never hide the last visible layer" or linked-layer groups all
belong here, not in C++.
"""

import python_hooks


def _scene_model(view_name):
    """Resolve a view's SceneModel (same lookup repulsion_tool uses)."""
    import __main__

    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model

    import animean_python

    wanted = f"{view_name}_paint_view"
    for info in animean_python.get_scene():
        if info.get("sceneName") == wanted:
            return info["scene"]
    raise RuntimeError(f"scene for view '{view_name}' is not registered")


def set_layer_visible(view, layer_index, visible):
    """Command entry point: also usable directly from scripts/debug pane."""
    import animean_python

    scene = _scene_model(view)
    scene.set_layer_visible(int(layer_index), bool(visible))
    animean_python.ui.layer.refresh()
    animean_python.ui.widget.refresh()


def _visibility_event(cell, stroke, message):
    view = message.get("view") or "main"
    layer_index = int(message.get("layer", -1))
    if layer_index < 0:
        return
    try:
        set_layer_visible(view, layer_index, bool(message.get("visible", True)))
    except Exception as error:
        print(f"[visibility] failed: {error}")
        return
    message["handled"] = True


def register_hooks():
    python_hooks.set_hook(_visibility_event, visibility=True)


register_hooks()
