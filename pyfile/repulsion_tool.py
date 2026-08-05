"""Repulsion pad: push overlapping strokes apart with a latching 2D handle.

The "Repulsion Pad" dock (C++ ForcePadPanel, a generic crosshair + circular
handle vector input) reports drag phases through the generic "pad" hook event
(pad name "force_pad"). Everything the pad MEANS lives here.

INTERACTION MODEL - the handle is an ABSOLUTE control over a cached baseline:

1. A BASELINE is cached from the active view's current frame the first time
   it is needed: every visible stroke's original geometry (its real path
   commands + raw points - the topology) plus a 4px flattening, and each
   flattened vertex's repulsion vector summed from ALL spatially-near
   vertices - other strokes and the same stroke's own fold-backs alike
   (UMAP-style); only neighbours ALONG the same polyline are excluded, since
   they are near because the line connects them (repelling those would
   inflate every stroke). Exactly coincident vertices - the "inseparable"
   case the tool exists for - have no geometric direction, so they fall back
   to a drawing-direction-independent canonical normal with a deterministic
   sign, giving the two sides opposite pushes. Forces are smoothed along
   each stroke and normalized so the strongest vertex has magnitude 1 (the
   normalized per-vertex repulsion map of the request).
2. Dragging previews displacement = handle * MAX_PUSH * force per axis,
   ALWAYS measured from the baseline originals. Releasing applies it and the
   handle LATCHES - it keeps showing the direction/strength in effect.
   Dragging further adjusts from the same baseline, so pulling the handle
   back to the center restores the original geometry exactly.
3. Applying preserves TOPOLOGY: the stroke's own command points (anchors and
   bezier handles) and raw points are displaced 1:1 - no resampling, no
   knot changes, no subpath splitting - and the stroke is swapped in place
   (z-order, id, property, color, width all survive). One "Repulsion"
   history entry per release.
4. The baseline dies when the drawing changes under it (drawing, erasing,
   moving, filling, undo/redo on that view): the pad recenters via
   ui.set_pad_value and the next press re-caches from the current state.

Architecture note: C++ provides only the generic pad widget, the "pad" hook
event, ui.set_pad_value and the replace_stroke_with_pieces binding. All
force semantics are in this file.
"""

import math
import time
import traceback

import python_hooks

RADIUS = 30.0        # px: neighbourhood radius for vertex repulsion
MAX_PUSH = 60.0      # px: displacement at full handle deflection
COINCIDENT_D = 2.0   # px: below this, treat vertices as coincident (no direction)
SMOOTH_WINDOW = 4    # vertices to each side in the per-stroke force smoothing
PREVIEW_INTERVAL = 0.03   # seconds between overlay preview pushes
POLY_STEP = 4.0
# ALL vertices repel each other (user: 所有顶点之间互相推远), including a
# stroke's own fold-backs - EXCEPT neighbours along the same polyline. Without
# the along-the-line exclusion every vertex sits 4px from its chain neighbours
# and each stroke would inflate itself instead of untangling from what crosses
# it. Arc length >= chord length, so anything closer than RADIUS in space but
# farther than ARC_EXCLUDE along the line is a genuine fold and DOES repel.
ARC_EXCLUDE = 1.5 * RADIUS

PREVIEW_ALPHA = 200

# Persistent baseline (see module docstring). "applied" is the handle vector
# currently written into the model; "gesture" tracks the live drag.
_BASELINE = {"valid": False, "view": None, "frame": -1, "strokes": [],
             "fingerprints": {}, "applied": (0.0, 0.0)}
_GESTURE = {"active": False, "moved": False, "last_preview": 0.0}


def _animean():
    import animean_python
    return animean_python


