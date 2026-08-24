"""Transfer: free transform of the current layer, the way a drawing app does it.

The armed tool frames the layer's artwork in a box with eight grips - four
corners and four edge midpoints - plus the box body itself:

  corner grip  : scale in both axes, the OPPOSITE corner pinned.
                 Alt / Ctrl / Shift lock the aspect ratio.
  edge grip    : stretch along that edge's axis only, opposite edge pinned.
                 A modifier mirrors the stretch about the box centre.
  inside a box : translate. Alt / Ctrl / Shift constrain the move to the
                 horizontal or vertical, whichever the gesture leads with -
                 the same "commit to the axis you started on" rule the pen's
                 straight-line snap uses.

The C++ side is a pure mechanism: it renders and hit-tests the handles,
reports press/move/release (with the modifier state), and applies an affine
matrix to the image (AnimeVectorImageModel::transform). Every decision - what
the grips mean, where the anchor sits, how a modifier constrains - is here.

The transform is applied INCREMENTALLY (each move sends the delta between the
transform now and the transform last applied), so a long drag never
accumulates its own rounding, and one history entry lands on release.
"""

import python_hooks

# view -> {"box": [x0, y0, x1, y1], "layer": int, "frame": int, "drag": ...}
_SESSIONS = {}

BOX_COLOR = (60, 130, 240, 235)
GRIP_COLOR = (255, 255, 255, 255)
SHAPE_SQUARE = 0

# grip -> (anchor_x, anchor_y, scales_x, scales_y). The anchor is given in
# box-relative coordinates: 0 = the low edge, 1 = the high edge, 0.5 = centre.
GRIPS = {
    "nw": (1.0, 1.0, True, True),
    "ne": (0.0, 1.0, True, True),
    "se": (0.0, 0.0, True, True),
    "sw": (1.0, 0.0, True, True),
    "n": (0.5, 1.0, False, True),
    "s": (0.5, 0.0, False, True),
    "w": (1.0, 0.5, True, False),
    "e": (0.0, 0.5, True, False),
}
BODY = "body"
MIN_SPAN = 1e-3
# Tool option: whether Alt/Ctrl/Shift constrain (keep ratio, lock the axis)
# or do nothing. On by default - the drawing-app convention.
_STATE = {"constrain": True}


def _animean():
    import animean_python
    return animean_python


def _scene_model(view_name):
    import __main__
    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model
    import animean_python
    target = f"{view_name}_paint_view"
    for info in animean_python.get_scene():
        if info.get("sceneName") == target:
            return info["scene"]
    raise KeyError(f"no scene registered as {target!r}")


def _layer_locked(view, layer):
    """The per-layer padlock (and an invisible layer): the board-wide lock is
    enforced in C++, this one is not - Transfer would otherwise be the only
    tool that rewrites a locked layer."""
    try:
        structure = _scene_model(view).get_structure()
    except Exception:
        return False
    for entry in structure.get("layers") or []:
        if entry.get("index") == layer:
            return bool(entry.get("locked")) or not entry.get("visible", True)
    return False


def _current_image(view, create=False):
    scene = _scene_model(view)
    frame = scene.current_frame()
    layer = scene.current_layer()
    if frame < 0 or layer < 0:
        return None, frame, layer
    if _layer_locked(view, layer):
        return None, frame, layer
    try:
        image = scene.image_at(frame, layer, create, "vector")
    except Exception:
        image = None
    return image, frame, layer


def _layer_box(view):
    """The artwork's bounds on the current cell, or None when it is empty."""
    image, frame, layer = _current_image(view)
    if image is None:
        return None, frame, layer
    try:
        bounds = image.bounds()
    except Exception:
        return None, frame, layer
    if not bounds:
        return None, frame, layer
    x, y, w, h = (float(v) for v in bounds)
    if w <= MIN_SPAN and h <= MIN_SPAN:
        return None, frame, layer
    # A hairline layer (one horizontal stroke) still needs a grabbable box.
    if w < 8.0:
        x, w = x - 0.5 * (8.0 - w), 8.0
    if h < 8.0:
        y, h = y - 0.5 * (8.0 - h), 8.0
    return [x, y, x + w, y + h], frame, layer


# --- what the view shows -----------------------------------------------------

def _normalised(box):
    """The box as a rectangle: the transform works with signed extents (so a
    flip stays a flip), everything the user SEES wants them ordered."""
    x0, y0, x1, y1 = box
    return [min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)]


def _grip_positions(box):
    x0, y0, x1, y1 = _normalised(box)
    mx = 0.5 * (x0 + x1)
    my = 0.5 * (y0 + y1)
    return {
        "nw": (x0, y0), "n": (mx, y0), "ne": (x1, y0),
        "w": (x0, my), "e": (x1, my),
        "sw": (x0, y1), "s": (mx, y1), "se": (x1, y1),
    }


