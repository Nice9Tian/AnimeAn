"""Real-time field preview on the main board while dragging mapping lines (issue #46).

Hooks into guide / additional-line drags (pyfile/auto_mapping.py) and Deform line
drags (pyfile/line_tools.py). Evaluates the warm-started mapping on a regular
child-space grid, packs (dx, dy, tag, 0) as little-endian float32 RGBA, and
streams it through animean_python.ui.set_field / set_field_view without touching
the scene model or undo history.
"""
import copy
import math
import time

import gridsolve
import pydeps

FIELD_KEY = "mapping_preview"
GRID_MAX_SIDE = 129
GRID_MIN_SIDE = 33
AREA_RASTER_STEP = 2.0
DEPTH_STRIDE = 8
MIN_UPDATE_INTERVAL = 0.008
GRID_MAX_POINTS = 2000   # refer grid samples per frame
GRID_STRIDES = (1, 2, 3, 6)   # the lattice has 6 samples a cell, ends included
SETTLE_DELAY_MS = 40     # no move for this long: the hand has stopped
SETTLE_PASSES = 40       # a frame that never settles must not spin forever

_SESSION = {
    "active": False,
    "failed": False,
    "error_printed": False,
    "generation": 0,
    "snapshot": None,
}

_LAST_TIMINGS = {}


def _am():
    import auto_mapping
    return auto_mapping


def _clock():
    return time.perf_counter()


def last_timings():
    """Return a copy of the most recent update() sub-stage timings (seconds)."""
    return dict(_LAST_TIMINGS)


def _take_asset_snapshot(am):
    return {
        "child": copy.deepcopy(am._assets_for("child")),
        "main": copy.deepcopy(am._assets_for("main")),
        "proxy_pairing": {
            "key": copy.deepcopy(am._PROXY_PAIRING.get("key")),
            "value": copy.deepcopy(am._PROXY_PAIRING.get("value")),
        },
    }


def _restore_asset_snapshot(am, snapshot):
    if not snapshot:
        return
    for view_name in ("child", "main"):
        target = am._assets_for(view_name)
        saved = snapshot.get(view_name)
        if isinstance(target, dict) and isinstance(saved, dict):
            target.clear()
            target.update(copy.deepcopy(saved))
    pairing = snapshot.get("proxy_pairing")
    if isinstance(pairing, dict):
        am._PROXY_PAIRING["key"] = copy.deepcopy(pairing.get("key"))
        am._PROXY_PAIRING["value"] = copy.deepcopy(pairing.get("value"))


def _clear_field_display(am):
    try:
        ui = getattr(am._animean(), "ui", None)
        if ui is not None and hasattr(ui, "set_field_view"):
            ui.set_field_view("main", None)
    except Exception:
        pass


def _handle_preview_error(am, error):
    if not _SESSION.get("error_printed"):
        _SESSION["error_printed"] = True
        print(f"[mapping_preview] preview disabled after error: {error!r}")
    snapshot = _SESSION.get("snapshot")
    gen = _SESSION.get("generation", 0) + 1
    was_active = bool(_SESSION.get("active"))
    _SESSION.clear()
    _SESSION.update({
        "active": False,
        "failed": True,
        "error_printed": True,
        "generation": gen,
        "snapshot": snapshot,
    })
    if was_active:
        _clear_field_display(am)


def _scanline_fill_grid(np, polygons, ox, oy, step_x, step_y, gw, gh):
    """Odd-even scanline rasterization of `polygons` onto a (gh, gw) boolean grid
    sampled at (ox + c * step_x, oy + r * step_y)."""
    x0_list = []
    y0_list = []
    x1_list = []
    y1_list = []
    for ring in polygons or []:
        if not ring or len(ring) < 3:
            continue
        pts = np.asarray(ring, dtype=np.float64)
        rx0 = pts[:, 0]
        ry0 = pts[:, 1]
        rx1 = np.roll(rx0, -1)
        ry1 = np.roll(ry0, -1)
        non_horiz = np.abs(ry1 - ry0) > 1e-18
        if np.any(non_horiz):
            x0_list.append(rx0[non_horiz])
            y0_list.append(ry0[non_horiz])
            x1_list.append(rx1[non_horiz])
            y1_list.append(ry1[non_horiz])

    if not x0_list:
        return np.zeros((gh, gw), dtype=bool)

    x0 = np.concatenate(x0_list)
    y0 = np.concatenate(y0_list)
    x1 = np.concatenate(x1_list)
    y1 = np.concatenate(y1_list)
    inv_dy = (x1 - x0) / (y1 - y0)

    ys = oy + np.arange(gh, dtype=np.float64) * step_y
    cond = (y0[None, :] > ys[:, None]) != (y1[None, :] > ys[:, None])
    r_idx, e_idx = np.nonzero(cond)
    if r_idx.size == 0:
        return np.zeros((gh, gw), dtype=bool)

    x_int = x0[e_idx] + (ys[r_idx] - y0[e_idx]) * inv_dy[e_idx]
    c_idx = np.clip(np.ceil((x_int - ox) / step_x).astype(np.int32), 0, gw)
    diff = np.zeros((gh, gw + 1), dtype=np.int32)
    np.add.at(diff, (r_idx, c_idx), 1)
    return (np.cumsum(diff[:, :-1], axis=1) & 1).astype(bool)


def _rasterize_area_lut(np, polygons, step=AREA_RASTER_STEP):
    """Pre-rasterize `polygons` onto a regular 2D boolean lookup table at cell centers."""
    xs = []
    ys = []
    for ring in polygons or []:
        if not ring or len(ring) < 3:
            continue
        pts = np.asarray(ring, dtype=np.float64)
        xs.append(pts[:, 0])
        ys.append(pts[:, 1])
    if not xs:
        return None
    all_x = np.concatenate(xs)
    all_y = np.concatenate(ys)
    xmin, xmax = float(np.min(all_x)), float(np.max(all_x))
    ymin, ymax = float(np.min(all_y)), float(np.max(all_y))
    ox = xmin - step
    oy = ymin - step
    gw = max(2, int(math.ceil((xmax - xmin) / step)) + 3)
    gh = max(2, int(math.ceil((ymax - ymin) / step)) + 3)
    mask = _scanline_fill_grid(
        np, polygons,
        ox + 0.5 * step, oy + 0.5 * step,
        step, step, gw, gh,
    )
    return (mask, ox, oy, 1.0 / step)


