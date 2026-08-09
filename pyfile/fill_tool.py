"""Fill tool policy: what a fill click MEANS lives here, not in C++.

C++ keeps only the mechanisms: collecting boundary segments and tracing the
region around the seed (scene.fill_boundary_path_at), storing fill regions
(image.add_fill_region / set_fill_region / remove_fill_area) and creating
fill layers (scene.add_fill_layer). This module decides:

- which layer the fill lands on (auto-create a Fill layer when the current
  layer is not one),
- scope handling (a fill layer cannot bound itself, so Current upgrades to
  ALL - the built-in C++ fallback does the same silently),
- what a repeated click inside an existing region does (recolor/update the
  region instead of stacking a duplicate).

The click is offered through the "fillrequest" hook event BEFORE the
built-in fill runs; setting message["handled"] suppresses the C++ fallback.

Extension point: a future fuzzy fill (closing small gaps so nearly-closed
shapes still fill) replaces _region_path() here - snap gap endpoints /
bridge segments in Python - with zero C++ changes. Fill is a single click,
not a per-frame interaction, so Python-side geometry is acceptable.
"""

import python_hooks


def _animean():
    import animean_python
    return animean_python


def _scene_model(view_name):
    """Resolve a view's SceneModel (same lookup repulsion_tool uses)."""
    import __main__

    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model

    wanted = f"{view_name}_paint_view"
    for info in _animean().get_scene():
        if info.get("sceneName") == wanted:
            return info["scene"]
    raise RuntimeError(f"scene for view '{view_name}' is not registered")


def _layer_info(structure, index):
    for layer in structure["layers"]:
        if layer["index"] == index:
            return layer
    return None


def _region_path(scene, frame, seed, bounds, layer_index):
    """Path COMMANDS of the region around the seed, or None when the seed
    sits in an open area. Swap this out for fuzzy fill later."""
    path = scene.fill_boundary_path_at(frame, seed, bounds, layer_index)
    if path is None:
        return None
    return path["commands"]


def _dispatch_fillfinish(message, cell):
    """Let observers (repulsion baseline, etc.) see the fill regardless of
    whether the Python policy or the C++ fallback performed it. `cell` must
    describe where the fill LANDED (the C++ path reads the model after
    fillAt, so its fillfinish points at the target fill layer). Returns True
    when a hook vetoed the history commit."""
    notify = {
        "event": "fillfinish",
        "view": message.get("view"),
        "tool": message.get("tool"),
        "base_tool": message.get("base_tool"),
        "property": message.get("property"),
        "cell": cell,
        "stroke": {},
        "position": message.get("position", {}),
        "delta": message.get("delta", {}),
    }
    python_hooks.dispatch(notify)
    return bool(notify.get("cancel_history"))


def _fill_request(cell, stroke, message):
    view = message.get("view") or "main"
    scene = _scene_model(view)
    structure = scene.get_structure()

    frame = scene.current_frame()
    if frame < 0 or frame >= structure["frame_count"]:
        return  # nothing to fill; C++ fallback will refuse the same way

    # Own the click from here on. Everything below mutates the model, so a
    # failure halfway through must surface as "this fill failed", never as
    # "unhandled" - the C++ fallback rerunning on a half-updated model would
    # stack a second region with different scope semantics.
    message["handled"] = True
    try:
        _run_fill(scene, structure, frame, message)
    except Exception:
        import traceback

        print(f"[fill] failed:\n{traceback.format_exc()}")


def _run_fill(scene, structure, frame, message):
    view = message.get("view") or "main"
    position = message.get("position") or {}
    seed = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
    bounds_info = message.get("bounds") or {}
    bounds = (
        float(bounds_info.get("x", 0.0)),
        float(bounds_info.get("y", 0.0)),
        float(bounds_info.get("width", 0.0)),
        float(bounds_info.get("height", 0.0)),
    )
    if not (bounds[0] <= seed[0] <= bounds[0] + bounds[2]
            and bounds[1] <= seed[1] <= bounds[1] + bounds[3]):
        return  # off-canvas click: a deliberate no-op

    original_layer = scene.current_layer()
    original_info = _layer_info(structure, original_layer)
    if original_info is not None and original_info.get("locked"):
        return  # locked layer never accepts a fill
    original_is_fill = bool(original_info and original_info.get("type") == "fill")

    all_layers = (message.get("fill_scope") == "all")
    if original_is_fill and not all_layers:
        # A fill layer carries no boundary strokes, so "Current" would find
        # no walls at all; widen to every visible layer.
        all_layers = True
    source_layer = -1 if all_layers else original_layer

    boundary_layer = -1 if (all_layers or original_layer < 0) else original_layer
    path = _region_path(scene, frame, seed, bounds, boundary_layer)
    if path is None:
        return  # open region: same refusal as the C++ path

    color_info = message.get("color") or {}
    color = (int(color_info.get("r", 0)), int(color_info.get("g", 0)),
             int(color_info.get("b", 0)), int(color_info.get("a", 255)))
    property_value = message.get("property") or ""

    target_layer = original_layer
    if not original_is_fill:
        target_layer = scene.add_fill_layer()
    if target_layer < 0:
        return
    scene.set_current_layer(target_layer)

    image = scene.image_at(frame, target_layer, True, "fill")
    if image is None:
        return

    # Clicking inside an existing compatible region recolors/re-traces it
    # (and collapses duplicates) instead of stacking a new one on top.
    # Removing only ever drops the CURRENT (descending) index, so the info
    # snapshot stays valid for the lower indices still to visit.
    infos = image.fill_regions_info()
    updated_existing = False
    for index in range(len(infos) - 1, -1, -1):
        info = infos[index]
        same_refer = bool(info["based_on_all_layers"]) == all_layers
        same_source = all_layers or info["source_layer_index"] == source_layer
        same_property = info["property"] == property_value
        if not (same_refer and same_source and same_property
                and image.fill_region_contains(index, seed)):
            continue
        if not updated_existing:
            image.set_fill_region(index, path=path, color=color, seed=seed)
            updated_existing = True
        else:
            image.remove_fill_area(index)

    if not updated_existing:
        image.add_fill_region(
            path,
            color,
            property=property_value,
            seed=seed,
            source_layer_index=source_layer,
            based_on_all_layers=all_layers,
        )

    # Observers see where the fill LANDED, matching the C++ path (which
    # reads the model after fillAt has switched to the target layer).
    landed = scene.cell_at(frame, target_layer)
    landed_cell = {
        "row": frame,
        "layer": target_layer,
        "asset": landed.asset_index,
        "frame_id": landed.frame_id,
    }

    animean = _animean()
    animean.ui.refresh()
    if not _dispatch_fillfinish(message, landed_cell):
        animean.ui.history_commit("Fill", view)


def register_hooks():
    python_hooks.set_hook(_fill_request, fillrequest=True)


register_hooks()
