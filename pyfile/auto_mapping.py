"""Auto-mapping tools: H/V center lines, mapping area, and child->main mapping.

Mapping assets (the H/V center lines and the mapping area) do NOT live in the
layer stack. They are captured into a Python dict (_MAPPING_ASSETS) the moment
they are drawn, removed from the scene model, and displayed through the
generic C++ overlay service (animean_python.ui.set_overlay). They never show
up in the Layers/Assets panels and are not part of the saved project.

Workflow:
1. In child_paint_view draw one "H Center Line" (blue) and one "V Center Line"
   (green). They define the pattern's UV frame. Redrawing replaces the old one.
2. Draw the pattern in child_paint_view with the normal tools.
3. In main_paint_view draw one H and one V center line to place the frame.
4. Optional: with "Mapping Area" click inside a closed shape (bucket-style
   region detection, computed here in Python via vectorlogic). An area in
   main_paint_view clips the mapped result; an area in child_paint_view
   selects which part of the pattern is used. Light blue, one per view.
5. Click "Auto Mapping": child pattern strokes are converted to UV through
   the child guides and re-created in main_paint_view through the main
   guides (arc-length evaluation, so curved guides bend the pattern).
   The result always goes into its own FRESH layer ("mapped layer",
   "mapped layer1", ...) - never into whatever layer is currently selected.
   Every click creates a new layer and previous results are left untouched,
   so different runs stack and can be compared, hidden or deleted
   individually. Each mapping asset shows an "x" badge on the canvas -
   click it to delete the item and redraw.
   The "Curve Mode" option controls the output geometry of each mapped
   stroke: because the warp is non-linear, all modes sample between the
   original vertices so the result follows the distortion. "spline" (default)
   and "bezier" fit real curves; "polyline" emits the sampled points joined
   by straight segments. See CURVE_MODES / _CURVE_MODE.

Architecture note: C++ only provides generic services (overlay display list,
"overlayremove" hook event, set_draw_color, geometry bindings, and building a
stroke from a curved path via make_stroke_object_from_path). All tool
semantics - property names, colors, the asset dict, region detection,
clipping, curve fitting - live in this file.
"""

import bisect
import heapq
import json
import math
import os
import re
import tempfile
import time

import bezier
import python_hooks
import overlay_stack
import script_store

H_PROPERTY = "h_center_line"
V_PROPERTY = "v_center_line"
GUIDE_PROPERTIES = (H_PROPERTY, V_PROPERTY)
MAPPED_PROPERTY = "auto_mapped"
MAPPED_LAYER_NAME = "mapped layer"
# Dual-layer fold output. Where the map is orientation-reversing (spec (5.1)
# det J < 0) the pattern is mirrored - physically, we are looking at the BACK
# of a fold. Rather than fight that (AM1 tried, and folded at the curvature
# radius for its trouble), the run is split at the fold boundary and the two
# sides go to their own layers, so the back can carry a lining colour and be
# hidden independently.
BACK_PROPERTY = "auto_mapped_back"
SEAL_PROPERTY = "auto_mapped_seal"
BACK_LAYER_NAME = "mapped layer back"
SEAL_LAYER_NAME = "mapped layer crease"
# Every run packs its output into one nested layer group: the two guide axes
# in a collapsed "H/V" subgroup, then the front / back / crease layers.
GUIDE_GROUP_NAME = "H/V"
H_LAYER_NAME = "H axis"
V_LAYER_NAME = "V axis"
# The axis snapshot layers carry WHICH axis they are in the stroke property,
# so restoring them never has to guess from a layer name the user may have
# renamed. GUIDE_LAYER_PROPERTY is the undifferentiated name the first version
# wrote; it is still recognised on load, falling back to the layer name.
GUIDE_LAYER_PROPERTY = "auto_mapped_guide"
H_GUIDE_LAYER_PROPERTY = "auto_mapped_guide_h"
V_GUIDE_LAYER_PROPERTY = "auto_mapped_guide_v"
# The nearest-end anchor's snapshot: recorded next to the two axes so a run's
# provenance is complete - re-expanding restores the stacking too.
NEAREST_LAYER_PROPERTY = "auto_mapped_guide_nearest"
NEAREST_LAYER_NAME = "Nearest Point"
MAPPING_GROUP_NAME = "Auto Mapping"
# Everything a run puts on the board. None of it may act as a wall for region
# detection, and none of it may be picked up as pattern by the next run.
# The Fukusato workflow routes its region detection through _detect_region
# too, so its output properties (literals: importing fukusato_mapping here
# would be a cycle) must be excluded for exactly the same reason - otherwise
# the first accepted mapping walls off every later re-detection.
MAPPING_OUTPUT_PROPERTIES = (MAPPED_PROPERTY, BACK_PROPERTY, SEAL_PROPERTY,
                             GUIDE_LAYER_PROPERTY, H_GUIDE_LAYER_PROPERTY,
                             V_GUIDE_LAYER_PROPERTY, NEAREST_LAYER_PROPERTY,
                             "fukusato_mapped", "fukusato_mapped_back")
_FOLD = {"split": True, "seal": True, "back_color": (104, 112, 140, 255),
         # Qt::PenStyle for the crease strokes: 2 = DashLine. The crease is an
         # annotation of the fold, and a dashed line reads as annotation where
         # a solid one reads as artwork.
         "seal_pen_style": 2}


def fold_split_enabled():
    return _FOLD["split"]


def fold_seal_enabled():
    return _FOLD["seal"]


def fold_back_color():
    return _FOLD["back_color"]


# Bezier Bridge (补全拓扑, user request 2026-08-24): when the checkbox is on,
# each severed gap between two consecutive UV islands of one source stroke is
# spanned by a cubic Bezier BUILT IN THIRD SPACE from the captured coordinates
# and trends -
#     P0 = A,  P1 = A + k*v_A,  P2 = B - k*v_B,  P3 = B
# where A/B are the Third lifts of the two cut points, v_A/v_B the islands'
# Third-space travel directions at those cuts (the departure trend extended,
# the arrival trend back-cast), and k = tension * |AB| (the slider; the
# classic smooth-join default is 1/3). The bridge then rides the ordinary
# Step 4 projection (main_of_third, forward-only) into the main view. It
# deliberately does NOT exist in child space - the gap is exactly the ground
# with no child coordinate - so it bypasses the child-space samplers.
_BRIDGE = {"enabled": False, "tension": 0.33}


def bridge_enabled():
    return _BRIDGE["enabled"]


def bridge_tension():
    return _BRIDGE["tension"]


# The one and only automapping button. Its internal id keeps the historical
# "_2" suffix ("Auto Mapping 2", Coons interpolation) so nothing stored in
# old sessions changes meaning; the retired spine-rotation algorithm lives in
# old_history/auto_mapping_1.py.
AUTO_MAPPING2_TOOL = "auto_mapping_2"
MAPPING_AREA_PROPERTY = "mapping_area"
POLY_STEP = 4.0

# The "nearest end" of the folded sheet: a red draggable handle on the main
# board that decides the STACKING of fold layers. Depth of any point = number
# of creases crossed between it and this anchor (in arc space); depth 0 paints
# on top. Stored in the main view's assets as arc coordinates so it rides the
# guides, defaulting to the crossing.
NEAREST_PROPERTY = "fold_nearest"
NEAREST_HANDLE_COLOR = (230, 45, 45, 255)

# "Additional line": a pink refinement guide drawn on either board on top of
# the H/V axes. Each line exists as a PAIR (child version, main version) - the
# side the user did not draw on is synthesized through the current mapping -
# and the pair guides the texture's FLOW: all pairs blend into one target
# gradient field that a Poisson solve integrates (see _FlowFieldWarp).
# Falloff shape and reach are tool options.
ADDITIONAL_PROPERTY = "additional_line"
ADDITIONAL_COLOR = (255, 105, 180, 255)
ADDITIONAL_FALLOFFS = ("linear", "quadratic")
# constrain_outline (menu checkbox 约束外轮廓, default ON = the original
# design): the MainView grid's outer contour - the outline the H/V axes
# form - is pinned and does not deform under interior additional lines.
# A side an additional line is drawn OUT across is RELEASED and follows
# the flow. Unchecked, the contour deforms freely (the nearest line
# shapes the edge).
_ADDITIONAL = {"falloff": "linear", "radius_factor": 0.5,
               "constrain_outline": True}


def additional_falloff():
    return _ADDITIONAL["falloff"]

GRID_COLOR = (255, 140, 0, 170)

# How the mapped strokes' geometry is reconstructed after the warp. The warp
# (build_mapper.map_point) is non-linear, so the straight line between two
# mapped ORIGINAL vertices is NOT the image of the source segment - it is only
# its chord. All three modes therefore share the same anchored sampling: the
# original vertices are anchors, samples are inserted between them until the
# warp is well captured, everything is mapped, and only the inserted samples
# are decimated again (RDP). They differ in the output geometry:
#   "polyline" - the decimated points joined by straight segments (no fitting).
#   "spline"   - centripetal Catmull-Rom interpolated through the points
#                (route 1: migrate spline knots).
#   "bezier"   - no polyline sampling at all: the artist's own Bezier segments
#                are kept and each control handle is transported through the
#                warp's local directional derivative, splitting adaptively
#                where that is not accurate enough (route 2: migrate handles).
CURVE_MODES = ("polyline", "spline", "bezier")
# Bezier is the default: it carries the stroke's OWN curve segments through the
# warp instead of resampling it into points and fitting a new curve through
# them, so the output keeps the geometry the artist drew rather than an
# approximation of it.
DEFAULT_CURVE_MODE = "bezier"
_CURVE_MODE = {"value": DEFAULT_CURVE_MODE}

# Curve-fitting tolerances, all in canvas (main-view) pixels.
_CURVE_TOL = 0.4        # max chord/handle deviation before subdividing further
_SPLINE_MAX_DEPTH = 8   # source-segment bisections in spline densification
_BEZIER_MAX_DEPTH = 6   # source-cubic bisections in bezier handle transport
_JAC_EPS = 0.5          # directional-derivative step, in child (source) pixels
_CATMULL_ALPHA = 0.5    # centripetal parametrization (no overshoot on uneven knots)
# Anti-aliasing guards: a straight source segment spanning whole periods of a
# wavy main guide can pass a small fixed set of probe points even though its
# true image oscillates (e.g. a full sine period has zero midpoint deviation).
_FORCE_STEP = 16.0      # output px: always sample at least this dense
_MAX_KNOTS_PER_SPAN = 48  # cap on structural knots forced into one source span
_PROBE_T = 0.381966     # golden-section probe; never rational vs the midpoint

# Topology severing (user request 2026-08-24). The map is the pure staged
# composition Child -> Third -> Main with NO residual term, so a child point
# is only mappable where the Newton lift into Third actually exists. Where the
# child frame folds over (the Jacobian determinant changes sign against the
# crossing's handedness) or the lift diverges, the topology is SEVERED: the
# stroke is cut at the fold line and the unreachable stretch is dropped -
# a UV seam, which is exactly what makes the pattern wrap out of sight there
# (occlusion by construction rather than residual fudging).
_SEVER_RESIDUAL = 0.4   # px: Newton residual above this = no computable preimage
# The fold gate: min over the two guides' direction pairs of |sin(angle)|,
# SIGNED against the crossing's handedness. Above this margin the child frame
# cannot fold anywhere and severing short-circuits to "one island" at zero
# per-point cost (straight guides in particular never pay for severing).
_SEVER_GATE_SIN = 0.02

# RDP decimation strength ("RDP" slider in the tool options, in 0.1px units).
# Only the samples INSERTED between two original vertices are decimated; the
# original vertices themselves are anchors and always survive (user rule:
# 原始点不能被降采样).
_RDP_STATE = {"eps": 0.3}


def rdp_eps():
    return _RDP_STATE["eps"]

# Refer-rect debug grid state, PER VIEW. It answers "is my mapping right?",
# and the two boards answer it separately: the child shows the reference frame
# itself, the main shows its image under the mapper, and wanting one is no
# reason to be shown the other.
_REFER_RECT = {"child": False, "main": False}
# Refer-rect grid density: iso-lines per axis over [-1, 1]. 5 is the
# historical look; finer settings are the debugging view the user asked
# for - reading a multi-line warp's topology needs more iso-lines than a
# placement check does. Sample density per iso-line scales with it so a
# fine grid stays smooth through folds.
GRID_DIVISION_CHOICES = (5, 9, 17, 33)
_GRID = {"divisions": 5}
# Grid polylines are O(n^2) in guide points to build (intersection searches):
# cache per view, invalidated whenever the guides change.
_GRID_CACHE = {"child": None, "main": None}

# The child-view "Occluded Areas" toggle (a script-defined view button):
# tint the parts of the child board that the CURRENT mapping folds
# face-down, so the user can see what the lining hides before mapping.
OCCLUSION_BUTTON = "occlusion_preview"
OCCLUSION_FILL = (104, 112, 140, 70)   # the lining color, translucent
_OCCLUSION = {"enabled": False}
_OCCLUSION_CACHE = {"items": None, "note": "", "share": 0.0}


def refer_rect_enabled(view_name="main"):
    """Grid display policy: the active unit's setting in unit mode, the
    per-board View-menu global otherwise."""
    if _UNIT_META:
        uid = _ACTIVE_UNIT["id"]
        return bool(uid) and bool(_unit_settings(uid).get("show_grid", False))
    return bool(_REFER_RECT.get(view_name, False))


def _occlusion_enabled():
    if _UNIT_META:
        uid = _ACTIVE_UNIT["id"]
        return bool(uid) and bool(_unit_settings(uid).get("show_occlusion", False))
    return bool(_OCCLUSION["enabled"])


def _grid_divisions():
    if _UNIT_META and _ACTIVE_UNIT["id"]:
        value = int(_unit_settings().get("grid_divisions", 5))
        return value if value in GRID_DIVISION_CHOICES else 5
    return int(_GRID.get("divisions", 5))


def curve_mode():
    return _CURVE_MODE["value"]


def _invalidate_grid_cache():
    _GRID_CACHE["child"] = None
    _GRID_CACHE["main"] = None
    # Same lifetime: both displays are pure functions of the guide assets.
    _OCCLUSION_CACHE["items"] = None

H_COLOR = (0, 0, 255, 255)
V_COLOR = (0, 255, 0, 255)
AREA_BORDER_COLOR = (120, 185, 250, 190)
AREA_FILL_COLOR = (150, 205, 255, 60)

# How every line this tool draws is DISPLAYED. One dict so the settings
# window has a single thing to read and write, and so nothing has to guess
# what "the crease colour" means. Widths are on-screen px; "style" is a
# Qt::PenStyle number (1 solid, 2 dash, 3 dot, 4 dash-dot).
LINE_STYLES = (("Solid", 1), ("Dashed", 2), ("Dotted", 3), ("Dash-Dot", 4))
_LINE_DISPLAY = {
    "h_color": H_COLOR,
    "h_width": 3.0,
    "h_style": 1,
    "v_color": V_COLOR,
    "v_width": 3.0,
    "v_style": 1,
    # The crease ("fold line") between the front and the back of a fold.
    "seal_color": (104, 112, 140, 255),
    "seal_width": 0.8,      # relative to the mapped stroke width
    "seal_style": 2,
    # The lining: the back-of-the-fold strokes, and the occlusion tint that
    # previews which parts of the texture land there.
    "back_color": (104, 112, 140, 255),
    "occlusion_alpha": 70,
    # Additional (refinement) lines: pink, on both boards.
    "additional_color": ADDITIONAL_COLOR,
    "additional_width": 2.5,
    "additional_style": 1,
}


def line_display():
    return dict(_LINE_DISPLAY)


def _display_color(key):
    value = _LINE_DISPLAY.get(key) or (0, 0, 0, 255)
    return tuple(int(c) for c in value)


def _display_style(key):
    style = int(_LINE_DISPLAY.get(key, 1))
    # 0 is Qt::NoPen: a guide that cannot be seen is never what was meant.
    return style if 1 <= style <= 5 else 1

ITEM_LABELS = {
    H_PROPERTY: "H center line",
    V_PROPERTY: "V center line",
    MAPPING_AREA_PROPERTY: "mapping area",
    NEAREST_PROPERTY: "nearest point",
    ADDITIONAL_PROPERTY: "additional line",
}

# view name -> {property -> item}; guide item: {"points": [...], "width": w},
# area item: {"polygons": [[(x, y), ...], ...]}
_MAPPING_ASSETS = {}

_last_run_handled = False


# ---------------------------------------------------------------------------
# scene access helpers
# ---------------------------------------------------------------------------

def _animean():
    import animean_python

    return animean_python


def _scene_model(view_name):
    import __main__

    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model

    wanted = f"{view_name}_paint_view"
    for info in _animean().get_scene():
        if info.get("sceneName") == wanted:
            return info["scene"]
    raise RuntimeError(f"scene for view '{view_name}' is not registered")


def _canvas_rect(view_name):
    """The page of `view_name`, straight from its scene.

    The globals below are a fallback for older builds; asking the model is
    both current (a global is only as fresh as the last state sync) and
    correct now that the page is a document property rather than the widget's
    size.
    """
    try:
        width, height = _scene_model(view_name).canvas_size()
        if width and height:
            return (0.0, 0.0, float(width), float(height))
    except Exception:
        pass

    import __main__

    width = getattr(__main__, f"{view_name}_canvas_width", None) or getattr(__main__, "canvas_width", 0)
    height = getattr(__main__, f"{view_name}_canvas_height", None) or getattr(__main__, "canvas_height", 0)
    if not width or not height:
        width, height = 4096, 4096
    return (0.0, 0.0, float(width), float(height))


def _assets_for(view_name):
    """The mapping-asset dict edits apply to RIGHT NOW.

    With an active mapping unit (an automapping layer has the focus) this is
    that unit's own per-view asset set; otherwise the legacy scene-global
    scratch set. Every internal consumer resolves through here, which is
    what makes the whole 10k-line pipeline per-unit without knowing it.
    """
    uid = _ACTIVE_UNIT["id"]
    if uid is None:
        return _MAPPING_ASSETS.setdefault(view_name, {})
    return _UNIT_ASSETS.setdefault(view_name, {}).setdefault(uid, {})


def _save_assets(view_name):
    """Persist this view's mapping assets into the scene's scriptData.

    scriptData travels with every history snapshot and with saved projects, so
    guides/areas become undoable and survive save/load.
    """
    if _ACTIVE_UNIT["id"] is not None:
        _save_units(view_name)
        return
    try:
        scene = _scene_model(view_name)
    except Exception:
        return
    script_store.write(scene, "mapping_assets", _assets_for(view_name))


def _load_assets(view_name):
    """Rebuild the dicts + overlays from the scene's scriptData (post-restore)."""
    try:
        scene = _scene_model(view_name)
    except Exception:
        return
    data = script_store.read(scene, "mapping_assets") or {}
    if not isinstance(data, dict):
        data = {}
    _MAPPING_ASSETS[view_name] = _sanitize_assets(data)
    _load_units(view_name, scene)
    _overlays_changed(view_name)


def _sanitize_assets(data):
    """One asset dict from raw scriptData: validated, typed, id-stamped."""
    assets = {}
    for prop, item in data.items():
        if prop == MAPPING_AREA_PROPERTY:
            polygons = [[(float(p[0]), float(p[1])) for p in polygon]
                        for polygon in item.get("polygons") or []]
            polygons = [polygon for polygon in polygons if len(polygon) >= 3]
            if polygons:
                assets[prop] = {"polygons": polygons}
        elif prop == NEAREST_PROPERTY:
            arc = item.get("arc") or []
            if len(arc) >= 2:
                assets[prop] = {"arc": [float(arc[0]), float(arc[1])]}
        elif prop == ADDITIONAL_PROPERTY:
            lines = []
            for line in item.get("lines") or []:
                points = [(float(p[0]), float(p[1]))
                          for p in line.get("points") or []]
                loaded = {"points": points, "width": float(line.get("width", 2.5))}
                if "id" in line:
                    try:
                        loaded["id"] = int(line["id"])
                    except (TypeError, ValueError):
                        pass
                if len(points) < 2:
                    # Keep the slot (with its id): legacy pairs match the
                    # other board by position, and dropping one here would
                    # shift every later pair.
                    loaded["points"] = []
                    lines.append(loaded)
                    continue
                if line.get("commands"):
                    loaded["commands"] = line["commands"]
                third = [[float(t[0]), float(t[1])]
                         for t in line.get("third") or [] if len(t) >= 2]
                # The stored Third polyline is only authoritative while it
                # still matches the drawn points one to one.
                if len(third) == len(points):
                    loaded["third"] = third
                lines.append(loaded)
            # Legacy assets carry no ids; stamp positional ones NOW, on both
            # boards alike, so identity survives later deletes. Left
            # implicit, a delete renumbered the id-less half of a pair out
            # from under its partner (measured: the surviving pair died and
            # its +20 px correction went to 0).
            for index, line in enumerate(lines):
                line.setdefault("id", index)
            if lines:
                assets[prop] = {"lines": lines}
        elif prop in GUIDE_PROPERTIES:
            points = [(float(p[0]), float(p[1])) for p in item.get("points") or []]
            if len(points) >= 2:
                loaded = {"points": points, "width": float(item.get("width", 3.0))}
                # Curve guides round-trip: dropping the commands here would
                # silently demote every guide to its flattening on the first
                # undo or project load.
                if item.get("commands"):
                    loaded["commands"] = item["commands"]
                assets[prop] = loaded
    return assets


# ---------------------------------------------------------------------------
# mapping units: one "automapping layer" = one tagged layer group whose
# config (guides, area, additional lines, display settings) lives in
# scriptData keyed by the group id. The layer stack holds only the OUTPUT;
# the unit's config is the authority and the output regenerates from it.
# ---------------------------------------------------------------------------

UNIT_TAG = "automapping"
UNIT_STORE_KEY = "mapping_units"
UNIT_SETTINGS_NAME = "automapping_unit"
UNIT_LAYER_TITLE = "Auto-Mapping Layer"
NEW_UNIT_ACTION = "new_automapping_layer"
NEW_LINE_LAYER_ACTION = "new_line_layer"
NEW_FILL_LAYER_ACTION = "new_fill_layer"
DUPLICATE_UNIT_ACTION = "duplicate_automapping_layer"
CONVERT_UNIT_ACTION = "convert_to_automapping_layer"

_UNIT_SETTING_DEFAULTS = {
    "show_h": True,          # H axis overlay (both boards)
    "show_v": True,          # V axis overlay
    "show_additional": True, # pink additional lines
    "show_area": True,       # mapping-area outline
    "show_nearest": True,    # red nearest-point handle (main)
    "show_grid": False,      # refer-rect grid
    "grid_divisions": 5,
    "show_occlusion": False, # occluded-areas tint (texture board)
    "front_visible": True,   # front content/lines member layers
    "back_visible": True,    # back content/lines member layers
    "seal_visible": True,    # crease-line member layers
    "auto_render": True,     # re-run the mapping live on every edit
}

# view -> {unit id -> {property -> item}}; the per-unit twin of
# _MAPPING_ASSETS, resolved through _assets_for.
_UNIT_ASSETS = {}
# unit id -> {"settings": {...}, "primary": layer id, "members":
# {layer id (str) -> {"role": "front"|"back"|"seal", "depth": int}}}.
# Owned by the MAIN scene (the unit's group lives in its layer tree).
_UNIT_META = {}
_ACTIVE_UNIT = {"id": None}
# The unit the Advanced Settings window edits: stashed by the layer-menu
# provider at right-click time (the C++ settings dialog carries no per-row
# context by design - see MainWindow::showLayerContextMenu).
_SETTINGS_TARGET = {"unit": None}
# Non-zero while a mapping run (or unit surgery) is mutating layers: the
# layerchange/pattern hooks it triggers are echoes, not user edits.
_RUN_GUARD = {"depth": 0}


def _unit_settings(uid=None):
    uid = uid if uid is not None else _ACTIVE_UNIT["id"]
    merged = dict(_UNIT_SETTING_DEFAULTS)
    meta = _UNIT_META.get(uid) if uid else None
    if meta:
        merged.update(meta.get("settings") or {})
    return merged


def _save_units(view_name):
    """Persist the per-unit store for one view.

    The main scene carries each unit's assets AND its meta (settings,
    primary layer, member roles); the child scene carries only its own
    per-unit assets - each scene owns exactly the state its history must
    restore, mirroring how the legacy per-view store split.
    """
    try:
        scene = _scene_model(view_name)
    except Exception:
        return
    units = {}
    store = _UNIT_ASSETS.setdefault(view_name, {})
    ids = set(store) | (set(_UNIT_META) if view_name == "main" else set())
    for uid in ids:
        entry = {"assets": store.get(uid) or {}}
        if view_name == "main":
            meta = _UNIT_META.get(uid) or {}
            entry["settings"] = meta.get("settings") or {}
            entry["primary"] = meta.get("primary") or 0
            entry["members"] = meta.get("members") or {}
        units[uid] = entry
    script_store.write(scene, UNIT_STORE_KEY, {"units": units} if units else None)


def _load_units(view_name, scene):
    """Rebuild this view's per-unit store (and, on main, the unit meta)."""
    data = script_store.read(scene, UNIT_STORE_KEY) or {}
    units = data.get("units") if isinstance(data, dict) else None
    store = {}
    meta = {}
    for uid, entry in (units or {}).items():
        if not isinstance(entry, dict):
            continue
        uid = str(uid)
        store[uid] = _sanitize_assets(entry.get("assets") or {})
        if view_name == "main":
            members = {}
            for lid, info in (entry.get("members") or {}).items():
                if isinstance(info, dict) and info.get("role"):
                    members[str(lid)] = {"role": str(info["role"]),
                                         "depth": int(info.get("depth", 0))}
            meta[uid] = {
                "settings": dict(entry.get("settings") or {}),
                "primary": int(entry.get("primary") or 0),
                "members": members,
            }
    _UNIT_ASSETS[view_name] = store
    if view_name == "main":
        _UNIT_META.clear()
        _UNIT_META.update(meta)
        # Units whose tagged group no longer exists in the restored tree
        # are dead: keeping them would latch unit mode (hidden overlays)
        # on a document that visibly has no automapping layers, and would
        # re-persist the orphan config forever.
        for dead in list(_UNIT_META):
            try:
                if scene.layer_group_tag(int(dead)) == UNIT_TAG:
                    continue
            except (AttributeError, TypeError, ValueError):
                continue
            _UNIT_META.pop(dead, None)
            for store_view in ("main", "child"):
                _UNIT_ASSETS.get(store_view, {}).pop(dead, None)
        # The restored document decides which unit is active now: re-derive
        # from the restored current layer rather than trusting the pre-undo
        # activation. Route the change through the same refresh the live
        # activation uses, or the OTHER board keeps the old unit's overlays.
        uid = None
        try:
            layer = scene.current_layer()
            if layer is not None and layer >= 0:
                uid = _unit_for_layer(scene, layer)
        except Exception:
            uid = None
        derived = uid if uid in _UNIT_META else None
        if derived != _ACTIVE_UNIT["id"]:
            _ACTIVE_UNIT["id"] = derived
            _invalidate_grid_cache()
            _push_overlay("main")
            _push_overlay("child")


def _unit_for_layer(scene, layer_index):
    """The unit id owning this layer, or None (old build / no unit)."""
    try:
        gid = scene.group_id_for_layer(layer_index, UNIT_TAG)
    except AttributeError:
        return None
    return str(gid) if gid else None


def _activate_unit(uid):
    if uid is not None and uid not in _UNIT_META:
        uid = None
    if uid == _ACTIVE_UNIT["id"]:
        return
    _ACTIVE_UNIT["id"] = uid
    # Entering a unit shows ITS guides; leaving hides everything - the
    # overlay builders read _assets_for/_unit_settings, so a repaint is the
    # whole policy.
    _invalidate_grid_cache()
    _push_overlay("main")
    _push_overlay("child")
    try:
        _animean().ui.refresh()
    except Exception:
        pass


def _layer_focus_event(message):
    """layerchange hook: unit activation follows the MAIN board's focus.

    The child board's layers carry the shared pattern, not units, so its
    focus changes are irrelevant - and must not tear down the overlays the
    user is editing guides under.
    """
    if message.get("view") != "main" or _RUN_GUARD["depth"]:
        return
    uid = None
    layer = message.get("layer", -1)
    if isinstance(layer, int) and layer >= 0:
        try:
            uid = _unit_for_layer(_scene_model("main"), layer)
        except Exception:
            uid = None
    _activate_unit(uid)


# ---------------------------------------------------------------------------
# geometry helpers (pure python, unit-testable without the embedded runtime)
# ---------------------------------------------------------------------------

def _stroke_points(stroke):
    points = []
    for polyline in stroke.get("polylines") or []:
        for point in polyline:
            points.append((float(point["x"]), float(point["y"])))
    if not points:
        for point in stroke.get("raw_points") or []:
            points.append((float(point["x"]), float(point["y"])))
    return points


def _stroke_segments(stroke):
    """Per-polyline segment list (no bogus joins between separate polylines)."""
    polylines = stroke.get("polylines") or []
    if not polylines and stroke.get("raw_points"):
        polylines = [stroke["raw_points"]]
    segments = []
    for polyline in polylines:
        for index in range(1, len(polyline)):
            a = polyline[index - 1]
            b = polyline[index]
            segments.append(((float(a["x"]), float(a["y"])),
                             (float(b["x"]), float(b["y"]))))
    return segments


def _cumulative_lengths(points):
    lengths = [0.0]
    for index in range(1, len(points)):
        lengths.append(lengths[-1] + math.hypot(points[index][0] - points[index - 1][0],
                                                points[index][1] - points[index - 1][1]))
    return lengths


def _segment_direction(points, index):
    dx = points[index + 1][0] - points[index][0]
    dy = points[index + 1][1] - points[index][1]
    length = math.hypot(dx, dy)
    if length <= 1e-12:
        return 1.0, 0.0
    return dx / length, dy / length


def _point_at_arc(points, cumulative, arc):
    total = cumulative[-1]
    if len(points) < 2 or total <= 0.0:
        return points[0]
    if arc <= 0.0:
        dx, dy = _segment_direction(points, 0)
        return (points[0][0] + dx * arc, points[0][1] + dy * arc)
    if arc >= total:
        dx, dy = _segment_direction(points, len(points) - 2)
        extra = arc - total
        return (points[-1][0] + dx * extra, points[-1][1] + dy * extra)

    index = bisect.bisect_right(cumulative, arc) - 1
    index = max(0, min(index, len(points) - 2))
    segment = cumulative[index + 1] - cumulative[index]
    t = 0.0 if segment <= 0.0 else (arc - cumulative[index]) / segment
    return (points[index][0] + (points[index + 1][0] - points[index][0]) * t,
            points[index][1] + (points[index + 1][1] - points[index][1]) * t)


def _direction_at_arc(points, cumulative, arc):
    """d/d(arc) of _point_at_arc: the unit direction of the active segment.

    Mirrors _point_at_arc's branch structure exactly, including the linear
    extrapolation past either end, so it really is that function's derivative
    (used as a Jacobian column when inverting the frame).
    """
    total = cumulative[-1]
    if len(points) < 2 or total <= 0.0:
        return (1.0, 0.0)
    if arc <= 0.0:
        return _segment_direction(points, 0)
    if arc >= total:
        return _segment_direction(points, len(points) - 2)
    index = bisect.bisect_right(cumulative, arc) - 1
    index = max(0, min(index, len(points) - 2))
    return _segment_direction(points, index)


def _tangent_at_arc(points, cumulative, arc, window=0.0):
    """Unit tangent of the polyline at arc position (end tangents outside).

    With a positive window the tangent is a central difference over
    [arc-window, arc+window]: hand-drawn guides carry per-segment direction
    jitter and release hooks at the ends, and the raw values would make the
    consumers (the handedness/mirror check in build_mapper, the direction
    arrows) flicker with the noise. The window keeps genuine curvature
    while averaging the noise away.
    """
    if len(points) < 2:
        return (1.0, 0.0)
    if window > 0.0:
        before = _point_at_arc(points, cumulative, arc - window)
        after = _point_at_arc(points, cumulative, arc + window)
        dx = after[0] - before[0]
        dy = after[1] - before[1]
        length = math.hypot(dx, dy)
        if length > 1e-9:
            return (dx / length, dy / length)
    if arc <= 0.0:
        return _segment_direction(points, 0)
    if arc >= cumulative[-1]:
        return _segment_direction(points, len(points) - 2)
    index = bisect.bisect_right(cumulative, arc) - 1
    index = max(0, min(index, len(points) - 2))
    return _segment_direction(points, index)


# A vertex turning more than this is a drawn CORNER, not hand jitter. Measured
# across every guide in the test corpus: jitter and genuine curvature stay
# under 15 deg per vertex (p99), the wildest hand wobble reaches 34 deg, and
# deliberately drawn corners start at 113 deg. 45 deg sits in that gap with
# margin on both sides. This flattening heuristic is the only signal a
# POLYLINE guide offers; it cannot go lower, because flattening chords fold
# genuine curvature into the measured turn.
_SHARP_TURN_DEG = 45.0
_SHARP_COS = math.cos(math.radians(_SHARP_TURN_DEG))

# CURVE guides carry a better signal: the stroke fitter emits a corner as a
# DELIBERATE tangent break at a segment joint (every other joint is G1 by
# construction), and its own threshold is 55 - 35*(corner/100) deg - 37.5 at
# the default pen, down to 20 at the slider max. All of those sit BELOW the
# flattening heuristic's 45, so reading the exact one-sided tangents at each
# joint is the lossless test: ~0 deg at a smooth joint, the full break angle
# at a corner, no flattening wobble folded in. 10 deg clears numerical noise
# by orders of magnitude and undercuts every fitter setting.
_SHARP_JOINT_DEG = 10.0
_SHARP_JOINT_COS = math.cos(math.radians(_SHARP_JOINT_DEG))

def _cubic_is_line(seg):
    """True when the control net lies on the chord (a line command in disguise)."""
    (x0, y0), c1, c2, (x3, y3) = seg
    dx, dy = x3 - x0, y3 - y0
    length = math.hypot(dx, dy)
    if length <= 1e-12:
        return True
    for cx, cy in (c1, c2):
        if abs((cx - x0) * dy - (cy - y0) * dx) / length > 1e-9:
            return False
    return True


def _sharp_vertex_arcs(points, arcs):
    """Arc positions of vertices that turn more than _SHARP_TURN_DEG.

    The map itself (`hv`) evaluates guide POSITIONS, so a drawn corner is a
    hard kink in the displacement field and the real fold it causes sits
    exactly on the corner's preimage. The orientation tests, however, read
    window-smoothed tangents (see tangent_at), and a plain central difference
    across a 113 deg corner smears the tangent jump into a ramp: the analytic
    fold locus then lands where the RAMP crosses zero - measured 11 child px
    away from where the artwork actually folds back, drifting with the row.
    Recording the sharp vertices lets tangent_at stop its window at them, so
    the smoothed tangent still de-noises jitter but jumps exactly where the
    position field kinks.
    """
    out = []
    for i in range(1, len(points) - 1):
        ax = points[i][0] - points[i - 1][0]
        ay = points[i][1] - points[i - 1][1]
        bx = points[i + 1][0] - points[i][0]
        by = points[i + 1][1] - points[i][1]
        la = math.hypot(ax, ay)
        lb = math.hypot(bx, by)
        if la < 1e-9 or lb < 1e-9:
            continue
        if (ax * bx + ay * by) / (la * lb) < _SHARP_COS:
            out.append(arcs[i])
    return out


def _segment_intersection(a1, a2, b1, b2):
    d1x = a2[0] - a1[0]
    d1y = a2[1] - a1[1]
    d2x = b2[0] - b1[0]
    d2y = b2[1] - b1[1]
    denom = d1x * d2y - d1y * d2x
    if abs(denom) < 1e-12:
        return None
    ox = b1[0] - a1[0]
    oy = b1[1] - a1[1]
    t = (ox * d2y - oy * d2x) / denom
    u = (ox * d1y - oy * d1x) / denom
    return t, u


def _polylines_cross(a, b):
    """True if the two polylines genuinely intersect."""
    eps = 1e-6
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = _segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -eps <= t <= 1.0 + eps and -eps <= u <= 1.0 + eps:
                return True
    return False


def _polyline_intersection(a, b):
    """Return (point, arc_on_a, arc_on_b) where the polylines cross.

    Falls back to the closest vertex pair when the lines do not intersect.
    """
    a_cum = _cumulative_lengths(a)
    b_cum = _cumulative_lengths(b)
    eps = 1e-6
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = _segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -eps <= t <= 1.0 + eps and -eps <= u <= 1.0 + eps:
                point = (a[i][0] + (a[i + 1][0] - a[i][0]) * t,
                         a[i][1] + (a[i + 1][1] - a[i][1]) * t)
                arc_a = a_cum[i] + (a_cum[i + 1] - a_cum[i]) * min(max(t, 0.0), 1.0)
                arc_b = b_cum[j] + (b_cum[j + 1] - b_cum[j]) * min(max(u, 0.0), 1.0)
                return point, arc_a, arc_b

    best = None
    for i, pa in enumerate(a):
        for j, pb in enumerate(b):
            distance = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
            if best is None or distance < best[0]:
                best = (distance, i, j)
    _, i, j = best
    point = ((a[i][0] + b[j][0]) * 0.5, (a[i][1] + b[j][1]) * 0.5)
    return point, a_cum[i], b_cum[j]


def _chord_sides(points, origin, chord_len):
    """Split a guide's chord at the origin: (toward-start, toward-end) lengths.

    Both sides are floored at 1% of the chord so a T-shaped crossing at an
    endpoint cannot divide by (nearly) zero.
    """
    ux = (points[-1][0] - points[0][0]) / chord_len
    uy = (points[-1][1] - points[0][1]) / chord_len
    t = (origin[0] - points[0][0]) * ux + (origin[1] - points[0][1]) * uy
    t = min(max(t, 0.0), chord_len)
    floor = 0.01 * chord_len
    return max(t, floor), max(chord_len - t, floor)


def _arc_sides(total, crossing_arc):
    floor = 0.01 * total
    return max(crossing_arc, floor), max(total - crossing_arc, floor)


# 8-node Gauss-Legendre on [-1, 1]: exact for polynomials to degree 15. The
# speed |r'(t)| of a cubic is not a polynomial, but on the few-px segments the
# smoother emits its quadrature error is far below machine-relevant scales.
_GL8 = ((-0.9602898564975363, 0.1012285362903763),
        (-0.7966664774136267, 0.2223810344533745),
        (-0.5255324099163290, 0.3137066458778873),
        (-0.1834346424956498, 0.3626837833783620),
        (0.1834346424956498, 0.3626837833783620),
        (0.5255324099163290, 0.3137066458778873),
        (0.7966664774136267, 0.2223810344533745),
        (0.9602898564975363, 0.1012285362903763))

# Per-segment arc<->t lookup resolution. Slices are ~seg/16 long and the speed
# barely varies across one, so inverting by linear interpolation in the table
# mis-parameterizes by well under 1e-3 px while every returned POINT still
# lies exactly on the curve - the error is where along the curve, never off it.
_CURVE_LUT = 16


class _Curve:
    """One guide: a chain of line/cubic segments with a Gauss-Legendre arc table.

    Two modes. A plain point list stays a POLYLINE and delegates to the exact
    module functions the frame always used, so every existing polyline guide
    (straight tests, legacy scriptData, stored axis snapshots) reproduces the
    previous arithmetic bit for bit. `commands` - the stroke's real mixed
    line/Bezier path, which capture now keeps - switch the guide to CURVE
    mode: arc length comes from GL(8) per t-slice instead of summed chords,
    and points/tangents are evaluated on the true curve. That is the honest
    fix for the flattening error: geometry and integral upgrade TOGETHER, so
    endpoint anchoring (spec 4.2) keeps holding exactly - patching only the
    integral was measured to overshoot the drawn guide end by 0.56 px.
    """

    def __init__(self, points, commands=None):
        cubics = []
        if commands:
            subpaths = _commands_to_subpaths(commands)
            if subpaths:
                cubics = [cubic for cubic in subpaths[0]
                          if _dist(cubic[0], cubic[3]) > 1e-12
                          or _dist(cubic[0], cubic[1]) > 1e-12
                          or _dist(cubic[2], cubic[3]) > 1e-12]
        self.curved = bool(cubics)
        if not self.curved:
            self.points = [(float(p[0]), float(p[1])) for p in points]
            self.cum = _cumulative_lengths(self.points)
            self.total = self.cum[-1]
            self.knots = self.cum
            # Every vertex lies on the "curve" (the polyline itself) and its
            # chord arc IS its arc, so the flattening table is the cum table.
            # Needed because a frame may pair a curve guide with a polyline
            # one (legacy asset, axis snapshot, absorbed guide) and the
            # crossing search then runs on both flattenings alike.
            self.flat_arcs = self.cum
            self.sharp_arcs = _sharp_vertex_arcs(self.points, self.flat_arcs)
            return

        # --- curve mode ---
        self.segs = cubics
        self.luts = []
        self.cum = [0.0]
        for p0, c1, c2, p3 in cubics:
            lut = [0.0]
            for k in range(_CURVE_LUT):
                a = k / _CURVE_LUT
                b = (k + 1) / _CURVE_LUT
                half, mid = (b - a) * 0.5, (a + b) * 0.5
                length = half * sum(w * self._speed(p0, c1, c2, p3, mid + half * x)
                                    for x, w in _GL8)
                lut.append(lut[-1] + length)
            self.luts.append(lut)
            self.cum.append(self.cum[-1] + lut[-1])
        self.total = self.cum[-1]
        self.knots = self.cum
        # A flattening for crossing seeds and any consumer that wants a point
        # list; every vertex lies exactly on the curve, and flat_arcs carries
        # its TRUE arc position so seeds convert into curve coordinates.
        self.points = []
        self.flat_arcs = []
        for index, (p0, c1, c2, p3) in enumerate(cubics):
            net = bezier.hull_length((p0, c1, c2, p3))
            count = max(1, int(math.ceil(net / POLY_STEP)))
            start = 0 if index == 0 else 1
            for k in range(start, count + 1):
                t = k / count
                self.points.append(_cubic_point((p0, c1, c2, p3), t))
                self.flat_arcs.append(self.cum[index] + self._partial(index, t))
        # Two sharp-vertex sources, merged. The flattening heuristic (45 deg)
        # catches cusps INSIDE a degenerate control net. Joints get the exact
        # test: the fitter emits corners as deliberate tangent breaks between
        # segments, and comparing the two one-sided tangents reads the break
        # angle losslessly - a 40 deg corner (default pen threshold 37.5) is
        # invisible to the flattening heuristic but a real kink in hv(), and
        # missing it re-opens the smeared-locus bug the clamp exists to fix.
        # Line-line joints stay OUT of the exact test: an all-line command
        # path is a polyline in disguise (a joint at every drawn vertex, hand
        # jitter and all), and a 10 deg gate there would clamp the de-noising
        # window at every wobble - the very shredding the window prevents.
        # Those keep the 45 deg flattening rule; a joint touching a genuine
        # cubic is fitter output, where a tangent break is deliberate.
        flat_sharp = _sharp_vertex_arcs(self.points, self.flat_arcs)
        joint_sharp = []
        for index in range(len(self.segs) - 1):
            if (_cubic_is_line(self.segs[index])
                    and _cubic_is_line(self.segs[index + 1])):
                continue
            ax, ay = self._seg_dir(index, 1.0)
            bx, by = self._seg_dir(index + 1, 0.0)
            if ax * bx + ay * by < _SHARP_JOINT_COS:
                joint_sharp.append(self.cum[index + 1])
        self.sharp_arcs = []
        for arc in sorted(flat_sharp + joint_sharp):
            if not self.sharp_arcs or arc - self.sharp_arcs[-1] > 1e-6:
                self.sharp_arcs.append(arc)

    @staticmethod
    def of(spec):
        """Build from a guide spec: a bare point list, or an asset dict
        {"points": ..., "commands": ...} (commands optional)."""
        if isinstance(spec, dict):
            return _Curve(spec.get("points") or [], spec.get("commands"))
        return _Curve(spec)

    @staticmethod
    def _speed(p0, c1, c2, p3, t):
        # |r'(t)| via the shared wheel pyfile/bezier.py (its arithmetic is
        # expression-identical, preserving this table's sub-1e-3px contract).
        return math.hypot(*bezier.cubic_derivative((p0, c1, c2, p3), t))

    def _partial(self, index, t):
        """Arc from the segment's start to parameter t (GL on the last slice)."""
        lut = self.luts[index]
        slot = min(_CURVE_LUT - 1, int(t * _CURVE_LUT))
        a = slot / _CURVE_LUT
        half, mid = (t - a) * 0.5, (t + a) * 0.5
        p0, c1, c2, p3 = self.segs[index]
        if half > 0.0:
            tail = half * sum(w * self._speed(p0, c1, c2, p3, mid + half * x)
                              for x, w in _GL8)
        else:
            tail = 0.0
        return lut[slot] + tail

    def _locate(self, arc):
        """(segment index, t) for an interior arc position."""
        index = bisect.bisect_right(self.cum, arc) - 1
        index = max(0, min(index, len(self.segs) - 1))
        local = arc - self.cum[index]
        lut = self.luts[index]
        slot = bisect.bisect_right(lut, local) - 1
        slot = max(0, min(slot, _CURVE_LUT - 1))
        span = lut[slot + 1] - lut[slot]
        frac = 0.0 if span <= 0.0 else (local - lut[slot]) / span
        return index, (slot + frac) / _CURVE_LUT

    def _seg_dir(self, index, t):
        # Hodograph from the shared wheel pyfile/bezier.py.
        seg = self.segs[index]
        dx, dy = bezier.cubic_derivative(seg, t)
        length = math.hypot(dx, dy)
        if length <= 1e-12:
            # degenerate derivative (a stubby control net): fall back to the
            # segment chord, mirroring _segment_direction's guard
            return _segment_direction([seg[0], seg[3]], 0)
        return dx / length, dy / length

    def point_at(self, arc):
        if not self.curved:
            return _point_at_arc(self.points, self.cum, arc)
        if arc <= 0.0:
            dx, dy = self._seg_dir(0, 0.0)
            p = self.segs[0][0]
            return (p[0] + dx * arc, p[1] + dy * arc)
        if arc >= self.total:
            dx, dy = self._seg_dir(len(self.segs) - 1, 1.0)
            p = self.segs[-1][3]
            extra = arc - self.total
            return (p[0] + dx * extra, p[1] + dy * extra)
        index, t = self._locate(arc)
        return _cubic_point(self.segs[index], t)

    def dir_at(self, arc):
        """Exact unit tangent at the arc position (end tangents outside)."""
        if not self.curved:
            return _direction_at_arc(self.points, self.cum, arc)
        if arc <= 0.0:
            return self._seg_dir(0, 0.0)
        if arc >= self.total:
            return self._seg_dir(len(self.segs) - 1, 1.0)
        index, t = self._locate(arc)
        return self._seg_dir(index, t)

    def tangent_at(self, arc, window=0.0):
        """Window-smoothed tangent; window 0 is the exact curve tangent.

        The window never averages ACROSS a sharp corner: it is clamped to the
        nearest sharp vertex on each side, degenerating to a one-sided
        difference next to one. A one-sided chord still spans up to a full
        window of samples, so jitter suppression survives; but the tangent now
        JUMPS exactly at the corner, matching the kink the position field
        (`hv`) actually has there. Without the clamp the smeared tangent put
        the analytic fold locus 11 child px off the real fold-back
        (see _sharp_vertex_arcs) - strokes visibly overshot the crease and
        folded back beyond it.
        """
        if window > 0.0 and self.sharp_arcs:
            lo, hi = arc - window, arc + window
            index = bisect.bisect_left(self.sharp_arcs, arc)
            if index > 0:
                lo = max(lo, self.sharp_arcs[index - 1])
            if index < len(self.sharp_arcs):
                hi = min(hi, self.sharp_arcs[index])
            if (lo, hi) != (arc - window, arc + window):
                if hi - lo > 1e-9:
                    before = self.point_at(lo)
                    after = self.point_at(hi)
                    dx, dy = after[0] - before[0], after[1] - before[1]
                    length = math.hypot(dx, dy)
                    if length > 1e-9:
                        return (dx / length, dy / length)
                return self.dir_at(arc)
        if not self.curved:
            return _tangent_at_arc(self.points, self.cum, arc, window)
        if window > 0.0:
            before = self.point_at(arc - window)
            after = self.point_at(arc + window)
            dx, dy = after[0] - before[0], after[1] - before[1]
            length = math.hypot(dx, dy)
            if length > 1e-9:
                return (dx / length, dy / length)
        return self.dir_at(arc)


def _curve_crossing(gh, gv):
    """(point, arc_on_h, arc_on_v) where two CURVE guides cross.

    Seeded on the flattenings (whose vertices lie exactly on the curves and
    carry their true arc positions), then converged onto the curves by a 2x2
    Newton in (arc_h, arc_v). Transversal crossings converge in a few steps;
    if the iteration will not settle, the flattening seed - already within a
    chord's deviation of the truth - is kept.
    """
    a, b = gh.points, gv.points
    seed = None
    eps = 1e-6
    for i in range(len(a) - 1):
        for j in range(len(b) - 1):
            hit = _segment_intersection(a[i], a[i + 1], b[j], b[j + 1])
            if hit is None:
                continue
            t, u = hit
            if -eps <= t <= 1.0 + eps and -eps <= u <= 1.0 + eps:
                t = min(max(t, 0.0), 1.0)
                u = min(max(u, 0.0), 1.0)
                seed = (gh.flat_arcs[i] + (gh.flat_arcs[i + 1] - gh.flat_arcs[i]) * t,
                        gv.flat_arcs[j] + (gv.flat_arcs[j + 1] - gv.flat_arcs[j]) * u)
                break
        if seed is not None:
            break
    if seed is None:
        best = None
        for i, pa in enumerate(a):
            for j, pb in enumerate(b):
                distance = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
                if best is None or distance < best[0]:
                    best = (distance, i, j)
        _, i, j = best
        point = ((a[i][0] + b[j][0]) * 0.5, (a[i][1] + b[j][1]) * 0.5)
        return point, gh.flat_arcs[i], gv.flat_arcs[j]

    arc_h, arc_v = seed
    for _ in range(12):
        ph = gh.point_at(arc_h)
        pv = gv.point_at(arc_v)
        fx, fy = ph[0] - pv[0], ph[1] - pv[1]
        if math.hypot(fx, fy) <= 1e-10:
            break
        hx, hy = gh.dir_at(arc_h)
        vx, vy = gv.dir_at(arc_v)
        # solve [hx -vx; hy -vy] [dh dv]^T = -[fx fy]^T by Cramer
        det = vx * hy - hx * vy
        if abs(det) < 1e-12:
            break
        step_h = (fx * vy - fy * vx) / det
        step_v = (fx * hy - fy * hx) / det
        if abs(step_h) > gh.total or abs(step_v) > gv.total:
            break
        arc_h = min(max(arc_h + step_h, 0.0), gh.total)
        arc_v = min(max(arc_v + step_v, 0.0), gv.total)
    return gh.point_at(arc_h), arc_h, arc_v


class _Frame:
    """One board's HV frame: two crossing guides plus the coordinate system
    they induce. `hv` is THE shared reconstruction function - child and main
    differ only in the guides handed to it.

    Coordinates (l_h, l_v) are SIGNED ARC LENGTHS measured from the crossing
    along each guide - on the CURVE when the guide carries its commands
    (Gauss-Legendre tables in _Curve), on the polyline otherwise. Arc length
    (not chord projection) is what makes the child side symmetric with the
    main side; see build_mapper.
    """

    def __init__(self, h_spec, v_spec):
        self.gh = _Curve.of(h_spec)
        self.gv = _Curve.of(v_spec)
        # The flattenings stay exposed: crossing seeds, direction arrows and
        # the chord-basis Newton seed all read plain point lists.
        self.h = self.gh.points
        self.v = self.gv.points
        self.h_cum = self.gh.cum
        self.v_cum = self.gv.cum
        self.h_total = self.gh.total
        self.v_total = self.gv.total
        if self.gh.curved or self.gv.curved:
            self.origin, self.h_arc, self.v_arc = _curve_crossing(self.gh, self.gv)
        else:
            self.origin, self.h_arc, self.v_arc = _polyline_intersection(self.h, self.v)
        # Outward extent on each side of the crossing, floored at 1% so a
        # T-shaped crossing cannot divide by (nearly) zero. The RAW values are
        # kept too: the floored ones cannot tell "collapsed side" apart from
        # "genuinely 1% long", which is what the transfer scales need to know.
        self.h_side = _arc_sides(self.h_total, self.h_arc)
        self.v_side = _arc_sides(self.v_total, self.v_arc)
        self.h_side_raw = (self.h_arc, self.h_total - self.h_arc)
        self.v_side_raw = (self.v_arc, self.v_total - self.v_arc)
        # Orientation tests compare guide DIRECTIONS, and the raw per-sample
        # directions of a hand-drawn guide jitter enough to flip that
        # comparison several times within a few px - measured on a real
        # 301-point guide, the fold count oscillated between 2 and 4 eleven
        # times along V, which shredded the output and tangled the crease.
        # CURVE guides keep the window too: the smoother's quad chain is
        # tangent-CONTINUOUS, but its tangent at every joint is exactly
        # (p_i - p_{i-1})/2 - the raw inter-sample direction, i.e. the very
        # jitter signal, merely interpolated continuously between joints.
        # Continuity is not smoothness; zeroing the window here was measured
        # to reverse the joint-to-joint heading 17 times over 161 joints.
        # (The window only feeds orientation tests; the Newton solve keeps
        # the exact tangents via jacobian/dir_at. And the window is CLAMPED at
        # sharp corners - see _Curve.tangent_at - so de-noising cannot move a
        # fold off the corner the artwork actually creases at.)
        self.h_window = max(2.0 * POLY_STEP, 0.03 * self.h_total)
        self.v_window = max(2.0 * POLY_STEP, 0.03 * self.v_total)

    def directions(self, l_h, l_v):
        """Window-smoothed guide directions, for ORIENTATION tests only.

        Never for the Newton solve in `solve`: there the exact tangent is the
        true derivative of `hv`, and smoothing it would break convergence.
        (On curve guides the window is zero and these ARE the exact tangents.)
        """
        return (self.gh.tangent_at(self.h_arc + l_h, self.h_window),
                self.gv.tangent_at(self.v_arc + l_v, self.v_window))

    def hv(self, l_h, l_v):
        """H(l_h) + V(l_v) - O: the Coons patch in its collapsed form."""
        on_h = self.gh.point_at(self.h_arc + l_h)
        on_v = self.gv.point_at(self.v_arc + l_v)
        return (on_h[0] + on_v[0] - self.origin[0],
                on_h[1] + on_v[1] - self.origin[1])

    def jacobian(self, l_h, l_v):
        """Columns of d(hv)/d(l_h, l_v): the two guide tangents."""
        return (self.gh.dir_at(self.h_arc + l_h),
                self.gv.dir_at(self.v_arc + l_v))

    def solve(self, point, guess_h, guess_v, iterations=24, tol=1e-7):
        """Invert hv; see solve_full. Kept for callers that only want arcs."""
        l_h, l_v, _error = self.solve_full(point, guess_h, guess_v,
                                           iterations, tol)
        return l_h, l_v

    def solve_full(self, point, guess_h, guess_v, iterations=24, tol=1e-7):
        """Invert hv: find (l_h, l_v) with hv(l_h, l_v) == point.

        Returns (l_h, l_v, residual). The residual is the achieved
        |hv(l_h, l_v) - point|: ~0 wherever the inverse exists, and LARGE
        exactly where the frame folds over and `point` has no preimage on
        the reachable sheet - which is the divergence half of the severing
        verdict (see third_of in build_mapper).

        DAMPED Newton (the descent variant). hv is piecewise affine - an
        arc-length lookup on two polylines - so an undamped step lands exactly
        once the iterate reaches the right cell, and on mild guides it does:
        the chord seed is exact for straight guides (0 iterations) and two or
        three steps suffice up to about 30 px of guide bow, with the residual
        squaring each step.

        Strongly bowed guides break that. hv becomes non-injective where the
        two tangents turn parallel, and an undamped Newton then oscillates
        between cells instead of settling - a measured residual cycle of
        6.6 -> 5.4 -> 15.9 -> 6.6 -> ... forever. Halving the step until it
        actually reduces the residual turns that into descent: on 60 px bowed
        guides the share of points solved to 1e-7 px went from 59.5% to 89.9%,
        while straight and mildly bowed guides converge in exactly the same
        number of iterations as before (the first trial is accepted, so there
        is no extra evaluation on the easy path).

        See docs/point_mapping_newton.md for the full derivation, the flow
        charts and the measurement tables.

        Never converging is a legitimate outcome, not a bug: some of those
        points have no preimage at all and others have several, because the
        frame genuinely folds. So the search only ever ACCEPTS an improvement,
        returns the best iterate it saw, and REPORTS the residual so the
        caller can sever the topology there instead of pretending the lift
        exists. Iterate 0 is the chord seed - the coordinates the pre-2026-08
        implementation used - so the answer is never worse than that.
        """
        limit = self.h_total + self.v_total + 1.0
        l_h, l_v = guess_h, guess_v
        current = self.hv(l_h, l_v)
        best_error = math.hypot(current[0] - point[0], current[1] - point[1])
        best = (l_h, l_v)
        for _ in range(iterations):
            if best_error <= tol:
                break
            fx = current[0] - point[0]
            fy = current[1] - point[1]
            (hx, hy), (vx, vy) = self.jacobian(l_h, l_v)
            det = hx * vy - hy * vx
            if abs(det) < 1e-9:
                break  # locally folded frame: no usable direction from here
            step_h = (-fx * vy + fy * vx) / det
            step_v = (-hx * fy + hy * fx) / det
            scale = math.hypot(step_h, step_v)
            if scale > limit:
                step_h *= limit / scale
                step_v *= limit / scale

            accepted = None
            damping = 1.0
            for _ in range(12):
                trial_h = l_h + step_h * damping
                trial_v = l_v + step_v * damping
                trial = self.hv(trial_h, trial_v)
                error = math.hypot(trial[0] - point[0], trial[1] - point[1])
                if error < best_error:
                    accepted = (trial_h, trial_v, trial, error)
                    break
                damping *= 0.5
            if accepted is None:
                break  # no downhill step along this direction; keep the best
            l_h, l_v, current, best_error = accepted
            best = (l_h, l_v)
        return best[0], best[1], best_error


def _transfer_scales(child_raw, child_total, main_side):
    """Per-side arc scale child -> main, with a CONTINUOUS collapsed-side blend.

    A side collapsed onto the 1% floor (T-shaped crossing at an endpoint) has
    no real extent, so dividing by the floor would catapult stray points by a
    ~100x amplifier; the old code switched to the opposite side's scale with a
    hard `if`, which made the map discontinuous exactly at the threshold -
    moving the crossing by 1e-7 px across it moved a mapped point by ~200 px
    (measured). Ramping the weight over [0, floor] of the RAW side length
    keeps both endpoint behaviours (fallback at 0, plain ratio at/above the
    floor) and removes the cliff.
    """
    raw_neg, raw_pos = child_raw
    m_neg, m_pos = main_side
    floor = 0.01 * child_total
    if floor <= 0.0:
        return 1.0, 1.0
    base_neg = m_neg / max(raw_neg, floor)
    base_pos = m_pos / max(raw_pos, floor)
    w_neg = min(1.0, max(0.0, raw_neg / floor))
    w_pos = min(1.0, max(0.0, raw_pos / floor))
    return ((1.0 - w_neg) * base_pos + w_neg * base_neg,
            (1.0 - w_pos) * base_neg + w_pos * base_pos)


def _branch_window(points, third):
    """Canvas-arc-fraction window (f_lo, f_hi) of the longest
    branch-continuous stretch of a line's Third lift, or None when no
    usable stretch exists.

    On a FOLDED frame the inverse solve is branch-ambiguous, and a line
    drawn ACROSS a fold edge has no continuous Third lift at all -
    canvas points 3.85 px apart landed 399 px apart in Third
    (ADD_TOPO_ERROR), and interpolating a profile across that jump
    fabricated a displacement field that folded clean ground into phantom
    layers. Detection compares each step's Third/canvas STRETCH RATIO to
    the line's own overall ratio (the Third parameterization legitimately
    stretches severalfold on curved guides, so no absolute Third scale
    works, and a Third-median test was blind to 2-3 point polylines - a
    straight drag flattens to exactly 2 points). Windows are measured in
    CANVAS arc fractions: a jump inflates the THIRD arc (one 399 px step
    was 80% of it), and jump-inflated fractions named different physical
    stretches on the pair's two sides - the clip then fabricated the very
    field it exists to prevent.
    """
    if len(points) < 2 or len(third) != len(points):
        return None
    c_cum = _cumulative_lengths(points)
    if c_cum[-1] <= 1e-6:
        return None
    t_cum = _cumulative_lengths(third)
    overall = t_cum[-1] / c_cum[-1]
    if len(points) == 2:
        # A single step IS the overall ratio - only an absolute runaway
        # betrays a jump (normal Third stretch stays single-digit even on
        # strongly curved guides; a branch jump multiplies it wholesale).
        return (0.0, 1.0) if overall <= 40.0 else None
    limit = max(6.0 * max(overall, 1.0), 15.0)
    best = (0.0, None)
    run_start = 0
    count = len(points) - 1
    for index in range(count + 1):
        jump = False
        if index < count:
            c_step = max(c_cum[index + 1] - c_cum[index], 0.75)
            t_step = t_cum[index + 1] - t_cum[index]
            jump = t_step > limit * c_step
        if index == count or jump:
            if index > run_start:
                length = c_cum[index] - c_cum[run_start]
                if length > best[0]:
                    best = (length, (c_cum[run_start] / c_cum[-1],
                                     c_cum[index] / c_cum[-1]))
            run_start = index + 1
    return best[1]


def _slice_third_by_canvas_fraction(points, third, f_lo, f_hi):
    """The Third polyline restricted to a canvas-arc-fraction window."""
    c_cum = _cumulative_lengths(points)
    t_cum = _cumulative_lengths(third)
    lo = c_cum[-1] * f_lo
    hi = c_cum[-1] * f_hi

    def third_at(canvas_arc):
        index = bisect.bisect_right(c_cum, canvas_arc) - 1
        index = max(0, min(index, len(points) - 2))
        span = c_cum[index + 1] - c_cum[index]
        t = 0.0 if span <= 1e-9 else (canvas_arc - c_cum[index]) / span
        return _point_at_arc(third, t_cum,
                             t_cum[index] + (t_cum[index + 1] - t_cum[index]) * t)

    inner = [i for i, c in enumerate(c_cum) if lo < c < hi]
    out = [third_at(lo)] + [tuple(third[i]) for i in inner] + [third_at(hi)]
    return out if len(out) >= 2 else []


def _resample_fractions(points, count):
    """`points` resampled to `count` stations at equal ARC FRACTIONS."""
    cum = _cumulative_lengths(points)
    total = cum[-1]
    if total <= 1e-12:
        return [tuple(points[0])] * count
    return [_point_at_arc(points, cum, total * k / (count - 1))
            for k in range(count)]


class _FlowFieldWarp:
    """The influence field of the additional (pink) refinement lines, as a
    GLOBAL flow field integrated by Poisson (user spec 2026-08-25).

    PARADIGM. The previous _AdditionalWarp treated each line as an absolute
    displacement constraint and CHAINED the lines (stage k evaluated in the
    rendering of stages 1..k-1). Chaining meant an earlier line's local
    space compression locked away the ground a later line needed - the
    measured gradient collapse the spec retires. Here a line no longer
    pushes coordinates; it REORIENTS the texture flow: each pair yields a
    per-station similarity J_k = tau_main / tau_child (rotation + scale as
    one complex ratio, station-matched by arc fraction), every point blends
    all lines' J_k by normal-distance decay into one target gradient field
    G, and the final map U is the energy minimiser of |grad U - G|^2 - a
    single Poisson solve, order-free, with conflicts resolved by least
    internal stress instead of by drawing order.

    THREE STAGES (the spec's pipeline):
      I  PRE-STRETCH. Lines may overrun the frame's Third window (版心).
         The tent field U_base translates each edge outward by the overrun
         T with weight W(t) = min(1, (|t|/B)^ALPHA) - zero on the two axes
         (the stable spine), full at the original edge - so the solve
         domain encloses every line and the Dirichlet boundary is honest
         ground, not a clamp through a line's band. U_base is SEPARABLE
         per axis, so its exact inverse is two 1-D bisections.
      II FLOW FIELD. G = (sum w_k J_k + w_amb grad(U_base)) /
         (sum w_k + w_amb) with w_amb = max(0, 1 - sum w_k): inside a
         band the lines own the field, outside it relaxes to the tent's
         own gradient, and the blend is continuous at every band edge
         (a bare sum/sum jumps from J_k to I where w_k reaches zero).
         w_k is the existing falloff (linear / quadratic tool option) of
         normal distance over R = radius_factor * max(chord, arc).
      III POISSON. Solve lap(U) = div(G) on a regular grid over the
         expanded window with U = U_base pinned on the boundary
         (eliminated Dirichlet, solved as lap(D) = div(G) - lap(U_base)
         for the homogeneous correction D = U - U_base), by conjugate
         gradients on the 5-point Laplacian - numpy-vectorised, a few ms
         at the default resolution, so the mapper can rebuild during
         guide drags.

    CONTRACT SURFACE (same insertion points as the old class):
      apply / unapply       forward field (bilinear on the solved grid,
                            tent outside it) and its inverse (fixed-point
                            + guarded Newton; exact tent inverse outside);
      det_sign              sign of det(grad U) - O(1) when the solved
                            field is orientation-preserving everywhere
                            (the normal case: Poisson smoothing is exactly
                            what keeps it so), per-cell lookup otherwise;
      fold_loci             det = 0 contours of THIS field (usually
                            none) - marched from the same per-node dets
                            det_sign reads, so loci and pointwise
                            orientation cannot disagree;
      pairs                 one entry per drawn line ({"child", "main",
                            "radius", ...}) - truthiness gates the warp;
      has_faces / face_at   permanently False / 0: the C-shape face system
                            rode on the chained sweeps and retires with
                            them.
    Intentional warp FOLDS retire too: under the flow model occlusion
    comes from the child frame (severing) and the main frame (front/back
    split), and a Poisson-integrated field resolves conflicting asks by
    stress minimisation instead of doubling the sheet back.
    """

    SAMPLES = 65
    GRID = 96             # grid nodes along the longer domain side
    STRETCH_ALPHA = 2.0   # tent decay exponent (spec: alpha >= 1)
    CG_TOL = 1e-10        # relative residual^2
    CG_MAX_ITER = 1500
    # A locally rotated target field is NOT integrable: complying costs
    # deformation of the identity-anchored surroundings that grows with
    # domain AREA, so plain least squares simply ignores the drawn ask
    # (energy audit: complying with a 25 deg ask cost ~50x more than
    # ignoring it - and the solve showed 4 deg on the line). Two counters:
    #   * FIT_GAIN - weighted least squares min integral w|grad U - G|^2
    #     with w = 1 + FIT_GAIN * (band influence): where the lines
    #     actually speak, matching them dominates; empty ground is the
    #     SOFTEST, so an incompatible ask resolves by bending the empty
    #     surroundings smoothly instead of by muting the drawn intent.
    #     (The opposite weighting - stiff empty ground to kill the far
    #     tails - was tried first and crushed rotation asks to 3.7 deg:
    #     compliance and compact support are the true trade-off here, and
    #     the drawn intent wins; the tails are smooth and Dirichlet-bound.)
    #   * BOOST_ROUNDS - after each solve the achieved gradient is
    #     measured ON each line and the per-station target is corrected
    #     by the SECANT rule a* = ask * a_n / achieved_n (the response is
    #     close to complex-linear, so the multiplicative step lands in
    #     one round), capped at BOOST_CAP x the ask so an unreachable ask
    #     cannot run away. Both leave an integrable ask (e.g. a pure
    #     translation pair, J = I) untouched: zero deficit, no boost.
    FIT_GAIN = 24.0
    BOOST_ROUNDS = 3
    BOOST_CAP = 4.0
    # The H/V axes are the ABSOLUTE stable zone (user 2026-08-25: no line
    # weight near the axes - the axes must not drift, ever). Two guards:
    # every line's influence ramps to ZERO near either axis line
    # (smoothstep over AXIS_GUARD), and a diagonal anchor pulls the
    # ground near the axes toward U_base (identity there) with no
    # exemption. Besides pinning the spine (measured 34 px drift from a
    # line 250 px away without any anchor), the interior anchor shortens
    # every harmonic tail.
    # The axes are pinned as HARD interior Dirichlet lines (the grid
    # rows/columns nearest x = 0 and y = 0 keep U = U_base exactly): a
    # diagonal penalty was an arms race against the bands' fit weight
    # (lambda = 8 still lost 19 px of V axis to a neighbouring band), and
    # elimination costs nothing to tune. AXIS_GUARD is the weight-free
    # halo that gives the field room to blend around the pinned lines.
    AXIS_GUARD = 0.12      # weight-free halo, as a fraction of the window
    DET_FOLD_TOL = 1e-3    # |det| below this is a graze, not a fold

    def __init__(self, pairs, falloff="linear", radius_factor=0.5,
                 frame_window=None):
        self.falloff = falloff if falloff in ADDITIONAL_FALLOFFS else "linear"
        self.has_faces = False
        self.pairs = []
        self.notes = []
        self._loci = None
        self._grid = None
        self._tent = None
        for position, entry in enumerate(pairs):
            child_third, main_third = entry[0], entry[1]
            # The DRAWN line number rides along when the caller provides
            # it: build_mapper drops pairs on the way here, so a local
            # enumerate would misnumber every note after a dropped line.
            index = entry[2] if len(entry) > 2 else position
            if len(child_third) < 2 or len(main_third) < 2:
                continue
            line = self._prepare([tuple(p) for p in child_third],
                                 [tuple(p) for p in main_third],
                                 radius_factor)
            if line is not None:
                line["index"] = index
                self.pairs.append(line)
                if line["neutral"]:
                    # A silent no-op must not stay silent: the user drew a
                    # line and nothing happened.
                    self.notes.append(
                        f"additional line {index + 1} is neutral (both "
                        "sides ask for the identity flow) - bend or "
                        "rotate either side to shape the mapping")
        if self.pairs:
            self._build(frame_window)

    # ---------------------------------------------------------------- prep

    def _prepare(self, child, main, radius_factor):
        """One drawn pair -> stations, per-segment similarities, radius."""
        # Direction alignment: station k pairs with station k, so a line
        # redrawn BACKWARDS against its untouched partner must not read
        # as a ~180 degree flow ask (measured 71 px of drift without the
        # flip). The chord dot decides, exactly like the old sweep did -
        # except for near-CLOSED lines, whose chord is a few percent of
        # their arc and its sign is decided by where the user happened to
        # lift the pen: those align by endpoint proximity instead.
        chord_c = (child[-1][0] - child[0][0], child[-1][1] - child[0][1])
        chord_m = (main[-1][0] - main[0][0], main[-1][1] - main[0][1])
        arc_c_raw = _cumulative_lengths(child)[-1]
        arc_m_raw = _cumulative_lengths(main)[-1]
        if (math.hypot(*chord_c) < 0.05 * max(arc_c_raw, 1e-9)
                or math.hypot(*chord_m) < 0.05 * max(arc_m_raw, 1e-9)):
            same = (math.hypot(child[0][0] - main[0][0],
                               child[0][1] - main[0][1])
                    + math.hypot(child[-1][0] - main[-1][0],
                                 child[-1][1] - main[-1][1]))
            cross = (math.hypot(child[0][0] - main[-1][0],
                                child[0][1] - main[-1][1])
                     + math.hypot(child[-1][0] - main[0][0],
                                  child[-1][1] - main[0][1]))
            if cross < same:
                main = list(reversed(main))
        elif chord_c[0] * chord_m[0] + chord_c[1] * chord_m[1] < 0.0:
            main = list(reversed(main))
        c = _resample_fractions(child, self.SAMPLES)
        m = _resample_fractions(main, self.SAMPLES)
        cum = _cumulative_lengths(c)
        arc = cum[-1]
        chord = math.hypot(c[-1][0] - c[0][0], c[-1][1] - c[0][1])
        if arc <= 1e-9:
            return None
        radius = max(radius_factor * max(chord, arc), 1e-6)
        sims = []
        last = (1.0, 0.0)
        for k in range(self.SAMPLES - 1):
            tcx = c[k + 1][0] - c[k][0]
            tcy = c[k + 1][1] - c[k][1]
            tmx = m[k + 1][0] - m[k][0]
            tmy = m[k + 1][1] - m[k][1]
            denom = tcx * tcx + tcy * tcy
            if denom <= 1e-12:
                sims.append(last)   # degenerate station: carry the trend
                continue
            # tau_main / tau_child as a complex ratio: one similarity
            # (rotation + scale) per station - full-vector intent, no
            # normal-only projection (a C's arms displace ALONG the chord).
            last = ((tmx * tcx + tmy * tcy) / denom,
                    (tmy * tcx - tmx * tcy) / denom)
            sims.append(last)
        # Light smoothing: hand-drawn stations jitter, and the target field
        # feeds a second derivative (div G) - two box passes over the
        # similarity coefficients kill the per-station spikes without
        # flattening the drawn shape.
        for _ in range(2):
            smoothed = list(sims)
            for k in range(1, len(sims) - 1):
                smoothed[k] = (
                    (sims[k - 1][0] + 2.0 * sims[k][0] + sims[k + 1][0]) / 4.0,
                    (sims[k - 1][1] + 2.0 * sims[k][1] + sims[k + 1][1]) / 4.0)
            sims = smoothed
        # A line whose ask is the identity EVERYWHERE (a pure translation
        # pair, or a freshly drawn line against its own auto-synced
        # partner) is NEUTRAL: it must not enter the field at all. Left
        # in, its J = I stations actively compete with other lines' asks
        # wherever the bands overlap - a straight untouched pair laid
        # across a tuned band was measured to bend it by 64.6 px.
        neutral = all(abs(a - 1.0) < 1e-6 and abs(b) < 1e-6
                      for a, b in sims)
        return {"child": c, "main": m, "cum": cum, "sims": sims,
                "radius": radius, "chord": chord, "arc": arc,
                "neutral": neutral}

    def _weight(self, x):
        if x >= 1.0:
            return 0.0
        base = 1.0 - x
        return base * base if self.falloff == "quadratic" else base

    # ---------------------------------------------------------------- tent

    @staticmethod
    def _smoothstep(t):
        t = min(1.0, max(0.0, t))
        return t * t * (3.0 - 2.0 * t)

    def _tent_axis(self, value, side_neg, side_pos):
        """Raw displacement of one axis under the pre-stretch tent.

        Piecewise: (|t|/B)^alpha rise to the full overrun T at the
        original edge, HOLD across the stretched window (so the Dirichlet
        boundary carries the full translation), then a smoothstep FADE
        back to zero - the clamped profile translated ALL far ground
        beyond the edge forever (a probe 3000 px out moved by the full
        overrun of a 200 px line). The fade span is >= 2T, so the slope
        stays above -0.75 and value + displacement remains monotone (the
        1-D inverse stays well-defined)."""
        t, b, f0, f1, dead = side_pos if value >= 0.0 else side_neg
        if t == 0.0:
            return 0.0
        v = abs(value)
        if v <= dead:
            # DEAD ZONE covering the pinned band: the rise must be exactly
            # zero at the rows/columns pinned to U_base, or the axis
            # between them interpolates half their tent displacement
            # (measured 1.05 px of H-axis drift on a tiny frame).
            disp = 0.0
        elif v <= b:
            disp = t * ((v - dead) / (b - dead)) ** self.STRETCH_ALPHA
        elif v <= f0:
            disp = t
        elif v >= f1:
            disp = 0.0
        else:
            s = (v - f0) / (f1 - f0)
            disp = t * (1.0 - s * s * (3.0 - 2.0 * s))
        return disp if value >= 0.0 else -disp

    def _tent_cross(self, value, zone, pad):
        """Cross-axis damping factor: ZERO across the pinned band (pad)
        and the axis itself, smoothstep to 1 at the halo's edge - the
        pinned rows sit up to a cell off-axis, and pinning them to a
        U_base that still carried tent displacement dragged the axes."""
        return self._smoothstep((abs(value) - pad) / zone)

    def _tent_apply(self, third):
        """The pre-stretch. Each component is damped to ZERO near the
        OTHER axis (smoothstep over the guard zone): the spec's separable
        formula slid points ALONG the axes (T_V * W_V(y) moved V-axis
        ground 19 px at (0, 150)), and the axes must not move at all."""
        tent = self._tent
        tx, ty = tent["x"], tent["y"]
        sx = self._tent_cross(third[0], tent["zx"], tent["px"])
        sy = self._tent_cross(third[1], tent["zy"], tent["py"])
        return (third[0] + self._tent_axis(third[0], tx[0], tx[1]) * sy,
                third[1] + self._tent_axis(third[1], ty[0], ty[1]) * sx)

    def _tent_invert(self, third):
        """Damped-Newton inverse of the (now non-separable) tent. The
        tent is smooth and modest, so this converges in a few steps; the
        seed ignores the cross damping (the separable inverse)."""
        x = (third[0] - self._tent_axis(third[0], *self._tent["x"]),
             third[1] - self._tent_axis(third[1], *self._tent["y"]))
        best = None
        for _ in range(60):
            fx, fy = self._tent_apply(x)
            rx, ry = fx - third[0], fy - third[1]
            res = math.hypot(rx, ry)
            if best is None or res < best[0]:
                best = (res, x)
            if res < 1e-9:
                return x
            step = max(1e-3, 0.05 * (abs(rx) + abs(ry)))
            xp = self._tent_apply((x[0] + step, x[1]))
            yp = self._tent_apply((x[0], x[1] + step))
            jxx = (xp[0] - fx) / step
            jyx = (xp[1] - fy) / step
            jxy = (yp[0] - fx) / step
            jyy = (yp[1] - fy) / step
            det = jxx * jyy - jxy * jyx
            if abs(det) < 1e-9:
                x = (x[0] - rx, x[1] - ry)
                continue
            cand = (x[0] - (jyy * rx - jxy * ry) / det,
                    x[1] - (jxx * ry - jyx * rx) / det)
            cfx, cfy = self._tent_apply(cand)
            cres = math.hypot(cfx - third[0], cfy - third[1])
            x = cand if cres < res else (x[0] - 0.5 * rx, x[1] - 0.5 * ry)
        return best[1]

    # --------------------------------------------------------------- build

    def _build(self, frame_window):
        import pydeps
        np = pydeps.ensure("numpy")
        if np is None:
            # Degraded path like every other ensure() caller: the warp
            # stays inert (apply == identity) instead of crashing the
            # whole mapper build.
            self.notes.append(
                "additional lines need numpy for the flow-field solve "
                "and it could not be provided - the lines are ignored")
            return

        # Neutral lines (identity asks) stay out of the field entirely -
        # they are drawn markers awaiting an edit, not requests.
        active = [line for line in self.pairs if not line.get("neutral")]
        if not active:
            return
        xs = [p[0] for line in active
              for p in line["child"] + line["main"]]
        ys = [p[1] for line in active
              for p in line["child"] + line["main"]]
        if frame_window is not None:
            (h_lo, h_hi), (v_lo, v_hi) = frame_window
        else:
            h_lo, h_hi = min(xs), max(xs)
            v_lo, v_hi = min(ys), max(ys)
        b_left = max(abs(h_lo), 1e-6)
        b_right = max(abs(h_hi), 1e-6)
        b_bottom = max(abs(v_lo), 1e-6)
        b_top = max(abs(v_hi), 1e-6)
        # Stage I - overrun scan: how far the lines escape the frame window.
        t_left = max(0.0, h_lo - min(xs))
        t_right = max(0.0, max(xs) - h_hi)
        t_bottom = max(0.0, v_lo - min(ys))
        t_top = max(0.0, max(ys) - v_hi)
        zone_x = max(1e-6, self.AXIS_GUARD * (b_left + b_right))
        zone_y = max(1e-6, self.AXIS_GUARD * (b_bottom + b_top))
        # Solve domain: the stretched window plus 1.5x every band's radius
        # - a band whose edge touched the Dirichlet boundary was clamped
        # into a violent one-cell gradient there (a 138 px tear across the
        # boundary seam on a small frame); the extra half radius lets the
        # field decay before the clamp.
        margin = 1.5 * max(line["radius"] for line in active)
        x0 = h_lo - t_left - margin
        x1 = h_hi + t_right + margin
        y0 = v_lo - t_bottom - margin
        y1 = v_hi + t_top + margin
        span_x = max(x1 - x0, 1e-6)
        span_y = max(y1 - y0, 1e-6)
        longest = max(span_x, span_y)
        # Floor 48: with 24 an anisotropic frame resolved its short axis by
        # a handful of cells and the two-cell pinned band ate a fifth of it.
        nx = max(48, int(round(self.GRID * span_x / longest)))
        ny = max(48, int(round(self.GRID * span_y / longest)))
        du = span_x / nx
        dv = span_y / ny

        # Tent sides as (T, rise_end, fade_start, fade_end): full
        # translation holds exactly to the domain edge (the Dirichlet
        # boundary carries it), then fades to zero over >= 2T so far
        # ground is untouched and the profile stays monotone. The rise
        # spans at least six grid cells: on a tiny frame the (|t|/B)^2
        # ramp fit between two nodes, the bilinear grid could not follow
        # it (a 5-138 px tear against the exact outside tent) and the
        # pinned rows a cell off-axis already carried 8 px of it (the
        # measured 3.4 px H-axis drift).
        def tent_side(t, b, edge, cell):
            rise = max(b, 6.0 * cell)
            if t > 0.0:
                rise = min(rise, 0.9 * edge)
            dead = min(2.0 * cell, 0.5 * rise)
            return (t, rise, edge, edge + max(2.0 * t, 0.25 * b), dead)

        pad_x = 1.25 * du     # the pinned band is tent-free (see _tent_cross)
        pad_y = 1.25 * dv
        self._tent = {"x": (tent_side(t_left, b_left,
                                      b_left + t_left + margin, du),
                            tent_side(t_right, b_right,
                                      b_right + t_right + margin, du)),
                      "y": (tent_side(t_bottom, b_bottom,
                                      b_bottom + t_bottom + margin, dv),
                            tent_side(t_top, b_top,
                                      b_top + t_top + margin, dv)),
                      "zx": zone_x, "zy": zone_y,
                      "px": pad_x, "py": pad_y}

        gx = np.linspace(x0, x1, nx + 1)
        gy = np.linspace(y0, y1, ny + 1)
        X, Y = np.meshgrid(gx, gy, indexing="ij")

        # Tent field on the nodes: the scalar helpers are exact and the
        # node count is small, so vectorisation buys nothing worth a
        # second copy of the piecewise profile drifting out of sync.
        tent_x = np.array([self._tent_axis(v, *self._tent["x"]) for v in gx])
        tent_y = np.array([self._tent_axis(v, *self._tent["y"]) for v in gy])
        cross_x = np.array([self._tent_cross(v, zone_x, pad_x) for v in gx])
        cross_y = np.array([self._tent_cross(v, zone_y, pad_y) for v in gy])
        ub_x = X + tent_x[:, None] * cross_y[None, :]
        ub_y = Y + tent_y[None, :] * cross_x[:, None]

        # Stage II - per-line node influence at the RAW node positions:
        # apply() is queried at raw Third coordinates (the lifts), so the
        # weights must be measured there too. Measuring at the STRETCHED
        # positions ub(X) was the review's headline find: with any real
        # tent overrun the band landed one tent-displacement away from
        # the drawn line and the ask evaporated (25 deg -> 1.6 deg, the
        # field's peak 125 px from the line). Geometry is static across
        # boost rounds, so weights and nearest-segment indices are
        # computed once per line.
        px = X.ravel()
        py = Y.ravel()

        # FAMILY GEODESIC weights (user 2026-08-25 restatement): an
        # additional line IS a drawn geodesic - an extra iso-line of the
        # texture grid.  A line whose child stroke runs more in x than in
        # y belongs to the H family (a displaced H iso-line) and shapes
        # ONLY the H flow - the x-column of the target gradient; a V-like
        # line shapes only the y-column.  ORTHOGONAL FAMILIES NEVER
        # INTERACT.  Across a line's family the weight is KEYFRAME
        # INTERPOLATION along its orthogonal coordinate q (y for H-like,
        # x for V-like), per half-plane:
        #   - 0 on the PARALLEL axis through the origin O (the family's
        #     own anchor geodesic; the axes stay hard-pinned),
        #   - ramping up to 1 at the innermost line,
        #   - an OUTER line's weight decays to 0 at its inner
        #     neighbour's position (nested keyframes interpolate; a
        #     locally absent neighbour - extent faded - hands the ramp
        #     back toward the axis),
        #   - HELD outward beyond the outermost line: that is what lets
        #     the nearest line shape a free edge / released side.
        # Along the family direction a finite stroke fades over its
        # radius beyond its span (the extent envelope).  The falloff
        # option shapes the axis-side ramp and the extent fade; the
        # interpolation BETWEEN nested lines stays linear.  This replaces
        # the fixed-width axis halo and its crossing exceptions: a V-like
        # line crossing the H axis keeps its voice beside H naturally
        # (H is orthogonal to it and never mutes it), while its own
        # weight still dies on the V axis.
        def family_of(pts):
            run_x = sum(abs(b[0] - a[0]) for a, b in zip(pts[:-1], pts[1:]))
            run_y = sum(abs(b[1] - a[1]) for a, b in zip(pts[:-1], pts[1:]))
            return "h" if run_x >= run_y else "v"

        fields = []
        for line in active:
            pts = line["child"]
            ax = np.array([p[0] for p in pts[:-1]])
            ay = np.array([p[1] for p in pts[:-1]])
            bx = np.array([p[0] for p in pts[1:]])
            by = np.array([p[1] for p in pts[1:]])
            ex = bx - ax
            ey = by - ay
            ee = np.maximum(ex * ex + ey * ey, 1e-12)
            dxs = px[:, None] - ax[None, :]
            dys = py[:, None] - ay[None, :]
            t = np.clip((dxs * ex[None, :] + dys * ey[None, :]) / ee[None, :],
                        0.0, 1.0)
            qx = dxs - t * ex[None, :]
            qy = dys - t * ey[None, :]
            dist2 = qx * qx + qy * qy
            seg = np.argmin(dist2, axis=1)
            fam = family_of(pts)
            if fam == "h":
                qmid = 0.5 * (ay + by)
                s_pt = px
                s_lo = min(p[0] for p in pts)
                s_hi = max(p[0] for p in pts)
            else:
                qmid = 0.5 * (ax + bx)
                s_pt = py
                s_lo = min(p[1] for p in pts)
                s_hi = max(p[1] for p in pts)
            d_out = np.maximum(0.0, np.maximum(s_lo - s_pt, s_pt - s_hi))
            env = np.clip(1.0 - d_out / line["radius"], 0.0, 1.0)
            if self.falloff == "quadratic":
                env = env * env
            fields.append({
                "fam": fam,
                "seg": seg,
                "q_line": qmid[seg],   # the line's LOCAL keyframe position
                "env": env,
                "ask": np.array([complex(s[0], s[1]) for s in line["sims"]]),
                "boost": None,   # filled per round
                "line": line,
            })

        # Keyframe interpolation per family and half-plane (flat arrays).
        tiny = 1e-9
        constrain = bool(_ADDITIONAL.get("constrain_outline", True))
        for fam, q_pt in (("h", py), ("v", px)):
            group = [f for f in fields if f["fam"] == fam]
            if not group:
                continue
            qp = np.abs(q_pt)
            sp = np.sign(q_pt)
            # Under CONSTRAIN OUTLINE a pinned window edge is the
            # family's outermost KEYFRAME (an undrawn iso-line held at
            # identity): weights ramp down to 0 there instead of pressing
            # full-strength against the Dirichlet cage (which diluted the
            # whole outward band and could fold beside the edge).  A
            # RELEASED side contributes no edge keyframe - influence is
            # held outward and the nearest line shapes that edge.
            if fam == "h":
                edge_pos = v_hi if (constrain and t_top == 0.0) else None
                edge_neg = -v_lo if (constrain and t_bottom == 0.0) else None
                o_pt, o_cell = px, du
            else:
                edge_pos = h_hi if (constrain and t_right == 0.0) else None
                edge_neg = -h_lo if (constrain and t_left == 0.0) else None
                o_pt, o_cell = py, dv
            # Numerical relief beside the ORTHOGONAL axis: its straddling
            # rows/cols are hard Dirichlet, and a full-weight ask one
            # cell away folds the fit against them.  A few CELLS of
            # smoothstep (resolution-scaled - NOT the old window-scaled
            # halo) give the pinned band its boundary layer without
            # muting the line's voice; orthogonal families otherwise
            # never interact.
            o_t = np.clip(np.abs(o_pt) / (4.0 * o_cell), 0.0, 1.0)
            relief = o_t * o_t * (3.0 - 2.0 * o_t)
            cell = dv if fam == "h" else du   # cell size along q
            for f in group:
                same = (np.sign(f["q_line"]) * sp) > 0.0
                q_k = np.abs(f["q_line"])
                # side_f is the PARALLEL-axis boundary layer: a
                # smoothstep of the stroke's LOCAL position over 4 cells.
                # It makes the half-plane handover of a stroke crossing
                # its own axis CONTINUOUS (a binary nearest-station sign
                # test put a 0-to-1 weight cliff one cell wide along the
                # crossing's perpendicular and folded the field), and for
                # a stroke hugging its axis it damps the weight so `rise`
                # cannot press full strength against the hard-pinned axis
                # rows. One taper only - the final gate measured that an
                # additional keyframe-position floor doubled the damping
                # for nothing and notched the partition for sub-cell
                # separated lines.
                s_t = np.clip(sp * f["q_line"] / (4.0 * cell), 0.0, 1.0)
                side_f = s_t * s_t * (3.0 - 2.0 * s_t)
                # Neighbour keyframes, ENVELOPE-AWARE (a locally absent
                # line - extent faded to 0 - must not claim the slot):
                #   - the ramp foot q_lo is the FARTHEST effective inner
                #     keyframe, each candidate weighted by its local
                #     envelope, so an absent middle line hands over to
                #     the nearest PRESENT inner line, not to the axis;
                #   - every outer candidate imposes its own fade ceiling
                #     (1 at the line, 1 - env at the candidate), and the
                #     ceilings COMBINE by minimum - an absent nearer
                #     neighbour imposes nothing and can no longer mask
                #     the pinned-edge / domain keyframes behind it.
                q_lo = np.zeros_like(qp)          # inner bound (axis = 0)
                fade = np.ones_like(qp)
                for g in group:
                    if g is f:
                        continue
                    g_same = (np.sign(g["q_line"]) * sp) > 0.0
                    q_g = np.abs(g["q_line"])
                    inner = g_same & same & (q_g < q_k - tiny)
                    q_lo = np.maximum(q_lo,
                                      np.where(inner, g["env"] * q_g, 0.0))
                    outer = g_same & same & (q_g > q_k + tiny)
                    g_fade = 1.0 - g["env"] * np.clip(
                        (qp - q_k) / np.maximum(q_g - q_k, tiny), 0.0, 1.0)
                    fade = np.minimum(fade, np.where(outer, g_fade, 1.0))
                # The DOMAIN boundary is always the final implicit
                # keyframe (it is the numerical frame, Dirichlet D = 0,
                # NOT the user-visible outline): without it a held band
                # pressed the ask against the boundary and folded there.
                # The margin (>= 1.5 R) keeps this fade gentle and well
                # beyond any released window edge.
                if fam == "h":
                    dom_pos, dom_neg = y1, -y0
                else:
                    dom_pos, dom_neg = x1, -x0
                for side_sign, q_edge in ((1.0, edge_pos), (-1.0, edge_neg),
                                          (1.0, dom_pos), (-1.0, dom_neg)):
                    if q_edge is None:
                        continue
                    applies = same & (sp == side_sign) & (q_k < q_edge - tiny)
                    e_fade = 1.0 - np.clip(
                        (qp - q_k) / np.maximum(q_edge - q_k, tiny), 0.0, 1.0)
                    fade = np.minimum(fade, np.where(applies, e_fade, 1.0))
                rise = np.clip((qp - q_lo)
                               / np.maximum(q_k - q_lo, tiny), 0.0, 1.0)
                if self.falloff == "quadratic":
                    rise = rise * rise
                w = rise * fade * f["env"] * side_f * relief
                f["w"] = w.reshape(X.shape)
                # Silently ineffective lines are unacceptable: say so.
                if float(np.max(f["w"])) < 0.05:
                    self.notes.append(
                        f"additional line {f['line']['index'] + 1} has "
                        "(nearly) no influence - it lies on its own "
                        "family's axis or its extent fades before any "
                        "sampled ground")
                else:
                    pts_q = ([p[1] for p in f["line"]["child"]]
                             if fam == "h"
                             else [p[0] for p in f["line"]["child"]])
                    med_q = sorted(abs(v) for v in pts_q)[len(pts_q) // 2]
                    one_side = min(pts_q) >= 0.0 or max(pts_q) <= 0.0
                    if one_side and med_q < 4.0 * cell:
                        self.notes.append(
                            f"additional line {f['line']['index'] + 1} "
                            "hugs its own family's axis inside the "
                            "numerical boundary layer - the axis may not "
                            "move, so its influence is strongly damped")

        # Ambient: the tent's own FULL gradient (the cross-axis damping
        # makes it non-separable, so the off-diagonals are no longer
        # zero), so an empty region integrates back to exactly U_base and
        # every band edge blends continuously. The NODE version below
        # serves only the boost's measurement reference; the solve blends
        # at the EDGES with the exact edge difference of U_base (a
        # node-centred ambient averaged onto edges disagreed with the
        # exact base term it was subtracted from, leaving a spurious
        # residual flux in line-free ground).
        amb_xx = np.gradient(ub_x, du, axis=0)
        amb_xy = np.gradient(ub_x, dv, axis=1)
        amb_yx = np.gradient(ub_y, du, axis=0)
        amb_yy = np.gradient(ub_y, dv, axis=1)
        # Per-COLUMN weight sums: the H family speaks only for the
        # x-column of G (the H flow), the V family only for the y-column
        # - orthogonal families never interact, each column blends its
        # own family's asks with its own ambient share.
        wsum_x = np.zeros_like(X)
        wsum_y = np.zeros_like(X)
        for field in fields:
            if field["fam"] == "h":
                wsum_x += field["w"]
            else:
                wsum_y += field["w"]
        w_amb_x = np.maximum(0.0, 1.0 - wsum_x) + 1e-9
        w_amb_y = np.maximum(0.0, 1.0 - wsum_y) + 1e-9
        total_x = wsum_x + w_amb_x
        total_y = wsum_y + w_amb_y

        def edge_x(values):
            return 0.5 * (values[1:, :] + values[:-1, :])

        def edge_y(values):
            return 0.5 * (values[:, 1:] + values[:, :-1])

        # Weighted least squares: matching the target is most expensive
        # exactly where the lines speak (see FIT_GAIN above) - empty
        # ground is the softest, so incompatible asks bend the empty
        # surroundings smoothly instead of muting the drawn intent.
        # x-edges carry the x-column (H family), y-edges the y-column.
        wsum_ex = edge_x(wsum_x)
        wsum_ey = edge_y(wsum_y)
        wamb_ex = np.maximum(0.0, 1.0 - wsum_ex) + 1e-9
        wamb_ey = np.maximum(0.0, 1.0 - wsum_ey) + 1e-9
        total_ex = wsum_ex + wamb_ex
        total_ey = wsum_ey + wamb_ey
        om_e = 1.0 + self.FIT_GAIN * (wsum_ex / total_ex)  # x-edge weights
        om_n = 1.0 + self.FIT_GAIN * (wsum_ey / total_ey)  # y-edge weights
        # Hard axis pinning (see AXIS_GUARD above): BOTH grid columns/rows
        # straddling each axis line hold D = 0 (U = U_base, the identity
        # on the axes - the tent's W(0) = 0). Both straddlers, not just
        # the nearest: with one pinned column the continuum line x = 0
        # interpolates between it and a FREE neighbour and still drifted
        # 19 px.
        axis_pinned = np.zeros_like(X, dtype=bool)
        axis_pinned[np.abs(gx) <= du * 0.999, :] = True
        axis_pinned[:, np.abs(gy) <= dv * 0.999] = True

        # CONSTRAIN OUTLINE (menu 约束外轮廓, user 2026-08-25): the outer
        # contour the H/V axes form - the frame window's outline - keeps
        # its SHAPE.  Pinning is COMPONENT-WISE: a left/right edge pins
        # only x (the edge stays the straight line x = const; ground may
        # slide ALONG it), a bottom/top edge pins only y.  Corners of two
        # pinned edges get both components and hold exactly; a corner
        # between a pinned edge and a RELEASED one slides along the
        # pinned edge, so the released side's deformation meets the
        # constraint without a single-cell tear (full pinning fought the
        # tent's pre-stretch there).  EXCEPTION: a side an additional
        # line is drawn OUT across (exactly the sides with tent overrun)
        # is released and follows the flow.  Unchecked, the outline
        # deforms freely and the nearest line shapes the edge.  The pin
        # value is identity-minus-base (nonzero Dirichlet) in case the
        # tent ever leaks onto a pinned edge.
        pin_x = np.zeros_like(X)
        pin_y = np.zeros_like(X)
        pinned_x = axis_pinned.copy()
        pinned_y = axis_pinned.copy()
        if _ADDITIONAL.get("constrain_outline", True):
            in_h = (gx >= h_lo - du) & (gx <= h_hi + du)
            in_v = (gy >= v_lo - dv) & (gy <= v_hi + dv)
            edge_v = np.zeros_like(X, dtype=bool)   # left/right edge bands
            edge_h = np.zeros_like(X, dtype=bool)   # bottom/top edge bands
            if t_left == 0.0:
                edge_v[np.abs(gx - h_lo) <= du * 0.999, :] |= in_v[None, :]
            if t_right == 0.0:
                edge_v[np.abs(gx - h_hi) <= du * 0.999, :] |= in_v[None, :]
            if t_bottom == 0.0:
                edge_h[:, np.abs(gy - v_lo) <= dv * 0.999] |= in_h[:, None]
            if t_top == 0.0:
                edge_h[:, np.abs(gy - v_hi) <= dv * 0.999] |= in_h[:, None]
            pin_x = np.where(edge_v, X - ub_x, 0.0)
            pin_y = np.where(edge_h, Y - ub_y, 0.0)
            pinned_x |= edge_v
            pinned_y |= edge_h

        def weighted_div_lap(n_x, n_y, base):
            """div(omega * (G_row - grad base)) with the blend done ON the
            edges: n_x / n_y are the LINE parts of the row's numerator at
            the nodes, the ambient part is the exact edge difference of
            `base` itself, so G_edge - grad(base)_edge collapses to
            (n_edge - wsum_edge * grad(base)_edge) / total_edge and a
            line-free edge contributes exactly zero flux."""
            base_gx = (base[1:, :] - base[:-1, :]) / du
            base_gy = (base[:, 1:] - base[:, :-1]) / dv
            flux_x = om_e * (edge_x(n_x) - wsum_ex * base_gx) / total_ex
            flux_y = om_n * (edge_y(n_y) - wsum_ey * base_gy) / total_ey
            out = np.zeros_like(base)
            out[1:-1, :] += (flux_x[1:, :] - flux_x[:-1, :]) / du
            out[:, 1:-1] += (flux_y[:, 1:] - flux_y[:, :-1]) / dv
            return out

        def apply_A(Z, mask):
            """-div(omega * grad Z); homogeneous Dirichlet on the domain
            boundary AND on this component's pinned nodes."""
            flux_x = om_e * (Z[1:, :] - Z[:-1, :]) / du
            flux_y = om_n * (Z[:, 1:] - Z[:, :-1]) / dv
            out = np.zeros_like(Z)
            out[1:-1, :] -= (flux_x[1:, :] - flux_x[:-1, :]) / du
            out[:, 1:-1] -= (flux_y[:, 1:] - flux_y[:, :-1]) / dv
            out[0, :] = out[-1, :] = 0.0
            out[:, 0] = out[:, -1] = 0.0
            out[mask] = 0.0
            return out

        def apply_A_raw(Z):
            """-div(omega * grad Z) with NO pin/boundary masking - the
            operator's action on the (nonzero) pin values, folded into
            the free nodes' right-hand side."""
            flux_x = om_e * (Z[1:, :] - Z[:-1, :]) / du
            flux_y = om_n * (Z[:, 1:] - Z[:, :-1]) / dv
            out = np.zeros_like(Z)
            out[1:-1, :] -= (flux_x[1:, :] - flux_x[:-1, :]) / du
            out[:, 1:-1] -= (flux_y[:, 1:] - flux_y[:, :-1]) / dv
            return out

        def solve(rhs, pin_val, mask):
            # apply_A is MINUS div(omega grad) (SPD), so the equation
            # div(omega grad D) = rhs with D = pin_val on the pinned nodes
            # becomes, for E = D - pin_val (E = 0 there),
            # A E = -rhs - A_raw(pin_val).
            R = np.zeros_like(rhs)
            interior = -rhs - apply_A_raw(pin_val)
            R[1:-1, 1:-1] = interior[1:-1, 1:-1]
            R[mask] = 0.0
            E = np.zeros_like(rhs)
            P = R.copy()
            rs = float(np.sum(R * R))
            if rs <= 0.0:
                return E + pin_val
            rs0 = rs
            for _ in range(self.CG_MAX_ITER):
                AP = apply_A(P, mask)
                denom = float(np.sum(P * AP)) or 1e-30
                alpha = rs / denom
                E += alpha * P
                R -= alpha * AP
                rs_new = float(np.sum(R * R))
                if rs_new <= self.CG_TOL * rs0:
                    break
                P = R + (rs_new / rs) * P
                rs = rs_new
            return E + pin_val

        def numerators(boosted):
            """The LINE parts of G's numerator per component, at nodes.
            An H-family line's similarity ask a+ib fills only the
            x-column (target dU/dx = (a, b)); a V-family line fills only
            the y-column (target dU/dy = (-b, a))."""
            n00 = np.zeros_like(X)
            n01 = np.zeros_like(X)
            n10 = np.zeros_like(X)
            n11 = np.zeros_like(X)
            for field in fields:
                sims = field["ask"]
                if boosted and field["boost"] is not None:
                    sims = field["boost"]
                sa = sims.real[field["seg"]].reshape(X.shape)
                sb = sims.imag[field["seg"]].reshape(X.shape)
                w2 = field["w"]
                if field["fam"] == "h":
                    n00 += w2 * sa
                    n10 += w2 * sb
                else:
                    n01 += w2 * -sb
                    n11 += w2 * sa
            return n00, n01, n10, n11

        def assemble_and_solve():
            n00, n01, n10, n11 = numerators(boosted=True)
            ux = ub_x + solve(weighted_div_lap(n00, n01, ub_x), pin_x,
                              pinned_x)
            uy = ub_y + solve(weighted_div_lap(n10, n11, ub_y), pin_y,
                              pinned_y)
            return ux, uy

        def bilinear_np(table, sx, sy):
            fx = np.clip((sx - x0) / du, 0.0, nx - 1e-9)
            fy = np.clip((sy - y0) / dv, 0.0, ny - 1e-9)
            i = fx.astype(int)
            j = fy.astype(int)
            tx = fx - i
            ty = fy - j
            return ((1.0 - tx) * ((1.0 - ty) * table[i, j]
                                  + ty * table[i, j + 1])
                    + tx * ((1.0 - ty) * table[i + 1, j]
                            + ty * table[i + 1, j + 1]))

        # The measurement REFERENCE is the blended field of the ORIGINAL
        # asks, fixed across rounds. G - not each line's own ask - is the
        # reference because G already encodes the weighted compromise
        # between overlapping lines, so the boost compensates the solver's
        # dilution without starting an arms race between conflicting lines
        # (boosting toward the raw asks escalated two opposing rotations
        # into a fold). And it must be the ORIGINAL blend: measuring
        # against the boosted blend chases a target that rises with every
        # round (a single 25 deg ask overshot to 46 deg).
        n00r, n01r, n10r, n11r = numerators(boosted=False)
        g00m = (n00r + w_amb_x * amb_xx) / total_x
        g01m = (n01r + w_amb_y * amb_xy) / total_y
        g10m = (n10r + w_amb_x * amb_yx) / total_x
        g11m = (n11r + w_amb_y * amb_yy) / total_y
        ux, uy = assemble_and_solve()
        for _round in range(self.BOOST_ROUNDS):
            duxx = np.gradient(ux, du, axis=0)
            duxy = np.gradient(ux, dv, axis=1)
            duyx = np.gradient(uy, du, axis=0)
            duyy = np.gradient(uy, dv, axis=1)
            worst = 0.0
            for field in fields:
                pts = field["line"]["child"]
                mx = np.array([(a[0] + b[0]) * 0.5
                               for a, b in zip(pts[:-1], pts[1:])])
                my = np.array([(a[1] + b[1]) * 0.5
                               for a, b in zip(pts[:-1], pts[1:])])
                # Measure the line's OWN family column as a complex
                # similarity (x-column (a,b) <-> a+ib for H; y-column
                # (-b,a) <-> a+ib for V), against the fixed reference G.
                if field["fam"] == "h":
                    achieved = (bilinear_np(duxx, mx, my)
                                + 1j * bilinear_np(duyx, mx, my))
                    target = (bilinear_np(g00m, mx, my)
                              + 1j * bilinear_np(g10m, mx, my))
                else:
                    achieved = (bilinear_np(duyy, mx, my)
                                - 1j * bilinear_np(duxy, mx, my))
                    target = (bilinear_np(g11m, mx, my)
                              - 1j * bilinear_np(g01m, mx, my))
                current = field["ask"] if field["boost"] is None \
                    else field["boost"]
                # Stations whose keyframe weight is small - the stroke
                # skims its family's axis, or its extent has faded -
                # cannot express a correction; cranking the target there
                # would fight the interpolation, so it fades with w.
                station_guard = np.clip(
                    bilinear_np(field["w"], mx, my), 0.0, 1.0)
                worst = max(worst,
                            float(np.max(station_guard
                                         * np.abs(target - achieved))))
                safe = np.where(np.abs(achieved) < 1e-6,
                                1e-6 + 0j, achieved)
                boosted = current * target / safe
                mag = np.abs(boosted)
                cap = self.BOOST_CAP * np.maximum(np.abs(field["ask"]), 1e-6)
                boosted = np.where(mag > cap, boosted * (cap / mag), boosted)
                field["boost"] = (station_guard * boosted
                                  + (1.0 - station_guard) * current)
            if worst < 0.02:
                break
            ux, uy = assemble_and_solve()

        def det_of(sol_x, sol_y):
            """Per-node orientation as the MINIMUM over every one-sided
            stencil (all four left/right x down/up combinations). Central
            differences average the two adjacent cells and cannot see a
            fold confined to one cell row - the review reproduced a field
            whose node dets certified injectivity while apply() measurably
            folded (probe steps moving backwards, 12.8 px inverse error,
            and the fidelity retreat never firing because it reads the
            same array)."""
            sxx = (sol_x[1:, :] - sol_x[:-1, :]) / du
            sxy = (sol_x[:, 1:] - sol_x[:, :-1]) / dv
            syx = (sol_y[1:, :] - sol_y[:-1, :]) / du
            syy = (sol_y[:, 1:] - sol_y[:, :-1]) / dv

            def sided(edge, axis):
                if axis == 0:
                    return (np.concatenate([edge[:1, :], edge], axis=0),
                            np.concatenate([edge, edge[-1:, :]], axis=0))
                return (np.concatenate([edge[:, :1], edge], axis=1),
                        np.concatenate([edge, edge[:, -1:]], axis=1))

            xx_l, xx_r = sided(sxx, 0)
            yx_l, yx_r = sided(syx, 0)
            xy_d, xy_u = sided(sxy, 1)
            yy_d, yy_u = sided(syy, 1)
            best = None
            for xx, yx in ((xx_l, yx_l), (xx_r, yx_r)):
                for xy, yy in ((xy_d, yy_d), (xy_u, yy_u)):
                    det = xx * yy - xy * yx
                    best = det if best is None else np.minimum(best, det)
            return best

        # FIDELITY RETREAT: compliance never at the price of a fold the
        # unboosted compromise did not have. The pure blend is injective
        # for conflicting asks (the spec's no-wrinkle promise); the boost
        # can graze det below zero (measured -0.02 pockets). If negatives
        # appear, halve every boost toward the original ask and re-solve;
        # at full retreat any remaining fold is the honest energy answer
        # (e.g. a strong ask pressed against a pinned axis) and stays.
        det = det_of(ux, uy)
        retreat = 0
        while np.any(det <= -self.DET_FOLD_TOL) and retreat < 3 \
                and any(field["boost"] is not None for field in fields):
            for field in fields:
                if field["boost"] is not None:
                    field["boost"] = (field["ask"]
                                      + 0.5 * (field["boost"] - field["ask"]))
                    if float(np.max(np.abs(field["boost"]
                                           - field["ask"]))) < 1e-6:
                        field["boost"] = None
            ux, uy = assemble_and_solve()
            det = det_of(ux, uy)
            retreat += 1
        # FULL retreat must actually be reached: three halvings still
        # leave 12.5% of the boost, and shipping a fold the UNBOOSTED
        # blend does not have breaks the contract above. If folds
        # persist, drop every boost and take the honest pure-blend
        # answer; a fold that survives even that is intrinsic and stays.
        if np.any(det <= -self.DET_FOLD_TOL) \
                and any(field["boost"] is not None for field in fields):
            for field in fields:
                field["boost"] = None
            ux, uy = assemble_and_solve()
            det = det_of(ux, uy)

        # Orientation bookkeeping: per-node det of grad U, marched by
        # fold_loci and read by det_sign - the SAME numbers, so loci and
        # pointwise orientation cannot disagree. The tolerance keeps a
        # last-bit graze of zero from registering a phantom locus (and
        # from making the retreat count vary between runs).
        self._all_positive = bool(np.all(det > -self.DET_FOLD_TOL))
        self._grid = {
            "x0": x0, "y0": y0, "du": du, "dv": dv, "nx": nx, "ny": ny,
            "ux": ux.tolist(), "uy": uy.tolist(),
            "det": None if self._all_positive else det.tolist(),
        }

    # ------------------------------------------------------------- queries

    def _bilinear(self, table, third):
        grid = self._grid
        fx = (third[0] - grid["x0"]) / grid["du"]
        fy = (third[1] - grid["y0"]) / grid["dv"]
        # Bounds on the FRACTIONS, not on int(fx): int() truncates toward
        # zero, so fx in (-1, 0) slipped past an `i < 0` guard and the
        # one-cell strip outside the low edges was silently EXTRAPOLATED
        # with negative weights instead of falling back to the tent.
        if fx < 0.0 or fy < 0.0 or fx >= grid["nx"] or fy >= grid["ny"]:
            return None
        i = int(fx)
        j = int(fy)
        tx = fx - i
        ty = fy - j
        row0 = table[i]
        row1 = table[i + 1]
        return ((1.0 - tx) * ((1.0 - ty) * row0[j] + ty * row0[j + 1])
                + tx * ((1.0 - ty) * row1[j] + ty * row1[j + 1]))

    def apply(self, third):
        if self._grid is None:
            return (third[0], third[1])
        x = self._bilinear(self._grid["ux"], third)
        if x is None:
            return self._tent_apply(third)
        y = self._bilinear(self._grid["uy"], third)
        # EDGE BLEND: within three cells of the grid boundary the solved
        # field hands over to the exact tent smoothly. The Dirichlet clamp
        # leaves the correction D with a one-cell gradient at the edge,
        # and a stroke crossing the seam was torn by up to 15 px on a
        # small frame; the fringe is margin territory (no line's band
        # reaches it), so the tent IS the intended field there.
        grid = self._grid
        fx = (third[0] - grid["x0"]) / grid["du"]
        fy = (third[1] - grid["y0"]) / grid["dv"]
        edge = min(fx, fy, grid["nx"] - fx, grid["ny"] - fy) / 3.0
        if edge < 1.0:
            s = self._smoothstep(edge)
            tx, ty = self._tent_apply(third)
            return (tx + (x - tx) * s, ty + (y - ty) * s)
        return (x, y)

    def displacement(self, third):
        z = self.apply(third)
        return (z[0] - third[0], z[1] - third[1])

    def jacobian(self, third, step=0.25):
        xp = self.apply((third[0] + step, third[1]))
        xm = self.apply((third[0] - step, third[1]))
        yp = self.apply((third[0], third[1] + step))
        ym = self.apply((third[0], third[1] - step))
        return ((xp[0] - xm[0]) / (2.0 * step),
                (yp[0] - ym[0]) / (2.0 * step),
                (xp[1] - xm[1]) / (2.0 * step),
                (yp[1] - ym[1]) / (2.0 * step))

    def unapply(self, third):
        """Inverse of the single global field: fixed-point iteration with a
        guarded Newton polish (the field is smooth, so this converges
        wherever det stays positive - the normal case by construction);
        outside the solved grid the tent inverse is exact and 1-D."""
        if self._grid is None:
            return (third[0], third[1])
        grid = self._grid
        step = 0.25 * min(grid["du"], grid["dv"])

        def solve_from(seed, iterations):
            """Guarded iteration from the FIRST step: a bare fixed point
            diverges wherever the fit-weighted field's gradient exceeds
            one (i.e. inside every strong band), so each candidate -
            Newton when the local Jacobian cooperates, damped fixed point
            otherwise - is accepted only if it does not worsen the
            residual."""
            x = seed
            fx, fy = self.apply(x)
            best = (math.hypot(fx - third[0], fy - third[1]), x)
            for _ in range(iterations):
                rx = fx - third[0]
                ry = fy - third[1]
                res = math.hypot(rx, ry)
                if res < 1e-9:
                    return (res, x)
                jxx, jxy, jyx, jyy = self.jacobian(x, step)
                det = jxx * jyy - jxy * jyx
                if abs(det) >= 1e-9:
                    cand = (x[0] - (jyy * rx - jxy * ry) / det,
                            x[1] - (jxx * ry - jyx * rx) / det)
                else:
                    cand = (x[0] - rx, x[1] - ry)
                scale = 1.0
                accepted = False
                for _damp in range(5):
                    trial = (x[0] + (cand[0] - x[0]) * scale,
                             x[1] + (cand[1] - x[1]) * scale)
                    tfx, tfy = self.apply(trial)
                    tres = math.hypot(tfx - third[0], tfy - third[1])
                    if tres < res:
                        x, fx, fy = trial, tfx, tfy
                        if tres < best[0]:
                            best = (tres, x)
                        accepted = True
                        break
                    scale *= 0.5
                if not accepted:
                    break
            return best

        # Seed with the tent inverse; when that lands OUTSIDE the grid the
        # true preimage may still be inside (the flow field reaches past
        # the tent), so the seed is clamped in and the iteration - whose
        # probes go through apply(), defined everywhere - decides.
        seed = self._tent_invert(third)
        lo_x, hi_x = grid["x0"], grid["x0"] + grid["nx"] * grid["du"]
        lo_y, hi_y = grid["y0"], grid["y0"] + grid["ny"] * grid["dv"]
        clamped = (min(max(seed[0], lo_x), hi_x),
                   min(max(seed[1], lo_y), hi_y))
        best = solve_from(clamped, 60)
        if best[0] > 1e-6 and clamped != seed:
            candidate = solve_from(seed, 30)
            if candidate[0] < best[0]:
                best = candidate
        if best[0] > 1e-6:
            # Multi-seed retry (the old warp's lesson): jitter around the
            # displacement-corrected seed and keep the best.
            base = (third[0] - (self.apply(clamped)[0] - clamped[0]),
                    third[1] - (self.apply(clamped)[1] - clamped[1]))
            spread = 2.0 * max(grid["du"], grid["dv"])
            for ox, oy in ((0.0, 0.0), (spread, 0.0), (-spread, 0.0),
                           (0.0, spread), (0.0, -spread)):
                candidate = solve_from((base[0] + ox, base[1] + oy), 30)
                if candidate[0] < best[0]:
                    best = candidate
                if best[0] < 1e-6:
                    break
        return best[1]

    def det_sign(self, third):
        """Sign of det(grad U) - +1 almost always (Poisson smoothing keeps
        the field orientation-preserving); read from the solved nodes'
        dets when it is not, so orientation and fold_loci agree."""
        if self._grid is None or self._all_positive:
            return 1
        value = self._bilinear(self._grid["det"], third)
        if value is None:
            return 1
        # Same tolerance as _all_positive and the retreat: once one node
        # genuinely folds, a graze in (-TOL, 0) elsewhere must not start
        # reading as a second fold.
        return 1 if value >= -self.DET_FOLD_TOL else -1

    def face_at(self, third):
        return 0

    def fold_loci(self, samples=48):
        """det = 0 contours of the solved field, in Third space - marched
        from the same per-node det values det_sign reads. Empty for an
        orientation-preserving field (the normal case). Fresh list copies
        per call: consumers register curves by id()."""
        if self._grid is None or self._all_positive:
            return []
        if self._loci is None:
            grid = self._grid
            det = grid["det"]
            nx, ny = grid["nx"], grid["ny"]
            du, dv = grid["du"], grid["dv"]
            x0, y0 = grid["x0"], grid["y0"]

            def edge_zero(va, vb, ax, ay, bx, by):
                t = va / (va - vb) if va != vb else 0.5
                return (ax + (bx - ax) * t, ay + (by - ay) * t)

            # March the det = -DET_FOLD_TOL level set: the same threshold
            # _all_positive, the retreat and det_sign use, so a graze in
            # (-TOL, 0) can neither flip a verdict nor grow a locus.
            tol = self.DET_FOLD_TOL
            segments = []
            for i in range(nx):
                for j in range(ny):
                    corners = ((det[i][j] + tol, x0 + du * i, y0 + dv * j),
                               (det[i + 1][j] + tol,
                                x0 + du * (i + 1), y0 + dv * j),
                               (det[i + 1][j + 1] + tol, x0 + du * (i + 1),
                                y0 + dv * (j + 1)),
                               (det[i][j + 1] + tol,
                                x0 + du * i, y0 + dv * (j + 1)))
                    crossings = []
                    for a in range(4):
                        va, ax_, ay_ = corners[a]
                        vb, bx_, by_ = corners[(a + 1) % 4]
                        if (va < 0.0) != (vb < 0.0):
                            crossings.append(
                                edge_zero(va, vb, ax_, ay_, bx_, by_))
                    if len(crossings) >= 2:
                        segments.append((crossings[0], crossings[1]))

            def key_of(point):
                return (round(point[0], 6), round(point[1], 6))

            links = {}
            for a, b in segments:
                links.setdefault(key_of(a), []).append((a, b))
                links.setdefault(key_of(b), []).append((b, a))
            used = set()
            curves = []
            for a, b in segments:
                if (key_of(a), key_of(b)) in used:
                    continue
                chain = [a, b]
                used.add((key_of(a), key_of(b)))
                used.add((key_of(b), key_of(a)))
                for grow_end in (True, False):
                    while True:
                        tip = chain[-1] if grow_end else chain[0]
                        extended = False
                        for start, far in links.get(key_of(tip), ()):
                            pair = (key_of(start), key_of(far))
                            if pair in used:
                                continue
                            used.add(pair)
                            used.add((pair[1], pair[0]))
                            if grow_end:
                                chain.append(far)
                            else:
                                chain.insert(0, far)
                            extended = True
                            break
                        if not extended:
                            break
                if len(chain) >= 2:
                    curves.append(chain)
            self._loci = curves
        return [list(curve) for curve in self._loci]


def build_mapper(child_h_spec, child_v_spec, main_h_spec, main_v_spec, info=None,
                 additional_pairs=None):
    """Build point mapper from the child frame to the main frame.

    Each guide spec is a bare point list (polyline guide - chord arithmetic,
    bit-identical to the previous implementation) or an asset dict whose
    optional "commands" carry the stroke's real mixed line/Bezier path, on
    which the frame then evaluates with Gauss-Legendre arc tables (_Curve).

    Returns (map_point, width_scale) or (None, reason).

    STAGED FORM (user request 2026-08-24): Third space enters from the very
    start. Both boards go through the SAME reconstruction function
    `_Frame.hv(l_h, l_v) = H(l_h) + V(l_v) - O`, and the map is the pure
    Child -> Third -> Main composition with NO residual term:

        Phi(p) = hv_main( s * warp( hv_child^{-1}(p) ) )

    where hv_child^{-1} is the Newton lift into Third space (arc-length
    coordinates from the cheap chord-basis seed) and `s` the per-side
    endpoint-anchored scales. The pre-2026-08-24 formula carried the extra
    residual `p - hv_child(l_h, l_v)`: zero wherever the inverse converged,
    and a silent glue term everywhere the child frame FOLDED - it kept the
    map "defined" on ground where no UV coordinate exists, smearing the
    pattern across the fold instead of hiding it. That fudge is gone. Where
    the lift does not exist the point is INVALID and the topology is severed
    (see third_of and _sever_source): strokes are cut at the fold line, the
    unreachable stretch is dropped, and the pattern visually wraps out of
    sight - 2D topology severing standing in for 3D occlusion. Dropping the
    residual also drops one hv evaluation per mapped point and the whole
    residual bookkeeping from every downstream consumer.

    Two properties inherited from the decoupled form (2026-08-10) survive
    unchanged on the computable sheet:
      * IDENTITY. Draw the same guides on both boards and Phi is the
        identity: exact for straight guides (the chord seed IS the lift) and
        to the Newton tolerance for curved ones - the two hv terms are the
        same function at the same argument, so nothing is left to cancel.
      * The child guides' curvature actually participates. Only their two
        endpoints did before 2026-08 (`eh`/`ev` were half-chords), so a
        curved child center line silently discarded its shape AND injected
        that shape as an off-axis displacement.

    Parametrization is still ENDPOINT-ANCHORED: the crossing splits each guide
    into two sides and each child side maps proportionally onto the matching
    main side, so endpoints land on endpoints and the crossing on the crossing.
    Differing side ratios still fold strokes that cross a guide (info dict, if
    given, receives h/v_scale_mismatch). Stroke width uses one global
    geometric-mean scale - an approximation once the sides differ.
    """
    def spec_points(spec):
        return spec["points"] if isinstance(spec, dict) else spec

    child_h_points = spec_points(child_h_spec)
    child_v_points = spec_points(child_v_spec)
    main_h_points = spec_points(main_h_spec)
    main_v_points = spec_points(main_v_spec)
    if min(len(child_h_points), len(child_v_points), len(main_h_points), len(main_v_points)) < 2:
        return None, "a center line has fewer than 2 points"

    child = _Frame(child_h_spec, child_v_spec)
    main = _Frame(main_h_spec, main_v_spec)
    if child.h_total <= 1e-9 or child.v_total <= 1e-9:
        return None, "child center lines are degenerate"
    if main.h_total <= 1e-9 or main.v_total <= 1e-9:
        return None, "main center lines are degenerate"

    # The chord basis is no longer the coordinate system, but it still seeds
    # the inverse - and a near-parallel child pair has no usable frame at all,
    # so keep rejecting it here (same angle test, same message as before).
    eh = ((child_h_points[-1][0] - child_h_points[0][0]) * 0.5,
          (child_h_points[-1][1] - child_h_points[0][1]) * 0.5)
    ev = ((child_v_points[-1][0] - child_v_points[0][0]) * 0.5,
          (child_v_points[-1][1] - child_v_points[0][1]) * 0.5)
    det = eh[0] * ev[1] - eh[1] * ev[0]
    axis_sin = abs(det) / max(1e-9, math.hypot(eh[0], eh[1]) * math.hypot(ev[0], ev[1]))
    if axis_sin < 0.05:  # ~3 degrees
        return None, "child center lines are (nearly) parallel"
    child_h_chord = max(2.0 * math.hypot(eh[0], eh[1]), 1e-6)
    child_v_chord = max(2.0 * math.hypot(ev[0], ev[1]), 1e-6)

    h_scale_neg, h_scale_pos = _transfer_scales(child.h_side_raw, child.h_total, main.h_side)
    v_scale_neg, v_scale_pos = _transfer_scales(child.v_side_raw, child.v_total, main.v_side)

    if info is not None:
        info["h_scale_mismatch"] = (max(h_scale_neg, h_scale_pos)
                                    / max(1e-9, min(h_scale_neg, h_scale_pos)))
        info["v_scale_mismatch"] = (max(v_scale_neg, v_scale_pos)
                                    / max(1e-9, min(v_scale_neg, v_scale_pos)))
        # Handedness: the map is direction-faithful on both axes (child start
        # -> main start, end -> end), so the result is a MIRROR image exactly
        # when the two frames have opposite orientation. Both sides now use
        # the tangents at their own crossing, which is the same quantity for
        # both boards instead of chord-vs-tangent as before.
        child_cross_h, child_cross_v = child.jacobian(0.0, 0.0)
        main_cross_h, main_cross_v = main.jacobian(0.0, 0.0)
        child_cross = child_cross_h[0] * child_cross_v[1] - child_cross_h[1] * child_cross_v[0]
        main_cross = main_cross_h[0] * main_cross_v[1] - main_cross_h[1] * main_cross_v[0]
        info["mirrored"] = (child_cross > 0.0) != (main_cross > 0.0)

    # The child crossing's handedness anchors the severing verdict, exactly
    # as fold_reference anchors front/back: a child frame drawn with V
    # clockwise of H has negative det EVERYWHERE without folding once, so
    # "folded" can only mean "flipped RELATIVE TO THE CROSSING". Windowed
    # directions, matching _orientation - raw per-sample tangents of a
    # hand-drawn guide jitter enough to flip a marginal verdict many times
    # within a few px, which would shred strokes into micro-islands.
    _hand_h, _hand_v = child.directions(0.0, 0.0)
    child_hand = 1.0 if (_hand_h[0] * _hand_v[1]
                         - _hand_h[1] * _hand_v[0]) >= 0.0 else -1.0

    # can_fold is defined BEFORE _lift on purpose: _lift's plateau-retry
    # guard calls it, and build_mapper itself runs _lift while rebuilding
    # an additional line's Third coordinates - binding the gate later made
    # that path a NameError (a pink line without a cached third, crossing
    # plateau ground, killed the whole mapper build).
    _fold_gate = {}

    def can_fold():
        """Can the child frame fold ANYWHERE? Cached frame-global gate.

        Min over all pairs of flattened guide segment directions of the
        signed sin(angle) against the crossing handedness. RAW directions on
        purpose - jitter only makes the gate more conservative (it opens the
        per-point severing machinery, whose verdicts are then windowed and
        stable). Above the margin no cell can flip, hv_child is a global
        homeomorphism onto its (linearly extended) sweep, and severing
        short-circuits to "one island" without a single extra solve - in
        particular straight guides never pay anything.

        The chords alone are NOT the whole field the verdict samples: the
        exact one-sided tangents at a sharp corner (tangent_at's clamp
        jumps to them) and the exact end tangents (point_at extends
        linearly along them beyond the guide) can both leave the chord
        cone - a 120-degree turn confined to one chord-short terminal
        cubic passed the chord gate at +0.50 while the windowed cell
        genuinely reached -0.36, silently disabling severing on a frame
        that folds. Those few extra directions join the sets; adding
        directions only ever OPENS the gate, so the conservative claim
        survives.
        """
        cached = _fold_gate.get("value")
        if cached is not None:
            return cached

        def gate_dirs(curve):
            dirs = []
            for a, b in zip(curve.points, curve.points[1:]):
                length = math.hypot(b[0] - a[0], b[1] - a[1])
                if length > 1e-9:
                    dirs.append(((b[0] - a[0]) / length,
                                 (b[1] - a[1]) / length))
            eps = 1e-6
            extremes = [0.0, curve.total]
            extremes.extend(getattr(curve, "sharp_arcs", None) or ())
            for arc in extremes:
                for side in (arc - eps, arc + eps):
                    dirs.append(curve.dir_at(side))
            return dirs

        h_dirs = gate_dirs(child.gh)
        v_dirs = gate_dirs(child.gv)
        lowest = None
        for hx, hy in h_dirs:
            for vx, vy in v_dirs:
                value = (hx * vy - hy * vx) * child_hand
                if lowest is None or value < lowest:
                    lowest = value
        result = lowest is None or lowest <= _SEVER_GATE_SIN
        _fold_gate["value"] = result
        return result

    def _lift(point, seed=None):
        """Newton lift Child -> Third, with the achieved residual.

        On an UNFOLDABLE frame (can_fold() False) hv_child is a
        homeomorphism, so a residual above _SEVER_RESIDUAL is a numeric
        plateau, not a missing preimage - retry once from the stalled
        iterate, which reaches it in practice. The retry lives HERE so
        every consumer of the lift (map_point, coords, third_of - and
        through them the 3D drape and additional-line sync) sees the same
        coordinate; patching only map_point left main_of_third(coords(p))
        disagreeing with map_point(p) on exactly the plateau points.
        Foldable frames never retry: there a high residual means severed
        ground and the stall IS the verdict."""
        if seed is not None:
            return child.solve_full(point, seed[0], seed[1])
        dx = point[0] - child.origin[0]
        dy = point[1] - child.origin[1]
        guess_h = (dx * ev[1] - dy * ev[0]) / det * 0.5 * child_h_chord
        guess_v = (eh[0] * dy - eh[1] * dx) / det * 0.5 * child_v_chord
        l_h, l_v, residual = child.solve_full(point, guess_h, guess_v)
        if residual > _SEVER_RESIDUAL and not can_fold():
            l_h, l_v, residual = child.solve_full(point, l_h, l_v)
        return l_h, l_v, residual

    def coords(point, seed=None):
        """A point's arc-length coordinates in the child frame (= Third).

        Seeded with the cheap chord-basis estimate, which is exact for
        straight child guides - that is why the whole mapper reduces to the
        previous implementation there. An explicit `seed` (child arcs)
        overrides it: on a FOLDED frame the solve is branch-ambiguous, and
        the caller may know which preimage it means (additional-line
        redraws seed from the replaced line's stored coordinates).
        """
        l_h, l_v, _residual = _lift(point, seed)
        return l_h, l_v

    def third_of(point, seed=None):
        """The Third-space lift plus its VALIDITY verdict: (l_h, l_v, valid).

        A point is valid when its UV coordinate is actually computable:
          * the Newton lift converged (residual <= _SEVER_RESIDUAL px) - a
            diverging lift means the point has no preimage on the reachable
            sheet at all; and
          * det J of the child frame at the lift keeps the crossing's sign -
            det <= 0 (relative to the crossing's handedness) is a foldover,
            where hv_child stops being injective and the same canvas point
            carries the front AND the back of the sheet.
        Invalid points are where the topology gets severed (_sever_source):
        they are dropped, never mapped, because no residual term exists to
        fudge them any more.
        """
        l_h, l_v, residual = _lift(point, seed)
        if residual > _SEVER_RESIDUAL:
            return l_h, l_v, False
        t_h, t_v = child.directions(l_h, l_v)
        cell = t_h[0] * t_v[1] - t_h[1] * t_v[0]
        return l_h, l_v, cell * child_hand > 0.0

    # The MAIN frame's inverse, mirror of coords: needed to carry points
    # drawn on the main board (additional lines) into Third space.
    m_eh = ((main_h_points[-1][0] - main_h_points[0][0]) * 0.5,
            (main_h_points[-1][1] - main_h_points[0][1]) * 0.5)
    m_ev = ((main_v_points[-1][0] - main_v_points[0][0]) * 0.5,
            (main_v_points[-1][1] - main_v_points[0][1]) * 0.5)
    m_det = m_eh[0] * m_ev[1] - m_eh[1] * m_ev[0]
    m_h_chord = max(2.0 * math.hypot(m_eh[0], m_eh[1]), 1e-6)
    m_v_chord = max(2.0 * math.hypot(m_ev[0], m_ev[1]), 1e-6)

    def main_coords(point, seed=None):
        if seed is not None:
            return main.solve(point, seed[0], seed[1])
        dx = point[0] - main.origin[0]
        dy = point[1] - main.origin[1]
        if abs(m_det) > 1e-9:
            guess_h = (dx * m_ev[1] - dy * m_ev[0]) / m_det * 0.5 * m_h_chord
            guess_v = (m_eh[0] * dy - m_eh[1] * dx) / m_det * 0.5 * m_v_chord
        else:
            guess_h = guess_v = 0.0
        return main.solve(point, guess_h, guess_v)

    def scale_arcs(l_h, l_v):
        """Third (child-arc) coordinates -> main arcs."""
        return (l_h * (h_scale_pos if l_h >= 0.0 else h_scale_neg),
                l_v * (v_scale_pos if l_v >= 0.0 else v_scale_neg))

    def unscale_arcs(arc_h, arc_v):
        """Main arcs -> Third (child-arc) coordinates; scales are positive,
        so the per-side selection is sign-consistent both ways."""
        return (arc_h / (h_scale_pos if arc_h >= 0.0 else h_scale_neg),
                arc_v / (v_scale_pos if arc_v >= 0.0 else v_scale_neg))

    # The additional (pink) lines' influence field, built in Third space
    # from each pair's two boards as ONE global flow field (user spec
    # 2026-08-25, _FlowFieldWarp): lines guide the texture's flow
    # direction, all lines blend into a single target gradient field, and
    # a Poisson solve integrates it - drawing order no longer matters.
    # A line's STORED Third polyline is authoritative when present: on a
    # folded frame the inverse solve is branch-ambiguous, and re-deriving
    # a synced partner through it was measured to fabricate a 278 px
    # delta on a pair the user never edited. Solving happens only for
    # legacy assets that carry no coordinates. None when there are no
    # pairs - and then every code path below is arithmetic-identical to
    # the warp-free mapper.
    warp = None
    additional_notes = []
    if additional_pairs:
        thirds = []
        for line_index, (child_item, main_item) in enumerate(additional_pairs):
            child_item = child_item or {}
            main_item = main_item or {}
            child_points = [tuple(p) for p in child_item.get("points") or []]
            main_points = [tuple(p) for p in main_item.get("points") or []]
            if len(child_points) < 2 or len(main_points) < 2:
                continue
            child_third = [tuple(t) for t in child_item.get("third") or []]
            if len(child_third) != len(child_points):
                child_third = [coords(p) for p in child_points]
            main_third = [tuple(t) for t in main_item.get("third") or []]
            if len(main_third) != len(main_points):
                main_third = [unscale_arcs(*main_coords(p)) for p in main_points]
            # BRANCH-CONTINUITY CLIP: a line drawn across a fold edge of
            # its frame has no continuous Third lift - the solve jumps
            # branches mid-line and cross-branch deltas are meaningless.
            # Keep the window (canvas arc fractions, jump-proof and
            # commensurate across the two boards) where BOTH sides are
            # continuous; drop the rest, loudly.
            windows = [_branch_window(child_points, child_third),
                       _branch_window(main_points, main_third)]
            if any(w is None for w in windows):
                additional_notes.append(
                    f"additional line {line_index + 1} has no continuous "
                    "Third lift (it crosses a fold edge of its frame) - "
                    "it cannot shape the mapping and was skipped")
                continue
            f_lo = max(w[0] for w in windows)
            f_hi = min(w[1] for w in windows)
            if f_lo > 1e-9 or f_hi < 1.0 - 1e-9:
                if f_hi - f_lo < 1e-6:
                    additional_notes.append(
                        f"additional line {line_index + 1} crosses fold "
                        "edges everywhere - it was skipped")
                    continue
                child_third = _slice_third_by_canvas_fraction(
                    child_points, child_third, f_lo, f_hi)
                main_third = _slice_third_by_canvas_fraction(
                    main_points, main_third, f_lo, f_hi)
                additional_notes.append(
                    f"additional line {line_index + 1} crosses a fold edge "
                    f"of its frame; only its longest continuous stretch "
                    f"({100.0 * (f_hi - f_lo):.0f}% of the drawn length) "
                    "shapes the mapping")
            if len(child_third) < 2 or len(main_third) < 2:
                continue
            thirds.append((child_third, main_third, line_index))
        if thirds:
            candidate = _FlowFieldWarp(
                thirds, _ADDITIONAL["falloff"],
                _ADDITIONAL["radius_factor"],
                frame_window=((-child.h_arc, child.h_total - child.h_arc),
                              (-child.v_arc, child.v_total - child.v_arc)))
            warp = candidate if candidate.pairs else None
            if warp is not None:
                additional_notes.extend(warp.notes)

    def main_of_third(third):
        """Forward projection Third -> Main: warp, per-side scale, rebuild.

        Pure forward arithmetic - no Newton - so a stored UV island projects
        into the main view at O(log n) per point. This is Step 4 of the
        staged pipeline; everything before it (lift, severing) happens in
        child/Third space and never needs to run again for a projection.
        """
        l_h, l_v = third
        if warp is not None:
            l_h, l_v = warp.apply((l_h, l_v))
        return main.hv(l_h * (h_scale_pos if l_h >= 0.0 else h_scale_neg),
                       l_v * (v_scale_pos if l_v >= 0.0 else v_scale_neg))

    def map_point(point):
        """Child -> Third -> Main, literally. No residual term on a frame
        that can fold: the lift IS the coordinate, and where it does not
        exist the caller severs.

        On an UNFOLDABLE frame severing is off, so a Newton lift that
        stalls on a plateau (a near-parallel frame, ground far outside the
        guides) has nobody to drop it - and emitting the stalled iterate
        verbatim was measured to drift an identity mapping by 113 px where
        the residual formula was bit-exact. The preimage does exist there
        (hv_child is a homeomorphism above the gate): _lift retries from
        the stalled iterate (shared with every other consumer), and if a
        residual still remains this one place absorbs it with the exact
        identity-preserving correction. Foldable frames never take this
        branch - there a high residual means severed ground, and the fudge
        is exactly what this pipeline removed."""
        l_h, l_v, residual = _lift(point)
        if residual > _SEVER_RESIDUAL and not can_fold():
            rebuilt = child.hv(l_h, l_v)
            image = main_of_third((l_h, l_v))
            return (image[0] + point[0] - rebuilt[0],
                    image[1] + point[1] - rebuilt[1])
        return main_of_third((l_h, l_v))

    def inverse_point(point):
        """Main-board point -> child-board point through the full mapping
        (frame inverse, then the warp's fixed-point inverse). Exact wherever
        both frame solves converge; used to sync additional lines."""
        third = unscale_arcs(*main_coords(point))
        if warp is not None:
            third = warp.unapply(third)
        return child.hv(*third)

    # Arc lengths on both sides now, so a curved child guide scales widths by
    # its real length rather than by its chord.
    width_scale = math.sqrt((main.h_total / child.h_total) * (main.v_total / child.v_total))

    map_point.coords = coords
    map_point.third_of = third_of
    map_point.main_of_third = main_of_third
    map_point.can_fold = can_fold
    map_point.main_coords = main_coords
    map_point.scale_arcs = scale_arcs
    map_point.unscale_arcs = unscale_arcs
    map_point.inverse = inverse_point
    map_point.warp = warp
    map_point.additional_notes = additional_notes
    map_point.child_frame = child
    map_point.main_frame = main
    map_point.h_scales = (h_scale_neg, h_scale_pos)
    map_point.v_scales = (v_scale_neg, v_scale_pos)
    # Front/back is measured against the crossing, so the orientation there is
    # the anchor (see _fold_sign). Computed once here rather than per point.
    map_point.fold_reference = 1
    map_point.fold_reference = _orientation(map_point, child.origin) or 1
    return map_point, width_scale


# ---------------------------------------------------------------------------
# mapping area geometry (region polygons -> polyline clipping)
# ---------------------------------------------------------------------------

def _command_point(value):
    return (float(value["x"]), float(value["y"]))


def _path_commands_to_polygons(commands):
    """Sample path commands (move/line/cubic) into closed polygon point lists."""
    polygons = []
    current = []

    def flush():
        if len(current) >= 3:
            polygons.append(list(current))

    for command in commands or []:
        kind = command.get("type")
        if kind == "move":
            flush()
            current = [_command_point(command["to"])]
        elif kind == "line":
            if not current and "from" in command:
                current.append(_command_point(command["from"]))
            current.append(_command_point(command["to"]))
        elif kind == "cubic":
            if not current and "from" in command:
                current.append(_command_point(command["from"]))
            if not current:
                continue
            # Evaluation via the shared wheel pyfile/bezier.py; the 6px /
            # 4..24 density is this sampler's own policy.
            cub = (current[-1],
                   _command_point(command["control1"]),
                   _command_point(command["control2"]),
                   _command_point(command["to"]))
            samples = max(4, min(24, int(math.ceil(bezier.hull_length(cub) / 6.0))))
            for step in range(1, samples + 1):
                current.append(_cubic_point(cub, step / samples))
    flush()
    return polygons


def _point_in_polygons(point, polygons):
    """Even-odd containment test over all polygons (each implicitly closed)."""
    inside = False
    px, py = point
    for polygon in polygons:
        j = len(polygon) - 1
        for i in range(len(polygon)):
            xi, yi = polygon[i]
            xj, yj = polygon[j]
            if (yi > py) != (yj > py):
                cross = xj + (py - yj) * (xi - xj) / (yi - yj)
                if px < cross:
                    inside = not inside
            j = i
    return inside


def _segment_polygon_crossings(a, b, polygons):
    """Parameters t in (0,1) where segment a->b crosses a polygon edge.

    Exact: one 2x2 solve per edge. The previous implementation only tested
    the polyline's own vertices and bisected between an inside and an outside
    sample, so the clip resolution was the SAMPLING density - a region
    narrower than the vertex spacing, or a stroke clipping a corner between
    two samples, was simply never seen.
    """
    abx, aby = b[0] - a[0], b[1] - a[1]
    if abx * abx + aby * aby <= 1e-24:
        return []
    crossings = []
    for polygon in polygons:
        count = len(polygon)
        for index in range(count):
            e0 = polygon[index]
            e1 = polygon[(index + 1) % count]
            ex, ey = e1[0] - e0[0], e1[1] - e0[1]
            den = abx * ey - aby * ex
            if abs(den) <= 1e-12:
                continue  # parallel (a grazing overlap changes no inside/outside run)
            wx, wy = e0[0] - a[0], e0[1] - a[1]
            t = (wx * ey - wy * ex) / den
            u = (wx * aby - wy * abx) / den
            if 0.0 < t < 1.0 and -1e-12 <= u <= 1.0 + 1e-12:
                crossings.append(t)
    return crossings


def _clip_runs(a, b, polygons):
    """Sub-intervals [t0, t1] of segment a->b with a definite inside/outside.

    Yields (t0, t1, inside). Between two consecutive crossings the segment
    cannot change side, so one midpoint test decides the whole run.
    """
    bounds = sorted(set(_segment_polygon_crossings(a, b, polygons)))
    bounds = [0.0] + bounds + [1.0]
    for t0, t1 in zip(bounds, bounds[1:]):
        if t1 - t0 <= 1e-12:
            continue
        mid = (t0 + t1) * 0.5
        point = (a[0] + (b[0] - a[0]) * mid, a[1] + (b[1] - a[1]) * mid)
        yield t0, t1, _point_in_polygons(point, polygons)


def _clip_polyline(points, polygons):
    """Split a polyline into the pieces that lie inside the polygons."""
    if not polygons:
        return [points]
    pieces = []
    current = []
    for a, b in zip(points, points[1:]):
        for t0, t1, inside in _clip_runs(a, b, polygons):
            if not inside:
                if len(current) >= 2:
                    pieces.append(current)
                current = []
                continue
            if not current:
                current.append((a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0))
            current.append((a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1))
    if len(current) >= 2:
        pieces.append(current)
    return pieces


# ---------------------------------------------------------------------------
# curve reconstruction (spline / bezier modes)
#
# The whole point of these helpers is that build_mapper.map_point (W below) is
# NON-LINEAR: W(straight source segment) is a curve, so joining mapped points
# with straight lines (the old "polyline" mode) drops the distortion between
# points. W is only PIECEWISE smooth (arc-length lookup on polyline guides is
# C0 at guide vertices; the per-side scales jump across the guide crossing), so
# every routine below is subdivision-based with a depth cap and never assumes a
# globally smooth map: a genuine tangent kink just makes the segments there
# small instead of sending the recursion to infinity.
# ---------------------------------------------------------------------------

def _dist(a, b):
    return math.hypot(a[0] - b[0], a[1] - b[1])


def _mid(a, b):
    return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)


# Exact bezier primitives come from the shared wheel pyfile/bezier.py (the
# mirror of algorithm/beziersplit.h) - do not re-derive them here. The
# aliases keep this module's historical names for its many call sites.
_lerp = bezier.lerp
_cubic_point = bezier.eval_cubic
_split_cubic = bezier.cubic_segment
_line_cubic = bezier.line_cubic


def _flatten_cubic(cub, out, step=POLY_STEP):
    """Append samples of one cubic to `out` (excluding its start point)."""
    net = bezier.hull_length(cub)
    samples = max(2, min(64, int(math.ceil(net / max(0.5, step)))))
    for k in range(1, samples + 1):
        out.append(_cubic_point(cub, k / samples))


def _rdp(points, eps):
    """Ramer-Douglas-Peucker decimation; keeps the polyline within eps."""
    if len(points) < 3:
        return list(points)
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        first, last = stack.pop()
        if last <= first + 1:
            continue
        ax, ay = points[first]
        bx, by = points[last]
        dx, dy = bx - ax, by - ay
        seg = math.hypot(dx, dy)
        worst = -1.0
        split = -1
        for i in range(first + 1, last):
            px, py = points[i]
            if seg <= 1e-12:
                d = math.hypot(px - ax, py - ay)
            else:
                # Distance to the SEGMENT, not to the infinite line through it.
                # The map folds wherever the two side scales differ, and a
                # folded-back point sits near that infinite line while being
                # far from the segment - the line metric deleted it silently.
                # Repro: _rdp([(0,0),(120,.1),(-40,-.1),(10,0)], 0.3) used to
                # collapse to the endpoints, a 110 px error.
                t = ((px - ax) * dx + (py - ay) * dy) / (seg * seg)
                t = 0.0 if t < 0.0 else (1.0 if t > 1.0 else t)
                d = math.hypot(px - (ax + dx * t), py - (ay + dy * t))
            if d > worst:
                worst = d
                split = i
        if worst > eps and split > 0:
            keep[split] = True
            stack.append((first, split))
            stack.append((split, last))
    return [points[i] for i in range(len(points)) if keep[i]]


def _catmull_rom_cubics(knots, alpha=_CATMULL_ALPHA):
    """Interpolating centripetal Catmull-Rom spline as a list of cubics.

    Centripetal (alpha=0.5) is used on purpose: the knots come out of RDP and
    are therefore unevenly spaced, and uniform Catmull-Rom overshoots / self-
    intersects on uneven knots while the centripetal form never does.
    """
    n = len(knots)
    if n < 2:
        return []
    if n == 2:
        return [_line_cubic(knots[0], knots[1])]

    ext = [knots[0]] + list(knots) + [knots[-1]]
    cubics = []
    for i in range(1, len(ext) - 2):
        p0, p1, p2, p3 = ext[i - 1], ext[i], ext[i + 1], ext[i + 2]
        t0 = 0.0
        t1 = t0 + max(_dist(p0, p1) ** alpha, 1e-9)
        t2 = t1 + max(_dist(p1, p2) ** alpha, 1e-9)
        t3 = t2 + max(_dist(p2, p3) ** alpha, 1e-9)
        # Hermite tangents at p1, p2 (non-uniform), scaled to this segment.
        def tangent(pa, pb, pc, ta, tb, tc):
            m1x = (pb[0] - pa[0]) / (tb - ta) - (pc[0] - pa[0]) / (tc - ta) + (pc[0] - pb[0]) / (tc - tb)
            m1y = (pb[1] - pa[1]) / (tb - ta) - (pc[1] - pa[1]) / (tc - ta) + (pc[1] - pb[1]) / (tc - tb)
            return (m1x * (t2 - t1), m1y * (t2 - t1))
        m1 = tangent(p0, p1, p2, t0, t1, t2)
        m2 = tangent(p1, p2, p3, t1, t2, t3)
        c1 = (p1[0] + m1[0] / 3.0, p1[1] + m1[1] / 3.0)
        c2 = (p2[0] - m2[0] / 3.0, p2[1] - m2[1] / 3.0)
        cubics.append((p1, c1, c2, p2))
    return cubics


def _structural_knots(map_point, a, b):
    """Parameters in (0,1) where the map stops being affine along a->b.

    The map is H(l) + V(l) rebuilt by arc length on both boards, so its
    kinks sit where a coordinate crosses a guide SEGMENT BOUNDARY - on the
    child frame (where the coordinates themselves break) or, after the
    per-side scale, on the main frame. Those are the samples flatness probes
    are worst at finding: a source segment can span whole periods of a wavy
    guide and land every probe back on the chord (the aliasing case in the
    spec). Feeding them in directly costs one coordinate solve per endpoint
    and removes the guesswork; the adaptive probes stay as the safety net for
    everything these do not cover (a non-injective child frame, where the
    coordinates are only a best effort and this whole argument breaks down).

    On POLYLINE guides the map is exactly affine between these knots. On
    CURVE guides (kept commands) it is smooth-but-curved inside a segment, so
    the knots degrade from "the only possible kinks" to "the candidate
    positions curvature concentrates at" - the adaptive probes and the fold
    split's midpoint sampling carry the rest, and each source piece is at
    most one POLY_STEP long, which bounds what a between-knot wiggle can hide.
    """
    coords = getattr(map_point, "coords", None)
    if coords is None:
        return []
    child, main = map_point.child_frame, map_point.main_frame
    start, end = coords(a), coords(b)

    knots = []
    for axis, (l0, l1) in enumerate(zip(start, end)):
        if abs(l1 - l0) <= 1e-12:
            continue
        if axis == 0:
            own_cum, own_arc = child.gh.knots, child.h_arc
            far_cum, far_arc = main.gh.knots, main.h_arc
            scale_neg, scale_pos = map_point.h_scales
        else:
            own_cum, own_arc = child.gv.knots, child.v_arc
            far_cum, far_arc = main.gv.knots, main.v_arc
            scale_neg, scale_pos = map_point.v_scales

        targets = [value - own_arc for value in own_cum[1:-1]]
        for value in far_cum[1:-1]:
            offset = value - far_arc
            scale = scale_pos if offset >= 0.0 else scale_neg
            if abs(scale) > 1e-12:
                targets.append(offset / scale)

        lo, hi = (l0, l1) if l0 < l1 else (l1, l0)
        for target in targets:
            if lo < target < hi:
                knots.append((target - l0) / (l1 - l0))
    knots = sorted({t for t in knots if 1e-9 < t < 1.0 - 1e-9})
    if len(knots) > _MAX_KNOTS_PER_SPAN:
        # Safety valve for a folded child frame, where the coordinates run
        # far and sweep hundreds of guide vertices inside one source segment.
        # Thin them evenly rather than dropping the tail; the probes still
        # cover whatever this misses.
        stride = len(knots) / _MAX_KNOTS_PER_SPAN
        knots = [knots[min(len(knots) - 1, int(i * stride))]
                 for i in range(_MAX_KNOTS_PER_SPAN)]
    return knots


def _flatness_recurse(sample, dlerp, result, tol, max_depth):
    """The shared flatness recursion of _adaptive_map_polyline and
    _project_third_cubic: subdivide until the image chord passes the
    three-term test - forced maximum output chord, midpoint deviation, and
    the golden-section probe that stops a straight source aliasing through
    whole periods of a wavy guide. `sample` maps a domain value to the
    image; `dlerp` interpolates the DOMAIN (2D child points for the
    polyline sampler, scalar t for a Third cubic). Inserted samples are
    appended to `result` flagged non-anchor."""
    def recurse(a, b, wa, wb, depth):
        if depth >= max_depth:
            return
        m = dlerp(a, b, 0.5)
        wm = sample(m)
        g = dlerp(a, b, _PROBE_T)
        wg = sample(g)
        if (_dist(wa, wb) > _FORCE_STEP
                or _dist(wm, _mid(wa, wb)) > tol
                or _dist(wg, _lerp(wa, wb, _PROBE_T)) > tol):
            recurse(a, m, wa, wm, depth + 1)
            result.append((wm, False))
            recurse(m, b, wm, wb, depth + 1)
    return recurse


def _adaptive_map_polyline(map_point, points, tol=_CURVE_TOL, max_depth=_SPLINE_MAX_DEPTH):
    """Map `points` through the warp, inserting samples between the ORIGINAL
    vertices so the mapped polyline stays within `tol` of the true warped
    curve.

    Returns [(mapped_point, is_original), ...]. The original vertices are
    anchors: downstream decimation only touches the inserted samples, never
    them. The flatness guards live in _flatness_recurse, shared with the
    bridge projector so the two samplers cannot drift apart.
    """
    if len(points) < 2:
        return [(map_point(p), True) for p in points]

    result = [(map_point(points[0]), True)]
    recurse = _flatness_recurse(map_point, _lerp, result, tol, max_depth)

    wa = result[0][0]
    for i in range(len(points) - 1):
        a, b = points[i], points[i + 1]
        wb = map_point(b)
        # Split the ORIGINAL interval at the map's own kinks first, then let
        # the probes refine whatever is left inside each smooth piece.
        previous, w_previous = a, wa
        for t in _structural_knots(map_point, a, b):
            knot = _lerp(a, b, t)
            w_knot = map_point(knot)
            recurse(previous, knot, w_previous, w_knot, 0)
            result.append((w_knot, False))
            previous, w_previous = knot, w_knot
        recurse(previous, b, w_previous, wb, 0)
        result.append((wb, True))
        wa = wb
    return result


def _clip_flagged(flagged, polygons):
    """_clip_polyline for (point, is_original) pairs; boundary cuts are anchors."""
    if not polygons:
        return [list(flagged)]
    pieces = []
    current = []
    for first, second in zip(flagged, flagged[1:]):
        a, b = first[0], second[0]
        for t0, t1, inside in _clip_runs(a, b, polygons):
            if not inside:
                if len(current) >= 2:
                    pieces.append(current)
                current = []
                continue
            if not current:
                head = first if t0 <= 0.0 else ((a[0] + (b[0] - a[0]) * t0,
                                                 a[1] + (b[1] - a[1]) * t0), True)
                current.append(head)
            tail = second if t1 >= 1.0 else ((a[0] + (b[0] - a[0]) * t1,
                                              a[1] + (b[1] - a[1]) * t1), True)
            current.append(tail)
    if len(current) >= 2:
        pieces.append(current)
    return pieces


def _decimate_between_anchors(flagged, eps):
    """RDP each run of inserted samples between two anchors; keep every anchor.

    RDP always keeps its endpoints, and each span's endpoints are the mapped
    original vertices, so the artist's points survive verbatim while the dense
    warp samples collapse back to the few knots the curvature needs.
    """
    knots = [flagged[0][0]]
    span = [flagged[0][0]]
    for point, is_anchor in flagged[1:]:
        span.append(point)
        if is_anchor:
            knots.extend(_rdp(span, eps)[1:])
            span = [point]
    if len(span) >= 2:  # tolerate a trailing non-anchor tail
        knots.extend(_rdp(span, eps)[1:])
    return knots


def _directional_image(map_point, base, ctrl):
    """Image of the handle vector (ctrl - base) under the warp at `base`.

    A one-sided derivative in the handle's OWN direction, not a full Jacobian:
    it never straddles the anchor's tangent kink, needs no matrix inverse (so a
    singular / anisotropic warp is harmless), and is first-order exact - the end
    tangent of a cubic is 3*(ctrl-base), and a smooth map sends a tangent vector
    v to (D_v W), so the 1/3 handle maps to base' + D_(ctrl-base) W.
    """
    vx, vy = ctrl[0] - base[0], ctrl[1] - base[1]
    length = math.hypot(vx, vy)
    wb = map_point(base)
    if length <= 1e-9:
        return wb
    eps = min(_JAC_EPS, 0.25 * length)
    ux, uy = vx / length, vy / length
    ahead = map_point((base[0] + ux * eps, base[1] + uy * eps))
    # derivative * length = image of the full handle vector
    return (wb[0] + (ahead[0] - wb[0]) / eps * length,
            wb[1] + (ahead[1] - wb[1]) / eps * length)


def _warp_cubic(map_point, cub, tol=_CURVE_TOL, max_depth=_BEZIER_MAX_DEPTH, depth=0):
    """Transport one source cubic through the warp, subdividing on error.

    Returns a list of output cubics whose union approximates W(cub). The handle
    transport is only first-order, so wherever the warp bends hard (or crosses a
    tangent kink) the transported cubic is checked at t=1/4,1/2,3/4 against the
    true warped point and the SOURCE cubic is bisected until it fits or the depth
    cap is hit (which also stops runaway recursion at a genuine discontinuity).
    """
    p0, c1, c2, p3 = cub
    w0 = map_point(p0)
    w3 = map_point(p3)
    out_c1 = _directional_image(map_point, p0, c1)
    out_c2 = _directional_image(map_point, p3, c2)
    out = (w0, out_c1, out_c2, w3)

    # Probe density scales with the OUTPUT size: a long cubic spanning whole
    # periods of a wavy guide can slip through any small fixed probe set. The
    # golden-section probe additionally breaks periodic alignment. Source
    # anchors always survive as output anchors (subdividing only ADDS knots),
    # matching the "originals are never decimated" rule of spline mode.
    net = bezier.hull_length(out)
    probes = max(3, min(17, int(math.ceil(net / _FORCE_STEP))))
    ts = [(k + 1.0) / (probes + 1.0) for k in range(probes)] + [_PROBE_T]
    worst = 0.0
    for t in ts:
        worst = max(worst, _dist(map_point(_cubic_point(cub, t)), _cubic_point(out, t)))
    if worst <= tol or depth >= max_depth:
        return [out]
    # One shared-wheel split (pyfile/bezier.py) yields both halves at once.
    left, right = bezier.split_cubic(cub, 0.5)
    return (_warp_cubic(map_point, left, tol, max_depth, depth + 1)
            + _warp_cubic(map_point, right, tol, max_depth, depth + 1))


# R^3 twins of the exact 2D primitives. They live HERE, not in bezier.py:
# that file mirrors algorithm/beziersplit.h bit-exactly and must not grow
# Python-only members. Only evaluation-side helpers need a z - the SOURCE
# side of a 3D warp stays 2D, so splitting keeps using bezier.split_cubic.

def _dist3(a, b):
    return math.sqrt((a[0] - b[0]) ** 2 + (a[1] - b[1]) ** 2
                     + (a[2] - b[2]) ** 2)


def _lerp3(a, b, t):
    return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t,
            a[2] + (b[2] - a[2]) * t)


def _cubic_point3(cub, t):
    p0, c1, c2, p3 = cub
    a = _lerp3(p0, c1, t)
    b = _lerp3(c1, c2, t)
    c = _lerp3(c2, p3, t)
    d = _lerp3(a, b, t)
    e = _lerp3(b, c, t)
    return _lerp3(d, e, t)


def _hull_length3(cub):
    p0, c1, c2, p3 = cub
    return _dist3(p0, c1) + _dist3(c1, c2) + _dist3(c2, p3)


def _directional_image_3d(map3, base, ctrl):
    """3D image of the handle vector (ctrl - base) under a lifted warp.

    The codomain-R^3 twin of _directional_image: the same one-sided
    derivative in the handle's OWN direction, at the same ABSOLUTE step
    min(_JAC_EPS, 0.25 * length). The step must stay at that scale: map3
    composes the Newton lift (residual plateau up to _SEVER_RESIDUAL near
    folds), the piecewise-affine hv (POLY_STEP treads) and a piecewise-
    linear z off the mesh, and a shrunken step reads those artifacts raw
    while 1/eps scales them up into the handle.
    """
    vx, vy = ctrl[0] - base[0], ctrl[1] - base[1]
    length = math.hypot(vx, vy)
    wb = map3(base)
    if length <= 1e-9:
        return wb
    eps = min(_JAC_EPS, 0.25 * length)
    ux, uy = vx / length, vy / length
    ahead = map3((base[0] + ux * eps, base[1] + uy * eps))
    return (wb[0] + (ahead[0] - wb[0]) / eps * length,
            wb[1] + (ahead[1] - wb[1]) / eps * length,
            wb[2] + (ahead[2] - wb[2]) / eps * length)


def _warp_cubic_3d(map3, cub, tol=_CURVE_TOL, max_depth=_BEZIER_MAX_DEPTH,
                   depth=0):
    """Transport one SOURCE (2D) cubic through a lifted warp into R^3.

    _warp_cubic with a z: identical control flow, probe set and bisection -
    in particular the output-length-scaled probe count plus the golden-
    section probe. A single midpoint probe cannot stand in for them: any
    odd-symmetric deviation (a full guide period, an S across one surface
    wave) is exactly zero at t=0.5 and would be accepted at depth 0 with
    no second chance. Returns [(out_cubic3, source_sub_cubic)] so callers
    can trace every leaf back to its source parameters.
    """
    p0, c1, c2, p3 = cub
    w0 = map3(p0)
    w3 = map3(p3)
    out_c1 = _directional_image_3d(map3, p0, c1)
    out_c2 = _directional_image_3d(map3, p3, c2)
    out = (w0, out_c1, out_c2, w3)

    net = _hull_length3(out)
    probes = max(3, min(17, int(math.ceil(net / _FORCE_STEP))))
    ts = [(k + 1.0) / (probes + 1.0) for k in range(probes)] + [_PROBE_T]
    worst = 0.0
    for t in ts:
        worst = max(worst, _dist3(map3(_cubic_point(cub, t)),
                                  _cubic_point3(out, t)))
    if worst <= tol or depth >= max_depth:
        return [(out, cub)]
    left, right = bezier.split_cubic(cub, 0.5)
    return (_warp_cubic_3d(map3, left, tol, max_depth, depth + 1)
            + _warp_cubic_3d(map3, right, tol, max_depth, depth + 1))


def _rdp_indices3d(points, tol, protect=()):
    """Indices _rdp_polyline3d would keep, with `protect`ed indices pinned.

    Protected indices (the original Bezier anchors of a draped stroke) both
    survive and bound the spans, so decimation never straddles - or drops -
    an anchor the snapshot metadata points at.
    """
    n = len(points)
    if n <= 2:
        return list(range(n))
    keep = [False] * n
    keep[0] = keep[-1] = True
    marks = sorted({0, n - 1, *(k for k in protect if 0 <= k < n)})
    for k in marks:
        keep[k] = True
    stack = list(zip(marks, marks[1:]))
    while stack:
        lo, hi = stack.pop()
        if hi - lo < 2:
            continue
        ax, ay, az = points[lo]
        dx = points[hi][0] - ax
        dy = points[hi][1] - ay
        dz = points[hi][2] - az
        norm2 = dx * dx + dy * dy + dz * dz
        worst = -1.0
        pick = None
        for k in range(lo + 1, hi):
            rx = points[k][0] - ax
            ry = points[k][1] - ay
            rz = points[k][2] - az
            if norm2 <= 1e-12:
                d2 = rx * rx + ry * ry + rz * rz
            else:
                t = (rx * dx + ry * dy + rz * dz) / norm2
                t = min(max(t, 0.0), 1.0)
                ex = rx - t * dx
                ey = ry - t * dy
                ez = rz - t * dz
                d2 = ex * ex + ey * ey + ez * ez
            if d2 > worst:
                worst = d2
                pick = k
        if pick is not None and worst > tol * tol:
            keep[pick] = True
            stack.append((lo, pick))
            stack.append((pick, hi))
    return [k for k in range(n) if keep[k]]


def _anchor_key(point):
    """1/64-px position key for tracking anchors through clip/sever cuts."""
    return (round(point[0] * 64.0), round(point[1] * 64.0))


def _commands_to_subpaths(commands):
    """Parse stroke `commands` into subpaths, each a list of cubic tuples.

    line -> a straight cubic; quad -> its exact cubic elevation; cubic -> as is.
    A "move" starts a new subpath so genuinely separate subpaths never get a
    bogus connecting segment (the flaw in _stroke_points that concatenates all
    polylines of a stroke into one list).
    """
    subpaths = []
    current = []
    start = None
    for command in commands or []:
        kind = command.get("type")
        if kind == "move":
            if current:
                subpaths.append(current)
            current = []
            start = _command_point(command["to"])
        elif kind == "line":
            a = start if start is not None else (
                _command_point(command["from"]) if "from" in command else None)
            b = _command_point(command["to"])
            if a is not None:
                current.append(_line_cubic(a, b))
            start = b
        elif kind == "quad":
            a = start if start is not None else (
                _command_point(command["from"]) if "from" in command else None)
            ctrl = _command_point(command["control"])
            b = _command_point(command["to"])
            if a is not None:
                # Elevation via the shared wheel pyfile/bezier.py.
                current.append(bezier.quad_cubic(a, ctrl, b))
            start = b
        elif kind == "cubic":
            a = start if start is not None else (
                _command_point(command["from"]) if "from" in command else None)
            c1 = _command_point(command["control1"])
            c2 = _command_point(command["control2"])
            b = _command_point(command["to"])
            if a is not None:
                current.append((a, c1, c2, b))
            start = b
    if current:
        subpaths.append(current)
    return subpaths


def _bernstein_cubic_roots(b0, b1, b2, b3):
    """Roots in (0, 1) of the cubic with these Bernstein coefficients.

    Isolation instead of a closed form: the derivative is a quadratic whose
    roots split [0, 1] into at most three MONOTONE pieces, and a monotone
    piece holds at most one root, found by bisection to machine precision.
    Robust for the double/triple roots a tangential touch produces, where
    Cardano loses most of its digits.
    """
    c0 = b0
    c1 = 3.0 * (b1 - b0)
    c2 = 3.0 * (b2 - 2.0 * b1 + b0)
    c3 = b3 - 3.0 * b2 + 3.0 * b1 - b0

    def value(t):
        return ((c3 * t + c2) * t + c1) * t + c0

    breaks = [0.0, 1.0]
    qa, qb, qc = 3.0 * c3, 2.0 * c2, c1
    if abs(qa) > 1e-14:
        disc = qb * qb - 4.0 * qa * qc
        if disc > 0.0:
            root = math.sqrt(disc)
            for extremum in ((-qb - root) / (2.0 * qa), (-qb + root) / (2.0 * qa)):
                if 0.0 < extremum < 1.0:
                    breaks.append(extremum)
    elif abs(qb) > 1e-14:
        extremum = -qc / qb
        if 0.0 < extremum < 1.0:
            breaks.append(extremum)
    breaks.sort()

    roots = []
    for lo, hi in zip(breaks, breaks[1:]):
        flo, fhi = value(lo), value(hi)
        if flo == 0.0 and 0.0 < lo < 1.0:
            roots.append(lo)
            continue
        if flo * fhi > 0.0 or flo == fhi:
            continue
        a, b = lo, hi
        for _ in range(60):
            mid = (a + b) * 0.5
            if value(a) * value(mid) <= 0.0:
                b = mid
            else:
                a = mid
        root = (a + b) * 0.5
        if 0.0 < root < 1.0:
            roots.append(root)
    return roots


def _cubic_polygon_crossings(cub, polygons):
    """Parameters t in (0,1) where a cubic crosses a polygon edge.

    Exact, like _segment_polygon_crossings: the signed distance of the cubic
    to an edge's line is itself a cubic whose Bernstein coefficients are just
    that distance evaluated at the four control points, so the convex-hull
    test rejects most edges without any root finding.
    """
    crossings = []
    for polygon in polygons:
        count = len(polygon)
        for index in range(count):
            e0 = polygon[index]
            e1 = polygon[(index + 1) % count]
            ex, ey = e1[0] - e0[0], e1[1] - e0[1]
            extent = ex * ex + ey * ey
            if extent <= 1e-18:
                continue
            side = [(point[0] - e0[0]) * ey - (point[1] - e0[1]) * ex for point in cub]
            if min(side) > 0.0 or max(side) < 0.0:
                continue  # whole curve on one side of this edge's line
            for t in _bernstein_cubic_roots(*side):
                hit = _cubic_point(cub, t)
                along = ((hit[0] - e0[0]) * ex + (hit[1] - e0[1]) * ey) / extent
                if -1e-12 <= along <= 1.0 + 1e-12:
                    crossings.append(t)
    return crossings


def _clip_cubics(cubics, polygons):
    """Split a subpath of cubics into the runs that lie inside `polygons`.

    Mirrors _clip_polyline's semantics but keeps curve segments: the crossing
    parameters are SOLVED (cubic-vs-edge), not sampled, and de Casteljau
    splitting extracts the inside sub-cubics. Runs that stay inside across a
    cubic boundary are kept as one piece.
    """
    if not polygons:
        return [list(cubics)]

    pieces = []
    current = []
    for cub in cubics:
        bounds = sorted({t for t in _cubic_polygon_crossings(cub, polygons)
                         if 1e-9 < t < 1.0 - 1e-9})
        bounds = [0.0] + bounds + [1.0]
        for t0, t1 in zip(bounds, bounds[1:]):
            if t1 - t0 <= 1e-9:
                continue
            if not _point_in_polygons(_cubic_point(cub, (t0 + t1) * 0.5), polygons):
                if current:
                    pieces.append(current)
                    current = []
                continue
            current.append(_split_cubic(cub, t0, t1))
    if current:
        pieces.append(current)
    return pieces


def _cubics_to_commands(cubics):
    """(command list for make_stroke_object_from_path, dense flattening)."""
    if not cubics:
        return [], []
    commands = [{"type": "move", "to": {"x": cubics[0][0][0], "y": cubics[0][0][1]}}]
    flat = [cubics[0][0]]
    for _, c1, c2, p3 in cubics:
        commands.append({
            "type": "cubic",
            "control1": {"x": c1[0], "y": c1[1]},
            "control2": {"x": c2[0], "y": c2[1]},
            "to": {"x": p3[0], "y": p3[1]},
        })
    for cub in cubics:
        _flatten_cubic(cub, flat)
    return commands, flat


# ---------------------------------------------------------------------------
# mapping asset dict + overlay display
# ---------------------------------------------------------------------------

def _direction_arrow_points(points, size):
    """Arrowhead polyline [wing, tip, wing] at a guide's END point.

    The mapping is direction-sensitive: child guide start/end maps onto main
    guide start/end, so drawing a main center line in the opposite direction
    deliberately flips the texture along that axis. The arrow makes the drawn
    direction visible. The tangent is smoothed over a window because raw
    hand-drawn end segments jitter (same smoothing as _tangent_at_arc's
    other consumers).
    """
    cumulative = _cumulative_lengths(points)
    total = cumulative[-1]
    if total <= 1e-6:
        return None
    window = max(2.0 * POLY_STEP, 0.05 * total)
    dx, dy = _tangent_at_arc(points, cumulative, total, window)
    tip = points[-1]
    back_x, back_y = -dx, -dy
    spread = math.radians(28.0)
    cos_s, sin_s = math.cos(spread), math.sin(spread)
    wing1 = (tip[0] + size * (back_x * cos_s - back_y * sin_s),
             tip[1] + size * (back_x * sin_s + back_y * cos_s))
    wing2 = (tip[0] + size * (back_x * cos_s + back_y * sin_s),
             tip[1] + size * (-back_x * sin_s + back_y * cos_s))
    return [wing1, tip, wing2]


_MAPPER_CACHE = {"key": None, "mapper": None}


def _mapper_fingerprint(child_assets, main_assets, pairs):
    """A cheap content key for _current_mapper's cache: the guide point
    lists, the pair geometry/authority, and the tool options that shape
    the mapping. Self-validating - no invalidation hooks to forget."""
    parts = []
    for assets in (child_assets, main_assets):
        for prop in GUIDE_PROPERTIES:
            item = assets.get(prop) or {}
            # commands by CONTENT: in curve mode the whole guide geometry
            # is the commands - reducing them to a bool served a stale
            # mapper when only the curve handles changed.
            parts.append((prop, tuple(map(tuple, item.get("points") or ())),
                          repr(item.get("commands"))))
    for child_item, main_item in pairs or []:
        for item in (child_item, main_item):
            item = item or {}
            parts.append((item.get("id"),
                          tuple(map(tuple, item.get("points") or ())),
                          tuple(map(tuple, item.get("third") or ()))))
    parts.append((_ADDITIONAL["falloff"], _ADDITIONAL["radius_factor"],
                  _ADDITIONAL.get("constrain_outline", True)))
    return hash(tuple(parts))


def _current_mapper():
    """The mapper for the guide assets as they stand, or (None, why-not).

    CACHED by content fingerprint: guide drags rebuild the overlay per
    mouse move, and with a flow-field warp present an uncached rebuild is
    a full Poisson solve (~hundreds of ms) per frame."""
    child_assets = _assets_for("child")
    main_assets = _assets_for("main")
    for view, assets in (("child", child_assets), ("main", main_assets)):
        missing = [ITEM_LABELS[prop] for prop in GUIDE_PROPERTIES if prop not in assets]
        if missing:
            return None, f"the {view} view is missing: {', '.join(missing)}"
    if not _polylines_cross(child_assets[H_PROPERTY]["points"],
                            child_assets[V_PROPERTY]["points"]):
        return None, "the child center lines do not cross"
    if not _polylines_cross(main_assets[H_PROPERTY]["points"],
                            main_assets[V_PROPERTY]["points"]):
        return None, "the main center lines do not cross"
    pairs = _additional_pairs()
    key = _mapper_fingerprint(child_assets, main_assets, pairs)
    if _MAPPER_CACHE["key"] == key and _MAPPER_CACHE["mapper"] is not None:
        return _MAPPER_CACHE["mapper"], ""
    mapper, _ = build_mapper(
        child_assets[H_PROPERTY], child_assets[V_PROPERTY],
        main_assets[H_PROPERTY], main_assets[V_PROPERTY],
        # The preview must show the mapping the RUN will use - with the
        # additional-line pairs. Without them the face-down bands sat at
        # the unwarped position (measured 6-7% of the domain mis-tinted).
        additional_pairs=pairs)
    if mapper is None:
        return None, "the mapper refuses these guides"
    _MAPPER_CACHE["key"] = key
    _MAPPER_CACHE["mapper"] = mapper
    return mapper, ""


def _occlusion_overlay_items():
    """Filled bands over the child regions the current mapping turns face-down.

    The domain is the child guide rectangle (both guides' full arc spans) in
    ARC coordinates, so no Newton inversion is ever needed: the fold sign at
    a grid cell is the same determinant test as _orientation, evaluated
    directly at the cell's arcs. Guide tangents are also cached per grid
    row/column - they only depend on one coordinate each - which turns the
    sweep into plain arithmetic (measured: the naive per-cell version spent
    its whole budget re-fetching the same smoothed tangents).

    Output: per scan row, each face-down run becomes one filled quad
    (bottom edge + reversed top edge), so adjacent rows tile without the
    seams or cap-bulges polyline bands would leave.
    """
    if not _occlusion_enabled():
        return []
    cached = _OCCLUSION_CACHE["items"]
    if cached is not None:
        return cached

    _OCCLUSION_CACHE["note"] = ""
    _OCCLUSION_CACHE["share"] = 0.0
    items = []
    mapper, note = _current_mapper()
    if mapper is None:
        _OCCLUSION_CACHE["note"] = note
        _OCCLUSION_CACHE["items"] = items
        return items

    child = mapper.child_frame
    main = mapper.main_frame
    h_lo, h_hi = -child.h_arc, child.h_cum[-1] - child.h_arc
    v_lo, v_hi = -child.v_arc, child.v_cum[-1] - child.v_arc
    if h_hi - h_lo <= 1e-6 or v_hi - v_lo <= 1e-6:
        _OCCLUSION_CACHE["note"] = "a guide has no length"
        _OCCLUSION_CACHE["items"] = items
        return items

    rows = 64
    cols = 160
    row_step = (v_hi - v_lo) / rows
    col_step = (h_hi - h_lo) / cols
    reference = mapper.fold_reference

    def tangents_h(arc_h):
        scale = mapper.h_scales[1] if arc_h >= 0.0 else mapper.h_scales[0]
        return (scale,
                child.gh.tangent_at(child.h_arc + arc_h, child.h_window),
                main.gh.tangent_at(main.h_arc + arc_h * scale, main.h_window))

    def tangents_v(arc_v):
        scale = mapper.v_scales[1] if arc_v >= 0.0 else mapper.v_scales[0]
        return (scale,
                child.gv.tangent_at(child.v_arc + arc_v, child.v_window),
                main.gv.tangent_at(main.v_arc + arc_v * scale, main.v_window))

    warp = getattr(mapper, "warp", None)

    def fold_sign(h_cache, v_cache, arc_h=None, arc_v=None):
        scale_h, (tcx, tcy), (tmx, tmy) = h_cache
        scale_v, (ncx, ncy), (nmx, nmy) = v_cache
        denominator = tcx * ncy - tcy * ncx
        if abs(denominator) < 1e-12:
            return 1  # folded child frame: no usable orientation, same as _orientation
        warp_sign = 1
        if warp is not None and arc_h is not None:
            # The additional-line warp moves the fold field's child-space
            # preimage; the cached main tangents are unwarped, so re-fetch
            # them at the warped arcs - the same route _orientation takes,
            # including the warp's own fold parity.
            warp_sign = warp.det_sign((arc_h, arc_v))
            w_h, w_v = warp.apply((arc_h, arc_v))
            scale_h = mapper.h_scales[1] if w_h >= 0.0 else mapper.h_scales[0]
            scale_v = mapper.v_scales[1] if w_v >= 0.0 else mapper.v_scales[0]
            tmx, tmy = main.gh.tangent_at(main.h_arc + w_h * scale_h,
                                          main.h_window)
            nmx, nmy = main.gv.tangent_at(main.v_arc + w_v * scale_v,
                                          main.v_window)
        value = warp_sign * scale_h * scale_v * (tmx * nmy - tmy * nmx) / denominator
        raw = 1 if value > 0.0 else -1
        return 1 if raw == reference else -1

    columns = [tangents_h(h_lo + (index + 0.5) * col_step) for index in range(cols)]
    back_arc = 0.0
    for row in range(rows):
        arc_v = v_lo + (row + 0.5) * row_step
        v_cache = tangents_v(arc_v)

        def sign_at(arc_h):
            return fold_sign(tangents_h(arc_h), v_cache, arc_h, arc_v)

        # cell-centred signs from the cached column tangents
        signs = [fold_sign(h_cache, v_cache,
                           h_lo + (index + 0.5) * col_step, arc_v)
                 for index, h_cache in enumerate(columns)]
        runs = []
        start = h_lo if signs[0] < 0 else None
        for index in range(cols - 1):
            if signs[index] == signs[index + 1]:
                continue
            lo_b = h_lo + (index + 0.5) * col_step
            hi_b = lo_b + col_step
            for _ in range(12):
                mid = (lo_b + hi_b) * 0.5
                if sign_at(mid) == signs[index]:
                    lo_b = mid
                else:
                    hi_b = mid
            boundary = (lo_b + hi_b) * 0.5
            if signs[index] < 0:
                runs.append((start, boundary))
                start = None
            else:
                start = boundary
        if start is not None:
            runs.append((start, h_hi))

        bottom = arc_v - 0.5 * row_step
        top = arc_v + 0.5 * row_step
        for a0, a1 in runs:
            if a1 - a0 <= 1e-6:
                continue
            back_arc += a1 - a0
            count = max(2, int(round((a1 - a0) / (2.0 * POLY_STEP))) + 1)
            arcs = [a0 + (a1 - a0) * k / (count - 1) for k in range(count)]
            polygon = ([child.hv(a, bottom) for a in arcs]
                       + [child.hv(a, top) for a in reversed(arcs)])
            items.append({
                "id": OCCLUSION_BUTTON,
                "points": polygon,
                "closed": True,
                "color": (0, 0, 0, 0),
                "fill_color": (*_display_color("back_color")[:3],
                                int(_LINE_DISPLAY.get("occlusion_alpha", 70))),
                "width": 0.1,
                "removable": False,
            })

    _OCCLUSION_CACHE["share"] = back_arc / ((h_hi - h_lo) * rows)
    _OCCLUSION_CACHE["items"] = items
    return items


def overlay_items(view_name):
    """This view's mapping overlay items (guides + arrows + area + grid).

    Public so other tools (e.g. repulsion_tool's drag preview) can COMPOSE
    their own items with the mapping display instead of clobbering it -
    ui.set_overlay replaces a view's whole item list.

    UNIT MODE (the document has automapping layers): everything here is
    focus-gated - no active unit means no mapping overlays at all, and the
    active unit's Advanced Settings decide which components draw. A document
    with no units keeps the legacy always-on behaviour.
    """
    settings = None
    if _UNIT_META:
        if _ACTIVE_UNIT["id"] is None:
            return []
        settings = _unit_settings()

    def wanted(key):
        return settings is None or bool(settings.get(key, True))

    assets = _assets_for(view_name)
    items = []
    for prop, key in ((H_PROPERTY, "h"), (V_PROPERTY, "v")):
        if not wanted(f"show_{key}"):
            continue
        guide = assets.get(prop)
        if guide and len(guide.get("points") or []) >= 2:
            # Colour, width and style come from the display settings now, not
            # from the stroke that happened to draw the guide.
            color = _display_color(f"{key}_color")
            width = float(_LINE_DISPLAY.get(f"{key}_width", 3.0))
            style = _display_style(f"{key}_style")
            items.append({
                "id": prop,
                "points": guide["points"],
                "color": color,
                "width": width,
                "pen_style": style,
                "removable": True,
                # Grabbable under any tool (C++ routes a draggable item's
                # press through the same "handle" events an edit handle
                # uses): placing an axis is a drag, not a redraw.
                "draggable": True,
            })
            arrow = _direction_arrow_points(guide["points"], max(12.0, 3.5 * width))
            if arrow:
                items.append({
                    "id": prop + "_arrow",
                    "points": arrow,
                    "color": color,
                    "width": width,
                    # The arrow says which way the guide runs; dashing it
                    # would make a short mark nearly invisible.
                    "removable": False,
                })
    additional_lines = ((assets.get(ADDITIONAL_PROPERTY) or {}).get("lines")
                        if wanted("show_additional") else None)
    for index, line in enumerate(additional_lines or []):
        if len(line.get("points") or []) < 2:
            continue
        items.append({
            # The pair id rides in the overlay id so removing one line can
            # remove the PAIR (both boards) rather than guessing by geometry.
            "id": f"{ADDITIONAL_PROPERTY}:{_line_id(line, index)}",
            "points": line["points"],
            "draggable": True,
            "color": _display_color("additional_color"),
            "width": float(_LINE_DISPLAY.get("additional_width", 2.5)),
            "pen_style": _display_style("additional_style"),
            "removable": True,
        })
    area = assets.get(MAPPING_AREA_PROPERTY) if wanted("show_area") else None
    if area:
        for polygon in area.get("polygons") or []:
            items.append({
                "id": MAPPING_AREA_PROPERTY,
                "points": polygon,
                "closed": True,
                "color": AREA_BORDER_COLOR,
                "fill_color": AREA_FILL_COLOR,
                "width": 1.5,
                "removable": True,
            })
    try:
        items.extend(_grid_overlay_items(view_name))
    except Exception as error:
        print(f"[auto_mapping] refer rect grid skipped: {error}")
    if view_name == "child":
        try:
            # UNDER the guides and area, so the tint never covers them.
            items[:0] = _occlusion_overlay_items()
        except Exception as error:
            print(f"[auto_mapping] occlusion preview skipped: {error}")
    if view_name == "main" and wanted("show_nearest"):
        try:
            # ON TOP of the guides: the anchor is the one thing here you grab.
            items.extend(_nearest_overlay_items())
        except Exception as error:
            print(f"[auto_mapping] nearest point overlay skipped: {error}")
    return items


def _push_overlay(view_name):
    """Send this view's mapping assets to the generic C++ overlay display."""
    try:
        overlay_stack.set_items(view_name, "auto_mapping", overlay_items(view_name))
    except Exception as error:
        print(f"[auto_mapping] overlay update failed: {error}")


def _set_draw_color(color):
    """Arm this tool's colour THROUGH the per-tool cache: setting it
    directly repainted whatever tool came next (pyfile/tool_colors.py)."""
    try:
        import tool_colors
        tool_colors.apply(color)
    except Exception:
        try:
            _animean().ui.set_draw_color(color)
        except Exception:
            pass


def _view_frame(view_name):
    """The reference frame of ONE board, from that board's own axes.

    Independent by construction: the texture's frame needs the texture's
    axes and nothing else, and the main board's needs the main board's. The
    refer grid used to build the main board's from the CHILD frame pushed
    through the mapper, which made a grid on one board unavailable whenever
    the OTHER board had no axes yet - the common state while a setup is only
    half drawn, and exactly when a reference rectangle is most wanted.
    """
    assets = _assets_for(view_name)
    if H_PROPERTY not in assets or V_PROPERTY not in assets:
        return None
    if (len(assets[H_PROPERTY].get("points") or []) < 2
            or len(assets[V_PROPERTY].get("points") or []) < 2):
        return None
    frame = _Frame(assets[H_PROPERTY], assets[V_PROPERTY])
    if frame.h_total <= 1e-6 or frame.v_total <= 1e-6:
        return None
    return frame


def _child_frame():
    """The child reference frame the MAPPER actually uses.

    This used to build a straight chord frame from the guides' end points,
    which disagreed with the mapper as soon as a guide was curved: the
    coordinates are arc length measured from the CROSSING, not a projection
    onto the start-to-end chord. Measured on sine guides, the nine anchors
    were off by 16 / 40 / 82 px at amplitude 10 / 30 / 60, and the anchor the
    grid calls "the H guide's end point" missed that end point by up to 30 px.
    Since the grid exists to reveal a wrong mapping at a glance, it has to be
    built out of the same frame the mapping is.
    """
    return _view_frame("child")


def _frame_point(frame, u_hat, v_hat):
    """Child-space point at normalised grid coords (u_hat, v_hat in [-1, 1])."""
    du = u_hat * (frame.h_side[1] if u_hat >= 0.0 else frame.h_side[0])
    dv = v_hat * (frame.v_side[1] if v_hat >= 0.0 else frame.v_side[0])
    return frame.hv(du, dv)


def _sever_edge_image(mapper, valid_p, valid_lift, invalid_p):
    """The main-canvas image of the seam point between a valid and an
    invalid grid sample: bisect the child-space segment on the SAME
    default-seeded verdict the strokes' severing consults, and project the
    valid-side lift. The reference grid's island edges then land ON the
    strokes' cuts, instead of at whatever sample happened to fall nearest
    (measured 42 px median / 79 px max error at the default density).
    Never seed the probes from the valid side: a warm-started solve tracks
    the local sheet straight across a DIVERGENCE gap (the lift there still
    exists for the far branch), reads every midpoint as valid, and the
    bisection collapses onto the invalid sample - the very error this
    helper exists to remove."""
    lo, lift = valid_p, valid_lift
    hi = invalid_p
    for _ in range(20):
        mid = ((lo[0] + hi[0]) * 0.5, (lo[1] + hi[1]) * 0.5)
        l_h, l_v, ok = mapper.third_of(mid)
        if ok:
            lo, lift = mid, (l_h, l_v)
        else:
            hi = mid
    return mapper.main_of_third(lift)


def _grid_overlay_items(view_name):
    """Refer-rect grid: the 3x3 anchor lattice (crossing, 4 guide endpoints,
    4 quadrant corners) with quarter-step iso-lines, for ONE board.

    The CHILD board draws its own frame from its own axes - a pure picture
    of the source coordinate system. The MAIN board draws the RENDERING
    mapping whenever the full mapper can be built (user request: the grid
    must reflect the additional lines' influence): the child lattice pushed
    through the mapper, warp included, so it bends where a pink line shapes
    the mapping and comparing the two boards' grids reads the whole mapping
    by eye. Only when the mapper is unavailable - the normal state while a
    setup is half drawn - does the main board fall back to its own frame,
    so a reference rectangle stays available either way.

    A board with no axes, or with axes that do not cross, has no frame and so
    draws nothing - there is no rectangle to refer to."""
    if not refer_rect_enabled(view_name):
        return []
    cached = _GRID_CACHE.get(view_name)
    if cached is not None:
        return cached

    items = []
    divisions = max(2, _grid_divisions())
    levels = [i / (divisions - 1) * 2.0 - 1.0 for i in range(divisions)]
    count = 6 * (divisions - 1) + 1   # divisions=5 -> 25, the historical density
    samples = [i / (count - 1) * 2.0 - 1.0 for i in range(count)]

    # The MAIN board's grid shows the RENDERING mapping when it exists: the
    # child frame's lattice pushed through the full mapper, additional-line
    # warp included - so the grid visibly bends where a pink line shapes
    # the mapping (user request). Comparing it with the child board's own
    # grid reads the whole mapping by eye. Without a full mapper (the
    # normal state while a setup is half drawn) the board falls back to its
    # own frame below, so the reference rectangle stays available.
    if view_name == "main":
        mapper, _note = _current_mapper()
        if mapper is not None:
            child_frame = mapper.child_frame
            # SEVERED GRID: on a folding child frame the iso lines break
            # where the Third lift stops existing, so the reference grid
            # shows the UV islands the pattern is actually cut into - grid
            # ground with no computable coordinate draws nothing rather
            # than a residual-era smear. The verdict reuses the lift the
            # projection needs anyway, so the folding path costs no extra
            # solve; non-folding frames skip the verdict entirely.
            can_fold = mapper.can_fold()
            for level in levels:
                iso_u = [_frame_point(child_frame, level, s) for s in samples]
                iso_v = [_frame_point(child_frame, s, level) for s in samples]
                for points in (iso_u, iso_v):
                    if can_fold:
                        # Runs end (and begin) at the BISECTED seam, not at
                        # the last sample that happened to be valid: the
                        # boundary points also keep a one-sample island
                        # drawable instead of silently blank.
                        runs = []
                        run = []
                        prev = None   # (point, lift, ok) of the last sample
                        for p in points:
                            l_h, l_v, ok = mapper.third_of(p)
                            if ok:
                                if (not run and prev is not None
                                        and not prev[2]):
                                    run.append(_sever_edge_image(
                                        mapper, p, (l_h, l_v), prev[0]))
                                run.append(mapper.main_of_third((l_h, l_v)))
                            elif run:
                                run.append(_sever_edge_image(
                                    mapper, prev[0], prev[1], p))
                                runs.append(run)
                                run = []
                            prev = (p, (l_h, l_v), ok)
                        if run:
                            runs.append(run)
                    else:
                        runs = [[mapper(p) for p in points]]
                    for run in runs:
                        if len(run) < 2:
                            continue
                        items.append({
                            "id": "refer_rect_grid",
                            "points": run,
                            "color": GRID_COLOR,
                            "width": 1.0,
                            "removable": False,
                        })
            if items:
                _GRID_CACHE[view_name] = items
                return items
            # A fully severed lattice (every iso-line blank) falls through
            # to the own-axes fallback below: the docstring's promise - a
            # reference rectangle stays available either way - outranks
            # showing nothing.

    # This board's own axes, and nothing else: a half-drawn setup still
    # deserves a reference rectangle. Failure paths CACHE their empty
    # result too - overlay_items runs per mouse move during drags, and an
    # uncached miss re-entered _current_mapper (two O(n*m) crossing scans)
    # every time.
    assets = _assets_for(view_name)
    if H_PROPERTY not in assets or V_PROPERTY not in assets:
        _GRID_CACHE[view_name] = []
        return []
    if not _polylines_cross(assets[H_PROPERTY]["points"],
                            assets[V_PROPERTY]["points"]):
        _GRID_CACHE[view_name] = []
        return []
    frame = _view_frame(view_name)
    if frame is None:
        _GRID_CACHE[view_name] = []
        return []

    for level in levels:
        iso_u = [_frame_point(frame, level, s) for s in samples]
        iso_v = [_frame_point(frame, s, level) for s in samples]
        for points in (iso_u, iso_v):
            items.append({
                "id": "refer_rect_grid",
                "points": points,
                "color": GRID_COLOR,
                "width": 1.0,
                "removable": False,
            })
    _GRID_CACHE[view_name] = items
    return items


def _overlays_changed(view_name):
    """Assets changed in view_name: refresh its overlay, and any OTHER view
    whose display genuinely depends on those assets.

    The occlusion tint is drawn on the texture but computed from the MAIN
    guides, so a main-side edit re-pushes the child overlay. The MAIN refer
    grid renders the full mapping (child lattice through the warp), so a
    child-side edit re-pushes the main overlay. The CHILD grid stays a pure
    function of the child's own axes."""
    _invalidate_grid_cache()
    _push_overlay(view_name)
    if _occlusion_enabled() and view_name != "child":
        _push_overlay("child")
    if refer_rect_enabled("main") and view_name != "main":
        _push_overlay("main")


def _main_guide_frame():
    """The MAIN board's own frame (for the nearest-end handle), or None."""
    assets = _assets_for("main")
    if H_PROPERTY not in assets or V_PROPERTY not in assets:
        return None
    if (len(assets[H_PROPERTY].get("points") or []) < 2
            or len(assets[V_PROPERTY].get("points") or []) < 2):
        return None
    try:
        frame = _Frame(assets[H_PROPERTY], assets[V_PROPERTY])
    except Exception:
        return None
    if frame.h_total <= 1e-6 or frame.v_total <= 1e-6:
        return None
    return frame


def _nearest_arc():
    """The nearest-end anchor in MAIN arc coordinates (crossing by default)."""
    item = _assets_for("main").get(NEAREST_PROPERTY) or {}
    arc = item.get("arc") or (0.0, 0.0)
    return float(arc[0]), float(arc[1])


def _nearest_overlay_items():
    """The nearest-end anchor as an OVERLAY OBJECT, same family as the guides.

    A small red ring with the same x badge the center lines carry (removing
    it resets the anchor to the crossing, since the depth default is the
    crossing) - and the one overlay item with `draggable`: C++ routes a drag
    on it through the "handle" hook events with its id, so it moves like a
    stroke's edit handle without ever touching the edit-handle channel that
    the Arrow/Connect sessions own.
    """
    frame = _main_guide_frame()
    if frame is None:
        return []
    arc_h, arc_v = _nearest_arc()
    # A stored anchor can outlive the guides it was set on; clamp it back
    # onto the sheet (point_at extrapolates linearly past the ends, which
    # would float the ring off into empty space).
    arc_h = min(max(arc_h, -frame.h_arc), frame.h_total - frame.h_arc)
    arc_v = min(max(arc_v, -frame.v_arc), frame.v_total - frame.v_arc)
    center = frame.hv(arc_h, arc_v)
    radius = 6.0
    ring = [(center[0] + radius * math.cos(2.0 * math.pi * k / 24),
             center[1] + radius * math.sin(2.0 * math.pi * k / 24))
            for k in range(24)]
    fill = (NEAREST_HANDLE_COLOR[0], NEAREST_HANDLE_COLOR[1],
            NEAREST_HANDLE_COLOR[2], 90)
    return [{
        "id": NEAREST_PROPERTY,
        "points": ring,
        "closed": True,
        "color": NEAREST_HANDLE_COLOR,
        "fill_color": fill,
        "width": 1.5,
        "removable": True,
        "draggable": True,
    }]


def _push_nearest_handle():
    """The anchor lives in the main overlay now; re-push that overlay."""
    _push_overlay("main")


_NEAREST_DRAG = {"frame": None, "moved": False, "offset": (0.0, 0.0),
                 "had_original": False, "original_arc": None}


# Guide/additional-line drags: where the line SITS is a placement, and a
# placement is a drag. The overlay item carries "draggable", C++ reports the
# press/move/release through the handle events, and the geometry is rewritten
# here - the same route the nearest-end anchor already took.
_GUIDE_DRAG = {}


def _guide_drag_event(message):
    overlay_id = str(message.get("handle") or "")
    is_guide = overlay_id in GUIDE_PROPERTIES
    is_additional = overlay_id.startswith(ADDITIONAL_PROPERTY + ":")
    if not is_guide and not is_additional:
        return
    view = message.get("view") or "main"
    phase = message.get("phase")
    position = message.get("position") or {}
    point = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
    assets = _assets_for(view)

    if phase == "press":
        if is_guide:
            item = assets.get(overlay_id)
            points = list(item.get("points") or []) if item else []
        else:
            index, line = _additional_line_by_overlay_id(view, overlay_id)
            points = list(line.get("points") or []) if line else []
        if len(points) < 2:
            return
        _GUIDE_DRAG[view] = {"id": overlay_id, "origin": point,
                             "points": [(float(x), float(y)) for x, y in points],
                             "moved": False}
        return

    if phase == "cancel":
        # A context change (tool/frame/property switch) abandons the drag:
        # restore the persisted baseline the move previews mutated in place,
        # exactly like _nearest_handle_event does for the anchor. Without
        # this, the uncommitted placement would silently become the position
        # the next mapping run uses.
        drag = _GUIDE_DRAG.pop(view, None)
        if drag is None or drag["id"] != overlay_id or not drag["moved"]:
            return
        if is_guide:
            item = assets.get(overlay_id)
            if item is not None:
                item["points"] = [list(p) for p in drag["points"]]
        else:
            _index, line = _additional_line_by_overlay_id(view, overlay_id)
            if line is not None:
                line["points"] = [list(p) for p in drag["points"]]
        _push_overlay(view)
        _invalidate_grid_cache()
        try:
            _animean().ui.refresh()
        except Exception:
            pass
        return

    if phase not in ("move", "release"):
        return
    drag = _GUIDE_DRAG.get(view)
    if drag is None or drag["id"] != overlay_id:
        return
    delta = (point[0] - drag["origin"][0], point[1] - drag["origin"][1])
    if abs(delta[0]) > 1e-9 or abs(delta[1]) > 1e-9:
        drag["moved"] = True
    moved = [(x + delta[0], y + delta[1]) for x, y in drag["points"]]

    if is_guide:
        item = assets.get(overlay_id)
        if item is None:
            return
        item["points"] = moved
        # The stored Bezier commands describe the OLD position; dropping them
        # keeps the guide honest as a polyline rather than drawing it in two
        # places at once. A redraw restores the curve.
        item.pop("commands", None)
    else:
        _index, line = _additional_line_by_overlay_id(view, overlay_id)
        if line is None:
            return
        line["points"] = moved
        # Its Third coordinates describe where it WAS.
        line.pop("third", None)

    if phase == "move":
        _push_overlay(view)
        _invalidate_grid_cache()
        try:
            _animean().ui.refresh()
        except Exception:
            pass
        return

    # release
    _GUIDE_DRAG.pop(view, None)
    if not drag["moved"]:
        _push_overlay(view)
        return
    if is_guide:
        # Third space IS the child arc plane, so a moved axis retires every
        # additional line's stored coordinates on this board.
        _invalidate_additional_thirds(view)
    _save_assets(view)
    _invalidate_grid_cache()
    # BOTH boards: the mapping is a relation between them, so the other
    # board's refer grid and occlusion tint describe the moved guide too and
    # would otherwise keep drawing the old placement.
    _push_overlay("main")
    _push_overlay("child")
    try:
        animean = _animean()
        animean.ui.refresh()
        animean.ui.history_commit("Move Guide" if is_guide
                                  else "Move Additional Line", view)
    except Exception:
        pass
    print(f"[auto_mapping] {overlay_id} moved by "
          f"({delta[0]:.1f}, {delta[1]:.1f})")
    # Editing an axis rebuilt the refer grid above; with unit focus the
    # mapping itself follows, no manual click.
    _maybe_auto_run()


def _additional_line_by_overlay_id(view, overlay_id):
    try:
        pair_id = int(overlay_id.split(":", 1)[1])
    except (IndexError, ValueError):
        return -1, None
    lines = (_assets_for(view).get(ADDITIONAL_PROPERTY) or {}).get("lines") or []
    for index, line in enumerate(lines):
        if _line_id(line, index) == pair_id:
            return index, line
    return -1, None


def _nearest_handle_event(message):
    """Drag the nearest-end anchor; it snaps onto the sheet (arc coords).

    A press only GRABS (the handle accepts clicks up to 7 screen px off
    centre, and applying the press position would teleport the anchor by
    that offset); only actual movement re-solves, and only a drag that moved
    commits history - a stray click must not truncate the redo tail.
    """
    if message.get("view") != "main":
        return
    phase = message.get("phase")
    if phase == "cancel":
        # Moving updates the in-memory overlay before release. A tool/frame
        # switch must restore the persisted baseline instead of leaving an
        # uncommitted anchor that the next mapping run would nevertheless use.
        if _NEAREST_DRAG.get("moved"):
            assets = _assets_for("main")
            if _NEAREST_DRAG.get("had_original"):
                assets[NEAREST_PROPERTY] = {
                    "arc": list(_NEAREST_DRAG.get("original_arc") or (0.0, 0.0))}
            else:
                assets.pop(NEAREST_PROPERTY, None)
            _push_nearest_handle()
        _NEAREST_DRAG["frame"] = None
        _NEAREST_DRAG["moved"] = False
        _NEAREST_DRAG["had_original"] = False
        _NEAREST_DRAG["original_arc"] = None
        _NEAREST_DRAG["offset"] = (0.0, 0.0)
        return
    if message.get("handle") != NEAREST_PROPERTY:
        return
    if phase == "press":
        frame = _main_guide_frame()
        _NEAREST_DRAG["frame"] = frame
        _NEAREST_DRAG["moved"] = False
        original = _assets_for("main").get(NEAREST_PROPERTY)
        original_arc = (original or {}).get("arc")
        _NEAREST_DRAG["had_original"] = bool(original_arc and len(original_arc) >= 2)
        _NEAREST_DRAG["original_arc"] = (
            [float(original_arc[0]), float(original_arc[1])]
            if _NEAREST_DRAG["had_original"] else None)
        # The handle accepts presses up to 7 screen px off centre; remember
        # the miss so the first move does not snap the anchor to the cursor.
        offset = (0.0, 0.0)
        if frame is not None:
            position = message.get("position") or {}
            grabbed = (float(position.get("x", 0.0)), float(position.get("y", 0.0)))
            shown = frame.hv(*_nearest_arc())
            offset = (shown[0] - grabbed[0], shown[1] - grabbed[1])
        _NEAREST_DRAG["offset"] = offset
        return
    if phase not in ("move", "release"):
        return
    frame = _NEAREST_DRAG.get("frame") or _main_guide_frame()
    if frame is None:
        return
    _NEAREST_DRAG["frame"] = frame  # guides cannot change mid-drag: build once
    if phase == "move":
        position = message.get("position") or {}
        offset = _NEAREST_DRAG.get("offset") or (0.0, 0.0)
        point = (float(position.get("x", 0.0)) + offset[0],
                 float(position.get("y", 0.0)) + offset[1])
        current = _nearest_arc()
        l_h, l_v = frame.solve(point, current[0], current[1])
        # Keep the anchor on the sheet the guides actually span.
        l_h = min(max(l_h, -frame.h_arc), frame.h_total - frame.h_arc)
        l_v = min(max(l_v, -frame.v_arc), frame.v_total - frame.v_arc)
        _assets_for("main")[NEAREST_PROPERTY] = {"arc": [l_h, l_v]}
        _NEAREST_DRAG["moved"] = True
        _push_nearest_handle()
        return
    # release
    moved = _NEAREST_DRAG["moved"]
    _NEAREST_DRAG["frame"] = None
    _NEAREST_DRAG["moved"] = False
    _NEAREST_DRAG["had_original"] = False
    _NEAREST_DRAG["original_arc"] = None
    _NEAREST_DRAG["offset"] = (0.0, 0.0)
    if not moved:
        return
    _save_assets("main")
    try:
        _animean().ui.history_commit("Move Nearest Point", "main")
    except Exception:
        pass  # older builds without the history binding
    l_h, l_v = _nearest_arc()
    print(f"[auto_mapping] nearest point at arc ({l_h:.1f}, {l_v:.1f}); "
          "the next Auto Mapping stacks fold layers from here.")
    _maybe_auto_run()


def _detect_region(scene, view_name, frame, seed):
    """Bucket-style region detection, computed in Python via vectorlogic."""
    segments = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return []
    for layer in structure["layers"]:
        if not layer["visible"] or layer["type"] == "fill":
            continue
        # Cheap probe first (no 4px path flattening): stacked mapping runs add
        # a layer per click, and every mapped layer is 100% MAPPED_PROPERTY
        # strokes, so the expensive to_poly fetch would be pure waste there.
        probe = scene.cell_to_dict(layer["index"], frame, False, POLY_STEP)
        if all((stroke.get("property") or "") in MAPPING_OUTPUT_PROPERTIES
               for stroke in probe["image"]["strokes"]):
            continue
        cell = scene.cell_to_dict(layer["index"], frame, True, POLY_STEP)
        for stroke in cell["image"]["strokes"]:
            # A previous mapping result must not act as a region boundary,
            # otherwise the mapped layer shatters the bucket into tiny pieces.
            # ALL of the run's output counts, not just the front layer: the
            # back and crease layers were already walling the bucket off, and
            # the two guide-axis layers would have cut every shape they cross
            # into quadrants.
            if (stroke.get("property") or "") in MAPPING_OUTPUT_PROPERTIES:
                continue
            segments.extend(_stroke_segments(stroke))
    if not segments:
        return []
    if view_name == "child":
        # The child board is an infinite canvas: the detection bounds follow
        # the actual content (plus a generous margin) instead of the nominal
        # page, so a shape drawn or panned anywhere still buckets correctly
        # and is never silently truncated at a page edge.
        bounds_rect = _content_rect(segments, seed)
    else:
        bounds_rect = _canvas_rect(view_name)
    path = _animean().vectorlogic.vector_region_path_at(seed, segments, bounds_rect)
    return _path_commands_to_polygons(path.get("commands"))


def _content_rect(segments, seed):
    xs = [seed[0]]
    ys = [seed[1]]
    for a, b in segments:
        xs.extend((a[0], b[0]))
        ys.extend((a[1], b[1]))
    min_x, max_x = min(xs), max(xs)
    min_y, max_y = min(ys), max(ys)
    pad = max(max_x - min_x, max_y - min_y, 512.0)
    return (min_x - pad, min_y - pad,
            (max_x - min_x) + 2.0 * pad, (max_y - min_y) + 2.0 * pad)


def _absorb_legacy_items(view_name, scene, frame):
    """Migrate guide strokes / area fills that older builds stored in layers.

    Fetched with to_poly=False: this scan runs on EVERY mapping click over
    every layer (a growing set now that runs stack), and flattening each
    stroke's path at 4px just to look for a legacy property is the dominant
    per-click cost. _stroke_points falls back to raw_points, which for a
    hand-drawn legacy guide are the drawn points themselves.
    """
    assets = _assets_for(view_name)
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return
    changed = False
    for layer in structure["layers"]:
        layer_index = layer["index"]
        cell = scene.cell_to_dict(layer_index, frame, False, POLY_STEP)
        strokes = cell["image"]["strokes"]
        for index in range(len(strokes) - 1, -1, -1):
            prop = strokes[index].get("property") or ""
            if prop in GUIDE_PROPERTIES:
                if prop not in assets:
                    assets[prop] = {
                        "points": _stroke_points(strokes[index]),
                        "width": float(strokes[index].get("width", 3.0)),
                    }
                scene.remove_stroke(frame, layer_index, index)
                changed = True
        fills = cell["image"]["fills"]
        for index in range(len(fills) - 1, -1, -1):
            if (fills[index].get("property") or "") == MAPPING_AREA_PROPERTY:
                if MAPPING_AREA_PROPERTY not in assets:
                    polygons = _path_commands_to_polygons(fills[index].get("commands"))
                    if polygons:
                        assets[MAPPING_AREA_PROPERTY] = {"polygons": polygons}
                scene.remove_fill_area(frame, layer_index, index)
                changed = True
    if changed:
        print(f"[auto_mapping] moved legacy mapping items out of {view_name} layers")
        _save_assets(view_name)
        _overlays_changed(view_name)


# ---------------------------------------------------------------------------
# scene scanning + mapping
# ---------------------------------------------------------------------------

def _collect_pattern_strokes(scene, frame, want_commands=False,
                             tag_source=False):
    """Pattern strokes on `frame`. With want_commands the strokes carry their
    real Bezier "commands" (to_poly=False, for bezier mode); otherwise the 4px
    "polylines" flattening (for polyline / spline modes). tag_source stamps
    each stroke with its snapshot address "src" = (layer index, stroke index
    in the cell) - valid only until the document changes (strokes carry no
    persistent ids; edits renumber)."""
    pattern = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return pattern
    # "auto_mapping" stays as a literal: strokes drawn while the retired
    # Auto Mapping 1 button was active carry that property in old sessions.
    skip = (*GUIDE_PROPERTIES, *MAPPING_OUTPUT_PROPERTIES, "auto_mapping",
            AUTO_MAPPING2_TOOL, MAPPING_AREA_PROPERTY)
    to_poly = not want_commands
    for layer in structure["layers"]:
        if not layer["visible"] or layer["type"] == "fill":
            continue
        cell = scene.cell_to_dict(layer["index"], frame, to_poly, POLY_STEP)
        for index, stroke in enumerate(cell["image"]["strokes"]):
            if (stroke.get("property") or "") in skip:
                continue
            if tag_source:
                stroke["src"] = (layer["index"], index)
            pattern.append(stroke)
    return pattern


def _create_mapped_layer(scene, row, name=MAPPED_LAYER_NAME):
    """Create a FRESH mapped layer for this run and return its index (0 = top).

    Every Auto Mapping click gets its own layer (user request 2026-07-30):
    results stack instead of replacing each other, so different guide setups
    can be compared side by side and bad attempts hidden or deleted
    individually (note: the Layers panel only lists a layer on frames where
    it has a cell, i.e. on the frame it was mapped on). uniqueLayerName()
    drifts the name to "mapped layer1", "mapped layer2", ... automatically.

    The creation cell that add_layer() writes - {private asset, frame_id 1} -
    is kept VERBATIM: the asset belongs to this run alone and holds exactly
    one cell, so frame id 1 is collision-free, and 1 is the canonical id every
    asset-resolution path assumes (assignAssetToLayer hard-codes it). Pointing
    the cell elsewhere would leave an empty canonical image behind: dragging
    the asset from the Asset panel onto the Layers panel would show nothing,
    and every save would carry a dead drawing (review-proven).

    add_layer() appends, and paintGL draws last-index-first, so an appended
    layer lands at the BOTTOM of the z-order - each new run would be occluded
    by the previous ones. The fresh column is therefore moved to index 0
    (top), with the fill source-layer indices remapped to follow the shift.
    The user's frame/layer/asset selection is restored before returning
    (add_layer() selects what it creates; the move shifts old indices by +1).
    """
    saved_frame = scene.current_frame()
    saved_layer = scene.current_layer()
    saved_asset = scene.current_asset()

    layer_index = scene.add_layer()
    if layer_index < 0:
        scene.set_current_frame(saved_frame)
        scene.set_current_layer(saved_layer)
        scene.set_current_asset(saved_asset)
        return -1
    scene.set_layer_name(layer_index, name)

    moved = scene.move_layer(layer_index, 0)
    if moved:
        scene.remap_fill_source_layers_after_move(layer_index, 0)
        layer_index = 0
        if saved_layer >= 0:
            saved_layer += 1

    scene.set_current_frame(saved_frame)
    scene.set_current_layer(saved_layer)
    scene.set_current_asset(saved_asset)
    print(f"[auto_mapping] created layer '{scene.layer_name(layer_index)}' in main_paint_view")
    return layer_index


def _discard_mapped_layer(scene, layer_index):
    """Roll back _create_mapped_layer when a run cannot commit anything.

    A failed or fully-clipped run must not leave an empty layer behind: with
    no history commit of its own, the orphan would silently ride along in the
    NEXT unrelated commit and could never be undone individually.
    """
    try:
        scene.delete_layer(layer_index)
        scene.remap_fill_source_layers_after_delete(layer_index)
    except Exception as error:
        print(f"[auto_mapping] could not roll back the empty mapped layer: {error}")


def _densify(points, step=POLY_STEP):
    """Split runs longer than `step` so straight stretches carry vertices too.

    The C++ flattener only subdivides CURVE elements: a lineTo contributes
    exactly two points no matter how long it is, so a 500 px straight stroke
    arrives here as a 2-point polyline. Two consequences, both real:
      * child mapping-area clipping tests inside/outside per vertex, so a
        region boundary crossing the middle of such a stroke is never seen;
      * the image of a straight source segment is a CURVE, and with only two
        anchors the sampler has to discover the whole deformation by probing
        - exactly the case its probes can be fooled on.
    Already-flattened curves arrive with ~4 px spacing, so this is a no-op
    for them; it only fills in the straight runs.
    """
    if len(points) < 2:
        return list(points)
    out = [points[0]]
    for a, b in zip(points, points[1:]):
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        # round(), not ceil(): a flattened curve arrives with spacing already
        # around `step`, and ceil() would split every one of those in two for
        # a few percent of overshoot. Only genuinely long runs get filled in.
        count = max(1, int(round(length / step))) if step > 0.0 else 1
        for k in range(1, count + 1):
            t = k / count
            out.append((a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t))
    return out


def _stroke_polylines(stroke):
    """Per-subpath point lists (does NOT concatenate separate subpaths, unlike
    _stroke_points, so distinct subpaths never get a bogus joining segment)."""
    result = []
    for polyline in stroke.get("polylines") or []:
        pts = [(float(point["x"]), float(point["y"])) for point in polyline]
        if len(pts) >= 2:
            result.append(_densify(pts))
    if not result:
        raw = [(float(point["x"]), float(point["y"])) for point in (stroke.get("raw_points") or [])]
        if len(raw) >= 2:
            result.append(_densify(raw))
    return result


def _orientation(map_point, point):
    """Raw sign of det J at `point` (spec (5.1)); +1/-1, or 0 if degenerate."""
    child = map_point.child_frame
    main = map_point.main_frame
    l_h, l_v = map_point.coords(point)
    (tcx, tcy), (ncx, ncy) = child.directions(l_h, l_v)
    denominator = tcx * ncy - tcy * ncx
    if abs(denominator) < 1e-12:
        return 0  # folded child frame: no usable orientation here
    # The additional-line warp sits between the child arcs and the main
    # evaluation. Its own fold parity multiplies in: a strong additional
    # line legitimately folds the map (that is how it creates occlusion),
    # and det D(warp) carries that flip.
    warp = getattr(map_point, "warp", None)
    warp_sign = 1
    if warp is not None:
        warp_sign = warp.det_sign((l_h, l_v))
        l_h, l_v = warp.apply((l_h, l_v))
    scale_h = map_point.h_scales[1] if l_h >= 0.0 else map_point.h_scales[0]
    scale_v = map_point.v_scales[1] if l_v >= 0.0 else map_point.v_scales[0]
    (tmx, tmy), (nmx, nmy) = main.directions(l_h * scale_h, l_v * scale_v)
    value = warp_sign * scale_h * scale_v * (tmx * nmy - tmy * nmx) / denominator
    return 1 if value > 0.0 else -1


def _fold_sign(map_point, point):
    """+1 front / -1 back, measured RELATIVE TO THE CROSSING.

    The raw sign of det J is not the answer on its own. Drawing the main
    guides with the opposite handedness to the child ones flips it
    everywhere, so a plain `det J < 0` test labels the whole pattern - the
    crossing included - as the back of a fold, and the lining colour lands on
    the artwork. That handedness flip is a MIRROR, a global property already
    reported as info["mirrored"]; it is not a fold.

    The crossing is the one place both frames are pinned to each other, so it
    is the natural anchor: it is FRONT by definition, and "back" means the
    orientation has flipped relative to it - which is exactly what crossing a
    fold does. Measured on the reported project (mirrored frames): 69.3% of
    the pattern was being called back; anchored at the crossing the same 69.3%
    is front, and the 30.7% beyond the fold is back.
    """
    raw = _orientation(map_point, point)
    if raw == 0:
        return 1
    return 1 if raw == map_point.fold_reference else -1


def _classified_runs(map_point, points, classify, snap_true=False):
    """Split a source polyline into runs of constant classification.

    The shared core of _split_by_fold (classify = fold side) and
    _sever_source (classify = lift validity): split each source segment at
    the structural knots, judge each piece at its midpoint, snap each
    change onto the exact boundary by bisection (the knots come from a
    linear interpolation of the coordinates, so they can sit off the true
    cell boundary; the classifier is exact), and regroup into
    [(run_points, verdict)] where consecutive runs share their boundary
    point. Returns None when there is nothing to judge.

    snap_true is the severing variant's contract: the classification is a
    BOOLEAN whose False runs get dropped, so
      * a change that falls on a source vertex is still bisected - ACROSS
        the vertex, inside whichever segment actually holds the
        transition. The fold splitter keeps the vertex (both sides
        survive, and on polyline guides the vertex IS the cell boundary),
        but a severed island that stopped at the vertex was measured to
        end on invalid ground with a branch-jumped lift;
      * the shared boundary point is the bracket end on the True side,
        never the straddling midpoint, so a surviving run's cut endpoint
        always HAS the coordinate it is about to be mapped with (the
        midpoint landed on the invalid side of det J = 0 about half the
        time - one rounding coin flip per cut).
    """
    pieces = []
    for index, (a, b) in enumerate(zip(points, points[1:])):
        bounds = [0.0] + _structural_knots(map_point, a, b) + [1.0]
        for t0, t1 in zip(bounds, bounds[1:]):
            if t1 - t0 <= 1e-12:
                continue
            pieces.append([index, a, b, t0, t1,
                           classify(_lerp(a, b, (t0 + t1) * 0.5))])
    if not pieces:
        return None

    def bisect_span(a, b, lo, hi, low_class):
        """[lo, hi] brackets one change inside segment (a, b): tighten it
        until (last t of low_class, first t of the other class) touch."""
        for _ in range(30):
            mid = (lo + hi) * 0.5
            if classify(_lerp(a, b, mid)) == low_class:
                lo = mid
            else:
                hi = mid
        return lo, hi

    def snapped(lo, hi, low_class):
        if not snap_true:
            return (lo + hi) * 0.5
        return lo if low_class else hi

    inserts = []
    for position, (left, right) in enumerate(zip(pieces, pieces[1:])):
        if left[5] == right[5]:
            continue
        if left[0] != right[0]:
            if not snap_true:
                continue  # fold split: the shared vertex IS the boundary
            vertex_class = classify(left[2])
            if vertex_class == left[5]:
                lo, hi = bisect_span(right[1], right[2],
                                     0.0, (right[3] + right[4]) * 0.5,
                                     vertex_class)
                boundary = snapped(lo, hi, vertex_class)
                if boundary - right[3] > 1e-12:
                    inserts.append((position + 1,
                                    [right[0], right[1], right[2],
                                     right[3], boundary, vertex_class]))
                right[3] = boundary
            else:
                lo, hi = bisect_span(left[1], left[2],
                                     (left[3] + left[4]) * 0.5, 1.0,
                                     left[5])
                boundary = snapped(lo, hi, left[5])
                if left[4] - boundary > 1e-12:
                    inserts.append((position + 1,
                                    [left[0], left[1], left[2],
                                     boundary, left[4], vertex_class]))
                left[4] = boundary
            continue
        lo, hi = bisect_span(left[1], left[2],
                             (left[3] + left[4]) * 0.5,
                             (right[3] + right[4]) * 0.5, left[5])
        boundary = snapped(lo, hi, left[5])
        left[4] = boundary
        right[3] = boundary
    for position, piece in reversed(inserts):
        pieces.insert(position, piece)

    runs = []
    first = pieces[0]
    current = [_lerp(first[1], first[2], first[3]),
               _lerp(first[1], first[2], first[4])]
    verdict = first[5]
    for piece in pieces[1:]:
        start = _lerp(piece[1], piece[2], piece[3])
        end = _lerp(piece[1], piece[2], piece[4])
        if piece[5] == verdict:
            current.append(end)
        else:
            runs.append((current, verdict))
            current = [start, end]
            verdict = piece[5]
    runs.append((current, verdict))
    return runs


def _split_by_fold(map_point, points):
    """Split a source polyline into runs of constant orientation.

    Returns [(run_points, side)] with side in (+1, -1); consecutive runs
    SHARE their boundary point, so front and back meet exactly (the map is
    continuous there - only its derivative flips, so a fold is never a gap).

    The mechanics live in _classified_runs, shared with _sever_source.
    Guide tangents are piecewise CONSTANT on polyline guides, so the
    orientation is constant inside a cell and can only flip at a cell
    boundary - a structural knot (§6.2) or a source vertex. Measured: every
    sign flip along a densified source lands on a knot (worst gap 0.01 px);
    the bisection pins the cut onto the crease, which is derived
    independently from the frames, so the two derivations agree by
    construction. Folds are rare, so it costs a handful of solves per run.
    """
    if len(points) < 2:
        return [(list(points), 1)]
    runs = _classified_runs(map_point, points,
                            lambda p: _fold_sign(map_point, p))
    if runs is None:
        return [(list(points), 1)]
    return runs


def _sever_source(map_point, points, seams=None):
    """Cut a source polyline into its computable UV ISLANDS (Step 2 of the
    staged pipeline: lift, verdict, sever).

    Every stretch whose Third lift is invalid (child-frame foldover, or a
    diverging Newton solve - see third_of) is DROPPED, and each surviving
    island ends exactly ON the fold line - on its VALID side, so the cut
    endpoint still has a lift: the cut position is solved by bisection on
    the validity verdict (_classified_runs with snap_true), the fold-line
    idea - never left at whatever sample happened to fall nearest.
    Downstream the islands flow through the unchanged reconstruction
    machinery, so a stroke crossing a child fold visually ENDS at the
    seam, the way a printed pattern wraps around to the hidden face of a
    folded sheet. With no residual term left in the map there is nothing
    else these points could do: their UV coordinate does not exist.

    `seams` (if given) collects the cut points (child space), one per severed
    boundary, for the run summary. The whole function is FREE on frames that
    cannot fold (can_fold gate): one branch, zero extra solves.
    """
    if len(points) < 2 or not map_point.can_fold():
        return [list(points)]
    runs = _classified_runs(map_point, points,
                            lambda p: map_point.third_of(p)[2],
                            snap_true=True)
    if runs is None:
        return [list(points)]
    if all(ok for _run, ok in runs):
        return [list(points)]  # entirely on the computable sheet

    islands = []
    for index, (run, ok) in enumerate(runs):
        if not ok:
            continue
        if seams is not None:
            if index > 0:
                seams.append(run[0])
            if index < len(runs) - 1:
                seams.append(run[-1])
        if len(run) >= 2:
            islands.append(run)
    return islands


def _third_end_tangent(map_point, points, at_end, reach=POLY_STEP):
    """Unit THIRD-SPACE tangent at one end of a child polyline, pointing in
    the direction of travel (index order).

    This is the "trend" the Bezier Bridge extends: the island's own Third
    trace direction at the cut. Walks inward until the lifted distance
    clears `reach`, so the sub-pixel sliver a bisected cut leaves next to
    the last sample cannot set the trend; returns None when every vertex
    lifts to (numerically) the same Third point - the caller falls back to
    the chord.
    """
    if len(points) < 2:
        return None
    coords = map_point.coords
    if at_end:
        anchor = coords(points[-1])
        walk = range(len(points) - 2, -1, -1)
        sign = 1.0
    else:
        anchor = coords(points[0])
        walk = range(1, len(points))
        sign = -1.0
    best = None
    for index in walk:
        t = coords(points[index])
        dx = (anchor[0] - t[0]) * sign
        dy = (anchor[1] - t[1]) * sign
        norm = math.hypot(dx, dy)
        if norm > 1e-9:
            best = (dx / norm, dy / norm)
            if norm >= reach:
                break
    return best


def _bridge_third_cubic(map_point, a_points, b_points):
    """The Bezier Bridge across one severed gap, as a cubic IN THIRD SPACE.

    `a_points` is a child polyline ENDING at cut A, `b_points` one STARTING
    at cut B (the two islands' facing ends). Control points per the user's
    formula (2026-08-24):

        P0 = A                      (the departure cut, lifted)
        P1 = A + k * v_A            (extend the departure trend)
        P2 = B - k * v_B            (back-cast the arrival trend)
        P3 = B                      (the arrival cut, lifted)

    with k = tension * |AB| (Third-space straight-line distance; the
    tension slider defaults to the classic smooth-join 1/3). A missing
    trend (degenerate island end) falls back to the chord direction, which
    degrades that side of the bridge to the straight join. Returns the
    Third cubic, or None when the two cuts lift to the same Third point
    (nothing to bridge).
    """
    a = map_point.coords(a_points[-1])
    b = map_point.coords(b_points[0])
    span = math.hypot(b[0] - a[0], b[1] - a[1])
    if span <= 1e-6:
        return None
    chord = ((b[0] - a[0]) / span, (b[1] - a[1]) / span)
    v_a = _third_end_tangent(map_point, a_points, at_end=True) or chord
    v_b = _third_end_tangent(map_point, b_points, at_end=False) or chord
    k = _BRIDGE["tension"] * span
    return (a,
            (a[0] + k * v_a[0], a[1] + k * v_a[1]),
            (b[0] - k * v_b[0], b[1] - k * v_b[1]),
            b)


def _project_third_cubic(map_point, cub, tol=_CURVE_TOL,
                         max_depth=_SPLINE_MAX_DEPTH):
    """Forward-project a THIRD-SPACE cubic into main canvas flagged points.

    Step 4 for a bridge: every probe is main_of_third - pure forward
    arithmetic, no Newton - and the sampling is the SAME _flatness_recurse
    the child-space sampler drives, judged in the IMAGE where the tolerance
    means pixels. Endpoints are anchors; inserted samples decimate
    downstream as usual.
    """
    def image(t):
        return map_point.main_of_third(_cubic_point(cub, t))

    first = image(0.0)
    last = image(1.0)
    result = [(first, True)]
    recurse = _flatness_recurse(image, lambda a, b, t: a + (b - a) * t,
                                result, tol, max_depth)
    recurse(0.0, 1.0, first, last, 0)
    result.append((last, True))
    return result


def _island_end_anchor(points, at_end, reach=POLY_STEP):
    """A verdict probe pulled back from an island's cut by `reach` child px.

    The cut endpoint itself sits ON det J = 0 (the bisection converges onto
    the seam), so any orientation or depth sampled there is decided by
    rounding noise - measured: 26% of sub-pixel input perturbations flipped
    a bridge to BACK while both its islands were FRONT. One step back into
    the island the verdict is the island's own."""
    walk = points[-2::-1] if at_end else points[1:]
    previous = points[-1] if at_end else points[0]
    run = 0.0
    for point in walk:
        run += math.hypot(point[0] - previous[0], point[1] - previous[1])
        previous = point
        if run >= reach:
            return point
    return previous if walk else points[0]


def _emit_bridges(out, map_point, gap_pairs, main_area, color_tuple, width,
                  curved, eps):
    """Emit one Bezier Bridge per severed gap (补全拓扑 checkbox).

    `gap_pairs` is [(a_points, b_points)] - per gap, the child polylines
    ending at cut A and starting at cut B. Side and stacking depth are
    borrowed from island A - probed one step INSIDE it, never at the cut
    itself, which sits on det J = 0 (_island_end_anchor): the bridge spans
    ground that HAS no child coordinate, which is also why it never feeds
    the crease/seal machinery - it is new geometry, not a fold of the
    sheet. Output is a fitted curve (`curved`) or a polyline, matching the
    emitter that asked. `out.bridges` records each gap ONCE, however many
    pieces the mapping area clips its projection into.
    """
    added = 0
    for a_points, b_points in gap_pairs:
        bridge = _bridge_third_cubic(map_point, a_points, b_points)
        if bridge is None:
            continue
        flagged = _project_third_cubic(map_point, bridge)
        if _FOLD["split"]:
            probe = _island_end_anchor(a_points, at_end=True)
            side = _fold_sign(map_point, probe)
            depth = _run_depth(map_point, [probe], side)
        else:
            side, depth = _MappedOutput.FRONT, 0
        emitted_pieces = 0
        for clipped in _clip_flagged(flagged, main_area):
            knots = _decimate_between_anchors(clipped, eps)
            if curved:
                commands, flat = _cubics_to_commands(_catmull_rom_cubics(knots))
                emitted = out.add_curved(side, commands, flat,
                                         _side_style(side, color_tuple),
                                         width, depth)
            else:
                emitted = out.add_polyline(side, knots,
                                           _side_style(side, color_tuple),
                                           width, depth)
            if emitted:
                emitted_pieces += 1
        if emitted_pieces:
            added += emitted_pieces
            out.bridges.append(bridge)
    return added


def _cubic_tail_polyline(cubics, span=4.0 * POLY_STEP):
    """A short child polyline probing an island's END trend (bezier mode).

    Walks backward over the island's last cubics until the polyline spans
    `span` of child ground: the final cubic alone can be the sub-pixel
    sliver a bisected cut leaves behind (the sever pass rejects parts by
    PARAMETER span, not geometry), and a probe confined to it defeated
    _third_end_tangent's walk-inward guard - the sliver's ill-conditioned
    direction set the trend. Earlier cubics feed the probe until there is
    real ground to measure. The last point stays the cut itself. Sampling
    is confined to the TAIL of each cubic (parameter range covering ~span
    of hull) at POLY_STEP resolution - a flat per-cubic cap once made the
    probe spacing hull/8 on a long end cubic, coarsening both the trend
    and _island_end_anchor's one-step pullback to tens of px."""
    points = []
    total = 0.0
    for cub in reversed(cubics):
        want = span - total
        if want <= 1e-9:
            # FP guard: the accumulation can land one ULP under `span`,
            # and walking on with t0 == 1.0 prepended a zero-length sliver
            # plus a jump back over the whole island.
            break
        hull = max(bezier.hull_length(cub), 1e-9)
        t0 = max(0.0, 1.0 - min(1.0, want / hull))
        count = max(2, min(16, int(math.ceil((1.0 - t0) * hull / POLY_STEP))))
        seg = [_cubic_point(cub, t0 + (1.0 - t0) * k / count)
               for k in range(count + 1)]
        points = seg[:-1] + points if points else seg
        total += (1.0 - t0) * hull
    return points


def _cubic_head_polyline(cubics, span=4.0 * POLY_STEP):
    """A short child polyline probing an island's START trend (bezier mode).

    The head twin of _cubic_tail_polyline: walks forward over the island's
    first cubics until `span` of child ground backs the probe, sampling
    each cubic's HEAD range at POLY_STEP resolution. The first point stays
    the cut itself."""
    points = []
    total = 0.0
    for cub in cubics:
        want = span - total
        if want <= 1e-9:
            break   # FP guard, mirror of _cubic_tail_polyline's
        hull = max(bezier.hull_length(cub), 1e-9)
        t1 = min(1.0, want / hull)
        count = max(2, min(16, int(math.ceil(t1 * hull / POLY_STEP))))
        seg = [_cubic_point(cub, t1 * k / count) for k in range(count + 1)]
        points = points + seg[1:] if points else seg
        total += t1 * hull
    return points


def _crease_scan(map_point, row_range, axis, samples=48, max_columns=600,
                 frame=None):
    """One directional sweep of the fold locus, as curves of (l_h, l_v) pairs.

    `frame` selects whose folds are traced: the MAIN frame by default (the
    creases the output folds along), or the CHILD frame for the severing
    seams - there the traced (l_h, l_v) pairs are Third coordinates directly
    and the image-space refinement below runs on the child canvas, which is
    exactly the space the seam cutters cut in.

    det J vanishes where the main H direction turns parallel to the main V
    direction: f(l_h, l_v) = T_h(l_h) x T_v(l_v) = 0. A sweep fixes one
    coordinate per row and walks the OTHER guide for sign changes, bracketing
    and bisecting each. axis "h" (rows are V arcs, walk H) finds every locus
    transversal to the H direction; axis "v" is the transpose.

    ONE sweep cannot find loci that run parallel to its walking direction.
    With a coiling V guide and a near-straight H guide the locus is nearly
    HORIZONTAL in (l_h, l_v): at any fixed l_v the sign of f barely varies
    along l_h, so the "h" sweep sees no crossings at all - measured, the
    crease vanished entirely (0 branches, 82 of 82 cuts orphaned) while the
    same coil on the H guide (a near-vertical locus) worked fine. That is why
    _crease_curves runs BOTH sweeps and merges.

    Chaining is BY RANK while the count holds (sign changes along a row
    alternate, so ranks cannot swap); on a count change branches continue by
    nearest arc within a tight window (see the inline note).
    """
    main = frame if frame is not None else map_point.main_frame
    low, high = row_range
    if axis == "h":
        scan_guide, scan_zero, scan_window = main.gh, main.h_arc, main.h_window
        row_guide, row_zero, row_window = main.gv, main.v_arc, main.v_window
    else:
        scan_guide, scan_zero, scan_window = main.gv, main.v_arc, main.v_window
        row_guide, row_zero, row_window = main.gh, main.h_arc, main.h_window
    if high - low <= 1e-6 or len(scan_guide.points) < 3:
        return [], []

    total = scan_guide.total
    step = max(POLY_STEP, total / 400.0)

    def crossings_at(row):
        normal = row_guide.tangent_at(row_zero + row, row_window)

        def side_at(arc):
            tangent = scan_guide.tangent_at(scan_zero + arc, scan_window)
            return tangent[0] * normal[1] - tangent[1] * normal[0]

        found = []
        arc = -scan_zero
        limit = total - scan_zero
        previous = side_at(arc)
        while arc < limit:
            nxt = min(arc + step, limit)
            value = side_at(nxt)
            if (value > 0.0) != (previous > 0.0):
                lo, hi = arc, nxt
                for _ in range(24):
                    mid = (lo + hi) * 0.5
                    if (side_at(mid) > 0.0) == (previous > 0.0):
                        lo = mid
                    else:
                        hi = mid
                found.append((lo + hi) * 0.5)
            arc, previous = nxt, value
        return found

    def hv_of(arc, row):
        return main.hv(arc, row) if axis == "h" else main.hv(row, arc)

    def column(row):
        arcs = crossings_at(row)
        return (row, arcs, [hv_of(arc, row) for arc in arcs])

    columns = [column(low + (high - low) * index / samples)
               for index in range(samples + 1)]

    # Refine in V until the crease polyline is fine enough IN IMAGE SPACE.
    # A uniform grid is not enough in the extreme: on a guide coiling through
    # 944 degrees the crease slid 23 px of H per px of V, so consecutive
    # samples were 70 px apart and a cut could sit 82 px from the polyline.
    #
    # Bounding the H arc alone was not enough either. The V spacing stayed at
    # (v_high - v_low)/48 - about 19 px on a tall drawing - and the image step
    # carries BOTH, so the chord could still cut the corner by more than the
    # anchor tolerance and throw a cut that is genuinely on the locus (0.00 px
    # at high resolution) 3 to 24 px off its own branch. Measured that way, a
    # cut was rejected and its whole branch deleted for want of anchors, and
    # in the worst case the entire crease layer vanished. The step that has to
    # be bounded is the one distances are measured in.
    #
    # Intervals where the crossing COUNT changes are refined too. Skipping
    # them was backwards: that is where a fold pair is born and the one place
    # the chaining below must decide whether two samples are the same branch,
    # and it was deciding it on the coarsest data in the sweep. Halving stops
    # at a floor, so a genuine bifurcation cannot spin.
    floor = (high - low) / 4096.0
    for _ in range(32):
        if len(columns) >= max_columns:
            break
        refined = [columns[0]]
        inserted = False
        for before, after in zip(columns, columns[1:]):
            if after[0] - before[0] > floor and len(refined) + 1 < max_columns:
                if len(before[1]) != len(after[1]):
                    coarse = True
                else:
                    coarse = any(_dist(p, q) > POLY_STEP
                                 for p, q in zip(before[2], after[2]))
                if coarse:
                    refined.append(column((before[0] + after[0]) * 0.5))
                    inserted = True
            refined.append(after)
        columns = refined
        if not inserted:
            break

    # Chain by rank. Branches alternate in sign, so the k-th crossing of one
    # column continues the k-th of the next; nearest-neighbour matching (what
    # this used to do) let a branch jump to a different one when a transient
    # pair appeared, which is what tied the crease in knots.
    curves = []
    open_curves = []
    previous_count = None
    for l_v, found, _image in columns:
        if previous_count == len(found) and open_curves:
            for rank, arc in enumerate(found):
                open_curves[rank].append((arc, l_v))
        elif open_curves:
            # The count changed - a fold pair appeared or vanished. Continuing
            # every branch by rank would now shift them all by one, so match
            # by arc instead, but only within a TIGHT window: refinement above
            # keeps a genuine continuation within POLY_STEP of its previous
            # sample, so anything further away is a different branch. (The
            # original code allowed a quarter of the guide's length here,
            # which is how branches used to swap and tie the crease in knots.)
            limit = 2.0 * POLY_STEP
            taken = set()
            survivors = []
            for curve in open_curves:
                best = None
                for index, arc in enumerate(found):
                    if index in taken:
                        continue
                    gap = abs(arc - curve[-1][0])
                    if best is None or gap < best[0]:
                        best = (gap, index, arc)
                if best is not None and best[0] <= limit:
                    taken.add(best[1])
                    curve.append((best[2], l_v))
                    survivors.append(curve)
                elif len(curve) >= 2:
                    curves.append(curve)
            for index, arc in enumerate(found):
                if index not in taken:
                    survivors.append([(arc, l_v)])
            open_curves = sorted(survivors, key=lambda curve: curve[-1][0])
        else:
            open_curves = [[(arc, l_v)] for arc in found]
        previous_count = len(found)
    curves.extend(curve for curve in open_curves if len(curve) >= 2)

    # The locus samples are exact (each is bisected onto det J = 0), but the
    # scanned position ripples a couple of px between adjacent rows. Smooth
    # that away so the cusp test in _emit_seals fires on real cusps only.
    smoothed = []
    for curve in curves:
        arcs = [arc for arc, _ in curve]
        for _ in range(2):
            arcs = ([arcs[0]]
                    + [(arcs[i - 1] + 2.0 * arcs[i] + arcs[i + 1]) * 0.25
                       for i in range(1, len(arcs) - 1)]
                    + [arcs[-1]])
        smoothed.append([(arcs[i], curve[i][1]) for i in range(len(curve))])

    # Grade each stretch by how well this sweep samples it. Where the locus
    # runs steeper than ~1:1 against the walk direction, consecutive rows
    # land far apart along the scan guide and the chain degenerates into long
    # chords (the refinement floor caps how finely rows can subdivide -
    # measured 12-23 px off the true locus there). The TRANSPOSED sweep sees
    # those stretches at slope < 1 and samples them densely, so they are
    # DEGRADED here, not deleted: an earlier version deleted them outright,
    # and everything the transposed sweep failed to re-cover - branch tips
    # (the slope is steepest there), stretches outside its row window - was
    # gone for good, shrinking branches under the minimum-length guard and
    # orphaning their cuts. The merge in _crease_curves accepts a degraded
    # stretch whenever nothing better covers it. The 25% margin keeps a band
    # both sweeps grade as good; the merge collapses the doubled band.
    good = []
    degraded = []
    for curve in smoothed:
        run = []
        run_ok = None
        for index, sample in enumerate(curve):
            ok = False
            for j in (index - 1, index):
                if 0 <= j < len(curve) - 1:
                    d_arc = abs(curve[j + 1][0] - curve[j][0])
                    d_row = abs(curve[j + 1][1] - curve[j][1])
                    if d_arc <= 1.25 * d_row + 1e-9:
                        ok = True
            if run_ok is None or ok == run_ok:
                run.append(sample)
                run_ok = ok
            else:
                (good if run_ok else degraded).append(run)
                run = [sample]
                run_ok = ok
        if run:
            (good if run_ok else degraded).append(run)

    # Normalize to (l_h, l_v) pairs whichever way the sweep ran.
    if axis != "h":
        good = [[(row, arc) for arc, row in curve] for curve in good]
        degraded = [[(row, arc) for arc, row in curve] for curve in degraded]
    return good, degraded


class _ArcGrid:
    """Spatial hash of locus segments in ARC space, for coverage queries.

    Cell size >= the query tolerance, so a point's 3x3 cell neighbourhood is
    guaranteed to contain every segment within tolerance of it.
    """

    def __init__(self, cell):
        self.cell = cell
        self.cells = {}

    def add_curve(self, curve):
        if len(curve) == 1:
            segments = [(curve[0], curve[0])]
        else:
            segments = list(zip(curve, curve[1:]))
        for a, b in segments:
            lo_x = int(min(a[0], b[0]) // self.cell)
            hi_x = int(max(a[0], b[0]) // self.cell)
            lo_y = int(min(a[1], b[1]) // self.cell)
            hi_y = int(max(a[1], b[1]) // self.cell)
            for cx in range(lo_x, hi_x + 1):
                for cy in range(lo_y, hi_y + 1):
                    self.cells.setdefault((cx, cy), []).append((a, b))

    def covered(self, point, tolerance):
        cx = int(point[0] // self.cell)
        cy = int(point[1] // self.cell)
        for dx in (-1, 0, 1):
            for dy in (-1, 0, 1):
                for a, b in self.cells.get((cx + dx, cy + dy), ()):
                    ux, uy = b[0] - a[0], b[1] - a[1]
                    length_sq = ux * ux + uy * uy
                    if length_sq <= 0.0:
                        t = 0.0
                    else:
                        t = ((point[0] - a[0]) * ux + (point[1] - a[1]) * uy) / length_sq
                        t = min(1.0, max(0.0, t))
                    gx = point[0] - (a[0] + ux * t)
                    gy = point[1] - (a[1] + uy * t)
                    if gx * gx + gy * gy <= tolerance * tolerance:
                        return True
        return False


def _uncovered_runs(curve, grid, tolerance):
    """The stretches of `curve` not already covered in the grid.

    Single-point runs are KEPT: the transposed copy of a culled branch tip is
    often exactly one sample (everything past it is covered), and dropping it
    was measured to cost a whole branch - the tip is what the stitcher needs
    to reach the anchoring cut. An isolated single point that never stitches
    onto anything is filtered after stitching instead.

    Coverage is judged in ARC space rather than image space because a folded
    sheet can bring two DISTINCT loci close together in the image while they
    stay far apart in the domain.
    """
    runs = []
    run = []
    for point in curve:
        if grid.covered(point, tolerance):
            if run:
                runs.append(run)
            run = []
        else:
            run.append(point)
    if run:
        runs.append(run)
    return runs


def _stitch_crease(pieces, tolerance, boundaries):
    """Join locus stretches whose endpoints meet, into whole loci.

    The two sweeps each contribute the stretches they sample well, so one
    physical locus arrives as alternating h- and v-conditioned pieces (plus
    the sweep's own conditioning splits). Anchoring MUST see whole loci:
    trimming fragments independently left each with at most one cut, and the
    single-cut rule then dropped them all - measured 5 of 16 cuts orphaned on
    a project that was fully anchored before the split sweep.

    Endpoints are matched in ARC space, where distinct loci stay far apart.
    A fold pair's two branches genuinely MEET at their birth point, so
    joining them there reconstructs the true topology (one continuous curve
    turning around); the image-space cusp splitter separates the arms again
    for drawing. The one refusal: endpoints that both sit on the SAME sweep
    boundary (the locus continues outside the swept window) are edge
    truncations, not meetings.

    Stitching serves DRAWING AND ANCHORING ONLY. At a junction of crossing
    loci (corner lines over each other or over smooth branches) several
    endpoints meet, the pairing is genuinely under-determined, and any
    greedy choice can chain arms of different loci into one path. That is
    harmless here - every piece is real locus geometry, the trim only needs
    connectivity, and direction-based join guards were each measured to
    break something real (a birth join at slopes near +/-1 on a cornerless
    project; the corner-to-smooth T-continuation, cutting the drawn crease
    from 197 to 113 px). The consumer that CANNOT tolerate chained paths -
    the fill splitter, whose _cut_ring_by_polyline pairs crossings along
    each cutter - takes the UNSTITCHED curve set instead (_crease_curves
    stitch=False), where injected corner lines are whole and every curve is
    a single locus.
    """
    pieces = {index: list(piece) for index, piece in enumerate(pieces) if piece}

    def on_same_boundary(a, b):
        for index, low, high in boundaries:
            for edge in (low, high):
                if (abs(a[index] - edge) <= 1e-6 and abs(b[index] - edge) <= 1e-6):
                    return True
        return False

    # Endpoint bucket grid + a lazily validated heap. The first version
    # rescanned every piece pair after every join - O(P^3) - and on ruffled
    # guides (8-lobe sines on both axes, 480 pieces) one Auto Mapping click
    # froze the UI for 13-15 s, 55+ s at 10 lobes. After a join only the
    # merged piece's endpoints gain new candidates, so the scan happens once
    # and each join adds local work only.
    cell = tolerance

    def bucket_key(point):
        return (int(point[0] // cell), int(point[1] // cell))

    buckets = {}

    def add_ends(pid):
        piece = pieces[pid]
        for end in (0, -1):
            buckets.setdefault(bucket_key(piece[end]), set()).add((pid, end))

    def remove_ends(pid, piece):
        for end in (0, -1):
            entries = buckets.get(bucket_key(piece[end]))
            if entries:
                entries.discard((pid, end))

    def candidates_for(pid):
        found = []
        piece = pieces[pid]
        for end in (0, -1):
            point = piece[end]
            cx, cy = bucket_key(point)
            for dx in (-1, 0, 1):
                for dy in (-1, 0, 1):
                    for other_id, other_end in buckets.get((cx + dx, cy + dy), ()):
                        if other_id == pid:
                            continue
                        other = pieces[other_id][other_end]
                        gap = math.hypot(point[0] - other[0], point[1] - other[1])
                        if gap <= tolerance and not on_same_boundary(point, other):
                            found.append((gap, pid, end, point, other_id, other_end, other))
        return found

    heap = []
    for pid in pieces:
        add_ends(pid)
    for pid in pieces:
        for candidate in candidates_for(pid):
            heapq.heappush(heap, candidate)

    while heap:
        gap, a_id, a_end, a_point, b_id, b_end, b_point = heapq.heappop(heap)
        if a_id not in pieces or b_id not in pieces:
            continue  # one side was already merged away
        if pieces[a_id][a_end] != a_point or pieces[b_id][b_end] != b_point:
            continue  # stale candidate: that endpoint moved in a merge
        remove_ends(a_id, pieces[a_id])
        remove_ends(b_id, pieces[b_id])
        a, b = pieces[a_id], pieces[b_id]
        if a_end == -1:
            a.extend(b if b_end == 0 else reversed(b))
        else:
            pieces[a_id] = (b if b_end == -1 else list(reversed(b))) + a
        del pieces[b_id]
        add_ends(a_id)
        for candidate in candidates_for(a_id):
            heapq.heappush(heap, candidate)
    return list(pieces.values())


def _corner_loci(map_point, v_range, h_range, spans=None, step=POLY_STEP,
                 frame=None):
    """Exact fold loci of guide CORNERS: constant-arc lines in arc space.

    `frame` selects the frame whose corners are enumerated (default MAIN);
    with the CHILD frame the emitted lines are Third-space seam candidates
    for the severing pass.

    A sharp corner folds the map along the corner's preimage - a straight
    line l_h = m_c (H corner) or l_v = m_c (V corner) in arc space, exactly
    where the position field kinks. The sweeps do trace these lines, but as
    the worst-conditioned geometry they meet: each line is parallel to one
    sweep's walk (invisible), ill-conditioned for the other (a handful of
    samples), and where corner lines cross each other or a smooth locus the
    rank chaining breaks all arms apart - measured on a two-corner-H plus
    one-corner-V project: the V line arrived as three fragments with a 40 px
    hole at a junction, and the fill splitter downstream lost the whole back
    band. There is nothing to trace: the line's position is known, and the
    only computation is its EXTENT - the rows where the sign of T_h x T_v
    actually differs across the corner (the row direction falls inside the
    corner's wedge). Rows are tested with the same windowed tangents the
    sweeps use, so these curves are drop-in members of the same pool - just
    exact, complete, and continuous through every junction.
    """
    main = frame if frame is not None else map_point.main_frame
    curves = []
    # The line POSITION gate uses the artwork's true arc extent (`spans`,
    # unpadded), not the padded scan window: a fold at an arc no material
    # reaches folds nothing - injecting it drew a 140 px dashed crease
    # across visually flat artwork (the corner sat 2.5 px from the guide
    # end, its line 14 px beyond the pattern, inside the pad). The ROW
    # ranges stay padded: along the line, branch tips still need room to
    # reach their anchors (6.1).
    h_span, v_span = spans if spans is not None else (h_range, v_range)

    def lines(guide, zero, window, other_guide, other_zero, other_window,
              span, rows, transpose):
        for raw in guide.sharp_arcs:
            m_c = raw - zero
            if not (span[0] <= m_c <= span[1]):
                continue
            # One-sided WINDOWED tangents (the corner clamp makes tangent_at
            # just beside the vertex exactly one-sided): _fold_sign classifies
            # with these, so the line's extent must be judged with them too -
            # exact tangents were measured to disagree on borderline rows and
            # leave the extent 14 px short of where the field actually flips.
            before = guide.tangent_at(max(0.0, raw - 1e-3), window)
            after = guide.tangent_at(min(guide.total, raw + 1e-3), window)

            def flips(row):
                normal = other_guide.tangent_at(other_zero + row, other_window)
                f_before = before[0] * normal[1] - before[1] * normal[0]
                f_after = after[0] * normal[1] - after[1] * normal[0]
                return (f_before > 0.0) != (f_after > 0.0)

            def boundary(inside, outside):
                for _ in range(24):
                    mid = (inside + outside) * 0.5
                    if flips(mid):
                        inside = mid
                    else:
                        outside = mid
                return inside

            def emit(run):
                if len(run) >= 2:
                    curves.append([(r, m_c) if transpose else (m_c, r)
                                   for r in run])

            run = []
            previous = None
            row = rows[0]
            while True:
                if flips(row):
                    if not run and previous is not None:
                        run.append(boundary(row, previous))
                    run.append(row)
                else:
                    if run:
                        run.append(boundary(run[-1], row))
                        emit(run)
                        run = []
                previous = row
                if row >= rows[1]:
                    break
                row = min(rows[1], row + step)
            emit(run)

    lines(main.gh, main.h_arc, main.h_window, main.gv, main.v_arc,
          main.v_window, h_span, v_range, False)
    lines(main.gv, main.v_arc, main.v_window, main.gh, main.h_arc,
          main.h_window, v_span, h_range, True)
    return curves


def _child_of_arcs(map_point, arcs):
    """Child-space point of a MAIN-arc coordinate pair (loci live in main arcs).

    The inverse route of _arc_of_point: unscale, then UNWARP (additional
    lines), then the child frame - so loci probes and cutters land on the
    same child points the forward map folds at.
    """
    a_h, a_v = arcs
    h_scales, v_scales = map_point.h_scales, map_point.v_scales
    third = (a_h / (h_scales[1] if a_h >= 0.0 else h_scales[0]),
             a_v / (v_scales[1] if a_v >= 0.0 else v_scales[0]))
    warp = getattr(map_point, "warp", None)
    if warp is not None:
        third = warp.unapply(third)
    return map_point.child_frame.hv(*third)


def _locus_flips(curve, to_child, changes, probe, samples):
    """Probe a traced locus at `samples` spread spots: does `changes` see a
    field flip across it anywhere?

    The shared sampling loop of _curve_is_fold and _sever_curve_is_real.
    `to_child` puts a locus vertex on the child canvas; `changes(p, q,
    vertex)` judges one probe pair one `probe` step to each side (the
    locus vertex rides along so the sever twin can seed its lifts). A
    locus nothing could judge (degenerate geometry) is kept, as before the
    phantom filters existed.
    """
    count = len(curve)
    if count < 2:
        return False
    tested = 0
    for k in range(samples):
        index = max(0, min(count - 2, int((k + 0.5) * (count - 1) / samples)))
        pa = to_child(curve[index])
        pb = to_child(curve[index + 1])
        dx, dy = pb[0] - pa[0], pb[1] - pa[1]
        length = math.hypot(dx, dy)
        if length <= 1e-9:
            continue
        nx, ny = -dy / length, dx / length
        mid = ((pa[0] + pb[0]) * 0.5, (pa[1] + pb[1]) * 0.5)
        tested += 1
        if changes((mid[0] + nx * probe, mid[1] + ny * probe),
                   (mid[0] - nx * probe, mid[1] - ny * probe),
                   curve[index]):
            return True
    return tested == 0  # nothing to judge: keep


def _curve_is_fold(map_point, curve, probe=POLY_STEP, samples=5):
    """Does the field actually CHANGE SIDE across this traced curve?

    The sweeps trace zeros of the analytic factor per row, but near a corner
    row the windowed tangent swings so fast that isolated per-row zeros
    chain into a long near-horizontal "curve" spanning the guide - while the
    field on either side never flips: at most a fold PAIR thinner than the
    probe hides inside (measured ~1.5 arc px - visually nothing, and the
    pointwise splitters already treat it as parity-neutral). Such a phantom
    still collected anchors from fill-edge crossings and drew a 140 px
    dashed crease across visually flat artwork, and its cutter shifted
    depth counts by a parity-neutral +2. Probing _fold_sign one step to
    each side at a few spots along the curve separates real creases (any
    sample flips - even near a birth point the arms part beyond the probe
    somewhere mid-branch) from phantoms (no sample flips anywhere).
    """
    return _locus_flips(
        curve,
        lambda entry: _child_of_arcs(map_point, entry),
        lambda p, q, _entry: (_fold_sign(map_point, p)
                              != _fold_sign(map_point, q)),
        probe, samples)


def _merged_loci(pools, keep=None):
    """Priority-merge sweep pools into one deduped locus set.

    The shared merge of _crease_curves and _sever_loci. Geometry is only
    ever DROPPED when something at least as good already covers it: pools
    arrive best-first (corner lines - exact and continuous through every
    junction - then the well-conditioned sweep stretches, then the degraded
    ones as fallback for whatever neither sweep sampled well; degraded
    beats deleted). `keep` filters each candidate run BEFORE it claims
    ground in the dedup grid, so a rejected run cannot shadow a real one
    from a later pool."""
    tolerance = 2.5 * POLY_STEP
    grid = _ArcGrid(4.0 * POLY_STEP)
    pieces = []
    for pool in pools:
        for curve in pool:
            for run in _uncovered_runs(curve, grid, tolerance):
                if keep is not None and (len(run) < 2 or not keep(run)):
                    continue
                pieces.append(run)
                grid.add_curve(run)
    return pieces


def _crease_curves(map_point, v_range, h_range=None, samples=48, max_columns=600,
                   stitch=True, corner_spans=None):
    """The fold loci, traced from the FRAMES rather than from the strokes.

    stitch=True returns loci joined into whole paths for drawing and
    anchoring; stitch=False returns the raw deduped curve SET (each curve a
    single locus) for the fill splitter and depth counting, where chained
    paths would mis-pair ring crossings - see _stitch_crease.

    corner_spans carries the artwork's UNPADDED (h, v) arc extents for the
    corner-line position gate (_corner_loci): the ranges themselves arrive
    padded so branch tips can reach their anchors, but a corner line at an
    arc no material reaches must not be injected at all.

    Both sweep directions run (see _crease_scan: one sweep is blind to loci
    parallel to its walk - a coiling V guide over a near-straight H guide
    lost the crease entirely). Each sweep keeps its well-conditioned
    stretches, the transposed sweep's duplicates are dropped in arc space,
    and the surviving stretches are stitched back into whole loci so the
    anchoring downstream sees complete branches.

    The crease is NOT in general a translated copy of the V guide - that only
    holds when the main V guide is straight, so that its direction (and hence
    the critical H position) is the same at every height. On a curved V guide
    the locus really does sweep along H: measured on a 211 px V guide bowing
    45 px off its chord, it travels 112-123 px, and that survives even a 65 px
    smoothing window, so it is geometry rather than noise.

    Deriving this per STROKE instead - recording where each stroke happened to
    change side - was wrong: several strokes crossing the SAME fold each
    reported a slightly different arc, so one fold was drawn as four copies.
    """
    h_good, h_bad = _crease_scan(map_point, v_range, "h", samples, max_columns)
    boundaries = [(1, v_range[0], v_range[1])]
    if h_range is None:
        pieces = h_good + h_bad
    else:
        v_good, v_bad = _crease_scan(map_point, h_range, "v", samples, max_columns)
        boundaries.append((0, h_range[0], h_range[1]))
        # Priority merge (see _merged_loci): corner lines first - they are
        # exact and continuous through every junction, so the sweeps'
        # fragmented versions of the same lines dedupe away against them
        # instead of the other way round.
        pieces = _merged_loci(
            (_corner_loci(map_point, v_range, h_range, corner_spans),
             h_good, v_good, h_bad, v_bad))
    def _with_warp_loci(curves):
        """Append the additional-line warp's own fold loci - exact analytic
        curves the frame sweeps are structurally blind to (they test main
        tangents only). Their child-space geometry is registered by object
        identity so _child_cutters can bypass the fixed-point unwarp,
        which is unreliable exactly where the warp folds; the same
        identity set exempts them from _curve_is_fold, whose probes go
        through that unwarp."""
        warp = getattr(map_point, "warp", None)
        if warp is None:
            return curves
        child_frame = map_point.child_frame
        ids = dict(getattr(map_point, "warp_curve_child", {}) or {})
        thirds = dict(getattr(map_point, "warp_curve_third", {}) or {})
        # Pin every registered curve object: the registry accumulates
        # across calls, and an id() of a garbage-collected list could be
        # recycled by a NEW curve, silently resolving to the wrong child
        # geometry. A held reference makes that impossible.
        keep = list(getattr(map_point, "warp_curve_keep", []) or [])
        for third_curve in warp.fold_loci():
            arc_curve = [map_point.scale_arcs(*warp.apply(t))
                         for t in third_curve]
            if len(arc_curve) < 2:
                continue
            ids[id(arc_curve)] = [child_frame.hv(*t) for t in third_curve]
            # The NATIVE Third geometry rides along: depth rays must count
            # warp loci in base Third - their unfolding space - because in
            # arc space the warp has already folded, so a warp locus there
            # is a fold EDGE image whose two sides are NOT different
            # sheets (counting it in arc space lifted an untouched far
            # region to depth 2 on add_s_error). No extension: fold_loci's
            # two sweep directions close a band's boundary at its ends, so
            # the ray count is the topological winding against a closed
            # boundary - extending each piece to infinity instead stacked
            # spurious crossings and blew the S-line document up to depth
            # 7 on ground the map covers twice.
            thirds[id(arc_curve)] = [tuple(t) for t in third_curve]
            keep.append(arc_curve)
            curves.append(arc_curve)
        map_point.warp_curve_child = ids
        map_point.warp_curve_third = thirds
        map_point.warp_curve_keep = keep
        return curves

    if not stitch:
        # The fill splitter wants the SET of loci, not paths: every curve a
        # single locus (injected corner lines arrive whole), so its ring
        # crossings pair correctly along each cutter. Junction bridges and
        # any chained arms the path-building below may produce are for
        # drawing and anchoring only - see _stitch_crease. Phantom curves
        # the field never flips across (_curve_is_fold) are dropped here so
        # they neither cut fills nor shift depth counts.
        return _with_warp_loci(
            [curve for curve in pieces
             if len(curve) >= 2 and _curve_is_fold(map_point, curve)])
    stitched = _stitch_crease(pieces, 4.0 * POLY_STEP, boundaries)
    return _with_warp_loci(
        [curve for curve in stitched if len(curve) >= 2])


def _split_at_cusps(points):
    """Break a crease polyline where it reverses on itself.

    ON the fold the two guide directions are parallel by definition, so the
    crease point H(s_x + a(t)) + V(t_x + t) - O has velocity
    (a'(t) +/- 1) * T_h: one fixed direction times a scalar. Where that scalar
    passes through zero the curve stops dead and comes back along itself - a
    cusp - and drawing it as one stroke ties the visible knot.

    Measured on the reported project: the branch whose slide rate a'(t) stayed
    below 1 (mean 0.48) never stalled - minimum step 3.72 px, no crossing -
    while the branch whose rate crossed 1 (max 2.58) fell to a 0.20 px step
    and crossed itself once. Note the arc stays MONOTONE through this, which
    is why splitting on arc reversal missed it entirely.
    """
    if len(points) < 3:
        return [points]
    pieces = []
    start = 0
    for index in range(1, len(points) - 1):
        ax = points[index][0] - points[index - 1][0]
        ay = points[index][1] - points[index - 1][1]
        bx = points[index + 1][0] - points[index][0]
        by = points[index + 1][1] - points[index][1]
        if math.hypot(ax, ay) < 1e-9 or math.hypot(bx, by) < 1e-9:
            continue
        if ax * bx + ay * by < 0.0:           # direction reversed: a cusp
            if index + 1 - start >= 2:
                pieces.append(points[start:index + 1])
            start = index
    if len(points) - start >= 2:
        pieces.append(points[start:])
    return pieces


def _polyline_arc_of(point, points):
    """(distance, arc along `points`) of the closest point on the polyline."""
    best = (float("inf"), 0.0)
    run = 0.0
    for a, b in zip(points, points[1:]):
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length <= 0.0:
            continue
        t = min(1.0, max(0.0, ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy)
                         / (length * length)))
        gap = math.hypot(point[0] - (a[0] + dx * t), point[1] - (a[1] + dy * t))
        if gap < best[0]:
            best = (gap, run + t * length)
        run += length
    return best


def _polyline_slice(points, start, end):
    """The part of a polyline between two arc positions along it."""
    sliced = []
    run = 0.0
    for a, b in zip(points, points[1:]):
        length = math.hypot(b[0] - a[0], b[1] - a[1])
        if length <= 0.0:
            continue
        low, run = run, run + length
        t0 = max(0.0, (start - low) / length)
        t1 = min(1.0, (end - low) / length)
        if t1 <= t0:
            continue
        if not sliced:
            sliced.append(_lerp(a, b, t0))
        sliced.append(_lerp(a, b, t1))
    return sliced


def _trim_to_anchors(run, cuts, tolerance):
    """Keep only the part of a crease that lies BETWEEN two cuts.

    The cuts are the only VERTICES the drawing gives the crease: the frames
    say where a fold COULD be, a cut says where one IS.

    A fold ends at a vertex or at the edge of the material; one that just
    stops in the middle of an intact sheet is not a fold. The crease here is
    derived from the FRAMES (§4.5), so it exists along the whole guide whether
    or not anything is drawn there, and the stretch past the outermost cut is
    a locus rather than a fold - nothing changes side across it, so there is
    no front edge for it to terminate. Measured on the reported project, that
    unanchored stretch was 61% of everything drawn, including a 242 px tail.

    Between two cuts the crease is kept even where no stroke touches it. The
    alternative - pair each cut with the one the same RUN of artwork joins it
    to, and keep only those spans - is stricter and was measured: on the two
    reported projects it left 7.6 px of 445 and 0.0 px of 220, because a line
    drawing mostly crosses a fold ONCE per stroke (16 cuts there yielded 6
    two-ended runs), so the pairs that survive are between strokes anyway.

    The cost of the rule is a branch carrying exactly ONE cut: it is dropped
    whole, so a fold the artwork crosses only once gets no crease at all even
    though its BACK run is real and its front edge is left unterminated. That
    is the intended behaviour here - a single crossing gives a point, and a
    crease needs an interval, and inventing one is what this function exists
    to stop - but it is a choice, not a theorem. It costs nothing on dense
    line art, where every branch collects several cuts; it costs the whole
    crease on a sketch of three strokes over the same guides.
    """
    if len(run) < 2 or len(cuts) < 2:
        return []
    arcs = sorted(arc for gap, arc in (_polyline_arc_of(cut, run) for cut in cuts)
                  if gap <= tolerance)
    if len(arcs) < 2:
        return []
    return _polyline_slice(run, arcs[0], arcs[-1])


def _stroke_style(stroke, width_scale):
    color = stroke.get("color") or {}
    color_tuple = (int(color.get("r", 0)), int(color.get("g", 0)),
                   int(color.get("b", 0)), int(color.get("a", 255)))
    width = max(0.5, float(stroke.get("width", 3.0)) * width_scale)
    return color_tuple, width


# ---------------------------------------------------------------------------
# fold depth (layer stacking from the nearest-end anchor) and fill topology
# ---------------------------------------------------------------------------

def _prepare_fold_context(map_point, h_range, v_range):
    """Attach the depth machinery to the mapper, once per run.

    Depth of a sheet point = number of creases crossed between it and the
    nearest-end anchor, walked as a straight segment in ARC space (where the
    loci are clean disjoint curves). Depth decides layer STACKING only;
    front/back COLORING stays with _fold_sign, and a parity mismatch between
    the two (a graze, a missed tangency) is settled in _fold_sign's favour by
    bumping the depth one step - each crease crossed flips the face, so depth
    parity and face must agree.
    """
    main = map_point.main_frame
    anchor = _nearest_arc()
    # Clamp a stale anchor (guides redrawn shorter since it was stored) onto
    # the sheet, and FOLD IT INTO THE SWEEP WINDOW: crossings between the
    # anchor and the artwork that fall outside the traced window would be
    # silently missed, and the parity fix would then merge distinct fold
    # layers instead of stacking them.
    anchor = (min(max(anchor[0], -main.h_arc), main.h_total - main.h_arc),
              min(max(anchor[1], -main.v_arc), main.v_total - main.v_arc))
    pad = 4.0 * POLY_STEP
    map_point.depth_curves = _crease_curves(
        map_point,
        (min(v_range[0], anchor[1]) - pad, max(v_range[1], anchor[1]) + pad),
        (min(h_range[0], anchor[0]) - pad, max(h_range[1], anchor[0]) + pad),
        stitch=False, corner_spans=(h_range, v_range))
    map_point.depth_anchor = anchor
    warp = getattr(map_point, "warp", None)
    if warp is not None:
        # The anchor's BASE Third position, for counting warp loci in
        # their own unfolding space. The anchor normally sits outside
        # every pink line's band, where unapply is the exact identity;
        # inside a fold band it is best-effort like every other consumer.
        map_point.depth_anchor_third = warp.unapply(
            map_point.unscale_arcs(*anchor))
    else:
        map_point.depth_anchor_third = None
    map_point.child_cutters = None
    # Build the cutters NOW, not lazily: for warp loci this also swaps the
    # depth-ray Third geometry to the extended cutter's coords preimage,
    # and the first depth query (stroke runs come before fills) must
    # already see the aligned geometry.
    _child_cutters(map_point)


def _arc_of_point(map_point, point):
    """A child point's MAIN arc coordinates (the space depth is counted in).

    Routed through the additional-line warp: depth counting, the loci and
    the cutters must all live in the SAME arc space the map evaluates in,
    or cuts drift off creases (the positional-consistency rule).
    """
    l_h, l_v = map_point.coords(point)
    warp = getattr(map_point, "warp", None)
    if warp is not None:
        l_h, l_v = warp.apply((l_h, l_v))
    return (l_h * (map_point.h_scales[1] if l_h >= 0.0 else map_point.h_scales[0]),
            l_v * (map_point.v_scales[1] if l_v >= 0.0 else map_point.v_scales[0]))


def _fold_depth(map_point, point, side):
    """Stacking depth of a child point (0 = nearest to the viewer).

    Each crease is counted in ITS OWN unfolding space - the space where
    crossing it really is stepping onto another sheet. Frame creases
    unfold in main-arc space (hv is single-valued there), and their count
    applies uniformly to every warp sheet stacked at an arc position.
    Warp creases unfold in BASE Third space - by the time the arcs exist
    the warp has already folded, so a warp locus in arc space is a fold
    edge IMAGE whose two sides are the same sheet; counting it there
    lifted a physically single-sheet region to depth 2 (add_s_error).
    """
    curves = getattr(map_point, "depth_curves", None)
    if not curves:
        return 0 if side == _MappedOutput.FRONT else 1
    anchor = map_point.depth_anchor
    warp_third = getattr(map_point, "warp_curve_third", {}) or {}
    anchor_third = getattr(map_point, "depth_anchor_third", None)
    # One frame solve, shared by both rays; the arc ray is derived (and
    # the warp applied) only if a frame curve actually needs it.
    point_third = None
    arc = None
    depth = 0
    face = 0
    warp = getattr(map_point, "warp", None)
    if warp is not None and getattr(warp, "has_faces", False):
        point_third = map_point.coords(point)
        face = warp.face_at(point_third)
    for curve in curves:
        native = warp_third.get(id(curve)) if warp_third else None
        if native is not None and anchor_third is not None:
            if point_third is None:
                point_third = map_point.coords(point)
            ray_a, ray_b = anchor_third, point_third
            segments = zip(native, native[1:])
        else:
            if arc is None:
                if point_third is None:
                    point_third = map_point.coords(point)
                z = point_third
                warp = getattr(map_point, "warp", None)
                if warp is not None:
                    z = warp.apply(z)
                arc = map_point.scale_arcs(*z)
            ray_a, ray_b = anchor, arc
            segments = zip(curve, curve[1:])
        for a, b in segments:
            hit = _segment_intersection(ray_a, ray_b, a, b)
            if hit is None:
                continue
            t, u = hit
            # Half-open on both parameters so a crossing shared by two
            # consecutive segments is counted once.
            if 0.0 <= t < 1.0 and 0.0 <= u < 1.0:
                depth += 1
    if (depth % 2 == 0) != (side == _MappedOutput.FRONT):
        # A parity mismatch means one crossing was miscounted; +1 assumes
        # a MISSED crease (deeper), -1 a SPURIOUS one (a graze counted
        # twice). Both are physical. A C-strategy convex label is the
        # evidence that picks: the convex (front) side resolves TOWARD
        # the viewer. The red handle keeps its authority - it still sets
        # the anchor and supplies the count, and wherever count and
        # colour already agree this branch is never reached. Either
        # direction moves depth by exactly 1, so parity (and with it the
        # count/colour consistency) is preserved by construction.
        depth = depth - 1 if (face > 0 and depth > 0) else depth + 1
    return depth


def _run_depth(map_point, run, side):
    if not _FOLD["split"] or len(run) == 0:
        return 0 if side == _MappedOutput.FRONT else 1
    return _fold_depth(map_point, run[len(run) // 2], side)


def _child_cutters(map_point):
    """The crease loci as polylines in CHILD image space, for cutting fills.

    The depth curves live in MAIN arc coordinates; dividing by the per-side
    transfer scales gives child arcs, and the child frame's hv puts them on
    the texture. In child space the loci are plain curves - the cusps are an
    IMAGE-space phenomenon - so they are valid polygon cutters.

    The depth curves are the raw locus SET (stitch=False), so where loci
    cross, a curve can END at the junction - INSIDE a fill. A cutter that
    dead-ends inside a ring crosses it an odd number of times and cuts
    nothing (see t_cutter), which was measured to collapse a three-depth
    fill into one uncut piece. Ends are therefore extended STRAIGHT until
    they leave everything the pattern can occupy: a cut along the extension
    is harmless - the two sides have the same crossing parity against the
    real loci, so they land at the same depth and colour - while a missing
    cut loses whole occlusion bands. The pre-extension geometry is kept on
    map_point.child_cutters_raw: crease anchors must come only from
    crossings with REAL loci, never with an extension.
    """
    cutters = getattr(map_point, "child_cutters", None)
    if cutters is not None:
        return cutters
    child = map_point.child_frame
    h_scales, v_scales = map_point.h_scales, map_point.v_scales
    reach = 2.0 * (child.gh.total + child.gv.total)
    bare = []
    direct = getattr(map_point, "warp_curve_child", {}) or {}
    for curve in getattr(map_point, "depth_curves", None) or []:
        if id(curve) in direct:
            # A warp fold locus carries its exact child geometry: the
            # fixed-point unwarp is unreliable precisely where the warp
            # folds, so it is bypassed.
            points = list(direct[id(curve)])
        else:
            # Through _child_of_arcs so the additional-line warp's inverse
            # is applied: a cutter must lie where the WARPED map folds.
            points = [_child_of_arcs(map_point, arc_pair) for arc_pair in curve]
        if len(points) >= 2:
            bare.append(points)
    cutters, raw = _cutter_polylines(bare, reach)
    map_point.child_cutters = cutters
    map_point.child_cutters_raw = raw
    return cutters


def _cutter_polylines(bare, reach):
    """(cutters, raw) from bare locus polylines: the shared construction of
    _child_cutters and _sever_cutters.

    Every end gets a straight extension of the full reach. Junction ends
    and dedupe splice ends dead-end INSIDE fills and shorter overhangs
    measurably under-cut (a depth-3 fill vanished at 32 px, and distance
    heuristics for "which ends are junctions" kept missing cases because
    arc-space gaps stretch unpredictably through the transfer scales). The
    cost is cosmetic only - extension crossings split same-depth pieces
    that render identically - and the bbox gate in _split_ring_by_fold
    keeps them off rings the real locus never approaches. `raw` keeps the
    pre-extension geometry: crease anchors and bbox gates must see only
    REAL loci, never an extension."""
    raw = [_densify(list(points)) for points in bare]
    cutters = []
    for points in bare:
        points = list(points)
        closed = (len(points) >= 3
                  and math.hypot(points[0][0] - points[-1][0],
                                 points[0][1] - points[-1][1]) <= 1e-6)
        if not closed:
            # A CLOSED locus (a bounded severed island's contour) needs no
            # extension - it already crosses any straddling ring an even
            # number of times. Bolting two reach-long rays onto its seam
            # vertex grew multi-thousand-px spikes out of small islands.
            for end, other in ((0, 1), (-1, -2)):
                dx = points[end][0] - points[other][0]
                dy = points[end][1] - points[other][1]
                length = math.hypot(dx, dy)
                if length <= 1e-9:
                    continue
                tip = points[end]
                extended = (tip[0] + dx / length * reach, tip[1] + dy / length * reach)
                if end == 0:
                    points.insert(0, extended)
                else:
                    points.append(extended)
        cutters.append(_densify(points))
    return cutters, raw


def _sever_curve_is_real(map_point, curve, probe=POLY_STEP, samples=5):
    """Does the VALIDITY verdict actually change across this traced seam?

    The severing twin of _curve_is_fold (same _locus_flips loop): the
    child-frame sweeps inherit the same phantom failure mode (near-corner
    rows chaining isolated per-row zeros into a long pseudo-curve the field
    never flips across). A phantom seam would slice fills cosmetically;
    probing third_of one step to each side separates real seams from
    phantoms. Probes run in CHILD CANVAS space - the space the cutters cut
    in - and each pair is judged under BOTH seedings, either flip counts:
      * the default chord seed sees a tangential fold's shadow side as
        unreachable (there is nothing to converge to) - but beside a
        RE-EMERGENCE seam it converges onto the far front sheet from both
        sides, so both probes read "valid" and a real seam was dropped as
        a phantom, leaving fills uncut where the strokes' verdict re-opens;
      * the locus's own Third coordinate as seed makes Newton report the
        LOCAL sheet, whose det genuinely flips at a re-emergence corner -
        but ON a tangential fold that seed is the singular point itself,
        where the solve stalls on both sides.
    A phantom flips under neither: no local zero crossing, and no global
    coverage change.
    """
    child = map_point.child_frame
    third_of = map_point.third_of

    def flips(p, q, entry):
        if third_of(p)[2] != third_of(q)[2]:
            return True
        return third_of(p, seed=entry)[2] != third_of(q, seed=entry)[2]

    return _locus_flips(curve, lambda entry: child.hv(*entry), flips,
                        probe, samples)


def _sever_loci(map_point):
    """The CHILD frame's fold loci in Third space, cached on the mapper.

    These are the UV SEAMS of the staged pipeline: the curves where
    det J_child = 0, on whose far side the Third lift stops existing. Same
    dual-sweep + corner-line + dedup machinery as the main-frame crease
    tracer, pointed at the child frame - the traced (l_h, l_v) pairs ARE
    Third coordinates, no unscaling, no warp (the additional-line warp acts
    AFTER the lift and cannot create or move child-frame folds). The window
    is frame-global (the guides bound where a fold can live), so the result
    is pattern-independent and computed once per mapper.
    """
    cached = getattr(map_point, "sever_curves", None)
    if cached is not None:
        return cached
    if not map_point.can_fold():
        map_point.sever_curves = []
        return map_point.sever_curves
    child = map_point.child_frame
    h_pad = 0.5 * child.h_total
    v_pad = 0.5 * child.v_total
    h_span = (-child.h_arc, child.h_total - child.h_arc)
    v_span = (-child.v_arc, child.v_total - child.v_arc)
    h_range = (h_span[0] - h_pad, h_span[1] + h_pad)
    v_range = (v_span[0] - v_pad, v_span[1] + v_pad)
    h_good, h_bad = _crease_scan(map_point, v_range, "h", frame=child)
    v_good, v_bad = _crease_scan(map_point, h_range, "v", frame=child)
    corner = _corner_loci(map_point, v_range, h_range,
                          spans=(h_span, v_span), frame=child)
    pieces = _merged_loci(
        (corner, h_good, v_good, h_bad, v_bad),
        keep=lambda run: _sever_curve_is_real(map_point, run))
    map_point.sever_curves = pieces
    return pieces


def _sever_cutters(map_point, grid_n=64):
    """The VALIDITY BOUNDARY as ring cutters in CHILD canvas space, cached.

    Marched from the verdict field itself (third_of), not projected from
    the traced Third loci: strokes sever wherever the pointwise verdict
    changes, and that boundary includes DIVERGENCE edges - ground where the
    chord-seed Newton stops reaching any front-branch preimage - which no
    Third locus can express. Measured on the zig frame: the strokes
    re-open at canvas x~273.5, while the nearest traced locus projects to
    the x=0 line - a loci-projected cutter never cuts fills there at all,
    and even where a locus existed its image sat 1.2 px off the stroke
    cuts (locus corner at x=120 vs windowed verdict edge at x=118.79).
    Cutting from the same field the strokes consult makes fills and
    strokes agree on where the pattern ends BY CONSTRUCTION.

    Marching squares over a padded frame window: verdict at the lattice
    nodes, each crossing edge bisected onto the verdict boundary
    (sub-pixel), cell segments chained into polylines, and straight end
    extensions applied to OPEN chains (_cutter_polylines) so a boundary
    leaving the window still cuts whole fills; closed contours stay
    closed (cut cyclically by _cut_ring_by_polyline) and add a straight
    slicer through their centre so a fully-contained shadow island still
    partitions its ring. map_point.sever_cutter_bounds carries each
    cutter's extended bbox for _sever_ring's skip gate. A verdict
    structure thinner than one cell (~window/64) can slip between the
    nodes - the same class of limit the loci tracer's phantom filter
    already accepted. Cached per mapper; free on frames that cannot fold.
    """
    cached = getattr(map_point, "sever_cutter_polys", None)
    if cached is not None:
        return cached
    if not map_point.can_fold():
        map_point.sever_cutter_polys = []
        map_point.sever_cutter_bounds = []
        return map_point.sever_cutter_polys
    child = map_point.child_frame
    third_of = map_point.third_of

    # Canvas window: hv = H + V - O componentwise, so the reachable canvas
    # box is the H-extent plus the V-extent (each over its padded arc
    # range, linear extensions included) minus the crossing.
    def curve_box(curve, arc, low, high, steps=33):
        xs, ys = [], []
        for k in range(steps):
            p = curve.point_at(arc + low + (high - low) * k / (steps - 1))
            xs.append(p[0])
            ys.append(p[1])
        return min(xs), min(ys), max(xs), max(ys)

    h_pad = 0.5 * child.h_total
    v_pad = 0.5 * child.v_total
    hx0, hy0, hx1, hy1 = curve_box(child.gh, child.h_arc,
                                   -child.h_arc - h_pad,
                                   child.h_total - child.h_arc + h_pad)
    vx0, vy0, vx1, vy1 = curve_box(child.gv, child.v_arc,
                                   -child.v_arc - v_pad,
                                   child.v_total - child.v_arc + v_pad)
    x0 = hx0 + vx0 - child.origin[0]
    x1 = hx1 + vx1 - child.origin[0]
    y0 = hy0 + vy0 - child.origin[1]
    y1 = hy1 + vy1 - child.origin[1]
    # The verdict field keeps changing BEYOND the sheet's image box: a
    # silhouette edge sits just outside it (the hook frame's at x=160.19
    # with the box ending at 160.0 - zero crossings sampled, no cutter at
    # all), and divergence edges bound the solvable region around it.
    # Widen by a quarter span each side; chains that cross the window get
    # straight-extended to full reach anyway.
    x0, x1 = x0 - 0.25 * (x1 - x0), x1 + 0.25 * (x1 - x0)
    y0, y1 = y0 - 0.25 * (y1 - y0), y1 + 0.25 * (y1 - y0)
    dx = (x1 - x0) / grid_n
    dy = (y1 - y0) / grid_n

    valid = [[third_of((x0 + dx * i, y0 + dy * j))[2]
              for j in range(grid_n + 1)] for i in range(grid_n + 1)]

    def edge_point(ax, ay, bx, by, a_ok):
        """Bisect the verdict change on one lattice edge onto the boundary."""
        for _ in range(14):
            mx, my = (ax + bx) * 0.5, (ay + by) * 0.5
            if third_of((mx, my))[2] == a_ok:
                ax, ay = mx, my
            else:
                bx, by = mx, my
        return ((ax + bx) * 0.5, (ay + by) * 0.5)

    crossings = {}   # ("h"/"v", i, j) -> boundary point on that edge

    def edge(kind, i, j, ax, ay, bx, by, a_ok):
        key = (kind, i, j)
        point = crossings.get(key)
        if point is None:
            point = edge_point(ax, ay, bx, by, a_ok)
            crossings[key] = point
        return point

    segments = []
    for i in range(grid_n):
        for j in range(grid_n):
            sw = valid[i][j]
            se = valid[i + 1][j]
            nw = valid[i][j + 1]
            ne = valid[i + 1][j + 1]
            if sw == se == nw == ne:
                continue
            xa, xb = x0 + dx * i, x0 + dx * (i + 1)
            ya, yb = y0 + dy * j, y0 + dy * (j + 1)
            sides = []
            if sw != se:
                sides.append(edge("h", i, j, xa, ya, xb, ya, sw))
            if nw != ne:
                sides.append(edge("h", i, j + 1, xa, yb, xb, yb, nw))
            if sw != nw:
                sides.append(edge("v", i, j, xa, ya, xa, yb, sw))
            if se != ne:
                sides.append(edge("v", i + 1, j, xb, ya, xb, yb, se))
            if len(sides) == 2:
                segments.append((sides[0], sides[1]))
            elif len(sides) == 4:
                # Saddle: the centre verdict decides which arms pair up.
                centre_ok = third_of(((xa + xb) * 0.5, (ya + yb) * 0.5))[2]
                bottom, top, left, right = sides
                if centre_ok == sw:
                    segments.append((left, top))
                    segments.append((bottom, right))
                else:
                    segments.append((left, bottom))
                    segments.append((top, right))

    # Chain the cell segments into polylines by shared endpoints.
    def key_of(point):
        return (round(point[0], 6), round(point[1], 6))

    links = {}
    for a, b in segments:
        links.setdefault(key_of(a), []).append((a, b))
        links.setdefault(key_of(b), []).append((b, a))
    used = set()
    bare = []
    for a, b in segments:
        if (key_of(a), key_of(b)) in used:
            continue
        chain = [a, b]
        used.add((key_of(a), key_of(b)))
        used.add((key_of(b), key_of(a)))
        for grow_end in (True, False):
            while True:
                tip = chain[-1] if grow_end else chain[0]
                extended = False
                for start, far in links.get(key_of(tip), ()):
                    pair = (key_of(start), key_of(far))
                    if pair in used:
                        continue
                    used.add(pair)
                    used.add((pair[1], pair[0]))
                    if grow_end:
                        chain.append(far)
                    else:
                        chain.insert(0, far)
                    extended = True
                    break
                if not extended:
                    break
        if len(chain) >= 2:
            bare.append(chain)

    reach = 2.0 * (child.gh.total + child.gv.total)
    cutters, _raw = _cutter_polylines(bare, reach)
    # A CLOSED contour cannot express a fully-contained shadow island as a
    # hole (this pipeline's fill pieces are simple rings), so each closed
    # chain also contributes one straight SLICER through its centre: a
    # ring containing the island gets sliced into simple pieces whose
    # votes then drop the island's ground - the deterministic version of
    # what the removed end-extension spikes used to achieve by accident.
    # Slicer cuts away from the island are cosmetic (same-verdict pieces).
    # Slicers go FIRST: each cutter is applied once, and a contour lying
    # strictly inside a ring has no boundary crossings to cut with until
    # the slicer has split the ring through the island.
    slicers = []
    for chain in bare:
        if (len(chain) >= 3
                and math.hypot(chain[0][0] - chain[-1][0],
                               chain[0][1] - chain[-1][1]) <= 1e-6):
            xs = [p[0] for p in chain]
            ys = [p[1] for p in chain]
            cy = (min(ys) + max(ys)) * 0.5
            cx = (min(xs) + max(xs)) * 0.5
            # Densified like every other cutter: chord vertices are spliced
            # verbatim into the pieces and mapped one-for-one, so a bare
            # 2-point slicer left a full-width straight edge that diverged
            # 40 px from the warped image of the same line.
            slicers.append(_densify([(cx - reach, cy), (cx + reach, cy)]))
    cutters[:0] = slicers
    map_point.sever_cutter_polys = cutters
    # The skip gate must see the EXTENDED geometry: a sever extension is a
    # real (straight-continued) validity boundary, not the crease system's
    # parity-neutral cutting aid - gating on the window-clamped bare
    # geometry let a fill beyond the marched window skip the cutter and be
    # kept or wiped whole by the vote while the strokes over the same
    # ground were cut.
    map_point.sever_cutter_bounds = [
        (min(p[0] for p in cutter), min(p[1] for p in cutter),
         max(p[0] for p in cutter), max(p[1] for p in cutter))
        for cutter in cutters]
    return cutters


def _point_in_ring(point, ring):
    """Odd-even test against a closed ring (implicit closing edge)."""
    inside = False
    n = len(ring)
    for i in range(n):
        a = ring[i]
        b = ring[(i + 1) % n]
        if (a[1] > point[1]) != (b[1] > point[1]):
            span = b[1] - a[1]
            x = a[0] + (b[0] - a[0]) * (point[1] - a[1]) / span
            if point[0] < x:
                inside = not inside
    return inside


def _point_in_polygons(point, polygons):
    """Odd-even over a polygon list (the mapping-area convention)."""
    inside = False
    for polygon in polygons or []:
        if len(polygon) >= 3 and _point_in_ring(point, polygon):
            inside = not inside
    return inside


def _ring_nesting_level(rings, index):
    """Median odd-even nesting depth of rings[index] among the other rings.

    A boundary vertex is never inside a subpath nested within its own ring;
    the median over a few spread vertices shrugs off a vertex that grazes
    another ring's edge. (Shared by _emit_fills and the Fukusato fill
    triangulation - the donut/letter-O fix must live in one place.)
    """
    ring = rings[index]
    count = min(5, len(ring))
    levels = sorted(
        sum(1 for j, other in enumerate(rings)
            if j != index
            and _point_in_ring(ring[(k * len(ring)) // count], other))
        for k in range(count))
    return levels[len(levels) // 2]


def _ring_interior_point(ring):
    """A point strictly inside the ring.

    The centroid first; then edge midpoints nudged INWARD along the edge
    normal (both signs tried). Plain vertex-pair midpoints were not enough:
    on a densified ring the midpoint of two vertices spanning a locally
    straight stretch lies exactly ON the boundary, and the fold side of such
    a point is decided by rounding noise - after a crease cut, part of every
    piece's boundary IS the crease.
    """
    n = len(ring)
    cx = sum(p[0] for p in ring) / n
    cy = sum(p[1] for p in ring) / n
    if _point_in_ring((cx, cy), ring):
        return (cx, cy)
    stride = max(1, n // 12)
    for i in range(0, n, stride):
        a = ring[i]
        b = ring[(i + 1) % n]
        mx, my = (a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length <= 1e-9:
            continue
        nx, ny = -dy / length, dx / length
        for offset in (0.35, -0.35, 1.0, -1.0):
            candidate = (mx + nx * offset, my + ny * offset)
            if _point_in_ring(candidate, ring):
                return candidate
    return (cx, cy)


def _cut_ring_by_polyline(ring, cutter, crossings_out=None):
    """Split a closed ring by an open polyline; returns the resulting rings.

    Crossings of the cutter with the ring boundary are collected exactly and
    ordered ALONG THE CUTTER; each stretch of cutter between two consecutive
    crossings whose midpoint lies inside the ring is a CHORD, and every chord
    splits the ring piece containing its endpoints in two. Consecutive
    stretches alternate inside/outside (each crossing is transversal), so
    chords never share endpoints and never cross each other, which is what
    lets them be applied one at a time.

    A locus that dead-ends INSIDE the ring (a fold pair born within the fill)
    contributes no chord and does not split - correct, since the region wraps
    over such a fold without its boundary changing sides.
    """
    n = len(ring)
    # Bounding-box gate: both operands are densified at POLY_STEP, so the
    # naive all-pairs intersection scan grows with board size squared; most
    # cutter segments are nowhere near the ring.
    ring_x0 = min(p[0] for p in ring)
    ring_x1 = max(p[0] for p in ring)
    ring_y0 = min(p[1] for p in ring)
    ring_y1 = max(p[1] for p in ring)
    cut_x0 = min(p[0] for p in cutter)
    cut_x1 = max(p[0] for p in cutter)
    cut_y0 = min(p[1] for p in cutter)
    cut_y1 = max(p[1] for p in cutter)
    if (cut_x1 < ring_x0 or cut_x0 > ring_x1
            or cut_y1 < ring_y0 or cut_y0 > ring_y1):
        return [ring]
    crossings = []
    for j in range(len(cutter) - 1):
        c0, c1 = cutter[j], cutter[j + 1]
        if (max(c0[0], c1[0]) < ring_x0 or min(c0[0], c1[0]) > ring_x1
                or max(c0[1], c1[1]) < ring_y0 or min(c0[1], c1[1]) > ring_y1):
            continue
        for i in range(n):
            a, b = ring[i], ring[(i + 1) % n]
            hit = _segment_intersection(a, b, c0, c1)
            if hit is None:
                continue
            t, u = hit
            if 0.0 <= t < 1.0 and 0.0 <= u < 1.0:
                point = (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)
                crossings.append((j + u, point))
    if len(crossings) < 2:
        return [ring]
    crossings.sort(key=lambda entry: entry[0])
    deduped = [crossings[0]]
    for entry in crossings[1:]:
        if entry[0] - deduped[-1][0] > 1e-9:
            deduped.append(entry)
    crossings = deduped
    if len(crossings) < 2:
        return [ring]

    def cutter_point(position):
        j = min(len(cutter) - 2, int(position))
        u = position - j
        a, b = cutter[j], cutter[j + 1]
        return (a[0] + (b[0] - a[0]) * u, a[1] + (b[1] - a[1]) * u)

    def cutter_slice(p0, p1):
        points = [crossings[p0][1]]
        j0 = int(crossings[p0][0])
        j1 = int(crossings[p1][0])
        for j in range(j0 + 1, j1 + 1):
            points.append(cutter[j])
        points.append(crossings[p1][1])
        return points

    chords = []
    for k in range(len(crossings) - 1):
        mid = cutter_point((crossings[k][0] + crossings[k + 1][0]) * 0.5)
        if _point_in_ring(mid, ring):
            chords.append(cutter_slice(k, k + 1))
            if crossings_out is not None:
                # The chord's endpoints are where this ring genuinely changes
                # side - a fill's equivalent of a stroke's cut, and the
                # anchors the crease needs to span the fill's fold edge.
                crossings_out.append(crossings[k][1])
                crossings_out.append(crossings[k + 1][1])
    if (len(crossings) >= 2 and len(cutter) >= 3
            and math.hypot(cutter[0][0] - cutter[-1][0],
                           cutter[0][1] - cutter[-1][1]) <= 1e-6):
        # A CLOSED cutter (a marched contour around a severed island) is
        # cyclic: the stretch that wraps past its parameter seam is a
        # chord like any other, invisible to the consecutive-pairs walk
        # above - whether a straddling ring got cut used to depend on
        # where the chaining happened to start the loop.
        total = float(len(cutter) - 1)
        first, last = crossings[0], crossings[-1]
        m = (last[0] + first[0] + total) * 0.5
        if m >= total:
            m -= total
        if _point_in_ring(cutter_point(m), ring):
            wrapped = [last[1]]
            for j in range(int(last[0]) + 1, len(cutter)):
                wrapped.append(cutter[j])
            for j in range(1, int(first[0]) + 1):
                wrapped.append(cutter[j])
            wrapped.append(first[1])
            chords.append(wrapped)
            if crossings_out is not None:
                crossings_out.append(last[1])
                crossings_out.append(first[1])

    pieces = [list(ring)]
    for chord in chords:
        for index, piece in enumerate(pieces):
            split = _split_ring_with_chord(piece, chord)
            if split is not None:
                pieces[index:index + 1] = split
                break
    return pieces


def _split_ring_with_chord(ring, chord):
    """Split one ring by a chord whose ends lie on its boundary, or None."""
    n = len(ring)

    def locate(point):
        best = None
        for i in range(n):
            a, b = ring[i], ring[(i + 1) % n]
            dx, dy = b[0] - a[0], b[1] - a[1]
            length_sq = dx * dx + dy * dy
            if length_sq <= 0.0:
                continue
            t = ((point[0] - a[0]) * dx + (point[1] - a[1]) * dy) / length_sq
            t = min(1.0, max(0.0, t))
            gx = point[0] - (a[0] + dx * t)
            gy = point[1] - (a[1] + dy * t)
            gap = gx * gx + gy * gy
            if best is None or gap < best[0]:
                best = (gap, i, t)
        return best

    start = locate(chord[0])
    end = locate(chord[-1])
    if start is None or end is None or start[0] > 0.25 or end[0] > 0.25:
        return None

    def boundary_path(a_edge, a_t, b_edge, b_t):
        """Ring vertices strictly between (a_edge, a_t) and (b_edge, b_t),
        walking forward; the chord endpoints themselves are appended by the
        caller."""
        if a_edge == b_edge and a_t <= b_t:
            return []
        points = []
        edge = (a_edge + 1) % n
        while True:
            points.append(ring[edge])
            if edge == b_edge:
                return points
            edge = (edge + 1) % n
            if len(points) > n:
                return points  # safety net; cannot loop past a full circle

    _, e0, t0 = start
    _, e1, t1 = end
    interior = chord[1:-1]
    side_a = [chord[0]] + boundary_path(e0, t0, e1, t1) + [chord[-1]] + list(reversed(interior))
    side_b = [chord[-1]] + boundary_path(e1, t1, e0, t0) + [chord[0]] + list(interior)
    result = []
    for candidate in (side_a, side_b):
        cleaned = [candidate[0]]
        for point in candidate[1:]:
            if math.hypot(point[0] - cleaned[-1][0], point[1] - cleaned[-1][1]) > 1e-9:
                cleaned.append(point)
        if (len(cleaned) >= 3
                and math.hypot(cleaned[0][0] - cleaned[-1][0],
                               cleaned[0][1] - cleaned[-1][1]) <= 1e-9):
            cleaned.pop()
        if len(cleaned) >= 3:
            result.append(cleaned)
    return result if len(result) == 2 else None


def _ring_on_valid_ground(map_point, ring, samples=9):
    """Is this ring piece on computable ground? A VOTE over spread samples
    nudged just INSIDE the boundary, never one interior probe.

    A single interior probe was measured to (a) drop a hole ring whose
    centre sat in shadow, painting the cut-out solid, (b) delete a whole
    donut over one isolated Newton-residual speckle at the shared centre,
    and (c) discard a piece spanning severed and lit ground wholesale (an
    interior point of a donut's outer ring is the HOLE - ground the fill
    does not even own). Raw boundary VERTICES are not usable either: after
    a cut, one whole edge of the piece is the seam chord, whose densified
    vertices all sit on the same side of the numeric boundary and out-vote
    the piece's real outline both ways. So each sample is an edge midpoint
    nudged 0.35 px inward (_ring_interior_point's proven trick) - a point
    just inside the piece is the piece's OWN ground: off the seam by
    construction, adjacent to the outline everywhere else. Ties keep the
    piece - fills err on the side of drawing.
    """
    n = len(ring)
    if n == 0:
        return False
    third_of = map_point.third_of
    count = min(samples, n)
    votes = 0
    for k in range(count):
        i = (k * n) // count
        a = ring[i]
        b = ring[(i + 1) % n]
        probe = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
        dx, dy = b[0] - a[0], b[1] - a[1]
        length = math.hypot(dx, dy)
        if length > 1e-9:
            nx, ny = -dy / length, dx / length
            for offset in (0.35, -0.35):
                candidate = (probe[0] + nx * offset, probe[1] + ny * offset)
                if _point_in_ring(candidate, ring):
                    probe = candidate
                    break
        votes += 1 if third_of(probe)[2] else -1
    return votes >= 0


def _sever_ring(map_point, ring, gate_bbox=None):
    """Cut a closed ring along the child frame's UV seams and keep only the
    pieces on computable ground - the severing stage of _split_ring_by_fold,
    shared with the 3D reconstruction (which must not drape geometry the 2D
    pipeline deleted). Free on frames that cannot fold."""
    if not map_point.can_fold():
        return [ring]
    if gate_bbox is None:
        rx0 = min(p[0] for p in ring)
        rx1 = max(p[0] for p in ring)
        ry0 = min(p[1] for p in ring)
        ry1 = max(p[1] for p in ring)
    else:
        rx0, ry0, rx1, ry1 = gate_bbox
    margin = 2.0 * POLY_STEP
    pieces = [ring]
    sever_cutters = _sever_cutters(map_point)
    bounds = (getattr(map_point, "sever_cutter_bounds", None)
              or [None] * len(sever_cutters))
    for cutter, box in zip(sever_cutters, bounds):
        if box is not None:
            # Gate on the EXTENDED cutter's bounds (see _sever_cutters):
            # unlike a crease extension, a sever extension is a real
            # validity boundary and must reach rings beyond the window.
            bx0, by0, bx1, by1 = box
            if (bx1 < rx0 - margin or bx0 > rx1 + margin
                    or by1 < ry0 - margin or by0 > ry1 + margin):
                continue
        cut = []
        for piece in pieces:
            cut.extend(_cut_ring_by_polyline(piece, cutter))
        pieces = cut
    # The verdict runs even when no seam touched the bbox: a small ring
    # can sit ENTIRELY on severed ground with no seam crossing it.
    return [piece for piece in pieces
            if _ring_on_valid_ground(map_point, piece)]


def _split_ring_by_fold(map_point, ring, crossings_out=None, gate_bbox=None):
    """[(sub_ring, side, interior_point)] after cutting by every crease.

    `gate_bbox` overrides the cosmetic-cut skip gate's bounds. Rings that
    must end up with CONSISTENT partitions - a fill's outer and its holes -
    must gate against the SAME bounds (the fill's union bbox): gating each
    ring by its own bbox let an extension slice the outer while skipping
    the hole (whose bbox missed the cutter's real geometry), so the whole
    hole straddled two outer pieces, attached to only one, and the other
    rendered solid over what the artist cut out (measured: a crescent of
    phantom fill on add_fill_error).

    `crossings_out` collects the ring/crease crossing points (child space):
    the fill's side-change positions, which the crease anchors on exactly
    like it anchors on the strokes' cuts.

    SEVERING runs first and unconditionally (it is map semantics, not the
    fold-split display option): the ring is cut along the child frame's UV
    seams and every piece on severed ground - where the Third lift does not
    exist - is dropped (_sever_ring, boundary-vote verdict), mirroring what
    _sever_source does to the strokes.
    """
    if gate_bbox is None:
        rx0 = min(p[0] for p in ring)
        rx1 = max(p[0] for p in ring)
        ry0 = min(p[1] for p in ring)
        ry1 = max(p[1] for p in ring)
    else:
        rx0, ry0, rx1, ry1 = gate_bbox
    margin = 2.0 * POLY_STEP

    pieces = _sever_ring(map_point, ring,
                         gate_bbox=(rx0, ry0, rx1, ry1))

    if not _FOLD["split"]:
        return [(piece, _MappedOutput.FRONT, _ring_interior_point(piece))
                for piece in pieces]
    raw_crossings = [] if crossings_out is not None else None
    cutters = _child_cutters(map_point)
    raws = getattr(map_point, "child_cutters_raw", None) or [None] * len(cutters)
    for cutter, raw in zip(cutters, raws):
        if raw is not None:
            # Crossing a cutter's straight extension never changes the
            # parity, so a cutter whose REAL geometry stays bbox-clear of
            # the gate bounds can only slice cosmetically - skip it.
            if (max(p[0] for p in raw) < rx0 - margin
                    or min(p[0] for p in raw) > rx1 + margin
                    or max(p[1] for p in raw) < ry0 - margin
                    or min(p[1] for p in raw) > ry1 + margin):
                continue
        cut = []
        for piece in pieces:
            cut.extend(_cut_ring_by_polyline(piece, cutter, raw_crossings))
        pieces = cut
    if raw_crossings:
        # Crossings with a cutter's straight EXTENSION are cutting aid, not
        # fold geometry - only crossings on the real loci may anchor the
        # crease (see _child_cutters).
        real = getattr(map_point, "child_cutters_raw", None) or []
        for q in raw_crossings:
            if any(_polyline_arc_of(q, r)[0] <= 2.0 * POLY_STEP for r in real):
                crossings_out.append(q)
    out = []
    for piece in pieces:
        rep = _ring_interior_point(piece)
        out.append((piece, _fold_sign(map_point, rep), rep))
    return out


def _grayscale(color):
    """The lining shade of a fill: same luminance, no hue, same alpha."""
    gray = int(round(0.299 * color[0] + 0.587 * color[1] + 0.114 * color[2]))
    gray = min(255, max(0, gray))
    return (gray, gray, gray, color[3])


def _clip_rings_to_area(rings, polygons):
    """Cut rings along an area boundary and keep the inside pieces.

    The area polygons double as CLOSED cutters for the same ring splitter the
    creases use, then a parity test keeps the pieces inside. This gives fills
    the same exact boundary cut the stroke emitters get from _clip_polyline -
    an earlier whole-piece keep/drop test made a straddling fill either spill
    past the area or vanish from it entirely.
    """
    if not polygons:
        return list(rings)
    kept = []
    for ring in rings:
        pieces = [ring]
        for polygon in polygons:
            if len(polygon) < 3:
                continue
            boundary = list(polygon) + [polygon[0]]
            cut = []
            for piece in pieces:
                cut.extend(_cut_ring_by_polyline(piece, boundary))
            pieces = cut
        for piece in pieces:
            if _point_in_polygons(_ring_interior_point(piece), polygons):
                kept.append(piece)
    return kept


def _collect_pattern_fills(scene, frame):
    """Fill regions on `frame` that are pattern content (not tool artifacts).

    Collected BOTTOM LAYER FIRST (paintGL walks columns last-index-first, so
    index 0 is the top): emission preserves this order inside each depth
    layer, and Qt paints fill regions in list order, so overlapping fills
    keep the stacking the artist set up on the texture board.
    """
    fills = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return fills
    skip = (MAPPING_AREA_PROPERTY, *MAPPING_OUTPUT_PROPERTIES)
    for layer in reversed(structure["layers"]):
        if not layer["visible"]:
            continue
        cell = scene.cell_to_dict(layer["index"], frame, False)
        for fill in cell["image"].get("fills") or []:
            if (fill.get("property") or "") in skip:
                continue
            fills.append(fill)
    return fills


def _pattern_arc_ranges(map_point, pattern, fills):
    """The MAIN-arc extent of everything about to be mapped, or None."""
    h_low = h_high = v_low = v_high = None

    def feed(point):
        nonlocal h_low, h_high, v_low, v_high
        arc_h, arc_v = _arc_of_point(map_point, point)
        h_low = arc_h if h_low is None else min(h_low, arc_h)
        h_high = arc_h if h_high is None else max(h_high, arc_h)
        v_low = arc_v if v_low is None else min(v_low, arc_v)
        v_high = arc_v if v_high is None else max(v_high, arc_v)

    for stroke in pattern:
        for poly in _stroke_polylines(stroke):
            for point in poly:
                feed(point)
    for fill in fills:
        for ring in _path_commands_to_polygons(fill.get("commands")):
            for point in ring:
                feed(point)
    if h_low is None:
        return None
    return (h_low, h_high), (v_low, v_high)


def _emit_fills(animean, out, map_point, fills, child_area, main_area):
    """Map fill regions: cut by the creases, stack by depth, gray the backs.

    Each source ring is cut into constant-side pieces along the crease loci
    (in child space), every piece gets a stacking depth from the nearest-end
    anchor, and the pieces regroup per (depth, side) into one output path so
    holes keep working through the odd-even rule. Painter's algorithm does
    the occlusion: deeper layers are created first, nearer ones paint over
    them - no boolean subtraction anywhere.
    """
    added = 0
    for fill in fills:
        color = fill.get("color") or {}
        base = (int(color.get("r", 0)), int(color.get("g", 0)),
                int(color.get("b", 0)), int(color.get("a", 255)))
        source_rings = []
        for ring in _path_commands_to_polygons(fill.get("commands")):
            if len(ring) >= 2 and _dist(ring[0], ring[-1]) <= 1e-9:
                ring = ring[:-1]
            if len(ring) < 3:
                continue
            source_rings.append(_densify(ring + [ring[0]])[:-1])
        if not source_rings:
            continue
        # Which source rings are HOLES: odd-even nesting among the fill's own
        # subpaths, judged in child space. The nesting probe must sit ON the
        # ring's OWN boundary - an interior point (the centroid) of an
        # annulus's outer ring lies inside the ring's own hole, which scored
        # the outer as nested level 1 and dropped the WHOLE fill (a centred
        # donut/eye/letter-O emitted zero regions, silently). A boundary
        # vertex is never inside a subpath nested within its own ring; the
        # median over a few spread vertices shrugs off a vertex that grazes
        # another ring's edge.
        ring_levels = [_ring_nesting_level(source_rings, index)
                       for index in range(len(source_rings))]
        is_hole = [level % 2 == 1 for level in ring_levels]

        # (depth, side) -> [outer piece entries], each carrying its CHILD
        # geometry so hole pieces can find the outer piece that contains
        # them. Outer and hole rings split against the SAME gate bounds
        # (the fill's union bbox) so their partitions are consistent - a
        # hole that skipped a cutter the outer took straddled two outer
        # pieces and could attach to only one.
        fill_bbox = (min(p[0] for ring in source_rings for p in ring),
                     min(p[1] for ring in source_rings for p in ring),
                     max(p[0] for ring in source_rings for p in ring),
                     max(p[1] for ring in source_rings for p in ring))
        outers = {}
        holes = []
        fold_crossings = []
        for index, ring in enumerate(source_rings):
            for clipped in _clip_rings_to_area([ring], child_area):
                for piece, side, rep in _split_ring_by_fold(
                        map_point, clipped, fold_crossings,
                        gate_bbox=fill_bbox):
                    if is_hole[index]:
                        # A hole's depth is its outer's - never probed.
                        holes.append(piece)
                    else:
                        depth = _fold_depth(map_point, rep, side)
                        entry = {"child": piece, "rings": [piece],
                                 "holes": [], "level": ring_levels[index]}
                        outers.setdefault((depth, side), []).append(entry)
        # A fill's fold-edge endpoints anchor the crease exactly like stroke
        # cuts do: without them the crease stopped at the last STROKE cut and
        # left the fill's own fold boundary without its terminator line.
        out.cuts.extend(map_point(p) for p in fold_crossings)
        # Attach holes by CHILD-space containment alone: with consistent
        # partitions a hole piece lies inside at least one outer piece and
        # shares the INNERMOST one's depth and side - re-deriving them from
        # the hole's own probe made attachment hostage to probe noise
        # beside a crease, and an unattached hole silently rendered as
        # solid fill. Innermost (deepest nesting level) matters for
        # level-2+ subpaths: an island's hole is contained by the level-0
        # outer too, and first-match attached it there, painting the
        # island's hole solid.
        #
        # The attachment point must sit ON the hole's own boundary: an
        # interior point (the centroid) of a hole ring lies inside any
        # ISLAND nested within it, and the innermost rule then attached
        # the hole to that island (4-ring nest: the outer lost its hole
        # and painted the hole band solid). But ONE boundary point is not
        # enough either - a cut hole piece SHARES its chord with the outer
        # piece's boundary, and an edge midpoint on that chord made
        # containment a coin flip (the severed donut's crescent detached
        # and rendered solid). Spread candidates, first hit wins: some
        # stretch of the boundary is the hole's own outline.
        for piece in holes:
            best = None
            n = len(piece)
            for k in range(min(7, n)):
                a = piece[(k * n) // min(7, n)]
                b = piece[((k * n) // min(7, n) + 1) % n]
                rep = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
                for entries in outers.values():
                    for entry in entries:
                        if _point_in_ring(rep, entry["child"]):
                            if best is None or entry["level"] > best["level"]:
                                best = entry
                if best is not None:
                    break
            if best is not None:
                best["holes"].append(piece)

        # Every OUTER piece becomes its own fill region (with its holes as
        # extra odd-even subpaths). Same-group pieces are never merged into
        # one path: two pieces of one group can overlap in the image (the
        # warp is not injective), and odd-even would XOR the overlap into a
        # transparent hole.
        for (depth, side), entries in sorted(outers.items()):
            shade = base if side == _MappedOutput.FRONT else _grayscale(base)
            for entry in entries:
                mapped = []
                for child_ring in [entry["child"]] + entry["holes"]:
                    image_ring = [map_point(p) for p in child_ring]
                    mapped.extend(_clip_rings_to_area([image_ring], main_area))
                if out.add_fill(side, depth, mapped, shade):
                    added += 1
    return added


class _MappedOutput:
    """Buffers emitted strokes and fills per STACKING DEPTH, then flushes
    them into layers.

    Buffering (rather than writing straight into an image) keeps two things
    simple: layers are created only for depths that actually got content -
    so a run with no fold produces exactly one layer, as before - and they
    can be created in z-order regardless of generation order. Depth 0 (the
    sheet layer holding the nearest-end anchor) lands on top, each further
    depth below the previous, the crease between depth 0 and the rest.
    Within one layer Qt paints fills before strokes: this depth's lines over
    this depth's colors, and a NEARER depth's colors over both - the normal
    cel stacking. KNOWN LIMIT: a fill the artist deliberately stacked ABOVE
    line art on the texture board (an opaque correction patch dragged over
    strokes) cannot be represented inside one depth layer, because the image
    model paints all fills before all strokes; such a patch comes out under
    the lines again.
    """

    FRONT, BACK, SEAL = 1, -1, 0
    _PROPERTIES = {FRONT: MAPPED_PROPERTY, BACK: BACK_PROPERTY, SEAL: SEAL_PROPERTY}

    def __init__(self, animean, scene, row):
        self.animean = animean
        self.scene = scene
        self.row = row
        self.depths = {}         # depth -> [(kind, payload, color, width, side)]
        self.seal_depths = {}    # depth -> seal strokes, occluded like content
        self.side_counts = {self.FRONT: 0, self.BACK: 0}
        self.layers = []
        # Parallel to self.layers: {"role": "front"|"back"|"seal",
        # "depth": int} per created layer, so a mapping unit can record
        # exactly which member plays which part (visibility toggles,
        # primary-layer selection) without name sniffing.
        self.layer_roles = []
        # Where the emitters actually cut the artwork, in mapped space. The
        # crease anchors onto these, so they have to come from the geometry
        # that gets DRAWN: bezier mode splits the smoothed cubics, while
        # _stroke_polylines falls back to the artist's raw input trail when a
        # stroke carries commands instead of polylines - a different curve.
        self.cuts = []
        # Child-fold SEAM cut points (child space): where the topology was
        # severed because the Newton lift into Third does not exist. These
        # are UV seams, not main-frame creases - they never feed the seal
        # pass (the pattern simply ENDS there, wrapped out of sight); they
        # are counted for the run summary.
        self.seams = []
        # Bezier Bridges (补全拓扑): the Third-space cubics that spanned
        # severed gaps this run, counted for the summary.
        self.bridges = []

    @staticmethod
    def _layer_name(depth, generic=False):
        if generic:
            # The depth scale was renormalized by an ODD shift, so depth 0 no
            # longer means "front face": parity-neutral names keep the panel
            # from calling back-face content "mapped layer".
            return f"{MAPPED_LAYER_NAME} depth {depth}"
        if depth <= 0:
            return MAPPED_LAYER_NAME
        if depth == 1:
            return BACK_LAYER_NAME
        return f"{MAPPED_LAYER_NAME} depth {depth}"

    def _bucket(self, side, depth):
        if side == self.SEAL:
            return self.seal_depths.setdefault(max(0, int(depth or 0)), [])
        if depth is None:
            depth = 0 if side == self.FRONT else 1
        self.side_counts[side] = self.side_counts.get(side, 0) + 1
        return self.depths.setdefault(max(0, int(depth)), [])

    def add_polyline(self, side, points, color, width, depth=None):
        if len(points) < 2:
            return False
        self._bucket(side, depth).append(("polyline", points, color, width, side))
        return True

    def add_curved(self, side, commands, flat, color, width, depth=None):
        if len(commands) < 2 or len(flat) < 2:
            return False
        self._bucket(side, depth).append(("curved", (commands, flat), color, width, side))
        return True

    def add_fill(self, side, depth, rings, color):
        rings = [ring for ring in rings if len(ring) >= 3]
        if not rings:
            return False
        self._bucket(side, depth).append(("fill", rings, color, 0.0, side))
        return True

    def count(self, side=None):
        seal_total = sum(len(items) for items in self.seal_depths.values())
        if side is None:
            return (sum(len(items) for items in self.depths.values())
                    + seal_total)
        if side == self.SEAL:
            return seal_total
        return self.side_counts.get(side, 0)

    def _track_new_layer(self, layer):
        """Record a freshly created layer; everything older shifted down one.

        _create_mapped_layer moves what it makes to index 0, so every index
        this object already holds is now one lower in the stack.
        """
        self.layers = [index + 1 for index in self.layers]
        self.layers.append(layer)
        return layer

    def group_output(self):
        """Pack this run's layers into one group (legacy, unit-less runs).

        Unit runs never call this - their layers are adopted into the unit's
        own tagged group in place (_install_unit_output).
        """
        try:
            if not self.layers:
                return 0
            return self.scene.create_layer_group(
                MAPPING_GROUP_NAME, list(self.layers), [], False)
        except AttributeError:
            return 0  # older build without the grouping bindings

    def _write_items(self, image, items, seal=False):
        written = 0
        for kind, payload, color, width, side in items:
            if kind == "fill":
                commands = []
                for ring in payload:
                    commands.append({"type": "move", "to": {"x": ring[0][0], "y": ring[0][1]}})
                    for point in ring[1:]:
                        commands.append({"type": "line", "to": {"x": point[0], "y": point[1]}})
                    commands.append({"type": "line", "to": {"x": ring[0][0], "y": ring[0][1]}})
                # based_on_all_layers=True: removeInvalidFillRegions deletes
                # any region with no valid SOURCE layer, and a mapped fill is
                # baked geometry with no source - without the flag the app
                # silently erased every mapped fill on the next layer edit.
                image.add_fill_region(commands, color, self._PROPERTIES[side],
                                      None, -1, True)
                written += 1
                continue
            if kind == "polyline":
                obj = self.animean.vectorlogic.make_stroke_object(
                    payload, color, width, image.stroke_count() + 1, False, False)
            else:
                commands, flat = payload
                obj = self.animean.vectorlogic.make_stroke_object_from_path(
                    commands, flat, color, width, image.stroke_count() + 1)
            obj.property = self._PROPERTIES[side]
            if seal:
                obj.pen_style = _display_style("seal_style")
            image.add_stroke_object(obj)
            written += 1
        return written

    def flush(self):
        """Create the needed layers bottom-up and write the buffers out."""
        added = 0
        # Normalize so the nearest occupied depth is 0: an anchor parked in a
        # region with no artwork would otherwise leave the top layer empty
        # and hand the front content a "back" layer name.
        occupied = [d for d in self.depths if self.depths[d]]
        shift = min(occupied) if occupied and min(occupied) > 0 else 0
        if shift:
            self.depths = {d - shift: items for d, items in self.depths.items() if items}
            self.seal_depths = {max(0, d - shift): items
                                for d, items in self.seal_depths.items() if items}
        generic = bool(shift % 2)
        # Each depth's crease layer sits DIRECTLY ABOVE its content layer:
        # above it, because a depth-0 fill is opaque right up to the fold
        # edge (its boundary IS the crease) and painted over the seal it
        # swallowed the crease's inner half; but BELOW every nearer depth,
        # because a crease under a covering flap is occluded artwork like
        # anything else - drawn blanket-on-top it poked out of the covering
        # colour (user report). Layers are created bottom-up, so within one
        # depth the order is content first, crease second.
        deep_first = sorted(set(d for d in self.depths if self.depths[d])
                            | set(d for d in self.seal_depths if self.seal_depths[d]),
                            reverse=True)
        plan = []
        for depth in deep_first:
            if self.depths.get(depth):
                plan.append((self._layer_name(depth, generic),
                             self.depths[depth], False, depth))
            if self.seal_depths.get(depth):
                name = (SEAL_LAYER_NAME if depth == 0 and not generic
                        else f"{SEAL_LAYER_NAME} depth {depth}")
                plan.append((name, self.seal_depths[depth], True, depth))
        for name, items, seal, depth in plan:
            layer = _create_mapped_layer(self.scene, self.row, name)
            if layer < 0:
                print(f"[auto_mapping] could not create the '{name}'.")
                continue
            self._track_new_layer(layer)
            self.layer_roles.append({
                "role": ("seal" if seal
                          else ("front" if depth == 0 else "back")),
                "depth": depth,
            })
            image = self.scene.image_at(self.row, layer, True)
            if image is None:
                print(f"[auto_mapping] '{name}' has no editable cell.")
                continue
            added += self._write_items(image, items, seal)
        return added

    def rollback(self):
        for layer in sorted(self.layers, reverse=True):
            _discard_mapped_layer(self.scene, layer)
        self.layers = []
        self.layer_roles = []


def _fold_runs(map_point, piece, cuts=None):
    """Source runs of constant orientation, or the whole piece when the
    front/back split is off. Records each cut for the crease to anchor on."""
    if not _FOLD["split"]:
        return [(piece, _MappedOutput.FRONT)]
    runs = _split_by_fold(map_point, piece)
    if cuts is not None:
        for before, _after in zip(runs, runs[1:]):
            cuts.append(map_point(before[0][-1]))
    return runs


def _side_style(side, color_tuple):
    return color_tuple if side == _MappedOutput.FRONT else _display_color("back_color")


def _emit_polyline_mode(animean, out, stroke, map_point, child_area, main_area, color_tuple, width):
    """Same anchored sampling as spline mode, but the output stays a polyline.

    Originals are anchors, samples are inserted between them, everything is
    mapped, the inserted samples are decimated span-wise - and the surviving
    points are joined with straight segments instead of a fitted curve.
    (User 2026-07-30: polyline mode must sample too; it is not a legacy mode.)
    """
    added = 0
    eps = rdp_eps()
    for poly in _stroke_polylines(stroke):
        for piece in _clip_polyline(poly, child_area):
            islands = _sever_source(map_point, piece, out.seams)
            for island in islands:
                for run, side in _fold_runs(map_point, island, out.cuts):
                    depth = _run_depth(map_point, run, side)
                    flagged = _adaptive_map_polyline(map_point, run)
                    for clipped in _clip_flagged(flagged, main_area):
                        points = _decimate_between_anchors(clipped, eps)
                        if out.add_polyline(side, points,
                                            _side_style(side, color_tuple),
                                            width, depth):
                            added += 1
            if _BRIDGE["enabled"] and len(islands) > 1:
                added += _emit_bridges(out, map_point,
                                       list(zip(islands, islands[1:])),
                                       main_area, color_tuple, width,
                                       curved=False, eps=eps)
    return added


def _emit_spline_mode(animean, out, stroke, map_point, child_area, main_area, color_tuple, width):
    """The user's route 1: originals stay anchors, only inserted samples decimate.

    Densify between the original vertices -> map everything -> RDP only the
    inserted samples of each span -> centripetal Catmull-Rom through the knots.
    """
    added = 0
    eps = rdp_eps()
    for poly in _stroke_polylines(stroke):
        for piece in _clip_polyline(poly, child_area):
            islands = _sever_source(map_point, piece, out.seams)
            for island in islands:
                for run, side in _fold_runs(map_point, island, out.cuts):
                    depth = _run_depth(map_point, run, side)
                    flagged = _adaptive_map_polyline(map_point, run)
                    for clipped in _clip_flagged(flagged, main_area):
                        knots = _decimate_between_anchors(clipped, eps)
                        commands, flat = _cubics_to_commands(
                            _catmull_rom_cubics(knots))
                        if out.add_curved(side, commands, flat,
                                          _side_style(side, color_tuple),
                                          width, depth):
                            added += 1
            if _BRIDGE["enabled"] and len(islands) > 1:
                added += _emit_bridges(out, map_point,
                                       list(zip(islands, islands[1:])),
                                       main_area, color_tuple, width,
                                       curved=True, eps=eps)
    return added


def _emit_bezier_mode(animean, out, stroke, map_point, child_area, main_area, color_tuple, width):
    """Keep the artist's Bezier segments; transport each handle through the warp."""
    added = 0
    for cubics in _commands_to_subpaths(stroke.get("commands")):
        for src_piece in _clip_cubics(cubics, child_area):
            islands = _sever_cubics_by_child_fold(map_point, src_piece,
                                                  out.seams)
            for island in islands:
                for run, side in _fold_runs_cubic(map_point, island, out.cuts):
                    depth = (_run_depth(map_point,
                                        [_cubic_point(run[len(run) // 2], 0.5)],
                                        side)
                             if run else (0 if side == _MappedOutput.FRONT else 1))
                    out_cubics = []
                    for cub in run:
                        out_cubics.extend(_warp_cubic(map_point, cub))
                    for out_piece in _clip_cubics(out_cubics, main_area):
                        commands, flat = _cubics_to_commands(out_piece)
                        if out.add_curved(side, commands, flat,
                                          _side_style(side, color_tuple),
                                          width, depth):
                            added += 1
            if _BRIDGE["enabled"] and len(islands) > 1:
                # The trend probes come from the facing islands' own
                # geometry: a short polyline just inside each cut, spanning
                # enough cubics that a sub-pixel sliver cannot set the trend.
                pairs = [(_cubic_tail_polyline(ia),
                          _cubic_head_polyline(ib))
                         for ia, ib in zip(islands, islands[1:])]
                added += _emit_bridges(out, map_point, pairs, main_area,
                                       color_tuple, width, curved=True,
                                       eps=rdp_eps())
    return added


def _classified_cubic_parts(map_point, cub, classify, snap_true=False):
    """Split ONE source cubic where `classify` changes inside it:
    [(sub_cubic, verdict)].

    The cubic twin of _classified_runs, shared by _split_cubic_by_fold and
    _sever_cubics_by_child_fold. There is no analytic knot list for a
    cubic, so the crossings are located by scanning the source parameter
    and bisecting - legitimate because the classification is piecewise
    constant, so a change between two probes brackets exactly one cell
    boundary. snap_true places each cut at the bracket end on the True
    side (see _classified_runs), and the parts then carry the SCAN
    REGION's verdict rather than a midpoint re-probe: when the boundary
    sits a fraction of a pixel inside t=0 or t=1, the outermost part's
    midpoint lands on the far side of the cut and the sliver was judged
    True while its outer endpoint - the island's terminal point, the
    bridge's anchor - had no lift (fuzzed: 4 of 385 severed cubics leaked
    an invalid endpoint through the midpoint probe).
    """
    net = bezier.hull_length(cub)
    probes = max(4, min(96, int(math.ceil(net / POLY_STEP))))
    ts = [k / probes for k in range(probes + 1)]
    marks = [classify(_cubic_point(cub, t)) for t in ts]

    cuts = []
    verdicts = [marks[0]]
    for k in range(1, len(ts)):
        if marks[k] == marks[k - 1]:
            continue
        lo, hi = ts[k - 1], ts[k]
        for _ in range(24):
            mid = (lo + hi) * 0.5
            if classify(_cubic_point(cub, mid)) == marks[k - 1]:
                lo = mid
            else:
                hi = mid
        if snap_true:
            cuts.append(lo if marks[k - 1] else hi)
        else:
            cuts.append((lo + hi) * 0.5)
        verdicts.append(marks[k])

    bounds = [0.0] + cuts + [1.0]
    parts = []
    for region, (t0, t1) in enumerate(zip(bounds, bounds[1:])):
        if t1 - t0 <= 1e-9:
            continue
        verdict = (verdicts[region] if snap_true
                   else classify(_cubic_point(cub, (t0 + t1) * 0.5)))
        parts.append((_split_cubic(cub, t0, t1), verdict))
    return parts


def _split_cubic_by_fold(map_point, cub):
    """Split ONE source cubic where the map's orientation flips inside it
    (see _classified_cubic_parts for the scan-and-bisect mechanics)."""
    return _classified_cubic_parts(
        map_point, cub, lambda p: _fold_sign(map_point, p))


def _sever_cubics_by_child_fold(map_point, cubics, seams=None):
    """Cut a run of source cubics into computable UV islands (bezier mode).

    The cubic twin of _sever_source: scan each cubic's parameter for
    validity changes of the Third lift (_classified_cubic_parts), bisect
    each change onto the fold line - cuts land on the VALID side, so an
    island's end cubic still has a lift - keep the valid parts, drop the
    rest, and regroup contiguous valid parts: a severed stretch splits the
    subpath into separate islands. Free on frames that cannot fold
    (can_fold gate).
    """
    if not cubics or not map_point.can_fold():
        return [list(cubics)] if cubics else []

    third_of = map_point.third_of
    parts = []
    for cub in cubics:
        parts.extend(_classified_cubic_parts(
            map_point, cub,
            lambda p: third_of(p)[2], snap_true=True))

    islands = []
    current = []
    previous_ok = None
    for part, ok in parts:
        if ok:
            if not current and previous_ok is False and seams is not None:
                seams.append(part[0])  # island opens ON the fold line
            current.append(part)
        elif current:
            if seams is not None:
                seams.append(current[-1][3])  # island closes ON the fold line
            islands.append(current)
            current = []
        previous_ok = ok
    if current:
        islands.append(current)
    return islands


def _emit_seals(animean, out, map_point, pattern, child_area, main_area, width_scale):
    """Draw the crease where the surface folds back on itself.

    The map is CONTINUOUS across a fold - only its derivative flips - so the
    front and back runs already meet exactly and there is no gap to fill.
    The crease matters for a different reason: once the back layer is hidden,
    the remaining front edge needs a terminator.

    Geometry: an H-axis fold happens at a fixed coordinate l_h = c, so its
    locus in source space is a straight line, and its image is
    H(c) + V(t) - O over the content's t range - a TRANSLATED COPY OF THE
    MAIN V GUIDE. (Symmetrically for a V-axis fold.) That is exactly the
    "use the V axis as the capping edge" idea, and it needs no new geometry.
    """
    main = map_point.main_frame
    h_low = h_high = v_low = v_high = None
    for stroke in pattern:
        for poly in _stroke_polylines(stroke):
            for piece in _clip_polyline(poly, child_area):
                for point in piece:
                    # Through _arc_of_point, NOT inline scaling: with an
                    # additional-line warp the artwork's true main-arc extent
                    # differs from the unwarped one (measured 52 arc px), and
                    # the crease windows below must agree with the depth
                    # pass's space or anchored loci get windowed out.
                    arc_h, arc_v = _arc_of_point(map_point, point)
                    h_low = arc_h if h_low is None else min(h_low, arc_h)
                    h_high = arc_h if h_high is None else max(h_high, arc_h)
                    v_low = arc_v if v_low is None else min(v_low, arc_v)
                    v_high = arc_v if v_high is None else max(v_high, arc_v)
    if v_low is None:
        return

    # The crease used to be clipped to the artwork's H extent instead. That is
    # both too weak - a bounding box says nothing about whether the sheet folds
    # at that height - and unsound: dropping the out-of-range samples from the
    # middle of a branch silently JOINED the two surviving halves with a
    # straight line across the gap. Anchoring subsumes it, since a cut can
    # never sit outside the artwork.
    #
    # The cuts come from the emitters, not from a second splitting pass here:
    # bezier mode splits the SMOOTHED cubics while _stroke_polylines falls back
    # to the artist's raw trail, so recomputing them would anchor the crease
    # onto a curve the output never contains.
    cuts = out.cuts
    # The tolerance is the crease polyline's own step, not the accuracy of a
    # cut. A cut is exact - bisected onto det J = 0 - and on a smooth stretch
    # it measures 0.01 px from its branch. But where a fold PAIR IS BORN the
    # two branches leave the birth point like +/- sqrt(l_v - l_v0), so the
    # locus turns a square-root corner that halving in V only resolves as
    # sqrt(2) per pass: measured 2.27 px off a branch whose steps are bounded
    # at 4 px, and at 2.0 px tolerance that cut was rejected and its branch
    # lost an anchor. Distinguishing branches does not need a tight number -
    # the nearest other branch in that test was 750 px away - and the two
    # branches that ARE close near a birth are close because they coincide
    # there, so attributing a cut to either is the same crease.
    tolerance = 2.0 * POLY_STEP

    color = _display_color("seal_color")
    # Thinner than the artwork: the crease is an annotation of the fold, not
    # part of the pattern, and at 2x width_scale it out-weighted every stroke
    # it was meant to terminate. The factor is a display setting now.
    width = max(0.5, float(_LINE_DISPLAY.get("seal_width", 0.8)) * width_scale)
    # Pad the sweep windows: a locus stretch whose anchoring cut sits right
    # at the pattern's arc extent needs the rows just beyond it to give the
    # branch tip room to reach the cut.
    pad = 4.0 * POLY_STEP
    # A traced curve can hold stretches OUTSIDE the artwork's arc extent: the
    # padded window traces them (branch tips need the room) and stitching can
    # chain a material stretch to an immaterial one. Nothing folds where no
    # material reaches - yet anchoring alone no longer excludes such
    # stretches: in a heavy-compression zone the image of a cut from a
    # NEIGHBOURING locus lands within the trim tolerance of the immaterial
    # stretch's image and anchors it (measured: a 140 px dashed crease across
    # visually flat artwork, on a corner line 14 px beyond the pattern's arc
    # extent - the cuts anchoring it belonged to a locus 14+ arc px away).
    # So clip in ARC space, where compression cannot confuse neighbours,
    # keeping each inside run as its own branch - splitting, never bridging,
    # so the old clip-and-join hazard (a straight seam across the dropped
    # middle) cannot return. The margin is the anchor tolerance: a cut ON
    # the extent boundary keeps a reachable tip.
    in_h = (h_low - tolerance, h_high + tolerance)
    in_v = (v_low - tolerance, v_high + tolerance)

    def _window_runs(curve):
        run = []
        run_indices = []
        for index, (arc, other) in enumerate(curve):
            if in_h[0] <= arc <= in_h[1] and in_v[0] <= other <= in_v[1]:
                run.append((arc, other))
                run_indices.append(index)
            else:
                if len(run) >= 2:
                    yield run, run_indices
                run = []
                run_indices = []
        if len(run) >= 2:
            yield run, run_indices

    whole_curves = _crease_curves(map_point, (v_low - pad, v_high + pad),
                                  (h_low - pad, h_high + pad),
                                  corner_spans=((h_low, h_high), (v_low, v_high)))
    # Read AFTER the call: _with_warp_loci publishes by REBINDING
    # map_point.warp_curve_child to a new dict, so a pre-call snapshot
    # never holds the ids of the curves this call just built - the whole
    # exact-geometry bypass below was dead code with the snapshot first.
    warp_child = getattr(map_point, "warp_curve_child", {}) or {}
    for whole in whole_curves:
      # A warp fold locus carries its exact child geometry and is verified
      # by construction - the _curve_is_fold probes (and the depth probes
      # below) go through the fixed-point unwarp, which is unreliable
      # exactly where the warp folds.
      whole_child = warp_child.get(id(whole))
      for curve, curve_indices in _window_runs(whole):
        if whole_child is None and not _curve_is_fold(map_point, curve):
            continue
        points = [main.hv(arc, other) for arc, other in curve]
        if len(points) < 2:
            continue
        # Keep the pre-trim image polyline and its 1:1 arc correspondence:
        # the crease's DEPTH is read back through them after trimming and
        # cusp-splitting have reshaped the drawn geometry.
        branch_arcs = list(curve)
        branch_points = list(points)
        branch_child = ([whole_child[i] for i in curve_indices]
                        if whole_child is not None else None)
        branch_depths = {}

        def seal_depth_at(image_point):
            """Depth of the crease at this drawn vertex: the nearer of the
            two flaps meeting at the fold (probed one step to each side in
            child space). Deeper flaps' creases are occluded by everything
            nearer, exactly like the flaps themselves."""
            best = None
            for index, q in enumerate(branch_points):
                d2 = ((q[0] - image_point[0]) ** 2
                      + (q[1] - image_point[1]) ** 2)
                if best is None or d2 < best[0]:
                    best = (d2, index)
            index = best[1]
            if index in branch_depths:
                return branch_depths[index]
            other = index + 1 if index + 1 < len(branch_arcs) else index - 1
            if branch_child is not None:
                pa = branch_child[index]
                pb = branch_child[other]
            else:
                pa = _child_of_arcs(map_point, branch_arcs[index])
                pb = _child_of_arcs(map_point, branch_arcs[other])
            dx, dy = pb[0] - pa[0], pb[1] - pa[1]
            length = math.hypot(dx, dy)
            depth = 0
            if length > 1e-9:
                nx, ny = -dy / length, dx / length
                sides = []
                for direction in (1.0, -1.0):
                    p = (pa[0] + nx * POLY_STEP * direction,
                         pa[1] + ny * POLY_STEP * direction)
                    sides.append(_fold_depth(map_point, p,
                                             _fold_sign(map_point, p)))
                depth = max(0, min(sides))
            branch_depths[index] = depth
            return depth
        # Anchor the WHOLE branch before cutting it up. A cusp is a feature of
        # one continuous fold, not a break in it, so a cut past the cusp still
        # vouches for the stretch leading up to it; anchoring the two arms
        # separately orphaned a cut 145 px from anything drawn.
        points = _trim_to_anchors(points, cuts, tolerance)
        if len(points) < 2:
            continue
        # No minimum span here: a non-empty trim is BY CONSTRUCTION vouched
        # for by two cuts, and a short anchored sliver is real geometry - the
        # map compresses whole source bands into a few px where det J nears
        # zero, so dozens of cuts can legitimately pile onto a 3 px stretch
        # (measured: 36 cuts on a 2.6 px trim; the old 8 px minimum deleted
        # it and orphaned every one of them). Unanchored stubs never reach
        # this point - the trim already returned [] for them.
        # The crease is an annotation, so decimate it like any other output -
        # but BEFORE splitting at cusps, which also dissolves the sub-pixel
        # reversals that would each otherwise become their own quarter-pixel
        # "stroke". Cusps are drawn as separate strokes because one stroke
        # through a cusp ties a visible knot, and every arm that survives is
        # inside the anchored span, so none may be dropped: consecutive arms
        # share their cusp vertex, so dropping a short one does not shorten the
        # crease, it punches a hole in the middle of it, and dropping an END
        # arm pulls the crease off the stroke it exists to terminate (7 px).
        points = _rdp(points, max(rdp_eps(), 0.5))
        # Cusps are drawn as separate strokes because one stroke through a cusp
        # ties a visible knot. A cusp can leave a stub under a pixel long,
        # which is not worth a stroke object of its own - but it may not be
        # DROPPED either: every arm is inside the anchored span, consecutive
        # arms share their cusp vertex, so dropping a short one punches a hole
        # in the middle of one continuous crease rather than shortening it,
        # and dropping an END arm pulls the crease off the very stroke it
        # exists to terminate (measured: 7 px short of it). So a stub is
        # folded back into its neighbour, where a sub-pixel reversal is
        # invisible, instead of being emitted or discarded.
        arms = []
        for arm in _split_at_cusps(points):
            if arms and _cumulative_lengths(arm)[-1] < 2.0 * POLY_STEP:
                arms[-1].extend(arm[1:])
            else:
                arms.append(list(arm))
        if len(arms) > 1 and _cumulative_lengths(arms[0])[-1] < 2.0 * POLY_STEP:
            arms[1][:0] = arms[0][:-1]
            del arms[0]
        for run in arms:
            for piece in _clip_polyline(run, main_area):
                if len(piece) < 2:
                    continue
                # Split where the crease's depth changes (it slides under a
                # nearer flap at locus crossings); consecutive sub-pieces
                # share the transition vertex so the crease stays gapless.
                depth_of = [seal_depth_at(p) for p in piece]
                start = 0
                for index in range(1, len(piece)):
                    if depth_of[index] != depth_of[start]:
                        out.add_polyline(_MappedOutput.SEAL,
                                         piece[start:index + 1], color, width,
                                         depth_of[start])
                        start = index
                out.add_polyline(_MappedOutput.SEAL, piece[start:], color,
                                 width, depth_of[start])


def _fold_runs_cubic(map_point, cubics, cuts=None):
    """Group source cubics into runs of constant orientation."""
    if not _FOLD["split"]:
        return [(list(cubics), _MappedOutput.FRONT)]
    runs = []
    for cub in cubics:
        for part, side in _split_cubic_by_fold(map_point, cub):
            if runs and runs[-1][1] == side:
                runs[-1][0].append(part)
            else:
                runs.append(([part], side))
    if cuts is not None:
        for before, _after in zip(runs, runs[1:]):
            cuts.append(map_point(before[0][-1][3]))
    return runs


_EMITTERS = {
    "polyline": _emit_polyline_mode,
    "spline": _emit_spline_mode,
    "bezier": _emit_bezier_mode,
}


def _perform_mapping():
    animean = _animean()
    child = _scene_model("child")
    main = _scene_model("main")
    mode = curve_mode()
    if mode not in _EMITTERS:
        mode = DEFAULT_CURVE_MODE

    child_frame = max(child.current_frame(), 0)
    main_frame = max(main.current_frame(), 0)

    _absorb_legacy_items("child", child, child_frame)
    _absorb_legacy_items("main", main, main_frame)

    child_assets = _assets_for("child")
    main_assets = _assets_for("main")

    ok = True
    for view_label, assets in (("child_paint_view", child_assets), ("main_paint_view", main_assets)):
        missing = [ITEM_LABELS[prop] for prop in GUIDE_PROPERTIES if prop not in assets]
        if missing:
            print(f"[auto_mapping] {view_label} is missing: {', '.join(missing)}")
            ok = False
    if not ok:
        print("[auto_mapping] draw the guides with the 'H Center Line' / 'V Center Line' tools first.")
        return False

    child_pattern = _collect_pattern_strokes(child, child_frame, want_commands=mode == "bezier")
    child_fills_probe = _collect_pattern_fills(child, child_frame)
    if not child_pattern and not child_fills_probe:
        print("[auto_mapping] child_paint_view has no pattern strokes or fills to map.")
        return False

    crossings_ok = True
    for view_label, assets in (("child_paint_view", child_assets), ("main_paint_view", main_assets)):
        if not _polylines_cross(assets[H_PROPERTY]["points"], assets[V_PROPERTY]["points"]):
            print(f"[auto_mapping] the H and V center lines in {view_label} do NOT cross — "
                  "extend them so they intersect, then run Auto Mapping again.")
            crossings_ok = False
    if not crossings_ok:
        # A guessed origin produces unbounded garbage under per-side scaling;
        # refusing beats mapping nonsense.
        return False

    mapper_info = {}
    additional = _additional_pairs()
    map_point, width_scale = build_mapper(
        child_assets[H_PROPERTY],
        child_assets[V_PROPERTY],
        main_assets[H_PROPERTY],
        main_assets[V_PROPERTY],
        mapper_info,
        additional_pairs=additional,
    )
    if map_point is None:
        print(f"[auto_mapping] cannot build mapping: {width_scale}")
        return False
    for note in getattr(map_point, "additional_notes", ()):
        print(f"[auto_mapping] warning: {note}")
    if getattr(map_point, "warp", None) is not None:
        print(f"[auto_mapping] {len(map_point.warp.pairs)} additional line "
              f"pair(s) shaping the mapping ({_ADDITIONAL['falloff']} falloff).")
    worst_mismatch = max(mapper_info.get("h_scale_mismatch", 1.0),
                         mapper_info.get("v_scale_mismatch", 1.0))
    if worst_mismatch > 1.5:
        print(f"[auto_mapping] tip: the crossings sit at different relative positions "
              f"(side scale mismatch x{worst_mismatch:.1f}); strokes crossing a center "
              "line will fold there. Place both crossings at similar positions along "
              "their lines to avoid it.")
    if mapper_info.get("mirrored"):
        print("[auto_mapping] note: the child and main guide frames have OPPOSITE "
              "handedness, so the result is a MIRROR image. If you wanted it "
              "unmirrored, reverse ONE main center line (redraw it in the other "
              "direction - watch the arrows).")

    child_area = (child_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")
    main_area = (main_assets.get(MAPPING_AREA_PROPERTY) or {}).get("polygons")

    child_fills = child_fills_probe
    if _FOLD["split"]:
        ranges = _pattern_arc_ranges(map_point, child_pattern, child_fills)
        if ranges is not None:
            # Depth context: crease loci + the nearest-end anchor, so every
            # run and fill piece can be stacked by how many folds separate it
            # from the red handle.
            _prepare_fold_context(map_point, ranges[0], ranges[1])

    out = _MappedOutput(animean, main, main_frame)
    emit = _EMITTERS[mode]
    generated = 0
    clipped_out = 0
    mapped_fills = 0
    mapping_group = 0
    layer_names = ""
    uid = _ACTIVE_UNIT["id"]
    unit_meta = _UNIT_META.get(uid) if uid else None
    try:
        for stroke in child_pattern:
            color_tuple, width = _stroke_style(stroke, width_scale)
            before = generated
            generated += emit(animean, out, stroke, map_point, child_area, main_area,
                              color_tuple, width)
            if generated == before:
                clipped_out += 1
        if child_fills:
            mapped_fills = _emit_fills(animean, out, map_point, child_fills,
                                       child_area, main_area)
        if _FOLD["split"] and _FOLD["seal"] and out.count(_MappedOutput.BACK):
            _emit_seals(animean, out, map_point, child_pattern, child_area,
                        main_area, width_scale)
        added = out.flush()
        if added:
            # Names read now: installing into a unit deletes the previous
            # run's members, which shifts every index recorded in out.layers.
            layer_names = ", ".join(f"'{main.layer_name(index)}'"
                                    for index in sorted(out.layers))
            if unit_meta is not None:
                # UNIT RUN: adopt the new layers into the unit's group and
                # retire the previous output in place - focus stays inside
                # the unit, no stale copies pile up, one panel row per unit.
                mapping_group = int(uid)
                _install_unit_output(main, out, uid, unit_meta)
            else:
                # Legacy run: every click still gets its own fresh group.
                # The axis-snapshot provenance layers (Re-expand) are gone -
                # a mapping's guides live in its unit config now, and
                # duplicating the unit replaces re-expanding a snapshot.
                mapping_group = out.group_output()
    except Exception:
        # A half-filled layer without its own history commit would silently
        # ride along in the NEXT unrelated commit; roll it back instead.
        out.rollback()
        animean.ui.refresh()
        raise

    if added == 0:
        out.rollback()
        animean.ui.refresh()
        reason = "fell outside the mapping area(s)"
        if out.seams:
            reason += " or lay entirely on severed ground (child-frame folds)"
        print(f"[auto_mapping] nothing mapped: all {clipped_out} stroke(s) "
              f"{reason}; the empty layer was discarded.")
        return False

    animean.ui.refresh()
    try:
        animean.ui.history_commit("Auto Mapping", "main")
    except Exception:
        pass  # older builds without the history binding
    summary = (f"[auto_mapping] Auto Mapping (coons interpolation, {mode} mode) mapped "
               f"{added} stroke(s) into layer(s) {layer_names} "
               f"(frame {main_frame + 1} of main_paint_view, width x{width_scale:.2f})")
    if unit_meta is not None:
        summary += f"; refreshed the automapping layer (group {uid}) in place"
    elif mapping_group:
        summary += f"; packed into layer group '{MAPPING_GROUP_NAME}'"
    if mapped_fills:
        deepest = max((d for d in out.depths if out.depths[d]), default=0)
        summary += f"; {mapped_fills} fill group(s) mapped"
        if deepest >= 1:
            summary += (f", stacked over {deepest + 1} depth layer(s) from the "
                        "nearest point (red handle)")
    back_count = out.count(_MappedOutput.BACK)
    if back_count:
        summary += (f"; {back_count} stroke/fill item(s) landed on the BACK of "
                    f"a fold (det J < 0)")
    if out.seams:
        summary += (f"; topology SEVERED at {len(out.seams)} UV-seam cut(s) - "
                    "the child frame folds there (det J <= 0 / diverging "
                    "lift), so the pattern wraps out of view at the seam")
    if out.bridges:
        summary += (f"; {len(out.bridges)} Bezier Bridge(s) spanned the "
                    f"severed gaps in Third space (补全拓扑, "
                    f"k = {_BRIDGE['tension']:.2f} x |AB|)")
    elif _BRIDGE["enabled"] and out.seams:
        summary += ("; topology bridging is ON but no gap had two islands "
                    "to join (a bridge needs the stroke to re-emerge from "
                    "the fold)")
    if mapper_info.get("mirrored"):
        summary += ", MIRRORED (opposite frame handedness)"
    if child_area:
        summary += ", source limited by child mapping area"
    if main_area:
        summary += ", output clipped by main mapping area"
    if clipped_out:
        summary += f", {clipped_out} stroke(s) fell fully outside"
    print(summary)
    return True


def _run():
    _RUN_GUARD["depth"] += 1
    try:
        _perform_mapping()
    except Exception as error:  # keep the UI alive; feedback goes to the debug dock
        print(f"[auto_mapping] error: {error!r}")
    finally:
        _RUN_GUARD["depth"] -= 1


# ---------------------------------------------------------------------------
# hooks + tool handlers
# ---------------------------------------------------------------------------

def _capture_mapping_item(cell, stroke, message):
    """Move a freshly drawn guide/area click out of the model into the dict."""
    prop = message.get("property")
    if prop not in (H_PROPERTY, V_PROPERTY, MAPPING_AREA_PROPERTY,
                    ADDITIONAL_PROPERTY):
        return
    view = message.get("view") or "main"
    row = cell.get("row")
    layer = cell.get("layer")
    index = stroke.get("index")
    if row is None or layer is None or index is None or row < 0 or layer < 0 or index < 0:
        return
    try:
        scene = _scene_model(view)
    except Exception as error:
        print(f"[auto_mapping] capture skipped: {error}")
        return

    strokes = scene.cell_to_dict(layer, row, True, POLY_STEP)["image"]["strokes"]
    if index >= len(strokes):
        return
    points = _stroke_points(strokes[index])
    width = float(strokes[index].get("width", 3.0))
    # The stroke's REAL path is a mixed line/Bezier chain (the smoother's quad
    # spline); the polyline above is only its 4 px flattening. Keep the
    # commands so the frame can evaluate on the true curve (_Curve).
    commands = None
    try:
        raw_strokes = scene.cell_to_dict(layer, row, False)["image"]["strokes"]
        if index < len(raw_strokes):
            commands = raw_strokes[index].get("commands") or None
    except Exception as error:
        print(f"[auto_mapping] curve capture unavailable ({error}); "
              "keeping the flattened guide.")
    scene.remove_stroke(row, layer, index)

    if _ACTIVE_UNIT["id"] is None:
        # Units are the default for new work: a guide drawn with no unit
        # focused creates one implicitly (adopting anything already in the
        # legacy scratch), so drawing axes flows straight into the live
        # workflow. This runs strictly AFTER the stroke has been read and
        # removed: _create_unit inserts a main column at index 0, which
        # shifts every layer index and would turn the cached cell["layer"]
        # stale. On a build without group tags the creation fails and the
        # capture simply continues into the legacy scratch, as before.
        if _create_unit() is not None:
            print("[auto_mapping] created a new automapping layer for this guide.")

    assets = _assets_for(view)
    if prop == MAPPING_AREA_PROPERTY:
        if not points:
            # The click left the model unchanged: veto the pending commit so
            # no empty history entry is created (and no redo tail is lost).
            message["cancel_history"] = True
            _animean().ui.widget.refresh()
            return
        polygons = _detect_region(scene, view, row, points[0])
        if not polygons:
            print(f"[auto_mapping] no closed region around the click in {view} view; "
                  "draw a closed shape first.")
            message["cancel_history"] = True
            _animean().ui.widget.refresh()
            return
        assets[MAPPING_AREA_PROPERTY] = {"polygons": polygons}
        print(f"[auto_mapping] mapping area set in {view} view (click 'x' to remove)")
    elif prop == ADDITIONAL_PROPERTY:
        # Saves and refreshes both boards itself (the pair lives on both).
        if not run_additional_line_tool(view, points, width, commands):
            message["cancel_history"] = True
        _animean().ui.widget.refresh()
        return
    else:
        # Same entry point Re-expand uses; it saves and refreshes the overlays
        # itself, which is why the shared tail below only runs for the area.
        if not run_center_line_tool(view, prop, points, width, commands):
            message["cancel_history"] = True
            _animean().ui.widget.refresh()
            return
        print(f"[auto_mapping] {ITEM_LABELS[prop]} set in {view} view (redraw replaces it)")
        # NO colour reset here. It existed because there was one global
        # drawing colour and the pen would otherwise inherit the guide's;
        # with per-tool colours (pyfile/tool_colors.py) the pen keeps its own
        # and this reset did active harm - it recorded BLACK against the
        # guide tool, so the first axis drew blue and every one after it drew
        # in the pen's colour (user report).
        _animean().ui.widget.refresh()
        return

    # Written BEFORE the C++ stroke commit fires, so the new guide/area rides
    # in the same history entry and is undone/redone together with it.
    _save_assets(view)
    _overlays_changed(view)
    # The item is captured. The drawing colour is NOT reset: each tool now
    # remembers its own (pyfile/tool_colors.py), so the pen gets its colour
    # back when the pen is armed - and resetting here poisoned this tool's
    # slot with black.
    _animean().ui.widget.refresh()


def _overlay_removed(cell, stroke, message):
    overlay = message.get("overlay") or {}
    item_id = overlay.get("id")
    if not item_id:
        return
    view = message.get("view") or "main"
    if item_id.startswith(ADDITIONAL_PROPERTY + ":"):
        # Additional lines exist as PAIRS - removing one on either board
        # removes the pair (matched by id) on both, and each board's write
        # is committed to ITS OWN history: a one-board commit left the
        # other board's live scriptData ahead of its snapshot, and the next
        # undo silently half-applied.
        try:
            pair_id = int(item_id.split(":", 1)[1])
        except ValueError:
            return
        removed_points = None
        missed = []
        for name in ("child", "main"):
            lines = (_assets_for(name).get(ADDITIONAL_PROPERTY) or {}).get("lines")
            if not lines:
                continue
            for index, line in enumerate(lines):
                if _line_id(line, index) == pair_id:
                    if name == view:
                        removed_points = line.get("points") or None
                    del lines[index]
                    break
            else:
                missed.append(name)
                continue
            if not lines:
                del _assets_for(name)[ADDITIONAL_PROPERTY]
            _save_assets(name)
            _overlays_changed(name)
            try:
                _animean().ui.history_commit("Remove additional line", name)
            except Exception:
                pass
        # Legacy projects can hold pairs whose ids disagree across boards
        # (created before ids existed, or through a since-fixed desync). The
        # id lookup then removes one half and the other board visibly keeps
        # its pink line - fall back to GEOMETRY, guarded twice (each guard's
        # absence was demonstrated to delete a HEALTHY pair's half):
        # only candidates that are THEMSELVES unpaired by id are eligible
        # (a genuine orphan's neighbours are all paired), and matching is by
        # CHORD ENDPOINTS - the two halves of a pair share one Third chord
        # by construction, so their endpoints coincide regardless of the
        # bend the pair encodes (a nearest-polyline match rejected the true
        # partner as soon as the bend exceeded the redraw threshold).
        for name in missed:
            lines = (_assets_for(name).get(ADDITIONAL_PROPERTY) or {}).get("lines")
            if not lines or not removed_points:
                continue
            mapper, _why = _mapper_from_assets(additional=False)
            if mapper is None:
                continue
            forward = mapper if view == "child" else mapper.inverse
            head = forward(removed_points[0])
            tail = forward(removed_points[-1])
            view_ids = {
                _line_id(line, index) for index, line in enumerate(
                    (_assets_for(view).get(ADDITIONAL_PROPERTY) or {}).get("lines") or [])}
            candidates = []
            for index, line in enumerate(lines):
                stored = line.get("points") or []
                if len(stored) < 2 or _line_id(line, index) in view_ids:
                    continue  # paired lines are never fallback targets
                ends = (stored[0], stored[-1])
                direct = max(math.hypot(head[0] - ends[0][0], head[1] - ends[0][1]),
                             math.hypot(tail[0] - ends[1][0], tail[1] - ends[1][1]))
                flipped = max(math.hypot(head[0] - ends[1][0], head[1] - ends[1][1]),
                              math.hypot(tail[0] - ends[0][0], tail[1] - ends[0][1]))
                gap = min(direct, flipped)
                chord = math.hypot(ends[1][0] - ends[0][0], ends[1][1] - ends[0][1])
                if gap <= max(30.0, 0.35 * chord):
                    candidates.append((gap, index))
            candidates.sort()
            if (not candidates
                    or (len(candidates) > 1
                        and candidates[0][0] > 0.5 * candidates[1][0])):
                print(f"[auto_mapping] the removed line's partner on the {name} "
                      "board could not be identified - remove its pink line "
                      "there with its own x.")
                continue
            partner = candidates[0][1]
            del lines[partner]
            if not lines:
                del _assets_for(name)[ADDITIONAL_PROPERTY]
            _save_assets(name)
            _overlays_changed(name)
            try:
                _animean().ui.history_commit("Remove additional line", name)
            except Exception:
                pass
        print(f"[auto_mapping] additional line (id {pair_id}) removed")
        _maybe_auto_run()
        return
    assets = _assets_for(view)
    if item_id in assets:
        del assets[item_id]
        label = ITEM_LABELS.get(item_id, item_id)
        if item_id in GUIDE_PROPERTIES:
            # The frame these coordinates lived in is gone; whatever
            # guide arrives next is a different frame, and the redraw
            # entry can no longer tell (the asset it would have
            # replaced was just deleted here).
            _invalidate_additional_thirds(view)
        _save_assets(view)
        try:
            _animean().ui.history_commit(f"Remove {label}", view)
        except Exception:
            pass  # older builds without the history binding
        if item_id == NEAREST_PROPERTY:
            # The anchor's default IS the crossing, so its x means "reset":
            # the ring reappears there on the refresh below.
            print("[auto_mapping] nearest point reset to the crossing")
        else:
            print(f"[auto_mapping] removed {label} in {view} view")
        _overlays_changed(view)
        _maybe_auto_run()


def _history_restored(cell, stroke, message):
    """After undo/redo/jump/reset the scene's scriptData is authoritative."""
    if (message.get("view") or "main") == "main":
        # A mid-drag Ctrl+Z invalidates the frame cached at press: the
        # restored guides may be entirely different, and solving against the
        # stale frame wrote the anchor in the OLD arc space and then
        # committed it on top of the state the user just undid. The restored
        # script data is authoritative, so discard every part of the old
        # gesture and let a later press build a fresh frame and baseline.
        _NEAREST_DRAG["frame"] = None
        _NEAREST_DRAG["moved"] = False
        _NEAREST_DRAG["had_original"] = False
        _NEAREST_DRAG["original_arc"] = None
        _NEAREST_DRAG["offset"] = (0.0, 0.0)
    # Same reasoning for an in-flight guide/additional-line drag: the restore
    # replaced the assets the gesture was mutating.
    _GUIDE_DRAG.pop(message.get("view") or "main", None)
    _load_assets(message.get("view") or "main")


def _auto_mapping_button(cell, stroke, message):
    global _last_run_handled
    _last_run_handled = True
    if _ACTIVE_UNIT["id"] is None:
        # The manual run also lives in a unit now: create one on the fly,
        # adopting whatever guides the user already drew into the legacy
        # scratch. On an old build this fails gracefully into the legacy
        # run path.
        _create_unit()
    _run()


def _tool_option_changed(cell, stroke, message):
    hook = message.get("hook")
    if hook == "curve_mode":
        value = str(message.get("value", "")).lower()
        if value in CURVE_MODES and _CURVE_MODE["value"] != value:
            _CURVE_MODE["value"] = value
            print(f"[auto_mapping] curve mode -> {value}")
            # RDP only exists in the sampled modes, and the mode lives in the
            # menu bar - outside the options panel - so the panel has to be
            # told to re-read its layout or the slider would linger a step
            # behind the mode it belongs to.
            try:
                _animean().ui.refresh_tool_options()
            except Exception:
                pass
            _maybe_auto_run()
        return
    if hook == "rdp_eps":
        try:
            eps = max(1, min(20, int(message.get("value", 3)))) / 10.0
        except (TypeError, ValueError):
            return
        if _RDP_STATE["eps"] != eps:
            _RDP_STATE["eps"] = eps
            print(f"[auto_mapping] RDP tolerance -> {eps:.1f}px")
            _maybe_auto_run()
        return
    if hook == "fold_split":
        enabled = str(message.get("value", "")).lower() == "on"
        if _FOLD["split"] != enabled:
            _FOLD["split"] = enabled
            print(f"[auto_mapping] front/back fold split {'ON' if enabled else 'OFF'}")
            _maybe_auto_run()
        return
    if hook == "fold_seal":
        enabled = str(message.get("value", "")).lower() == "on"
        if _FOLD["seal"] != enabled:
            _FOLD["seal"] = enabled
            print(f"[auto_mapping] crease strokes {'ON' if enabled else 'OFF'}")
            _maybe_auto_run()
        return
    if hook == "bridge_topology":
        enabled = str(message.get("value", "")).lower() == "on"
        if _BRIDGE["enabled"] != enabled:
            _BRIDGE["enabled"] = enabled
            print(f"[auto_mapping] topology bridging (补全拓扑) "
                  f"{'ON' if enabled else 'OFF'}")
            # The tension slider hides itself via visible_when - both
            # controls live in this panel, so no refresh is needed (the
            # RDP slider needs one only because curve mode lives in the
            # menu bar, outside the panel).
            _maybe_auto_run()
        return
    if hook == "bridge_tension":
        try:
            tension = max(5, min(100, int(message.get("value", 33)))) / 100.0
        except (TypeError, ValueError):
            return
        if _BRIDGE["tension"] != tension:
            _BRIDGE["tension"] = tension
            print(f"[auto_mapping] bridge tension k -> {tension:.2f} x |AB|")
            _maybe_auto_run()
        return
    if hook == "back_shade":
        try:
            shade = max(0, min(100, int(message.get("value", 45))))
        except (TypeError, ValueError):
            return
        # One slider drives a neutral cool lining colour: 0 = near black,
        # 100 = near white. Tools that want a specific colour can set
        # _FOLD["back_color"] directly from the debug pane.
        level = int(round(20 + shade * 2.0))
        shade_color = (level, level + 8, level + 32, 255)
        if _FOLD["back_color"] == shade_color:
            return
        _FOLD["back_color"] = shade_color
        print(f"[auto_mapping] back/lining shade -> {_FOLD['back_color'][:3]}")
        _maybe_auto_run()
        return
    # The refer grid used to be a tool option here. It moved to a per-board
    # View menu: it is a display choice about a BOARD, not a setting of the
    # mapping tool, and as one shared option it could not be answered
    # separately for the two views.


VIEW_MENU_NAME = "view"
REFER_RECT_ITEM = "refer_rect"


def _view_menu_items(view_name):
    """The View menu of one board, re-read on every open so ticks are true.

    In unit mode these entries EDIT THE ACTIVE UNIT's display settings (the
    same state the Advanced Settings window shows) - leaving them wired to
    the legacy globals would have made them dead controls that still looked
    alive. With no unit focused they are disabled rather than hidden, so
    the menu shape stays stable."""
    def build():
        unit_mode = bool(_UNIT_META)
        settings = _unit_settings() if _ACTIVE_UNIT["id"] else None
        enabled = (not unit_mode) or settings is not None
        grid_on = (settings["show_grid"] if settings is not None
                   else _REFER_RECT.get(view_name, False))
        divisions = (settings["grid_divisions"] if settings is not None
                     else _GRID["divisions"])
        occlusion_on = (settings["show_occlusion"] if settings is not None
                        else _OCCLUSION["enabled"])
        items = [{"name": REFER_RECT_ITEM, "title": "Mapping Refer Rect",
                  "kind": "check", "checked": grid_on, "enabled": enabled}]
        # Grid density lives with the grid it densifies. One shared value
        # for both boards: the point of the fine setting is COMPARING the
        # two boards' grids cell by cell, which needs them to agree.
        items.append({
            "name": "refer_rect_divisions", "title": "Refer Rect Divisions",
            "kind": "submenu",
            "items": [{"name": f"grid_div_{n}", "title": f"{n} x {n}",
                       "kind": "radio", "enabled": enabled,
                       "checked": divisions == n}
                      for n in GRID_DIVISION_CHOICES]})
        if view_name == "child":
            items.append({"name": OCCLUSION_BUTTON, "title": "Occluded Areas",
                          "kind": "check", "checked": occlusion_on,
                          "enabled": enabled})
        return items
    return build


def _unit_view_setting(name, value):
    """Route a View-menu toggle onto the active unit's settings."""
    uid = _ACTIVE_UNIT["id"]
    meta = _UNIT_META.get(uid)
    if not meta:
        return False
    meta.setdefault("settings", {})[name] = value
    _save_units("main")
    _invalidate_grid_cache()
    _push_overlay("main")
    _push_overlay("child")
    try:
        animean = _animean()
        animean.ui.refresh()
        animean.ui.history_commit("Unit Settings", "main")
    except Exception:
        pass
    return True


def _view_menu_action(message):
    if message.get("menu") != VIEW_MENU_NAME:
        return
    view = message.get("view") or "main"
    name = message.get("name") or ""
    checked = bool(message.get("checked"))
    if name == REFER_RECT_ITEM:
        if _UNIT_META:
            if _unit_view_setting("show_grid", checked):
                print(f"[auto_mapping] unit refer rect grid "
                      f"{'ON' if checked else 'OFF'}")
            return
        if _REFER_RECT.get(view) == checked:
            return
        _REFER_RECT[view] = checked
        _invalidate_grid_cache()
        _push_overlay(view)
        print(f"[auto_mapping] refer rect grid on {view} "
              f"{'ON' if checked else 'OFF'}")
        return
    if name.startswith("grid_div_"):
        try:
            divisions = int(name[len("grid_div_"):])
        except ValueError:
            return
        if divisions not in GRID_DIVISION_CHOICES:
            return
        if _UNIT_META:
            if _unit_view_setting("grid_divisions", divisions):
                print(f"[auto_mapping] unit grid -> {divisions} x {divisions}")
            return
        if _GRID["divisions"] == divisions:
            return
        _GRID["divisions"] = divisions
        _invalidate_grid_cache()
        for board in ("child", "main"):
            if _REFER_RECT.get(board):
                _push_overlay(board)
        print(f"[auto_mapping] refer rect grid -> {divisions} x {divisions} "
              "iso-lines")
        return
    if name == OCCLUSION_BUTTON:
        if _UNIT_META:
            if _unit_view_setting("show_occlusion", checked):
                print(f"[auto_mapping] unit occlusion preview "
                      f"{'ON' if checked else 'OFF'}")
            return
        _set_occlusion(checked)


def _set_occlusion(enabled):
    _OCCLUSION["enabled"] = bool(enabled)
    _OCCLUSION_CACHE["items"] = None
    _push_overlay("child")  # computes and caches the bands as a side effect
    if not _OCCLUSION["enabled"]:
        print("[auto_mapping] occlusion preview OFF")
        return
    _report_occlusion()


def _report_occlusion():
    note = _OCCLUSION_CACHE.get("note")
    items = _OCCLUSION_CACHE.get("items") or []
    if note:
        print(f"[auto_mapping] occlusion preview: nothing to show - {note}.")
    elif not items:
        print("[auto_mapping] occlusion preview: these guides fold nothing face-down.")
    else:
        share = _OCCLUSION_CACHE.get("share", 0.0)
        print(f"[auto_mapping] occlusion preview ON: {share * 100.0:.0f}% of the "
              f"guide rectangle lands face-down (tinted).")


def _legacy_guide_axis(stroke, layer_name):
    """Which axis an OLD snapshot stroke is, when both shared one property.

    By COLOUR, not by layer name. The name looks like the obvious key and is
    the wrong one: _create_mapped_layer routes every name through
    uniqueLayerName, which drifts a taken name to "H axis1", "H axis2", ... -
    so only the very first run in a document ever owns the bare "H axis", and
    keying on it silently lost every later run's snapshot. The colour is
    exact: the old code picked it from the loop's intended name before the
    model renamed anything, so blue IS the H axis and green IS the V axis.
    The name is kept as a last resort, matched as a prefix.
    """
    color = stroke.get("color") or {}
    try:
        rgb = (int(color.get("r", -1)), int(color.get("g", -1)), int(color.get("b", -1)))
    except (TypeError, ValueError):
        rgb = (-1, -1, -1)
    if rgb == H_COLOR[:3]:
        return H_GUIDE_LAYER_PROPERTY
    if rgb == V_COLOR[:3]:
        return V_GUIDE_LAYER_PROPERTY
    if layer_name.startswith(H_LAYER_NAME):
        return H_GUIDE_LAYER_PROPERTY
    if layer_name.startswith(V_LAYER_NAME):
        return V_GUIDE_LAYER_PROPERTY
    return ""


def _guide_axes_in_layers(scene, frame, layer_indices):
    """{property -> (points, width, commands)} for the axis snapshots on
    these layers.

    Identified by the stroke property, so a renamed layer still restores
    correctly; the layer name is only consulted for snapshots written before
    the two axes carried separate properties. `commands` is the snapshot
    stroke's real path (None on snapshots from before guides kept curves) -
    the restore hands it back to run_center_line_tool so a curve guide
    round-trips as a curve.
    """
    found = {}
    for index in layer_indices:
        if index is None or index < 0:
            continue
        try:
            cell = scene.cell_to_dict(index, frame, True, POLY_STEP)
        except Exception:
            continue
        raw_strokes = []
        try:
            raw_strokes = scene.cell_to_dict(index, frame, False)["image"]["strokes"]
        except Exception:
            pass
        name = ""
        try:
            name = scene.layer_name(index)
        except Exception:
            pass
        for position, stroke in enumerate(cell["image"]["strokes"]):
            prop = stroke.get("property") or ""
            if prop == GUIDE_LAYER_PROPERTY:
                prop = _legacy_guide_axis(stroke, name)
            if prop == NEAREST_LAYER_PROPERTY and prop not in found:
                # The layer NAME carries the exact arc; the ring centre is
                # the geometric fallback (renamed layer). The ring closes on
                # a duplicated first vertex - averaging it twice biased the
                # centre by radius/17 px, drifting the anchor a third of a
                # pixel per map->re-expand cycle, always the same direction.
                arc = None
                match = re.search(r"\(\s*(-?\d+(?:\.\d+)?)\s*,\s*(-?\d+(?:\.\d+)?)\s*\)",
                                  name or "")
                if match:
                    arc = (float(match.group(1)), float(match.group(2)))
                points = _stroke_points(stroke)
                if points and len(points) >= 2 and _dist(points[0], points[-1]) <= 1e-9:
                    points = points[:-1]
                if points:
                    cx = sum(p[0] for p in points) / len(points)
                    cy = sum(p[1] for p in points) / len(points)
                    found[prop] = ((cx, cy), 0.0, arc)
                continue
            if prop not in (H_GUIDE_LAYER_PROPERTY, V_GUIDE_LAYER_PROPERTY):
                continue
            points = _stroke_points(stroke)
            if len(points) < 2 or prop in found:
                continue
            commands = None
            if position < len(raw_strokes):
                commands = raw_strokes[position].get("commands") or None
            found[prop] = (points, float(stroke.get("width", 3.0)), commands)
    return found


def _install_unit_output(scene, out, uid, meta):
    """Adopt this run's layers into the unit group and retire the previous
    output IN PLACE.

    Focus survives on the new front layer (set BEFORE the old members die,
    so the current layer never dangles into a deleted column), stale copies
    never pile up, and only layers the unit RECORDED as its own are touched
    - anything the user dragged into the group by hand stays.
    """
    try:
        adopted = scene.add_layers_to_group(int(uid), list(out.layers))
    except AttributeError:
        adopted = -1  # older build without the grouping binding
    if adopted == 0 and out.layers:
        # The unit's group is gone (pruned when its last member died) and
        # add_layers_to_group reports that with a plain 0. Re-house the
        # unit around this run's layers; config and settings migrate to
        # the new group id.
        new_gid = 0
        try:
            new_gid = scene.create_layer_group(UNIT_LAYER_TITLE,
                                               list(out.layers), [], True)
            if new_gid and not scene.set_layer_group_tag(new_gid, UNIT_TAG):
                new_gid = 0
        except AttributeError:
            new_gid = 0
        if new_gid:
            healed = str(new_gid)
            _UNIT_META[healed] = meta
            _UNIT_META.pop(uid, None)
            for store_view in ("main", "child"):
                store = _UNIT_ASSETS.setdefault(store_view, {})
                if uid in store:
                    store[healed] = store.pop(uid)
            if _ACTIVE_UNIT["id"] == uid:
                _ACTIVE_UNIT["id"] = healed
            _save_units("child")
            print(f"[auto_mapping] automapping layer re-housed as group "
                  f"{healed} (its group had been deleted).")
            uid = healed

    members = {}
    primary_id = 0
    primary_index = None
    for index, role in zip(out.layers, out.layer_roles):
        lid = scene.layer_id_at(index)
        if lid:
            members[str(lid)] = dict(role)
        if role.get("role") == "front" and primary_index is None:
            primary_index = index
            primary_id = lid
    if primary_index is None and out.layers:
        primary_index = out.layers[0]
        primary_id = scene.layer_id_at(primary_index)

    if primary_index is not None:
        # Directly on the MAIN model, never ui.set_current: that binding
        # writes whichever view is active, and a live re-run triggered from
        # a texture-board edit would move the CHILD's layer focus instead.
        try:
            scene.set_current_layer(primary_index)
        except Exception:
            pass

    old_members = set((meta.get("members") or {}).keys())
    old_members.add(str(meta.get("primary") or 0))
    for lid in old_members:
        try:
            lid_int = int(lid)
        except (TypeError, ValueError):
            continue
        if lid_int <= 0 or str(lid_int) in members:
            continue
        index = scene.layer_index_for_id(lid_int)
        if index >= 0:
            _discard_mapped_layer(scene, index)

    meta["members"] = members
    meta["primary"] = primary_id
    _apply_member_visibility(uid, scene)
    _save_units("main")


def _apply_member_visibility(uid, scene=None):
    """Push the unit's front/back/crease display toggles onto its member
    layers - the settings window's checkboxes ARE layer visibility."""
    meta = _UNIT_META.get(uid)
    if not meta:
        return
    if scene is None:
        try:
            scene = _scene_model("main")
        except Exception:
            return
    settings = _unit_settings(uid)
    for lid, info in (meta.get("members") or {}).items():
        try:
            index = scene.layer_index_for_id(int(lid))
        except (TypeError, ValueError):
            continue
        if index < 0:
            continue
        visible = bool(settings.get(f"{info.get('role', 'front')}_visible", True))
        try:
            scene.set_layer_visible(index, visible)
        except Exception:
            pass


def _maybe_auto_run():
    """Live re-render: re-run the active unit's mapping after a committed
    edit (guide drag, redraw, additional line, nearest move, option change,
    pattern stroke). A no-op outside unit focus, while a run is already in
    flight, or when the unit's Live Re-render checkbox is off."""
    uid = _ACTIVE_UNIT["id"]
    if uid is None or _RUN_GUARD["depth"]:
        return
    if not _unit_settings(uid).get("auto_render", True):
        return
    _run()


def _pattern_changed(cell, stroke, message):
    """Texture-board artwork edits refresh the focused unit automatically."""
    if message.get("view") != "child" or _RUN_GUARD["depth"]:
        return
    if message.get("tool") == "extra" and message.get("event") == "linefinish":
        return  # a guide/area/additional capture: its own tail re-runs
    _maybe_auto_run()


def _create_unit(view="main", commit=True):
    """New Auto-Mapping Layer: a tagged group + one (empty) primary member.

    The unit starts with no guides - the user draws H/V with the center-line
    tools while the unit has focus, and every capture lands in THIS unit's
    config. The member layer exists from the start so the unit is focusable
    (and stays focusable across re-runs)."""
    try:
        scene = _scene_model("main")
    except Exception as error:
        print(f"[auto_mapping] unit creation skipped: {error}")
        return None
    row = max(scene.current_frame(), 0)
    layer = _create_mapped_layer(scene, row)
    if layer < 0:
        print("[auto_mapping] could not create the unit's layer.")
        return None
    try:
        # Collapsed and named like a layer: the unit reads as ONE row in the
        # panel (clicking it focuses its first member), not a nested tree.
        gid = scene.create_layer_group(UNIT_LAYER_TITLE, [layer], [], True)
    except AttributeError:
        gid = 0
    if not gid:
        print("[auto_mapping] this build cannot group layers; no unit created.")
        _discard_mapped_layer(scene, layer)
        return None
    try:
        scene.set_layer_group_tag(gid, UNIT_TAG)
    except AttributeError:
        print("[auto_mapping] this build cannot tag layer groups; no unit created.")
        return None
    uid = str(gid)
    primary_id = scene.layer_id_at(layer)
    _UNIT_META[uid] = {
        "settings": dict(_UNIT_SETTING_DEFAULTS),
        "primary": primary_id,
        "members": {str(primary_id): {"role": "front", "depth": 0}},
    }
    for adopt_view in ("main", "child"):
        # Guides/areas/additional lines drawn BEFORE any unit existed live
        # in the legacy scratch; the first unit adopts them so "draw axes,
        # then run" flows straight into the unit workflow instead of
        # leaving the axes stranded in the hidden legacy set.
        scratch = _MAPPING_ASSETS.get(adopt_view) or {}
        _UNIT_ASSETS.setdefault(adopt_view, {})[uid] = (
            _sanitize_assets(scratch) if scratch else {})
        if scratch:
            _MAPPING_ASSETS[adopt_view] = {}
    _save_units("main")
    _save_units("child")
    animean = _animean()
    try:
        # Directly on the main model (ui.set_current writes whichever view
        # is active); the refresh below re-syncs panels and attention.
        scene.set_current_layer(layer)
    except Exception:
        pass
    _activate_unit(uid)
    animean.ui.refresh()
    if commit:
        try:
            animean.ui.history_commit("New Auto-Mapping Layer", "main")
            # The child scene's scriptData gained this unit's (empty) slot:
            # without its own commit, one texture-board undo would drop the
            # child half of the config while main kept it.
            animean.ui.history_commit("New Auto-Mapping Layer", "child")
        except Exception:
            pass
    return uid


def _duplicate_unit(source_uid):
    """Duplicate an automapping layer: copy its CONFIG into a fresh unit and
    re-render. The output regenerates from the config, so nothing else needs
    deep-copying - this is the workflow that replaced Re-expand."""
    if source_uid not in _UNIT_META:
        print("[auto_mapping] duplicate: that group is not an automapping layer.")
        return None
    source_settings = dict(_UNIT_META[source_uid].get("settings") or {})
    source_assets = {
        view: _UNIT_ASSETS.get(view, {}).get(source_uid) or {}
        for view in ("main", "child")
    }
    uid = _create_unit(commit=False)
    if uid is None:
        return None
    _UNIT_META[uid]["settings"] = source_settings
    for view in ("main", "child"):
        # _sanitize_assets builds fresh structures from what it reads, so
        # the copy shares nothing mutable with the source unit.
        _UNIT_ASSETS[view][uid] = _sanitize_assets(source_assets[view])
        _save_units(view)
    _invalidate_grid_cache()
    _push_overlay("main")
    _push_overlay("child")
    _maybe_auto_run()
    try:
        animean = _animean()
        animean.ui.history_commit("Duplicate Auto-Mapping Layer", "main")
        animean.ui.history_commit("Duplicate Auto-Mapping Layer", "child")
    except Exception:
        pass
    return uid


def _convert_group_to_unit(scene, gid, member_indices):
    """Adopt a legacy 'Auto Mapping' group as a mapping unit.

    Pre-unit runs left nested groups: output layers plus a collapsed H/V
    subgroup of axis-snapshot layers. Conversion tags the group, absorbs a
    CONFIG for it - the live legacy scratch guides when present, else the
    group's own axis snapshots - classifies the output members by name into
    front/back/seal roles, deletes the snapshot layers the config replaces,
    and collapses the group into the one-row automapping-layer look. The
    document runs in unit mode from here on.
    """
    try:
        if scene.layer_group_tag(gid) == UNIT_TAG:
            return str(gid)
        if not scene.set_layer_group_tag(gid, UNIT_TAG):
            return None
    except AttributeError:
        print("[auto_mapping] this build cannot tag layer groups.")
        return None
    uid = str(gid)
    frame = max(scene.current_frame(), 0)

    # Config: the group's OWN axis snapshots are the authority - they are
    # the record of the run that made this group. The live scratch fills
    # gaps only, and only for the FIRST conversion: the scratch is one
    # scene-global set, and letting a second group adopt it silently handed
    # every later conversion the first group's guides while its own
    # snapshot layers were deleted below - irrecoverably.
    first_unit = not _UNIT_META
    scratch_main = dict(_MAPPING_ASSETS.get("main") or {}) if first_unit else {}
    child_assets = dict(_MAPPING_ASSETS.get("child") or {}) if first_unit else {}
    snapshots = _guide_axes_in_layers(scene, frame, member_indices)
    main_assets = {key: value for key, value in scratch_main.items()
                   if key not in GUIDE_PROPERTIES}
    for prop, target in ((H_GUIDE_LAYER_PROPERTY, H_PROPERTY),
                         (V_GUIDE_LAYER_PROPERTY, V_PROPERTY)):
        if prop in snapshots:
            points, width, commands = snapshots[prop]
            item = {"points": [tuple(p) for p in points], "width": width}
            if commands:
                item["commands"] = commands
            main_assets[target] = item
        elif target in scratch_main:
            main_assets[target] = scratch_main[target]
    marker = snapshots.get(NEAREST_LAYER_PROPERTY)
    if marker and marker[2]:
        main_assets[NEAREST_PROPERTY] = {"arc": [float(marker[2][0]),
                                                 float(marker[2][1])]}

    # Output members classified by name (the only record legacy runs kept);
    # snapshot layers are dropped - their information lives in the config now.
    members = {}
    primary_id = 0
    drop_ids = []
    for index in member_indices or []:
        if index is None or index < 0:
            continue
        lid = scene.layer_id_at(index)
        if not lid:
            continue
        try:
            name = scene.layer_name(index) or ""
        except Exception:
            name = ""
        if (name.startswith(H_LAYER_NAME) or name.startswith(V_LAYER_NAME)
                or name.startswith(NEAREST_LAYER_NAME)):
            drop_ids.append(lid)
            continue
        if name.startswith(SEAL_LAYER_NAME):
            role = "seal"
        elif name.startswith(BACK_LAYER_NAME) or " depth " in name:
            role = "back"
        else:
            role = "front"
        members[str(lid)] = {"role": role,
                             "depth": 0 if role == "front" else 1}
        if role == "front" and not primary_id:
            primary_id = lid
    if not primary_id and members:
        primary_id = int(next(iter(members)))

    _UNIT_META[uid] = {"settings": dict(_UNIT_SETTING_DEFAULTS),
                       "primary": primary_id,
                       "members": members}
    _UNIT_ASSETS.setdefault("main", {})[uid] = _sanitize_assets(main_assets)
    _UNIT_ASSETS.setdefault("child", {})[uid] = _sanitize_assets(child_assets)

    for lid in drop_ids:
        index = scene.layer_index_for_id(lid)
        if index >= 0:
            _discard_mapped_layer(scene, index)
    try:
        scene.set_layer_group_name(gid, UNIT_LAYER_TITLE)
        scene.set_layer_group_collapsed(gid, True)
    except AttributeError:
        pass
    _save_units("main")
    _save_units("child")
    if primary_id:
        index = scene.layer_index_for_id(primary_id)
        if index >= 0:
            try:
                scene.set_current_layer(index)
            except Exception:
                pass
    _activate_unit(uid)
    try:
        animean = _animean()
        animean.ui.refresh()
        animean.ui.history_commit("Convert to Auto-Mapping Layer", "main")
        animean.ui.history_commit("Convert to Auto-Mapping Layer", "child")
    except Exception:
        pass
    have_guides = all(p in main_assets for p in GUIDE_PROPERTIES)
    print(f"[auto_mapping] group {gid} converted to an automapping layer "
          f"({len(members)} member(s); "
          + ("guides adopted" if have_guides
             else "guides incomplete - draw the missing axes while focused")
          + ").")
    return uid


def _create_plain_layer(view, fill=False):
    """New Line Layer / New Fill Layer from the panel's context menu."""
    try:
        scene = _scene_model(view)
    except Exception as error:
        print(f"[auto_mapping] layer creation skipped: {error}")
        return
    index = scene.add_fill_layer() if fill else scene.add_layer()
    if index is None or index < 0:
        return
    try:
        scene.set_layer_name(index, "fill layer" if fill else "line layer")
    except Exception:
        pass
    animean = _animean()
    try:
        animean.ui.set_current(layer=index)
    except Exception:
        pass
    animean.ui.refresh()
    try:
        animean.ui.history_commit(
            "New Fill Layer" if fill else "New Line Layer", view)
    except Exception:
        pass


def _unit_from_menu_message(scene, message):
    """Resolve which unit a layer-menu action targets."""
    group = int(message.get("group") or 0)
    if group > 0:
        try:
            if scene.layer_group_tag(group) == UNIT_TAG:
                return str(group)
        except AttributeError:
            return None
        return None
    layer = message.get("layer")
    if isinstance(layer, int) and layer >= 0:
        return _unit_for_layer(scene, layer)
    return None


def _layer_menu_items(context):
    """The layer panel's right-click entries.

    An automapping unit's rows get Advanced Settings + Duplicate; every
    context (rows and the empty panel area alike) gets the typed-layer
    creation entries. The old Re-expand entry is gone: guides live in the
    unit's config now, and duplicating the unit is the supported way to
    derive a variant mapping.
    """
    items = []
    view = context.get("view") or "main"
    uid = None
    if context.get("kind") == "group" and context.get("tag") == UNIT_TAG:
        uid = str(context.get("group") or 0)
    elif context.get("kind") == "layer" and context.get("owner_tag") == UNIT_TAG:
        uid = str(context.get("owner_group") or 0)
    if uid and uid in _UNIT_META:
        # Stash the target for the settings window: the C++ dialog opens
        # with no per-row context by design (see showLayerContextMenu).
        _SETTINGS_TARGET["unit"] = uid
        items.append({"name": "unit_settings", "title": "Advanced Settings...",
                      "kind": "settings", "settings": UNIT_SETTINGS_NAME})
        items.append({"name": DUPLICATE_UNIT_ACTION,
                      "title": f"Duplicate {UNIT_LAYER_TITLE}"})
        items.append({"kind": "separator", "name": "-"})
    elif (context.get("kind") == "group" and view == "main"
          and context.get("tag") != UNIT_TAG):
        # A pre-refactor "Auto Mapping" group (or any hand-made group the
        # user wants to promote): one click migrates it to a unit.
        items.append({"name": CONVERT_UNIT_ACTION,
                      "title": f"Convert to {UNIT_LAYER_TITLE}"})
        items.append({"kind": "separator", "name": "-"})
    if view == "main":
        items.append({"name": NEW_UNIT_ACTION,
                      "title": f"New {UNIT_LAYER_TITLE}"})
    items.append({"name": NEW_LINE_LAYER_ACTION, "title": "New Line Layer"})
    items.append({"name": NEW_FILL_LAYER_ACTION, "title": "New Fill Layer"})
    return items


def _layer_menu_action(message):
    action = message.get("action") or ""
    view = message.get("view") or "main"
    if action == NEW_UNIT_ACTION:
        _create_unit(view)
        return
    if action == NEW_LINE_LAYER_ACTION:
        _create_plain_layer(view, fill=False)
        return
    if action == NEW_FILL_LAYER_ACTION:
        _create_plain_layer(view, fill=True)
        return
    if action == DUPLICATE_UNIT_ACTION:
        try:
            scene = _scene_model("main")
        except Exception as error:
            print(f"[auto_mapping] duplicate skipped: {error}")
            return
        uid = _unit_from_menu_message(scene, message)
        if uid:
            _duplicate_unit(uid)
        return
    if action == CONVERT_UNIT_ACTION:
        gid = int(message.get("group") or 0)
        if gid <= 0:
            return
        try:
            scene = _scene_model("main")
        except Exception as error:
            print(f"[auto_mapping] conversion skipped: {error}")
            return
        _convert_group_to_unit(scene, gid, message.get("members") or [])
        return


def _unit_settings_layout():
    """The Advanced Settings window of one automapping layer.

    Re-evaluated every time the window opens (register_settings takes the
    callable), so the checkboxes always show the TARGET unit's stored state.
    """
    uid = _SETTINGS_TARGET["unit"] or _ACTIVE_UNIT["id"]
    s = _unit_settings(uid)

    def check(name, title, row):
        return {"name": name, "hook": "unit_setting", "type": "check",
                "title": title, "value": "on" if s.get(name) else "off",
                "row": row, "start_column": 0, "end_column": 1}

    controls = [
        check("show_h", "H Axis", 0),
        check("show_v", "V Axis", 1),
        check("show_additional", "Additional Lines", 2),
        check("show_area", "Mapping Area", 3),
        check("show_nearest", "Nearest-Point Handle", 4),
        check("show_grid", "Refer-Rect Grid", 5),
        {"name": "grid_divisions", "hook": "unit_setting", "type": "list",
         "title": "Grid Density",
         "options": [{"title": str(n), "value": str(n)}
                     for n in GRID_DIVISION_CHOICES],
         "value": str(s.get("grid_divisions", 5)),
         "row": 6, "start_column": 0, "end_column": 1,
         "visible_when": {"name": "show_grid", "values": ["on"]}},
        check("show_occlusion", "Occluded Areas (texture board)", 7),
        check("front_visible", "Front / Front Lines", 8),
        check("back_visible", "Back / Back Lines", 9),
        check("seal_visible", "Crease Lines", 10),
        check("auto_render", "Live Re-render", 11),
    ]
    return {"controls": controls}


def _unit_setting_changed(cell, stroke, message):
    """One handler for the whole Advanced Settings window."""
    if message.get("hook") != "unit_setting":
        return
    uid = _SETTINGS_TARGET["unit"] or _ACTIVE_UNIT["id"]
    meta = _UNIT_META.get(uid)
    if not meta:
        return
    name = message.get("name") or ""
    raw = message.get("value")
    settings = meta.setdefault("settings", {})
    if name == "grid_divisions":
        try:
            value = int(raw)
        except (TypeError, ValueError):
            return
        if value not in GRID_DIVISION_CHOICES:
            return
        settings[name] = value
    elif name in _UNIT_SETTING_DEFAULTS:
        settings[name] = str(raw).lower() in ("on", "true", "1")
    else:
        return
    _save_units("main")
    if name in ("front_visible", "back_visible", "seal_visible"):
        _apply_member_visibility(uid)
    if uid == _ACTIVE_UNIT["id"]:
        _invalidate_grid_cache()
        _push_overlay("main")
        _push_overlay("child")
    try:
        animean = _animean()
        animean.ui.refresh()
        # The settings live in scriptData and drive real layer visibility:
        # a document edit, so it must be undoable.
        animean.ui.history_commit("Unit Settings", "main")
    except Exception:
        pass


MENU_NAME = "auto_mapping"
LINE_SETTINGS_NAME = "auto_mapping_lines"


def _menu_items():
    """Built fresh every time the menu opens, so the ticks are the truth."""
    mode = curve_mode()
    return [
        {"name": "line_settings", "title": "Line Display Settings...",
         "kind": "settings", "settings": LINE_SETTINGS_NAME},
        {"kind": "separator"},
        {"name": "calc_mode", "title": "Calculation Mode", "kind": "submenu",
         "items": [
             {"name": "mode_bezier", "title": "Bezier", "kind": "radio",
              "checked": mode == "bezier"},
             {"name": "mode_spline", "title": "Spline", "kind": "radio",
              "checked": mode == "spline"},
             {"name": "mode_polyline", "title": "Polyline", "kind": "radio",
              "checked": mode == "polyline"},
         ]},
        {"name": "additional_falloff", "title": "Additional Line Falloff",
         "kind": "submenu",
         "items": [
             {"name": "falloff_linear", "title": "Linear", "kind": "radio",
              "checked": _ADDITIONAL["falloff"] == "linear"},
             {"name": "falloff_quadratic", "title": "Quadratic", "kind": "radio",
              "checked": _ADDITIONAL["falloff"] == "quadratic"},
         ]},
        {"name": "constrain_outline", "title": "约束外轮廓 / Constrain Outline",
         "kind": "check", "checked": _ADDITIONAL["constrain_outline"]},
        {"kind": "separator"},
        {"name": "to_3d", "title": "To 3D"},
    ]


def _line_settings_layout():
    """The settings window, in the same control schema the tool panel uses."""
    def color(name, title, key, row):
        return {"name": name, "type": "color", "title": title,
                "hook": "line_display", "value": _hex_color(_display_color(key)),
                "row": row, "start_column": 0, "end_column": 1}

    def style(name, title, key, row):
        return {"name": name, "type": "list", "title": title,
                "hook": "line_display", "value": str(_display_style(key)),
                "row": row, "start_column": 0, "end_column": 2,
                "options": [{"title": t, "value": str(v)} for t, v in LINE_STYLES]}

    def width(name, title, key, row, low, high, scale=1.0):
        return {"name": name, "type": "slider", "title": title,
                "hook": "line_display", "min": low, "max": high,
                "value": int(round(float(_LINE_DISPLAY.get(key, 3.0)) * scale)),
                "row": row, "start_column": 0, "end_column": 2}

    return {
        "row_spacing": 8,
        "column_spacing": 6,
        "controls": [
            color("h_color", "H axis colour", "h_color", 0),
            width("h_width", "H axis width", "h_width", 1, 1, 30),
            style("h_style", "H axis line", "h_style", 2),

            color("v_color", "V axis colour", "v_color", 3),
            width("v_width", "V axis width", "v_width", 4, 1, 30),
            style("v_style", "V axis line", "v_style", 5),

            color("seal_color", "Crease colour", "seal_color", 6),
            # Stored as a FACTOR of the mapped stroke width, so the slider is
            # in tenths and the label says so.
            width("seal_width", "Crease width (x0.1)", "seal_width", 7, 1, 40, 10.0),
            style("seal_style", "Crease line", "seal_style", 8),

            color("back_color", "Lining colour", "back_color", 9),
            {"name": "occlusion_alpha", "type": "slider",
             "title": "Occluded-area tint", "hook": "line_display",
             "min": 0, "max": 255,
             "value": int(_LINE_DISPLAY.get("occlusion_alpha", 70)),
             "row": 10, "start_column": 0, "end_column": 2},
        ],
    }


def _hex_color(rgba):
    r, g, b, a = (list(rgba) + [255, 255, 255, 255])[:4]
    return f"#{a:02x}{r:02x}{g:02x}{b:02x}"


def _parse_hex_color(text):
    text = str(text).strip().lstrip("#")
    try:
        if len(text) == 8:      # AARRGGBB, what the colour control reports
            a, r, g, b = (int(text[i:i + 2], 16) for i in (0, 2, 4, 6))
            return (r, g, b, a)
        if len(text) == 6:
            r, g, b = (int(text[i:i + 2], 16) for i in (0, 2, 4))
            return (r, g, b, 255)
    except ValueError:
        pass
    return None


def _menu_action(message):
    if message.get("menu") != MENU_NAME:
        return
    name = message.get("name") or ""
    if name == "to_3d":
        try:
            run_to_3d()
        except Exception as error:
            print(f"[auto_mapping] To 3D failed: {error}")
        return
    if name.startswith("falloff_"):
        falloff = name[len("falloff_"):]
        if falloff in ADDITIONAL_FALLOFFS and _ADDITIONAL["falloff"] != falloff:
            _ADDITIONAL["falloff"] = falloff
            # The occlusion preview bakes the warp, so its cache is stale now.
            _invalidate_grid_cache()
            _push_overlay("child")
            _push_overlay("main")
            print(f"[auto_mapping] additional line falloff -> {falloff} "
                  "(applies on the next run)")
        return
    if name == "constrain_outline":
        _ADDITIONAL["constrain_outline"] = not _ADDITIONAL["constrain_outline"]
        _invalidate_grid_cache()
        _push_overlay("child")
        _push_overlay("main")
        state = "ON" if _ADDITIONAL["constrain_outline"] else "OFF"
        print(f"[auto_mapping] constrain outline (约束外轮廓) {state} - "
              + ("the H/V window outline holds its shape; a side an "
                 "additional line crosses out of is released"
                 if _ADDITIONAL["constrain_outline"] else
                 "the outline deforms freely with the nearest line"))
        return
    if not name.startswith("mode_"):
        return
    mode = name[len("mode_"):]
    if mode not in CURVE_MODES or _CURVE_MODE["value"] == mode:
        return
    _CURVE_MODE["value"] = mode
    print(f"[auto_mapping] calculation mode -> {mode}")


def _line_display_changed(message):
    """One hook for the whole settings window; the control name is the key."""
    if message.get("hook") != "line_display":
        return
    name = message.get("name") or ""
    value = message.get("value")
    if name.endswith("_color"):
        parsed = _parse_hex_color(value)
        if parsed is None:
            return
        _LINE_DISPLAY[name] = parsed
    elif name.endswith("_style"):
        try:
            _LINE_DISPLAY[name] = max(1, min(5, int(value)))
        except (TypeError, ValueError):
            return
    elif name == "occlusion_alpha":
        try:
            _LINE_DISPLAY[name] = max(0, min(255, int(value)))
        except (TypeError, ValueError):
            return
    elif name.endswith("_width"):
        try:
            number = float(value)
        except (TypeError, ValueError):
            return
        # The crease width is a factor, so its slider is in tenths.
        _LINE_DISPLAY[name] = number / 10.0 if name == "seal_width" else number
    else:
        return

    # Guides are overlays, so a display change shows immediately; the crease
    # and the lining are baked into strokes and apply to the NEXT run.
    _invalidate_grid_cache()
    _push_overlay("main")
    _push_overlay("child")
    print(f"[auto_mapping] {name} -> {_LINE_DISPLAY[name]}")


def register_hooks():
    python_hooks.set_hook(_capture_mapping_item, linefinish=True, tool="extra")
    python_hooks.set_hook(_overlay_removed, overlayremove=True)
    python_hooks.set_hook(_history_restored, historyrestore=True)
    python_hooks.set_hook(_auto_mapping_button, extra=True, tool=AUTO_MAPPING2_TOOL)
    # tool="extra" keeps this hook from intercepting (and debug-dock-spamming)
    # every built-in tool's option events — refer_rect only exists on extra
    # tools anyway.
    python_hooks.set_hook(_tool_option_changed, option=True, tool="extra")


def run_center_line_tool(view_name, property_value, points, width=3.0, commands=None):
    """Install one center line. THE entry point of the H / V line tools.

    Drawing a guide and re-expanding a stored one are the same act - "this
    line is now the H (or V) axis of this board" - so they go through this
    one function instead of each writing the asset dict themselves. Anything
    that installs a guide gets the save, the overlay refresh and the
    validation for free, and there is one place to change when installing a
    guide has to do more.

    `commands` is the stroke's real mixed line/Bezier path when the caller
    has it (capture does; stored axis snapshots and legacy assets do not).
    With commands the frame evaluates the guide on the true curve with
    Gauss-Legendre arc tables; without, it stays the flattened polyline.

    Note it deliberately does NOT arm the drawing tool: re-expanding from the
    layer panel must not silently swap the pen out from under the user.
    """
    if property_value not in GUIDE_PROPERTIES:
        return False
    cleaned = [(float(x), float(y)) for x, y in points or []]
    if len(cleaned) < 2:
        print(f"[auto_mapping] {ITEM_LABELS.get(property_value, property_value)} "
              "needs at least two points.")
        return False
    item = {"points": cleaned, "width": float(width)}
    if commands:
        try:
            if _Curve(cleaned, commands).curved:
                item["commands"] = commands
        except Exception as error:
            print(f"[auto_mapping] guide commands rejected ({error}); "
                  "keeping the flattened polyline.")
    assets = _assets_for(view_name)
    assets[property_value] = item
    # Installing a guide changes the frame the additional lines' stored
    # coordinates live in; they must re-derive from their drawn points.
    # UNCONDITIONALLY: gating this on "replacing" missed the
    # delete-then-redraw workflow - the x removed the guide asset first,
    # so the redraw saw no predecessor, skipped the invalidation, and
    # the stale thirds kept describing the retired frame (the user
    # report: the additional deformation never recomputed). With no
    # additional lines it is a no-op.
    _invalidate_additional_thirds(view_name)
    # The moment BOTH main axes exist, the nearest-end anchor comes into
    # being AT THE CROSSING (arc (0,0)) as a real asset - saved, undoable,
    # and its red handle visible immediately - rather than materializing
    # only when Auto Mapping first needs it.
    if (view_name == "main"
            and NEAREST_PROPERTY not in assets
            and all(prop in assets for prop in GUIDE_PROPERTIES)
            and _polylines_cross(assets[H_PROPERTY]["points"],
                                 assets[V_PROPERTY]["points"])):
        assets[NEAREST_PROPERTY] = {"arc": [0.0, 0.0]}
        print("[auto_mapping] nearest point created at the crossing "
              "(drag the red handle to restack fold layers).")
    _save_assets(view_name)
    _overlays_changed(view_name)
    # A redrawn axis rebuilds the grid above; with unit focus the mapping
    # re-renders live too.
    _maybe_auto_run()
    return True


def _line_id(line, index):
    """A line's pair identity: its stored id, or its index for legacy assets."""
    try:
        return int(line["id"])
    except (KeyError, TypeError, ValueError):
        return index


def _additional_pairs():
    """The (child, main) additional-line pairs, matched BY IDENTITY and
    returned in DRAWING ORDER.

    Positional zip was measured to re-pair surviving lines after a one-board
    undo desynced the lists - marrying unrelated guides into a fabricated
    120 px deformation. Each line carries an id shared with its partner;
    ids missing on one board (an undone half of a pair) are ORPHANS and are
    ignored with a note, never re-matched. Legacy assets without ids fall
    back to their list position, which reproduces the old pairing exactly
    for in-step boards.

    ORDER MATTERS now: the warp stages COMPOSE, each later line computed
    in the rendering the earlier ones produce. Ids are allocated max+1 at
    creation and a redraw keeps its line's id, so ascending id IS the
    drawing order - stable across deletion, save/load and legacy stamping
    (position order approximates creation order there).
    """
    child_lines = (_assets_for("child").get(ADDITIONAL_PROPERTY) or {}).get("lines") or []
    main_lines = (_assets_for("main").get(ADDITIONAL_PROPERTY) or {}).get("lines") or []

    def by_id(lines):
        table = {}
        for index, line in enumerate(lines):
            table.setdefault(_line_id(line, index), line)
        return table

    child_map = by_id(child_lines)
    main_map = by_id(main_lines)
    pairs = []
    orphans = 0
    for key in sorted(child_map):
        child_line = child_map[key]
        main_line = main_map.get(key)
        if main_line is None:
            orphans += 1
            continue
        if (len(child_line.get("points") or []) >= 2
                and len(main_line.get("points") or []) >= 2):
            pairs.append((child_line, main_line))
    orphans += sum(1 for key in main_map if key not in child_map)
    if orphans:
        print(f"[auto_mapping] {orphans} unpaired additional line(s) ignored "
              "(an undo may have split a pair - remove the stray line with "
              "its x to clean up).")
    return pairs or None


def _mapper_from_assets(additional=True):
    """The current mapper straight from both boards' assets, or (None, why)."""
    specs = {}
    for view in ("child", "main"):
        assets = _assets_for(view)
        for prop in GUIDE_PROPERTIES:
            item = assets.get(prop)
            if not item or len(item.get("points") or []) < 2:
                return None, f"draw the H and V center lines on the {view} board first"
            specs[(view, prop)] = item
        if not _polylines_cross(assets[H_PROPERTY]["points"],
                                assets[V_PROPERTY]["points"]):
            return None, f"the H and V center lines on the {view} board do not cross"
    return build_mapper(specs[("child", H_PROPERTY)], specs[("child", V_PROPERTY)],
                        specs[("main", H_PROPERTY)], specs[("main", V_PROPERTY)],
                        additional_pairs=_additional_pairs() if additional else None)


def _nearest_additional_index(lines, points):
    """The stored line the new stroke REDRAWS, or None if it is a new one.

    Matching is by mean distance of the new stroke's points to each stored
    polyline - the H/V redraw-replaces convention, made index-aware because
    additional lines are many. The threshold follows the STORED line's own
    length (the first version scaled it with the NEW stroke, so one long
    stroke could swallow a deliberately distinct neighbour 180 px away),
    and an ambiguous match - the runner-up nearly as close as the winner -
    APPENDS instead: appending a redundant line is one x-click to undo,
    silently mangling an existing pair is not.
    """
    step = max(1, len(points) // 16)
    probes = points[::step]
    candidates = []
    new_arc = _cumulative_lengths(points)[-1]
    for index, line in enumerate(lines):
        stored = line.get("points") or []
        if len(stored) < 2:
            continue
        mean = sum(_polyline_arc_of(p, stored)[0] for p in probes) / len(probes)
        # Capped absolutely and by the NEW stroke's own scale: a 2000 px
        # shaped line must not swallow an obviously separate 120 px line
        # 280 px away (measured - the shaped line was silently destroyed).
        threshold = max(20.0, min(0.15 * _cumulative_lengths(stored)[-1],
                                  0.5 * new_arc, 100.0))
        if mean <= threshold:
            candidates.append((mean, index))
    if not candidates:
        return None
    candidates.sort()
    if len(candidates) > 1 and candidates[0][0] > 0.5 * candidates[1][0]:
        return None
    return candidates[0][1]


def _third_of_drawn(base, view_name, points, previous_third=None,
                    previous_points=None):
    """Third-space coordinates of a freshly drawn line, one per point.

    On a folded frame the inverse solves are branch-ambiguous; a REDRAW
    seeds each point's solve from the replaced line's stored coordinates at
    the matching arc fraction, so the edit stays on the branch the pair
    already lived on. A redraw in the opposite stroke direction samples the
    seeds reversed, matched by which end of the OLD line the new stroke
    starts nearest to.
    """
    seeds = [None] * len(points)
    if previous_third and len(previous_third) >= 2:
        prev = [tuple(t) for t in previous_third]
        reverse = False
        if previous_points and len(previous_points) >= 2:
            head, tail = previous_points[0], previous_points[-1]
            start = points[0]
            reverse = (math.hypot(start[0] - tail[0], start[1] - tail[1])
                       < math.hypot(start[0] - head[0], start[1] - head[1]))
        if reverse:
            prev = list(reversed(prev))
        prev_cum = _cumulative_lengths(prev)
        cum = _cumulative_lengths(points)
        total = cum[-1] or 1.0
        for index in range(len(points)):
            third_seed = _point_at_arc(prev, prev_cum,
                                       prev_cum[-1] * cum[index] / total)
            seeds[index] = (third_seed if view_name == "child"
                            else base.scale_arcs(*third_seed))
    if view_name == "child":
        return [list(base.coords(p, seeds[i])) for i, p in enumerate(points)]
    return [list(base.unscale_arcs(*base.main_coords(p, seeds[i])))
            for i, p in enumerate(points)]


def _invalidate_additional_thirds(view_name):
    """Drop stored Third coordinates made stale by a guide edit.

    Third space IS the child frame's arc plane: a child-guide edit
    redefines it wholesale, and a main-guide edit re-parameterizes the
    main-board lines' relation to their canvas points. The drawn pink
    points stay put (they are what the user sees); coordinates re-derive
    from them on the next build. Without this the stored third kept
    describing the RETIRED frame - measured: a 40 px guide nudge slid the
    correction 40 px off the line that defines it.
    """
    dropped = 0
    boards = ("child", "main") if view_name == "child" else ("main",)
    for name in boards:
        lines = (_assets_for(name).get(ADDITIONAL_PROPERTY) or {}).get("lines") or []
        here = 0
        for line in lines:
            if line.pop("third", None) is not None:
                here += 1
        if here:
            _save_assets(name)
            if name != view_name:
                # This write lands on the OTHER board: commit its
                # history too, or its live state sits ahead of its own
                # snapshot and a History-panel jump resurrects the
                # stale third - the very staleness being retired here.
                # (The pair-removal branch commits per board for the
                # same reason.)
                try:
                    _animean().ui.history_commit(
                        "Retire additional-line coordinates", name)
                except Exception:
                    pass
        dropped += here
    if dropped:
        print("[auto_mapping] the guide edit re-anchors the additional lines: "
              "their coordinates re-derive from the drawn points on the next run.")


def run_additional_line_tool(view_name, points, width=2.5, commands=None):
    """Install one additional (pink) refinement line. Entry of the tool.

    Drawing NEAR an existing additional line on this board REPLACES that
    line (redraw-to-edit, the H/V convention) and leaves its partner on the
    other board untouched - the pair's Third-space difference is what
    drives the warp. Drawing elsewhere APPENDS a new pair - the LAST stage
    of the composition chain - whose partner is the drawn line's CHORD in
    the RENDERING the standing lines already produce: a child-drawn line
    is pushed through the chain and straightened there, a main-drawn
    line's frame arcs already ARE that rendering and its child anchor
    pulls back through the chain (best-effort inside a fold band, with a
    loud note). The line's own bend takes effect immediately either way.
    NEUTRALITY is judged in the rendering: a straight main-drawn line asks
    for nothing; a straight child-drawn line inside a standing warp band
    asks to FLATTEN that band - that is the composition rule, not a leak.
    What stays banned is re-deriving a STORED third by inverse solves at
    build time (branch-ambiguous on folded frames) - both sides carry
    authoritative third coordinates from the moment they are made.
    Pairs share an id; the partner write is committed to the OTHER board's
    history too, so a one-board undo cannot silently orphan it.
    """
    cleaned = [(float(x), float(y)) for x, y in points or []]
    if len(cleaned) < 2:
        print("[auto_mapping] an additional line needs at least two points.")
        return False
    other = "main" if view_name == "child" else "child"
    base, why = _mapper_from_assets(additional=False)
    if base is None:
        print(f"[auto_mapping] additional line refused: {why}.")
        return False

    item = {"points": cleaned, "width": float(width)}
    if commands:
        item["commands"] = commands
    assets_here = _assets_for(view_name)
    lines_here = assets_here.setdefault(ADDITIONAL_PROPERTY, {"lines": []})["lines"]
    replaced = _nearest_additional_index(lines_here, cleaned)
    if replaced is not None:
        old = lines_here[replaced]
        item["id"] = _line_id(old, replaced)
        item["third"] = _third_of_drawn(base, view_name, cleaned,
                                        old.get("third"), old.get("points"))
        lines_here[replaced] = item
        print(f"[auto_mapping] additional line {replaced + 1} redrawn on the "
              f"{view_name} board; run Auto Mapping to apply the deformation.")
    else:
        lines_other = _assets_for(other).setdefault(
            ADDITIONAL_PROPERTY, {"lines": []})["lines"]
        new_id = 1 + max(
            (_line_id(line, index)
             for index, line in enumerate(lines_here + lines_other)),
            default=-1)
        item["id"] = new_id
        item["third"] = _third_of_drawn(base, view_name, cleaned)
        # The partner is the drawn line's CHORD - its straightened Third
        # form. Per the geodesic argument the chord IS the line's shape on
        # the flattened surface, so the line's own bend relative to it is
        # the deformation, and drawing a curved line takes effect
        # IMMEDIATELY. The first version mirrored the drawn line verbatim
        # (delta == 0), and the tool appeared to do nothing until the
        # partner was edited.
        #
        # THE PARTNER LIVES IN RENDERING SPACE: the chord is taken after
        # pushing a child-drawn line through the STANDING flow field, and
        # a main-drawn line's frame arcs already ARE rendering space (the
        # frame inverse never involves the warp). The synced partner's
        # stored coordinates keep each board's own convention (child
        # stores base Third, main stores rendering), so the child-side
        # chord pulls back through the field's inverse. A partner
        # synthesized in base coordinates instead landed displaced by
        # exactly the standing warp's shift when drawn over a warped
        # region (measured under the old model; the anchoring argument
        # is field-shape independent).
        chain = None
        if lines_here or lines_other:  # the new item is not appended yet
            full, _chain_why = _mapper_from_assets()
            chain = getattr(full, "warp", None) if full is not None else None
        third = [tuple(t) for t in item["third"]]
        staged = ([chain.apply(t) for t in third]
                  if chain is not None and view_name == "child" else third)
        cum = _cumulative_lengths(staged)
        total = cum[-1] or 1.0
        head, tail = staged[0], staged[-1]
        chord_third = [(head[0] + (tail[0] - head[0]) * (cum[i] / total),
                        head[1] + (tail[1] - head[1]) * (cum[i] / total))
                       for i in range(len(staged))]
        if view_name == "child":
            display = [base.main_frame.hv(*base.scale_arcs(*t))
                       for t in chord_third]
            partner_third = chord_third
        else:
            partner_third = ([chain.unapply(t) for t in chord_third]
                             if chain is not None else chord_third)
            if chain is not None:
                # A failed pullback must never become geometry silently:
                # the build derives this stage's delta from the partner's
                # pushed-forward position, so any solver residual here
                # reads as DRAWN deformation downstream.
                worst = max(math.hypot(*(a - b for a, b in
                                         zip(chain.apply(p), t)))
                            for p, t in zip(partner_third, chord_third))
                if worst > 0.5:
                    print(f"[auto_mapping] warning: the new line crosses a "
                          f"fold band of an earlier additional line - its "
                          f"child-side anchor is approximate there "
                          f"({worst:.1f} px off one branch).")
            display = [base.child_frame.hv(*t) for t in partner_third]
        lines_here.append(item)
        lines_other.append({"points": display, "width": float(width),
                            "id": new_id,
                            "third": [list(t) for t in partner_third]})
        _save_assets(other)
        _overlays_changed(other)
        try:
            _animean().ui.history_commit("Additional line pair", other)
        except Exception:
            pass  # older builds without the history binding
        print(f"[auto_mapping] additional line (id {new_id}) set on the "
              f"{view_name} board; its straightened chord landed on the "
              f"{other} board. The line's bend now shapes the mapping - "
              "redraw either side to adjust.")
    _save_assets(view_name)
    _overlays_changed(view_name)
    # Immediate feedback when the fresh line (or any standing one) crosses
    # a fold edge and gets clipped or skipped - a silently degraded line
    # reads as "the tool ignored me".
    check, _why = _mapper_from_assets()
    for note in getattr(check, "additional_notes", ()) if check else ():
        print(f"[auto_mapping] warning: {note}")
    _maybe_auto_run()
    return True


def activate_additional_line_tool(name="additional_line",
                                  property_value=ADDITIONAL_PROPERTY):
    register_hooks()
    _set_draw_color(ADDITIONAL_COLOR)
    print("[auto_mapping] additional line active: draw a refinement line on "
          "either board (needs H/V center lines on both). Redraw near a line "
          "to edit it; its pair on the other board stays, and the difference "
          "bends the mapping.")
    return property_value


def activate_center_line_tool(name="h_center_line", property_value=H_PROPERTY):
    register_hooks()
    _set_draw_color(H_COLOR if property_value == H_PROPERTY else V_COLOR)
    axis = "horizontal" if property_value == H_PROPERTY else "vertical"
    print(f"[auto_mapping] {name} active: draw ONE {axis} center line (property={property_value}).")
    return property_value


def activate_mapping_area_tool(name="mapping_area", property_value=MAPPING_AREA_PROPERTY):
    register_hooks()
    _set_draw_color(AREA_BORDER_COLOR)
    print("[auto_mapping] mapping area active: click inside a closed shape.")
    print("[auto_mapping] area in main view clips the mapped output; area in child view selects the source.")
    return property_value


def run_auto_mapping(name=AUTO_MAPPING2_TOOL, property_value=AUTO_MAPPING2_TOOL):
    global _last_run_handled
    register_hooks()
    if _last_run_handled:
        # the "extra" event hook already performed this click's mapping
        _last_run_handled = False
        return property_value
    _run()
    return property_value


def _signed_area(ring):
    total = 0.0
    for a, b in zip(ring, ring[1:] + ring[:1]):
        total += a[0] * b[1] - b[0] * a[1]
    return 0.5 * total


def _segments_cross(a, b, c, d):
    """Proper crossing of open segments ab and cd (shared endpoints ok)."""
    d1 = (b[0] - a[0]) * (c[1] - a[1]) - (b[1] - a[1]) * (c[0] - a[0])
    d2 = (b[0] - a[0]) * (d[1] - a[1]) - (b[1] - a[1]) * (d[0] - a[0])
    d3 = (d[0] - c[0]) * (a[1] - c[1]) - (d[1] - c[1]) * (a[0] - c[0])
    d4 = (d[0] - c[0]) * (b[1] - c[1]) - (d[1] - c[1]) * (b[0] - c[0])
    return d1 * d2 < 0.0 and d3 * d4 < 0.0


def _rdp_polyline3d(points, tol):
    """Ramer-Douglas-Peucker on an OPEN 3D polyline, iterative.

    The transfer grid drapes at solid-scale density across the whole
    refer rect, so a tiny drawing in a huge frame pinned every iso-line
    at the sample cap - measured 20k-135k redundant points, a 2.2-9.2x
    HTML blowup, while a 0.25 px tolerance keeps under 1% of them. The
    dense pass still runs first (draping must SEE the relief); this
    keeps only what the eye can."""
    if len(points) <= 2:
        return list(points)
    keep = [False] * len(points)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        lo, hi = stack.pop()
        ax, ay, az = points[lo]
        dx = points[hi][0] - ax
        dy = points[hi][1] - ay
        dz = points[hi][2] - az
        norm2 = dx * dx + dy * dy + dz * dz
        worst = -1.0
        pick = None
        for k in range(lo + 1, hi):
            rx = points[k][0] - ax
            ry = points[k][1] - ay
            rz = points[k][2] - az
            if norm2 <= 1e-12:
                d2 = rx * rx + ry * ry + rz * rz
            else:
                t = (rx * dx + ry * dy + rz * dz) / norm2
                t = min(max(t, 0.0), 1.0)
                ex = rx - t * dx
                ey = ry - t * dy
                ez = rz - t * dz
                d2 = ex * ex + ey * ey + ez * ez
            if d2 > worst:
                worst = d2
                pick = k
        if pick is not None and worst > tol * tol:
            keep[pick] = True
            stack.append((lo, pick))
            stack.append((pick, hi))
    return [p for p, kept in zip(points, keep) if kept]


def _rdp_ring(ring, epsilon):
    """Douglas-Peucker on a closed ring: geometry-true simplification.

    A COUNT cap (keep every Nth point) turned curved stretches into long
    chords, and the fill boundary visibly parted from the outline stroke
    drawn over the same path (user report). A geometric tolerance keeps
    every deviation below sub-pixel instead."""
    if len(ring) <= 4:
        return list(ring)

    def simplify(points):
        if len(points) < 3:
            return list(points)
        a, b = points[0], points[-1]
        span = math.hypot(b[0] - a[0], b[1] - a[1])
        worst = -1.0
        index = 0
        for k in range(1, len(points) - 1):
            p = points[k]
            if span < 1e-9:
                d = math.hypot(p[0] - a[0], p[1] - a[1])
            else:
                d = abs((b[0] - a[0]) * (a[1] - p[1])
                        - (a[0] - p[0]) * (b[1] - a[1])) / span
            if d > worst:
                worst = d
                index = k
        if worst <= epsilon:
            return [points[0], points[-1]]
        left = simplify(points[:index + 1])
        right = simplify(points[index:])
        return left[:-1] + right

    # Split at the two farthest-apart vertices so the closed ring gets two
    # open runs (DP needs anchored endpoints).
    far = max(range(len(ring)), key=lambda k: (ring[k][0] - ring[0][0]) ** 2
              + (ring[k][1] - ring[0][1]) ** 2)
    first = simplify(ring[:far + 1])
    second = simplify(ring[far:] + [ring[0]])
    out = first[:-1] + second[:-1]
    return out if len(out) >= 3 else list(ring)


def _triangulate_quality(outer, holes, max_area, constraints=None):
    """(vertices, triangles) via the `triangle` LIBRARY (Shewchuk):
    constrained Delaunay with a quality bound and a max triangle area.

    This is the primary mesher: the boundary is an exact constraint, and
    interior refinement comes from the SAME triangulation - no separate
    subdivision pass, so there are no T-junctions and no hairline seams
    between triangles (the longest-edge bisection stand-in split an edge
    on one side but not always its neighbour's, and the cracks showed).
    Returns None when the library is unavailable or rejects the input
    (e.g. a self-intersecting artist ring) - callers fall back to earcut.
    """
    import pydeps
    tri = pydeps.ensure("triangle")
    np = pydeps.ensure("numpy")
    if tri is None or np is None:
        return None
    rings = [list(outer)] + [list(hole) for hole in holes]
    points = []
    segments = []
    for ring in rings:
        base = len(points)
        count = len(ring)
        points.extend(ring)
        segments.extend([[base + k, base + (k + 1) % count]
                         for k in range(count)])
    # Interior CONSTRAINT polylines (fold creases): the mesh edges align
    # with them, so the z kink at a crease is a clean ridge instead of a
    # sawtooth of triangles straddling it (user report). Open chains.
    for chain in constraints or []:
        if len(chain) < 2:
            continue
        base = len(points)
        points.extend(chain)
        segments.extend([[base + k, base + k + 1]
                         for k in range(len(chain) - 1)])
    payload = {"vertices": np.array(points, dtype=float),
               "segments": np.array(segments, dtype=int)}
    hole_points = [_ring_interior_point(hole) for hole in holes]
    if hole_points:
        payload["holes"] = np.array(hole_points, dtype=float)
    try:
        # q30: a 20-degree minimum angle still admits 1:3 slivers, and
        # their interpolated normals rendered zigzag shading along every
        # edge (user report). 30 degrees keeps triangles near-uniform
        # and is still within Ruppert-termination territory.
        result = tri.triangulate(payload, f"pq30a{max_area:.4f}")
        vertices = [tuple(p) for p in result["vertices"]]
        triangles = [tuple(int(i) for i in t) for t in result["triangles"]]
    except Exception as error:
        print(f"[auto_mapping] triangle library rejected a ring ({error}); "
              "falling back to earcut")
        return None
    if not triangles:
        return None
    return vertices, triangles


def _triangulate_with_earcut(outer, holes):
    """(vertices, triangles) via the mapbox_earcut LIBRARY, or None.

    User directive: prefer real Python libraries over hand-rolled
    algorithms - the embedded runtime is a full python312 with pip, the
    repo bundles version-pinned wheels in pywheels/, and pydeps
    self-installs them offline on first use.
    """
    import pydeps
    earcut = pydeps.ensure("mapbox_earcut")
    np = pydeps.ensure("numpy")
    if earcut is None or np is None:
        return None
    rings = [list(outer)] + [list(hole) for hole in holes]
    flat = [p for ring in rings for p in ring]
    ends = []
    total = 0
    for ring in rings:
        total += len(ring)
        ends.append(total)
    try:
        array = np.array(flat, dtype=np.float64).reshape(-1, 2)
        ring_ends = np.asarray(ends, dtype=np.uint32)
        indices = earcut.triangulate_float64(array, ring_ends)
    except Exception as error:
        print(f"[auto_mapping] earcut failed ({error}); using the "
              "built-in triangulator")
        return None
    triangles = [(int(indices[i]), int(indices[i + 1]), int(indices[i + 2]))
                 for i in range(0, len(indices), 3)]
    if not triangles:
        return None
    return flat, triangles


def _triangulate_polygon(outer, holes):
    """(vertices, triangles) for a polygon with holes.

    WHY TRIANGLES AT ALL (user question): GPUs rasterize triangles only -
    there is no vector-topology rendering in WebGL, and SVG's vector fill
    is flat 2D (no bent surface, no orbiting camera, no lighting).
    Three.js's own ShapeGeometry earcuts too, but only for planar shapes;
    our surface carries a z field, so we triangulate the outline
    faithfully here and subdivide interiors to follow the bend.

    Primary path: the mapbox_earcut library (bundled wheels, offline
    self-install via pydeps). The pure-python ear clipper below stays as
    the last-resort fallback for a runtime where even the bundled
    install fails.
    """
    library = _triangulate_with_earcut(outer, holes)
    if library is not None:
        return library
    outer = list(outer)
    if _signed_area(outer) < 0.0:
        outer.reverse()
    chain = outer
    for hole in sorted(holes,
                       key=lambda h: -max(p[0] for p in h)):
        hole = list(hole)
        if _signed_area(hole) > 0.0:
            hole.reverse()
        m_index = max(range(len(hole)), key=lambda k: hole[k][0])
        hole = hole[m_index:] + hole[:m_index]
        m = hole[0]
        # Bridge to a chain vertex visible from the hole's rightmost
        # point: nearest first, fall back to anything unobstructed.
        order = sorted(range(len(chain)),
                       key=lambda k: (chain[k][0] - m[0]) ** 2
                       + (chain[k][1] - m[1]) ** 2)
        pick = None
        for candidate in order:
            p = chain[candidate]
            blocked = False
            for i in range(len(chain)):
                a = chain[i]
                b = chain[(i + 1) % len(chain)]
                if a is p or b is p:
                    continue
                if _segments_cross(m, p, a, b):
                    blocked = True
                    break
            if not blocked:
                for i in range(1, len(hole)):
                    a = hole[i]
                    b = hole[(i + 1) % len(hole)]
                    if _segments_cross(m, p, a, b):
                        blocked = True
                        break
            if not blocked:
                pick = candidate
                break
        if pick is None:
            pick = order[0]
        chain = (chain[:pick + 1] + hole + [hole[0]]
                 + chain[pick:])

    vertices = list(chain)
    indices = list(range(len(vertices)))
    triangles = []

    def convex(i0, i1, i2):
        a, b, c = vertices[i0], vertices[i1], vertices[i2]
        return ((b[0] - a[0]) * (c[1] - a[1])
                - (b[1] - a[1]) * (c[0] - a[0])) > 1e-12

    def contains_other(i0, i1, i2):
        a, b, c = vertices[i0], vertices[i1], vertices[i2]
        for k in indices:
            if k in (i0, i1, i2):
                continue
            p = vertices[k]
            # The hole bridges DUPLICATE two vertices (the bridge runs
            # there and back); a coincident copy is not an obstruction,
            # but treating it as one starved the clipper of ears and the
            # forced fallback filled every hole solid.
            if p == a or p == b or p == c:
                continue
            d1 = (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0])
            d2 = (c[0] - b[0]) * (p[1] - b[1]) - (c[1] - b[1]) * (p[0] - b[0])
            d3 = (a[0] - c[0]) * (p[1] - c[1]) - (a[1] - c[1]) * (p[0] - c[0])
            if d1 > 1e-9 and d2 > 1e-9 and d3 > 1e-9:
                return True
        return False

    guard = 0
    while len(indices) > 3 and guard < 4 * len(vertices):
        guard += 1
        clipped = False
        n = len(indices)
        for pos in range(n):
            i0 = indices[(pos - 1) % n]
            i1 = indices[pos]
            i2 = indices[(pos + 1) % n]
            if convex(i0, i1, i2) and not contains_other(i0, i1, i2):
                triangles.append((i0, i1, i2))
                indices.pop(pos)
                clipped = True
                break
        if not clipped:
            # numeric stalemate (collinear runs): drop the flattest corner
            n = len(indices)
            flattest = min(range(n), key=lambda pos: abs(
                _signed_area([vertices[indices[(pos - 1) % n]],
                              vertices[indices[pos]],
                              vertices[indices[(pos + 1) % n]]])))
            i0 = indices[(flattest - 1) % n]
            i1 = indices[flattest]
            i2 = indices[(flattest + 1) % n]
            triangles.append((i0, i1, i2))
            indices.pop(flattest)
    if len(indices) == 3:
        triangles.append(tuple(indices))
    return vertices, triangles


def _subdivide_mesh(vertices, triangles, max_edge):
    """Longest-edge bisection until no edge exceeds max_edge; midpoints
    are cached per edge so neighbouring triangles stay watertight."""
    vertices = list(vertices)
    max_sq = max_edge * max_edge
    for _round in range(9):
        midpoints = {}

        def midpoint(i, j):
            key = (i, j) if i < j else (j, i)
            index = midpoints.get(key)
            if index is None:
                a, b = vertices[i], vertices[j]
                vertices.append(((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5))
                index = len(vertices) - 1
                midpoints[key] = index
            return index

        out = []
        changed = False
        for (a, b, c) in triangles:
            lengths = []
            for i, j in ((a, b), (b, c), (c, a)):
                dx = vertices[i][0] - vertices[j][0]
                dy = vertices[i][1] - vertices[j][1]
                lengths.append(dx * dx + dy * dy)
            longest = max(range(3), key=lambda k: lengths[k])
            if lengths[longest] <= max_sq:
                out.append((a, b, c))
                continue
            changed = True
            if longest == 0:
                mid = midpoint(a, b)
                out.append((a, mid, c))
                out.append((mid, b, c))
            elif longest == 1:
                mid = midpoint(b, c)
                out.append((b, mid, a))
                out.append((mid, c, a))
            else:
                mid = midpoint(c, a)
                out.append((c, mid, b))
                out.append((mid, a, b))
        triangles = out
        if not changed:
            break
    return vertices, triangles


def _reconstruct_surface_3d(map_point, child_fills, child_pattern,
                            grid_target=52, child_area=None):
    """Isometric shape-from-template over the Third plane.

    THE MODEL (user specification): the Third Cartesian plane is the
    OBJECT in its undeformed state - filled regions are the solid - and
    MainView is a camera looking at the deformed object in the world
    (camera fixed at (0,-100)). The mapping's JACOBIAN carries the
    reconstruction: an isometrically bent sheet projects each unit
    tangent with foreshortening cos(theta) along the bend direction, so
    the SVD of J = dM/d(u,v) gives, pointwise,
      - the local slope magnitude  |dz/ds| = sqrt(s0^2 - sigma_min^2)/s0
        (s0 = the un-foreshortened scale, taken as the high quantile of
        sigma_max over the solid - a developable sheet always has an
        uncompressed direction),
      - the slope DIRECTION (the singular vector of sigma_min - the
        foreshortened axis is the one tilted away from the camera),
      - the FACE via sign(det J): crossing a fold flips the sheet, and
        with it the z-gradient - the parity is read straight off the
        Jacobian, no crease bookkeeping needed.
    Integrating that gradient field (least-squares Poisson, conjugate
    gradients on the solid mask) yields z(u,v); world x/y are the
    MainView image itself, so looking from the camera reproduces the
    MainView picture exactly (weak-perspective lift).
    Returns dict with vertices/faces/colors/strokes3d/bounds, or None.
    """
    warp = getattr(map_point, "warp", None)

    def image_of_third(u, v):
        z = warp.apply((u, v)) if warp is not None else (u, v)
        return map_point.main_frame.hv(*map_point.scale_arcs(*z))

    # The solid: fills' rings pulled to Third; strokes join the extent so
    # linework outside the fills still gets a relief field to drape on.
    # Containment testing decimates each ring to <=192 points and carries
    # a bbox pre-filter: the raw rings arrive densified (thousands of
    # points), and the un-filtered scan froze the UI for tens of seconds.
    def decimate(ring, cap=192):
        if len(ring) <= cap:
            return ring
        step = len(ring) / cap
        return [ring[int(k * step)] for k in range(cap)]

    rings_third = []
    rings_fine = []
    fill_colors = []
    for fill in child_fills or []:
        color = fill.get("color") or {}
        rgba = (int(color.get("r", 0)), int(color.get("g", 0)),
                int(color.get("b", 0)), int(color.get("a", 255)))
        group = []
        group_fine = []
        source_rings = []
        for source_ring in _path_commands_to_polygons(fill.get("commands")):
            if len(source_ring) >= 2 \
                    and _dist(source_ring[0], source_ring[-1]) <= 1e-9:
                source_ring = source_ring[:-1]  # drop the closing duplicate
            if len(source_ring) >= 3:
                source_rings.append(source_ring)
        # One shared gate bbox per fill, exactly like _emit_fills: outer
        # and holes must take the same cutters or their partitions diverge
        # and a hole ring skips a cut its outer took.
        fill_bbox = None
        if source_rings:
            fill_bbox = (min(p[0] for r in source_rings for p in r),
                         min(p[1] for r in source_rings for p in r),
                         max(p[0] for r in source_rings for p in r),
                         max(p[1] for r in source_rings for p in r))
        for source_ring in source_rings:
            # Respect the child mapping area: the real mapping deletes
            # what falls outside it, and the object must match. SEVERING
            # matches too (_sever_ring): the 2D pipeline is the authority
            # on what exists, and lifting severed ground here built the
            # solid from stalled branch-jumped iterates - geometry piled
            # up at fabricated Third positions the render had deleted.
            for clipped in _clip_rings_to_area([source_ring], child_area):
                for ring in _sever_ring(map_point, clipped,
                                        gate_bbox=fill_bbox):
                    if len(ring) < 3:
                        continue
                    exact = [tuple(map_point.coords(p)) for p in ring]
                    # TWO resolutions with two jobs: containment probes use
                    # a decimated copy (bbox-gated, cheap), but
                    # TRIANGULATION keeps the EXACT ring - the boundary
                    # must be the same geometry the outline strokes drape
                    # along, or the two part with sawtooth gaps (user
                    # report; an RDP pass here chorded curves the strokes
                    # still followed).
                    coarse = decimate(exact)
                    bbox = (min(p[0] for p in coarse),
                            min(p[1] for p in coarse),
                            max(p[0] for p in coarse),
                            max(p[1] for p in coarse))
                    group.append((coarse, bbox))
                    group_fine.append(exact)
        if group:
            rings_third.append(group)
            rings_fine.append(group_fine)
            fill_colors.append(rgba)
    stroke_info = []
    for stroke in child_pattern or []:
        style_color, style_width = _stroke_style(stroke, 1.0)
        entry = {"color": style_color, "width": style_width,
                 "src": stroke.get("src"), "anchors": {}, "islands": [],
                 "polys": []}
        subpaths = _commands_to_subpaths(stroke.get("commands"))
        if subpaths:
            # BEZIER route (want_commands collection): the anchors are the
            # command endpoints, numbered in chain order across the whole
            # stroke - the same ordering edit_tool walks - and keyed by
            # position so they survive the curve-space clip and sever
            # below (de Casteljau cuts preserve the endpoints they keep).
            ordinal = 0
            for cubics in subpaths:
                if not cubics:
                    continue
                entry["anchors"][_anchor_key(cubics[0][0])] = ordinal
                ordinal += 1
                for cub in cubics:
                    entry["anchors"][_anchor_key(cub[3])] = ordinal
                    ordinal += 1
            # Same gates the 2D emitters apply, in their cubic twins: only
            # islands whose lift exists may drape onto the sheet.
            for cubics in subpaths:
                for piece in _clip_cubics(cubics, child_area):
                    entry["islands"].extend(
                        island for island
                        in _sever_cubics_by_child_fold(map_point, piece)
                        if island)
            # The grid-extent polylines flatten from the SEVERED islands,
            # so the node grid sees exactly the ground the drape will use.
            for island in entry["islands"]:
                flat = [island[0][0]]
                for cub in island:
                    _flatten_cubic(cub, flat)
                if len(flat) >= 2:
                    entry["polys"].append(
                        [tuple(map_point.coords(p)) for p in flat])
        else:
            for poly in _stroke_polylines(stroke):
                for piece in _clip_polyline(poly, child_area):
                    # Same severing the 2D emitters apply: only islands
                    # whose lift exists may drape onto the sheet.
                    for island in _sever_source(map_point, piece):
                        if len(island) >= 2:
                            entry["polys"].append(
                                [tuple(map_point.coords(p))
                                 for p in island])
        stroke_info.append(entry)
    stroke_third = [poly for entry in stroke_info for poly in entry["polys"]]
    solid_pts = [p for group in rings_third for ring, _b in group
                 for p in ring]
    if not solid_pts:
        solid_pts = [p for poly in stroke_third for p in poly]
    if not solid_pts:
        return None
    extent_pts = solid_pts + [p for poly in stroke_third for p in poly]
    u0 = min(p[0] for p in extent_pts)
    u1 = max(p[0] for p in extent_pts)
    v0 = min(p[1] for p in extent_pts)
    v1 = max(p[1] for p in extent_pts)
    span = max(u1 - u0, v1 - v0, 1e-6)
    pad = 0.02 * span
    u0 -= pad; u1 += pad; v0 -= pad; v1 += pad
    nx = max(8, int(round(grid_target * (u1 - u0) / span)))
    ny = max(8, int(round(grid_target * (v1 - v0) / span)))
    du = (u1 - u0) / nx
    dv = (v1 - v0) / ny

    def in_group(u, v, group):
        hits = 0
        for ring, (bx0, by0, bx1, by1) in group:
            if bx0 <= u <= bx1 and by0 <= v <= by1 \
                    and _point_in_ring((u, v), ring):
                hits += 1
        return hits % 2 == 1

    def inside_solid(u, v):
        if not rings_third:
            return True
        return any(in_group(u, v, group) for group in rings_third)

    def color_at(u, v):
        # TOP fill wins: _collect_pattern_fills returns bottom-first
        # (paintGL order), and the first hit used to hand overlapping
        # fills the bottom colour.
        for group, rgba in zip(reversed(rings_third),
                               reversed(fill_colors)):
            if in_group(u, v, group):
                return rgba
        return None

    # Node grid: keep nodes whose neighbourhood touches the solid OR a
    # stroke - linework outside the fills needs a relief field too, or it
    # snapped flat to z=0 the moment it left the mask.
    stroke_cells = set()
    for poly in stroke_third:
        cum = _cumulative_lengths(poly)
        count = max(len(poly), int(cum[-1] / max(min(du, dv), 1e-6)) + 2)
        for k in range(count):
            p = _point_at_arc(poly, cum, cum[-1] * k / max(count - 1, 1))
            stroke_cells.add((int((p[0] - u0) / du), int((p[1] - v0) / dv)))

    node_index = {}
    nodes = []
    for j in range(ny + 1):
        for i in range(nx + 1):
            u = u0 + du * i
            v = v0 + dv * j
            near_stroke = any((i + a, j + b) in stroke_cells
                              for a in (-1, 0) for b in (-1, 0))
            if near_stroke or any(
                    inside_solid(u + du * a * 0.5, v + dv * b * 0.5)
                    for a in (-1, 0, 1) for b in (-1, 0, 1)):
                node_index[(i, j)] = len(nodes)
                nodes.append((i, j, u, v))
    if len(nodes) < 4:
        return None

    # Jacobian, SVD-derived slope field and face sign per node. The
    # Quarter-cell probe. Widening to 0.6 cells (to average the hv()
    # POLY_STEP staircase treads) was tried and rolled back: every
    # crease-adjacent node's stencil then straddled the fold and blended
    # both sides' Jacobians - wrong-face nodes at creases rose 2-4x,
    # each feeding a full-amplitude inverted slope into the Poisson RHS,
    # while the waviness metric moved barely 1%.
    h = 0.25 * min(du, dv)
    jac = []
    sigma_max_all = []
    for _i, _j, u, v in nodes:
        px = image_of_third(u + h, v)
        mx = image_of_third(u - h, v)
        py = image_of_third(u, v + h)
        my = image_of_third(u, v - h)
        a = (px[0] - mx[0]) / (2 * h)
        c = (px[1] - mx[1]) / (2 * h)
        b = (py[0] - my[0]) / (2 * h)
        d = (py[1] - my[1]) / (2 * h)
        jac.append((a, b, c, d))
        ee = a * a + c * c
        gg = b * b + d * d
        ff = a * b + c * d
        tr = ee + gg
        det = ee * gg - ff * ff
        disc = max(0.0, tr * tr * 0.25 - det)
        s_max = math.sqrt(max(1e-12, tr * 0.5 + math.sqrt(disc)))
        sigma_max_all.append(s_max)
    # The un-foreshortened scale: the MEDIAN of sigma_max. Most of the
    # sheet lies flat (sigma_max = sigma_min = s0); a high quantile got
    # polluted by the fold band, where the WARP genuinely stretches the
    # material (AM2 is not isometric inside a fold) - reading that
    # stretch as "facing the camera" inflated every slope and tripled
    # the relief. Where sigma exceeds s0 the excess is in-plane stretch,
    # not tilt, and the point is treated as flat (cos = 1).
    scale0 = sorted(sigma_max_all)[len(sigma_max_all) // 2]
    scale0 = max(scale0, 1e-9)

    grads = []
    handed = []
    for (a, b, c, d) in jac:
        ee = a * a + c * c
        gg = b * b + d * d
        ff = a * b + c * d
        tr = ee + gg
        det2 = ee * gg - ff * ff
        disc = max(0.0, tr * tr * 0.25 - det2)
        s_min_sq = max(0.0, tr * 0.5 - math.sqrt(disc))
        s_max_sq = max(1e-12, tr * 0.5 + math.sqrt(disc))
        # TILT is sigma_min falling below the point's UN-TILTED scale:
        #   cos(theta) = sigma_min / min(sigma_max, s0).
        # The denominator choice carries three measured lessons at once:
        # dividing by the global s0 alone misread every isotropic scale
        # wobble as a full-magnitude slope in a rounding-determined
        # direction (hand-jittered guides fabricated relief on a
        # bit-exact identity map), dividing by sigma_max alone misread
        # the fold band's ANISOTROPIC in-plane stretch (sigma_max 2.5,
        # sigma_min 1 - the warp stretches material along the fold-back)
        # as a steep tilt, and min(sigma_max, s0) keeps both flat while
        # a genuine tilt (sigma_max ~ s0, sigma_min below) still reads
        # its full angle. A 2% dead zone absorbs flattening noise.
        cos_t = min(1.0, math.sqrt(s_min_sq) /
                    max(min(math.sqrt(s_max_sq), scale0), 1e-9) / 0.98)
        mag = math.sqrt(max(0.0, 1.0 - cos_t * cos_t))
        # eigenvector of J^T J for the SMALL eigenvalue: foreshortened axis
        lam = s_min_sq
        vx, vy = (ff, lam - ee) if abs(ff) > 1e-12 else \
            ((1.0, 0.0) if ee <= gg else (0.0, 1.0))
        norm = math.hypot(vx, vy) or 1.0
        grads.append((mag * vx / norm, mag * vy / norm))
        handed.append(1.0 if (a * d - b * c) >= 0.0 else -1.0)
    majority = 1.0 if sum(handed) >= 0 else -1.0

    # Orient the (sign-ambiguous) eigenvectors coherently, then flip by
    # face parity: crossing a fold reverses the physical slope.
    oriented = [None] * len(nodes)
    for k, (i, j, _u, _v) in enumerate(nodes):
        best = None
        for (di, dj) in ((-1, 0), (0, -1), (-1, -1), (1, -1)):
            n = node_index.get((i + di, j + dj))
            if n is not None and oriented[n] is not None:
                best = oriented[n]
                break
        g = grads[k]
        if best is not None and g[0] * best[0] + g[1] * best[1] < 0.0:
            g = (-g[0], -g[1])
        oriented[k] = g
    # SIGN RECOVERY ACROSS TANGENCY LINES. Direction coherence alone can
    # only reverse the slope where det J flips, but a crest or trough -
    # the sheet momentarily facing the camera - reverses the physical
    # slope with NO det flip, and integrating the unsigned field turned
    # every wave into a monotone staircase of double the true relief
    # (and broke mirror symmetry on a symmetric squeeze). The two
    # branches are genuinely indistinguishable in the image (the metric
    # sees only |grad z|), so the MINIMAL-RELIEF prior decides: sloped
    # regions are segmented along the low-magnitude valleys (tangency
    # lines), and ADJACENT regions get OPPOSITE signs over a spanning
    # tree - a wave comes back as a wave, a symmetric squeeze as a
    # symmetric bulge, never as a doubled-height staircase. (Per-node
    # annealing was tried first: a chain of nodes must flip together,
    # and single-site moves cannot cross that barrier.)
    mags = [math.hypot(*g) for g in oriented]
    positive = sorted(m for m in mags if m > 1e-6)
    mag_hi = 0.35 * positive[len(positive) // 2] if positive else 0.0
    labels = [-1] * len(nodes)
    region_count = 0
    for seed in range(len(nodes)):
        if labels[seed] != -1 or mags[seed] <= mag_hi:
            continue
        queue = [seed]
        labels[seed] = region_count
        while queue:
            k = queue.pop()
            ki, kj = nodes[k][0], nodes[k][1]
            for (di, dj) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = node_index.get((ki + di, kj + dj))
                if n is not None and labels[n] == -1 and mags[n] > mag_hi:
                    labels[n] = region_count
                    queue.append(n)
        region_count += 1
    region_sign = [1.0] * region_count
    if region_count > 1:
        # Adjacency across valleys: scan rows and columns; consecutive
        # labelled stretches separated only by low-mag nodes are
        # neighbours.
        adjacency = {}
        for axis in (0, 1):
            majors = range(ny + 1) if axis == 0 else range(nx + 1)
            minors = range(nx + 1) if axis == 0 else range(ny + 1)
            for major in majors:
                last_label = -1
                gap = 0
                for minor in minors:
                    key = (minor, major) if axis == 0 else (major, minor)
                    n = node_index.get(key)
                    if n is None:
                        last_label = -1
                        continue
                    lab = labels[n]
                    if lab == -1:
                        gap += 1
                        continue
                    if last_label != -1 and lab != last_label:
                        pair = (min(last_label, lab), max(last_label, lab))
                        adjacency[pair] = adjacency.get(pair, 0) + 1
                    last_label = lab
                    gap = 0
        # Spanning propagation: neighbours alternate (minimal-relief
        # prior; the DEPTH-anchored orientation below overrides it
        # wherever the 2D pipeline has an opinion).
        visited = [False] * region_count
        for root in range(region_count):
            if visited[root]:
                continue
            visited[root] = True
            queue = [root]
            while queue:
                r = queue.pop()
                for (p, q), _votes in adjacency.items():
                    other = q if p == r else (p if q == r else None)
                    if other is not None and not visited[other]:
                        visited[other] = True
                        region_sign[other] = -region_sign[r]
                        queue.append(other)

    # THE RED HANDLE DECIDES NEAR AND FAR. The 2D pipeline stacks its
    # layers by _fold_depth from the nearest-point anchor, and the 3D
    # object must occlude the same way (user report: they disagreed -
    # the sign priors above are geometry-blind). Each node gets the 2D
    # stacking depth of its child point; each sloped region is then
    # oriented so walking UP its slope walks toward SMALLER depth
    # (nearer the camera = larger z).
    node_depth = [None] * len(nodes)
    if getattr(map_point, "depth_curves", None):
        child_frame_local = map_point.child_frame
        for k, (_i, _j, u, v) in enumerate(nodes):
            # Only SOLID nodes carry stacking evidence: the grid extends
            # past the fills to drape strokes, and those overhang nodes
            # (no gradient information, drifting z) once diluted the
            # depth-class means until the ordering check misfired.
            if not inside_solid(u, v):
                continue
            child_pt = child_frame_local.hv(u, v)
            try:
                side = _fold_sign(map_point, child_pt)
                node_depth[k] = _fold_depth(map_point, child_pt, side)
            except Exception:
                node_depth[k] = None
        for region in range(region_count):
            score = 0.0
            votes = 0
            for k in range(len(nodes)):
                if labels[k] != region:
                    continue
                g = oriented[k]
                length = math.hypot(*g)
                if length < 1e-9:
                    continue
                gx = g[0] * region_sign[region] * handed[k] * majority \
                    / length
                gy = g[1] * region_sign[region] * handed[k] * majority \
                    / length
                ki, kj = nodes[k][0], nodes[k][1]
                d_here = node_depth[k]
                if d_here is None:
                    continue

                def first_other_depth(sx, sy):
                    # Walk until the stacking CHANGES: a frame fold's
                    # depth step lives across a narrow crease, and a
                    # fixed two-cell probe often landed short of it.
                    for radius in range(1, 7):
                        n = node_index.get((ki + int(round(sx * radius)),
                                            kj + int(round(sy * radius))))
                        if n is None:
                            return None
                        dn = node_depth[n]
                        if dn is not None and dn != d_here:
                            return dn
                    return None

                da = first_other_depth(gx, gy)
                db = first_other_depth(-gx, -gy)
                if da is None and db is None:
                    continue
                # +grad points toward larger z; depth must SHRINK there.
                if da is not None:
                    score += d_here - da
                    votes += 1
                if db is not None:
                    score += db - d_here
                    votes += 1
            if os.environ.get("ANIMEAN_TO3D_DEBUG"):
                print(f"[to3d-debug] region {region}: votes {votes} "
                      f"score {score:+.1f} sign {region_sign[region]:+.0f}"
                      f"{' -> FLIP' if votes and score < 0.0 else ''}")
            if votes and score < 0.0:
                region_sign[region] = -region_sign[region]
    signs = [region_sign[labels[k]] if labels[k] != -1 else 1.0
             for k in range(len(nodes))]
    # Valley nodes inherit the nearest labelled sign along their row so
    # the near-zero band blends smoothly between the two regions.
    for k in range(len(nodes)):
        if labels[k] != -1:
            continue
        ki, kj = nodes[k][0], nodes[k][1]
        for radius in range(1, 4):
            found = None
            for (di, dj) in ((radius, 0), (-radius, 0),
                             (0, radius), (0, -radius)):
                n = node_index.get((ki + di, kj + dj))
                if n is not None and labels[n] != -1:
                    found = signs[n]
                    break
            if found is not None:
                signs[k] = found
                break
    target = [(oriented[k][0] * signs[k] * handed[k] * majority,
               oriented[k][1] * signs[k] * handed[k] * majority)
              for k in range(len(nodes))]
    # Smooth the slope field between SAME-FACE neighbours before
    # integrating: the per-node SVD carries sampling noise from the
    # polyline guides, and integrating the raw field produced an
    # unnatural depth-direction ripple. Averaging never crosses a fold
    # (handed flips there and the two sides genuinely oppose), so
    # creases keep their kink while flat stretches actually flatten.
    for _pass in range(2):
        smoothed = list(target)
        for k, (i, j, _u, _v) in enumerate(nodes):
            acc_x = 2.0 * target[k][0]
            acc_y = 2.0 * target[k][1]
            weight = 2.0
            for (di, dj) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                n = node_index.get((i + di, j + dj))
                if n is not None and handed[n] == handed[k]:
                    acc_x += target[n][0]
                    acc_y += target[n][1]
                    weight += 1.0
            smoothed[k] = (acc_x / weight, acc_y / weight)
        target = smoothed

    # Least-squares Poisson: minimize |grad z - target|^2 over the mask
    # (pure-python conjugate gradients; the grid is ~2-3k nodes).
    neigh = []
    for i, j, _u, _v in nodes:
        entry = []
        for (di, dj, axis, sign) in ((1, 0, 0, 1.0), (-1, 0, 0, -1.0),
                                     (0, 1, 1, 1.0), (0, -1, 1, -1.0)):
            n = node_index.get((i + di, j + dj))
            if n is not None:
                entry.append((n, axis, sign))
        neigh.append(entry)

    rhs_grad = [0.0] * len(nodes)
    for k, entry in enumerate(neigh):
        acc = 0.0
        for n, axis, sign in entry:
            step = du if axis == 0 else dv
            tk = target[k][axis]
            tn = target[n][axis]
            # NEGATED divergence so that grad z == +target: the earlier
            # sign made z the NEGATIVE integral (harmless while a global
            # flip absorbed it, but the depth votes assume walking up
            # +target walks up z - with the old sign the votes flipped
            # regions exactly backwards).
            acc -= sign * 0.5 * (tk + tn) * step
        rhs_grad[k] = acc

    def solve(anchor_weight, z_ref):
        def apply_A(z):
            out = [0.0] * len(z)
            for k, entry in enumerate(neigh):
                acc = anchor_weight[k] * z[k] if anchor_weight else 0.0
                for n, _axis, _sign in entry:
                    acc += z[k] - z[n]
                out[k] = acc
            return out

        grounded = bool(anchor_weight) and any(anchor_weight)
        rhs = list(rhs_grad)
        if grounded:
            for k in range(len(rhs)):
                rhs[k] += anchor_weight[k] * z_ref[k]
        z = [0.0] * len(nodes)
        r = [rhs[k] - v for k, v in enumerate(apply_A(z))]
        if not grounded:
            mean_r = sum(r) / len(r)
            r = [v - mean_r for v in r]      # Neumann null space
        p = list(r)
        rs_old = sum(v * v for v in r)
        for _ in range(min(400, 4 * len(nodes))):
            if rs_old < 1e-8:
                break
            ap = apply_A(p)
            denom = sum(pv * apv for pv, apv in zip(p, ap)) or 1e-12
            alpha = rs_old / denom
            z = [zv + alpha * pv for zv, pv in zip(z, p)]
            r = [rv - alpha * apv for rv, apv in zip(r, ap)]
            if not grounded:
                mean_r = sum(r) / len(r)
                r = [v - mean_r for v in r]
            rs_new = sum(v * v for v in r)
            p = [rv + (rs_new / rs_old) * pv for rv, pv in zip(r, p)]
            rs_old = rs_new
        return z

    # PASS 1: pure isometric integration - the shape.
    z = solve(None, None)
    # PASS 2, only on disagreement: ground the layers on the 2D stacking.
    # When pass 1 already stacks every depth class in the red-handle
    # order, the pure isometric answer stands untouched. When it does
    # not (integration cannot see the offset across a fold, and on frame
    # folds it drifted into the wrong order - the user's report), a WEAK
    # anchor (lambda 0.2 against a Laplacian diagonal of ~4) pulls each
    # node toward its layer's reference height: 2D occlusion wins over
    # isometric purity, locally the shape stays gradient-driven. The gap
    # unit comes from pass 1's own relief.
    depths_known = sorted({d for d in node_depth if d is not None})

    def stacking_violations(field):
        """Fraction of NEIGHBOURING node pairs with different 2D depths
        whose z-order contradicts the stacking. Class means once passed
        while whole neighbourhoods interleaved - exactly what the eye
        sees at a fold edge - so the check is local, where occlusion is
        actually felt."""
        bad = 0
        total = 0
        for k, entry in enumerate(neigh):
            dk = node_depth[k]
            if dk is None:
                continue
            for n, _axis, _sign in entry:
                if n <= k:
                    continue
                dn = node_depth[n]
                if dn is None or dn == dk:
                    continue
                total += 1
                if (field[k] - field[n]) * (dn - dk) < 0.0:
                    bad += 1
        return ((bad / total) if total else 0.0), total

    ordered = True
    if len(depths_known) >= 2:
        rate, pair_total = stacking_violations(z)
        ordered = pair_total == 0 or rate < 0.15
        if os.environ.get("ANIMEAN_TO3D_DEBUG"):
            print(f"[to3d-debug] pass1 neighbour stacking violations: "
                  f"{100.0 * rate:.1f}% of {pair_total} pairs")
    if len(depths_known) >= 2 and not ordered:
        zs_sorted = sorted(z)
        low = zs_sorted[int(0.05 * (len(z) - 1))]
        high = zs_sorted[int(0.95 * (len(z) - 1))]
        gap = max((high - low) / (depths_known[-1] - depths_known[0] + 1),
                  4.0 * max(du, dv))
        d_mean = sum(depths_known) / len(depths_known)
        z_ref = [0.0] * len(nodes)
        anchor_weight = [0.0] * len(nodes)
        for k in range(len(nodes)):
            if node_depth[k] is not None:
                z_ref[k] = -(node_depth[k] - d_mean) * gap
                anchor_weight[k] = 0.35
        z = solve(anchor_weight, z_ref)
        # (A boundary-band-only anchor was tried and rejected: freeing
        # the class interiors let the integration balloon between the
        # pinned band and the free middle - waviness rose 45%.)
        if os.environ.get("ANIMEAN_TO3D_DEBUG"):
            rate2, pair_total2 = stacking_violations(z)
            print(f"[to3d-debug] grounded pass: violations "
                  f"{100.0 * rate2:.1f}% of {pair_total2} pairs, "
                  f"gap {gap:.1f}")
    z_mean = sum(z) / len(z)
    z = [(v - z_mean) * scale0 for v in z]   # into canvas units
    # Global concavity: the per-region orientation fixes RELATIVE signs;
    # the remaining all-or-nothing flip is settled the same way - deeper
    # 2D stacking must sit farther from the camera (smaller z).
    known = [(zv, node_depth[k]) for k, zv in enumerate(z)
             if node_depth[k] is not None]
    if known:
        za = sum(v for v, _d in known) / len(known)
        da = sum(d for _v, d in known) / len(known)
        covariance = sum((v - za) * (d - da) for v, d in known)
        if covariance > 0.0:
            z = [-v for v in z]

    # Relief lookup off the node grid; mesh vertices and draped strokes
    # both read it. CATMULL-ROM bicubic: the render mesh is finer than
    # the solve grid, and bilinear z left a C1 crease along every grid
    # line - a regular depth-direction ripple across the whole sheet
    # (user report). The solved field is first padded with a 2-ring
    # halo of linearly extrapolated nodes so the 4x4 window is complete
    # everywhere inside the solid: without the halo z_at hard-switched
    # to bilinear along a ring ~1 cell inside every outline, and the
    # two interpolants disagree by 1/8 of the local second difference
    # at the switch - a rim line around each fill baked into the mesh.
    # Halo nodes only support interpolation, never the solve or stats;
    # with linearly extrapolated ends Catmull-Rom degenerates to linear,
    # so the halo cells blend continuously into the old bilinear look.
    zmap = {}
    for key, n in node_index.items():
        zmap[key] = z[n]
    for _ring in range(2):
        fresh = {}
        for (i, j) in list(zmap.keys()):
            for (di, dj) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                spot = (i + di, j + dj)
                if spot in zmap or spot in fresh:
                    continue
                est = 0.0
                cnt = 0
                for (ei, ej) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                    n1 = zmap.get((spot[0] - ei, spot[1] - ej))
                    n2 = zmap.get((spot[0] - 2 * ei, spot[1] - 2 * ej))
                    if n1 is not None and n2 is not None:
                        est += 2.0 * n1 - n2
                        cnt += 1
                if cnt:
                    fresh[spot] = est / cnt
        zmap.update(fresh)
    # Fill the REST of the clamp rectangle by nearest-occupied BFS: the
    # transfer grid runs the whole refer rect, and beyond the halo the
    # empty-cell fallback returned exactly 0.0 - measured 46-70% of grid
    # samples flat at zero with ~95 px cliffs ringing the artwork
    # (review). With the fill, far field reads as a flat continuation
    # of the boundary relief. Mesh vertices and draped strokes never
    # sample outside the solid's own cells (measured), so this is
    # far-field-only support.
    frontier = list(zmap.keys())
    while frontier:
        grown = []
        for (i, j) in frontier:
            zv = zmap[(i, j)]
            for (di, dj) in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                spot = (i + di, j + dj)
                if (-1 <= spot[0] <= nx + 1 and -1 <= spot[1] <= ny + 1
                        and spot not in zmap):
                    zmap[spot] = zv
                    grown.append(spot)
        frontier = grown

    def z_bilinear(u, v):
        fi = min(max((u - u0) / du, 0.0), nx - 1e-6)
        fj = min(max((v - v0) / dv, 0.0), ny - 1e-6)
        i = int(fi)
        j = int(fj)
        acc = 0.0
        wsum = 0.0
        for (ii, jj, wu, wv) in ((i, j, 1 - (fi - i), 1 - (fj - j)),
                                 (i + 1, j, fi - i, 1 - (fj - j)),
                                 (i, j + 1, 1 - (fi - i), fj - j),
                                 (i + 1, j + 1, fi - i, fj - j)):
            n = node_index.get((ii, jj))
            if n is not None:
                w = wu * wv
                acc += w * z[n]
                wsum += w
        return acc / wsum if wsum > 1e-9 else 0.0

    def spline4(p0, p1, p2, p3, t):
        return 0.5 * ((2.0 * p1)
                      + (-p0 + p2) * t
                      + (2.0 * p0 - 5.0 * p1 + 4.0 * p2 - p3) * t * t
                      + (-p0 + 3.0 * p1 - 3.0 * p2 + p3) * t * t * t)

    def z_at(u, v):
        fi = min(max((u - u0) / du, 0.0), nx - 1e-6)
        fj = min(max((v - v0) / dv, 0.0), ny - 1e-6)
        i = int(fi)
        j = int(fj)
        rows = []
        for jj in (j - 1, j, j + 1, j + 2):
            row = []
            for ii in (i - 1, i, i + 1, i + 2):
                zv = zmap.get((ii, jj))
                if zv is None:
                    return z_bilinear(u, v)
                row.append(zv)
            rows.append(row)
        tx = fi - i
        ty = fj - j
        col = [spline4(*row, tx) for row in rows]
        return spline4(col[0], col[1], col[2], col[3], ty)

    # MESH: each fill triangulated FROM ITS OUTLINE (ear clipping with
    # hole bridging, then longest-edge subdivision so the interior
    # follows the bend) - the boundary is the artist's contour, not a
    # grid staircase. Rings nest odd-even by boundary probes; fills stack
    # bottom-to-top with a hair of z separation against z-fighting.
    vertices = []
    colors = []
    faces = []
    vertex_uv = []
    # ONE target edge length everywhere. Boundary rings, crease
    # constraints, stroke draping and the interior area bound all share
    # it: a dense boundary against a coarse interior produced a ring of
    # sliver fans along every edge and crease, and their interpolated
    # normals shaded as zigzags (user report).
    mesh_len = min(du, dv)
    max_edge = 1.4 * mesh_len
    layer_lift = 0.05

    # Fold creases as INTERIOR mesh constraints: without them triangles
    # straddle the crease and its z kink renders as a sawtooth of
    # corners (user report). child_cutters_raw carries every crease -
    # frame folds and warp loci alike - in child space; pull to Third
    # and resample to the shared step.
    crease_step = mesh_len
    crease_chains = []
    if getattr(map_point, "depth_curves", None):
        try:
            _child_cutters(map_point)
        except Exception:
            pass
        for raw in getattr(map_point, "child_cutters_raw", None) or []:
            chain = [tuple(map_point.coords(p)) for p in raw]
            cum = _cumulative_lengths(chain)
            if cum[-1] < 2.0 * crease_step:
                continue
            count = max(2, int(cum[-1] / crease_step))
            crease_chains.append(
                [_point_at_arc(chain, cum, cum[-1] * k / count)
                 for k in range(count + 1)])

    def clip_chain(chain, ring, holes):
        runs = []
        run = []
        for p in chain:
            inside = _point_in_ring(p, ring) \
                and not any(_point_in_ring(p, hole) for hole in holes)
            if inside:
                run.append(p)
            else:
                if len(run) >= 2:
                    runs.append(run)
                run = []
        if len(run) >= 2:
            runs.append(run)
        out = []
        for r in runs:
            # shrink the ends one sample: a constraint endpoint touching
            # the boundary polygon is a degenerate PSLG for the mesher
            trimmed = r[1:-1] if len(r) > 3 else r
            if len(trimmed) >= 2:
                out.append(trimmed)
        return out

    def boundary_probe(ring):
        a, b = ring[0], ring[1 % len(ring)]
        return ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)

    for order, (group_fine, rgba) in enumerate(zip(rings_fine, fill_colors)):
        rings = group_fine
        levels = [_ring_nesting_level(rings, index)
                  for index in range(len(rings))]
        for index, ring in enumerate(rings):
            if levels[index] % 2 != 0:
                continue
            holes = []
            for j, other in enumerate(rings):
                if j != index and levels[j] % 2 == 1 \
                        and _point_in_ring(boundary_probe(other), ring):
                    holes.append(other)
            # Primary: constrained-Delaunay quality mesh (library) - the
            # area bound does the interior refinement, so no separate
            # subdivision and no T-junction seams. The boundary rings are
            # densified to the SAME arc step the strokes drape at, so an
            # outline drawn along the fill edge lands on the mesh
            # boundary point-for-point instead of cutting chords across
            # it. Fallback: earcut on RDP-simplified rings plus bisection
            # (degraded: chords and hairline seams possible).
            def dense_ring(points):
                # Corner-preserving: RDP drops only collinear clutter
                # (0.15 px), then long SEGMENTS get interior points at
                # the shared step. Uniform arc resampling replaced the
                # ring's corners with chords, and strokes drawn along
                # the true outline fell OUTSIDE the mesh (2 px gaps).
                slim = _rdp_ring(points, 0.15)
                out = []
                closed = slim + [slim[0]]
                for a, b in zip(closed, closed[1:]):
                    out.append(a)
                    seg = math.hypot(b[0] - a[0], b[1] - a[1])
                    pieces = int(seg / mesh_len)
                    for k in range(1, pieces):
                        t = k * mesh_len / seg
                        out.append((a[0] + (b[0] - a[0]) * t,
                                    a[1] + (b[1] - a[1]) * t))
                return out

            constraints = []
            for chain in crease_chains:
                constraints.extend(clip_chain(chain, ring, holes))
            # Equilateral area at the shared edge length, so interior
            # triangles come out the same size as the boundary segments
            # instead of 3-6x coarser (the sliver-fan source).
            quality = _triangulate_quality(dense_ring(ring),
                                           [dense_ring(h) for h in holes],
                                           0.433 * mesh_len * mesh_len,
                                           constraints=constraints)
            if quality is not None:
                flat_verts, tris = quality
            else:
                flat_verts, tris = _triangulate_polygon(
                    _rdp_ring(ring, 0.6), [_rdp_ring(h, 0.6) for h in holes])
                flat_verts, tris = _subdivide_mesh(flat_verts, tris, max_edge)
            base = len(vertices)
            for (u, v) in flat_verts:
                q = image_of_third(u, v)
                vertices.append((q[0], q[1], z_at(u, v)
                                 + order * layer_lift))
                colors.append(rgba)
                vertex_uv.append((u, v))
            faces.extend((base + a, base + b, base + c)
                         for a, b, c in tris)

    # Drape strokes ON THE MESH ITSELF: a stroke point's z comes from
    # barycentric interpolation over the triangle that contains it, so a
    # line running along a fill edge lies exactly in the fill's surface
    # (sampling the node-grid field instead left a z-chord gap wherever
    # the boundary edge spanned curvature). Falls back to the grid field
    # off the mesh.
    tri_hash = {}
    hash_cell = max(mesh_len, 1e-6)
    for f_index, (a, b, c) in enumerate(faces):
        us = [vertex_uv[a][0], vertex_uv[b][0], vertex_uv[c][0]]
        vs = [vertex_uv[a][1], vertex_uv[b][1], vertex_uv[c][1]]
        for gx in range(int(min(us) / hash_cell) - 1,
                        int(max(us) / hash_cell) + 2):
            for gy in range(int(min(vs) / hash_cell) - 1,
                            int(max(vs) / hash_cell) + 2):
                tri_hash.setdefault((gx, gy), []).append(f_index)

    def z_on_mesh(u, v):
        for f_index in tri_hash.get((int(u / hash_cell),
                                     int(v / hash_cell)), ()):
            a, b, c = faces[f_index]
            ax, ay = vertex_uv[a]
            bx, by = vertex_uv[b]
            cx, cy = vertex_uv[c]
            det = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
            if abs(det) < 1e-12:
                continue
            w0 = ((by - cy) * (u - cx) + (cx - bx) * (v - cy)) / det
            w1 = ((cy - ay) * (u - cx) + (ax - cx) * (v - cy)) / det
            w2 = 1.0 - w0 - w1
            # Loose tolerance: boundary-riding stroke points sit exactly
            # on mesh edges and must still claim the adjacent face.
            if w0 < -1e-3 or w1 < -1e-3 or w2 < -1e-3:
                continue
            return (w0 * vertices[a][2] + w1 * vertices[b][2]
                    + w2 * vertices[c][2])
        return None

    def world_of_third(u, v):
        q = image_of_third(u, v)
        zz = z_on_mesh(u, v)
        if zz is None:
            zz = z_at(u, v)
        return (q[0], q[1], zz)

    # The lifted warp for _warp_cubic_3d: ONE Newton lift per distinct
    # child point (endpoints recur across sibling leaves and handles), then
    # the pure-forward image + surface lookup, exactly the composition the
    # polyline drape below uses. The cached (u, v) also feeds the ribbons.
    lift_cache = {}

    def lift_of(p):
        key = (p[0], p[1])
        hit = lift_cache.get(key)
        if hit is None:
            u, v = map_point.coords(p)
            hit = world_of_third(u, v) + (u, v)
            lift_cache[key] = hit
        return hit

    def map_point_3d(p):
        return lift_of(p)[:3]

    def ribbon_for(uvs, width):
        # Stroke width made it to 3D at last: the ink lives ON the sheet,
        # so the centerline offsets sideways in THIRD space (the object's
        # own units - deformation foreshortens the ribbon exactly as it
        # foreshortens the artwork) and both edges drape through the same
        # surface lookup as the centerline.
        half = 0.5 * width
        count = len(uvs)
        if count < 2 or half <= 0.0:
            return None
        left = []
        right = []
        normal = (0.0, 0.0)
        for k in range(count):
            a = uvs[max(k - 1, 0)]
            b = uvs[min(k + 1, count - 1)]
            dx, dy = b[0] - a[0], b[1] - a[1]
            length = math.hypot(dx, dy)
            if length > 1e-9:
                normal = (-dy / length, dx / length)
            u, v = uvs[k]
            left.append(world_of_third(u + normal[0] * half,
                                       v + normal[1] * half))
            right.append(world_of_third(u - normal[0] * half,
                                        v - normal[1] * half))
        return [left, right]

    strokes3d = []
    for entry in stroke_info:
        color3 = [entry["color"][0], entry["color"][1], entry["color"][2]]

        def emit(pts, uvs, anchor_marks):
            if len(pts) < 2:
                return
            # Decimation runs BETWEEN anchors (the 2D spline-mode rule:
            # originals are never decimated), and the metadata indices are
            # remapped onto the surviving points.
            kept = _rdp_indices3d(pts, 0.25, [i for i, _o in anchor_marks])
            remap = {old: new for new, old in enumerate(kept)}
            pts_kept = [pts[i] for i in kept]
            uvs_kept = [uvs[i] for i in kept]
            strokes3d.append({
                "points": pts_kept, "color": color3,
                "width": entry["width"], "src": entry["src"],
                "anchors": [[remap[i], o] for i, o in anchor_marks
                            if i in remap],
                "ribbon": ribbon_for(uvs_kept, entry["width"])})

        if entry["islands"]:
            # BEZIER DRAPE: transport each severed island's cubics through
            # the lifted warp (_warp_cubic_3d - endpoint + handle transport
            # with the full 2D probe set), flatten the leaves, and tag the
            # surviving original anchors with their chain ordinals. Leaf
            # (u, v) evaluates a THIRD-space cubic transported through the
            # lift with the plane _directional_image - its probe points are
            # the very ones _warp_cubic_3d just cached, so this costs no
            # extra Newton. A chord interpolation was wrong here: one leaf
            # can span a whole curved cubic when the WARP is tame, and the
            # ribbon then rode the chord instead of the curve.
            def lift_uv(p):
                return lift_of(p)[3:5]

            for island in entry["islands"]:
                leaves = []
                for cub in island:
                    leaves.extend(_warp_cubic_3d(map_point_3d, cub))
                if not leaves:
                    continue
                pts = [leaves[0][0][0]]
                uvs = [lift_uv(leaves[0][1][0])]
                anchor_marks = []
                first = entry["anchors"].get(_anchor_key(leaves[0][1][0]))
                if first is not None:
                    anchor_marks.append((0, first))
                for out, src in leaves:
                    third_cub = (lift_uv(src[0]),
                                 _directional_image(lift_uv, src[0], src[1]),
                                 _directional_image(lift_uv, src[3], src[2]),
                                 lift_uv(src[3]))
                    samples = max(2, min(64, int(math.ceil(
                        _hull_length3(out) / max(0.5, POLY_STEP)))))
                    for k in range(1, samples + 1):
                        t = k / samples
                        pts.append(_cubic_point3(out, t))
                        uvs.append(_cubic_point(third_cub, t))
                    ordinal = entry["anchors"].get(_anchor_key(src[3]))
                    if ordinal is not None:
                        anchor_marks.append((len(pts) - 1, ordinal))
                emit(pts, uvs, anchor_marks)
        else:
            for poly in entry["polys"]:
                dense = poly
                cum = _cumulative_lengths(poly)
                if cum[-1] > 0:
                    # Fine draping step - decoupled from the mesh edge
                    # length, since z now comes off the mesh surface
                    # itself; bounded, because a tiny solid once made
                    # du/dv microscopic and a long stroke exploded.
                    count = max(len(poly),
                                min(int(cum[-1] / (0.5 * mesh_len)) + 2,
                                    4 * len(poly) + 2048))
                    dense = [_point_at_arc(poly, cum,
                                           cum[-1] * k / (count - 1))
                             for k in range(count)]
                emit([world_of_third(u, v) for (u, v) in dense],
                     dense, [])

    # TRANSFER GRID: the refer-rect lattice draped on the sheet (the
    # viewer's Transfer Grid button shows the whole deformed reference
    # frame). The lattice lives in Third space directly - (du, dv) =
    # the normalized levels times the frame's side extents, the same
    # lattice _grid_overlay_items draws in 2D - and each iso-line
    # drapes exactly like a stroke: z off the mesh surface first, the
    # relief field beyond it.
    grid3d = []
    child_frame = map_point.child_frame
    divisions = max(2, int(_GRID.get("divisions", 5)))
    grid_levels = [i / (divisions - 1) * 2.0 - 1.0 for i in range(divisions)]

    def third_of_hat(u_hat, v_hat):
        return (u_hat * (child_frame.h_side[1] if u_hat >= 0.0
                         else child_frame.h_side[0]),
                v_hat * (child_frame.v_side[1] if v_hat >= 0.0
                         else child_frame.v_side[0]))

    for level in grid_levels:
        for iso in ([third_of_hat(level, -1.0), third_of_hat(level, 1.0)],
                    [third_of_hat(-1.0, level), third_of_hat(1.0, level)]):
            cum = _cumulative_lengths(iso)
            if cum[-1] <= 1e-6:
                continue
            count = min(int(cum[-1] / (0.5 * mesh_len)) + 2, 2048)
            pts = []
            for k in range(count):
                u, v = _point_at_arc(iso, cum, cum[-1] * k / (count - 1))
                q = image_of_third(u, v)
                zz = z_on_mesh(u, v)
                if zz is None:
                    zz = z_at(u, v)
                pts.append((q[0], q[1], zz))
            pts = _rdp_polyline3d(pts, 0.25)
            if len(pts) >= 2:
                grid3d.append(pts)

    return {"vertices": vertices, "faces": faces, "colors": colors,
            "strokes": strokes3d,
            "scale0": scale0, "uv": vertex_uv, "grid": grid3d}


def run_to_3d():
    """Menu entry: reconstruct the mapped sheet's 3D shape and open a
    Three.js viewer for it.

    User specification: the Third Cartesian plane is the OBJECT MATRIX in
    its undeformed state (filled regions are the solid), MainView is the
    camera view of the deformed object (a constant camera, the
    (0,-100)-style world placement), and the mapping's JACOBIAN recovers
    the deformed object state - see _reconstruct_surface_3d.
    """
    child = _scene_model("child")
    main = _scene_model("main")
    child_frame = max(child.current_frame(), 0)
    # Same preflight as a real run: guides may still live in layers on a
    # freshly loaded legacy document, and _mapper_from_assets carries the
    # missing-guide AND crossing checks (a guessed origin from
    # non-crossing guides exports unbounded garbage).
    _absorb_legacy_items("child", child, child_frame)
    _absorb_legacy_items("main", main, max(main.current_frame(), 0))
    child_pattern = _collect_pattern_strokes(child, child_frame,
                                             want_commands=False)
    child_fills = _collect_pattern_fills(child, child_frame)
    if not child_pattern and not child_fills:
        print("[auto_mapping] To 3D: child_paint_view has nothing to map.")
        return False
    map_point, width_scale = _mapper_from_assets()
    if map_point is None:
        print(f"[auto_mapping] To 3D: cannot build mapping: {width_scale}")
        return False
    for note in getattr(map_point, "additional_notes", ()):
        print(f"[auto_mapping] warning: {note}")
    # The 2D pipeline's occlusion is the AUTHORITY on near/far: the red
    # nearest-point handle anchors _fold_depth, and the reconstruction
    # orients its slopes so the 3D stacking matches what the 2D render
    # shows (without it, the sign prior guessed and could disagree).
    if _FOLD["split"]:
        ranges = _pattern_arc_ranges(map_point, child_pattern, child_fills)
        if ranges is not None:
            _prepare_fold_context(map_point, ranges[0], ranges[1])

    child_area = (_assets_for("child").get(MAPPING_AREA_PROPERTY)
                  or {}).get("polygons")
    # The drape gets the strokes' REAL Bezier commands (with snapshot
    # addresses for the export metadata); the arc-range sweep above keeps
    # the 4px flattening it always used. _reconstruct_surface_3d falls
    # back to polyline draping for any stroke without commands.
    pattern_bezier = _collect_pattern_strokes(child, child_frame,
                                              want_commands=True,
                                              tag_source=True)
    surface = _reconstruct_surface_3d(map_point, child_fills, pattern_bezier,
                                      child_area=child_area)
    if surface is None or not surface["faces"]:
        print("[auto_mapping] To 3D: no solid (filled) region to "
              "reconstruct - fill the pattern on the child board first.")
        return False
    surface["frame"] = child_frame

    path = os.path.join(tempfile.gettempdir(),
                        f"animean_to3d_{int(time.time())}.html")
    _export_three_html(surface, path)
    zs = [v[2] for v in surface["vertices"]]
    print(f"[auto_mapping] To 3D: reconstructed the deformed sheet - "
          f"{len(surface['vertices'])} vertices / {len(surface['faces'])} "
          f"faces, {len(surface['strokes'])} draped stroke(s), relief "
          f"span {max(zs) - min(zs):.1f} px -> {path}")
    try:
        os.startfile(path)
    except Exception as error:
        print(f"[auto_mapping] To 3D: could not open the browser ({error}); "
              "open the file above manually.")
    return True


def _three_importmap():
    """Import map for the viewer: the vendored three.js runtime as data:
    URLs (offline export - the unpkg page was blank without network), the
    pinned CDN only if the vendor module is missing from this deployment."""
    try:
        import three_vendor
        return {"imports": {
            "three": "data:text/javascript;base64,"
                     + three_vendor.THREE_MODULE_B64,
            "three/addons/controls/OrbitControls.js":
                "data:text/javascript;base64,"
                + three_vendor.ORBIT_CONTROLS_B64,
        }}
    except Exception as error:
        print(f"[auto_mapping] To 3D: vendored three.js unavailable "
              f"({error}); the viewer will need network access.")
        return {"imports": {
            "three": "https://unpkg.com/three@0.160.0/build/"
                     "three.module.min.js",
            "three/addons/":
                "https://unpkg.com/three@0.160.0/examples/jsm/",
        }}


def _export_three_html(surface, path):
    """Self-contained Three.js viewer for the reconstructed sheet: the
    triangulated solid with per-vertex fill colors, draped strokes (width
    ribbons + hairline centerlines), orbit controls, a relief-scale slider,
    and the CONSTANT camera (the user's (0,-100)-style world placement,
    expressed as a fixed matrix over the normalized scene).

    Each stroke also carries the bidirectional-edit SLOT (display-inert for
    now): "src" = [layer index, stroke index] on child frame "frame", and
    "anchors" = [point index, anchor ordinal] pairs marking which polyline
    points are original Bezier anchors. SNAPSHOT addresses only - the scene
    has no persistent stroke/vertex ids yet, so they die with the next edit."""

    def rounded(poly):
        return [[round(x, 2), round(y, 2), round(z, 2)] for x, y, z in poly]

    data = {
        "vertices": rounded(surface["vertices"]),
        "faces": [list(face) for face in surface["faces"]],
        "colors": [[c[0], c[1], c[2]] for c in surface["colors"]],
        "frame": surface.get("frame"),
        "strokes": [{
            "points": rounded(stroke["points"]),
            "color": [stroke["color"][0], stroke["color"][1],
                      stroke["color"][2]],
            "width": round(stroke["width"], 2),
            "src": (list(stroke["src"]) if stroke.get("src") else None),
            "anchors": [list(mark) for mark in stroke["anchors"]],
            "ribbon": ([rounded(side) for side in stroke["ribbon"]]
                       if stroke.get("ribbon") else None),
        } for stroke in surface["strokes"]],
        "grid": [rounded(poly) for poly in surface.get("grid") or []],
    }
    payload = json.dumps(data, separators=(",", ":")).replace("</", "<\\/")
    importmap = json.dumps(_three_importmap()).replace("</", "<\\/")
    html = (_TO3D_TEMPLATE
            .replace("__ANIMEAN_IMPORTMAP__", importmap)
            .replace("__ANIMEAN_DATA__", payload))
    with open(path, "w", encoding="utf-8") as handle:
        handle.write(html)


_TO3D_TEMPLATE = r"""<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<title>AnimeAn To 3D</title>
<style>
  html, body { margin: 0; height: 100%; overflow: hidden; background: #eef0f4; }
  #ui { position: fixed; top: 10px; left: 10px; color: #23262e;
        font: 13px/1.5 system-ui, sans-serif; background: rgba(255,255,255,.85);
        padding: 10px 14px; border-radius: 8px; user-select: none;
        box-shadow: 0 2px 8px rgba(30,34,44,.18); }
  #ui input[type=range] { width: 180px; vertical-align: middle; }
</style>
</head>
<body>
<div id="ui">
  <div><b>AnimeAn To 3D</b> - reconstructed sheet</div>
  <div>Relief scale <input id="relief" type="range" min="0" max="300"
       value="100"></div>
  <div><label><input id="flip" type="checkbox"> flip relief</label>
       <label style="margin-left:12px"><input id="wire" type="checkbox">
       wireframe</label></div>
  <div><button id="homecam" type="button" style="margin-top:4px">
       original camera</button>
       <button id="transfergrid" type="button" style="margin-top:4px">
       Transfer Grid</button></div>
  <div style="opacity:.7">drag: rotate &middot; wheel: zoom &middot;
       right-drag: pan</div>
</div>
<script type="importmap">
__ANIMEAN_IMPORTMAP__
</script>
<script type="module">
import * as THREE from 'three';
import { OrbitControls } from 'three/addons/controls/OrbitControls.js';

const DATA = __ANIMEAN_DATA__;

const scene = new THREE.Scene();
scene.background = new THREE.Color(0xeef0f4);
const renderer = new THREE.WebGLRenderer({ antialias: true });
renderer.setSize(innerWidth, innerHeight);
renderer.setPixelRatio(devicePixelRatio);
document.body.appendChild(renderer.domElement);

// Normalize: canvas x/y (y flipped up), z = relief. The camera looks down
// -z exactly like MainView does, so the initial view IS the MainView
// picture; the constant matrix is the (0,-100)-style world placement.
let minX = 1e9, minY = 1e9, maxX = -1e9, maxY = -1e9;
for (const [x, y] of DATA.vertices) {
  if (x < minX) minX = x; if (x > maxX) maxX = x;
  if (y < minY) minY = y; if (y > maxY) maxY = y;
}
const cx = (minX + maxX) / 2, cy = (minY + maxY) / 2;
const span = Math.max(maxX - minX, maxY - minY, 1);
const S = 2 / span;

const group = new THREE.Group();
scene.add(group);

const positions = new Float32Array(DATA.vertices.length * 3);
const baseZ = new Float32Array(DATA.vertices.length);
const colors = new Float32Array(DATA.vertices.length * 3);
DATA.vertices.forEach(([x, y, z], i) => {
  positions[3 * i] = (x - cx) * S;
  positions[3 * i + 1] = (cy - y) * S;
  positions[3 * i + 2] = z * S;
  baseZ[i] = z * S;
  const c = DATA.colors[i] || [200, 200, 200];
  colors[3 * i] = c[0] / 255;
  colors[3 * i + 1] = c[1] / 255;
  colors[3 * i + 2] = c[2] / 255;
});
const geo = new THREE.BufferGeometry();
geo.setAttribute('position', new THREE.BufferAttribute(positions, 3));
geo.setAttribute('color', new THREE.BufferAttribute(colors, 3));
geo.setIndex(DATA.faces.flat());
geo.computeVertexNormals();
const mat = new THREE.MeshStandardMaterial({
  vertexColors: true, side: THREE.DoubleSide, metalness: 0.0,
  roughness: 0.85, flatShading: false });
const mesh = new THREE.Mesh(geo, mat);
group.add(mesh);

const lineObjs = [];
DATA.strokes.forEach(stroke => {
  const sc = stroke.color || [28, 30, 36];
  const color = new THREE.Color(sc[0] / 255, sc[1] / 255, sc[2] / 255);
  // Width ribbon: the stroke's ink as a strip lying IN the sheet surface
  // (offset + draped Python-side), under a hairline centerline that keeps
  // sub-pixel strokes visible at any zoom.
  if (stroke.ribbon) {
    const [L, R] = stroke.ribbon;
    const rPos = new Float32Array(L.length * 2 * 3);
    const rBase = new Float32Array(L.length * 2);
    for (let i = 0; i < L.length; i++) {
      for (const [k, p] of [[2 * i, L[i]], [2 * i + 1, R[i]]]) {
        rPos[3 * k] = (p[0] - cx) * S;
        rPos[3 * k + 1] = (cy - p[1]) * S;
        rPos[3 * k + 2] = p[2] * S;
        rBase[k] = p[2] * S;
      }
    }
    const rIndex = [];
    for (let i = 0; i + 1 < L.length; i++)
      rIndex.push(2 * i, 2 * i + 1, 2 * i + 2,
                  2 * i + 1, 2 * i + 3, 2 * i + 2);
    const rGeo = new THREE.BufferGeometry();
    rGeo.setAttribute('position', new THREE.BufferAttribute(rPos, 3));
    rGeo.setIndex(rIndex);
    const ribbon = new THREE.Mesh(rGeo, new THREE.MeshBasicMaterial({
        color, side: THREE.DoubleSide }));
    ribbon.userData.baseZ = rBase;
    ribbon.userData.lift = 0.0035;
    lineObjs.push(ribbon);
    group.add(ribbon);
  }
  const pts = stroke.points.map(([x, y, z]) =>
      new THREE.Vector3((x - cx) * S, (cy - y) * S, z * S));
  const g = new THREE.BufferGeometry().setFromPoints(pts);
  const line = new THREE.Line(g, new THREE.LineBasicMaterial({ color }));
  line.userData.baseZ = stroke.points.map(p => p[2] * S);
  line.userData.lift = 0.004;
  // Bidirectional-edit slot (metadata only, no interaction yet): src =
  // [layer index, stroke index] on child frame DATA.frame; anchors =
  // [point index, anchor ordinal] pairs marking the original Bezier
  // anchors among the points. Snapshot addresses - valid only for the
  // document state this file was exported from.
  line.userData.src = stroke.src;
  line.userData.anchors = stroke.anchors;
  lineObjs.push(line);
  group.add(line);
});

// Transfer Grid: the deformed reference frame (refer rect) draped on
// the sheet - hidden until its button is pressed. The grid sits a hair
// BELOW the strokes so linework stays legible over it.
const gridGroup = new THREE.Group();
gridGroup.visible = false;
group.add(gridGroup);
(DATA.grid || []).forEach(poly => {
  const pts = poly.map(([x, y, z]) =>
      new THREE.Vector3((x - cx) * S, (cy - y) * S, z * S));
  const g = new THREE.BufferGeometry().setFromPoints(pts);
  const line = new THREE.Line(g, new THREE.LineBasicMaterial({
      color: 0xff8c00, transparent: true, opacity: 0.85 }));
  line.userData.baseZ = poly.map(p => p[2] * S);
  line.userData.lift = 0.003;
  lineObjs.push(line);
  gridGroup.add(line);
});

function applyRelief(factor) {
  const pos = geo.getAttribute('position');
  for (let i = 0; i < baseZ.length; i++) pos.setZ(i, baseZ[i] * factor);
  pos.needsUpdate = true;
  geo.computeVertexNormals();
  for (const line of lineObjs) {
    const p = line.geometry.getAttribute('position');
    const bz = line.userData.baseZ;
    const lift = line.userData.lift;
    for (let i = 0; i < bz.length; i++)
      p.setZ(i, bz[i] * factor + lift);
    p.needsUpdate = true;
  }
}
let reliefValue = 1.0, flip = false;
const update = () => applyRelief(reliefValue * (flip ? -1 : 1));
document.getElementById('relief').addEventListener('input', e => {
  reliefValue = parseFloat(e.target.value) / 100; update(); });
document.getElementById('flip').addEventListener('change', e => {
  flip = e.target.checked; update(); });
update();   // apply the stroke lift NOW, or lines z-fight until touched
document.getElementById('wire').addEventListener('change', e => {
  mat.wireframe = e.target.checked; });
const gridBtn = document.getElementById('transfergrid');
gridBtn.addEventListener('click', () => {
  gridGroup.visible = !gridGroup.visible;
  gridBtn.style.fontWeight = gridGroup.visible ? 'bold' : 'normal';
});

scene.add(new THREE.AmbientLight(0xffffff, 0.75));
const sun = new THREE.DirectionalLight(0xffffff, 1.1);
sun.position.set(1.2, 1.8, 2.4);
scene.add(sun);
scene.add(new THREE.GridHelper(4, 20, 0xb9bfcc, 0xd4d8e2)
    .translateY(-1.4));

// ORTHOGRAPHIC camera: the reconstruction is a weak-perspective lift, so
// only a parallel projection reproduces MainView exactly - a perspective
// start violated the method's own premise (and the relief slider could
// push the sheet through the near plane).
const aspect0 = innerWidth / innerHeight;
const camera = new THREE.OrthographicCamera(
    -1.35 * aspect0, 1.35 * aspect0, 1.35, -1.35, -50, 50);
// CONSTANT camera matrix: the (0,-100)-style placement - straight back
// along the view axis, looking at the sheet exactly as MainView does
// (world units are normalized; the distance is depth-irrelevant under
// an orthographic projection).
const CAMERA_MATRIX = new THREE.Matrix4().fromArray([
  1, 0, 0, 0,
  0, 1, 0, 0,
  0, 0, 1, 0,
  0, 0, 2.6, 1]);
CAMERA_MATRIX.decompose(camera.position, camera.quaternion, camera.scale);

const controls = new OrbitControls(camera, renderer.domElement);
controls.enableDamping = true;
controls.target.set(0, 0, 0);
// "original camera": restore the constant initial framing (the MainView
// angle) after any amount of orbiting.
const HOME = { position: camera.position.clone(),
               quaternion: camera.quaternion.clone(),
               zoom: camera.zoom,
               target: controls.target.clone() };
document.getElementById('homecam').addEventListener('click', () => {
  camera.position.copy(HOME.position);
  camera.quaternion.copy(HOME.quaternion);
  camera.zoom = HOME.zoom;
  camera.updateProjectionMatrix();
  controls.target.copy(HOME.target);
  controls.update();
});

addEventListener('resize', () => {
  const aspect = innerWidth / innerHeight;
  camera.left = -1.35 * aspect;
  camera.right = 1.35 * aspect;
  camera.updateProjectionMatrix();
  renderer.setSize(innerWidth, innerHeight);
});
(function tick() {
  requestAnimationFrame(tick);
  controls.update();
  renderer.render(scene, camera);
})();
</script>
</body>
</html>
"""


# A View menu on each board. Both are named "view" and differ by HOST, which
# is the point: "show the refer grid" is a question about one board, and the
# answer for the texture is not the answer for the main view. Registered at
# import time because the menus exist from startup, before any tool has been
# armed (register_hooks() only runs on tool activation).
for _host in ("main", "child"):
    python_hooks.register_menu({
        "name": VIEW_MENU_NAME,
        "title": "View",
        "host": _host,
        "items": _view_menu_items(_host),   # callable: re-read on every open
    })
python_hooks.set_hook(_view_menu_action, menu=True)
# The texture board can be locked against accidental edits; these are the
# tools that still work while it is.
python_hooks.register_protected_properties("child", [H_PROPERTY, V_PROPERTY,
                                                     ADDITIONAL_PROPERTY])
# A guide is annotation, not drawing: ghosting it would put a second axis on
# the paper for every frame in the onion lane. The timeline can ask for them
# back (Guide Line), which is why this is a list rather than a rule in C++.
python_hooks.register_onion_guide_properties([
    H_PROPERTY, V_PROPERTY, ADDITIONAL_PROPERTY,
    H_GUIDE_LAYER_PROPERTY, V_GUIDE_LAYER_PROPERTY, NEAREST_LAYER_PROPERTY])
# The layer-panel context menu has the same requirement: right-clicking a
# mapping group must work in a fresh session, before any tool is armed.
python_hooks.register_menu_provider(_layer_menu_items)
# The menu bar and its settings window: registered at import so they exist in
# a fresh session, before any tool has been armed.
python_hooks.register_menu({
    "name": MENU_NAME,
    "title": "Auto Mapping",
    "items": _menu_items,          # callable: re-evaluated on every open
})
python_hooks.register_settings(LINE_SETTINGS_NAME, _line_settings_layout)
# Each automapping layer's Advanced Settings window (right-click -> settings;
# the provider stashes WHICH unit before the window opens).
python_hooks.register_settings(UNIT_SETTINGS_NAME, _unit_settings_layout)
python_hooks.set_hook(_menu_action, menu=True)
python_hooks.set_hook(_line_display_changed, option=True)
python_hooks.set_hook(_unit_setting_changed, option=True)
python_hooks.set_hook(_layer_menu_action, layermenu=True)
# Unit focus follows the MAIN board's current layer: entering an automapping
# layer shows its guides and arms live re-render, leaving hides them.
python_hooks.set_hook(_layer_focus_event, layerchange=True)
# Live re-render on texture-board artwork edits while a unit has focus.
python_hooks.set_hook(_pattern_changed, linefinish=True, erasefinish=True,
                      deletefinish=True, fillfinish=True, movefinish=True)
# The nearest-end anchor (red handle) reacts from startup: its drag events
# arrive whenever the main guides exist, not only while a tool is armed.
python_hooks.set_hook(_nearest_handle_event, handle=True)
# Guides and additional lines are draggable overlays; their drag is reported
# through the same handle events.
python_hooks.set_hook(_guide_drag_event, handle=True)