def _scene_model(view_name):
    """Resolve a view's SceneModel the same way auto_mapping does.

    get_scene() returns INFO DICTS ({"sceneName": ..., "scene": model}), not
    model objects - calling scene_name() on them was the bug behind the
    original "pad does nothing" report (AttributeError at press, session
    never opened). The __main__ globals are the fast path kept in sync by
    syncEmbeddedPythonState.
    """
    import __main__

    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model

    wanted = f"{view_name}_paint_view"
    for info in _animean().get_scene():
        if info.get("sceneName") == wanted:
            return info["scene"]
    raise RuntimeError(f"scene for view '{view_name}' is not registered")


# ---------------------------------------------------------------------------
# baseline: original geometry + per-vertex normalized repulsion field
# ---------------------------------------------------------------------------

def _collect_strokes(scene, frame):
    """Visible strokes with BOTH their real topology and a 4px flattening.

    Keyed by IMAGE identity - the (asset_index, frame_id) pair - not by
    layer: two layers can share one drawing, and collecting it twice would
    make every vertex coincident with its own twin and corrupt the index
    bookkeeping. Locked layers still repel (they are obstacles) but are
    never edited.
    """
    strokes = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return strokes
    sid = 0
    seen_images = set()
    for layer in structure["layers"]:
        if not layer["visible"] or layer["type"] == "fill":
            continue
        cmd_cell = scene.cell_to_dict(layer["index"], frame, False, POLY_STEP)
        image_key = (cmd_cell.get("asset_index", -1), cmd_cell.get("frame_id", 0))
        if image_key[0] < 0 or image_key in seen_images:
            continue
        seen_images.add(image_key)
        poly_cell = scene.cell_to_dict(layer["index"], frame, True, POLY_STEP)
        cmd_strokes = cmd_cell["image"]["strokes"]
        poly_strokes = poly_cell["image"]["strokes"]
        for index, (cmd_stroke, poly_stroke) in enumerate(zip(cmd_strokes, poly_strokes)):
            polylines = []
            for polyline in poly_stroke.get("polylines") or []:
                pts = [(float(p["x"]), float(p["y"])) for p in polyline]
                if len(pts) >= 2:
                    polylines.append(pts)
            if not polylines:
                continue
            raw_points = [(float(p["x"]), float(p["y"]))
                          for p in (cmd_stroke.get("raw_points") or [])]
            color = cmd_stroke.get("color") or {}
            strokes.append({
                "sid": sid,
                "asset": image_key[0],
                "frame_id": image_key[1],
                "index": index,
                "id": int(cmd_stroke.get("id", 0)),
                "editable": not layer.get("locked", False),
                "property": cmd_stroke.get("property") or "",
                "color": (int(color.get("r", 0)), int(color.get("g", 0)),
                          int(color.get("b", 0)), int(color.get("a", 255))),
                "width": float(cmd_stroke.get("width", 3.0)),
                "commands": cmd_stroke.get("commands") or [],
                "raw_points": raw_points,
                "polylines": polylines,
                "forces": None,
            })
            sid += 1
    return strokes


def _canonical_normals(points):
    """Per-vertex unit normals, INDEPENDENT of the drawing direction.

    A raw perpendicular (-dy, dx) flips with the polyline's direction, so the
    coincident fallback would push two overlapping strokes the SAME way when
    the artist drew one of them backwards - the exact pair the tool exists to
    separate (review-caught, verified numerically). Canonicalize the first
    normal into a fixed half-plane, then propagate sign-continuity along the
    stroke (dot(n_i, n_{i-1}) >= 0) so a curving stroke keeps a smooth field
    instead of kinking where its tangent crosses the axis.
    """
    normals = []
    prev = None
    for i in range(len(points)):
        a = points[max(0, i - 1)]
        b = points[min(len(points) - 1, i + 1)]
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length <= 1e-9:
            n = prev if prev is not None else (1.0, 0.0)
        else:
            n = (-dy / length, dx / length)
            if prev is None:
                if n[1] < 0.0 or (n[1] == 0.0 and n[0] < 0.0):
                    n = (-n[0], -n[1])
            elif n[0] * prev[0] + n[1] * prev[1] < 0.0:
                n = (-n[0], -n[1])
        normals.append(n)
        prev = n
    return normals


