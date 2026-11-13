"""Out-of-band inspection probe run inside AnimeAnBackend's embedded Python.

Loaded on demand via ``python.run`` with ``scriptDir`` set to this folder.
Structured results are written to a temporary IPC response file so stdout
stays a single quiet summary line and never floods ``window.pythonDebug``.
"""

from __future__ import annotations

from collections import deque
import json
import math
import os
import sys
import time
import traceback
from typing import Any


def _get_trace_state() -> dict[str, Any]:
    state = getattr(sys, "_animean_mcp_trace_state", None)
    if not isinstance(state, dict):
        state = {
            "active": False,
            "orig_dispatch": None,
            "wrapper": None,
            "max_events": 200,
            "events_filter": None,
            "coalesce_moves": True,
            "buffer": deque(maxlen=200),
            "seq": 0,
            "dropped_moves": 0,
            "total_dispatched": 0,
        }
        sys._animean_mcp_trace_state = state
    return state


def _get_views() -> dict[str, Any]:
    main_mod = sys.modules.get("__main__")
    views: dict[str, Any] = {}
    if main_mod is not None:
        for key, attr in (("main", "main_model"), ("child", "child_model")):
            wrapper = getattr(main_mod, attr, None)
            if wrapper is not None:
                views[key] = wrapper
    return views


def _scene_of(wrapper: Any) -> Any:
    return getattr(wrapper, "scene", wrapper)


def _selected_views(args: dict[str, Any]) -> list[str]:
    if "views" in args and isinstance(args["views"], list):
        return [str(v) for v in args["views"] if str(v) in ("main", "child")]
    view = args.get("view")
    if view in ("main", "child"):
        return [str(view)]
    return ["main", "child"]


def _field_display_supported(view: str) -> bool | None:
    try:
        import animean_python
        ui = getattr(animean_python, "ui", None)
        fn = getattr(ui, "field_display_supported", None)
        if callable(fn):
            return bool(fn(view))
    except Exception:
        pass
    return None


def _overlay_summary(view: str) -> dict[str, Any]:
    overlay_mod = sys.modules.get("overlay_stack")
    if overlay_mod is None:
        return {}
    layers = getattr(overlay_mod, "_LAYERS", {})
    by_owner = layers.get(view, {}) if isinstance(layers, dict) else {}
    summary: dict[str, Any] = {}
    for owner, items in by_owner.items():
        if not isinstance(items, (list, tuple)):
            summary[str(owner)] = {"count": 0, "kinds": []}
            continue
        kinds: list[str] = []
        for item in items:
            if isinstance(item, dict):
                kind = str(item.get("kind") or item.get("type") or "item")
            else:
                kind = type(item).__name__
            if kind not in kinds:
                kinds.append(kind)
        summary[str(owner)] = {"count": len(items), "kinds": kinds}
    return summary


def _registered_hooks_summary() -> list[dict[str, Any]]:
    hooks_mod = sys.modules.get("python_hooks")
    if hooks_mod is None:
        return []
    raw_hooks = getattr(hooks_mod, "_HOOKS", [])
    out: list[dict[str, Any]] = []
    for entry in raw_hooks:
        if not isinstance(entry, dict):
            continue
        fn = entry.get("function")
        fn_name = (
            f"{getattr(fn, '__module__', '?')}.{getattr(fn, '__qualname__', getattr(fn, '__name__', repr(fn)))}"
            if fn is not None
            else None
        )
        out.append(
            {
                "function": fn_name,
                "events": sorted(str(e) for e in entry.get("events", ())),
                "tool": entry.get("tool"),
                "property": entry.get("property"),
                "order": entry.get("order"),
            }
        )
    return out


