"""Compose independent Python-tool overlays onto the one C++ display list."""

_LAYERS = {"main": {}, "child": {}}


def _animean():
    import animean_python
    return animean_python


def items(view):
    merged = []
    for owner in sorted(_LAYERS.setdefault(view, {})):
        merged.extend(_LAYERS[view][owner])
    return merged


def set_items(view, owner, values):
    layers = _LAYERS.setdefault(view, {})
    values = list(values or [])
    if values:
        layers[str(owner)] = values
    else:
        layers.pop(str(owner), None)
    _animean().ui.set_overlay(view, items(view))


def clear_owner(owner):
    for view in tuple(_LAYERS):
        set_items(view, owner, [])
