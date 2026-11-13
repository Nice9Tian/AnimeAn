"""Headless benchmark for single-step guide / additional-line drag cost (issue #46 step 1).

Usage:
    py tests/bench_drag_cost.py --rig A
    py tests/bench_drag_cost.py --rig B
    py tests/bench_drag_cost.py --rig C
    py tests/bench_drag_cost.py --all
    py tests/bench_drag_cost.py --rig B --warm
    py tests/bench_drag_cost.py --rig C --warm
    py tests/bench_drag_cost.py --preview --all
"""
import argparse
import ctypes
import inspect
import json
import math
import os
import subprocess
import sys
import textwrap
import time
import types
import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if os.path.join(ROOT, "pyfile") not in sys.path:
    sys.path.insert(0, os.path.join(ROOT, "pyfile"))

# Headless mock of animean_python (same approach as tests/t_flowfield.py)
_mock_mod = types.ModuleType("animean_python")
_mock_mod.ui = types.SimpleNamespace(
    set_overlay=lambda *a, **k: None,
    history_commit=lambda *a, **k: None,
    refresh=lambda *a, **k: None,
    refresh_tool_options=lambda *a, **k: None,
    set_draw_color=lambda *a, **k: None,
    set_hook_subscriptions=lambda *a, **k: None,
    set_hook_events=lambda *a, **k: None,
)
sys.modules["animean_python"] = _mock_mod

import auto_mapping as am  # noqa: E402


# ---------------------------------------------------------------------------
# Monkeypatch build_mapper and _FlowFieldWarp for internal sub-stage timing
# ---------------------------------------------------------------------------

_SOLVE_TRACE = {}


def _reset_solve_trace():
    _SOLVE_TRACE.clear()
    _SOLVE_TRACE.update({
        "frame_init_perf": 0.0,
        "frame_init_proc": 0.0,
        "lift_thirds_perf": 0.0,
        "lift_thirds_proc": 0.0,
        "warp_total_perf": 0.0,
        "warp_total_proc": 0.0,
        "warp_prepare_perf": 0.0,
        "warp_prepare_proc": 0.0,
        "field_pre_perf": 0.0,
        "field_pre_proc": 0.0,
        "field_post_perf": 0.0,
        "field_post_proc": 0.0,
        "cg_r0_perf": 0.0,
        "cg_r0_proc": 0.0,
        "cg_boost_1_perf": 0.0,
        "cg_boost_1_proc": 0.0,
        "cg_boost_2_perf": 0.0,
        "cg_boost_2_proc": 0.0,
        "cg_boost_3_perf": 0.0,
        "cg_boost_3_proc": 0.0,
        "cg_retreat_perf": 0.0,
        "cg_retreat_proc": 0.0,
        "cg_iters": [],          # list of (round_tag, component, iterations)
        "current_round": "r0",
    })


_PATCHED = False


