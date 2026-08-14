"""ARCHIVED: Auto Mapping 1 ("spine rotation") - removed from the app.

This is a reference snapshot, NOT importable code. It preserves everything
that was specific to the first auto-mapping algorithm when it was retired
and "Auto Mapping 2" (pure per-quadrant Coons interpolation, translation
sweep) became the app's only automapping:

- the `spine_rotation=True` branch of `build_mapper` (kept below in full,
  exactly as it last shipped in pyfile/auto_mapping.py),
- the `_MAPPER_VERSION` switch, the "auto_mapping" extra-tool button, its
  hook and entry point.

WHY IT WAS REMOVED (see old_history/auto_mapping_algorithms.md for the full
differential-geometry analysis): AM1 expressed the V displacement in the
spine's local rotating frame. That makes the Jacobian structurally singular
at the spine's curvature radius (injectivity bounded by rho = 1/kappa_max)
and amplifies position error linearly with off-axis distance - the
"topology chaos" and far-end artifacts users hit were inherent to the
construction, not implementation bugs. AM2's Jacobian is independent of the
off-axis distance and never degenerates; for straight guides both
algorithms coincide exactly.

Everything below depended on shared helpers that still live in
pyfile/auto_mapping.py (_polyline_intersection, _cumulative_lengths,
_point_at_arc, _tangent_at_arc, _chord_sides, _arc_sides, POLY_STEP).
The _run()/_perform_mapping() plumbing also survives, but lost its
`version` parameter in the same change - it now runs Coons unconditionally.
Only the AM1-specific parts are archived here.
"""

# --- constants (as removed) -------------------------------------------------

AUTO_MAPPING_TOOL = "auto_mapping"   # button name, tool id and property value

# Selected the algorithm for _perform_mapping and the refer-rect grid:
# version 1 = this file's spine rotation, version 2 = Coons interpolation.
_MAPPER_VERSION = {"value": 1}


# --- build_mapper, last shipped version with the spine_rotation switch ------
# (the live build_mapper is this function with the `spine_rotation` parameter
# and the rotation branch removed; the Coons return is the surviving path)