def _summarize_value(value: Any, *, max_depth: int = 6, full_arrays: bool = False, _depth: int = 0) -> Any:
    if value is None or isinstance(value, (bool, int, str)):
        return value
    if isinstance(value, float):
        if math.isnan(value) or math.isinf(value):
            return str(value)
        return value
    if _depth >= max_depth:
        if isinstance(value, dict):
            return {"__truncated__": "dict", "keys": list(value.keys())[:20], "length": len(value)}
        if isinstance(value, (list, tuple)):
            return {"__truncated__": "list", "length": len(value)}
        return repr(value)
    if isinstance(value, dict):
        return {
            str(k): _summarize_value(v, max_depth=max_depth, full_arrays=full_arrays, _depth=_depth + 1)
            for k, v in value.items()
        }
    if isinstance(value, (list, tuple)):
        if (
            not full_arrays
            and len(value) > 64
            and all(isinstance(item, (int, float)) and not isinstance(item, bool) for item in value[:64])
        ):
            numeric = [float(item) for item in value if isinstance(item, (int, float)) and not isinstance(item, bool)]
            return {
                "__summary__": "numeric_array",
                "length": len(value),
                "sampleHead": list(value[:8]),
                "min": min(numeric) if numeric else None,
                "max": max(numeric) if numeric else None,
            }
        if not full_arrays and len(value) > 120:
            return {
                "__summary__": "list",
                "length": len(value),
                "sampleHead": [
                    _summarize_value(item, max_depth=max_depth, full_arrays=full_arrays, _depth=_depth + 1)
                    for item in value[:12]
                ],
            }
        return [
            _summarize_value(item, max_depth=max_depth, full_arrays=full_arrays, _depth=_depth + 1)
            for item in value
        ]
    return str(value)


def _read_script_data_dict(scene: Any) -> dict[str, Any]:
    script_store = sys.modules.get("script_store")
    if script_store is not None and hasattr(script_store, "read_all"):
        data = script_store.read_all(scene)
        return data if isinstance(data, dict) else {}
    try:
        raw = scene.script_data()
        data = json.loads(raw) if raw else {}
        return data if isinstance(data, dict) else {}
    except Exception:
        return {}


def _stroke_endpoints(stroke: dict[str, Any]) -> tuple[dict[str, float] | None, dict[str, float] | None, bool, int]:
    commands = stroke.get("commands")
    if isinstance(commands, list) and commands:
        start_pt = None
        end_pt = None
        for cmd in commands:
            if not isinstance(cmd, dict):
                continue
            pt_to = cmd.get("to")
            pt_from = cmd.get("from")
            if start_pt is None:
                start_pt = pt_from if isinstance(pt_from, dict) else (pt_to if isinstance(pt_to, dict) else None)
            if isinstance(pt_to, dict):
                end_pt = pt_to
        is_closed = False
        if isinstance(start_pt, dict) and isinstance(end_pt, dict):
            dx = float(start_pt.get("x", 0.0)) - float(end_pt.get("x", 0.0))
            dy = float(start_pt.get("y", 0.0)) - float(end_pt.get("y", 0.0))
            is_closed = (dx * dx + dy * dy) <= 1e-4
        return start_pt, end_pt, is_closed, len(commands)
    polylines = stroke.get("polylines")
    if isinstance(polylines, list) and polylines:
        first_poly = polylines[0] if isinstance(polylines[0], list) else []
        last_poly = polylines[-1] if isinstance(polylines[-1], list) else []
        start_pt = first_poly[0] if first_poly else None
        end_pt = last_poly[-1] if last_poly else None
        is_closed = False
        if isinstance(start_pt, dict) and isinstance(end_pt, dict):
            dx = float(start_pt.get("x", 0.0)) - float(end_pt.get("x", 0.0))
            dy = float(start_pt.get("y", 0.0)) - float(end_pt.get("y", 0.0))
            is_closed = (dx * dx + dy * dy) <= 1e-4
        return start_pt, end_pt, is_closed, sum(len(p) for p in polylines if isinstance(p, list))
    return None, None, False, 0