def _push(view, box):
    animean = _animean()
    if box is None:
        animean.ui.set_edit_handles(view, [])
        _push_overlay(view, None)
        return
    handles = [{"id": f"tf:{name}", "x": pos[0], "y": pos[1],
                "shape": SHAPE_SQUARE, "color": GRIP_COLOR}
               for name, pos in _grip_positions(box).items()]
    animean.ui.set_edit_handles(view, handles)
    _push_overlay(view, box)


def _push_overlay(view, box):
    """The box outline, drawn under the grips. Other scripts own overlay
    items too, so ours go through overlay_stack: this tool owns one slot of
    the composed display list, and replacing the list outright (or hand-
    merging auto_mapping's items) wiped every other owner's overlays."""
    import overlay_stack
    items = []
    if box is not None:
        x0, y0, x1, y1 = _normalised(box)
        items.append({
            "id": "transfer_box",
            "points": [(x0, y0), (x1, y0), (x1, y1), (x0, y1)],
            "closed": True,
            "color": BOX_COLOR,
            "width": 1.0,
            "pen_style": 2,      # dashed: a frame, not artwork
            "removable": False,
        })
    overlay_stack.set_items(view, "transfer_tool", items)


def _refresh(view):
    box, frame, layer = _layer_box(view)
    session = _SESSIONS.setdefault(view, {})
    session.update({"box": box, "frame": frame, "layer": layer, "drag": None})
    _push(view, box)
    return box


def _clear(view):
    _SESSIONS.pop(view, None)
    _push(view, None)


# --- the gesture -------------------------------------------------------------

def _inside(box, position):
    x0, y0, x1, y1 = _normalised(box)
    return x0 <= position[0] <= x1 and y0 <= position[1] <= y1


def _constrained(modifiers):
    if not _STATE["constrain"] or not isinstance(modifiers, dict):
        return False
    return bool(modifiers.get("constrain"))


def _option_changed(message):
    if message.get("hook") != "transfer_constrain":
        return
    _STATE["constrain"] = str(message.get("value", "on")).lower() != "off"
    print(f"[transfer] modifier constraint -> "
          f"{'on' if _STATE['constrain'] else 'off'}")


def _target_box(session, position, modifiers):
    """Where the box goes for this cursor position - the whole policy."""
    drag = session["drag"]
    start = drag["start_box"]
    grip = drag["grip"]
    origin = drag["origin"]
    x0, y0, x1, y1 = start

    if grip == BODY:
        dx = position[0] - origin[0]
        dy = position[1] - origin[1]
        if _constrained(modifiers):
            # Commit to the axis the gesture leads with, exactly like the
            # pen's axis snap: the larger displacement wins, the other is
            # zeroed.
            if abs(dx) >= abs(dy):
                dy = 0.0
            else:
                dx = 0.0
        return [x0 + dx, y0 + dy, x1 + dx, y1 + dy]

    anchor_x, anchor_y, scales_x, scales_y = GRIPS[grip]
    ax = x0 + anchor_x * (x1 - x0)
    ay = y0 + anchor_y * (y1 - y0)

    if _constrained(modifiers) and scales_x != scales_y:
        # Edge grip with a modifier: mirror the stretch about the centre, so
        # the box grows from both sides at once.
        cx = 0.5 * (x0 + x1)
        cy = 0.5 * (y0 + y1)
        if scales_x:
            half = max(abs(position[0] - cx), MIN_SPAN)
            return [cx - half, y0, cx + half, y1]
        half = max(abs(position[1] - cy), MIN_SPAN)
        return [x0, cy - half, x1, cy + half]

    scale_x = 1.0
    scale_y = 1.0
    if scales_x:
        moving = x0 if anchor_x > 0.5 else x1
        base = moving - ax
        if abs(base) > MIN_SPAN:
            scale_x = (position[0] - ax) / base
    if scales_y:
        moving = y0 if anchor_y > 0.5 else y1
        base = moving - ay
        if abs(base) > MIN_SPAN:
            scale_y = (position[1] - ay) / base

    if scales_x and scales_y and _constrained(modifiers):
        # Aspect lock on a corner: one magnitude for both axes, each keeping
        # its own sign so a flip through the anchor still flips.
        magnitude = max(abs(scale_x), abs(scale_y))
        scale_x = magnitude if scale_x >= 0.0 else -magnitude
        scale_y = magnitude if scale_y >= 0.0 else -magnitude

    # SIGNED on purpose: normalising here (min/max) threw away the flip, so
    # dragging a grip past its anchor slid the artwork instead of mirroring
    # it. The box is normalised only for DISPLAY (_normalised), while the
    # transform reads these signed extents.
    nx0 = ax + (x0 - ax) * scale_x
    nx1 = ax + (x1 - ax) * scale_x
    ny0 = ay + (y0 - ay) * scale_y
    ny1 = ay + (y1 - ay) * scale_y
    return [nx0, ny0, nx1, ny1]


