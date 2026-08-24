"""Paper-facing interaction and data pipeline for Fukusato mapping.

One guide is authored on the garment (MainView), captured as an overlay and
attached to the triangulated garment.  It can be translated directly on the
canvas.  The check/x pair in its upper-right corner is the transaction
boundary: until check is clicked, no artwork changes and the optional weight
heatmap shows the pending handle's support in both panel and UV space.
"""

from __future__ import annotations

import json
import math

import auto_mapping
import crease_line_tool
import fukusato_mapping as core
from fukusato_mesh import (GarmentMesh, point_in_region, point_in_ring,
                           signed_area)
import overlay_stack
import python_hooks
import script_store

HANDLE_PROPERTY = "fukusato_line"
TOOL_NAME = "fukusato_line"
RUN_TOOL = "fukusato_guide_mapping"
STORE_KEY = "fukusato_mapping"
MENU_NAME = "fukusato_mapping"
POLY_STEP = 2.0
GUIDE_COLOR = (230, 60, 190, 255)
PENDING_COLOR = (255, 70, 180, 255)
_OWNER = "fukusato_mapping"
_VIEW = {"weight_preview": False, "topology": False}
_CACHE = {"key": None, "mesh": None}
_DRAG = {"id": None, "origin": None, "points": None, "moved": False,
         "was_accepted": False}

_EXCLUDED = {
    HANDLE_PROPERTY, crease_line_tool.PROPERTY,
    core.MAPPED_PROPERTY, core.BACK_PROPERTY, core.FUKUSATO_TOOL,
    "auto_mapping", "auto_mapping_2", "auto_mapped", "auto_mapped_back",
    "auto_mapped_seal", "auto_mapped_guide", "auto_mapped_guide_h",
    "auto_mapped_guide_v", "h_center_line", "v_center_line", "mapping_area",
}


def _animean():
    import animean_python
    return animean_python


def _scene_model(view):
    return core._scene_model(view)


def _default_state():
    return {"next_id": 1, "guides": [], "solutions": {}}


def _state(scene=None):
    scene = scene or _scene_model("main")
    value = script_store.read(scene, STORE_KEY, {})
    if not isinstance(value, dict):
        value = {}
    state = _default_state()
    state.update(value)
    if not isinstance(state.get("guides"), list):
        state["guides"] = []
    if not isinstance(state.get("solutions"), dict):
        state["solutions"] = {}
    state["next_id"] = max(1, int(state.get("next_id", 1)))
    return state


def _save(scene, state):
    script_store.write(scene, STORE_KEY, state)


def _frame_guides(state, frame):
    return [guide for guide in state["guides"]
            if int(guide.get("frame", 0)) == int(frame)]


def _pending(state, frame):
    return next((guide for guide in _frame_guides(state, frame)
                 if not bool(guide.get("accepted", False))), None)


def _guide_id(guide):
    return f"fukusato-guide:{int(guide['id'])}"


def _stroke_points(stroke):
    result = []
    for polyline in stroke.get("polylines") or []:
        points = [(float(p["x"]), float(p["y"])) for p in polyline]
        if len(points) >= 2:
            result.extend(points if not result else points[1:])
    if not result:
        result = [(float(p["x"]), float(p["y"]))
                  for p in stroke.get("raw_points") or []]
    return result