def _op_inspect_scene(args: dict[str, Any]) -> dict[str, Any]:
    views = _get_views()
    wanted = _selected_views(args)
    include_empty_cells = bool(args.get("includeEmptyCells", False))
    result_views: dict[str, Any] = {}

    for view_name in wanted:
        wrapper = views.get(view_name)
        if wrapper is None:
            result_views[view_name] = {"available": False}
            continue
        scene = _scene_of(wrapper)
        structure = scene.get_structure()
        non_empty_cells: list[dict[str, Any]] = []
        for layer_info in structure.get("layers", []):
            layer_idx = layer_info.get("index")
            layer_name = layer_info.get("name", "")
            for cell_info in layer_info.get("cells", []):
                if cell_info.get("empty", True) and not include_empty_cells:
                    continue
                asset_type = str(cell_info.get("asset_type") or "vector")
                img = scene.image_at(int(cell_info["frame_index"]), int(layer_idx), False, asset_type)
                has_raster = bool(img.has_raster()) if img is not None else False
                if (
                    not include_empty_cells
                    and cell_info.get("stroke_count", 0) == 0
                    and cell_info.get("fill_count", 0) == 0
                    and not has_raster
                ):
                    continue
                non_empty_cells.append(
                    {
                        "frame": cell_info.get("frame_index"),
                        "layer": layer_idx,
                        "layerName": layer_name,
                        "assetIndex": cell_info.get("asset_index"),
                        "assetName": cell_info.get("asset_name"),
                        "assetType": cell_info.get("asset_type"),
                        "frameId": cell_info.get("frame_id"),
                        "strokeCount": cell_info.get("stroke_count", 0),
                        "fillCount": cell_info.get("fill_count", 0),
                        "hasRaster": has_raster,
                        "bounds": cell_info.get("bounds", {}),
                    }
                )

        script_data = _read_script_data_dict(scene)
        script_summary: dict[str, Any] = {}
        for k, v in script_data.items():
            try:
                encoded = json.dumps(v, ensure_ascii=False, separators=(",", ":"))
                byte_len = len(encoded.encode("utf-8"))
            except Exception:
                byte_len = None
            script_summary[str(k)] = {
                "type": type(v).__name__,
                "bytes": byte_len,
                "keys": list(v.keys())[:25] if isinstance(v, dict) else None,
                "length": len(v) if isinstance(v, (list, dict, str)) else None,
            }

        cw, ch = scene.canvas_size()
        result_views[view_name] = {
            "available": True,
            "canvasSize": {"width": cw, "height": ch},
            "frameCount": scene.frame_count(),
            "layerCount": scene.layer_count(),
            "assetCount": scene.asset_count(),
            "currentFrame": scene.current_frame(),
            "currentLayer": scene.current_layer(),
            "currentAsset": scene.current_asset(),
            "structure": {
                "frames": structure.get("frames", []),
                "layers": [
                    {k: v for k, v in layer.items() if k != "cells"}
                    for layer in structure.get("layers", [])
                ],
                "assets": structure.get("assets", []),
            },
            "layerTree": scene.layer_tree(),
            "nonEmptyCells": non_empty_cells,
            "scriptDataSummary": script_summary,
            "overlays": _overlay_summary(view_name),
            "fieldDisplaySupported": _field_display_supported(view_name),
        }

    trace_state = _get_trace_state()
    main_mod = sys.modules.get("__main__")
    return {
        "activeView": getattr(main_mod, "active_view", None) if main_mod is not None else None,
        "views": result_views,
        "registeredHooks": _registered_hooks_summary(),
        "hookTrace": {
            "active": bool(trace_state["active"]),
            "buffered": len(trace_state["buffer"]),
            "seq": int(trace_state["seq"]),
        },
    }