def _lookup_area_lut(np, lut, x_arr, y_arr):
    """Look up (x_arr, y_arr) in a pre-rasterized polygon boolean LUT."""
    mask, ox, oy, inv_step = lut
    gh, gw = mask.shape
    ci = np.floor((x_arr - ox) * inv_step).astype(np.int32)
    ri = np.floor((y_arr - oy) * inv_step).astype(np.int32)
    in_bounds = (ci >= 0) & (ci < gw) & (ri >= 0) & (ri < gh)
    ci_safe = np.where(in_bounds, ci, 0)
    ri_safe = np.where(in_bounds, ri, 0)
    return in_bounds & mask[ri_safe, ci_safe]


def _fill_invalid_from_neighbors(np, dx, dy, valid, passes=2):
    out_x = np.where(valid, dx, 0.0).copy()
    out_y = np.where(valid, dy, 0.0).copy()
    filled = valid.copy()
    if np.all(filled) or not np.any(filled):
        return out_x, out_y
    for _ in range(passes):
        need = ~filled
        if not np.any(need):
            break
        sum_x = np.zeros_like(out_x)
        sum_y = np.zeros_like(out_y)
        count = np.zeros(out_x.shape, dtype=np.int32)
        if out_x.shape[0] > 1:
            m = filled[:-1, :]
            sum_x[1:, :] += np.where(m, out_x[:-1, :], 0.0)
            sum_y[1:, :] += np.where(m, out_y[:-1, :], 0.0)
            count[1:, :] += m.astype(np.int32)
            m = filled[1:, :]
            sum_x[:-1, :] += np.where(m, out_x[1:, :], 0.0)
            sum_y[:-1, :] += np.where(m, out_y[1:, :], 0.0)
            count[:-1, :] += m.astype(np.int32)
        if out_x.shape[1] > 1:
            m = filled[:, :-1]
            sum_x[:, 1:] += np.where(m, out_x[:, :-1], 0.0)
            sum_y[:, 1:] += np.where(m, out_y[:, :-1], 0.0)
            count[:, 1:] += m.astype(np.int32)
            m = filled[:, 1:]
            sum_x[:, :-1] += np.where(m, out_x[:, 1:], 0.0)
            sum_y[:, :-1] += np.where(m, out_y[:, 1:], 0.0)
            count[:, :-1] += m.astype(np.int32)
        hit = need & (count > 0)
        if not np.any(hit):
            break
        out_x[hit] = sum_x[hit] / count[hit]
        out_y[hit] = sum_y[hit] / count[hit]
        filled[hit] = True
    out_x[~np.isfinite(out_x)] = 0.0
    out_y[~np.isfinite(out_y)] = 0.0
    return out_x, out_y


def _preview_pairing(am, child_assets, main_assets, session):
    child_item = child_assets.get(am.PROXY_PROPERTY)
    main_item = main_assets.get(am.PROXY_PROPERTY)
    if not (child_item and main_item
            and child_item.get("points") and main_item.get("points")):
        return None, None
    kept = am._kept_pairing(child_item, main_item)
    if kept is not None:
        paired = {
            "child": child_item[am.PAIRING_KEY]["stations"],
            "main": main_item[am.PAIRING_KEY]["stations"],
        }
        pkey = am._pairing_key(child_item, main_item)
        return paired, pkey
    raw_key = (
        tuple(map(tuple, child_item["points"])),
        tuple(map(tuple, main_item["points"])),
    )
    cache = session.setdefault("pairing_cache", {})
    if raw_key in cache:
        paired = cache[raw_key]
    else:
        import proxy_shape
        paired, _why = proxy_shape.match(child_item["points"], main_item["points"])
        cache[raw_key] = paired
    if paired is None:
        return None, None
    return paired, raw_key


def _effective_specs_and_pairs(am, session):
    """Build (child_h, child_v, main_h, main_v, additional_pairs, ground_pairs)
    for the current drag state without mutating main auto-axes in place."""
    child_assets = am._assets_for("child")
    main_assets = am._assets_for("main")
    if any(prop not in child_assets for prop in am.GUIDE_PROPERTIES):
        return None
    child_h = child_assets[am.H_PROPERTY]
    child_v = child_assets[am.V_PROPERTY]

    paired, pairing_key = _preview_pairing(am, child_assets, main_assets, session)
    main_specs = {}
    main_auto_moved = False
    carried_cache = session.setdefault("carried_axes", {})

    for prop in am.GUIDE_PROPERTIES:
        item = main_assets.get(prop)
        if paired is not None and (item is None or item.get("auto")):
            source = hash((tuple(map(tuple, child_assets[prop]["points"])), pairing_key))
            if item is not None and item.get("source") == source:
                main_specs[prop] = item
            else:
                ckey = (prop, source)
                if ckey in carried_cache:
                    line = carried_cache[ckey]
                else:
                    import proxy_shape
                    line = proxy_shape.carried_axis(paired, child_assets[prop]["points"])
                    carried_cache[ckey] = line
                if line:
                    spec = {"points": line, "width": 3.0, "auto": True, "source": source}
                    if item and item.get(am.GROUND_FLAG):
                        spec[am.GROUND_FLAG] = True
                    main_specs[prop] = spec
                    main_auto_moved = True
                elif item is not None:
                    main_specs[prop] = item
        elif item is not None:
            main_specs[prop] = item

    if any(prop not in main_specs for prop in am.GUIDE_PROPERTIES):
        return None
    main_h = main_specs[am.H_PROPERTY]
    main_v = main_specs[am.V_PROPERTY]

    if (len(child_h.get("points") or []) < 2
            or len(child_v.get("points") or []) < 2
            or len(main_h.get("points") or []) < 2
            or len(main_v.get("points") or []) < 2):
        return None
    drag_view = session.get("view")
    drag_id = session.get("overlay_id")
    drop_child_third = (drag_view == "child" and drag_id in am.GUIDE_PROPERTIES)
    drop_main_third = (
        (drag_view == "main" and drag_id in am.GUIDE_PROPERTIES)
        or main_auto_moved
    )
    if (not session.get("active") or drop_child_third) and not am._polylines_cross(
        child_h["points"], child_v["points"]
    ):
        return None
    if (not session.get("active") or drop_main_third) and not am._polylines_cross(
        main_h["points"], main_v["points"]
    ):
        return None

    raw_additional = am._additional_pairs()
    additional_pairs = []
    for c_item, m_item in raw_additional or []:
        c_use = dict(c_item) if (drop_child_third and c_item) else c_item
        if drop_child_third and c_use:
            c_use.pop("third", None)
        m_use = dict(m_item) if (drop_main_third and m_item) else m_item
        if drop_main_third and m_use:
            m_use.pop("third", None)
        additional_pairs.append((c_use, m_use))

    ground_pairs = am._ground_pairs(main_specs, paired=paired)
    return (child_h, child_v, main_h, main_v,
            additional_pairs or None, ground_pairs)