def _guide_seed(guide):
    points = guide.get("before") or guide.get("after") or []
    return tuple(points[len(points) // 2])


def _region_for(seed, frame):
    scene = _scene_model("main")
    polygons = auto_mapping._detect_region(scene, "main", frame, seed)
    rings = [[tuple(map(float, point)) for point in ring]
             for ring in polygons if len(ring) >= 3]
    containing = [ring for ring in rings if point_in_ring(seed, ring)]
    if not containing:
        raise RuntimeError("the guide is not inside a closed garment region")
    outer = max(containing, key=lambda ring: abs(signed_area(ring)))
    holes = []
    for ring in rings:
        if ring is outer:
            continue
        probe = ring[0]
        if point_in_ring(probe, outer) and not point_in_ring(seed, ring):
            holes.append(ring)
    return outer, holes


def _same_garment_region(first_seed, second_seed, frame):
    """Whether two seeds resolve to the same bucket-detected garment face."""
    first_outer, first_holes = _region_for(first_seed, frame)
    second_outer, second_holes = _region_for(second_seed, frame)
    # Mutual containment rejects adjacent faces and nested-but-distinct faces.
    return (point_in_region(second_seed, first_outer, first_holes)
            and point_in_region(first_seed, second_outer, second_holes))


def _mesh_for(state, frame, guide=None, force=False):
    guides = _frame_guides(state, frame)
    guide = guide or (guides[-1] if guides else None)
    if guide is None:
        raise RuntimeError("draw a Fukusato guide inside the garment first")
    outer, holes = _region_for(_guide_seed(guide), frame)
    creases = crease_line_tool.get_creases(_scene_model("main"), frame)
    payload = {
        "frame": frame,
        "outer": outer,
        "holes": holes,
        "creases": creases,
        "grid": int(core._OPTIONS["grid"]),
    }
    key = json.dumps(payload, sort_keys=True, separators=(",", ":"))
    if not force and _CACHE["key"] == key and _CACHE["mesh"] is not None:
        return _CACHE["mesh"]
    xs = [p[0] for p in outer]
    ys = [p[1] for p in outer]
    grid = max(8, int(core._OPTIONS["grid"]))
    max_area = max((max(xs) - min(xs)) * (max(ys) - min(ys))
                   / (2.0 * grid * grid), 0.25)
    mesh = GarmentMesh.triangulate(outer, holes, creases, max_area=max_area)
    _CACHE.update(key=key, mesh=mesh)
    return mesh


def invalidate_mesh():
    _CACHE.update(key=None, mesh=None)


def _creases_changed():
    invalidate_mesh()
    refresh_overlays()


def _solution_uv(state, frame, mesh):
    solution = state["solutions"].get(str(frame)) or {}
    stored_mesh = solution.get("mesh")
    uv = solution.get("uv")
    if stored_mesh == mesh.to_dict() and uv and len(uv) == len(mesh.P):
        return [tuple(map(float, point)) for point in uv]
    return list(mesh.base_uv)


def _sample_controls(mesh, guides):
    before_uv = []
    after_uv = []
    after_panel = []
    measures = []
    longest = max((max(core._length(g.get("before") or []),
                       core._length(g.get("after") or [])) for g in guides),
                  default=1.0)
    spacing = longest / max(1, int(core._OPTIONS["samples"]) - 1)
    for guide in guides:
        before = [tuple(map(float, p)) for p in guide.get("before") or []]
        after = [tuple(map(float, p)) for p in guide.get("after") or []]
        arc = max(core._length(before), core._length(after))
        count = 1 if arc < 1.0 else max(2, int(round(arc / spacing)) + 1)
        pp = core._resample(before, count)
        qq = core._resample(after, count)
        kept = []
        for p, q in zip(pp, qq):
            p_uv = mesh.uv_at(p, mesh.base_uv)
            q_uv = mesh.uv_at(q, mesh.base_uv)
            if p_uv is None or q_uv is None:
                continue
            before_uv.append(p_uv)
            after_uv.append(q_uv)
            after_panel.append(q)
            kept.append(q)
        if kept:
            measures.extend([1.0 / len(kept)] * len(kept))
    return before_uv, after_uv, after_panel, measures


def _solve_uv(mesh, guides):
    p_uv, q_uv, after_panel, measures = _sample_controls(mesh, guides)
    if not p_uv:
        raise RuntimeError("all guide samples are outside the garment")
    adj = mesh.edge_graph()
    weights = core.build_weights(mesh, adj, after_panel,
                                 float(core._OPTIONS["alpha"]),
                                 float(core._OPTIONS["beta"]), measures)
    src = [complex(*p) for p in q_uv]
    dst = [complex(*p) for p in p_uv]
    uv = []
    for vertex, query in enumerate(mesh.base_uv):
        value = core.mls_deform(src, dst, weights[vertex], complex(*query),
                                core._OPTIONS["variant"])
        uv.append((value.real, value.imag))
    return uv


def _collect_pattern(child, frame):
    return core._collect(child, frame, None, exclude=tuple(_EXCLUDED))


def _collect_pattern_fills(child, frame):
    return auto_mapping._collect_pattern_fills(child, frame)


def _fill_triangles(fill):
    """Triangulate one odd-even vector fill in texture/UV coordinates."""
    rings = []
    for source in auto_mapping._path_commands_to_polygons(fill.get("commands")):
        ring = [tuple(map(float, point)) for point in source]
        if len(ring) > 1 and math.dist(ring[0], ring[-1]) <= 1e-8:
            ring.pop()
        if len(ring) >= 3 and abs(signed_area(ring)) > 1e-8:
            rings.append(ring)
    if not rings:
        return []

    # A boundary sample cannot sit in one of the ring's own children. Taking
    # the median over spread vertices also makes a touching/tangent vertex
    # harmless. This is the same odd-even classification used by AutoMapping.
    levels = []
    for index, ring in enumerate(rings):
        count = min(5, len(ring))
        candidates = sorted(
            sum(1 for other_index, other in enumerate(rings)
                if other_index != index
                and point_in_ring(ring[(sample * len(ring)) // count], other))
            for sample in range(count))
        levels.append(candidates[len(candidates) // 2])

    components = [{"ring": ring, "level": levels[index], "holes": []}
                  for index, ring in enumerate(rings)
                  if levels[index] % 2 == 0]
    for index, hole in enumerate(rings):
        if levels[index] % 2 == 0:
            continue
        probe = ((hole[0][0] + hole[1][0]) * 0.5,
                 (hole[0][1] + hole[1][1]) * 0.5)
        parents = [entry for entry in components
                   if entry["level"] == levels[index] - 1
                   and point_in_ring(probe, entry["ring"])]
        if not parents:
            parents = [entry for entry in components
                       if entry["level"] < levels[index]
                       and point_in_ring(probe, entry["ring"])]
        if parents:
            max(parents, key=lambda entry: entry["level"])["holes"].append(hole)

    result = []
    for component in components:
        vertices, triangles = auto_mapping._triangulate_polygon(
            component["ring"], component["holes"])
        for triangle in triangles:
            points = [tuple(vertices[index]) for index in triangle]
            if abs(signed_area(points)) > 1e-10:
                result.append(points)
    return result


def _clip_polygon_to_uv_triangle(polygon, corners, area2):
    """Sutherland-Hodgman intersection with an oriented UV triangle."""
    result = list(polygon)
    sign = 1.0 if area2 > 0.0 else -1.0
    for edge_index in range(3):
        if not result:
            break
        a = corners[edge_index]
        b = corners[(edge_index + 1) % 3]

        def side(point):
            return sign * ((b[0] - a[0]) * (point[1] - a[1])
                           - (b[1] - a[1]) * (point[0] - a[0]))

        clipped = []
        previous = result[-1]
        previous_side = side(previous)
        for current in result:
            current_side = side(current)
            previous_inside = previous_side >= -1e-10
            current_inside = current_side >= -1e-10
            if previous_inside != current_inside:
                denominator = previous_side - current_side
                if abs(denominator) > 1e-15:
                    amount = previous_side / denominator
                    clipped.append((previous[0] + (current[0] - previous[0]) * amount,
                                    previous[1] + (current[1] - previous[1]) * amount))
            if current_inside:
                clipped.append(current)
            previous = current
            previous_side = current_side
        result = clipped
    cleaned = []
    for point in result:
        if not cleaned or math.dist(point, cleaned[-1]) > 1e-8:
            cleaned.append(point)
    if len(cleaned) > 1 and math.dist(cleaned[0], cleaned[-1]) <= 1e-8:
        cleaned.pop()
    return cleaned if len(cleaned) >= 3 and abs(signed_area(cleaned)) > 1e-9 else []


def _fill_commands(polygons):
    commands = []
    for polygon in polygons:
        commands.append({"type": "move",
                         "to": {"x": polygon[0][0], "y": polygon[0][1]}})
        for point in polygon[1:]:
            commands.append({"type": "line",
                             "to": {"x": point[0], "y": point[1]}})
        commands.append({"type": "line",
                         "to": {"x": polygon[0][0], "y": polygon[0][1]}})
    return commands


def _emit_fills(image, fills, index):
    """Emit vector fills through the same exact piecewise-affine UV map."""
    added_pieces = 0
    for fill in fills:
        color_data = fill.get("color") or {}
        color = (int(color_data.get("r", 0)), int(color_data.get("g", 0)),
                 int(color_data.get("b", 0)), int(color_data.get("a", 255)))
        front = []
        back = []
        for source_triangle in _fill_triangles(fill):
            xs = [point[0] for point in source_triangle]
            ys = [point[1] for point in source_triangle]
            for entry in index.candidates(min(xs), min(ys), max(xs), max(ys)):
                _bounds, _triangle, corners_uv, corners_panel, area2 = entry
                clipped = _clip_polygon_to_uv_triangle(
                    source_triangle, corners_uv, area2)
                if not clipped:
                    continue
                mapped = [core._affine_uv_to_panel(
                    point, corners_uv, corners_panel, area2) for point in clipped]
                (back if area2 < 0.0 else front).append(mapped)
                added_pieces += 1
        for polygons, property_value in ((front, core.MAPPED_PROPERTY),
                                         (back, core.BACK_PROPERTY)):
            if polygons:
                image.add_fill_region(_fill_commands(polygons), color,
                                      property_value, None, -1, True)
    return added_pieces


def _owned_output_layers(scene, frame):
    owned = []
    for layer in scene.get_structure()["layers"]:
        cell = scene.cell_to_dict(layer["index"], frame, False, POLY_STEP)
        strokes = cell["image"]["strokes"]
        fills = cell["image"].get("fills") or []
        allowed = (core.MAPPED_PROPERTY, core.BACK_PROPERTY)
        if ((strokes or fills)
                and all((stroke.get("property") or "") in allowed for stroke in strokes)
                and all((fill.get("property") or "") in allowed for fill in fills)):
            owned.append(layer["index"])
    return owned


def _shifted_old_layers(old_layers, new_layer):
    """Old layer indices after inserting `new_layer`, in safe delete order."""
    # _create_mapped_layer moves the new layer to zero when possible. If that
    # move fails, it returns the append index and existing layers do not shift.
    offset = 1 if new_layer == 0 else 0
    return sorted((index + offset for index in old_layers), reverse=True)


def _emit(mesh, uv, frame):
    animean = _animean()
    child = _scene_model("child")
    main = _scene_model("main")
    child_frame = max(child.current_frame(), 0)
    pattern = _collect_pattern(child, child_frame)
    fills = _collect_pattern_fills(child, child_frame)
    if not pattern and not fills:
        raise RuntimeError("ChildView has no texture strokes or fills to map")
    old_layers = _owned_output_layers(main, frame)
    layer = core._create_mapped_layer(main)
    if layer < 0:
        raise RuntimeError("could not create the Fukusato output layer")
    image = main.image_at(frame, layer, True)
    if image is None:
        core._discard_mapped_layer(main, layer)
        raise RuntimeError("the Fukusato output layer has no editable cell")

    neighbours = mesh.tri_neighbours()
    index = core._UvIndex(mesh, uv)
    panel_length = uv_length = 0.0
    for tri in mesh.tris:
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            panel_length += math.dist(mesh.P[a], mesh.P[b])
            uv_length += math.dist(uv[a], uv[b])
    width_scale = panel_length / uv_length if uv_length > 1e-12 else 1.0
    added = 0
    try:
        added += _emit_fills(image, fills, index)
        for entry in pattern:
            color, width, pen_style = core._stroke_style(entry["stroke"])
            for points, back in core.emit_pattern(mesh, uv, entry["points"],
                                                  neighbours, index=index):
                obj = animean.vectorlogic.make_stroke_object(
                    points, color, max(0.5, width * width_scale),
                    image.stroke_count() + 1, False, False)
                obj.property = core.BACK_PROPERTY if back else core.MAPPED_PROPERTY
                obj.pen_style = pen_style
                image.add_stroke_object(obj)
                added += 1
    except Exception:
        core._discard_mapped_layer(main, layer)
        raise
    if not added:
        core._discard_mapped_layer(main, layer)
        raise RuntimeError("no texture content lies under the garment UV footprint")

    for old in _shifted_old_layers(old_layers, layer):
        core._discard_mapped_layer(main, old)
    return added, width_scale


def _weight_values(mesh, guide):
    _p, _q, samples, measures = _sample_controls(mesh, [guide])
    if not samples:
        return None
    rows = core.build_weights(mesh, mesh.edge_graph(), samples,
                              float(core._OPTIONS["alpha"]),
                              float(core._OPTIONS["beta"]), measures)
    values = [sum(row) for row in rows]
    finite = sorted(math.log10(max(v, 1e-30)) for v in values if math.isfinite(v) and v > 0)
    if not finite:
        return None
    lo = finite[max(0, int(0.05 * (len(finite) - 1)))]
    hi = finite[min(len(finite) - 1, int(0.95 * (len(finite) - 1)))]
    span = max(hi - lo, 1e-9)
    return [min(1.0, max(0.0, (math.log10(max(v, 1e-30)) - lo) / span))
            for v in values]


def _heat_color(value):
    # Compact blue -> cyan -> yellow -> red ramp, with enough transparency to
    # keep the line drawing readable below it.
    stops = ((0.0, (30, 55, 190)), (0.35, (20, 190, 230)),
             (0.68, (250, 225, 55)), (1.0, (235, 45, 35)))
    for (a, ca), (b, cb) in zip(stops, stops[1:]):
        if value <= b:
            t = (value - a) / max(b - a, 1e-9)
            return tuple(int(ca[i] + (cb[i] - ca[i]) * t) for i in range(3)) + (92,)
    return stops[-1][1] + (92,)


def _heat_items(mesh, coordinates, values, prefix):
    if values is None:
        return []
    result = []
    for index, tri in enumerate(mesh.tris):
        value = sum(values[v] for v in tri) / 3.0
        color = _heat_color(value)
        result.append({
            "id": f"{prefix}:heat:{index}",
            "points": [coordinates[v] for v in tri],
            "closed": True,
            "color": color[:3] + (38,),
            "fill_color": color,
            "width": 0.4,
            "removable": False,
        })
    return result


def _topology_items(mesh, coordinates, prefix):
    result = []
    seen = set()
    for tri in mesh.tris:
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            edge = tuple(sorted((a, b)))
            if edge in seen:
                continue
            seen.add(edge)
            result.append({
                "id": f"{prefix}:edge:{edge[0]}:{edge[1]}",
                "points": [coordinates[a], coordinates[b]],
                "color": (35, 35, 35, 145),
                "width": 0.75,
                "removable": False,
            })
    return result


def _guide_items(state, frame):
    def displayed(points):
        if len(points) != 1:
            return points, False
        x, y = points[0]
        radius = 4.0
        return ([[x + radius * math.cos(2.0 * math.pi * k / 16.0),
                  y + radius * math.sin(2.0 * math.pi * k / 16.0)]
                 for k in range(16)], True)

    result = []
    for guide in _frame_guides(state, frame):
        pending = not bool(guide.get("accepted", False))
        shown, closed = displayed(guide.get("after") or [])
        result.append({
            "id": _guide_id(guide),
            "points": shown,
            "closed": closed,
            "color": PENDING_COLOR if pending else GUIDE_COLOR,
            "width": float(guide.get("width", 3.0)),
            "removable": True,
            "confirmable": pending,
            "draggable": True,
        })
        before = guide.get("before") or []
        after = guide.get("after") or []
        if pending and before != after:
            before_shown, before_closed = displayed(before)
            result.append({
                "id": f"{_guide_id(guide)}:before",
                "points": before_shown,
                "closed": before_closed,
                "color": (100, 100, 100, 150),
                "width": 1.2,
                "pen_style": 2,
                "removable": False,
            })
    return result


def refresh_overlays(state_override=None):
    try:
        main = _scene_model("main")
        frame = max(main.current_frame(), 0)
        state = state_override or _state(main)
        guides = _frame_guides(state, frame)
        main_items = []
        child_items = []
        mesh = None
        if guides and (_VIEW["weight_preview"] or _VIEW["topology"]):
            try:
                mesh = _mesh_for(state, frame, guides[-1])
            except Exception as error:
                print(f"[fukusato] preview unavailable: {error}")
        if mesh is not None:
            uv = _solution_uv(state, frame, mesh)
            if _VIEW["weight_preview"]:
                pending = _pending(state, frame)
                if pending is not None:
                    values = _weight_values(mesh, pending)
                    main_items.extend(_heat_items(mesh, mesh.P, values, "fk-main"))
                    child_items.extend(_heat_items(mesh, uv, values, "fk-child"))
            if _VIEW["topology"]:
                main_items.extend(_topology_items(mesh, mesh.P, "fk-main"))
                child_items.extend(_topology_items(mesh, uv, "fk-child"))
        main_items.extend(_guide_items(state, frame))
        overlay_stack.set_items("main", _OWNER, main_items)
        overlay_stack.set_items("child", _OWNER, child_items)
    except Exception as error:
        print(f"[fukusato] overlay update failed: {error}")


def _capture_guide(cell, stroke, message):
    if message.get("view") != "main" or message.get("property") != HANDLE_PROPERTY:
        if message.get("property") == HANDLE_PROPERTY:
            row, layer, index = cell.get("row"), cell.get("layer"), stroke.get("index")
            if all(value is not None and int(value) >= 0 for value in (row, layer, index)):
                try:
                    _scene_model(message.get("view") or "child").remove_stroke(row, layer, index)
                    _animean().ui.widget.refresh()
                except Exception:
                    pass
            print("[fukusato] guides belong on MainView (the modeling panel).")
            message["cancel_history"] = True
        return
    row, layer, index = cell.get("row"), cell.get("layer"), stroke.get("index")
    if any(value is None or int(value) < 0 for value in (row, layer, index)):
        return
    scene = _scene_model("main")
    strokes = scene.cell_to_dict(layer, row, True, POLY_STEP)["image"]["strokes"]
    if index >= len(strokes):
        return
    points = _stroke_points(strokes[index])
    width = float(strokes[index].get("width", 3.0))
    scene.remove_stroke(row, layer, index)
    if not points:
        message["cancel_history"] = True
        _animean().ui.widget.refresh()
        return
    state = _state(scene)
    if _pending(state, row) is not None:
        print("[fukusato] finish the pending guide with its check or x before drawing another.")
        message["cancel_history"] = True
        _animean().ui.widget.refresh()
        return
    seed = tuple(points[len(points) // 2])
    try:
        existing = _frame_guides(state, row)
        if existing:
            if not _same_garment_region(_guide_seed(existing[0]), seed, row):
                raise RuntimeError(
                    "all Fukusato guides on one frame must belong to the same garment region")
        else:
            _region_for(seed, row)  # validate the first guide immediately
    except Exception as error:
        message["cancel_history"] = True
        _animean().ui.widget.refresh()
        print(f"[fukusato] guide rejected: {error}")
        return
    guide_id = state["next_id"]
    state["next_id"] += 1
    state["guides"].append({
        "id": guide_id, "frame": int(row),
        "before": [list(p) for p in points],
        "after": [list(p) for p in points],
        "width": width, "accepted": False,
    })
    _save(scene, state)
    invalidate_mesh()
    refresh_overlays()
    _animean().ui.widget.refresh()
    print(f"[fukusato] guide {guide_id} is pending. Drag the magenta line to its "
          "target, inspect Weight Preview if desired, then click check; x cancels.")


def _find_guide(state, handle_id):
    if not str(handle_id).startswith("fukusato-guide:"):
        return None
    try:
        guide_id = int(str(handle_id).split(":", 1)[1])
    except ValueError:
        return None
    return next((guide for guide in state["guides"]
                 if int(guide.get("id", -1)) == guide_id), None)


def _drag_guide(message):
    if message.get("view") != "main":
        return
    scene = _scene_model("main")
    state = _state(scene)
    phase = message.get("phase")
    if phase == "cancel":
        # Move previews are transient. Cancelling a mouse gesture restores the
        # last persisted pending/accepted state; the x badge remains the
        # explicit way to cancel an already-persisted guide edit transaction.
        _DRAG.update(id=None, origin=None, points=None, moved=False,
                     was_accepted=False)
        refresh_overlays()
        return
    guide = _find_guide(state, message.get("handle"))
    if guide is None:
        return
    position = message.get("position") or {}
    point = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
    if phase == "press":
        if _pending(state, int(guide.get("frame", 0))) not in (None, guide):
            print("[fukusato] another guide is awaiting check/x.")
            return
        _DRAG.update(id=int(guide["id"]), origin=point,
                     points=[tuple(p) for p in guide.get("after") or []],
                     moved=False, was_accepted=bool(guide.get("accepted")))
        return
    if int(guide["id"]) != _DRAG.get("id") or phase not in ("move", "release", "cancel"):
        return
    if phase == "move":
        dx = point[0] - _DRAG["origin"][0]
        dy = point[1] - _DRAG["origin"][1]
        guide["after"] = [[p[0] + dx, p[1] + dy] for p in _DRAG["points"]]
        if _DRAG["was_accepted"]:
            guide["rollback_after"] = [list(p) for p in _DRAG["points"]]
            guide["accepted"] = False
        _DRAG["moved"] = True
        refresh_overlays(state)
        return
    dx = point[0] - _DRAG["origin"][0]
    dy = point[1] - _DRAG["origin"][1]
    moved = _DRAG["moved"] or math.hypot(dx, dy) > 1e-8
    was_accepted = _DRAG["was_accepted"]
    if moved:
        guide["after"] = [[p[0] + dx, p[1] + dy] for p in _DRAG["points"]]
        if was_accepted:
            guide["rollback_after"] = [list(p) for p in _DRAG["points"]]
            guide["accepted"] = False
    _DRAG.update(id=None, origin=None, points=None, moved=False,
                 was_accepted=False)
    if moved:
        _save(scene, state)
        try:
            _animean().ui.history_commit("Move Fukusato Guide", "main")
        except Exception:
            pass
        refresh_overlays()


def _remove_guide(scene, state, guide):
    frame = int(guide.get("frame", 0))
    was_accepted = bool(guide.get("accepted", False))
    original_guides = state["guides"]
    state["guides"] = [entry for entry in state["guides"] if entry is not guide]
    accepted = [entry for entry in _frame_guides(state, frame)
                if bool(entry.get("accepted", False))]
    try:
        if was_accepted and accepted:
            _apply_guides(scene, state, frame, commit_history=False)
        elif was_accepted:
            for layer in sorted(_owned_output_layers(scene, frame), reverse=True):
                core._discard_mapped_layer(scene, layer)
            state["solutions"].pop(str(frame), None)
            _save(scene, state)
        else:
            _save(scene, state)
    except Exception as error:
        state["guides"] = original_guides
        _save(scene, state)
        refresh_overlays()
        print(f"[fukusato] guide {guide['id']} was not removed: {error}")
        return
    refresh_overlays()
    try:
        _animean().ui.history_commit("Remove Fukusato Guide", "main")
    except Exception:
        pass


def _apply_guides(scene, state, frame, commit_history=True):
    guides = [guide for guide in _frame_guides(state, frame)
              if bool(guide.get("accepted", False))]
    if not guides:
        raise RuntimeError("there are no accepted guides to apply")
    mesh = _mesh_for(state, frame, guides[0], force=True)
    foreign = [guide for guide in guides
               if not mesh.contains(*_guide_seed(guide))]
    if foreign:
        ids = ", ".join(str(guide.get("id", "?")) for guide in foreign)
        raise RuntimeError(
            f"guide(s) {ids} belong to another garment region; use one region per frame")
    uv = _solve_uv(mesh, guides)
    added, width_scale = _emit(mesh, uv, frame)
    state["solutions"][str(frame)] = {
        "mesh": mesh.to_dict(), "uv": [list(point) for point in uv],
    }
    _save(scene, state)
    _animean().ui.refresh()
    if commit_history:
        try:
            _animean().ui.history_commit("Fukusato Mapping", "main")
        except Exception:
            pass
    print(f"[fukusato] applied {len(guides)} handle(s) on constrained garment "
          f"mesh ({len(mesh.P)} vertices, {len(mesh.tris)} triangles): "
          f"{added} texture piece(s), stroke width x{width_scale:.2f}.")


def _overlay_action(cell, stroke, message):
    overlay = message.get("overlay") or {}
    item_id = overlay.get("id")
    action = overlay.get("action") or "remove"
    scene = _scene_model("main")
    state = _state(scene)
    guide = _find_guide(state, item_id)
    if guide is None:
        return
    if action == "remove":
        if not guide.get("accepted") and guide.get("rollback_after") is not None:
            guide["after"] = guide.pop("rollback_after")
            guide["accepted"] = True
            _save(scene, state)
            refresh_overlays()
            try:
                _animean().ui.history_commit("Cancel Fukusato Guide Edit", "main")
            except Exception:
                pass
            print(f"[fukusato] edit of guide {guide['id']} cancelled")
            return
        _remove_guide(scene, state, guide)
        return
    if action != "accept":
        return
    frame = int(guide.get("frame", 0))
    rollback_after = guide.pop("rollback_after", None)
    guide["accepted"] = True
    try:
        _apply_guides(scene, state, frame)
    except Exception as error:
        guide["accepted"] = False
        if rollback_after is not None:
            guide["rollback_after"] = rollback_after
        _save(scene, state)
        refresh_overlays()
        print(f"[fukusato] guide {guide['id']} was not applied: {error}")
        return
    refresh_overlays()


def _history_restored(cell, stroke, message):
    _DRAG.update(id=None, origin=None, points=None, moved=False,
                 was_accepted=False)
    invalidate_mesh()
    refresh_overlays()


def _frame_changed(cell, stroke, message):
    if message.get("view") != "main":
        return
    _DRAG.update(id=None, origin=None, points=None, moved=False,
                 was_accepted=False)
    invalidate_mesh()
    refresh_overlays()


def _option_changed(cell, stroke, message):
    hook = str(message.get("hook") or "")
    if not hook.startswith("fk_") or hook == "fk_rerun":
        return
    try:
        core._apply_option(hook, message.get("value"))
    except (TypeError, ValueError) as error:
        print(f"[fukusato] ignored bad option value for {hook}: {error}")
        return
    invalidate_mesh()
    refresh_overlays()


def _menu_items():
    pending = False
    try:
        scene = _scene_model("main")
        pending = _pending(_state(scene), max(scene.current_frame(), 0)) is not None
    except Exception:
        pass
    return [
        {"name": "weight_preview", "title": "Weight Preview / 权重预览",
         "kind": "check", "checked": _VIEW["weight_preview"]},
        {"name": "topology", "title": "Triangle Topology / 三角形拓扑浏览",
         "kind": "check", "checked": _VIEW["topology"]},
        {"kind": "separator"},
        {"name": "rebuild_mesh", "title": "Rebuild Garment Mesh"},
        {"name": "apply_pending", "title": "Apply Pending Guide",
         "enabled": pending},
    ]


def _menu_action(message):
    if message.get("menu") != MENU_NAME:
        return
    name = message.get("name")
    if name in ("weight_preview", "topology"):
        _VIEW[name] = bool(message.get("checked"))
        refresh_overlays()
        print(f"[fukusato] {name.replace('_', ' ')} {'ON' if _VIEW[name] else 'OFF'}")
        return
    if name == "rebuild_mesh":
        invalidate_mesh()
        refresh_overlays()
        print("[fukusato] garment triangulation rebuilt")
        return
    if name == "apply_pending":
        scene = _scene_model("main")
        state = _state(scene)
        frame = max(scene.current_frame(), 0)
        guide = _pending(state, frame)
        if guide is not None:
            _overlay_action({}, {}, {"overlay": {"id": _guide_id(guide),
                                                   "action": "accept"}})


def activate_handle_tool(name=TOOL_NAME, property_value=HANDLE_PROPERTY):
    register_hooks()
    try:
        _animean().ui.set_draw_color(GUIDE_COLOR)
    except Exception:
        pass
    print("[fukusato] draw a handle on MainView, drag its overlay to the desired "
          "position, preview the weights, then click check to deform.")
    return property_value


def run_mapping(name=RUN_TOOL, property_value=RUN_TOOL):
    scene = _scene_model("main")
    state = _state(scene)
    frame = max(scene.current_frame(), 0)
    pending = _pending(state, frame)
    if pending is not None:
        _overlay_action({}, {}, {"overlay": {"id": _guide_id(pending),
                                               "action": "accept"}})
    elif _frame_guides(state, frame):
        try:
            _apply_guides(scene, state, frame)
            refresh_overlays()
        except Exception as error:
            print(f"[fukusato] mapping failed: {error}")
    else:
        print("[fukusato] draw a guide first")
    return property_value


def register_hooks():
    python_hooks.set_hook(_capture_guide, linefinish=True, property=HANDLE_PROPERTY)
    python_hooks.set_hook(_drag_guide, handle=True)
    python_hooks.set_hook(_overlay_action, overlayremove=True, overlayaction=True)
    python_hooks.set_hook(_history_restored, historyrestore=True)
    python_hooks.set_hook(_frame_changed, framechange=True)
    python_hooks.set_hook(_option_changed, option=True)


python_hooks.register_menu({
    "name": MENU_NAME,
    "title": "Fukusato Mapping",
    "host": "main",
    "items": _menu_items,
})
python_hooks.set_hook(_menu_action, menu=True)
crease_line_tool.add_change_listener(_creases_changed)
register_hooks()
