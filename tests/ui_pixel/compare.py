"""Pixel comparison tool for Qt and Web UI window screenshots.

Usage:
    py tests/ui_pixel/compare.py A.png B.png --radius 10 --tolerance 10 --out-dir <dir>
"""
import argparse
import json
import math
import os
import sys

import numpy as np
from PIL import Image

LADDER_RADII = (0, 1, 2, 4, 10)
LADDER_TOLERANCES = (0, 2, 5, 10)


def _tol_to_thresh(tol_percent: float) -> int:
    if tol_percent < 0.0:
        return -1
    if tol_percent >= 100.0:
        return 255
    return int(math.floor(tol_percent * 255.0 / 100.0 + 1e-9))


def _slice_pair(size: int, delta: int):
    if delta >= 0:
        return slice(0, size - delta), slice(delta, size), size - delta
    return slice(-delta, size), slice(0, size + delta), size + delta


def compare_arrays(arr_a: np.ndarray, arr_b: np.ndarray, radius: float = 10.0, tolerance: float = 10.0):
    """Compare two HxWx3 uint8 arrays and return (report_dict, passed_mask)."""
    if arr_a.shape != arr_b.shape:
        raise ValueError(f"Image sizes differ: {arr_a.shape} vs {arr_b.shape}")

    h, w = arr_a.shape[:2]
    total = h * w
    size_wh = [int(w), int(h)]

    if total == 0:
        ladder = [
            {"radius": r, "tolerance": t, "pass_rate": 1.0}
            for r in LADDER_RADII
            for t in LADDER_TOLERANCES
        ]
        report = {
            "size_a": size_wh,
            "size_b": size_wh,
            "pass": True,
            "pass_rate": 1.0,
            "failed_pixels": 0,
            "exact_match_rate": 1.0,
            "mean_abs_diff_percent": 0.0,
            "ladder": ladder,
        }
        return report, np.ones((h, w), dtype=bool)

    a0 = np.ascontiguousarray(arr_a[..., 0], dtype=np.uint8)
    a1 = np.ascontiguousarray(arr_a[..., 1], dtype=np.uint8)
    a2 = np.ascontiguousarray(arr_a[..., 2], dtype=np.uint8)
    b0 = np.ascontiguousarray(arr_b[..., 0], dtype=np.uint8)
    b1 = np.ascontiguousarray(arr_b[..., 1], dtype=np.uint8)
    b2 = np.ascontiguousarray(arr_b[..., 2], dtype=np.uint8)

    # Offset (0, 0): compute per-channel absolute differences in uint8
    d0 = np.maximum(a0, b0) - np.minimum(a0, b0)
    d1 = np.maximum(a1, b1) - np.minimum(a1, b1)
    d2 = np.maximum(a2, b2) - np.minimum(a2, b2)

    total_abs_diff = (
        int(d0.sum(dtype=np.uint64))
        + int(d1.sum(dtype=np.uint64))
        + int(d2.sum(dtype=np.uint64))
    )
    mean_abs_diff_percent = float(total_abs_diff / (total * 3 * 255.0) * 100.0)

    diff0 = np.maximum(np.maximum(d0, d1), d2)
    exact_pixels = int(np.count_nonzero(diff0 == 0))
    exact_match_rate = float(exact_pixels / total)

    min_ab = diff0.copy()
    min_ba = diff0.copy()

    req_r = float(radius)
    all_radii = sorted({float(r) for r in LADDER_RADII} | {req_r})
    max_r = max(all_radii)
    max_r_int = int(math.floor(max_r))

    offsets = []
    max_r_sq = max_r * max_r
    for dy in range(-max_r_int, max_r_int + 1):
        for dx in range(-max_r_int, max_r_int + 1):
            if dy == 0 and dx == 0:
                continue
            dist_sq = dy * dy + dx * dx
            if dist_sq <= max_r_sq:
                offsets.append((dist_sq, dy, dx))
    offsets.sort(key=lambda item: item[0])

    # Preallocated contiguous 1D uint8 scratch buffers
    _d = np.empty(total, dtype=np.uint8)
    _t1 = np.empty(total, dtype=np.uint8)
    _t2 = np.empty(total, dtype=np.uint8)
    both_buf = np.empty((h, w), dtype=np.uint8)

    ladder_rates = {}
    passed_mask = None
    target_thresh = _tol_to_thresh(tolerance)
    ladder_thresh = {t: _tol_to_thresh(t) for t in LADDER_TOLERANCES}

    all_zero = exact_pixels == total
    off_idx = 0
    num_offsets = len(offsets)

    for r_val in all_radii:
        r_sq = r_val * r_val + 1e-9
        if not all_zero:
            while off_idx < num_offsets and offsets[off_idx][0] <= r_sq:
                _, dy, dx = offsets[off_idx]
                off_idx += 1
                if abs(dy) >= h or abs(dx) >= w:
                    continue
                sy_a, sy_b, sh = _slice_pair(h, dy)
                sx_a, sx_b, sw = _slice_pair(w, dx)
                n = sh * sw

                d = _d[:n].reshape(sh, sw)
                t1 = _t1[:n].reshape(sh, sw)
                t2 = _t2[:n].reshape(sh, sw)

                sa = (sy_a, sx_a)
                sb = (sy_b, sx_b)

                # Channel 0
                np.maximum(a0[sa], b0[sb], out=d)
                np.minimum(a0[sa], b0[sb], out=t1)
                np.subtract(d, t1, out=d)

                # Channel 1
                np.maximum(a1[sa], b1[sb], out=t1)
                np.minimum(a1[sa], b1[sb], out=t2)
                np.subtract(t1, t2, out=t1)
                np.maximum(d, t1, out=d)

                # Channel 2
                np.maximum(a2[sa], b2[sb], out=t1)
                np.minimum(a2[sa], b2[sb], out=t2)
                np.subtract(t1, t2, out=t1)
                np.maximum(d, t1, out=d)

                np.minimum(min_ab[sa], d, out=min_ab[sa])
                np.minimum(min_ba[sb], d, out=min_ba[sb])

        if all_zero:
            if r_val == req_r:
                passed_mask = np.ones((h, w), dtype=bool)
            for r_lad in LADDER_RADII:
                if float(r_lad) == r_val:
                    for t_lad in LADDER_TOLERANCES:
                        ladder_rates[(r_lad, t_lad)] = 1.0
        else:
            np.maximum(min_ab, min_ba, out=both_buf)
            if r_val == req_r:
                if target_thresh < 0:
                    passed_mask = np.zeros((h, w), dtype=bool)
                else:
                    passed_mask = both_buf <= target_thresh

            for r_lad in LADDER_RADII:
                if float(r_lad) == r_val:
                    for t_lad in LADDER_TOLERANCES:
                        th = ladder_thresh[t_lad]
                        cnt = int(np.count_nonzero(both_buf <= th))
                        ladder_rates[(r_lad, t_lad)] = float(cnt / total)
                        if t_lad == 0 and cnt == total:
                            all_zero = True

    passed_pixels = int(np.count_nonzero(passed_mask))
    failed_pixels = int(total - passed_pixels)
    pass_rate = float(passed_pixels / total)

    ladder = [
        {"radius": r, "tolerance": t, "pass_rate": ladder_rates[(r, t)]}
        for r in LADDER_RADII
        for t in LADDER_TOLERANCES
    ]

    report = {
        "size_a": size_wh,
        "size_b": size_wh,
        "pass": failed_pixels == 0,
        "pass_rate": pass_rate,
        "failed_pixels": failed_pixels,
        "exact_match_rate": exact_match_rate,
        "mean_abs_diff_percent": mean_abs_diff_percent,
        "ladder": ladder,
    }
    return report, passed_mask


