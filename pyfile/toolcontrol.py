import json


def _stabilizer(state):
    """The tool panel's one drawing slider: REALTIME ANTI-SHAKE, nothing else.

    It used to be labelled "Smooth" and drove both the stabilizer and the
    curve fit, which are near-orthogonal in effect. Fitting moved to the
    Draw Setting window (draw_settings.py); this slider is the stabilizer,
    and the two are kept showing the same number.
    """
    try:
        import draw_settings

        return int(draw_settings.stabilizer())
    except Exception:
        return int(state.get("smooth", 50))


def _color_controls():
    return [
        {
            "name": "color",
            "type": "button",
            "title": "Black",
            "hook": "color",
            "value": "black",
            "row": 0,
            "column": 0,
            "state": {"color": "black"},
        },
        {
            "name": "color",
            "type": "button",
            "title": "Blue",
            "hook": "color",
            "value": "blue",
            "row": 0,
            "column": 1,
            "state": {"color": "blue"},
        },
        {
            "name": "color",
            "type": "button",
            "title": "Green",
            "hook": "color",
            "value": "green",
            "row": 0,
            "column": 2,
            "state": {"color": "green"},
        },
    ]


def _slider(name, title, hook, minimum, maximum, value, row):
    return {
        "name": name,
        "type": "slider",
        "title": title,
        "hook": hook,
        "min": minimum,
        "max": maximum,
        "value": value,
        "row": row,
        "start_column": 0,
        "end_column": 2,
    }


def options_for_tool(tool, state=None):
    state = state or {}
    tool = str(tool).lower()

    if tool == "fill":
        controls = [
            *_color_controls(),
            {
                "name": "fill_scope",
                "type": "list",
                "title": "Fill Scope",
                "hook": "fill_scope",
                "value": state.get("fill_scope", "current"),
                "row": 1,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "ALL", "value": "all"},
                    {"title": "Current", "value": "current"},
                ],
            },
        ]
    elif tool in ("eraser", "delete_line", "deleteline", "cut_line", "cutline"):
        # Area rubs a radius away, Line deletes the whole stroke it touches,
        # Cut trims one stroke back to where it crosses its neighbours.
        if tool in ("delete_line", "deleteline"):
            eraser_mode = "line"
        elif tool in ("cut_line", "cutline"):
            eraser_mode = "cut"
        else:
            eraser_mode = "area"
        controls = [
            {
                "name": "eraser_mode",
                "type": "list",
                "title": "Eraser Mode",
                "hook": "eraser_mode",
                "value": eraser_mode,
                "row": 0,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "LineMode", "value": "line"},
                    {"title": "AreaMode", "value": "area"},
                    {"title": "CutMode", "value": "cut"},
                ],
            },
        ]
    elif tool == "arrow":
        # The Arrow is the edit tool: clicking a stroke grows draggable
        # handles. Debug shows the stored control points verbatim; Artist
        # shows perceptually-spaced pseudo-handles (see edit_tool.py).
        try:
            import edit_tool
            edit_mode = edit_tool.edit_mode()
        except Exception:
            edit_mode = "artist"
        controls = [
            {
                "name": "edit_mode",
                "type": "list",
                "title": "Edit Mode",
                "hook": "edit_mode",
                "value": edit_mode,
                "row": 0,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "Artist", "value": "artist"},
                    {"title": "Debug", "value": "debug"},
                ],
            },
        ]
    elif tool == "connect":
        # Bridge two snapped vertices (pyfile/connect_tool.py). Auto Snap can
        # be turned off for free-point clicks; the Smooth slider only shows
        # in Smooth mode (0 = pure CurveMode, 100 = the straight chord); Auto
        # Accept skips the on-canvas accept/delete buttons entirely.
        try:
            import connect_tool
            connect_state = connect_tool.options_state()
        except Exception:
            connect_state = {"auto_snap": True, "mode": "poly",
                             "smooth": 50, "auto_accept": False}
        controls = [
            {
                "name": "connect_auto_snap",
                "type": "check",
                "title": "Auto Snap",
                "hook": "connect_auto_snap",
                "value": "on" if connect_state["auto_snap"] else "off",
                "row": 0,
                "start_column": 0,
                "end_column": 2,
            },
            {
                "name": "connect_mode",
                "type": "list",
                "title": "Connect Mode",
                "hook": "connect_mode",
                "value": connect_state["mode"],
                "row": 1,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "PolyMode", "value": "poly"},
                    {"title": "CurveMode", "value": "curve"},
                    {"title": "SmoothMode", "value": "smooth"},
                ],
            },
            {
                "name": "connect_smooth",
                "type": "slider",
                "title": "Smooth",
                "hook": "connect_smooth",
                "min": 0,
                "max": 100,
                "value": int(connect_state["smooth"]),
                "row": 2,
                "start_column": 0,
                "end_column": 2,
                "visible_when": {"name": "connect_mode", "values": ["smooth"]},
            },
            {
                "name": "connect_auto_accept",
                "type": "check",
                "title": "Auto Accept",
                "hook": "connect_auto_accept",
                "value": "on" if connect_state["auto_accept"] else "off",
                "row": 3,
                "start_column": 0,
                "end_column": 2,
            },
        ]
    elif tool == "move":
        controls = []
    else:
        controls = [
            *_color_controls(),
            _slider("smooth", "Stabilizer", "smooth", 0, 100, _stabilizer(state), 1),
            _slider("pen_width", "Width", "pen_width", 1, 50, int(state.get("pen_width", 5)), 2),
        ]

    return {
        "row_spacing": 8,
        "column_spacing": 6,
        "controls": controls,
    }