def _install_monkeypatches():
    """Patch am._FlowFieldWarp and am.build_mapper in memory without touching pyfile/."""
    global _PATCHED
    if _PATCHED:
        return
    _PATCHED = True

    # 1. Patch _FlowFieldWarp._build to time field construction, CG rounds, and CG iterations
    fb_src = textwrap.dedent(inspect.getsource(am._FlowFieldWarp._build))

    target_pcg_call = (
        "        E, steps_used = gridsolve.pcg(\n"
        "            lambda P: apply_A(P, mask), R, x0=x0_solve, precond=jacobi,\n"
        "            tol=cur_tol, max_iter=cur_max_iter, step_tol=cur_step_tol)"
    )
    replacement_pcg_call = (
        "        E, steps_used = gridsolve.pcg(\n"
        "            lambda P: apply_A(P, mask), R, x0=x0_solve, precond=jacobi,\n"
        "            tol=cur_tol, max_iter=cur_max_iter, step_tol=cur_step_tol)\n"
        "        _BENCH_TRACE['cg_iters'].append((_BENCH_TRACE['current_round'], component, steps_used))"
    )
    assert target_pcg_call in fb_src, "Failed to locate gridsolve.pcg call in _FlowFieldWarp._build"
    fb_src = fb_src.replace(target_pcg_call, replacement_pcg_call, 1)

    target_r0_and_boost = (
        "    ux, uy = assemble_and_solve(0)\n"
        "    ux0, uy0 = ux, uy\n"
        "    for _round in range(self.BOOST_ROUNDS):"
    )
    replacement_r0_and_boost = (
        "    _t_pre_perf = time.perf_counter()\n"
        "    _t_pre_proc = time.process_time()\n"
        "    _BENCH_TRACE['field_pre_perf'] += _t_pre_perf - _b_start_perf\n"
        "    _BENCH_TRACE['field_pre_proc'] += _t_pre_proc - _b_start_proc\n"
        "    _BENCH_TRACE['current_round'] = 'r0'\n"
        "    ux, uy = assemble_and_solve(0)\n"
        "    ux0, uy0 = ux, uy\n"
        "    _t_r0_perf = time.perf_counter()\n"
        "    _t_r0_proc = time.process_time()\n"
        "    _BENCH_TRACE['cg_r0_perf'] += _t_r0_perf - _t_pre_perf\n"
        "    _BENCH_TRACE['cg_r0_proc'] += _t_r0_proc - _t_pre_proc\n"
        "    for _round in range(self.BOOST_ROUNDS):\n"
        "        _t_rb_perf = time.perf_counter()\n"
        "        _t_rb_proc = time.process_time()\n"
        "        _BENCH_TRACE['current_round'] = f'boost_{_round + 1}'"
    )
    assert target_r0_and_boost in fb_src, "Failed to locate Round 0 / BOOST_ROUNDS in _build"
    fb_src = fb_src.replace(target_r0_and_boost, replacement_r0_and_boost, 1)

    target_boost_end = (
        "        if worst < 0.02:\n"
        "            break\n"
        "        ux, uy = assemble_and_solve(_round + 1)\n"
        "\n"
        "    def det_of(sol_x, sol_y):"
    )
    replacement_boost_end = (
        "        if worst < 0.02:\n"
        "            _BENCH_TRACE[f'cg_boost_{_round + 1}_perf'] += time.perf_counter() - _t_rb_perf\n"
        "            _BENCH_TRACE[f'cg_boost_{_round + 1}_proc'] += time.process_time() - _t_rb_proc\n"
        "            break\n"
        "        ux, uy = assemble_and_solve(_round + 1)\n"
        "        _BENCH_TRACE[f'cg_boost_{_round + 1}_perf'] += time.perf_counter() - _t_rb_perf\n"
        "        _BENCH_TRACE[f'cg_boost_{_round + 1}_proc'] += time.process_time() - _t_rb_proc\n"
        "    _t_post_perf = time.perf_counter()\n"
        "    _t_post_proc = time.process_time()\n"
        "    _BENCH_TRACE['current_round'] = 'retreat'\n"
        "\n"
        "    def det_of(sol_x, sol_y):"
    )
    assert target_boost_end in fb_src, "Failed to locate boost end in _build"
    fb_src = fb_src.replace(target_boost_end, replacement_boost_end, 1)

    # Also track any retreat assemble_and_solve() separately from field_post
    target_retreat1 = (
        "        if warm is not None:\n"
        "            ux, uy = 0.5 * (ux0 + ux), 0.5 * (uy0 + uy)\n"
        "        else:\n"
        "            ux, uy = assemble_and_solve((\"retreat\", retreat))\n"
        "        det = det_of(ux, uy)\n"
        "        retreat += 1"
    )
    replacement_retreat1 = (
        "        _tr_p0, _tr_c0 = time.perf_counter(), time.process_time()\n"
        "        if warm is not None:\n"
        "            ux, uy = 0.5 * (ux0 + ux), 0.5 * (uy0 + uy)\n"
        "        else:\n"
        "            ux, uy = assemble_and_solve((\"retreat\", retreat))\n"
        "        _BENCH_TRACE['cg_retreat_perf'] += time.perf_counter() - _tr_p0\n"
        "        _BENCH_TRACE['cg_retreat_proc'] += time.process_time() - _tr_c0\n"
        "        det = det_of(ux, uy)\n"
        "        retreat += 1"
    )
    assert target_retreat1 in fb_src, "Failed to locate retreat loop in _build"
    fb_src = fb_src.replace(target_retreat1, replacement_retreat1, 1)

    target_retreat2 = (
        "        for field in fields:\n"
        "            field[\"boost\"] = None\n"
        "        if warm is not None:\n"
        "            ux, uy = ux0, uy0\n"
        "        else:\n"
        "            ux, uy = assemble_and_solve(\"full_retreat\")\n"
        "        det = det_of(ux, uy)"
    )
    replacement_retreat2 = (
        "        for field in fields:\n"
        "            field[\"boost\"] = None\n"
        "        _tr_p0, _tr_c0 = time.perf_counter(), time.process_time()\n"
        "        if warm is not None:\n"
        "            ux, uy = ux0, uy0\n"
        "        else:\n"
        "            ux, uy = assemble_and_solve(\"full_retreat\")\n"
        "        _BENCH_TRACE['cg_retreat_perf'] += time.perf_counter() - _tr_p0\n"
        "        _BENCH_TRACE['cg_retreat_proc'] += time.process_time() - _tr_c0\n"
        "        det = det_of(ux, uy)"
    )
    assert target_retreat2 in fb_src, "Failed to locate full retreat in _build"
    fb_src = fb_src.replace(target_retreat2, replacement_retreat2, 1)

    fb_src = fb_src.replace(
        "def _build(self, frame_window, warm=None):\n",
        "def _build(self, frame_window, warm=None):\n"
        "    _b_start_perf = time.perf_counter()\n"
        "    _b_start_proc = time.process_time()\n",
        1,
    )

    target_grid_end = (
        "        \"_ux_src\": ux_list, \"_uy_src\": uy_list, \"_det_src\": det_list,\n"
        "    }"
    )
    replacement_grid_end = (
        target_grid_end + "\n"
        "    _BENCH_TRACE['field_post_perf'] += (time.perf_counter() - _t_post_perf) - _BENCH_TRACE['cg_retreat_perf']\n"
        "    _BENCH_TRACE['field_post_proc'] += (time.process_time() - _t_post_proc) - _BENCH_TRACE['cg_retreat_proc']"
    )
    assert target_grid_end in fb_src, "Failed to locate self._grid end in _build"
    fb_src = fb_src.replace(target_grid_end, replacement_grid_end, 1)

    ns = dict(am.__dict__)
    ns["_BENCH_TRACE"] = _SOLVE_TRACE
    exec(fb_src, ns)
    patched_build = ns["_build"]

    orig_ffw_init = am._FlowFieldWarp.__init__

    def _patched_ffw_init(self, *args, **kwargs):
        p0, c0 = time.perf_counter(), time.process_time()
        orig_ffw_init(self, *args, **kwargs)
        _SOLVE_TRACE["warp_total_perf"] += time.perf_counter() - p0
        _SOLVE_TRACE["warp_total_proc"] += time.process_time() - c0

    def _wrapped_build(self, frame_window, warm=None):
        p0, c0 = time.perf_counter(), time.process_time()
        patched_build(self, frame_window, warm=warm)
        _SOLVE_TRACE["_build_total_perf"] = time.perf_counter() - p0
        _SOLVE_TRACE["_build_total_proc"] = time.process_time() - c0

    am._FlowFieldWarp.__init__ = _patched_ffw_init
    am._FlowFieldWarp._build = _wrapped_build

    # 2. Patch build_mapper to time _Frame construction and Third coordinate lifting
    bm_src = inspect.getsource(am.build_mapper)
    target_frames = (
        "    frames = warm.setdefault(\"frames\", _FrameCache()) if warm is not None else None\n"
        "    child = _reused_frame(frames, \"child\", child_h_spec, child_v_spec)\n"
        "    main = _reused_frame(frames, \"main\", main_h_spec, main_v_spec)"
    )
    replacement_frames = (
        "    _tf_p0, _tf_c0 = time.perf_counter(), time.process_time()\n"
        "    frames = warm.setdefault(\"frames\", _FrameCache()) if warm is not None else None\n"
        "    child = _reused_frame(frames, \"child\", child_h_spec, child_v_spec)\n"
        "    main = _reused_frame(frames, \"main\", main_h_spec, main_v_spec)\n"
        "    _BENCH_TRACE['frame_init_perf'] += time.perf_counter() - _tf_p0\n"
        "    _BENCH_TRACE['frame_init_proc'] += time.process_time() - _tf_c0"
    )
    assert target_frames in bm_src, "Failed to locate _Frame construction in build_mapper"
    bm_src = bm_src.replace(target_frames, replacement_frames, 1)

    target_lift = (
        "    if additional_pairs or ground_pairs:\n"
        "        thirds = lifted_pairs(additional_pairs or [], \"additional line\")\n"
        "        stationed = [ground_stations(*pair) for pair in ground_pairs or []]\n"
        "        grounds = []\n"
        "        for child_third, main_third, index in lifted_pairs(stationed, \"ground line\"):\n"
        "            item = stationed[index][0]\n"
        "            grounds.append((child_third, child_third if item.get(\"same_third\") else main_third,\n"
        "                            index, bool(item.get(\"closed\"))))\n"
        "        if thirds or grounds:"
    )
    replacement_lift = (
        "    if additional_pairs or ground_pairs:\n"
        "        _tl_p0, _tl_c0 = time.perf_counter(), time.process_time()\n"
        "        thirds = lifted_pairs(additional_pairs or [], \"additional line\")\n"
        "        stationed = [ground_stations(*pair) for pair in ground_pairs or []]\n"
        "        grounds = []\n"
        "        for child_third, main_third, index in lifted_pairs(stationed, \"ground line\"):\n"
        "            item = stationed[index][0]\n"
        "            grounds.append((child_third, child_third if item.get(\"same_third\") else main_third,\n"
        "                            index, bool(item.get(\"closed\"))))\n"
        "        _BENCH_TRACE['lift_thirds_perf'] += time.perf_counter() - _tl_p0\n"
        "        _BENCH_TRACE['lift_thirds_proc'] += time.process_time() - _tl_c0\n"
        "        if thirds or grounds:"
    )
    assert target_lift in bm_src, "Failed to locate lifted_pairs block in build_mapper"
    bm_src = bm_src.replace(target_lift, replacement_lift, 1)

    ns_bm = dict(am.__dict__)
    ns_bm["_BENCH_TRACE"] = _SOLVE_TRACE
    exec(bm_src, ns_bm)
    am.build_mapper = ns_bm["build_mapper"]


