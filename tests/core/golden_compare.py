import math
import sys


def parse_path_from_tokens(tokens):
    try:
        ec_idx = tokens.index("ec")
    except ValueError:
        return None
    if ec_idx + 9 >= len(tokens):
        return None
    if tokens[ec_idx + 2] != "len" or tokens[ec_idx + 4] != "rect" or tokens[ec_idx + 9] != "el":
        return None

    elem_count = int(tokens[ec_idx + 1])
    path_len = float(tokens[ec_idx + 3])
    rx = float(tokens[ec_idx + 5])
    ry = float(tokens[ec_idx + 6])
    rw = float(tokens[ec_idx + 7])
    rh = float(tokens[ec_idx + 8])

    elems = []
    pos = ec_idx + 10
    for _ in range(elem_count):
        if pos + 2 >= len(tokens):
            return None
        etype = int(tokens[pos])
        ex = float(tokens[pos + 1])
        ey = float(tokens[pos + 2])
        elems.append((etype, ex, ey))
        pos += 3

    return {
        "ec": elem_count,
        "len": path_len,
        "rect": (rx, ry, rw, rh),
        "elems": elems,
    }


def extract_subpaths(elems):
    subpaths = []
    cur = []
    i = 0
    n = len(elems)
    while i < n:
        etype, x, y = elems[i]
        if etype == 0:
            if cur:
                subpaths.append(cur)
            cur = [(x, y)]
            i += 1
        elif etype == 1:
            cur.append((x, y))
            i += 1
        elif etype == 2 and i + 2 < n:
            p0 = cur[-1] if cur else (x, y)
            c1 = (x, y)
            c2 = (elems[i + 1][1], elems[i + 1][2])
            p3 = (elems[i + 2][1], elems[i + 2][2])
            steps = 16
            for s in range(1, steps + 1):
                t = s / steps
                mt = 1.0 - t
                px = (mt ** 3) * p0[0] + 3 * (mt ** 2) * t * c1[0] + 3 * mt * (t ** 2) * c2[0] + (t ** 3) * p3[0]
                py = (mt ** 3) * p0[1] + 3 * (mt ** 2) * t * c1[1] + 3 * mt * (t ** 2) * c2[1] + (t ** 3) * p3[1]
                cur.append((px, py))
            i += 3
        else:
            cur.append((x, y))
            i += 1
    if cur:
        subpaths.append(cur)
    return subpaths


def shoelace_signed_area_and_perim(pts):
    if len(pts) < 3:
        return 0.0, 0.0
    poly = list(pts)
    if len(poly) > 1 and poly[0] == poly[-1]:
        poly.pop()
    m = len(poly)
    if m < 3:
        return 0.0, 0.0
    area2 = 0.0
    perim = 0.0
    for i in range(m):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % m]
        area2 += x0 * y1 - x1 * y0
        perim += math.hypot(x1 - x0, y1 - y0)
    return 0.5 * area2, perim


def point_in_poly(pt, poly):
    px, py = pt
    inside = False
    m = len(poly)
    for i in range(m):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % m]
        if (y0 > py) != (y1 > py):
            x_int = x0 + (py - y0) * (x1 - x0) / (y1 - y0)
            if px < x_int:
                inside = not inside
    return inside


def path_signed_area_and_perim(parsed):
    subpaths = extract_subpaths(parsed["elems"])
    cleaned = []
    for sp in subpaths:
        pts = list(sp)
        while len(pts) > 1 and math.hypot(pts[-1][0] - pts[0][0], pts[-1][1] - pts[0][1]) <= 1e-9:
            pts.pop()
        if len(pts) >= 3:
            cleaned.append(pts)

    if not cleaned:
        return 0.0, 0.0

    # Use OddEven containment depth so holes always subtract from outer contours
    # even if a simplifier emits hole vertices in the same winding direction.
    total_area = 0.0
    total_perim = 0.0
    for idx, poly in enumerate(cleaned):
        raw_area, perim = shoelace_signed_area_and_perim(poly)
        total_perim += perim
        if len(cleaned) == 1:
            total_area += raw_area
        else:
            test_pt = poly[0]
            depth = 0
            for jdx, other in enumerate(cleaned):
                if idx == jdx:
                    continue
                if point_in_poly(test_pt, other):
                    depth += 1
            sign = -1.0 if (depth % 2 == 1) else 1.0
            total_area += sign * abs(raw_area)

    return total_area, max(total_perim, parsed["len"])


def compare_boolean_path_lines(old_tokens, new_tokens):
    p_old = parse_path_from_tokens(old_tokens)
    p_new = parse_path_from_tokens(new_tokens)
    if p_old is None or p_new is None:
        return False, "parse_error"

    if p_old["ec"] == 0 and p_new["ec"] == 0:
        return True, "both_empty"
    if (p_old["ec"] == 0) != (p_new["ec"] == 0):
        return False, f"empty_mismatch old_ec={p_old['ec']} new_ec={p_new['ec']}"

    rx0, ry0, rw0, rh0 = p_old["rect"]
    rx1, ry1, rw1, rh1 = p_new["rect"]
    bbox_diffs = [
        abs(rx0 - rx1),
        abs(ry0 - ry1),
        abs(rw0 - rw1),
        abs(rh0 - rh1),
        abs((rx0 + rw0) - (rx1 + rw1)),
        abs((ry0 + rh0) - (ry1 + rh1)),
    ]
    max_bbox_diff = max(bbox_diffs)
    if max_bbox_diff > 1.5:
        return False, f"bbox_diff={max_bbox_diff:.6g}"

    area_old, perim_old = path_signed_area_and_perim(p_old)
    area_new, perim_new = path_signed_area_and_perim(p_new)
    area_diff = abs(area_old - area_new)
    max_area = max(abs(area_old), abs(area_new))
    perim = max(perim_old, perim_new)

    rel_ok = (max_area > 0.0) and ((area_diff / max_area) <= 0.005)
    abs_ok = area_diff <= (perim * 0.1)
    if not (rel_ok or abs_ok):
        return False, f"area_old={area_old:.6g} area_new={area_new:.6g} diff={area_diff:.6g} perim={perim:.6g}"

    return True, "ok"