def _op_inspect_cell_geometry(args: dict[str, Any]) -> dict[str, Any]:
    views = _get_views()
    view_name = str(args.get("view") or "main")
    wrapper = views.get(view_name)
    if wrapper is None:
        raise ValueError(f"Unknown or unavailable view: {view_name}")

    scene = _scene_of(wrapper)
    frame = int(scene.current_frame()) if args.get("frame") is None else int(args["frame"])
    layer = int(scene.current_layer()) if args.get("layer") is None else int(args["layer"])
    if frame < 0 or frame >= scene.frame_count():
        raise ValueError(f"Frame index out of range: {frame} (frameCount={scene.frame_count()})")
    if layer < 0 or layer >= scene.layer_count():
        raise ValueError(f"Layer index out of range: {layer} (layerCount={scene.layer_count()})")
    include_commands = bool(args.get("includeCommands", True))
    to_poly = bool(args.get("toPoly", False))
    poly_step = float(args.get("polyStep", 4.0))
    property_filter = args.get("propertyFilter")
    stroke_indices = args.get("strokeIndices")
    if isinstance(stroke_indices, list):
        wanted_indices = {int(i) for i in stroke_indices}
    else:
        wanted_indices = None
    max_strokes = max(1, int(args.get("maxStrokes", 100)))

    cell = scene.cell_to_dict(layer, frame, to_poly, poly_step)
    image = cell.get("image", {})
    raw_strokes = image.get("strokes", [])
    raw_fills = image.get("fills", [])

    strokes_out: list[dict[str, Any]] = []
    matched_count = 0
    truncated = False
    for idx, stroke in enumerate(raw_strokes):
        if not isinstance(stroke, dict):
            continue
        prop = stroke.get("property", "")
        if property_filter is not None and prop != str(property_filter):
            continue
        if wanted_indices is not None and idx not in wanted_indices:
            continue
        matched_count += 1
        if len(strokes_out) >= max_strokes:
            truncated = True
            continue
        start_pt, end_pt, is_closed, seg_count = _stroke_endpoints(stroke)
        entry: dict[str, Any] = {
            "index": idx,
            "id": stroke.get("id"),
            "property": prop,
            "color": stroke.get("color"),
            "width": stroke.get("width"),
            "penStyle": stroke.get("pen_style"),
            "bounds": stroke.get("bounds"),
            "totalLength": stroke.get("total_length"),
            "groupId": stroke.get("group_id"),
            "isPoint": stroke.get("is_point"),
            "selected": stroke.get("selected"),
            "segmentCount": seg_count,
            "startPoint": start_pt,
            "endPoint": end_pt,
            "isClosed": is_closed,
        }
        if include_commands:
            if to_poly:
                entry["polylines"] = stroke.get("polylines", [])
            else:
                entry["commands"] = stroke.get("commands", [])
            if args.get("includeRawPoints", False):
                entry["rawPoints"] = stroke.get("raw_points", [])
        strokes_out.append(entry)

    fills_out: list[dict[str, Any]] = []
    for idx, fill in enumerate(raw_fills):
        if not isinstance(fill, dict):
            continue
        fill_entry = {
            "index": idx,
            "id": fill.get("id"),
            "property": fill.get("property", ""),
            "seed": fill.get("seed"),
            "color": fill.get("color"),
            "bounds": fill.get("bounds"),
            "sourceLayerIndex": fill.get("source_layer_index"),
            "basedOnAllLayers": fill.get("based_on_all_layers"),
            "commandCount": len(fill.get("commands", [])),
        }
        if bool(args.get("includeFillCommands", False)):
            fill_entry["commands"] = fill.get("commands", [])
        fills_out.append(fill_entry)

    return {
        "view": view_name,
        "frame": frame,
        "layer": layer,
        "layerName": scene.layer_name(layer) if 0 <= layer < scene.layer_count() else "",
        "assetIndex": cell.get("asset_index"),
        "frameId": cell.get("frame_id"),
        "empty": bool(cell.get("empty", True)),
        "totalStrokeCount": int(image.get("stroke_count", len(raw_strokes))),
        "matchedStrokeCount": matched_count,
        "returnedStrokeCount": len(strokes_out),
        "truncated": truncated,
        "fillCount": int(image.get("fill_count", len(raw_fills))),
        "hasRaster": bool(image.get("has_raster", False)),
        "raster": image.get("raster", {}),
        "bounds": image.get("bounds", {}),
        "strokes": strokes_out,
        "fills": fills_out,
        "overlays": _overlay_summary(view_name),
    }


def _collect_guide_strokes(scene: Any, guide_properties: set[str], frame_override: int | None) -> list[dict[str, Any]]:
    frame = int(scene.current_frame()) if frame_override is None else int(frame_override)
    if frame < 0 or frame >= scene.frame_count():
        return []
    out: list[dict[str, Any]] = []
    for layer_idx in range(scene.layer_count()):
        cell = scene.cell_to_dict(layer_idx, frame, False, 4.0)
        strokes = cell.get("image", {}).get("strokes", [])
        for stroke_idx, stroke in enumerate(strokes):
            prop = str(stroke.get("property", ""))
            if not prop:
                continue
            start_pt, end_pt, is_closed, seg_count = _stroke_endpoints(stroke)
            out.append(
                {
                    "frame": frame,
                    "layer": layer_idx,
                    "layerName": scene.layer_name(layer_idx),
                    "strokeIndex": stroke_idx,
                    "id": stroke.get("id"),
                    "property": prop,
                    "isOnionGuide": prop in guide_properties,
                    "bounds": stroke.get("bounds"),
                    "totalLength": stroke.get("total_length"),
                    "segmentCount": seg_count,
                    "startPoint": start_pt,
                    "endPoint": end_pt,
                    "isClosed": is_closed,
                }
            )
    return out


