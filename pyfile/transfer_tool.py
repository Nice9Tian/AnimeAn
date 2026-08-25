"""Transfer: free transform of the current layer, the way a drawing app does it.

The armed tool frames the layer's artwork in a box with eight grips - four
corners and four edge midpoints - plus the box body and, just outside each
corner, a rotation ring:

  corner grip  : scale in both axes, the OPPOSITE corner pinned.
                 Alt / Ctrl / Shift lock the aspect ratio.
  edge grip    : stretch along that edge's axis only, opposite edge pinned.
                 A modifier mirrors the stretch about the box centre.
  inside a box : translate. Alt / Ctrl / Shift constrain the move to the
                 horizontal or vertical, whichever the gesture leads with -
                 the same "commit to the axis you started on" rule the pen's
                 straight-line snap uses.
  corner ring  : rotate about the box centre. A modifier snaps to 15 degrees.

The box is a signed axis-aligned rect PLUS an angle about a fixed origin, never
a stored quad: the grips and the outline are the rect's points carried through
that rotation, and a scale drag runs the same signed-extent maths after
rotating the cursor back into the box's own frame. On release the transform is
committed and the box re-frames on the artwork's new bounds with the angle back
at zero - Photoshop's behaviour when a transform is accepted.

A raster cell refuses rotation. The model stores a bitmap as a top-left plus an
image (algorithm/animemodel.cpp), so an angle cannot be represented at all: the
ring stops claiming presses AND the pointer stops promising one, because a
rotate cursor over a press that will not rotate is a lie.

The C++ side is a pure mechanism: it renders and hit-tests the handles, reports
press/move/release/hover (with the modifier state and the zoom), draws whatever
pointer this module NAMES, and applies an affine matrix to the image
(AnimeVectorImageModel::transform). Every decision - what the grips mean, where
the anchor sits, how a modifier constrains, which pointer belongs to which
region - is here.

The transform is applied INCREMENTALLY (each move sends the delta between the
transform now and the transform last applied), so a long drag never
accumulates its own rounding, and one history entry lands on release.
"""

import math

import python_hooks
import viewscale

# view -> {"box": [x0, y0, x1, y1], "angle": radians, "origin": (x, y),
#          "layer": int, "frame": int, "rotatable": bool, "drag": ...}
_SESSIONS = {}
# view -> the cursor name last pushed. Hover arrives every 33 ms; re-naming the
# pointer it is already wearing is a cross-language call per tick for nothing.
_CURSORS = {}

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
ROTATE = "rot"
MIN_SPAN = 1e-3
# Half-size of a grip's hit box, mirroring kEditHandleHitPx in
# openglwidget.cpp, and the reach of the rotation ring outside a corner. Both
# are SCREEN px - they belong to the hand, not to the drawing - and go through
# viewscale, the one home of screen<->canvas conversion.
GRIP_HIT_PX = 7.0
ROTATE_RING_PX = 21.0
ROTATE_SNAP_DEG = 15.0
IDENTITY = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)
# Tool option: whether Alt/Ctrl/Shift constrain (keep ratio, lock the axis,
# snap the angle) or do nothing. On by default - the drawing-app convention.
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


def _rotatable(image):
    """Whether this cell may be turned: raster placement is a top-left plus a
    bitmap, so a rotation would come back as a scaled AABB with the mirror
    flags guessed from m11/m22 (algorithm/animemodel.cpp).

    A binding too old to answer counts as NOT rotatable. Losing the ring is
    loud and one rebuild away; silently mangling a bitmap is neither.
    """
    try:
        return not bool(image.has_raster())
    except Exception:
        return False


def _layer_box(view):
    """The artwork's bounds on the current cell and whether it may be rotated;
    the box is None when the cell is empty."""
    image, frame, layer = _current_image(view)
    if image is None:
        return None, frame, layer, False
    try:
        bounds = image.bounds()
    except Exception:
        return None, frame, layer, False
    if not bounds:
        return None, frame, layer, False
    x, y, w, h = (float(v) for v in bounds)
    if w <= MIN_SPAN and h <= MIN_SPAN:
        return None, frame, layer, False
    # A hairline layer (one horizontal stroke) still needs a grabbable box.
    if w < 8.0:
        x, w = x - 0.5 * (8.0 - w), 8.0
    if h < 8.0:
        y, h = y - 0.5 * (8.0 - h), 8.0
    return [x, y, x + w, y + h], frame, layer, _rotatable(image)


