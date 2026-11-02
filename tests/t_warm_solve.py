"""Tests for warm-started _FlowFieldWarp and build_mapper (issue #46).

Covers:
  1. Bit-for-bit equality (np.array_equal) of ux, uy, det between the
     pre-refactor inline CG / det_of reference implementation and the
     refactored cold path (warm=None) on Rigs B and C.
  2. Continuous drag (>= 16 steps) with warm={} on Rigs B and C:
     - max flow-field difference on grid nodes vs cold exact solve at the
       same step <= 0.5 canvas px;
     - warm build_mapper frames at least 5x faster than cold, timed alone.
  3. Grid shape change invalidates warm initial guesses cleanly and restarts
     from zero without error.
  4. Per-line field cache reuses unchanged lines and recomputes moved lines.
"""

import math
import os
import sys
import time
import unittest

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.path.insert(0, os.path.join(ROOT, "tests"))

import auto_mapping as am
import bench_drag_cost as bdc
import gridsolve


def _legacy_cg(apply_a, b, x0=None, precond=None, tol=1e-10, max_iter=1500,
               step_tol=None):
    """Exact pre-refactor inline CG loop from _FlowFieldWarp._build."""
    R = b.copy()
    E = np.zeros_like(b)
    Z = R if precond is None else R / precond
    P = Z.copy()
    rs = float(np.sum(R * Z))
    if rs <= 0.0:
        return E, 0
    rs0 = rs
    it = 0
    for it in range(1, max_iter + 1):
        AP = apply_a(P)
        denom = float(np.sum(P * AP)) or 1e-30
        alpha = rs / denom
        E += alpha * P
        R -= alpha * AP
        Z = R if precond is None else R / precond
        rs_new = float(np.sum(R * Z))
        if rs_new <= tol * rs0:
            break
        P = Z + (rs_new / rs) * P
        rs = rs_new
    return E, it


def _legacy_det2(sol_x, sol_y, du, dv):
    """Exact pre-refactor inline det_of from _FlowFieldWarp._build."""
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


def _build_current(warm=None):
    c_assets = am._assets_for("child")
    m_assets = am._assets_for("main")
    mp, _ = am.build_mapper(
        c_assets[am.H_PROPERTY], c_assets[am.V_PROPERTY],
        m_assets[am.H_PROPERTY], m_assets[am.V_PROPERTY],
        additional_pairs=am._additional_pairs(),
        ground_pairs=am._ground_pairs(),
        warm=warm,
    )
    return mp


