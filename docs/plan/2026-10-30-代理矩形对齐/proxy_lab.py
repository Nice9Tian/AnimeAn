"""Browser test bench for pyfile/proxy_rect.py: the page only draws, every
answer comes from the real proxy_rect.guides and auto_mapping.build_mapper.

Run from anywhere with `py proxy_lab.py`, then open http://127.0.0.1:8764.
"""
import json
import math
import os
import sys
import types
import threading
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", "..", ".."))
sys.path.insert(0, os.path.join(ROOT, "pyfile"))
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import auto_mapping as am
import proxy_rect as pr

PORT = 8764
EDGE_STEPS = 24
PATTERN_STEP = 6.0
TEXTURE_H = [(-150.0, 0.0), (150.0, 0.0)]
TEXTURE_V = [(0.0, -100.0), (0.0, 100.0)]
_SOLVE_LOCK = threading.Lock()      # threads only so an idle browser socket
                                    # cannot block the bench; the mapper is not
                                    # meant to run concurrently


def asset(points, line_id):
    return {"points": [list(p) for p in points], "width": 3.0, "id": line_id}


def densify(edge, steps=EDGE_STEPS):
    return [(edge[0][0] + (edge[1][0] - edge[0][0]) * i / steps,
             edge[0][1] + (edge[1][1] - edge[0][1]) * i / steps)
            for i in range(steps + 1)]


def pattern_lines():
    """What is drawn on the texture board, in board coordinates: the rectangle
    there only marks what maps, so the pattern never follows it. Grid lines
    plus an F: the F has no symmetry, so a turn or flip shows."""
    lines = []
    for x in range(-150, 151, 50):
        lines.append(("grid", [(x, -100), (x, 100)]))
    for y in range(-100, 101, 50):
        lines.append(("grid", [(-150, y), (150, y)]))
    lines.append(("letter", [(-42, 64), (-42, -64), (54, -64)]))
    lines.append(("letter", [(-42, -4), (30, -4)]))
    return lines


def resample(polyline):
    out = [polyline[0]]
    for a, b in zip(polyline, polyline[1:]):
        steps = max(1, int(math.hypot(b[0] - a[0], b[1] - a[1]) / PATTERN_STEP))
        out.extend((a[0] + (b[0] - a[0]) * i / steps, a[1] + (b[1] - a[1]) * i / steps)
                   for i in range(1, steps + 1))
    return out


def solve(request):
    child, main = request["child"], request["main"]
    g, why = pr.guides(child, main, TEXTURE_H, TEXTURE_V)
    if g is None:
        return {"ok": False, "reason": why}
    frame = (g["child"]["h"], g["child"]["v"], g["main"]["h"], g["main"]["v"])
    mode = request.get("mode")
    pairs = []
    if mode in ("edges", "ground"):
        for family in ("h", "v"):
            for ce, me in zip(g["child"]["edges"][family], g["main"]["edges"][family]):
                pairs.append((asset(densify(ce), len(pairs) + 1),
                              asset(densify(me), len(pairs) + 1)))
    info = {}
    map_point, why = am.build_mapper(
        *frame, info, additional_pairs=pairs if mode == "edges" else None,
        ground_pairs=pairs if mode == "ground" else None)
    if map_point is None:
        return {"ok": False, "reason": str(why), "guides": g}

    pattern = []
    for kind, polyline in pattern_lines():
        points = resample(polyline)
        pattern.append({"kind": kind, "child": points, "main": [map_point(p) for p in points]})

    errors = []
    for family in ("h", "v"):
        for ce, me in zip(g["child"]["edges"][family], g["main"]["edges"][family]):
            for c, m in zip(densify(ce), densify(me)):
                p = map_point(c)
                errors.append(math.hypot(p[0] - m[0], p[1] - m[1]))
    a = g["alignment"]
    return {
        "ok": True,
        "guides": g,
        "pattern": pattern,
        "rotation": math.degrees(a["rotation"]),
        "scale": a["scale"],
        "residual": a["residual"],
        "cost": a["cost"],
        "mirrored": info.get("mirrored"),
        "h_mismatch": info.get("h_scale_mismatch"),
        "v_mismatch": info.get("v_scale_mismatch"),
        "contour_mean": sum(errors) / len(errors),
        "contour_max": max(errors),
    }


class Handler(BaseHTTPRequestHandler):
    def _send(self, body, kind):
        self.send_response(200)
        self.send_header("Content-Type", kind)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        with open(os.path.join(HERE, "proxy_lab.html"), "rb") as page:
            self._send(page.read(), "text/html; charset=utf-8")

    def do_POST(self):
        request = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
        try:
            with _SOLVE_LOCK:
                answer = solve(request)
        except Exception as error:      # the bench must survive what it is fed
            answer = {"ok": False, "reason": f"{type(error).__name__}: {error}"}
        self._send(json.dumps(answer).encode("utf-8"), "application/json")

    def log_message(self, *args):
        pass


if __name__ == "__main__":
    print(f"proxy_rect bench on http://127.0.0.1:{PORT}", flush=True)
    ThreadingHTTPServer(("127.0.0.1", PORT), Handler).serve_forever()