def _op_inspect_automapping(args: dict[str, Any]) -> dict[str, Any]:
    views = _get_views()
    wanted_keys = set(str(k) for k in args["keys"]) if isinstance(args.get("keys"), list) else None
    max_depth = int(args.get("maxDepth", 6))
    full_arrays = bool(args.get("fullArrays", False))
    frame_override = None if args.get("frame") is None else int(args["frame"])

    hooks_mod = sys.modules.get("python_hooks")
    guide_props = set(getattr(hooks_mod, "_ONION_GUIDE_PROPERTIES", []) or []) if hooks_mod else set()
    am_layer_tag = getattr(hooks_mod, "_ONION_LAYER_TAG", "") if hooks_mod else ""
    am_mod = sys.modules.get("auto_mapping")

    views_out: dict[str, Any] = {}
    for view_name in _selected_views(args):
        wrapper = views.get(view_name)
        if wrapper is None:
            views_out[view_name] = {"available": False}
            continue
        scene = _scene_of(wrapper)
        raw_script_data = _read_script_data_dict(scene)
        filtered_script = {
            k: _summarize_value(v, max_depth=max_depth, full_arrays=full_arrays)
            for k, v in raw_script_data.items()
            if wanted_keys is None or str(k) in wanted_keys
        }
        views_out[view_name] = {
            "available": True,
            "currentFrame": scene.current_frame(),
            "currentLayer": scene.current_layer(),
            "scriptData": filtered_script,
            "guideStrokes": _collect_guide_strokes(scene, guide_props, frame_override),
            "overlays": _overlay_summary(view_name),
            "fieldDisplaySupported": _field_display_supported(view_name),
        }

    am_constants = {}
    if am_mod is not None:
        for attr in (
            "H_PROPERTY",
            "V_PROPERTY",
            "MAPPING_AREA_PROPERTY",
            "ADDITIONAL_PROPERTY",
            "PROXY_PROPERTY",
            "CHOICE_PROPERTY",
            "DEFORM_PROPERTY",
            "AUTO_MAPPING2_TOOL",
            "UNIT_STORE_KEY",
        ):
            if hasattr(am_mod, attr):
                am_constants[attr] = getattr(am_mod, attr)

    return {
        "onionGuideProperties": sorted(guide_props),
        "onionLayerTag": am_layer_tag,
        "autoMappingConstants": am_constants,
        "views": views_out,
    }


def _summarize_stroke_for_trace(stroke: Any) -> dict[str, Any] | None:
    if not isinstance(stroke, dict) or not stroke:
        return None
    out: dict[str, Any] = {}
    for key in ("index", "id", "property", "width", "color", "bounds"):
        if key in stroke:
            out[key] = stroke[key]
    points = stroke.get("points") or stroke.get("raw_points")
    if isinstance(points, list):
        out["pointCount"] = len(points)
    return out or None


def _summarize_hook_record(
    seq: int,
    message: dict[str, Any],
    duration_ms: float,
    handled_before: bool,
    matched_count: int,
    error_text: str | None,
) -> dict[str, Any]:
    record: dict[str, Any] = {
        "seq": seq,
        "ts": round(time.time(), 4),
        "durationMs": round(duration_ms, 3),
        "event": message.get("event"),
        "view": message.get("view"),
        "tool": message.get("tool"),
        "baseTool": message.get("base_tool"),
        "property": message.get("property"),
        "cell": message.get("cell"),
        "handledBefore": handled_before,
        "handledAfter": bool(message.get("handled", False)),
        "cancelHistory": bool(message.get("cancel_history", False)),
        "matchedHooks": matched_count,
    }
    for opt_key in ("action", "phase", "id", "name", "button", "buttons", "modifiers", "position", "delta"):
        if opt_key in message and message[opt_key] is not None:
            record[opt_key] = message[opt_key]
    if "option" in message and isinstance(message["option"], dict):
        record["option"] = message["option"]
    stroke_summary = _summarize_stroke_for_trace(message.get("stroke"))
    if stroke_summary is not None:
        record["strokeSummary"] = stroke_summary
    if error_text:
        record["error"] = error_text
    return record