def _source_bounds_and_columns(am, child_scene, child_frame):
    structure = child_scene.get_structure()
    if child_frame < 0 or child_frame >= structure.get("frame_count", 0):
        return None, None
    layers = structure.get("layers") or []
    columns = [
        int(layer["index"])
        for layer in reversed(layers)
        if layer.get("visible", True) and not layer.get("internal", False)
    ]
    if not columns:
        return None, None

    child_pattern = am._collect_pattern_strokes(child_scene, child_frame, want_commands=False)
    child_fills = am._collect_pattern_fills(child_scene, child_frame)
    if not child_pattern and not child_fills:
        return None, None

    xs = []
    ys = []
    max_width = 3.0
    for stroke in child_pattern:
        w_s = float(stroke.get("width", 3.0) or 3.0)
        if w_s > max_width:
            max_width = w_s
        polys = am._stroke_polylines(stroke)
        if polys:
            for poly in polys:
                for pt in poly:
                    xs.append(float(pt[0]))
                    ys.append(float(pt[1]))
        else:
            b = stroke.get("bounds") or {}
            if "width" in b and "height" in b:
                bx, by = float(b.get("x", 0.0)), float(b.get("y", 0.0))
                bw, bh = float(b.get("width", 0.0)), float(b.get("height", 0.0))
                xs.extend((bx, bx + bw))
                ys.extend((by, by + bh))

    for fill in child_fills:
        rings = am._path_commands_to_polygons(fill.get("commands"))
        if rings:
            for ring in rings:
                for pt in ring:
                    xs.append(float(pt[0]))
                    ys.append(float(pt[1]))
        else:
            b = fill.get("bounds") or {}
            if "width" in b and "height" in b:
                bx, by = float(b.get("x", 0.0)), float(b.get("y", 0.0))
                bw, bh = float(b.get("width", 0.0)), float(b.get("height", 0.0))
                xs.extend((bx, bx + bw))
                ys.extend((by, by + bh))

    if not xs or not ys:
        return None, None

    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    pad = max(8.0, max_width)
    x0 = min_x - pad
    y0 = min_y - pad
    w = max((max_x - min_x) + 2.0 * pad, 16.0)
    h = max((max_y - min_y) + 2.0 * pad, 16.0)
    return (float(x0), float(y0), float(w), float(h)), columns


def _unit_hide_columns(am, main_scene, uid):
    meta = am._UNIT_META.get(uid) or {}
    member_ids = list((meta.get("members") or {}).keys())
    if meta.get("primary"):
        member_ids.append(meta.get("primary"))
    hide_cols = []
    seen = set()
    for lid in member_ids:
        try:
            idx = int(main_scene.layer_index_for_id(int(lid)))
        except Exception:
            continue
        if idx >= 0 and idx not in seen:
            seen.add(idx)
            hide_cols.append(idx)
    hide_cols.sort()
    return hide_cols


def _typical_stretch(np, phi_x, phi_y, valid, du, dv):
    """How much the mapping scales lengths across the strokes, as one number.

    The scope widens strokes in texture space and the field then carries the
    outline, so the field already applies the stretch. The release draws
    `width * width_scale` in board space instead; dividing by the typical
    stretch makes the two agree wherever the stretch is typical."""
    det = gridsolve.det2(phi_x, phi_y, du, dv)
    keep = valid & np.isfinite(det)
    if not np.any(keep):
        return 1.0
    stretch = float(np.median(np.sqrt(np.abs(det[keep]))))
    return stretch if stretch > 1e-6 else 1.0