def _smooth_forces(forces, window=SMOOTH_WINDOW):
    """Moving average along the stroke: raw per-vertex sums are jittery."""
    if len(forces) <= 2 or window <= 0:
        return forces
    smoothed = []
    for i in range(len(forces)):
        lo = max(0, i - window)
        hi = min(len(forces), i + window + 1)
        sx = sum(f[0] for f in forces[lo:hi])
        sy = sum(f[1] for f in forces[lo:hi])
        count = hi - lo
        smoothed.append((sx / count, sy / count))
    return smoothed


def _build_forces(strokes):
    """UMAP-style repulsion between ALL vertex pairs, smoothed, normalized.

    Every vertex repels every spatially-near vertex - other strokes AND the
    same stroke's own fold-backs - EXCEPT pairs that are neighbours ALONG the
    same polyline (arc distance < ARC_EXCLUDE): those are near because the
    line connects them, not because anything overlaps, and repelling them
    would just inflate each stroke.
    """
    cell_size = RADIUS
    grid = {}
    for stroke in strokes:
        for pl_index, points in enumerate(stroke["polylines"]):
            arc = 0.0
            prev = None
            for x, y in points:
                if prev is not None:
                    arc += math.hypot(x - prev[0], y - prev[1])
                prev = (x, y)
                key = (int(x // cell_size), int(y // cell_size))
                grid.setdefault(key, []).append((x, y, stroke["sid"], pl_index, arc))

    max_norm = 0.0
    for stroke in strokes:
        sid = stroke["sid"]
        stroke_forces = []
        for pl_index, points in enumerate(stroke["polylines"]):
            arcs = []
            arc = 0.0
            prev = None
            for x, y in points:
                if prev is not None:
                    arc += math.hypot(x - prev[0], y - prev[1])
                prev = (x, y)
                arcs.append(arc)

            forces = []
            normals = None
            for i, (x, y) in enumerate(points):
                fx = fy = 0.0
                nearest_coincident = None
                cx, cy = int(x // cell_size), int(y // cell_size)
                for gx in (cx - 1, cx, cx + 1):
                    for gy in (cy - 1, cy, cy + 1):
                        for ox, oy, osid, opl, oarc in grid.get((gx, gy), ()):
                            if (osid == sid and opl == pl_index
                                    and abs(oarc - arcs[i]) < ARC_EXCLUDE):
                                continue  # chain neighbour, not an overlap
                            dx, dy = x - ox, y - oy
                            d = math.hypot(dx, dy)
                            if d >= RADIUS:
                                continue
                            if d < COINCIDENT_D:
                                if nearest_coincident is None or d < nearest_coincident[0]:
                                    nearest_coincident = (d, osid, oarc)
                                continue
                            w = (1.0 - d / RADIUS) ** 2
                            fx += w * dx / d
                            fy += w * dy / d
                if nearest_coincident is not None and math.hypot(fx, fy) < 0.25:
                    # Exactly overlapping vertices: no geometric direction, so
                    # push along the CANONICAL normal (drawing-direction
                    # independent) with a deterministic sign - stroke order
                    # for different strokes, arc order for a stroke retracing
                    # itself - so the two sides always separate.
                    if normals is None:
                        normals = _canonical_normals(points)
                    nx, ny = normals[i]
                    _d, osid, oarc = nearest_coincident
                    if osid != sid:
                        sign = 1.0 if sid > osid else -1.0
                    else:
                        sign = 1.0 if arcs[i] > oarc else -1.0
                    fx += sign * nx
                    fy += sign * ny
                forces.append((fx, fy))
            forces = _smooth_forces(forces)
            for fx, fy in forces:
                max_norm = max(max_norm, math.hypot(fx, fy))
            stroke_forces.append(forces)
        stroke["forces"] = stroke_forces

    if max_norm > 1e-9:
        for stroke in strokes:
            stroke["forces"] = [[(fx / max_norm, fy / max_norm) for fx, fy in forces]
                                for forces in stroke["forces"]]
    return max_norm > 1e-9


def _image_fingerprint(image):
    """(stroke_count, ids) - cheap identity check for baseline validity."""
    strokes = image.to_dict(False, POLY_STEP)["strokes"]
    return (len(strokes), tuple(int(s.get("id", 0)) for s in strokes))


def _reset_pad():
    try:
        _animean().ui.set_pad_value("force_pad", 0.0, 0.0)
    except Exception:
        pass  # older builds without the binding


def _invalidate_baseline(reason=None):
    had_state = _BASELINE["valid"] or _BASELINE["applied"] != (0.0, 0.0)
    view = _BASELINE["view"]
    _BASELINE.update({"valid": False, "strokes": [], "fingerprints": {},
                      "applied": (0.0, 0.0)})
    if _GESTURE["active"]:
        _GESTURE.update({"active": False, "moved": False})
        if view:
            _restore_overlay(view)
    if had_state:
        _reset_pad()
        if reason:
            print(f"[repulsion] baseline reset ({reason}); the pad recentered.")


def _ensure_baseline(view):
    """Reuse the cached baseline when it still matches the model, else rebuild."""
    scene = _scene_model(view)
    frame = max(scene.current_frame(), 0)
    if _BASELINE["valid"] and _BASELINE["view"] == view and _BASELINE["frame"] == frame:
        intact = True
        for key, fingerprint in _BASELINE["fingerprints"].items():
            image = scene.asset_image(key[0], key[1], False)
            if image is None or _image_fingerprint(image) != fingerprint:
                intact = False
                break
        if intact:
            return True
        _invalidate_baseline("drawing changed")

    strokes = _collect_strokes(scene, frame)
    has_field = _build_forces(strokes) if strokes else False
    fingerprints = {}
    for s in strokes:
        key = (s["asset"], s["frame_id"])
        if key not in fingerprints:
            image = scene.asset_image(s["asset"], s["frame_id"], False)
            if image is not None:
                fingerprints[key] = _image_fingerprint(image)
    _BASELINE.update({"valid": True, "view": view, "frame": frame,
                      "strokes": strokes, "fingerprints": fingerprints,
                      "applied": (0.0, 0.0)})
    total = sum(len(pl) for s in strokes for pl in s["polylines"])
    if not has_field:
        print("[repulsion] no repulsion anywhere: strokes never come within "
              f"{RADIUS:.0f}px of another stroke (view: {view}_paint_view, "
              f"{len(strokes)} stroke(s)).")
    else:
        print(f"[repulsion] field cached: {len(strokes)} stroke(s), {total} vertices "
              f"(view: {view}_paint_view). Drag to push; the handle holds the "
              "applied force, center = original.")
    return has_field


# ---------------------------------------------------------------------------
# displacement (always absolute, measured from the baseline originals)
# ---------------------------------------------------------------------------

def _displaced_polylines(stroke, scale_x, scale_y):
    result = []
    moved = False
    for points, forces in zip(stroke["polylines"], stroke["forces"]):
        displaced = []
        for (x, y), (fx, fy) in zip(points, forces):
            dx = scale_x * MAX_PUSH * fx
            dy = scale_y * MAX_PUSH * fy
            if not moved and (abs(dx) > 0.05 or abs(dy) > 0.05):
                moved = True
            displaced.append((x + dx, y + dy))
        result.append(displaced)
    return result, moved


def _force_lookup(stroke):
    """Nearest-vertex force sampler over the stroke's own flattened field."""
    samples = []
    for points, forces in zip(stroke["polylines"], stroke["forces"]):
        samples.extend(zip(points, forces))

    def lookup(x, y):
        best = None
        best_d = None
        for (vx, vy), force in samples:
            d = (vx - x) * (vx - x) + (vy - y) * (vy - y)
            if best_d is None or d < best_d:
                best_d = d
                best = force
        return best or (0.0, 0.0)

    return lookup


def _displace_commands(stroke, scale_x, scale_y):
    """Displace the stroke's OWN command points 1:1 - topology untouched.

    Anchors, bezier handles and raw points all move through the same field
    (sampled at each point's original position); no resampling, no knot
    insertion, no subpath splitting. Returns (commands, raw_points).
    """
    lookup = _force_lookup(stroke)

    def moved_point(p):
        x, y = float(p["x"]), float(p["y"])
        fx, fy = lookup(x, y)
        return {"x": x + scale_x * MAX_PUSH * fx, "y": y + scale_y * MAX_PUSH * fy}

    commands = []
    for command in stroke["commands"]:
        kind = command.get("type")
        out = {"type": kind}
        for key in ("from", "to", "control", "control1", "control2"):
            if key in command:
                out[key] = moved_point(command[key])
        if kind == "rect" and "rect" in command:
            out["rect"] = command["rect"]
        commands.append(out)

    raw = []
    for x, y in stroke["raw_points"]:
        fx, fy = lookup(x, y)
        raw.append((x + scale_x * MAX_PUSH * fx, y + scale_y * MAX_PUSH * fy))
    return commands, raw


# ---------------------------------------------------------------------------
# preview + apply
# ---------------------------------------------------------------------------

def _push_preview(view, scale_x, scale_y):
    import auto_mapping
    items = auto_mapping.overlay_items(view)
    for stroke in _BASELINE["strokes"]:
        displaced, _ = _displaced_polylines(stroke, scale_x, scale_y)
        r, g, b, _a = stroke["color"]
        for points in displaced:
            items.append({
                "id": "repulsion_preview",
                "points": points,
                "color": (r, g, b, PREVIEW_ALPHA),
                "width": stroke["width"],
                "removable": False,
            })
    _animean().ui.set_overlay(view, items)


def _restore_overlay(view):
    import auto_mapping
    auto_mapping._push_overlay(view)


def _polyline_length(points):
    return sum(math.hypot(points[i + 1][0] - points[i][0],
                          points[i + 1][1] - points[i][1])
               for i in range(len(points) - 1))


def _apply(view, scale_x, scale_y):
    animean = _animean()
    scene = _scene_model(view)

    # 1:1 replacement keeps stroke counts and ids stable, so indices stay
    # valid across repeated applies from the same baseline. Descending order
    # per image is kept as belt-and-braces against a skipped/degenerate
    # replacement shifting higher indices.
    ordered = sorted(_BASELINE["strokes"],
                     key=lambda s: (s["asset"], s["frame_id"], -s["index"]))

    replaced = 0
    skipped_stale = 0
    checked_images = {}
    for stroke in ordered:
        if not stroke["editable"] or not stroke["commands"]:
            continue
        image_key = (stroke["asset"], stroke["frame_id"])
        if image_key not in checked_images:
            image = scene.asset_image(stroke["asset"], stroke["frame_id"], False)
            fingerprint = _BASELINE["fingerprints"].get(image_key)
            if image is None or fingerprint is None or _image_fingerprint(image) != fingerprint:
                checked_images[image_key] = None
            else:
                checked_images[image_key] = image
        image = checked_images[image_key]
        if image is None:
            skipped_stale += 1
            continue
        commands, raw = _displace_commands(stroke, scale_x, scale_y)
        points = raw if len(raw) >= 2 else None
        if points is None:
            # imported strokes can carry sparse raw points; fall back to the
            # displaced flattening for the hit-test geometry
            flat, _ = _displaced_polylines(stroke, scale_x, scale_y)
            points = [p for piece in flat for p in piece]
        if len(points) < 2 or _polyline_length(points) <= 0.001:
            continue
        piece = animean.vectorlogic.make_stroke_object_from_path(
            commands, points, stroke["color"], stroke["width"], stroke["id"])
        piece.property = stroke["property"]
        if image.replace_stroke_with_pieces(stroke["index"], [piece]) > 0:
            replaced += 1

    if replaced:
        # The model now holds the displaced geometry; the baseline stays the
        # reference so the next drag (or centering the handle) is absolute.
        _BASELINE["applied"] = (scale_x, scale_y)
        fingerprints = {}
        for key, image in checked_images.items():
            if image is not None:
                fingerprints[key] = _image_fingerprint(image)
        _BASELINE["fingerprints"].update(fingerprints)
        animean.ui.refresh()
        try:
            animean.ui.history_commit("Repulsion", view)
        except Exception:
            pass  # older builds without the history binding
        summary = (f"[repulsion] holding {replaced} stroke(s) at "
                   f"x={scale_x:+.2f} y={scale_y:+.2f} (max {MAX_PUSH:.0f}px; "
                   "center the handle to restore)")
        if skipped_stale:
            summary += f"; {skipped_stale} skipped (image changed)"
        print(summary)
    elif skipped_stale:
        print("[repulsion] nothing applied: the drawing changed during the drag; "
              "press again to re-cache.")
    else:
        print("[repulsion] handle at center: strokes back at their original "
              "positions." if _BASELINE["applied"] == (0.0, 0.0)
              else "[repulsion] nothing moved (empty field).")


# ---------------------------------------------------------------------------
# pad hook
# ---------------------------------------------------------------------------

def _pad_event(cell, stroke, message):
    if message.get("pad") != "force_pad":
        return
    phase = message.get("phase")
    view = message.get("view") or "main"
    value = message.get("value") or {}
    x = float(value.get("x", 0.0))
    y = float(value.get("y", 0.0))

    if phase == "press":
        if _BASELINE["valid"] and _BASELINE["view"] != view:
            # The pad follows the active view; a leftover baseline for the
            # other view no longer matches what the handle claims.
            _invalidate_baseline("active view changed")
        try:
            _ensure_baseline(view)
        except Exception:
            print(f"[repulsion] baseline failed:\n{traceback.format_exc()}")
            return
        _GESTURE.update({"active": True, "moved": False, "last_preview": 0.0})
        return

    if phase == "release":
        if not _GESTURE["active"]:
            return
        _GESTURE["active"] = False
        session_view = _BASELINE["view"] or view
        try:
            if _GESTURE["moved"]:
                _apply(session_view, x, y)
            else:
                # A bare click never showed a preview; keep the pad honest by
                # snapping it back to the force actually in effect.
                applied = _BASELINE["applied"]
                try:
                    _animean().ui.set_pad_value("force_pad", applied[0], applied[1])
                except Exception:
                    pass
                print("[repulsion] click ignored - drag the handle to push.")
        except Exception:
            print(f"[repulsion] apply failed:\n{traceback.format_exc()}")
        finally:
            _GESTURE["moved"] = False
            _restore_overlay(session_view)
        return

    if not _GESTURE["active"] or not _BASELINE["valid"]:
        return

    if phase == "move":
        _GESTURE["moved"] = True
        now = time.monotonic()
        if now - _GESTURE["last_preview"] >= PREVIEW_INTERVAL:
            _GESTURE["last_preview"] = now
            try:
                _push_preview(_BASELINE["view"], x, y)
            except Exception as error:
                print(f"[repulsion] preview failed: {error}")


def _history_restored(cell, stroke, message):
    """Undo/redo replaces the whole model: the baseline no longer matches."""
    if _BASELINE["valid"] and (message.get("view") or "main") == _BASELINE["view"]:
        _invalidate_baseline("undo/redo")


def _canvas_changed(cell, stroke, message):
    """Any finished drawing gesture on the baseline's view invalidates it."""
    if _BASELINE["valid"] and (message.get("view") or "main") == _BASELINE["view"]:
        _invalidate_baseline("canvas edited")


def register_hooks():
    python_hooks.set_hook(_pad_event, pad=True)
    python_hooks.set_hook(_history_restored, historyrestore=True)
    python_hooks.set_hook(_canvas_changed, linefinish=True, erasefinish=True,
                          deletefinish=True, fillfinish=True, movefinish=True)


# The pad dock exists from startup, so the hooks must too (modules are
# imported by initalize.import_all_modules at app launch).
register_hooks()