class WarmSolveTests(unittest.TestCase):

    def test_1_cold_path_bit_identical_to_legacy_reference(self):
        for rig in ("B", "C"):
            step_drag = bdc._make_rig(rig)
            step_drag(3)
            bdc._clear_step_caches()

            orig_pcg = gridsolve.pcg
            orig_det2 = gridsolve.det2
            try:
                gridsolve.pcg = _legacy_cg
                gridsolve.det2 = _legacy_det2
                mp_ref = _build_current(warm=None)
            finally:
                gridsolve.pcg = orig_pcg
                gridsolve.det2 = orig_det2

            bdc._clear_step_caches()
            mp_new = _build_current(warm=None)

            g_ref = mp_ref.warp._grid
            g_new = mp_new.warp._grid
            self.assertEqual(g_ref["nx"], g_new["nx"])
            self.assertEqual(g_ref["ny"], g_new["ny"])
            self.assertTrue(
                np.array_equal(np.asarray(g_ref["ux"]), np.asarray(g_new["ux"])),
                f"Rig {rig}: cold ux is not bit-for-bit identical to legacy loop",
            )
            self.assertTrue(
                np.array_equal(np.asarray(g_ref["uy"]), np.asarray(g_new["uy"])),
                f"Rig {rig}: cold uy is not bit-for-bit identical to legacy loop",
            )
            self.assertEqual(g_ref["det"], g_new["det"])
        print("1) cold path (warm=None) ux/uy/det bit-identical to legacy loop on Rigs B & C")

    def test_2_warm_drag_accuracy_and_speed_rigs_b_and_c(self):
        steps = 20
        for rig in ("B", "C", "D"):
            step_drag = bdc._make_rig(rig)
            cx0, cy0 = (640.0, 360.0) if rig == "D" else (0.0, 0.0)
            sample_pts = bdc._make_uniform_grids(cx0, cy0)[33][::9]
            warm = {}
            times_ms = []
            cold_ms = []
            max_errs_px = []
            max_mapper_errs_px = []

            for s in range(steps):
                step_drag(s)
                bdc._clear_step_caches()
                t0 = time.perf_counter()
                mp_cold = _build_current(warm=None)
                cold_ms.append((time.perf_counter() - t0) * 1000.0)
                ux_cold = np.asarray(mp_cold.warp._grid["ux"], dtype=float)
                uy_cold = np.asarray(mp_cold.warp._grid["uy"], dtype=float)
                sx = max(mp_cold.h_scales[0], mp_cold.h_scales[1], 1.0)
                sy = max(mp_cold.v_scales[0], mp_cold.v_scales[1], 1.0)

                bdc._clear_step_caches()
                t0 = time.perf_counter()
                mp_warm = _build_current(warm=warm)
                dt_ms = (time.perf_counter() - t0) * 1000.0

                ux_warm = np.asarray(mp_warm.warp._grid["ux"], dtype=float)
                uy_warm = np.asarray(mp_warm.warp._grid["uy"], dtype=float)
                err_px = float(np.max(np.hypot(
                    (ux_warm - ux_cold) * sx,
                    (uy_warm - uy_cold) * sy,
                )))
                m_err = 0.0
                for pt in sample_pts:
                    qw = mp_warm(pt)
                    qc = mp_cold(pt)
                    if qw is not None and qc is not None:
                        d = float(np.hypot(qw[0] - qc[0], qw[1] - qc[1]))
                        if d > m_err:
                            m_err = d
                if s > 0:
                    times_ms.append(dt_ms)
                    max_errs_px.append(err_px)
                    max_mapper_errs_px.append(m_err)

            med_ms = sorted(times_ms)[len(times_ms) // 2]
            worst_err = max(max_errs_px)
            worst_m_err = max(max_mapper_errs_px)
            print(
                f"2) Rig {rig} ({steps - 1} warm drag steps): "
                f"median={med_ms:.2f} ms, max_err={worst_err:.4f} px, "
                f"max_mapper_err={worst_m_err:.4f} px (<=0.5 px)"
            )
            self.assertLessEqual(
                worst_err, 0.5,
                f"Rig {rig}: warm flow field drifted {worst_err:.4f} px > 0.5 px",
            )
            self.assertLessEqual(
                worst_m_err, 0.5,
                f"Rig {rig}: warm mapper(p) drifted {worst_m_err:.4f} px > 0.5 px",
            )

            # Timed alone, as a drag runs: no cold solve between frames and
            # no cache clearing, neither of which a preview frame pays for.
            warm = {}
            pure_ms = []
            for s in range(steps, steps + 12):
                step_drag(s)
                t0 = time.perf_counter()
                _build_current(warm=warm)
                pure_ms.append((time.perf_counter() - t0) * 1000.0)
            pure_med = sorted(pure_ms[1:])[len(pure_ms[1:]) // 2]
            cold_med = sorted(cold_ms)[len(cold_ms) // 2]
            print(f"2) Rig {rig} warm frames alone: median={pure_med:.2f} ms, "
                  f"cold {cold_med:.2f} ms")
            # A ratio, not milliseconds: a process under load here gets moved to
            # a core half as fast (a bare Python loop doubles too), mid-suite.
            self.assertLessEqual(
                pure_med * 5.0, cold_med,
                f"Rig {rig}: warm {pure_med:.2f} ms is not 5x faster than cold {cold_med:.2f} ms",
            )

    def test_5_a_frame_over_budget_says_so_and_settles_on_the_same_geometry(self):
        """A jump outruns the step cap; solving on unchanged lines then converges."""
        step_drag = bdc._make_rig("C")
        warm = {}
        step_drag(0)
        bdc._clear_step_caches()
        _build_current(warm=warm)
        for s in range(1, 41):
            step_drag(s)
        bdc._clear_step_caches()
        mapper = _build_current(warm=warm)
        self.assertTrue(warm.get("unsettled"), "a 40-step jump should hit the step cap")
        warm["settling"] = True
        for _ in range(40):
            if not warm.get("unsettled"):
                break
            bdc._clear_step_caches()
            mapper = _build_current(warm=warm)
        self.assertFalse(warm.get("unsettled"), "the solve never settled")
        bdc._clear_step_caches()
        cold = _build_current(warm=None)
        points = bdc._make_uniform_grids(0.0, 0.0)[33][::9]
        worst = max(math.hypot(mapper(p)[0] - cold(p)[0], mapper(p)[1] - cold(p)[1])
                    for p in points)
        print(f"5) Rig C settled after a jump: mapper err {worst:.4f} px (<=0.5 px)")
        self.assertLessEqual(worst, 0.5)

    def test_3_grid_shape_change_invalidates_warm_cleanly(self):
        step_drag = bdc._make_rig("B")
        step_drag(0)
        warm = {}
        mp0 = _build_current(warm=warm)
        shape0 = warm.get("shape")
        self.assertIsNotNone(shape0)
        self.assertTrue(len(warm.get("solves", {})) > 0)

        # Inject a bogus shape and bogus array sizes into warm["solves"]
        warm["shape"] = (17, 19)
        warm["solves"] = {(0, 0): np.ones((17, 19)), (0, 1): np.ones((17, 19))}

        step_drag(1)
        bdc._clear_step_caches()
        mp_warm = _build_current(warm=warm)
        self.assertEqual(warm.get("shape"), shape0)

        bdc._clear_step_caches()
        mp_cold = _build_current(warm=None)
        ux_w = np.asarray(mp_warm.warp._grid["ux"])
        uy_w = np.asarray(mp_warm.warp._grid["uy"])
        ux_c = np.asarray(mp_cold.warp._grid["ux"])
        uy_c = np.asarray(mp_cold.warp._grid["uy"])
        err = float(np.max(np.hypot(ux_w - ux_c, uy_w - uy_c)))
        self.assertLess(err, 1e-3)
        print("3) grid shape mismatch invalidates warm solves and restarts cleanly")

    def test_4_line_field_cache_reuses_unchanged_lines(self):
        step_drag = bdc._make_rig("B")
        step_drag(0)
        warm = {}
        _build_current(warm=warm)
        cache0 = dict(warm["line_fields"])
        self.assertEqual(len(cache0), 3)  # 3 active non-neutral lines on Rig B

        # Perturb child points of active line 1 (shorter than line 0 so domain
        # margin is unchanged); the other 2 active lines keep identical child
        # Third geometry and must reuse their cached tuple objects.
        add_c = am._assets_for("child")[am.ADDITIONAL_PROPERTY]["lines"]
        add_c[1]["points"] = [[x, y + 5.0] for x, y in add_c[1]["points"]]
        add_c[1].pop("third", None)
        bdc._clear_step_caches()
        _build_current(warm=warm)
        cache1 = warm["line_fields"]
        self.assertEqual(len(cache1), 3)
        reused = sum(
            1 for k, v in cache1.items()
            if k in cache0 and cache0[k][1] is v[1]
        )
        self.assertEqual(reused, 2)
        print(f"4) per-line field cache reused {reused}/3 unchanged active lines on child edit")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(WarmSolveTests)
    result = unittest.TextTestRunner(verbosity=0).run(suite)
    if not result.wasSuccessful():
        sys.exit(1)
    print("t_warm_solve: ALL OK")