def begin(view, overlay_id):
    """Prepare a live preview session at drag press."""
    am = _am()
    gen = _SESSION.get("generation", 0) + 1
    if _SESSION.get("active"):
        _clear_field_display(am)
    _drop_grid(am)
    _SESSION.clear()
    _SESSION.update({
        "active": False,
        "failed": False,
        "error_printed": False,
        "generation": gen,
        "view": view or "main",
        "overlay_id": str(overlay_id or ""),
        "snapshot": None,
    })

    try:
        np = pydeps.ensure("numpy")
        if np is None:
            return
        uid = am._ACTIVE_UNIT.get("id")
        if uid is None or uid not in am._UNIT_META:
            return
        settings = am._unit_settings(uid)
        if not settings.get("auto_render", True):
            return
        ui = getattr(am._animean(), "ui", None)
        if (ui is None
                or not hasattr(ui, "field_display_supported")
                or not bool(ui.field_display_supported("main"))):
            return

        child_scene = am._scene_model("child")
        main_scene = am._scene_model("main")
        child_frame = max(int(child_scene.current_frame()), 0)
        main_frame = int(main_scene.current_frame())
        rect, columns = _source_bounds_and_columns(am, child_scene, child_frame)
        if rect is None or not columns:
            return

        specs = _effective_specs_and_pairs(am, _SESSION)
        if specs is None:
            return
        child_h, child_v, main_h, main_v, add_pairs, gnd_pairs = specs
        warm = {}
        mapper, width_scale = am.build_mapper(
            child_h, child_v, main_h, main_v,
            additional_pairs=add_pairs, ground_pairs=gnd_pairs, warm=warm,
        )
        if mapper is None:
            return

        x0, y0, w, h = rect
        if w >= h:
            W = GRID_MAX_SIDE
            H = max(GRID_MIN_SIDE, min(GRID_MAX_SIDE, int(round(GRID_MAX_SIDE * h / w))))
        else:
            H = GRID_MAX_SIDE
            W = max(GRID_MIN_SIDE, min(GRID_MAX_SIDE, int(round(GRID_MAX_SIDE * w / h))))

        du = w / (W - 1)
        dv = h / (H - 1)
        max_edge = float(max(du, dv))

        gx = np.linspace(x0, x0 + w, W, dtype=np.float64)
        gy = np.linspace(y0, y0 + h, H, dtype=np.float64)
        px, py = np.meshgrid(gx, gy, indexing="ij")

        child_area = (am._assets_for("child").get(am.MAPPING_AREA_PROPERTY) or {}).get("polygons")
        in_child_area = (
            _scanline_fill_grid(np, child_area, x0, y0, du, dv, W, H).T
            if child_area else None
        )
        main_area = (am._assets_for("main").get(am.MAPPING_AREA_PROPERTY) or {}).get("polygons")
        main_area_lut = _rasterize_area_lut(np, main_area) if main_area else None

        coarse_i = np.unique(np.append(np.arange(0, W, DEPTH_STRIDE), W - 1))
        coarse_j = np.unique(np.append(np.arange(0, H, DEPTH_STRIDE), H - 1))
        nearest_ci = np.argmin(np.abs(np.arange(W)[:, None] - coarse_i[None, :]), axis=1)
        nearest_cj = np.argmin(np.abs(np.arange(H)[:, None] - coarse_j[None, :]), axis=1)

        skip_properties = list((
            *am.GUIDE_PROPERTIES,
            *am.MAPPING_OUTPUT_PROPERTIES,
            "auto_mapping",
            am.AUTO_MAPPING2_TOOL,
            am.MAPPING_AREA_PROPERTY,
        ))
        hide_columns = _unit_hide_columns(am, main_scene, uid)
        l_h, l_v, ok = mapper.third_of_batch(px, py)
        phi_x, phi_y = mapper.main_of_third_batch(l_h, l_v)
        stretch = _typical_stretch(np, phi_x, phi_y, ok if in_child_area is None else ok & in_child_area, du, dv)
        third_cache = {
            "key": _child_axes_key(child_h, child_v),
            "value": (l_h, l_v, ok),
        }
        snapshot = _take_asset_snapshot(am)
        _SESSION.update({
            "active": True,
            "snapshot": snapshot,
            "np": np,
            "uid": uid,
            "main_frame": main_frame,
            "warm": warm,
            "columns": columns,
            "frame": child_frame,
            "skip_properties": skip_properties,
            "hide_columns": hide_columns,
            "rect": rect,
            "W": W,
            "H": H,
            "du": du,
            "dv": dv,
            "max_edge": max_edge,
            "width_scale": float(width_scale) / stretch,
            "px": px,
            "py": py,
            "in_child_area": in_child_area,
            "main_area_lut": main_area_lut,
            "third_cache": third_cache,
            "coarse_i": coarse_i,
            "coarse_j": coarse_j,
            "nearest_ci": nearest_ci,
            "nearest_cj": nearest_cj,
            "coarse_depth": None,
            "last_passes": None,
            "last_finish_time": None,
            "pending": False,
            "timer_armed": False,
            "prev_labels": None,
            "prev_n_front": 0,
            "prev_n_regions": 0,
            "prev_region_depths": None,
        })
        if am._FOLD["split"]:
            phi_x, phi_y = mapper.main_of_third_batch(l_h, l_v)
            dx_raw = phi_x - px
            dy_raw = phi_y - py
            valid = ok & np.isfinite(dx_raw) & np.isfinite(dy_raw)
            if in_child_area is not None:
                valid = valid & in_child_area
            if main_area_lut is not None:
                safe_px = np.where(np.isfinite(phi_x), phi_x, 0.0)
                safe_py = np.where(np.isfinite(phi_y), phi_y, 0.0)
                valid = valid & _lookup_area_lut(np, main_area_lut, safe_px, safe_py)
            dx, dy = _fill_invalid_from_neighbors(np, dx_raw, dy_raw, valid, passes=2)
            det = gridsolve.det2(
                (px + dx) * float(mapper.fold_reference),
                py + dy,
                du,
                dv,
            )
            is_front = det >= 0.0
            _update_coarse_depth_table(
                am, np, _SESSION, mapper, l_h, l_v, valid, is_front
            )
            _init_region_depths_from_coarse(np, _SESSION, valid, is_front)
    except Exception as error:
        _handle_preview_error(am, error)


def _child_axes_key(child_h, child_v):
    return (
        tuple(map(tuple, child_h.get("points") or ())),
        repr(child_h.get("commands")),
        tuple(map(tuple, child_v.get("points") or ())),
        repr(child_v.get("commands")),
    )


def _update_coarse_depth_table(am, np, session, mapper, l_h, l_v, valid, is_front):
    t0 = _clock()
    coarse_i = session["coarse_i"]
    coarse_j = session["coarse_j"]
    ci_grid, cj_grid = np.meshgrid(coarse_i, coarse_j, indexing="ij")
    c_valid = valid[ci_grid, cj_grid]
    c_lh = l_h[ci_grid, cj_grid]
    c_lv = l_v[ci_grid, cj_grid]

    if np.any(c_valid):
        v_lh = c_lh[c_valid]
        v_lv = c_lv[c_valid]
    elif np.any(valid):
        v_lh = l_h[valid]
        v_lv = l_v[valid]
    else:
        v_lh = None
        v_lv = None

    depths = np.zeros((len(coarse_i), len(coarse_j)), dtype=np.int32)
    if v_lh is not None and v_lh.size > 0:
        warp = getattr(mapper, "warp", None)
        if warp is not None:
            w_h, w_v = warp.apply_batch(v_lh, v_lv)
        else:
            w_h, w_v = v_lh, v_lv
        arc_h = np.where(w_h >= 0.0, w_h * mapper.h_scales[1], w_h * mapper.h_scales[0])
        arc_v = np.where(w_v >= 0.0, w_v * mapper.v_scales[1], w_v * mapper.v_scales[0])
        h_range = (float(np.min(arc_h)), float(np.max(arc_h)))
        v_range = (float(np.min(arc_v)), float(np.max(arc_v)))

        am._prepare_fold_context(mapper, h_range, v_range)
        px = session["px"]
        py = session["py"]
        front_const = am._MappedOutput.FRONT
        back_const = am._MappedOutput.BACK
        for ii, i in enumerate(coarse_i):
            for jj, j in enumerate(coarse_j):
                if not valid[i, j]:
                    continue
                pt = (float(px[i, j]), float(py[i, j]))
                side = front_const if is_front[i, j] else back_const
                depths[ii, jj] = int(am._fold_depth(mapper, pt, side))

        if not np.all(c_valid) and np.any(c_valid):
            d_float, _ = _fill_invalid_from_neighbors(
                np, depths.astype(np.float64), np.zeros_like(depths, dtype=np.float64),
                c_valid, passes=4,
            )
            depths = np.rint(d_float).astype(np.int32)

    dt = _clock() - t0
    session["coarse_depth"] = depths
    return dt


