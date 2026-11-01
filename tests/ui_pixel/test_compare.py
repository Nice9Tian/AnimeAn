"""Unit tests for tests/ui_pixel/compare.py."""
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

import compare  # noqa: E402

COMPARE_SCRIPT = os.path.join(HERE, "compare.py")


class TestCompare(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.tmp_dir = self._tmp.name

    def tearDown(self):
        self._tmp.cleanup()

    def _save_img(self, name: str, arr: np.ndarray) -> str:
        path = os.path.join(self.tmp_dir, name)
        Image.fromarray(arr.astype(np.uint8), mode="RGB").save(path)
        return path

    def _run_cli(self, path_a: str, path_b: str, radius=10, tolerance=10, out_dir=None):
        cmd = [
            sys.executable,
            COMPARE_SCRIPT,
            path_a,
            path_b,
            "--radius",
            str(radius),
            "--tolerance",
            str(tolerance),
        ]
        if out_dir is not None:
            cmd.extend(["--out-dir", out_dir])
        return subprocess.run(cmd, capture_output=True, text=True)

    def test_identical_images(self):
        """完全相同的图：合格率 100%，退出码 0，ladder 全为 1.0。"""
        arr = np.zeros((60, 80, 3), dtype=np.uint8)
        arr[:, :] = (240, 240, 240)
        arr[10:30, 15:45] = (30, 120, 200)
        arr[35:50, 20:70] = (220, 60, 40)

        path_a = self._save_img("a.png", arr)
        path_b = self._save_img("b.png", arr)
        out_dir = os.path.join(self.tmp_dir, "out_ident")

        proc = self._run_cli(path_a, path_b, radius=0, tolerance=0, out_dir=out_dir)
        self.assertEqual(proc.returncode, 0)
        data = json.loads(proc.stdout)
        self.assertEqual(data["size_a"], [80, 60])
        self.assertEqual(data["size_b"], [80, 60])
        self.assertTrue(data["pass"])
        self.assertEqual(data["pass_rate"], 1.0)
        self.assertEqual(data["failed_pixels"], 0)
        self.assertEqual(data["exact_match_rate"], 1.0)
        self.assertEqual(data["mean_abs_diff_percent"], 0.0)
        self.assertEqual(len(data["ladder"]), 20)
        for item in data["ladder"]:
            self.assertEqual(item["pass_rate"], 1.0)

        self.assertTrue(os.path.isfile(os.path.join(out_dir, "diff.png")))
        self.assertTrue(os.path.isfile(os.path.join(out_dir, "report.json")))

    def test_shift_3px_fails_radius_2_passes_radius_3(self):
        """整体错开 3 像素在半径 2 时失败、半径 3 时通过。"""
        arr_a = np.full((60, 80, 3), 255, dtype=np.uint8)
        arr_b = np.full((60, 80, 3), 255, dtype=np.uint8)
        arr_a[15:45, 20:50] = (20, 40, 60)
        arr_b[15:45, 23:53] = (20, 40, 60)  # 整体向右错开 3 像素

        path_a = self._save_img("shift_a.png", arr_a)
        path_b = self._save_img("shift_b.png", arr_b)

        proc_r2 = self._run_cli(path_a, path_b, radius=2, tolerance=0)
        self.assertEqual(proc_r2.returncode, 1)
        data_r2 = json.loads(proc_r2.stdout)
        self.assertFalse(data_r2["pass"])
        self.assertGreater(data_r2["failed_pixels"], 0)
        self.assertLess(data_r2["pass_rate"], 1.0)

        proc_r3 = self._run_cli(path_a, path_b, radius=3, tolerance=0)
        self.assertEqual(proc_r3.returncode, 0)
        data_r3 = json.loads(proc_r3.stdout)
        self.assertTrue(data_r3["pass"])
        self.assertEqual(data_r3["failed_pixels"], 0)
        self.assertEqual(data_r3["pass_rate"], 1.0)

    def test_color_diff_9_percent_passes_11_percent_fails(self):
        """颜色差 9% 通过、差 11% 失败（tolerance = 10%）。"""
        base = np.full((32, 32, 3), 100, dtype=np.uint8)
        # 9% of 255 = 22.95 -> 23 (23 / 255 = 9.02% <= 10%)
        diff_9 = np.full((32, 32, 3), 123, dtype=np.uint8)
        # 11% of 255 = 28.05 -> 28 (28 / 255 = 10.98% > 10%)
        diff_11 = np.full((32, 32, 3), 128, dtype=np.uint8)

        path_base = self._save_img("base.png", base)
        path_9 = self._save_img("diff9.png", diff_9)
        path_11 = self._save_img("diff11.png", diff_11)

        proc_9 = self._run_cli(path_base, path_9, radius=0, tolerance=10)
        self.assertEqual(proc_9.returncode, 0)
        data_9 = json.loads(proc_9.stdout)
        self.assertTrue(data_9["pass"])
        self.assertEqual(data_9["failed_pixels"], 0)

        proc_11 = self._run_cli(path_base, path_11, radius=0, tolerance=10)
        self.assertEqual(proc_11.returncode, 1)
        data_11 = json.loads(proc_11.stdout)
        self.assertFalse(data_11["pass"])
        self.assertEqual(data_11["failed_pixels"], 32 * 32)

    def test_size_mismatch_exits_with_code_2(self):
        """两张图尺寸不同时报错退出，退出码 2。"""
        arr_a = np.zeros((40, 50, 3), dtype=np.uint8)
        arr_b = np.zeros((40, 60, 3), dtype=np.uint8)
        path_a = self._save_img("size_a.png", arr_a)
        path_b = self._save_img("size_b.png", arr_b)

        proc = self._run_cli(path_a, path_b, radius=10, tolerance=10)
        self.assertEqual(proc.returncode, 2)
        data = json.loads(proc.stdout)
        self.assertEqual(data["size_a"], [50, 40])
        self.assertEqual(data["size_b"], [60, 40])
        self.assertFalse(data["pass"])

    def test_isolated_single_bad_pixel_detected(self):
        """一个孤立错误像素被判出来，且 diff.png 将该像素标红。"""
        arr_a = np.full((50, 50, 3), 100, dtype=np.uint8)
        arr_b = arr_a.copy()
        arr_b[25, 30] = (200, 200, 200)

        path_a = self._save_img("iso_a.png", arr_a)
        path_b = self._save_img("iso_b.png", arr_b)
        out_dir = os.path.join(self.tmp_dir, "out_iso")

        proc = self._run_cli(path_a, path_b, radius=10, tolerance=10, out_dir=out_dir)
        self.assertEqual(proc.returncode, 1)
        data = json.loads(proc.stdout)
        self.assertFalse(data["pass"])
        self.assertEqual(data["failed_pixels"], 1)
        self.assertAlmostEqual(data["pass_rate"], (2500 - 1) / 2500)

        with Image.open(os.path.join(out_dir, "diff.png")) as diff_img:
            diff_arr = np.asarray(diff_img)
        self.assertEqual(tuple(diff_arr[25, 30]), (255, 0, 0))
        red_mask = (diff_arr[..., 0] == 255) & (diff_arr[..., 1] == 0) & (diff_arr[..., 2] == 0)
        self.assertEqual(int(np.count_nonzero(red_mask)), 1)

    def test_directional_symmetry_extra_line(self):
        """方向对称：A 比 B 多一条线，或 B 比 A 多一条线，都要被判出来且结果一致。"""
        blank = np.full((60, 80, 3), 255, dtype=np.uint8)
        with_line = blank.copy()
        with_line[30, 10:70] = (0, 0, 0)  # 60 个像素的一条黑线

        path_blank = self._save_img("blank.png", blank)
        path_line = self._save_img("line.png", with_line)

        # A 比 B 多一条线
        proc_ab = self._run_cli(path_line, path_blank, radius=10, tolerance=10)
        self.assertEqual(proc_ab.returncode, 1)
        data_ab = json.loads(proc_ab.stdout)
        self.assertFalse(data_ab["pass"])
        self.assertEqual(data_ab["failed_pixels"], 60)

        # B 比 A 多一条线
        proc_ba = self._run_cli(path_blank, path_line, radius=10, tolerance=10)
        self.assertEqual(proc_ba.returncode, 1)
        data_ba = json.loads(proc_ba.stdout)
        self.assertFalse(data_ba["pass"])
        self.assertEqual(data_ba["failed_pixels"], 60)

        self.assertEqual(data_ab["pass_rate"], data_ba["pass_rate"])
        self.assertEqual(data_ab["exact_match_rate"], data_ba["exact_match_rate"])
        self.assertAlmostEqual(data_ab["mean_abs_diff_percent"], data_ba["mean_abs_diff_percent"])
        self.assertEqual(data_ab["ladder"], data_ba["ladder"])

    def test_2mp_radius_10_performance(self):
        """2 百万像素的图、半径 10，在 20 秒内跑完（含不可提前退出的最坏情况）。"""
        h, w = 1000, 2000  # 2,000,000 pixels
        arr_a = np.full((h, w, 3), 220, dtype=np.uint8)
        arr_a[100:900:50, 20:1980] = (40, 50, 60)
        arr_a[20:980, 100:1900:80] = (80, 120, 160)
        arr_b = np.roll(arr_a, shift=(6, 8), axis=(0, 1))
        arr_b[500, 1000] = (0, 0, 0)  # 1 处无法匹配的像素，强制跑满半径 10 的全部偏移

        t0 = time.perf_counter()
        report, _ = compare.compare_arrays(arr_a, arr_b, radius=10, tolerance=10)
        elapsed = time.perf_counter() - t0

        self.assertFalse(report["pass"])
        self.assertEqual(report["failed_pixels"], 1)
        self.assertLess(elapsed, 20.0, f"2MP radius-10 took {elapsed:.2f}s (> 20s limit)")


if __name__ == "__main__":
    unittest.main()