def make_diff_image(arr_a: np.ndarray, arr_b: np.ndarray, passed_mask: np.ndarray) -> Image.Image:
    """Create diff image where failed pixels are bright red (255, 0, 0) and passed pixels are faded."""
    faded = (((arr_a.astype(np.uint16) + arr_b.astype(np.uint16)) >> 3) + 160).astype(np.uint8)
    out = faded.copy()
    failed = ~passed_mask
    out[failed, 0] = 255
    out[failed, 1] = 0
    out[failed, 2] = 0
    return Image.fromarray(out, mode="RGB")


def compare_files(path_a: str, path_b: str, radius: float = 10.0, tolerance: float = 10.0, out_dir: str | None = None):
    """Compare two image files and return (report_dict, exit_code)."""
    with Image.open(path_a) as img_a_raw:
        size_a = [int(img_a_raw.width), int(img_a_raw.height)]
        img_a = img_a_raw.convert("RGB")
    with Image.open(path_b) as img_b_raw:
        size_b = [int(img_b_raw.width), int(img_b_raw.height)]
        img_b = img_b_raw.convert("RGB")

    if size_a != size_b:
        report = {
            "size_a": size_a,
            "size_b": size_b,
            "pass": False,
            "error": f"Image sizes do not match: {size_a} vs {size_b}",
        }
        if out_dir:
            os.makedirs(out_dir, exist_ok=True)
            with open(os.path.join(out_dir, "report.json"), "w", encoding="utf-8") as f:
                json.dump(report, f, indent=2)
        return report, 2

    arr_a = np.asarray(img_a, dtype=np.uint8)
    arr_b = np.asarray(img_b, dtype=np.uint8)
    report, passed_mask = compare_arrays(arr_a, arr_b, radius=radius, tolerance=tolerance)

    if out_dir:
        os.makedirs(out_dir, exist_ok=True)
        diff_img = make_diff_image(arr_a, arr_b, passed_mask)
        diff_img.save(os.path.join(out_dir, "diff.png"))
        with open(os.path.join(out_dir, "report.json"), "w", encoding="utf-8") as f:
            json.dump(report, f, indent=2)

    exit_code = 0 if report["pass"] else 1
    return report, exit_code


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description="Compare two UI screenshots with radius and color tolerance.")
    parser.add_argument("image_a", help="Path to screenshot A")
    parser.add_argument("image_b", help="Path to screenshot B")
    parser.add_argument("--radius", type=float, default=10.0, help="Neighborhood radius in pixels (default: 10)")
    parser.add_argument("--tolerance", type=float, default=10.0, help="Per-channel tolerance in percent (default: 10)")
    parser.add_argument("--out-dir", type=str, default=None, help="Directory to write diff.png and report.json")
    args = parser.parse_args(argv)

    report, exit_code = compare_files(
        args.image_a,
        args.image_b,
        radius=args.radius,
        tolerance=args.tolerance,
        out_dir=args.out_dir,
    )
    if exit_code == 2:
        print(report["error"], file=sys.stderr)
    print(json.dumps(report))
    return exit_code


if __name__ == "__main__":
    sys.exit(main())