def _label_front_back_regions(np, valid, is_front):
    front_mask = valid & is_front
    back_mask = valid & ~is_front
    front_labels, n_front = gridsolve.label_regions(front_mask)
    back_labels, n_back = gridsolve.label_regions(back_mask)
    if n_back > 0 and n_front > 0:
        labels = np.where(back_labels > 0, back_labels + n_front, front_labels)
    elif n_back > 0:
        labels = back_labels
    else:
        labels = front_labels
    return labels, int(n_front), int(n_front + n_back)


def _region_adjacency(np, labels, n_regions):
    adj4 = np.zeros((n_regions + 1, n_regions + 1), dtype=bool)
    adj8 = np.zeros((n_regions + 1, n_regions + 1), dtype=bool)
    if n_regions <= 1:
        return adj4, adj8

    if labels.shape[0] > 1:
        a = labels[:-1, :]
        b = labels[1:, :]
        m = (a != b) & (a > 0) & (b > 0)
        if np.any(m):
            am_idx, bm_idx = a[m], b[m]
            adj4[am_idx, bm_idx] = True
            adj4[bm_idx, am_idx] = True
    if labels.shape[1] > 1:
        a = labels[:, :-1]
        b = labels[:, 1:]
        m = (a != b) & (a > 0) & (b > 0)
        if np.any(m):
            am_idx, bm_idx = a[m], b[m]
            adj4[am_idx, bm_idx] = True
            adj4[bm_idx, am_idx] = True

    adj8 |= adj4
    if labels.shape[0] > 1 and labels.shape[1] > 1:
        for a, b in (
            (labels[:-1, :-1], labels[1:, 1:]),
            (labels[:-1, 1:], labels[1:, :-1]),
        ):
            m = (a != b) & (a > 0) & (b > 0)
            if np.any(m):
                am_idx, bm_idx = a[m], b[m]
                adj8[am_idx, bm_idx] = True
                adj8[bm_idx, am_idx] = True
    return adj4, adj8


def _propagate_unassigned_depths(region_depths, adj4, n_front, n_regions, fallback_fn=None):
    while True:
        progressed = False
        for r in range(1, n_regions + 1):
            if region_depths[r] >= 0:
                continue
            nbrs = [
                int(region_depths[nb])
                for nb in range(1, n_regions + 1)
                if adj4[r, nb] and region_depths[nb] >= 0
            ]
            if nbrs:
                want_even = (r <= n_front)
                d = min(nbrs) + 1
                if ((d % 2) == 0) != want_even:
                    d += 1
                region_depths[r] = d
                progressed = True
        if not progressed:
            break

    for r in range(1, n_regions + 1):
        want_even = (r <= n_front)
        if region_depths[r] < 0:
            d = int(fallback_fn(r)) if fallback_fn is not None else (0 if want_even else 1)
            if ((d % 2) == 0) != want_even:
                d += 1
            region_depths[r] = d
        elif ((int(region_depths[r]) % 2) == 0) != want_even:
            region_depths[r] += 1


def _init_region_depths_from_coarse(np, session, valid, is_front):
    c_depth = session["coarse_depth"]
    nci = session["nearest_ci"]
    ncj = session["nearest_cj"]
    depth = c_depth[nci[:, None], ncj[None, :]].copy()
    mismatch = ((depth % 2) == 0) != is_front
    depth[mismatch] += 1

    labels, n_front, n_regions = _label_front_back_regions(np, valid, is_front)
    region_depths = np.full(n_regions + 1, -1, dtype=np.int32)
    region_depths[0] = 0
    if n_regions > 0:
        coarse_i = session["coarse_i"]
        coarse_j = session["coarse_j"]
        ci_grid, cj_grid = np.meshgrid(coarse_i, coarse_j, indexing="ij")
        c_labels = labels[ci_grid, cj_grid]
        c_valid = valid[ci_grid, cj_grid]
        for r in range(1, n_regions + 1):
            want_even = (r <= n_front)
            cmask = c_valid & (c_labels == r)
            if np.any(cmask):
                vals = c_depth[cmask]
                vals = np.where(((vals % 2) == 0) != want_even, vals + 1, vals)
                counts = np.bincount(np.maximum(0, vals))
                d = int(np.argmax(counts))
                if ((d % 2) == 0) != want_even:
                    d += 1
                region_depths[r] = d

        if np.any(region_depths[1:] < 0):
            adj4, _adj8 = _region_adjacency(np, labels, n_regions)

            def _fallback_fine(r):
                vals = depth[labels == r]
                if vals.size > 0:
                    return int(np.argmax(np.bincount(np.maximum(0, vals))))
                return 0 if (r <= n_front) else 1

            _propagate_unassigned_depths(region_depths, adj4, n_front, n_regions, _fallback_fine)

    session["prev_labels"] = labels
    session["prev_n_front"] = n_front
    session["prev_n_regions"] = n_regions
    session["prev_region_depths"] = region_depths
    return region_depths[labels]