# --- matrices ----------------------------------------------------------------
# Every matrix here is a 6-tuple in QTransform's own argument order,
# (m11, m12, m21, m22, dx, dy), so one goes straight to image.transform(). Qt
# maps a point with
#     x' = m11 * x + m21 * y + dx
#     y' = m12 * x + m22 * y + dy
# so m12 and m21 are the OFF-diagonal terms a rotation needs - exactly what the
# old per-axis (sx, sy, tx, ty) form could not carry.

def _mat_apply(matrix, point):
    return (matrix[0] * point[0] + matrix[2] * point[1] + matrix[4],
            matrix[1] * point[0] + matrix[3] * point[1] + matrix[5])


def _mat_compose(after, first):
    """`first` applied, then `after`."""
    a11, a12, a21, a22, adx, ady = after
    b11, b12, b21, b22, bdx, bdy = first
    return (a11 * b11 + a21 * b12,
            a12 * b11 + a22 * b12,
            a11 * b21 + a21 * b22,
            a12 * b21 + a22 * b22,
            a11 * bdx + a21 * bdy + adx,
            a12 * bdx + a22 * bdy + ady)


def _mat_det(matrix):
    return matrix[0] * matrix[3] - matrix[2] * matrix[1]


def _mat_inverse(matrix):
    """None when the matrix is singular. The caller must bail rather than
    guess: the incremental chain divides by this, and a flattened step would
    take the artwork onto a line with no way back."""
    det = _mat_det(matrix)
    if abs(det) < 1e-18:
        return None
    i11 = matrix[3] / det
    i21 = -matrix[2] / det
    i12 = -matrix[1] / det
    i22 = matrix[0] / det
    return (i11, i12, i21, i22,
            -(i11 * matrix[4] + i21 * matrix[5]),
            -(i12 * matrix[4] + i22 * matrix[5]))


def _rotation(angle, origin):
    cos = math.cos(angle)
    sin = math.sin(angle)
    ox, oy = origin
    return (cos, sin, -sin, cos,
            ox - cos * ox + sin * oy,
            oy - sin * ox - cos * oy)


def _rotate_point(point, origin, angle):
    return _mat_apply(_rotation(angle, origin), point)


# --- the oriented box --------------------------------------------------------
# A state is the triple (box, angle, origin): the signed rect, and the turn the
# whole rect is read through. The origin is FIXED for as long as the angle
# lives, so scaling the rect cannot drag the rotation out from under the
# anchor - which is exactly what turning about "the box's current centre"
# would have done.

def _box_centre(box):
    return (0.5 * (box[0] + box[2]), 0.5 * (box[1] + box[3]))


def _normalised(box):
    """The box as a rectangle: the transform works with signed extents (so a
    flip stays a flip), everything the user SEES wants them ordered."""
    x0, y0, x1, y1 = box
    return [min(x0, x1), min(y0, y1), max(x0, x1), max(y0, y1)]


def _to_local(state, point):
    """The cursor in the box's own frame, so the axis-aligned maths below
    reads the same at any angle."""
    box, angle, origin = state
    if not angle:
        return tuple(point)
    return _rotate_point(point, origin, -angle)


def _box_matrix(state):
    """The affine taking the unit square onto this state's oriented box.

    The spans are SIGNED: a box whose x1 has crossed its x0 is a mirror, and
    clamping the denominator by magnitude keeps a degenerate box from dividing
    by zero without turning a flip into a slide.
    """
    box, angle, origin = state
    span_x = box[2] - box[0]
    span_y = box[3] - box[1]
    if abs(span_x) < MIN_SPAN:
        span_x = MIN_SPAN if span_x >= 0.0 else -MIN_SPAN
    if abs(span_y) < MIN_SPAN:
        span_y = MIN_SPAN if span_y >= 0.0 else -MIN_SPAN
    scale = (span_x, 0.0, 0.0, span_y, box[0], box[1])
    if not angle:
        return scale
    return _mat_compose(_rotation(angle, origin), scale)


def _affine(start, target):
    """The matrix carrying the START box - and everything drawn inside it -
    onto the TARGET box, both oriented. None when the start is degenerate."""
    inverse = _mat_inverse(_box_matrix(start))
    if inverse is None:
        return None
    return _mat_compose(_box_matrix(target), inverse)