def _affine(start, target):
    """(sx, sy, tx, ty) taking the start box onto the target box.

    The spans are SIGNED: a target whose x1 has crossed its x0 is a mirror,
    and clamping the denominator by magnitude keeps a degenerate start from
    dividing by zero without turning a flip into a slide."""
    span_x = start[2] - start[0]
    span_y = start[3] - start[1]
    if abs(span_x) < MIN_SPAN:
        span_x = MIN_SPAN if span_x >= 0.0 else -MIN_SPAN
    if abs(span_y) < MIN_SPAN:
        span_y = MIN_SPAN if span_y >= 0.0 else -MIN_SPAN
    sx = (target[2] - target[0]) / span_x
    sy = (target[3] - target[1]) / span_y
    tx = target[0] - sx * start[0]
    ty = target[1] - sy * start[1]
    return sx, sy, tx, ty


def _apply(view, session, target):  # noqa: C901 - one policy, read top-down
    """Move the artwork from where it currently IS onto `target`.

    The matrix sent is (transform to target) composed with the inverse of
    the transform already applied, so a long drag never accumulates its own
    rounding and a drag that returns to the start returns the artwork too.
    """
    image, frame, layer = _current_image(view)
    if image is None:
        return
    if frame != session.get("frame") or layer != session.get("layer"):
        # The current cell changed under the box (a layer or frame click
        # mid-session). Transforming THIS cell with a box that frames
        # ANOTHER one would fling it across the canvas; re-frame instead.
        _refresh(view)
        return
    drag = session["drag"]
    sx, sy, tx, ty = _affine(drag["start_box"], target)
    psx, psy, ptx, pty = drag["applied"]
    if abs(psx) < 1e-9 or abs(psy) < 1e-9:
        return
    # delta = T . T_prev^-1, per axis (this tool never produces a rotation).
    dsx = sx / psx
    dsy = sy / psy
    dtx = tx - dsx * ptx
    dty = ty - dsy * pty
    if abs(dsx) < 1e-6 or abs(dsy) < 1e-6:
        return   # a fully collapsed box would destroy the artwork
    if (abs(dsx - 1.0) < 1e-12 and abs(dsy - 1.0) < 1e-12
            and abs(dtx) < 1e-9 and abs(dty) < 1e-9):
        # Nothing to do. Applying the identity anyway marked the drag as
        # changed, so a plain click committed a history entry and truncated
        # the redo tail - and it re-ran the whole image transform per move.
        return
    image.transform(dsx, 0.0, 0.0, dsy, dtx, dty)
    drag["applied"] = (sx, sy, tx, ty)
    drag["changed"] = True
    session["box"] = list(target)


# --- events ------------------------------------------------------------------

def _handle_event(message):
    if message.get("base_tool") != "transfer":
        return
    view = message.get("view") or "main"
    phase = message.get("phase")
    position = message.get("position") or {}
    point = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
    modifiers = message.get("modifiers") or {}

    if phase == "arm":
        if _refresh(view) is None:
            print("[transfer] this layer is empty - draw something to transform.")
        return
    if phase == "cancel":
        _clear(view)
        return

    session = _SESSIONS.get(view)
    if session is None or not session.get("box"):
        if phase in ("pick", "press"):
            _refresh(view)
        return

    if phase == "pick":
        if _inside(session["box"], point):
            # Claim the gesture: C++ turns this press into a drag under the
            # id we answer with, so the moves arrive as "move" events.
            session["drag"] = {"grip": BODY, "origin": point,
                               "start_box": list(session["box"]),
                               "applied": (1.0, 1.0, 0.0, 0.0),
                               "changed": False}
            message["grab"] = f"tf:{BODY}"
            return
        # A click outside re-frames the box on whatever is current now.
        _refresh(view)
        return

    handle = str(message.get("handle") or "")
    if not handle.startswith("tf:"):
        return
    grip = handle[3:]

    if phase == "press":
        if grip not in GRIPS:
            return
        session["drag"] = {"grip": grip, "origin": point,
                           "start_box": list(session["box"]),
                           "applied": (1.0, 1.0, 0.0, 0.0),
                           "changed": False}
        return

    drag = session.get("drag")
    if drag is None:
        return

    if phase == "move":
        _apply(view, session, _target_box(session, point, modifiers))
        _push(view, session["box"])
        _animean().ui.refresh()
        return

    if phase == "release":
        _apply(view, session, _target_box(session, point, modifiers))
        changed = bool(drag.get("changed"))
        session["drag"] = None
        # Re-frame from the artwork itself: stroke widths grew with the
        # scale, so the true bounds are a hair wider than the dragged box.
        _refresh(view)
        animean = _animean()
        animean.ui.refresh()
        if not changed:
            # A click that transformed nothing must not burn a history entry
            # (and with it the redo tail).
            return
        try:
            animean.ui.history_commit("Transfer", view)
        except Exception:
            pass


def _history_restored(message):
    view = message.get("view") or "main"
    if view in _SESSIONS:
        _refresh(view)


def activate_transfer_tool(name="transfer", property_value=""):
    """Extra-tool entry point, for scripts that arm Transfer directly."""
    _refresh("main")
    return property_value


python_hooks.set_hook(_handle_event, handle=True)
python_hooks.set_hook(_history_restored, historyrestore=True)
python_hooks.set_hook(_option_changed, option=True)