def compare_percent_lines(old_tokens, new_tokens):
    if len(old_tokens) != len(new_tokens):
        return False
    for a_str, b_str in zip(old_tokens[2:], new_tokens[2:]):
        if a_str == b_str:
            continue
        try:
            a = float(a_str)
            b = float(b_str)
        except ValueError:
            return False
        diff = abs(a - b)
        scale = max(abs(a), abs(b), 1.0)
        if diff > 1e-12 * scale:
            return False
    return True


def summarize_line_diff(old_line: str, new_line: str):
    old_tokens = old_line.split()
    new_tokens = new_line.split()
    if len(old_tokens) == len(new_tokens) and len(old_tokens) > 2:
        diffs = []
        for idx, (ot, nt) in enumerate(zip(old_tokens, new_tokens)):
            if ot != nt:
                ctx_prefix = old_tokens[idx - 1] + " " if idx > 0 else ""
                if len(ot) > 120 or len(nt) > 120:
                    pos = next((k for k, (ca, cb) in enumerate(zip(ot, nt)) if ca != cb), 0)
                    start = max(0, pos - 25)
                    diffs.append((ot[start:start + 70], nt[start:start + 70]))
                else:
                    diffs.append((f"{ctx_prefix}{ot}", f"{ctx_prefix}{nt}"))
                if len(diffs) >= 4:
                    break
        if diffs:
            old_summary = " ; ".join(d[0] for d in diffs)
            new_summary = " ; ".join(d[1] for d in diffs)
            return old_summary, new_summary
    return old_line[:160], new_line[:160]


def main():
    if len(sys.argv) < 3:
        print("Usage: golden_compare.py <old.txt> <new.txt>", file=sys.stderr)
        return 2

    with open(sys.argv[1], "r", encoding="utf-8", errors="replace") as f_old:
        old_lines = [line.rstrip("\r\n") for line in f_old]
    with open(sys.argv[2], "r", encoding="utf-8", errors="replace") as f_new:
        new_lines = [line.rstrip("\r\n") for line in f_new]

    total_lines = max(len(old_lines), len(new_lines))
    exact_lines = 0
    relaxed_lines = 0
    relaxed_boolean = 0
    relaxed_percent = 0
    diff_lines = 0
    diff_samples = []

    boolean_cases = {
        "computeVectorRegionFaces",
        "vectorRegionPathAt",
        "fillPathFromMask",
    }
    percent_cases = set()

    for i in range(total_lines):
        if i >= len(old_lines):
            diff_lines += 1
            if len(diff_samples) < 30:
                diff_samples.append((i + 1, "<missing_old>", "<EOF>", new_lines[i][:160]))
            continue
        if i >= len(new_lines):
            diff_lines += 1
            if len(diff_samples) < 30:
                diff_samples.append((i + 1, "<missing_new>", old_lines[i][:160], "<EOF>"))
            continue

        ol = old_lines[i]
        nl = new_lines[i]
        if ol == nl:
            exact_lines += 1
            continue

        old_tokens = ol.split()
        new_tokens = nl.split()
        if len(old_tokens) < 2 or len(new_tokens) < 2 or old_tokens[:2] != new_tokens[:2]:
            diff_lines += 1
            if len(diff_samples) < 30:
                key = f"{old_tokens[0] if old_tokens else '?'} vs {new_tokens[0] if new_tokens else '?'}"
                diff_samples.append((i + 1, key, ol[:160], nl[:160]))
            continue

        case_name = old_tokens[0]
        case_key = f"{old_tokens[0]} {old_tokens[1]}"

        if case_name in boolean_cases:
            ok, reason = compare_boolean_path_lines(old_tokens, new_tokens)
            if ok:
                relaxed_lines += 1
                relaxed_boolean += 1
                continue
            diff_lines += 1
            if len(diff_samples) < 30:
                old_s, new_s = summarize_line_diff(ol, nl)
                diff_samples.append((i + 1, f"{case_key} ({reason})", old_s, new_s))
            continue

        if case_name in percent_cases:
            if compare_percent_lines(old_tokens, new_tokens):
                relaxed_lines += 1
                relaxed_percent += 1
                continue
            diff_lines += 1
            if len(diff_samples) < 30:
                old_s, new_s = summarize_line_diff(ol, nl)
                diff_samples.append((i + 1, case_key, old_s, new_s))
            continue

        diff_lines += 1
        if len(diff_samples) < 30:
            old_s, new_s = summarize_line_diff(ol, nl)
            diff_samples.append((i + 1, case_key, old_s, new_s))

    if diff_samples:
        print(f"First {len(diff_samples)} different cases:")
        for line_no, key, ol_val, nl_val in diff_samples:
            print(f"  [Line {line_no}] {key} | OLD: {ol_val} | NEW: {nl_val}")

    print(f"Total lines: {total_lines}")
    print(f"Exact match lines: {exact_lines}")
    print(f"Relaxed match lines: {relaxed_lines} (boolean_ops={relaxed_boolean}, percent_point={relaxed_percent})")
    print(f"Different lines: {diff_lines}")

    return 0 if diff_lines == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