def _delta_matrix(total, applied):
    """delta = T . T_prev^-1: what still has to happen to the artwork given
    that `applied` already happened. None when there is nothing to do, or when
    doing it would destroy the artwork."""
    previous = _mat_inverse(applied)
    if previous is None:
        return None
    delta = _mat_compose(total, previous)
    if (math.hypot(delta[0], delta[1]) < 1e-6
            or math.hypot(delta[2], delta[3]) < 1e-6):
        return None   # an axis collapsed onto nothing: no way back from that
    if (abs(delta[0] - 1.0) < 1e-12 and abs(delta[3] - 1.0) < 1e-12
            and abs(delta[1]) < 1e-12 and abs(delta[2]) < 1e-12
            and abs(delta[4]) < 1e-9 and abs(delta[5]) < 1e-9):
        # Nothing to do. Applying the identity anyway marked the drag as
        # changed, so a plain click committed a history entry and truncated
        # the redo tail - and it re-ran the whole image transform per move.
        return None
    return delta


# --- what the view shows -----------------------------------------------------

def _grip_positions(state):
    box, angle, origin = state
    x0, y0, x1, y1 = _normalised(box)
    mx = 0.5 * (x0 + x1)
    my = 0.5 * (y0 + y1)
    local = {
        "nw": (x0, y0), "n": (mx, y0), "ne": (x1, y0),
        "w": (x0, my), "e": (x1, my),
        "sw": (x0, y1), "s": (mx, y1), "se": (x1, y1),
    }
    if not angle:
        return local
    return {name: _rotate_point(pos, origin, angle)
            for name, pos in local.items()}


def _box_corners(state):
    box, angle, origin = state
    x0, y0, x1, y1 = _normalised(box)
    corners = [(x0, y0), (x1, y0), (x1, y1), (x0, y1)]
    if not angle:
        return corners
    return [_rotate_point(corner, origin, angle) for corner in corners]


def _state(session):
    """(box, angle, origin), the form every geometry helper here wants. None
    when the session frames nothing."""
    box = session.get("box")
    if not box:
        return None
    return (box, session.get("angle") or 0.0,
            session.get("origin") or _box_centre(box))


def _push(view, state):
    animean = _animean()
    if state is None:
        animean.ui.set_edit_handles(view, [])
        _push_overlay(view, None)
        return
    handles = [{"id": f"tf:{name}", "x": pos[0], "y": pos[1],
                "shape": SHAPE_SQUARE, "color": GRIP_COLOR}
               for name, pos in _grip_positions(state).items()]
    animean.ui.set_edit_handles(view, handles)
    _push_overlay(view, state)


def _push_overlay(view, state):
    """The box outline, drawn under the grips. Other scripts own overlay
    items too, so ours go through overlay_stack: this tool owns one slot of
    the composed display list, and replacing the list outright (or hand-
    merging auto_mapping's items) wiped every other owner's overlays."""
    import overlay_stack
    items = []
    if state is not None:
        items.append({
            "id": "transfer_box",
            "points": _box_corners(state),
            "closed": True,
            "color": BOX_COLOR,
            "width": 1.0,
            "pen_style": 2,      # dashed: a frame, not artwork
            "removable": False,
        })
    overlay_stack.set_items(view, "transfer_tool", items)


def _refresh(view):
    box, frame, layer, rotatable = _layer_box(view)
    session = _SESSIONS.setdefault(view, {})
    session.update({"box": box, "angle": 0.0,
                    "origin": _box_centre(box) if box else (0.0, 0.0),
                    "frame": frame, "layer": layer,
                    "rotatable": rotatable, "drag": None})
    _push(view, _state(session))
    return box


def _clear(view):
    _SESSIONS.pop(view, None)
    _push(view, None)
    _set_cursor(view, "")
    # Forget the name as well: C++ drops the request on every setTool, so a
    # remembered one would be a pointer nobody is actually wearing, and the
    # next arm would skip re-pushing it.
    _CURSORS.pop(view, None)


# --- the pointer -------------------------------------------------------------

# Grip -> the direction it pushes, in DOC coordinates (y grows downward, so
# "s" points down the screen). A resize pointer is a line rather than an arrow,
# so the heading folds into a half turn and opposite grips share one.
_GRIP_DIRECTION = {"e": 0.0, "se": 45.0, "s": 90.0, "sw": 135.0,
                   "w": 180.0, "nw": 225.0, "n": 270.0, "ne": 315.0}


