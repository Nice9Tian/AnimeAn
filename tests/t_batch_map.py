"""Tests for vectorized batch evaluation on _Curve, _Frame, _FlowFieldWarp,
and build_mapper (issue #46).

Covers:
  1. Rigs A, B, C (reused from bench_drag_cost.py) on a 129x129 grid:
     - third_of_batch vs scalar third_of: coordinate diff <= 0.05 px, any ok
       disagreement confined within 1 grid cell of a fold/seam boundary;
     - main_of_third_batch vs scalar main_of_third: coordinate diff <= 0.05 px;
     - warp.apply_batch / warp.det_sign_batch vs scalar apply / det_sign;
     - vectorization invariants on 129x129: single-pass batch calls with zero
       scalar fallbacks (wall-clock ms budgets live in bench_drag_cost.py).
  2. Curved guides carrying cubic Bezier `commands` (_Curve.curved == True),
     both unfoldable and foldable (including sharp corners and explicit seeds).
  3. Folded flow-field warp (_all_positive == False) and out-of-grid tent
     fallback for warp.apply_batch and warp.det_sign_batch.
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


def _make_cubic_spec(cubics):
    """Build a guide asset dict with `points` and `commands` from cubic tuples."""
    cmds = [{"type": "move", "to": {"x": cubics[0][0][0], "y": cubics[0][0][1]}}]
    pts = [cubics[0][0]]
    for p0, c1, c2, p3 in cubics:
        cmds.append({
            "type": "cubic",
            "from": {"x": p0[0], "y": p0[1]},
            "control1": {"x": c1[0], "y": c1[1]},
            "control2": {"x": c2[0], "y": c2[1]},
            "to": {"x": p3[0], "y": p3[1]},
        })
        pts.append(p3)
    return {"points": pts, "commands": cmds, "width": 3.0}


def _check_ok_seam_rule(ok_batch, ok_scalar):
    """Verify that any mismatch between ok_batch and ok_scalar on a 2D grid
    lies within 1 grid cell of a seam where ok_scalar changes value."""
    diff = ok_batch != ok_scalar
    if not np.any(diff):
        return True
    seam = np.zeros_like(ok_scalar, dtype=bool)
    seam[:-1, :] |= ok_scalar[:-1, :] != ok_scalar[1:, :]
    seam[1:, :] |= ok_scalar[:-1, :] != ok_scalar[1:, :]
    seam[:, :-1] |= ok_scalar[:, :-1] != ok_scalar[:, 1:]
    seam[:, 1:] |= ok_scalar[:, :-1] != ok_scalar[:, 1:]
    dilated = seam.copy()
    for di in (-1, 0, 1):
        for dj in (-1, 0, 1):
            if di == 0 and dj == 0:
                continue
            s_i = slice(max(0, di), ok_scalar.shape[0] + min(0, di))
            d_i = slice(max(0, -di), ok_scalar.shape[0] + min(0, -di))
            s_j = slice(max(0, dj), ok_scalar.shape[1] + min(0, dj))
            d_j = slice(max(0, -dj), ok_scalar.shape[1] + min(0, -dj))
            dilated[d_i, d_j] |= seam[s_i, s_j]
    return bool(np.all(dilated[diff]))


class BatchMapTests(unittest.TestCase):

    def test_1_rigs_a_b_c_129x129_accuracy_and_speed(self):
        pts_129 = bdc._make_uniform_grids()[129]
        px = np.array([p[0] for p in pts_129], dtype=float).reshape(129, 129)
        py = np.array([p[1] for p in pts_129], dtype=float).reshape(129, 129)

        for rig in ("A", "B", "C"):
            step_drag = bdc._make_rig(rig)
            step_drag(2)
            c_assets = am._assets_for("child")
            m_assets = am._assets_for("main")
            mp, _ = am.build_mapper(
                c_assets[am.H_PROPERTY], c_assets[am.V_PROPERTY],
                m_assets[am.H_PROPERTY], m_assets[am.V_PROPERTY],
                additional_pairs=am._additional_pairs(),
                ground_pairs=am._ground_pairs(),
            )

            # Warm up once so _ensure_batch caches are populated
            lh_w, lv_w, _ok_w = mp.third_of_batch(px, py)
            mp.main_of_third_batch(lh_w, lv_w)

            # Verify single-pass vectorized batch execution (zero scalar calls)
            counts = {
                "solve_batch": 0, "dirs_batch": 0, "hv_batch": 0, "warp_batch": 0,
                "point_flat": 0, "scalar": 0,
            }
            saved = []

            def wrap(obj, attr, bucket):
                orig = getattr(obj, attr)
                saved.append((obj, attr, orig))

                def wrapped(*a, **kw):
                    counts[bucket] += 1
                    return orig(*a, **kw)

                setattr(obj, attr, wrapped)

            wrap(mp.child_frame, "solve_full_batch", "solve_batch")
            wrap(mp.child_frame, "directions_batch", "dirs_batch")
            wrap(mp.child_frame, "solve_full", "scalar")
            wrap(mp.child_frame, "directions", "scalar")
            wrap(mp.main_frame, "hv_batch", "hv_batch")
            wrap(mp.main_frame, "hv", "scalar")
            for fr in (mp.child_frame, mp.main_frame):
                for g in (fr.gh, fr.gv):
                    wrap(g, "_point_flat", "point_flat")
                    wrap(g, "point_at", "scalar")
                    wrap(g, "dir_at", "scalar")
            if mp.warp is not None:
                wrap(mp.warp, "apply_batch", "warp_batch")
                wrap(mp.warp, "apply", "scalar")

            try:
                t0 = time.perf_counter()
                lh_b, lv_b, ok_b = mp.third_of_batch(px, py)
                ms_third = (time.perf_counter() - t0) * 1000.0
                third_counts = dict(counts)
                for k in counts:
                    counts[k] = 0
                t0 = time.perf_counter()
                mx_b, my_b = mp.main_of_third_batch(lh_b, lv_b)
                ms_main = (time.perf_counter() - t0) * 1000.0
                main_counts = dict(counts)
            finally:
                for obj, attr, orig in reversed(saved):
                    setattr(obj, attr, orig)

            max_solve_batches = 1 if mp.can_fold() else 2
            self.assertGreaterEqual(third_counts["solve_batch"], 1)
            self.assertLessEqual(third_counts["solve_batch"], max_solve_batches)
            self.assertEqual(third_counts["dirs_batch"], 1)
            self.assertEqual(third_counts["scalar"], 0)
            self.assertLess(third_counts["point_flat"], 600)

            self.assertEqual(main_counts["hv_batch"], 1)
            self.assertEqual(main_counts["point_flat"], 2)
            self.assertEqual(main_counts["scalar"], 0)
            if mp.warp is not None:
                self.assertEqual(main_counts["warp_batch"], 1)

            # Pointwise scalar reference
            lh_s = np.empty(len(pts_129), dtype=float)
            lv_s = np.empty(len(pts_129), dtype=float)
            ok_s = np.empty(len(pts_129), dtype=bool)
            mx_s = np.empty(len(pts_129), dtype=float)
            my_s = np.empty(len(pts_129), dtype=float)
            for idx, pt in enumerate(pts_129):
                h_, v_, ok_ = mp.third_of(pt)
                lh_s[idx] = h_
                lv_s[idx] = v_
                ok_s[idx] = ok_
                x_, y_ = mp.main_of_third((h_, v_))
                mx_s[idx] = x_
                my_s[idx] = y_
            lh_s = lh_s.reshape(129, 129)
            lv_s = lv_s.reshape(129, 129)
            ok_s = ok_s.reshape(129, 129)
            mx_s = mx_s.reshape(129, 129)
            my_s = my_s.reshape(129, 129)

            both_ok = ok_b & ok_s
            third_diff = float(np.max(np.hypot(
                lh_b[both_ok] - lh_s[both_ok],
                lv_b[both_ok] - lv_s[both_ok],
            )))
            main_diff = float(np.max(np.hypot(
                mx_b[both_ok] - mx_s[both_ok],
                my_b[both_ok] - my_s[both_ok],
            )))
            ok_mismatches = int(np.sum(ok_b != ok_s))

            self.assertTrue(
                _check_ok_seam_rule(ok_b, ok_s),
                f"Rig {rig}: ok mismatch outside 1-cell seam neighbourhood",
            )
            self.assertLessEqual(third_diff, 0.05)
            self.assertLessEqual(main_diff, 0.05)

            if mp.warp is not None:
                wx_b, wy_b = mp.warp.apply_batch(lh_b, lv_b)
                ds_b = mp.warp.det_sign_batch(lh_b, lv_b)
                wx_s = np.empty_like(wx_b)
                wy_s = np.empty_like(wy_b)
                ds_s = np.empty_like(ds_b)
                for i in range(129):
                    for j in range(129):
                        uv = (float(lh_b[i, j]), float(lv_b[i, j]))
                        wx_s[i, j], wy_s[i, j] = mp.warp.apply(uv)
                        ds_s[i, j] = mp.warp.det_sign(uv)
                w_diff = float(np.max(np.hypot(wx_b - wx_s, wy_b - wy_s)))
                self.assertLessEqual(w_diff, 0.05)
                self.assertTrue(np.array_equal(ds_b, ds_s))

            print(
                f"1) Rig {rig} 129x129: third_of_batch={ms_third:.2f} ms "
                f"(point_flat={third_counts['point_flat']}, diff {third_diff:.2e} px, ok_diff={ok_mismatches}), "
                f"main_of_third_batch={ms_main:.2f} ms (diff {main_diff:.2e} px)"
            )

    def test_2_curved_commands_and_foldable_frame(self):
        # Unfoldable cubic-command guides
        ch_spec = _make_cubic_spec([
            ((-300.0, 0.0), (-200.0, 45.0), (-100.0, -35.0), (0.0, 6.0)),
            ((0.0, 6.0), (100.0, 42.0), (200.0, -25.0), (300.0, 12.0)),
        ])
        cv_spec = _make_cubic_spec([
            ((0.0, -200.0), (-28.0, -130.0), (22.0, -60.0), (2.0, 0.0)),
            ((2.0, 0.0), (-18.0, 60.0), (26.0, 130.0), (-6.0, 200.0)),
        ])
        mh_spec = _make_cubic_spec([
            ((-310.0, -10.0), (-200.0, 55.0), (-100.0, -20.0), (5.0, 12.0)),
            ((5.0, 12.0), (110.0, 44.0), (210.0, -10.0), (310.0, 25.0)),
        ])
        mv_spec = _make_cubic_spec([
            ((-8.0, -210.0), (-30.0, -140.0), (15.0, -70.0), (5.0, 12.0)),
            ((5.0, 12.0), (-5.0, 80.0), (30.0, 140.0), (10.0, 210.0)),
        ])
        mp, _ = am.build_mapper(ch_spec, cv_spec, mh_spec, mv_spec)
        self.assertTrue(mp.child_frame.gh.curved and mp.child_frame.gv.curved)
        self.assertTrue(mp.main_frame.gh.curved and mp.main_frame.gv.curved)
        self.assertNotEqual(len(mp.child_frame.h_cum), len(mp.child_frame.h))
        self.assertEqual(len(mp.child_frame.gh.flat_arcs), len(mp.child_frame.h))

        gx = np.linspace(-260.0, 260.0, 45)
        gy = np.linspace(-170.0, 170.0, 45)
        px, py = np.meshgrid(gx, gy, indexing="ij")
        lh_b, lv_b, ok_b = mp.third_of_batch(px, py)
        mx_b, my_b = mp.main_of_third_batch(lh_b, lv_b)

        # Also test explicit seed in third_of_batch
        lh_seed, lv_seed, ok_seed = mp.third_of_batch(
            px, py, seed=(lh_b * 0.95, lv_b * 0.95)
        )
        self.assertLess(float(np.max(np.hypot(lh_seed - lh_b, lv_seed - lv_b))), 1e-6)
        self.assertTrue(np.array_equal(ok_seed, ok_b))

        for i in range(45):
            for j in range(45):
                pt = (float(px[i, j]), float(py[i, j]))
                h_, v_, ok_ = mp.third_of(pt)
                x_, y_ = mp.main_of_third((h_, v_))
                self.assertEqual(bool(ok_b[i, j]), ok_)
                self.assertLess(math.hypot(lh_b[i, j] - h_, lv_b[i, j] - v_), 1e-5)
                self.assertLess(math.hypot(mx_b[i, j] - x_, my_b[i, j] - y_), 1e-5)

        # Foldable curved cubic-command guide (sharp hook that reverses orientation)
        fold_ch_spec = _make_cubic_spec([
            ((-250.0, 0.0), (-120.0, 0.0), (-40.0, 0.0), (40.0, 0.0)),
            ((40.0, 0.0), (140.0, 90.0), (140.0, 180.0), (20.0, 210.0)),
        ])
        mp_fold, _ = am.build_mapper(fold_ch_spec, cv_spec, mh_spec, mv_spec)
        self.assertTrue(mp_fold.can_fold())
        lh_f, lv_f, ok_f = mp_fold.third_of_batch(px, py)
        ok_f_s = np.empty_like(ok_f)
        lh_f_s = np.empty_like(lh_f)
        lv_f_s = np.empty_like(lv_f)
        for i in range(45):
            for j in range(45):
                lh_f_s[i, j], lv_f_s[i, j], ok_f_s[i, j] = mp_fold.third_of(
                    (float(px[i, j]), float(py[i, j]))
                )
        self.assertFalse(bool(np.all(ok_f_s)))  # genuinely has severed/folded region
        self.assertTrue(_check_ok_seam_rule(ok_f, ok_f_s))
        valid = ok_f & ok_f_s
        self.assertLessEqual(
            float(np.max(np.hypot(lh_f[valid] - lh_f_s[valid], lv_f[valid] - lv_f_s[valid]))),
            0.05,
        )
        print("2) curved cubic-command guides (unfoldable + foldable + seeded): ALL OK")

    def test_3_warp_tent_fringe_and_folded_det_sign_batch(self):
        # Build a warp with tent overrun and strong opposing asks that fold
        c_line1 = [(-200.0 + 400.0 * k / 32, 320.0) for k in range(33)]
        m_line1 = [(-200.0 + 400.0 * k / 32, 420.0) for k in range(33)]
        c_line2 = [(-120.0 + 240.0 * k / 32, 15.0) for k in range(33)]
        # Strong ask right next to pinned H axis to force an intrinsic fold
        m_line2 = [(-120.0 + 240.0 * k / 32, -180.0) for k in range(33)]
        warp = am._FlowFieldWarp(
            [(c_line1, m_line1, 0), (c_line2, m_line2, 1)],
            falloff="linear",
            radius_factor=0.5,
            frame_window=((-300.0, 300.0), (-250.0, 250.0)),
        )
        # Query across a wide range covering interior, 3-cell edge blend, and outside tent
        qx = np.linspace(-600.0, 600.0, 61)
        qy = np.linspace(-600.0, 600.0, 61)
        X, Y = np.meshgrid(qx, qy, indexing="ij")
        wx_b, wy_b = warp.apply_batch(X, Y)
        ds_b = warp.det_sign_batch(X, Y)

        wx_s = np.empty_like(wx_b)
        wy_s = np.empty_like(wy_b)
        ds_s = np.empty_like(ds_b)
        for i in range(61):
            for j in range(61):
                pt = (float(X[i, j]), float(Y[i, j]))
                wx_s[i, j], wy_s[i, j] = warp.apply(pt)
                ds_s[i, j] = warp.det_sign(pt)

        self.assertLess(float(np.max(np.hypot(wx_b - wx_s, wy_b - wy_s))), 1e-9)
        self.assertTrue(np.array_equal(ds_b, ds_s))
        print("3) warp.apply_batch & det_sign_batch (interior, fringe, tent, folded): ALL OK")


if __name__ == "__main__":
    suite = unittest.defaultTestLoader.loadTestsFromTestCase(BatchMapTests)
    result = unittest.TextTestRunner(verbosity=0).run(suite)
    if not result.wasSuccessful():
        sys.exit(1)
    print("t_batch_map: ALL OK")