def _inherit_region_depths(am, np, session, mapper, l_h, l_v, valid, is_front):
    t0 = _clock()
    labels, n_front, n_regions = _label_front_back_regions(np, valid, is_front)
    if n_regions == 0:
        session["prev_labels"] = labels
        session["prev_n_front"] = 0
        session["prev_n_regions"] = 0
        session["prev_region_depths"] = np.zeros(1, dtype=np.int32)
        dt = _clock() - t0
        return np.zeros_like(labels, dtype=np.int32), dt

    prev_labels = session.get("prev_labels")
    prev_n_front = int(session.get("prev_n_front", 0))
    prev_n_regions = int(session.get("prev_n_regions", 0))
    prev_depths = session.get("prev_region_depths")

    adj4, adj8 = _region_adjacency(np, labels, n_regions)
    region_depths = np.full(n_regions + 1, -1, dtype=np.int32)
    region_depths[0] = 0
    best_prev = np.zeros(n_regions + 1, dtype=np.int32)
    best_prev_ov = np.zeros(n_regions + 1, dtype=np.int32)

    if prev_labels is not None and prev_depths is not None and prev_n_regions > 0:
        n_cols = prev_n_regions + 1
        pair_idx = labels.ravel() * n_cols + prev_labels.ravel()
        overlap = np.bincount(
            pair_idx, minlength=(n_regions + 1) * n_cols
        ).reshape(n_regions + 1, n_cols)
        overlap[0, :] = 0
        overlap[:, 0] = 0
        overlap[1:n_front + 1, prev_n_front + 1:] = 0
        overlap[n_front + 1:, 1:prev_n_front + 1] = 0

        for r in range(1, n_regions + 1):
            row = overlap[r, 1:]
            mx = int(np.max(row))
            if mx > 0:
                best_prev[r] = int(np.argmax(row)) + 1
                best_prev_ov[r] = mx

        claimants_of_p = {}
        for r in range(1, n_regions + 1):
            p = int(best_prev[r])
            if p > 0:
                claimants_of_p.setdefault(p, []).append(r)

        anchor_dist2 = None
        for p, r_list in claimants_of_p.items():
            p_depth = int(prev_depths[p])
            if len(r_list) == 1:
                region_depths[r_list[0]] = p_depth
            else:
                if anchor_dist2 is None:
                    main_f = mapper.main_frame
                    anc = am._nearest_arc()
                    anc = (
                        min(max(anc[0], -main_f.h_arc), main_f.h_total - main_f.h_arc),
                        min(max(anc[1], -main_f.v_arc), main_f.v_total - main_f.v_arc),
                    )
                    anc_lh, anc_lv = mapper.unscale_arcs(*anc)
                    warp = getattr(mapper, "warp", None)
                    if warp is not None:
                        anc_lh, anc_lv = warp.unapply((anc_lh, anc_lv))
                    anchor_dist2 = (l_h - anc_lh) ** 2 + (l_v - anc_lv) ** 2

                def _split_rank(r_cand):
                    rmask = (labels == r_cand)
                    min_d2 = float(np.min(anchor_dist2[rmask])) if np.any(rmask) else float("inf")
                    return (round(min_d2, 0), -int(best_prev_ov[r_cand]))

                r_best = min(r_list, key=_split_rank)
                region_depths[r_best] = p_depth
                same_sheet = {r_best}
                changed = True
                while changed:
                    changed = False
                    for r_other in r_list:
                        if r_other in same_sheet:
                            continue
                        if any(adj8[r_other, s] for s in same_sheet):
                            same_sheet.add(r_other)
                            region_depths[r_other] = p_depth
                            changed = True
                for r_other in r_list:
                    if r_other in same_sheet:
                        continue
                    if int(best_prev_ov[r_other]) <= 4 or not np.any(adj4[r_other, 1:]):
                        region_depths[r_other] = p_depth

        for p in range(1, prev_n_regions + 1):
            col = overlap[1:, p]
            mx = int(np.max(col))
            if mx > 0:
                r_win = int(np.argmax(col)) + 1
                if region_depths[r_win] >= 0 and mx * 2 >= int(np.sum(col)):
                    p_depth = int(prev_depths[p])
                    if p_depth < region_depths[r_win]:
                        region_depths[r_win] = p_depth

    def _fallback_prev(r):
        p = int(best_prev[r])
        if p > 0 and prev_depths is not None:
            return int(prev_depths[p])
        return 0 if (r <= n_front) else 1

    _propagate_unassigned_depths(region_depths, adj4, n_front, n_regions, _fallback_prev)

    depth = region_depths[labels]
    session["prev_labels"] = labels
    session["prev_n_front"] = n_front
    session["prev_n_regions"] = n_regions
    session["prev_region_depths"] = region_depths
    dt = _clock() - t0
    return depth, dt


def _build_passes(am, session, valid, depth):
    uid = session["uid"]
    settings = am._unit_settings(uid)
    front_vis = bool(settings.get("front_visible", True))
    back_vis = bool(settings.get("back_visible", True))
    bc = am._FOLD["back_color"]
    tint = (int(bc[0]), int(bc[1]), int(bc[2]), 255)

    passes = []
    np = session["np"]
    if np.any(valid):
        valid_depths = depth[valid]
        max_d = int(np.max(valid_depths))
        for d in range(max_d, -1, -1):
            if not np.any(valid_depths == d):
                continue
            is_back = (d % 2) == 1
            if is_back and not back_vis:
                continue
            if not is_back and not front_vis:
                continue
            entry = {
                "tag": (float(d) + 0.5, float(d) + 1.5),
                "opacity": 1.0,
            }
            if is_back:
                entry["tint"] = tint
            passes.append(entry)
    return passes


def _grid_lattice_points(am, np, frame):
    """The refer grid's iso-lines through `frame`, as (lines, samples) arrays."""
    levels, samples = am._grid_lattice()
    total = 2 * len(levels) * len(samples)
    stride = next((s for s in GRID_STRIDES if total / s <= GRID_MAX_POINTS),
                  GRID_STRIDES[-1])
    along = np.array(samples[::stride], dtype=np.float64)
    across = np.array(levels, dtype=np.float64)
    u = np.concatenate((np.repeat(across, along.size), np.tile(along, across.size)))
    v = np.concatenate((np.tile(along, across.size), np.repeat(across, along.size)))
    du = u * np.where(u >= 0.0, frame.h_side[1], frame.h_side[0])
    dv = v * np.where(v >= 0.0, frame.v_side[1], frame.v_side[0])
    x, y = frame.hv_batch(du, dv)
    return (np.asarray(x).reshape(-1, along.size),
            np.asarray(y).reshape(-1, along.size))