# ---------------------------------------------------------------------------
# Synthetic Rigs A, B, C & Synthetic Strokes / Uniform Grids
# ---------------------------------------------------------------------------

def _make_rig(rig_name):
    """Configure am._MAPPING_ASSETS for Rig A, B, or C and return a step mutator."""
    am._MAPPING_ASSETS.clear()
    am._UNIT_META.clear()
    am._ACTIVE_UNIT["id"] = None
    am._REFER_RECT.update(child=True, main=True)
    am._OCCLUSION.update(enabled=True)
    am._GRID.update(divisions=5, extend=0)
    am._FOLD.update(split=True, seal=True)

    if rig_name == "A":
        child_h = [(-300.0, 0.0), (300.0, 0.0)]
        child_v = [(0.0, -200.0), (0.0, 200.0)]
        main_h = [(-300.0, 0.0), (300.0, 0.0)]
        main_v = [(0.0, -200.0), (0.0, 200.0)]
        am._MAPPING_ASSETS["child"] = {
            am.H_PROPERTY: {"points": [list(p) for p in child_h], "width": 3.0},
            am.V_PROPERTY: {"points": [list(p) for p in child_v], "width": 3.0},
        }
        am._MAPPING_ASSETS["main"] = {
            am.H_PROPERTY: {"points": [list(p) for p in main_h], "width": 3.0},
            am.V_PROPERTY: {"points": [list(p) for p in main_v], "width": 3.0},
        }

        def step_drag(step_idx):
            # Translate main H axis by a small step
            pts = am._MAPPING_ASSETS["main"][am.H_PROPERTY]["points"]
            for p in pts:
                p[0] += 0.45
                p[1] += 0.30

        return step_drag

    if rig_name == "D":
        child_h = [[240.0, 360.0], [1040.0, 360.0]]
        child_v = [[640.0, 90.0], [640.0, 630.0]]
        main_v = [[640.0, 90.0], [640.0, 630.0]]
        main_h = [
            [
                240.0 + 800.0 * (k / 60.0) + 170.0 * math.sin((k / 60.0) * math.pi * 3.0 + 0.6),
                360.0 + 90.0 * math.sin((k / 60.0) * math.pi * 3.0),
            ]
            for k in range(61)
        ]
        child_lines = [{
            "id": 1,
            "width": 2.5,
            "points": [[330.0 + 16.0 * k, 230.0] for k in range(41)],
        }]
        main_lines = [{
            "id": 1,
            "width": 2.5,
            "points": [
                [330.0 + 16.0 * k, 210.0 + 30.0 * math.sin(k / 40.0 * math.pi)]
                for k in range(41)
            ],
        }]
        am._MAPPING_ASSETS["child"] = {
            am.H_PROPERTY: {"points": child_h, "width": 3.0},
            am.V_PROPERTY: {"points": child_v, "width": 3.0},
            am.ADDITIONAL_PROPERTY: {"lines": child_lines},
        }
        am._MAPPING_ASSETS["main"] = {
            am.H_PROPERTY: {"points": main_h, "width": 3.0},
            am.V_PROPERTY: {"points": main_v, "width": 3.0},
            am.ADDITIONAL_PROPERTY: {"lines": main_lines},
        }

        def step_drag(step_idx):
            for p in am._MAPPING_ASSETS["main"][am.H_PROPERTY]["points"]:
                p[1] -= 0.6
            for side in ("child", "main"):
                for line in am._MAPPING_ASSETS[side][am.ADDITIONAL_PROPERTY]["lines"]:
                    line.pop("third", None)

        return step_drag

    # Shared curved axes for B and C (24 segments = 25 points each)
    n_seg = 24
    if rig_name == "B":
        child_h = [
            (-300.0 + 600.0 * i / n_seg,
             22.0 * math.sin(math.pi * (-300.0 + 600.0 * i / n_seg) / 300.0))
            for i in range(n_seg + 1)
        ]
    else:
        # Rig C: child H curls at its right tip so mapper.can_fold() is True
        child_h = [
            (-300.0 + 500.0 * i / 20.0,
             22.0 * math.sin(math.pi * (-300.0 + 500.0 * i / 20.0) / 300.0))
            for i in range(21)
        ]
        bx, by = child_h[-1]
        for k in range(1, 7):
            ang = (k / 6.0) * (math.pi * 0.68)
            child_h.append((bx + 45.0 * math.sin(ang),
                            by + 45.0 * (1.0 - math.cos(ang))))

    child_v = [
        (18.0 * math.sin(math.pi * (-200.0 + 400.0 * i / n_seg) / 200.0),
         -200.0 + 400.0 * i / n_seg)
        for i in range(n_seg + 1)
    ]
    main_h = [
        (-310.0 + 620.0 * i / n_seg,
         32.0 * math.sin(0.8 * math.pi * (-310.0 + 620.0 * i / n_seg) / 310.0)
         + 10.0 * ((-310.0 + 620.0 * i / n_seg) / 310.0) ** 2)
        for i in range(n_seg + 1)
    ]
    main_v = [
        (-24.0 * math.sin(0.9 * math.pi * (-210.0 + 420.0 * i / n_seg) / 210.0)
         + 8.0 * ((-210.0 + 420.0 * i / n_seg) / 210.0) ** 2,
         -210.0 + 420.0 * i / n_seg)
        for i in range(n_seg + 1)
    ]

    # 3 non-neutral additional lines (20 segments = 21 points each)
    n_line = 20
    child_lines = [
        {
            "id": 1,
            "width": 2.5,
            "points": [
                [-160.0 + 320.0 * i / n_line,
                 65.0 + 6.0 * math.sin(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        },
        {
            "id": 2,
            "width": 2.5,
            "points": [
                [-150.0 + 300.0 * i / n_line,
                 -70.0 - 5.0 * math.cos(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        },
        {
            "id": 3,
            "width": 2.5,
            "points": [
                [85.0 + 6.0 * math.sin(math.pi * i / n_line),
                 -120.0 + 240.0 * i / n_line]
                for i in range(n_line + 1)
            ],
        },
    ]
    main_lines = [
        {
            "id": 1,
            "width": 2.5,
            "points": [
                [-160.0 + 320.0 * i / n_line,
                 72.0 + 28.0 * (i / n_line - 0.5) + 10.0 * math.sin(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        },
        {
            "id": 2,
            "width": 2.5,
            "points": [
                [-150.0 + 300.0 * i / n_line,
                 -78.0 - 24.0 * (i / n_line - 0.5) - 8.0 * math.cos(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        },
        {
            "id": 3,
            "width": 2.5,
            "points": [
                [92.0 - 22.0 * (i / n_line - 0.5) + 9.0 * math.sin(math.pi * i / n_line),
                 -120.0 + 240.0 * i / n_line]
                for i in range(n_line + 1)
            ],
        },
    ]

    if rig_name == "C":
        # Add 1 ground line (flagged with GROUND_FLAG = "ground": True)
        child_lines.append({
            "id": 4,
            "width": 2.5,
            am.GROUND_FLAG: True,
            "points": [
                [-100.0 + 200.0 * i / n_line,
                 -125.0 + 3.0 * math.sin(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        })
        main_lines.append({
            "id": 4,
            "width": 2.5,
            am.GROUND_FLAG: True,
            "points": [
                [-100.0 + 200.0 * i / n_line,
                 -129.0 + 4.0 * math.sin(math.pi * i / n_line)]
                for i in range(n_line + 1)
            ],
        })

    am._MAPPING_ASSETS["child"] = {
        am.H_PROPERTY: {"points": [list(p) for p in child_h], "width": 3.0},
        am.V_PROPERTY: {"points": [list(p) for p in child_v], "width": 3.0},
        am.ADDITIONAL_PROPERTY: {"lines": child_lines},
    }
    am._MAPPING_ASSETS["main"] = {
        am.H_PROPERTY: {"points": [list(p) for p in main_h], "width": 3.0},
        am.V_PROPERTY: {"points": [list(p) for p in main_v], "width": 3.0},
        am.ADDITIONAL_PROPERTY: {"lines": main_lines},
    }

    def step_drag(step_idx):
        # Translate main additional line #1 by a small step
        line0 = am._MAPPING_ASSETS["main"][am.ADDITIONAL_PROPERTY]["lines"][0]
        for p in line0["points"]:
            p[0] += 0.35
            p[1] += 0.25
        line0.pop("third", None)

    return step_drag


def _clear_step_caches():
    """Clear all auto_mapping caches before each drag step."""
    # 1. Refer-rect grid & occlusion preview caches (_GRID_CACHE, _OCCLUSION_CACHE)
    am._invalidate_grid_cache()
    # 2. Fingerprint-keyed mapper cache (_MAPPER_CACHE)
    am._MAPPER_CACHE["key"] = None
    am._MAPPER_CACHE["mapper"] = None
    # 3. Proxy contour pairing cache (_PROXY_PAIRING)
    am._PROXY_PAIRING["key"] = None
    am._PROXY_PAIRING["value"] = None
    # 4. Cached Third-space polylines on additional/ground lines
    am._invalidate_additional_thirds("child")
    am._invalidate_additional_thirds("main")


def _make_uniform_grids(cx=0.0, cy=0.0):
    """Pre-build 33x33, 65x65, 129x129 uniform sample grids on the texture board."""
    grids = {}
    for n in (33, 65, 129):
        pts = [
            (cx - 240.0 + 480.0 * i / (n - 1), cy - 160.0 + 320.0 * j / (n - 1))
            for i in range(n)
            for j in range(n)
        ]
        grids[n] = pts
    return grids


def _make_synthetic_strokes(num_strokes=200, num_segments=40, cx=0.0, cy=0.0):
    """Pre-build 200 synthetic strokes, each with 40 segments (41 vertices)."""
    polys = []
    strokes = []
    for s in range(num_strokes):
        row, col = divmod(s, 20)
        bx = cx - 210.0 + col * 19.0
        by = cy - 130.0 + row * 26.0
        pts = [
            (bx + k * 1.2, by + 2.0 * math.sin(k * 0.2 + s * 0.1))
            for k in range(num_segments + 1)
        ]
        polys.append(pts)
        strokes.append({
            "polylines": [[{"x": x, "y": y} for x, y in pts]],
            "width": 2.0,
            "color": {"r": 0, "g": 0, "b": 0, "a": 255},
        })
    return polys, strokes


def _percentile(sorted_vals, q):
    if not sorted_vals:
        return 0.0
    if len(sorted_vals) == 1:
        return sorted_vals[0]
    pos = (len(sorted_vals) - 1) * q
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    if lo == hi:
        return sorted_vals[lo]
    return sorted_vals[lo] + (sorted_vals[hi] - sorted_vals[lo]) * (pos - lo)


def _stats_ms(samples_sec):
    s = sorted(v * 1000.0 for v in samples_sec)
    return _percentile(s, 0.50), _percentile(s, 0.90)


def run_rig_benchmark(rig_name, steps=40, warm=False):
    _install_monkeypatches()
    step_drag = _make_rig(rig_name)
    cx, cy = (640.0, 360.0) if rig_name == "D" else (0.0, 0.0)
    grids = _make_uniform_grids(cx, cy)
    polys, strokes = _make_synthetic_strokes(200, 40, cx, cy)

    # Pre-build numpy arrays for 129x129 batch benchmarks
    pts_129 = grids[129]
    px_129 = np.array([p[0] for p in pts_129], dtype=np.float64)
    py_129 = np.array([p[1] for p in pts_129], dtype=np.float64)

    # Ordered list of stage keys
    stage_keys = [
        "1. 求解: build_mapper 总时长",
        "  ├─ _Frame 构造 (child+main)",
        "  ├─ 附加线/地线 Third 坐标抬升",
        "  ├─ _FlowFieldWarp 场构造",
        "  ├─ _FlowFieldWarp CG 求解总计",
        "  │  ├─ CG Round 0 (初始求解)",
        "  │  ├─ CG Boost Round 1",
        "  │  ├─ CG Boost Round 2",
        "  │  └─ CG Boost Round 3",
        "  └─ build_mapper 其余开销",
        "2. 逐点 33x33 (1089点): mapper(p)",
        "2. 逐点 33x33 (1089点): third_of(p)",
        "2. 逐点 33x33 (1089点): main_of_third(t)",
        "2. 逐点 33x33 (1089点): warp.apply(t)",
        "2. 逐点 33x33 (1089点): warp.det_sign(t)",
        "2. 逐点 65x65 (4225点): mapper(p)",
        "2. 逐点 65x65 (4225点): third_of(p)",
        "2. 逐点 65x65 (4225点): main_of_third(t)",
        "2. 逐点 65x65 (4225点): warp.apply(t)",
        "2. 逐点 65x65 (4225点): warp.det_sign(t)",
        "2. 逐点 129x129 (16641点): mapper(p)",
        "2. 逐点 129x129 (16641点): third_of(p)",
        "2. 逐点 129x129 (16641点): main_of_third(t)",
        "2. 逐点 129x129 (16641点): warp.apply(t)",
        "2. 逐点 129x129 (16641点): warp.det_sign(t)",
        "2b. 批量 129x129 (16641点): third_of_batch",
        "2b. 批量 129x129 (16641点): main_of_third_batch",
        "2b. 批量 129x129 (16641点): warp.apply_batch",
        "2b. 批量 129x129 (16641点): warp.det_sign_batch",
        "3. 叠加层: _grid_overlay_items('main')",
        "3. 叠加层: _grid_overlay_items('child')",
        "3. 叠加层: _occlusion_overlay_items()",
        "4. 笔画映射: _adaptive_map_polyline (200×40段)",
        "4. 笔画映射: _emit_spline_mode (200×40段全管线)",
        "Σ 单步小计 A (UI拖动释放: 求解+叠加层)",
        "Σ 单步总时长 B (求解+叠加层+_adaptive_map_polyline)",
        "Σ 单步总时长 C (求解+叠加层+_emit_spline_mode)",
    ]

    perf_records = {k: [] for k in stage_keys}
    proc_records = {k: [] for k in stage_keys}
    cg_iter_records = []  # per kept step: dict with per-round and total iterations
    warm_err_records = []  # per kept step: max grid-node canvas px error vs cold solve
    meta_checks = {}
    warm_state = {} if warm else None

    for step in range(steps):
        step_drag(step)
        _clear_step_caches()
        _reset_solve_trace()

        child_assets = am._assets_for("child")
        main_assets = am._assets_for("main")
        add_pairs = am._additional_pairs()
        gnd_pairs = am._ground_pairs()

        # --- Stage 1: Solve (build_mapper) ---
        p0, c0 = time.perf_counter(), time.process_time()
        mapper, width_scale = am.build_mapper(
            child_assets[am.H_PROPERTY],
            child_assets[am.V_PROPERTY],
            main_assets[am.H_PROPERTY],
            main_assets[am.V_PROPERTY],
            additional_pairs=add_pairs,
            ground_pairs=gnd_pairs,
            warm=warm_state,
        )
        bm_perf = time.perf_counter() - p0
        bm_proc = time.process_time() - c0
        assert mapper is not None, f"Rig {rig_name} mapper failed: {width_scale}"

        if step == 0:
            if rig_name == "A":
                assert mapper.warp is None and not mapper.can_fold()
            elif rig_name == "B":
                assert mapper.warp is not None and len(mapper.warp.pairs) == 3
                assert not mapper.can_fold()
            elif rig_name == "C":
                assert mapper.warp is not None and len(mapper.warp.pairs) == 3
                assert len(mapper.warp.grounds) == 1 and mapper.can_fold()
            elif rig_name == "D":
                assert mapper.warp is not None and len(mapper.warp.pairs) == 1
            meta_checks = {
                "can_fold": bool(mapper.can_fold()),
                "warp_pairs": len(mapper.warp.pairs) if mapper.warp else 0,
                "warp_grounds": len(mapper.warp.grounds) if mapper.warp else 0,
                "grid_size": (
                    f"{mapper.warp._grid['nx']}x{mapper.warp._grid['ny']}"
                    if (mapper.warp and mapper.warp._grid) else "None"
                ),
            }

        # Populate _MAPPER_CACHE with the mapper just built in Stage 1 so Stage 3
        # overlays measure their own computation rather than rebuilding the mapper.
        am._MAPPER_CACHE["key"] = am._mapper_fingerprint(
            child_assets, main_assets, add_pairs, gnd_pairs
        )
        am._MAPPER_CACHE["mapper"] = mapper

        # Extract Stage 1 sub-timings
        frame_perf = _SOLVE_TRACE["frame_init_perf"]
        frame_proc = _SOLVE_TRACE["frame_init_proc"]
        lift_perf = _SOLVE_TRACE["lift_thirds_perf"]
        lift_proc = _SOLVE_TRACE["lift_thirds_proc"]

        cg_r0_perf = _SOLVE_TRACE["cg_r0_perf"]
        cg_r0_proc = _SOLVE_TRACE["cg_r0_proc"]
        cg_b1_perf = _SOLVE_TRACE["cg_boost_1_perf"]
        cg_b1_proc = _SOLVE_TRACE["cg_boost_1_proc"]
        cg_b2_perf = _SOLVE_TRACE["cg_boost_2_perf"]
        cg_b2_proc = _SOLVE_TRACE["cg_boost_2_proc"]
        cg_b3_perf = _SOLVE_TRACE["cg_boost_3_perf"]
        cg_b3_proc = _SOLVE_TRACE["cg_boost_3_proc"]
        cg_ret_perf = _SOLVE_TRACE["cg_retreat_perf"]
        cg_ret_proc = _SOLVE_TRACE["cg_retreat_proc"]

        cg_total_perf = cg_r0_perf + cg_b1_perf + cg_b2_perf + cg_b3_perf + cg_ret_perf
        cg_total_proc = cg_r0_proc + cg_b1_proc + cg_b2_proc + cg_b3_proc + cg_ret_proc

        warp_total_perf = _SOLVE_TRACE["warp_total_perf"]
        warp_total_proc = _SOLVE_TRACE["warp_total_proc"]
        # Field construction = _FlowFieldWarp total minus CG solve rounds
        field_perf = max(0.0, warp_total_perf - cg_total_perf)
        field_proc = max(0.0, warp_total_proc - cg_total_proc)

        bm_other_perf = max(0.0, bm_perf - frame_perf - lift_perf - warp_total_perf)
        bm_other_proc = max(0.0, bm_proc - frame_proc - lift_proc - warp_total_proc)

        # --- Stage 2: Pointwise Evaluation on 33x33, 65x65, 129x129 ---
        pw_perf = {}
        pw_proc = {}
        third_of = mapper.third_of
        main_of_third = mapper.main_of_third
        warp = mapper.warp
        warp_apply = warp.apply if warp is not None else None
        warp_det_sign = warp.det_sign if warp is not None else None

        for n, pts in grids.items():
            p0, c0 = time.perf_counter(), time.process_time()
            for pt in pts:
                mapper(pt)
            pw_perf[(n, "mapper")] = time.perf_counter() - p0
            pw_proc[(n, "mapper")] = time.process_time() - c0

            p0, c0 = time.perf_counter(), time.process_time()
            for pt in pts:
                third_of(pt)
            pw_perf[(n, "third_of")] = time.perf_counter() - p0
            pw_proc[(n, "third_of")] = time.process_time() - c0

            p0, c0 = time.perf_counter(), time.process_time()
            for pt in pts:
                main_of_third(pt)
            pw_perf[(n, "main_of_third")] = time.perf_counter() - p0
            pw_proc[(n, "main_of_third")] = time.process_time() - c0

            if warp_apply is not None:
                p0, c0 = time.perf_counter(), time.process_time()
                for pt in pts:
                    warp_apply(pt)
                pw_perf[(n, "warp.apply")] = time.perf_counter() - p0
                pw_proc[(n, "warp.apply")] = time.process_time() - c0

                p0, c0 = time.perf_counter(), time.process_time()
                for pt in pts:
                    warp_det_sign(pt)
                pw_perf[(n, "warp.det_sign")] = time.perf_counter() - p0
                pw_proc[(n, "warp.det_sign")] = time.process_time() - c0
            else:
                pw_perf[(n, "warp.apply")] = 0.0
                pw_proc[(n, "warp.apply")] = 0.0
                pw_perf[(n, "warp.det_sign")] = 0.0
                pw_proc[(n, "warp.det_sign")] = 0.0

        # --- Stage 2b: Batch Evaluation on 129x129 ---
        p0, c0 = time.perf_counter(), time.process_time()
        mapper.third_of_batch(px_129, py_129)
        b_third_perf = time.perf_counter() - p0
        b_third_proc = time.process_time() - c0

        p0, c0 = time.perf_counter(), time.process_time()
        mapper.main_of_third_batch(px_129, py_129)
        b_main_perf = time.perf_counter() - p0
        b_main_proc = time.process_time() - c0

        if warp is not None:
            p0, c0 = time.perf_counter(), time.process_time()
            warp.apply_batch(px_129, py_129)
            b_wapp_perf = time.perf_counter() - p0
            b_wapp_proc = time.process_time() - c0

            p0, c0 = time.perf_counter(), time.process_time()
            warp.det_sign_batch(px_129, py_129)
            b_wdet_perf = time.perf_counter() - p0
            b_wdet_proc = time.process_time() - c0
        else:
            b_wapp_perf = b_wapp_proc = 0.0
            b_wdet_perf = b_wdet_proc = 0.0

        # --- Stage 3: Overlays ---
        p0, c0 = time.perf_counter(), time.process_time()
        grid_main_items = am._grid_overlay_items("main")
        gm_perf = time.perf_counter() - p0
        gm_proc = time.process_time() - c0

        p0, c0 = time.perf_counter(), time.process_time()
        grid_child_items = am._grid_overlay_items("child")
        gc_perf = time.perf_counter() - p0
        gc_proc = time.process_time() - c0

        p0, c0 = time.perf_counter(), time.process_time()
        occ_items = am._occlusion_overlay_items()
        occ_perf = time.perf_counter() - p0
        occ_proc = time.process_time() - c0
        assert grid_main_items and grid_child_items

        # --- Stage 4: Stroke Mapping (200 strokes x 40 segments) ---
        p0, c0 = time.perf_counter(), time.process_time()
        for poly in polys:
            am._adaptive_map_polyline(mapper, poly)
        sm_poly_perf = time.perf_counter() - p0
        sm_poly_proc = time.process_time() - c0

        out = am._MappedOutput(None, None, 0)
        p0, c0 = time.perf_counter(), time.process_time()
        for st in strokes:
            am._emit_spline_mode(None, out, st, mapper, None, None, (0, 0, 0, 255), 2.0)
        sm_spline_perf = time.perf_counter() - p0
        sm_spline_proc = time.process_time() - c0

        # Discard step 0 (cold cache)
        if step == 0:
            continue

        # Measure grid-node flow-field drift vs cold exact solve when warm is enabled
        if warm and warp is not None and warp._grid is not None:
            saved_iters = list(_SOLVE_TRACE["cg_iters"])
            cold_mapper, _ = am.build_mapper(
                child_assets[am.H_PROPERTY],
                child_assets[am.V_PROPERTY],
                main_assets[am.H_PROPERTY],
                main_assets[am.V_PROPERTY],
                additional_pairs=add_pairs,
                ground_pairs=gnd_pairs,
                warm=None,
            )
            _SOLVE_TRACE["cg_iters"] = saved_iters
            cw = cold_mapper.warp
            sx = max(mapper.h_scales[0], mapper.h_scales[1], 1.0)
            sy = max(mapper.v_scales[0], mapper.v_scales[1], 1.0)
            dux = ( np.asarray(warp._grid["ux"], dtype=float) - np.asarray(cw._grid["ux"], dtype=float) ) * sx
            duy = ( np.asarray(warp._grid["uy"], dtype=float) - np.asarray(cw._grid["uy"], dtype=float) ) * sy
            err_px = float(np.max(np.hypot(dux, duy)))
            # Also check mapped canvas point difference mapper(p) vs cold_mapper(p) on 33x33 grid
            pts_33 = grids[33]
            map_diffs = []
            for pt in pts_33:
                pw = mapper(pt)
                pc = cold_mapper(pt)
                if pw is not None and pc is not None:
                    map_diffs.append(math.hypot(pw[0] - pc[0], pw[1] - pc[1]))
            map_err_px = max(map_diffs) if map_diffs else 0.0
            warm_err_records.append(max(err_px, map_err_px))

        ui_step_perf = bm_perf + gm_perf + gc_perf + occ_perf
        ui_step_proc = bm_proc + gm_proc + gc_proc + occ_proc
        full_poly_perf = ui_step_perf + sm_poly_perf
        full_poly_proc = ui_step_proc + sm_poly_proc
        full_spline_perf = ui_step_perf + sm_spline_perf
        full_spline_proc = ui_step_proc + sm_spline_proc

        step_vals = {
            "1. 求解: build_mapper 总时长": (bm_perf, bm_proc),
            "  ├─ _Frame 构造 (child+main)": (frame_perf, frame_proc),
            "  ├─ 附加线/地线 Third 坐标抬升": (lift_perf, lift_proc),
            "  ├─ _FlowFieldWarp 场构造": (field_perf, field_proc),
            "  ├─ _FlowFieldWarp CG 求解总计": (cg_total_perf, cg_total_proc),
            "  │  ├─ CG Round 0 (初始求解)": (cg_r0_perf, cg_r0_proc),
            "  │  ├─ CG Boost Round 1": (cg_b1_perf, cg_b1_proc),
            "  │  ├─ CG Boost Round 2": (cg_b2_perf, cg_b2_proc),
            "  │  └─ CG Boost Round 3": (cg_b3_perf, cg_b3_proc),
            "  └─ build_mapper 其余开销": (bm_other_perf, bm_other_proc),
            "2b. 批量 129x129 (16641点): third_of_batch": (b_third_perf, b_third_proc),
            "2b. 批量 129x129 (16641点): main_of_third_batch": (b_main_perf, b_main_proc),
            "2b. 批量 129x129 (16641点): warp.apply_batch": (b_wapp_perf, b_wapp_proc),
            "2b. 批量 129x129 (16641点): warp.det_sign_batch": (b_wdet_perf, b_wdet_proc),
            "3. 叠加层: _grid_overlay_items('main')": (gm_perf, gm_proc),
            "3. 叠加层: _grid_overlay_items('child')": (gc_perf, gc_proc),
            "3. 叠加层: _occlusion_overlay_items()": (occ_perf, occ_proc),
            "4. 笔画映射: _adaptive_map_polyline (200×40段)": (sm_poly_perf, sm_poly_proc),
            "4. 笔画映射: _emit_spline_mode (200×40段全管线)": (sm_spline_perf, sm_spline_proc),
            "Σ 单步小计 A (UI拖动释放: 求解+叠加层)": (ui_step_perf, ui_step_proc),
            "Σ 单步总时长 B (求解+叠加层+_adaptive_map_polyline)": (full_poly_perf, full_poly_proc),
            "Σ 单步总时长 C (求解+叠加层+_emit_spline_mode)": (full_spline_perf, full_spline_proc),
        }
        for n in (33, 65, 129):
            cnt = n * n
            for fn_label, fn_key in (
                ("mapper(p)", "mapper"),
                ("third_of(p)", "third_of"),
                ("main_of_third(t)", "main_of_third"),
                ("warp.apply(t)", "warp.apply"),
                ("warp.det_sign(t)", "warp.det_sign"),
            ):
                k = f"2. 逐点 {n}x{n} ({cnt}点): {fn_label}"
                step_vals[k] = (pw_perf[(n, fn_key)], pw_proc[(n, fn_key)])

        for k in stage_keys:
            perf_records[k].append(step_vals[k][0])
            proc_records[k].append(step_vals[k][1])

        cg_iter_records.append(list(_SOLVE_TRACE["cg_iters"]))

    # Aggregate statistics
    rows_out = []
    ui_med_perf, _ = _stats_ms(perf_records["Σ 单步小计 A (UI拖动释放: 求解+叠加层)"])
    tot_b_med_perf, _ = _stats_ms(perf_records["Σ 单步总时长 B (求解+叠加层+_adaptive_map_polyline)"])

    point_counts = {
        "33x33": 33 * 33,
        "65x65": 65 * 65,
        "129x129": 129 * 129,
    }

    for k in stage_keys:
        med_perf, p90_perf = _stats_ms(perf_records[k])
        med_proc, p90_proc = _stats_ms(proc_records[k])
        per_pt_str = "-"
        for tag, cnt in point_counts.items():
            if tag in k:
                if rig_name == "A" and ("warp.apply" in k or "warp.det_sign" in k):
                    per_pt_str = "N/A (无warp)"
                else:
                    per_pt_str = f"{(med_perf * 1000.0 / cnt):.3f} / {(p90_perf * 1000.0 / cnt):.3f} μs"
                break

        if k.startswith("2. 逐点") or k.startswith("2b. 批量"):
            pct_ui = "-"
            pct_tot = "-"
        else:
            pct_ui = f"{(100.0 * med_perf / ui_med_perf):.1f}%" if ui_med_perf > 0 else "0.0%"
            pct_tot = f"{(100.0 * med_perf / tot_b_med_perf):.1f}%" if tot_b_med_perf > 0 else "0.0%"

        rows_out.append({
            "rig": rig_name,
            "stage": k,
            "med_perf": med_perf,
            "p90_perf": p90_perf,
            "med_proc": med_proc,
            "p90_proc": p90_proc,
            "per_pt": per_pt_str,
            "pct_ui": pct_ui,
            "pct_tot": pct_tot,
        })

    # Summarize CG iteration statistics
    cg_summary = {"total_iters_per_step": [], "by_round": {}}
    for step_iters in cg_iter_records:
        cg_summary["total_iters_per_step"].append(sum(it for _, _, it in step_iters))
        for r_tag, comp, it in step_iters:
            key = f"{r_tag}_u{'x' if comp == 0 else 'y'}"
            cg_summary["by_round"].setdefault(key, []).append(it)

    warm_err_max = max(warm_err_records) if warm_err_records else 0.0
    warm_err_med = _percentile(sorted(warm_err_records), 0.50) if warm_err_records else 0.0

    if warm and rig_name in ("B", "C", "D"):
        bm_med, _ = _stats_ms(perf_records["1. 求解: build_mapper 总时长"])
        b_third_med, _ = _stats_ms(perf_records["2b. 批量 129x129 (16641点): third_of_batch"])
        b_main_med, _ = _stats_ms(perf_records["2b. 批量 129x129 (16641点): main_of_third_batch"])
        assert bm_med <= 25.0, f"Rig {rig_name} warm build_mapper median {bm_med:.2f}ms > 25ms"
        assert warm_err_max <= 0.5, f"Rig {rig_name} warm max error {warm_err_max:.4f}px > 0.5px"
        # Rig C foldable multi-seed third_of_batch is ~13-16ms depending on multi-core turbo bin (vs ~125ms scalar)
        assert b_third_med <= 20.0, f"Rig {rig_name} third_of_batch median {b_third_med:.2f}ms > 20ms"
        assert b_main_med <= 3.0, f"Rig {rig_name} main_of_third_batch median {b_main_med:.2f}ms > 3ms"

    return {
        "rig": rig_name,
        "warm": bool(warm),
        "steps_total": steps,
        "steps_kept": steps - 1,
        "meta": meta_checks,
        "rows": rows_out,
        "cg_summary": cg_summary,
        "warm_err_max_px": warm_err_max,
        "warm_err_med_px": warm_err_med,
    }


def _print_report(results):
    warm_flag = any(r.get("warm") for r in results)
    mode_tag = " [WARM 模式]" if warm_flag else ""
    print(f"# AnimeAn 拖动单步耗时基准报告 (issue #46){mode_tag}\n")
    print("- **计时方法**：同一进程内每轮依次执行全部阶段，共跑 N 步（默认 40 步），丢弃第 1 轮（step 0 冷缓存），对后 39 轮统计中位数（p50）与 p90；同时记录 `time.perf_counter` 与 `time.process_time`。")
    print("- **每步开始前清理的缓存**：")
    print("  1. `am._invalidate_grid_cache()`（清空 `_GRID_CACHE['child']`、`_GRID_CACHE['main']`、`_OCCLUSION_CACHE['items']`）")
    print("  2. `am._MAPPER_CACHE['key'] = None; am._MAPPER_CACHE['mapper'] = None`（强制每步重新 `build_mapper`，阶段 1 完成后写入当前步指纹供阶段 3 叠加层复用，与真实拖动一致）")
    print("  3. `am._PROXY_PAIRING['key'] = None; am._PROXY_PAIRING['value'] = None`")
    print("  4. `am._invalidate_additional_thirds('child')` 与 `am._invalidate_additional_thirds('main')` 及被拖动线的 `line.pop('third', None)`（强制每步重新执行附加线/地线 Third 坐标抬升）\n")

    print("| 装置 | 阶段 | 中位 ms (perf / proc) | p90 ms (perf / proc) | 单点耗时 (中位 / p90) | 占UI拖动单步% | 占含笔画单步% |")
    print("| :--- | :--- | ---: | ---: | ---: | ---: | ---: |")
    for res in results:
        for r in res["rows"]:
            print(
                f"| {r['rig']} | {r['stage']} "
                f"| {r['med_perf']:.3f} / {r['med_proc']:.3f} "
                f"| {r['p90_perf']:.3f} / {r['p90_proc']:.3f} "
                f"| {r['per_pt']} "
                f"| {r['pct_ui']} "
                f"| {r['pct_tot']} |"
            )

    print("\n## CG 迭代次数统计\n")
    print("| 装置 | 网格规模 | 单步总CG迭代 (min / med / p90 / max) | 各轮各分量迭代 (r0_ux, r0_uy, boost1_ux, boost1_uy, boost2_ux, boost2_uy, boost3_ux, boost3_uy) |")
    print("| :--- | :--- | :--- | :--- |")
    for res in results:
        rig = res["rig"]
        grid_sz = res["meta"].get("grid_size", "None")
        tots = sorted(res["cg_summary"]["total_iters_per_step"])
        if not tots or max(tots) == 0:
            print(f"| {rig} | {grid_sz} | 0 (无流场求解) | - |")
            continue
        t_min = tots[0]
        t_med = int(round(_percentile(tots, 0.50)))
        t_p90 = int(round(_percentile(tots, 0.90)))
        t_max = tots[-1]
        by_r = res["cg_summary"]["by_round"]
        parts = []
        for k in ("r0_ux", "r0_uy", "boost_1_ux", "boost_1_uy", "boost_2_ux", "boost_2_uy", "boost_3_ux", "boost_3_uy"):
            vals = sorted(by_r.get(k, [0]))
            parts.append(f"{k}:{vals[0]}..{int(round(_percentile(vals, 0.5)))}..{vals[-1]}")
        print(f"| {rig} | {grid_sz} | {t_min} / {t_med} / {t_p90} / {t_max} | {', '.join(parts)} |")

    if warm_flag:
        print("\n## Warm 求解相对同一步冷启动精确解的网格节点最大流场偏差\n")
        print("| 装置 | 连续步数 | 中位节点最大偏差 (px) | 全程最大节点偏差 (px) | 验收阈值 (<= 0.5 px) |")
        print("| :--- | ---: | ---: | ---: | :--- |")
        for res in results:
            rig = res["rig"]
            if res["meta"].get("grid_size", "None") == "None":
                continue
            med_e = res.get("warm_err_med_px", 0.0)
            max_e = res.get("warm_err_max_px", 0.0)
            status = "PASS" if max_e <= 0.5 else "FAIL"
            print(f"| {rig} | {res['steps_kept']} | {med_e:.4f} | {max_e:.4f} | {status} |")


def opt_out_of_power_throttling():
    """Match backend/main.cpp optOutOfPowerThrottling() so windowless benchmark
    subprocesses on Windows are not demoted to efficiency cores mid-run."""
    if sys.platform != "win32":
        return
    try:
        class _ThrottlingState(ctypes.Structure):
            _fields_ = [
                ("version", ctypes.c_ulong),
                ("controlMask", ctypes.c_ulong),
                ("stateMask", ctypes.c_ulong),
            ]

        k32 = ctypes.WinDLL("kernel32", use_last_error=True)
        set_info = getattr(k32, "SetProcessInformation", None)
        if set_info is None:
            return
        state = _ThrottlingState(1, 1, 0)  # PROCESS_POWER_THROTTLING_EXECUTION_SPEED off
        set_info(k32.GetCurrentProcess(), 4, ctypes.byref(state), ctypes.sizeof(state))
    except OSError:
        pass


def run_preview_benchmark(rig_name, steps=40):
    """End-to-end mapping_preview drag frame-budget benchmark (33 ms median, 45 ms max)."""
    tests_dir = os.path.join(ROOT, "tests")
    if tests_dir not in sys.path:
        sys.path.insert(0, tests_dir)
    import statistics
    import mapping_preview as mp
    import t_mapping_preview as tmp

    _scenes, fake, _uid = tmp.fresh_rig_world(rig_name)
    offset = [0.0]
    mp._clock = lambda: time.perf_counter() + offset[0]

    if rig_name == "D":
        handle = am.H_PROPERTY
        press_pos = {"x": 640.0, "y": 360.0}
        move_pos = lambda s: {"x": 640.0, "y": 360.0 - s * 0.6}
    else:
        handle = f"{am.ADDITIONAL_PROPERTY}:1"
        press_pos = {"x": 0.0, "y": 72.0}
        move_pos = lambda s: {"x": s * 0.35, "y": 72.0 + s * 0.25}

    am._guide_drag_event({"view": "main", "handle": handle, "phase": "press", "position": press_pos})
    durations_ms = []
    sub_timings = []
    for step in range(1, steps + 1):
        offset[0] += 0.060
        t0 = time.perf_counter()
        am._guide_drag_event({"view": "main", "handle": handle, "phase": "move", "position": move_pos(step)})
        durations_ms.append((time.perf_counter() - t0) * 1000.0)
        sub_timings.append(mp.last_timings())
    mp.end()

    assert len(fake.ui.set_field_calls) == steps
    sorted_ms = sorted(durations_ms)
    med_ms = statistics.median(sorted_ms)
    p90_ms = _percentile(sorted_ms, 0.90)
    max_ms = sorted_ms[-1]
    stages = {
        k: statistics.median([st.get(k, 0.0) * 1000.0 for st in sub_timings])
        for k in ("build_mapper", "third_of_batch", "main_of_third_batch",
                  "det2", "depth_table", "pack_and_upload", "total")
    }
    assert med_ms <= 33.0, f"Rig {rig_name} preview median {med_ms:.2f}ms > 33.0ms"
    if rig_name == "D":
        assert max_ms <= 45.0, f"Rig D preview max {max_ms:.2f}ms > 45.0ms"
    return {
        "rig": rig_name,
        "steps": steps,
        "med_ms": med_ms,
        "p90_ms": p90_ms,
        "max_ms": max_ms,
        "stages_med_ms": stages,
    }


def _print_preview_report(results):
    print("# AnimeAn mapping_preview 帧预算基准报告 (optOutOfPowerThrottling 已开启)\n")
    print("| 装置 | 步数 | 中位 ms (<=33ms) | p90 ms | 最大 ms | build_mapper | main_of_third | depth_table | pack_and_upload |")
    print("| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
    for r in results:
        st = r["stages_med_ms"]
        print(
            f"| {r['rig']} | {r['steps']} | {r['med_ms']:.2f} | {r['p90_ms']:.2f} | {r['max_ms']:.2f} "
            f"| {st['build_mapper']:.2f} | {st['main_of_third_batch']:.2f} "
            f"| {st['depth_table']:.2f} | {st['pack_and_upload']:.2f} |"
        )


def main():
    opt_out_of_power_throttling()
    parser = argparse.ArgumentParser(description="Benchmark single-step guide drag cost.")
    parser.add_argument("--rig", choices=("A", "B", "C", "D"), help="Run single rig (A, B, C, or D).")
    parser.add_argument("--all", action="store_true", help="Run rigs sequentially in subprocesses.")
    parser.add_argument("--warm", action="store_true", help="Enable warm solver state across drag steps.")
    parser.add_argument("--preview", action="store_true", help="Benchmark end-to-end mapping_preview frame budgets.")
    parser.add_argument("--steps", type=int, default=40, help="Number of drag steps (default: 40).")
    parser.add_argument("--json-out", type=str, default="", help="Internal: write JSON result to path.")
    args = parser.parse_args()

    if args.preview:
        if args.all:
            results = []
            for rig in ("B", "C", "D"):
                cmd = [
                    sys.executable, os.path.abspath(__file__),
                    "--preview", "--rig", rig, "--steps", str(args.steps), "--json-out", "-",
                ]
                proc = subprocess.run(cmd, capture_output=True, text=True)
                if proc.returncode != 0:
                    raise RuntimeError(
                        f"Subprocess for rig {rig} failed (rc={proc.returncode}):\n"
                        f"{proc.stdout}\n{proc.stderr}"
                    )
                json_line = next(
                    (ln[len("__BENCH_JSON__:"):] for ln in reversed(proc.stdout.splitlines())
                     if ln.startswith("__BENCH_JSON__:")),
                    None,
                )
                if json_line is None:
                    raise RuntimeError(f"Subprocess for rig {rig} produced no JSON:\n{proc.stdout}\n{proc.stderr}")
                results.append(json.loads(json_line))
            _print_preview_report(results)
            return
        rig = args.rig or "D"
        res = run_preview_benchmark(rig, steps=args.steps)
        if args.json_out == "-":
            print("__BENCH_JSON__:" + json.dumps(res, ensure_ascii=False))
        else:
            _print_preview_report([res])
        return

    if args.all:
        results = []
        for rig in ("A", "B", "C"):
            cmd = [
                sys.executable,
                os.path.abspath(__file__),
                "--rig", rig,
                "--steps", str(args.steps),
                "--json-out", "-",
            ]
            if args.warm:
                cmd.append("--warm")
            proc = subprocess.run(cmd, capture_output=True, text=True)
            if proc.returncode != 0:
                raise RuntimeError(
                    f"Subprocess for rig {rig} failed (rc={proc.returncode}):\n"
                    f"{proc.stdout}\n{proc.stderr}"
                )
            # Extract JSON line from stdout
            json_line = None
            for line in reversed(proc.stdout.splitlines()):
                if line.startswith("__BENCH_JSON__:"):
                    json_line = line[len("__BENCH_JSON__:"):]
                    break
            if json_line is None:
                raise RuntimeError(f"Subprocess for rig {rig} produced no JSON:\n{proc.stdout}\n{proc.stderr}")
            results.append(json.loads(json_line))
        _print_report(results)
        return

    rig = args.rig or "B"
    res = run_rig_benchmark(rig, steps=args.steps, warm=args.warm)
    if args.json_out == "-":
        print("__BENCH_JSON__:" + json.dumps(res, ensure_ascii=False))
    else:
        _print_report([res])


if __name__ == "__main__":
    main()