def _resize_cursor(grip, angle):
    """The pointer for a grip on a box turned by `angle`. fdiag is the "\\"
    diagonal (nw-se while the box is upright), bdiag the "/" one."""
    heading = (_GRIP_DIRECTION[grip] + math.degrees(angle)) % 180.0
    if heading < 22.5 or heading >= 157.5:
        return "size_h"
    if heading < 67.5:
        return "size_fdiag"
    if heading < 112.5:
        return "size_v"
    return "size_bdiag"


def _corner_under(state, point, zoom):
    """The corner whose rotation ring holds this point, or None."""
    reach = viewscale.to_canvas_length(ROTATE_RING_PX, zoom)
    grips = _grip_positions(state)
    for name in ("nw", "ne", "se", "sw"):
        pos = grips[name]
        if math.hypot(point[0] - pos[0], point[1] - pos[1]) <= reach:
            return name
    return None


def _cursor_name(state, point, zoom, rotatable):
    """Which pointer belongs over this point - the whole cursor policy."""
    # The grip hit box is a screen-axis square, and doc->screen is a uniform
    # scale plus a pan, so it stays an axis square here. Scanned back to front
    # like editHandleAt, so the pointer names the grip a press would take.
    hit = viewscale.to_canvas_length(GRIP_HIT_PX, zoom)
    for name, pos in reversed(list(_grip_positions(state).items())):
        if abs(point[0] - pos[0]) <= hit and abs(point[1] - pos[1]) <= hit:
            return _resize_cursor(name, state[1])
    if _inside(state, point):
        return "size_all"
    if _corner_under(state, point, zoom) is not None:
        return "rotate" if rotatable else "arrow"
    return ""


def _set_cursor(view, name):
    if _CURSORS.get(view) == name:
        return
    _CURSORS[view] = name
    try:
        _animean().ui.set_cursor(view, name)
    except AttributeError:
        pass   # a binding older than the cursor API: the box still works


# --- the gesture -------------------------------------------------------------

def _inside(state, point):
    x0, y0, x1, y1 = _normalised(state[0])
    local = _to_local(state, point)
    return x0 <= local[0] <= x1 and y0 <= local[1] <= y1


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


def _drag_record(grip, point, session):
    """The press-time snapshot every drag reads from. None when the gesture
    cannot start - a rotation whose pointer sits on its own pivot has no angle
    to measure from."""
    box, angle, origin = _state(session)
    record = {"grip": grip, "origin": point,
              "start_box": list(box), "start_angle": angle,
              "start_origin": origin,
              "applied": IDENTITY, "changed": False}
    if grip != ROTATE:
        return record
    # Turn about the box centre, the drawing-app default - unless the box
    # already carries an angle, in which case turning about the SAME origin is
    # what keeps the two rotations composable as one angle.
    pivot = origin if angle else _box_centre(box)
    if math.hypot(point[0] - pivot[0], point[1] - pivot[1]) < MIN_SPAN:
        return None
    record["pivot"] = pivot
    record["start_pointer"] = math.atan2(point[1] - pivot[1],
                                         point[0] - pivot[0])
    return record