def options_for_tool_json(tool, state=None):
    return json.dumps(options_for_tool(tool, state), ensure_ascii=False)


def options_for_extra_tool(tool, state=None):
    """Tool options for ExtraTools (script tools). Unknown tools get none."""
    state = state or {}
    tool = str(tool).lower()

    if tool in ("h_center_line", "v_center_line"):
        # Center lines are pen strokes, so they honour the same smoothing and
        # width parameters as the pen tool.
        controls = [
            _slider("smooth", "Stabilizer", "smooth", 0, 100, _stabilizer(state), 0),
            _slider("pen_width", "Width", "pen_width", 1, 50, int(state.get("pen_width", 5)), 1),
        ]
    elif tool == "auto_mapping_2":
        # Curve Mode picks how the mapped strokes' geometry is rebuilt after the
        # (non-linear) warp; RDP is the decimation tolerance for the samples
        # inserted between original points (0.1px units; originals are never
        # decimated). The refer grid moved to each board's View menu.
        # MIGRATED OUT of this panel: Curve Mode now lives in the menu bar
        # (Auto Mapping > Calculation Mode) and every DISPLAY setting - the
        # guide axes, the crease and the lining - in Auto Mapping > Line
        # Display Settings. What stays here is what is neither: the sampling
        # tolerance, the debug grid, and whether the fold is split at all.
        try:
            import auto_mapping
            rdp_tenths = int(round(auto_mapping.rdp_eps() * 10))
            split = "on" if auto_mapping.fold_split_enabled() else "off"
            seal = "on" if auto_mapping.fold_seal_enabled() else "off"
            sampled = auto_mapping.curve_mode() in ("polyline", "spline")
        except Exception:
            rdp_tenths = 3
            split = "on"
            seal = "on"
            sampled = False
        controls = []
        # RDP decimates the samples inserted between original points, so it
        # exists in the SAMPLED modes (polyline/spline) only - the bezier route
        # transports handles instead and has nothing to decimate. The mode is a
        # menu-bar choice, outside this panel, so the control cannot hide itself
        # against a sibling: the panel is simply rebuilt when the mode changes
        # (auto_mapping calls ui.refresh_tool_options) and the slider is only
        # emitted when it has meaning.
        if sampled:
            controls.append(
                _slider("rdp_eps", "RDP (x0.1px)", "rdp_eps",
                        1, 20, rdp_tenths, 0))
        # Rows follow what was actually emitted: leaving the folds on rows 1
        # and 2 would open the panel with an empty gap where the slider is not.
        fold_row = len(controls)
        controls += [
            # Refer Rect used to sit here. It moved onto each board's View
            # menu: it is a display choice about a BOARD, not a property of
            # the mapping tool, and a single shared checkbox could not answer
            # it separately for the texture and the main view.
            # Where the map turns orientation-reversing (a fold past ~135 deg
            # or a U-turn) the pattern is mirrored - that is the BACK of the
            # fold. Split sends it to its own layer; Crease draws the fold
            # line so hiding the back still reads. Both change WHAT is
            # produced, so they stay here; their colours do not.
            {
                "name": "fold_split",
                "type": "check",
                "title": "Front/Back Split",
                "hook": "fold_split",
                "value": split,
                "row": fold_row,
                "start_column": 0,
                "end_column": 2,
            },
            {
                "name": "fold_seal",
                "type": "check",
                "title": "Crease Line",
                "hook": "fold_seal",
                "value": seal,
                "row": fold_row + 1,
                "start_column": 0,
                "end_column": 2,
                "visible_when": {"name": "fold_split", "values": ["on"]},
            },
        ]
    elif tool in ("fukusato_line", "fukusato_cut"):
        # Handle / crease strokes are ordinary drawing: same knobs as the pen.
        controls = [
            _slider("smooth", "Stabilizer", "smooth", 0, 100, _stabilizer(state), 0),
            _slider("pen_width", "Width", "pen_width", 1, 50, int(state.get("pen_width", 5)), 1),
        ]
    elif tool == "fukusato_guide_mapping":
        # Alpha blends the geodesic weight field (cut aware) with the plain
        # euclidean one (paper Sec. 4.2); 0 = the cut fully blocks influence.
        # Beta is the falloff exponent, Grid the mesh resolution, Samples the
        # number of control points taken from each handle curve.
        try:
            import fukusato_mapping
            opts = fukusato_mapping.options()
        except Exception:
            opts = {"alpha": 0.0, "beta": 2.0, "grid": 32, "samples": 16, "variant": "rigid"}
        controls = [
            {
                "name": "fk_variant",
                "type": "list",
                "title": "MLS Variant",
                "hook": "fk_variant",
                "value": opts["variant"],
                "row": 0,
                "start_column": 0,
                "end_column": 2,
                "options": [
                    {"title": "Rigid", "value": "rigid"},
                    {"title": "Similarity", "value": "similarity"},
                ],
            },
            _slider("fk_alpha", "Euclid Blend (%)", "fk_alpha", 0, 100,
                    int(round(opts["alpha"] * 100)), 1),
            _slider("fk_beta", "Falloff (x0.1)", "fk_beta", 1, 40,
                    int(round(opts["beta"] * 10)), 2),
            _slider("fk_grid", "Mesh Grid", "fk_grid", 8, 64, int(opts["grid"]), 3),
            # samples on the LONGEST handle; shorter ones get proportionally
            # fewer so influence follows arc length
            _slider("fk_samples", "Samples (longest)", "fk_samples", 2, 64,
                    int(opts["samples"]), 4),
            {
                # Paper Sec. 4.1 editing mode: releasing an Arrow-tool handle
                # drag re-runs the mapping and replaces the previous output.
                "name": "fk_rerun",
                "type": "check",
                "title": "Rerun On Edit",
                "hook": "fk_rerun",
                "value": "on" if opts.get("rerun", False) else "off",
                "row": 5,
                "start_column": 0,
                "end_column": 2,
            },
        ]
    else:
        controls = []

    return {
        "row_spacing": 8,
        "column_spacing": 6,
        "controls": controls,
    }


def options_for_extra_tool_json(tool, state=None):
    return json.dumps(options_for_extra_tool(tool, state), ensure_ascii=False)