def _grid_items(am, np, x, y, keep):
    """Overlay items for the iso-lines, broken where `keep` is false."""
    items = []
    for row in range(x.shape[0]):
        kept = keep[row]
        edges = np.flatnonzero(np.diff(np.concatenate(([False], kept, [False]))))
        for first, last in zip(edges[0::2].tolist(), edges[1::2].tolist()):
            if last - first < 2:
                continue
            items.append({
                "id": "refer_rect_grid",
                "points": list(zip(x[row, first:last].tolist(), y[row, first:last].tolist())),
                "color": am.GRID_COLOR,
                "width": 1.0,
                "removable": False,
            })
    return items


def _update_grid(am, np, mapper, ckey):
    """The refer grid follows the drag on the frame's mapper, in one batch. An
    edge at a fold stops at its last valid sample; the release bisects it."""
    drawn_on = [view for view in ("main", "child")
                if am.refer_rect_enabled(view)
                and (view == "main" or _SESSION["view"] == "child")]
    if not drawn_on:
        return 0.0
    t0 = _clock()
    lattice = _SESSION.setdefault("grid_lattice", {})
    if lattice.get("key") != ckey:
        lattice["key"] = ckey
        lattice["points"] = _grid_lattice_points(am, np, mapper.child_frame)
        lattice["third"] = None
    px, py = lattice["points"]
    pushed = _SESSION.setdefault("grid_items", {})
    if "child" in drawn_on:
        pushed["child"] = am._GRID_CACHE["child"] = _grid_items(
            am, np, px, py, np.isfinite(px) & np.isfinite(py))
    if "main" in drawn_on:
        if lattice["third"] is None:
            lattice["third"] = mapper.third_of_batch(px, py)
        l_h, l_v, ok = lattice["third"]
        x, y = mapper.main_of_third_batch(l_h, l_v)
        keep = np.isfinite(x) & np.isfinite(y)
        if mapper.can_fold():
            keep = keep & ok
        items = _grid_items(am, np, x, y, keep)
        if items:
            pushed["main"] = am._GRID_CACHE["main"] = items
    for view in drawn_on:
        am._push_overlay(view)
    return _clock() - t0


def _drop_grid(am):
    """A preview's grid must not outlive it: put back the mapping's own."""
    stale = [view for view, items in _SESSION.get("grid_items", {}).items()
             if am._GRID_CACHE.get(view) is items]
    if stale:
        am._invalidate_grid_cache()
        for view in stale:
            am._push_overlay(view)


def _context_valid(am):
    if am._ACTIVE_UNIT.get("id") != _SESSION.get("uid"):
        return False
    try:
        main_frame = int(am._scene_model("main").current_frame())
    except Exception:
        return False
    return main_frame == _SESSION.get("main_frame")


def _suspend_invalid_frame(am):
    _SESSION["last_passes"] = None
    _clear_field_display(am)
    _SESSION["last_finish_time"] = _clock()
    return False


def _compute_and_push():
    am = _am()
    if not _SESSION.get("active") or _SESSION.get("failed"):
        return False
    t_start = _clock()
    np = _SESSION["np"]

    specs = _effective_specs_and_pairs(am, _SESSION)
    if specs is None:
        return _suspend_invalid_frame(am)
    child_h, child_v, main_h, main_v, add_pairs, gnd_pairs = specs

    t_bm0 = _clock()
    mapper, _ws = am.build_mapper(
        child_h, child_v, main_h, main_v,
        additional_pairs=add_pairs, ground_pairs=gnd_pairs,
        warm=_SESSION["warm"],
    )
    t_bm = _clock() - t_bm0
    if mapper is None:
        return _suspend_invalid_frame(am)

    px = _SESSION["px"]
    py = _SESSION["py"]
    t_th0 = _clock()
    tcache = _SESSION["third_cache"]
    ckey = _child_axes_key(child_h, child_v)
    if _SESSION["view"] != "child" and "value" in tcache:
        l_h, l_v, ok = tcache["value"]
    elif tcache.get("key") == ckey and "value" in tcache:
        l_h, l_v, ok = tcache["value"]
    else:
        l_h, l_v, ok = mapper.third_of_batch(px, py)
        tcache["key"] = ckey
        tcache["value"] = (l_h, l_v, ok)
    t_th = _clock() - t_th0

    t_fwd0 = _clock()
    phi_x, phi_y = mapper.main_of_third_batch(l_h, l_v)
    t_fwd = _clock() - t_fwd0

    dx_raw = phi_x - px
    dy_raw = phi_y - py
    valid = ok & np.isfinite(dx_raw) & np.isfinite(dy_raw)
    if _SESSION["in_child_area"] is not None:
        valid = valid & _SESSION["in_child_area"]
    t_area = 0.0
    main_area_lut = _SESSION.get("main_area_lut")
    if main_area_lut is not None:
        t_area0 = _clock()
        safe_px = np.where(np.isfinite(phi_x), phi_x, 0.0)
        safe_py = np.where(np.isfinite(phi_y), phi_y, 0.0)
        valid = valid & _lookup_area_lut(np, main_area_lut, safe_px, safe_py)
        t_area = _clock() - t_area0

    dx, dy = _fill_invalid_from_neighbors(np, dx_raw, dy_raw, valid, passes=2)

    t_det0 = _clock()
    if am._FOLD["split"]:
        safe_phi_x = px + dx
        safe_phi_y = py + dy
        det = gridsolve.det2(
            safe_phi_x * float(mapper.fold_reference),
            safe_phi_y,
            _SESSION["du"],
            _SESSION["dv"],
        )
        is_front = det >= 0.0
    else:
        is_front = np.ones((_SESSION["W"], _SESSION["H"]), dtype=bool)
    t_det = _clock() - t_det0

    t_dep = 0.0
    if am._FOLD["split"] and _SESSION.pop("exact_depth", None):
        # Inheriting cannot see a crease sweep over the anchor: re-judge once
        # the hand has stopped.
        t_dep = _update_coarse_depth_table(am, np, _SESSION, mapper, l_h, l_v, valid, is_front)
        depth = _init_region_depths_from_coarse(np, _SESSION, valid, is_front)
    elif am._FOLD["split"]:
        depth, t_dep = _inherit_region_depths(
            am, np, _SESSION, mapper, l_h, l_v, valid, is_front
        )
    else:
        depth = np.zeros((_SESSION["W"], _SESSION["H"]), dtype=np.int32)

    t_pack0 = _clock()
    tag = np.where(valid, (1 + depth).astype(np.float32), np.float32(0.0))
    W = _SESSION["W"]
    H = _SESSION["H"]
    buf = np.zeros((H, W, 4), dtype="<f4")
    buf[:, :, 0] = dx.T
    buf[:, :, 1] = dy.T
    buf[:, :, 2] = tag.T
    data = buf.tobytes()

    ui = am._animean().ui
    ui.set_field(FIELD_KEY, W, H, data)

    passes = _build_passes(am, _SESSION, valid, depth)
    if passes != _SESSION["last_passes"]:
        _SESSION["last_passes"] = passes
        spec = {
            "field": FIELD_KEY,
            "rect": _SESSION["rect"],
            "max_edge": _SESSION["max_edge"],
            "source_view": "child",
            "columns": list(_SESSION["columns"]),
            "frame": int(_SESSION["frame"]),
            "width_scale": float(_SESSION["width_scale"]),
            "passes": list(passes),
            "hide_columns": list(_SESSION["hide_columns"]),
            "skip_properties": list(_SESSION["skip_properties"]),
        }
        ui.set_field_view("main", spec)
    t_pack = _clock() - t_pack0

    t_grid = _update_grid(am, np, mapper, ckey)

    t_end = _clock()
    total = t_end - t_start
    _SESSION["last_finish_time"] = t_end
    _LAST_TIMINGS.clear()
    _LAST_TIMINGS.update({
        "build_mapper": t_bm,
        "third_of_batch": t_th,
        "main_of_third_batch": t_fwd,
        "main_area": t_area,
        "det2": t_det,
        "depth_table": t_dep,
        "pack_and_upload": t_pack,
        "grid": t_grid,
        "total": total,
    })
    return True