def _target_state(drag, position, constrain):  # noqa: C901 - one policy, top-down
    """Where the box goes for this cursor position - the whole policy.

    Returns the (box, angle, origin) triple; `constrain` is the live modifier
    verdict, read on every move and never latched at press.
    """
    start = drag["start_box"]
    angle = drag["start_angle"]
    origin = drag["start_origin"]
    grip = drag["grip"]
    x0, y0, x1, y1 = start

    if grip == ROTATE:
        pivot = drag["pivot"]
        pointer = math.atan2(position[1] - pivot[1], position[0] - pivot[0])
        turned = angle + (pointer - drag["start_pointer"])
        if constrain:
            step = math.radians(ROTATE_SNAP_DEG)
            turned = round(turned / step) * step
        # The rect itself never moves under a rotation; only the frame it is
        # read in does.
        return list(start), turned, pivot

    if grip == BODY:
        dx = position[0] - drag["origin"][0]
        dy = position[1] - drag["origin"][1]
        if constrain:
            # Commit to the axis the gesture leads with, exactly like the
            # pen's axis snap: the larger displacement wins, the other is
            # zeroed.
            if abs(dx) >= abs(dy):
                dy = 0.0
            else:
                dx = 0.0
        if angle:
            # The hand pushes along the SCREEN axes, so the offset is carried
            # into the box's frame before it moves the rect.
            dx, dy = _rotate_point((dx, dy), (0.0, 0.0), -angle)
        return [x0 + dx, y0 + dy, x1 + dx, y1 + dy], angle, origin

    anchor_x, anchor_y, scales_x, scales_y = GRIPS[grip]
    ax = x0 + anchor_x * (x1 - x0)
    ay = y0 + anchor_y * (y1 - y0)
    # Everything below is axis-aligned maths in the box's OWN frame: on a
    # turned box the cursor comes in through the same rotation the grips went
    # out through, so the grip under the hand is the grip that moves.
    local = _to_local((start, angle, origin), position)

    if constrain and scales_x != scales_y:
        # Edge grip with a modifier: mirror the stretch about the centre, so
        # the box grows from both sides at once.
        cx = 0.5 * (x0 + x1)
        cy = 0.5 * (y0 + y1)
        if scales_x:
            half = max(abs(local[0] - cx), MIN_SPAN)
            return [cx - half, y0, cx + half, y1], angle, origin
        half = max(abs(local[1] - cy), MIN_SPAN)
        return [x0, cy - half, x1, cy + half], angle, origin

    scale_x = 1.0
    scale_y = 1.0
    if scales_x:
        moving = x0 if anchor_x > 0.5 else x1
        base = moving - ax
        if abs(base) > MIN_SPAN:
            scale_x = (local[0] - ax) / base
    if scales_y:
        moving = y0 if anchor_y > 0.5 else y1
        base = moving - ay
        if abs(base) > MIN_SPAN:
            scale_y = (local[1] - ay) / base

    if scales_x and scales_y and constrain:
        # Aspect lock on a corner: one magnitude for both axes, each keeping
        # its own sign so a flip through the anchor still flips. The SMALLER
        # magnitude wins, so the locked box fits INSIDE the rectangle the
        # cursor drew; taking the larger let the long side shoot past the
        # pointer, which is not what "keep ratio" does in any drawing app.
        magnitude = min(abs(scale_x), abs(scale_y))
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
    return [nx0, ny0, nx1, ny1], angle, origin


def _apply(view, session, target):
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
    total = _affine((drag["start_box"], drag["start_angle"],
                     drag["start_origin"]), target)
    if total is None:
        return
    delta = _delta_matrix(total, drag["applied"])
    if delta is None:
        return
    image.transform(*delta)
    drag["applied"] = total
    drag["changed"] = True
    session["box"] = list(target[0])
    session["angle"] = target[1]
    session["origin"] = target[2]


# --- events ------------------------------------------------------------------

def _handle_event(message):  # noqa: C901 - one dispatch, read top-down
    if message.get("base_tool") != "transfer":
        return
    view = message.get("view") or "main"
    phase = message.get("phase")
    position = message.get("position") or {}
    point = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
    modifiers = message.get("modifiers") or {}
    zoom = message.get("zoom", 1.0)

    if phase == "arm":
        if _refresh(view) is None:
            print("[transfer] this layer is empty - draw something to transform.")
        return
    if phase == "cancel":
        _clear(view)
        return

    session = _SESSIONS.get(view)
    if session is None or not session.get("box"):
        if phase == "hover":
            _set_cursor(view, "")
        elif phase in ("pick", "press"):
            _refresh(view)
        return

    if phase == "hover":
        _set_cursor(view, _cursor_name(_state(session), point, zoom,
                                       bool(session.get("rotatable"))))
        return

    if phase == "pick":
        state = _state(session)
        if _inside(state, point):
            # Claim the gesture: C++ turns this press into a drag under the
            # id we answer with, so the moves arrive as "move" events.
            session["drag"] = _drag_record(BODY, point, session)
            message["grab"] = f"tf:{BODY}"
            return
        corner = (_corner_under(state, point, zoom)
                  if session.get("rotatable") else None)
        if corner is not None:
            drag = _drag_record(ROTATE, point, session)
            if drag is not None:
                session["drag"] = drag
                message["grab"] = f"tf:{ROTATE}:{corner}"
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
        session["drag"] = _drag_record(grip, point, session)
        return

    drag = session.get("drag")
    if drag is None:
        return
    constrain = _constrained(modifiers)

    if phase == "move":
        _apply(view, session, _target_state(drag, point, constrain))
        _push(view, _state(session))
        _animean().ui.refresh()
        return

    if phase == "release":
        _apply(view, session, _target_state(drag, point, constrain))
        changed = bool(drag.get("changed"))
        session["drag"] = None
        # Re-frame from the artwork itself: stroke widths grew with the
        # scale, so the true bounds are a hair wider than the dragged box -
        # and a committed rotation lives in the geometry now, so the box
        # comes back upright around whatever shape that left.
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