def _is_coalesceable_move(record: dict[str, Any]) -> bool:
    if record.get("error"):
        return False
    event = record.get("event")
    if event == "handle" and record.get("action") in ("move", "hover"):
        return True
    if event == "pad" and record.get("phase") == "move":
        return True
    return False


def _trace_status(state: dict[str, Any]) -> dict[str, Any]:
    buf = state["buffer"]
    return {
        "active": bool(state["active"]),
        "maxEvents": int(state["max_events"]),
        "eventsFilter": state["events_filter"],
        "coalesceMoves": bool(state["coalesce_moves"]),
        "bufferedCount": len(buf),
        "seq": int(state["seq"]),
        "totalDispatched": int(state["total_dispatched"]),
        "coalescedMoves": int(state["dropped_moves"]),
    }


def _op_trace_hooks(args: dict[str, Any]) -> dict[str, Any]:
    import python_hooks

    state = _get_trace_state()
    action = str(args.get("action") or "status").lower()

    if action == "start":
        max_events = max(10, min(2000, int(args.get("maxEvents", 200))))
        events_filter = args.get("eventsFilter")
        if isinstance(events_filter, list) and events_filter:
            state["events_filter"] = sorted(str(e) for e in events_filter)
        else:
            state["events_filter"] = None
        state["coalesce_moves"] = bool(args.get("coalesceMoves", True))
        if max_events != state["max_events"]:
            state["max_events"] = max_events
            state["buffer"] = deque(list(state["buffer"])[-max_events:], maxlen=max_events)

        if not state["active"]:
            orig = python_hooks.dispatch
            if getattr(orig, "_animean_mcp_wrapper", False) and state["orig_dispatch"] is not None:
                orig = state["orig_dispatch"]
            state["orig_dispatch"] = orig

            def _traced_dispatch(message: dict[str, Any]) -> int:
                trace_state = _get_trace_state()
                trace_state["total_dispatched"] += 1
                ev_name = message.get("event") if isinstance(message, dict) else None
                ev_filter = trace_state.get("events_filter")
                if ev_filter and ev_name not in ev_filter:
                    return trace_state["orig_dispatch"](message)

                handled_before = bool(message.get("handled", False)) if isinstance(message, dict) else False
                t0 = time.perf_counter()
                matched = 0
                err_text = None
                try:
                    matched = trace_state["orig_dispatch"](message)
                    return matched
                except Exception:
                    err_text = traceback.format_exc()
                    raise
                finally:
                    duration_ms = (time.perf_counter() - t0) * 1000.0
                    if isinstance(message, dict):
                        trace_state["seq"] += 1
                        seq = trace_state["seq"]
                        record = _summarize_hook_record(
                            seq, message, duration_ms, handled_before, matched, err_text
                        )
                        buf: deque = trace_state["buffer"]
                        if (
                            trace_state["coalesce_moves"]
                            and buf
                            and _is_coalesceable_move(record)
                            and _is_coalesceable_move(buf[-1])
                            and buf[-1].get("event") == record.get("event")
                            and buf[-1].get("action") == record.get("action")
                            and buf[-1].get("view") == record.get("view")
                            and buf[-1].get("tool") == record.get("tool")
                        ):
                            coalesced_from = buf[-1].get("coalescedCount", 1) + 1
                            record["coalescedCount"] = coalesced_from
                            buf[-1] = record
                            trace_state["dropped_moves"] += 1
                        else:
                            buf.append(record)

            _traced_dispatch._animean_mcp_wrapper = True  # type: ignore[attr-defined]
            state["wrapper"] = _traced_dispatch
            python_hooks.dispatch = _traced_dispatch
            state["active"] = True

        return _trace_status(state)

    if action == "read":
        since_seq = int(args.get("sinceSeq", 0))
        limit = max(1, min(1000, int(args.get("limit", 100))))
        events = [item for item in state["buffer"] if int(item.get("seq", 0)) > since_seq]
        if len(events) > limit:
            events = events[-limit:]
        return {
            **_trace_status(state),
            "events": events,
        }

    if action == "clear":
        state["buffer"].clear()
        state["dropped_moves"] = 0
        return _trace_status(state)

    if action == "stop":
        if state["active"] and state["orig_dispatch"] is not None:
            if getattr(python_hooks.dispatch, "_animean_mcp_wrapper", False):
                python_hooks.dispatch = state["orig_dispatch"]
        state["active"] = False
        return _trace_status(state)

    if action == "status":
        return _trace_status(state)

    raise ValueError(f"Unknown trace_hooks action: {action}")