def build_mapper(child_h_points, child_v_points, main_h_points, main_v_points, info=None,
                 spine_rotation=True):
    """Build point mapper from child UV frame to main frame.

    Returns (map_point, width_scale) or (None, reason).
    Shared front end (chord-based UV decomposition, endpoint-anchored
    per-side arc scaling) - see the live pyfile/auto_mapping.py docstring.
    """
    import math
    from auto_mapping import (POLY_STEP, _polyline_intersection, _cumulative_lengths,
                              _point_at_arc, _tangent_at_arc, _chord_sides, _arc_sides)

    if min(len(child_h_points), len(child_v_points), len(main_h_points), len(main_v_points)) < 2:
        return None, "a center line has fewer than 2 points"

    child_origin, _, _ = _polyline_intersection(child_h_points, child_v_points)
    eh = ((child_h_points[-1][0] - child_h_points[0][0]) * 0.5,
          (child_h_points[-1][1] - child_h_points[0][1]) * 0.5)
    ev = ((child_v_points[-1][0] - child_v_points[0][0]) * 0.5,
          (child_v_points[-1][1] - child_v_points[0][1]) * 0.5)
    det = eh[0] * ev[1] - eh[1] * ev[0]
    axis_sin = abs(det) / max(1e-9, math.hypot(eh[0], eh[1]) * math.hypot(ev[0], ev[1]))
    if axis_sin < 0.05:  # ~3 degrees
        return None, "child center lines are (nearly) parallel"

    main_h_cum = _cumulative_lengths(main_h_points)
    main_v_cum = _cumulative_lengths(main_v_points)
    if main_h_cum[-1] <= 1e-9 or main_v_cum[-1] <= 1e-9:
        return None, "main center lines are degenerate"
    main_origin, main_h_arc, main_v_arc = _polyline_intersection(main_h_points, main_v_points)
    tangent_window = max(2.0 * POLY_STEP, 0.03 * main_h_cum[-1])
    base_tangent = _tangent_at_arc(main_h_points, main_h_cum, main_h_arc, tangent_window)

    child_h_len = max(2.0 * math.hypot(eh[0], eh[1]), 1e-6)
    child_v_len = max(2.0 * math.hypot(ev[0], ev[1]), 1e-6)
    child_h_neg, child_h_pos = _chord_sides(child_h_points, child_origin, child_h_len)
    child_v_neg, child_v_pos = _chord_sides(child_v_points, child_origin, child_v_len)
    main_h_neg, main_h_pos = _arc_sides(main_h_cum[-1], main_h_arc)
    main_v_neg, main_v_pos = _arc_sides(main_v_cum[-1], main_v_arc)

    def side_scales(c_neg, c_pos, m_neg, m_pos, chord_len):
        floor = 0.01 * chord_len + 1e-9
        s_neg = m_neg / c_neg
        s_pos = m_pos / c_pos
        if c_neg <= floor < c_pos:
            s_neg = s_pos
        elif c_pos <= floor < c_neg:
            s_pos = s_neg
        return s_neg, s_pos

    h_scale_neg, h_scale_pos = side_scales(child_h_neg, child_h_pos,
                                           main_h_neg, main_h_pos, child_h_len)
    v_scale_neg, v_scale_pos = side_scales(child_v_neg, child_v_pos,
                                           main_v_neg, main_v_pos, child_v_len)
    if info is not None:
        info["h_scale_mismatch"] = (max(h_scale_neg, h_scale_pos)
                                    / max(1e-9, min(h_scale_neg, h_scale_pos)))
        info["v_scale_mismatch"] = (max(v_scale_neg, v_scale_pos)
                                    / max(1e-9, min(v_scale_neg, v_scale_pos)))
        main_h_dir = _tangent_at_arc(main_h_points, main_h_cum, main_h_arc, tangent_window)
        main_v_dir = _tangent_at_arc(main_v_points, main_v_cum, main_v_arc, tangent_window)
        main_cross = main_h_dir[0] * main_v_dir[1] - main_h_dir[1] * main_v_dir[0]
        info["mirrored"] = (det > 0.0) != (main_cross > 0.0)

    def side_map(units, scale_neg, scale_pos):
        return units * (scale_pos if units >= 0.0 else scale_neg)

    def map_point(point):
        dx = point[0] - child_origin[0]
        dy = point[1] - child_origin[1]
        u = (dx * ev[1] - dy * ev[0]) / det
        v = (eh[0] * dy - eh[1] * dx) / det
        u_units = u * 0.5 * child_h_len
        v_units = v * 0.5 * child_v_len
        arc_u = main_h_arc + side_map(u_units, h_scale_neg, h_scale_pos)
        on_h = _point_at_arc(main_h_points, main_h_cum, arc_u)
        on_v = _point_at_arc(main_v_points, main_v_cum,
                             main_v_arc + side_map(v_units, v_scale_neg, v_scale_pos))
        # AM1: the H guide is the spine; the V displacement is expressed in
        # the spine's LOCAL frame, rotating with its tangent. A curved spine
        # keeps deforming the pattern all the way to its far ends (and
        # beyond, where the end tangent freezes the rotation) instead of
        # fading into a translated copy. THIS is the branch whose Jacobian
        # degenerates at the curvature radius - the reason AM1 was retired.
        off_x = on_v[0] - main_origin[0]
        off_y = on_v[1] - main_origin[1]
        if not spine_rotation:
            # AM2 path (the survivor): pure per-quadrant Coons interpolation,
            # collapses to H(s) + V(t) - O.
            return (on_h[0] + off_x, on_h[1] + off_y)
        tangent = _tangent_at_arc(main_h_points, main_h_cum, arc_u, tangent_window)
        cos_t = tangent[0] * base_tangent[0] + tangent[1] * base_tangent[1]
        sin_t = tangent[1] * base_tangent[0] - tangent[0] * base_tangent[1]
        return (on_h[0] + off_x * cos_t - off_y * sin_t,
                on_h[1] + off_x * sin_t + off_y * cos_t)

    width_scale = math.sqrt((main_h_cum[-1] / child_h_len) * (main_v_cum[-1] / child_v_len))
    return map_point, width_scale


# --- UI entry points (as removed) --------------------------------------------
# extra_tools.py carried this button entry:
#     {
#         "name": "auto_mapping",
#         "title": "Auto Mapping",
#         "property": auto_mapping.AUTO_MAPPING_TOOL,
#         "handler": "auto_mapping.run_auto_mapping",
#     }
# register_hooks() registered:
#     python_hooks.set_hook(_auto_mapping_button, extra=True, tool=AUTO_MAPPING_TOOL)
# and _perform_mapping(version=1) selected spine_rotation=True, committed
# history under the label "Auto Mapping".


def _auto_mapping_button(cell, stroke, message):
    global _last_run_handled
    _last_run_handled = True
    _run(1)  # noqa: F821 - archived; _run lived in pyfile/auto_mapping.py


def run_auto_mapping(name=AUTO_MAPPING_TOOL, property_value=AUTO_MAPPING_TOOL):
    global _last_run_handled
    register_hooks()  # noqa: F821 - archived
    if _last_run_handled:  # noqa: F821 - archived
        # the "extra" event hook already performed this click's mapping
        _last_run_handled = False
        return property_value
    _run(1)  # noqa: F821 - archived
    return property_value