def _on_timer(gen):
    am = _am()
    if (not _SESSION.get("active")
            or _SESSION.get("failed")
            or _SESSION.get("generation") != gen):
        return
    _SESSION["timer_armed"] = False
    if not _context_valid(am):
        _SESSION["active"] = False
        _SESSION["pending"] = False
        _clear_field_display(am)
        return
    if not _SESSION.get("pending"):
        return
    _SESSION["pending"] = False
    _SESSION["warm"]["settling"] = False
    try:
        if _compute_and_push():
            try:
                am._animean().ui.refresh()
            except Exception:
                pass
            _arm_settle(am)
    except Exception as error:
        _handle_preview_error(am, error)


def _arm_settle(am):
    """Frames during a drag take shortcuts: the solver budget, inherited layer
    order, walked nearest stations. Once the hand stops, one pass does it all
    properly, and solving goes on until it converges. A move takes the token, so
    this never competes with a moving hand."""
    warm = _SESSION.get("warm") or {}
    if not (warm.get("unsettled") or _SESSION.get("needs_final")):
        return
    if _SESSION.get("settle_passes", 0) >= SETTLE_PASSES:
        return
    token = _SESSION["settle_token"] = _SESSION.get("settle_token", 0) + 1
    gen = _SESSION["generation"]
    am._animean().ui.call_later(SETTLE_DELAY_MS, lambda: _on_settle(gen, token))


def _on_settle(gen, token):
    am = _am()
    if (not _SESSION.get("active")
            or _SESSION.get("failed")
            or _SESSION.get("generation") != gen
            or _SESSION.get("settle_token") != token):
        return
    if not _context_valid(am):
        _SESSION["active"] = False
        _clear_field_display(am)
        return
    _SESSION["settle_passes"] = _SESSION.get("settle_passes", 0) + 1
    _SESSION["warm"]["settling"] = True
    _SESSION["exact_depth"], _SESSION["needs_final"] = _SESSION.get("needs_final"), False
    try:
        if _compute_and_push():
            try:
                am._animean().ui.refresh()
            except Exception:
                pass
            _arm_settle(am)
    except Exception as error:
        _handle_preview_error(am, error)


def update():
    """Update the live preview on drag move (throttled with trailing call_later)."""
    if not _SESSION.get("active") or _SESSION.get("failed"):
        return
    am = _am()
    _SESSION["settle_passes"] = 0
    _SESSION["settle_token"] = _SESSION.get("settle_token", 0) + 1
    _SESSION["warm"]["settling"] = False
    _SESSION["needs_final"] = True
    try:
        if not _context_valid(am):
            _SESSION["active"] = False
            _SESSION["pending"] = False
            _SESSION["timer_armed"] = False
            _clear_field_display(am)
            return
        now = _clock()
        last_finish = _SESSION.get("last_finish_time")
        if last_finish is not None:
            # C++ already lets a drag move through once per 33 ms, so moves
            # cannot pile up; waiting a share of the last frame on top of that
            # only stretched every frame by half its cost.
            min_gap = MIN_UPDATE_INTERVAL
            elapsed = now - last_finish
            if elapsed < min_gap:
                _SESSION["pending"] = True
                if not _SESSION.get("timer_armed"):
                    _SESSION["timer_armed"] = True
                    delay_ms = max(1, int(math.ceil((min_gap - elapsed) * 1000.0)))
                    gen = _SESSION["generation"]
                    am._animean().ui.call_later(delay_ms, lambda: _on_timer(gen))
                return
        _SESSION["pending"] = False
        _compute_and_push()
        _arm_settle(am)
    except Exception as error:
        _handle_preview_error(am, error)


def end():
    """Clear the live preview on drag release (after _maybe_auto_run)."""
    am = _am()
    was_active = bool(_SESSION.get("active"))
    gen = _SESSION.get("generation", 0) + 1
    _drop_grid(am)
    _SESSION.clear()
    _SESSION.update({
        "active": False,
        "failed": False,
        "error_printed": False,
        "generation": gen,
        "snapshot": None,
    })
    if was_active:
        _clear_field_display(am)


def cancel(restore_assets=True):
    """Cancel the live preview and restore the press-time asset snapshot."""
    am = _am()
    was_active = bool(_SESSION.get("active"))
    snapshot = _SESSION.get("snapshot")
    gen = _SESSION.get("generation", 0) + 1
    if was_active and restore_assets and snapshot is not None:
        _restore_asset_snapshot(am, snapshot)
    _drop_grid(am)
    _SESSION.clear()
    _SESSION.update({
        "active": False,
        "failed": False,
        "error_printed": False,
        "generation": gen,
        "snapshot": None,
    })
    if was_active:
        _clear_field_display(am)