def _op_export_repro_data(args: dict[str, Any]) -> dict[str, Any]:
    views = _get_views()
    include_commands = bool(args.get("includeCommands", True))
    to_poly = bool(args.get("toPoly", False))
    views_dump: dict[str, Any] = {}

    for view_name, wrapper in views.items():
        scene = _scene_of(wrapper)
        structure = scene.get_structure()
        cells_dump: list[dict[str, Any]] = []
        for layer_info in structure.get("layers", []):
            layer_idx = int(layer_info["index"])
            for cell_info in layer_info.get("cells", []):
                if cell_info.get("empty", True):
                    continue
                frame_idx = int(cell_info["frame_index"])
                cell_dict = scene.cell_to_dict(layer_idx, frame_idx, to_poly, 4.0)
                if not include_commands and isinstance(cell_dict.get("image"), dict):
                    strokes = cell_dict["image"].get("strokes", [])
                    for s in strokes:
                        if isinstance(s, dict):
                            s.pop("commands", None)
                            s.pop("raw_points", None)
                cells_dump.append(cell_dict)

        cw, ch = scene.canvas_size()
        views_dump[view_name] = {
            "canvasSize": {"width": cw, "height": ch},
            "structure": structure,
            "layerTree": scene.layer_tree(),
            "scriptData": _read_script_data_dict(scene),
            "nonEmptyCells": cells_dump,
            "overlays": _overlay_summary(view_name),
            "fieldDisplaySupported": _field_display_supported(view_name),
        }

    trace_state = _get_trace_state()
    return {
        "capturedAt": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "views": views_dump,
        "registeredHooks": _registered_hooks_summary(),
        "hookTrace": {
            **_trace_status(trace_state),
            "events": list(trace_state["buffer"]),
        },
    }


_OPS = {
    "inspect_scene": _op_inspect_scene,
    "inspect_cell_geometry": _op_inspect_cell_geometry,
    "inspect_automapping": _op_inspect_automapping,
    "trace_hooks": _op_trace_hooks,
    "export_repro_data": _op_export_repro_data,
}


def run_ipc(req_path: str, resp_path: str) -> None:
    """Execute one probe request file and write JSON response atomically."""
    op = "<unknown>"
    payload: dict[str, Any]
    try:
        with open(req_path, "r", encoding="utf-8") as fh:
            req = json.load(fh)
        op = str(req.get("op") or "")
        args = req.get("args") or {}
        if op not in _OPS:
            raise ValueError(f"Unknown probe op: {op}")
        data = _OPS[op](args)
        payload = {"ok": True, "op": op, "data": data}
        summary = f"[mcp_probe] ok:{op}"
    except Exception as exc:
        payload = {
            "ok": False,
            "op": op,
            "error": str(exc),
            "traceback": traceback.format_exc(),
        }
        summary = f"[mcp_probe] err:{op}"

    tmp_path = f"{resp_path}.tmp"
    os.makedirs(os.path.dirname(os.path.abspath(resp_path)), exist_ok=True)
    with open(tmp_path, "w", encoding="utf-8") as fh:
        json.dump(payload, fh, ensure_ascii=False, separators=(",", ":"))
    os.replace(tmp_path, resp_path)
    print(summary)
