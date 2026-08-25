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
  region instead of stacking a duplicate),
- which line layer an auto-created fill layer TRACKS, and what tracking
  means (re-derive the child's regions from their stored seeds whenever the
  parent's topology changes).

The click is offered through the "fillrequest" hook event BEFORE the
built-in fill runs; setting message["handled"] suppresses the C++ fallback.

Layer parenting is the C++ mechanism (AnimeColumn::parentLayerId, stored by
stable column id and validated in normalizeLayerTree); it carries no
behaviour of its own. Everything a parent link MEANS - when it is set, when
it is honoured, when the user may cut it - is decided here.

Extension point: a future fuzzy fill (closing small gaps so nearly-closed
shapes still fill) replaces _region_path() here - snap gap endpoints /
bridge segments in Python - with zero C++ changes. Fill is a single click,
not a per-frame interaction, so Python-side geometry is acceptable.
"""

import python_hooks

TO_INDEPENDENT_ACTION = "fill_to_independent"

# Events after which a tracked child is re-derived. deletefinish covers both
# Cut Line and Delete Line (C++ shares the name); historyrestore covers undo,
# redo and opening a file.
TOPOLOGY_EVENTS = ("linefinish", "erasefinish", "deletefinish", "historyrestore")

# The re-trace writes fill regions, and a future dispatcher on that write (or
# a refresh that re-enters through historyrestore) would call us again while
# we are still inside the first pass. Module level, like auto_mapping's run
# guard, because the recursion it stops is across hook dispatches, not within
# one call.
_RETRACE_GUARD = {"depth": 0}


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
    created_layer = False
    if not original_is_fill:
        target_layer = scene.add_fill_layer()
        created_layer = True
    if target_layer < 0:
        return
    scene.set_current_layer(target_layer)

    if created_layer and source_layer >= 0:
        # This fill layer was BORN from one line layer's topology, so it can
        # follow it: park the parent link now, while the source is still
        # unambiguous. Scope All has no single source to follow, and a fill
        # layer the user already had keeps whatever parenting they chose -
        # a click must never silently re-home an existing layer.
        try:
            scene.set_layer_parent_id(target_layer, scene.layer_id_at(source_layer))
        except Exception as error:
            # Never at the cost of the fill itself: the region below is the
            # artwork, the parent link is only how it keeps up.
            print(f"[fill] could not track the source layer: {error}")

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


# --- tracked children: follow the parent layer's topology -------------------

def _child_layer_indices(scene, structure, layer_index):
    """Layers whose parent link names `layer_index`, in panel order.

    Read off the structure rather than through child_layer_indices() so one
    snapshot answers for every layer in the pass; the ids in it are stable
    column ids, so nothing here shifts if a layer moves mid-gesture.
    """
    parent_id = scene.layer_id_at(layer_index)
    if not parent_id:
        return []
    return [layer["index"] for layer in structure["layers"]
            if layer.get("parent_layer_id") == parent_id]


def _boundary_bounds(scene, frame, layer_index, cache):
    """The wall bounds fill_boundary_path_at would trace against, cached per
    scope for the pass. None when the frame carries no walls at all."""
    if layer_index in cache:
        return cache[layer_index]
    bounds = scene.fill_boundary_bounds(frame, layer_index)
    if bounds is not None:
        bounds = (float(bounds["x"]), float(bounds["y"]),
                  float(bounds["width"]), float(bounds["height"]))
    cache[layer_index] = bounds
    return bounds


def _retrace_child(scene, frame, parent_index, child_index, cache):
    """Re-derive every region of one tracked layer from its stored seed.

    A seed that no longer resolves KEEPS its old path: topology tracking may
    reshape artwork, never delete it. Returns how many regions changed.
    """
    image = scene.image_at(frame, child_index, False, "fill")
    if image is None:
        return 0
    changed = 0
    for info in image.fill_regions_info():
        # The region's OWN stored scope decides which walls it is re-derived
        # against, not the parent link: an all-layers fill living in a
        # tracked layer is still bounded by every visible layer, and tracing
        # it against one column would shrink it to nothing.
        if info["based_on_all_layers"]:
            if info["source_layer_index"] != -1:
                continue  # flags disagree: leave a record we cannot read alone
            layer_index = -1
        elif info["source_layer_index"] not in (-1, parent_index):
            continue  # bounded by some OTHER column; this event is not about it
        else:
            layer_index = parent_index
        bounds = _boundary_bounds(scene, frame, layer_index, cache)
        if bounds is None:
            continue  # no walls left on the frame: keep what is drawn
        seed_info = info["seed"] or {}
        seed = (float(seed_info.get("x", 0.0)), float(seed_info.get("y", 0.0)))
        path = _region_path(scene, frame, seed, bounds, layer_index)
        if path is None:
            continue
        image.set_fill_region(info["index"], path=path)
        changed += 1
    return changed


def _retrace_children(scene, message):
    structure = scene.get_structure()
    frame = scene.current_frame()
    if frame < 0 or frame >= structure["frame_count"]:
        return 0
    cell = message.get("cell") or {}
    parent_index = cell.get("layer")
    if not isinstance(parent_index, int) or parent_index < 0:
        return 0
    children = _child_layer_indices(scene, structure, parent_index)
    if not children:
        return 0
    cache = {}
    changed = 0
    for child_index in children:
        changed += _retrace_child(scene, frame, parent_index, child_index, cache)
    return changed


def _topology_changed(message):
    """The parent layer's lines moved; its tracked fills follow.

    Deliberately commits NOTHING: this rides the gesture that caused it, so
    the stroke and the re-traced fills land in one history entry - and a
    restore-triggered pass must not push an entry on top of the restore.
    """
    if _RETRACE_GUARD["depth"]:
        return
    view = message.get("view") or "main"
    _RETRACE_GUARD["depth"] += 1
    try:
        scene = _scene_model(view)
        if _retrace_children(scene, message):
            _animean().ui.widget.refresh()
    except Exception:
        import traceback

        print(f"[fill] tracked-layer retrace failed:\n{traceback.format_exc()}")
    finally:
        _RETRACE_GUARD["depth"] -= 1


# --- context menu: cut the link, keep the artwork ---------------------------

def _fill_menu_items(context):
    """Offer the release entry on a row that tracks another layer."""
    if context.get("kind") != "layer":
        return []
    if not int(context.get("parent_layer_id") or 0):
        return []
    return [{"name": TO_INDEPENDENT_ACTION, "title": "To Independent Layer"}]


def _fill_menu_action(message):
    if (message.get("action") or "") != TO_INDEPENDENT_ACTION:
        return
    layer = message.get("layer")
    if not isinstance(layer, int) or layer < 0:
        return
    view = message.get("view") or "main"
    try:
        scene = _scene_model(view)
    except Exception as error:
        print(f"[fill] release skipped: {error}")
        return
    # Only the link goes: the regions stay exactly as they are drawn, which
    # is the whole point of "keep the artwork, stop following".
    scene.set_layer_parent_id(layer, 0)
    animean = _animean()
    animean.ui.layer.refresh()
    animean.ui.history_commit("To Independent Layer", view)


def register_hooks():
    python_hooks.set_hook(_fill_request, fillrequest=True)
    python_hooks.set_hook(_topology_changed,
                          **{event: True for event in TOPOLOGY_EVENTS})
    python_hooks.set_hook(_fill_menu_action, layermenu=True)
    python_hooks.register_menu_provider(_fill_menu_items)


register_hooks()
