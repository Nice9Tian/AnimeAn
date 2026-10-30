"""Extra probes (fable_band): roots exactly on edges / nodes (with +-k ulp nudges),
near-parallel cells, far extension points, guide-end (sheet / extension) sides,
and the prefilter ablation (v3 with prefilter == v1 without, bit for bit).
usage: py -B probe_extra.py <module>"""
import importlib
import math
import os
import random
import sys
import types

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, "C:/Users/admin/Documents/AnimeAn/pyfile")
sys.path.insert(0, HERE)
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
NAME = sys.argv[1] if len(sys.argv) > 1 else "chord_intersect"
src = open(os.path.join(HERE, "probe_band.py"), encoding="utf-8").read().split("if __name__")[0]
sys.argv = [sys.argv[0], NAME]
src = src.replace('OUT = open(os.path.join(HERE, "probe_detail_%s.txt" % NAME), "w", encoding="utf-8")', 'OUT = open(os.devnull, "w")')
exec(src)                                    # Truth, winding, sin_guide, s_guides, raw_cells, ci = module
import auto_mapping as am  # noqa: E402


def nudges(p, k=2):
    out = []
    for a in range(-k, k + 1):
        for b in range(-k, k + 1):
            x, y = p
            for _ in range(abs(a)):
                x = math.nextafter(x, math.inf if a > 0 else -math.inf)
            for _ in range(abs(b)):
                y = math.nextafter(y, math.inf if b > 0 else -math.inf)
            out.append((x, y))
    return out


def on_lattice(name, H, V):
    """A = hv(arcs) with the arcs exactly at a node or an edge midpoint, plus +-2 ulp nudges of A.
    lost   = no report within 1e-6 of the arcs (the root is missed)
    law    = merged sum(orient) != w
    false/lost-vs-truth as in probe_band (exact mesh truth of the rounded point)."""
    frame = am._Frame(H, V)
    sv = ci.ChordIntersect.from_frame(frame)
    sv.prepare_grid()
    truth = Truth(sv)
    w = winding(sv)
    o = lambda i, j: int(truth.orient[i, j])
    rng = random.Random(1)
    nodes = [(i, j) for i in range(1, sv.n_h) for j in range(1, sv.n_v)]
    nodes = rng.sample(nodes, min(200, len(nodes)))
    stats = {}
    for kind in ("node", "edge-fold", "edge-plain"):
        stats[kind] = dict(points=0, lost=0, law=0, false=0, lost_truth=0)
    items = [("node", i, j, sv.h_cum[i] - sv.h_arc, sv.v_cum[j] - sv.v_arc) for i, j in nodes]
    for i, j in nodes:            # vertical edge (i-1, j) | (i, j) at H node i, V mid of segment j
        if j < sv.n_v:
            kind = "edge-fold" if o(i - 1, j) != o(i, j) else "edge-plain"
            items.append((kind, i, j, sv.h_cum[i] - sv.h_arc, 0.5 * (sv.v_cum[j] + sv.v_cum[j + 1]) - sv.v_arc))
    for kind, i, j, lh, lv in items:
        A = sv.hv(lh, lv)
        for p in nudges(A):
            st = stats[kind]
            st["points"] += 1
            res = sv.solve(p, extensions="all")
            if not any(abs(h.l_h - lh) < 1e-6 and abs(h.l_v - lv) < 1e-6 for h in res.hits):
                st["lost"] += 1
            st["law"] += sum(h.orient for h in res.hits) != w
            rep = {(a, b) for a, b, _, _ in raw_cells(sv, p)}
            tr = truth.cells(p)
            st["false"] += len(rep - tr)
            st["lost_truth"] += len(tr - rep)
    print("%-10s exact-on-lattice roots (+-2 ulp nudges): %s" % (name, "; ".join(
        "%s: %d pts, root missed %d, law %d, false %d, lost-vs-truth %d" % (k, v["points"], v["lost"], v["law"], v["false"], v["lost_truth"])
        for k, v in stats.items())))


def thin_rig():
    """H straight, V a zigzag whose segments alternate between nearly parallel to H
    (sin = 1e-3 .. 1e-9) and steep: thin cells beside ordinary ones."""
    H = [(-200.0 + 400.0 * k / 40, 0.0) for k in range(41)]
    V = [(0.0, -200.0)]
    rng = random.Random(4)
    for k in range(40):
        x, y = V[-1]
        if k % 2:
            V.append((x + 3.0, y + 10.0))
        else:
            sl = 10.0 ** rng.uniform(-9, -3)
            V.append((x - 10.0, y + 10.0 * sl))
    return H, V


def generic(name, H, V, n_edge=300, far=True):
    frame = am._Frame(H, V)
    sv = ci.ChordIntersect.from_frame(frame)
    sv.prepare_grid()
    truth = Truth(sv)
    w = winding(sv)
    rng = random.Random(8)
    law = false = lost = probes = 0
    pts = []
    # near every interior side at many distances
    r, q = sv._r, sv._q
    for _ in range(n_edge):
        if rng.random() < 0.5 and sv.n_h > 1:
            i = rng.randrange(sv.n_h - 1)
            j = rng.randrange(sv.n_v)
            sh, svv, e = sv.h_cum[i + 1] - sv.h_arc, sv.v_cum[j] + rng.random() * (sv.v_cum[j + 1] - sv.v_cum[j]) - sv.v_arc, q[j]
        else:
            i = rng.randrange(sv.n_h)
            j = rng.randrange(max(1, sv.n_v - 1))
            sh, svv, e = sv.h_cum[i] + rng.random() * (sv.h_cum[i + 1] - sv.h_cum[i]) - sv.h_arc, sv.v_cum[j + 1] - sv.v_arc, r[i]
        P = sv.hv(sh, svv)
        n = math.hypot(*e)
        for side in (1, -1):
            for d in (1e-13, 1e-11, 1e-9, 3e-8, 1e-7, 3e-7, 1e-6, 1e-4):
                pts.append((P[0] + side * d * -e[1] / n, P[1] + side * d * e[0] / n))
    if far:
        pts += [(rng.uniform(-1e5, 1e5), rng.uniform(-1e5, 1e5)) for _ in range(500)]
    for p in pts:
        probes += 1
        res = sv.solve(p, extensions="all")
        law += sum(h.orient for h in res.hits) != w
        rep = {(a, b) for a, b, _, _ in raw_cells(sv, p)}
        tr = truth.cells(p)
        false += len(rep - tr)
        lost += len(tr - rep)
    print("%-10s w=%d: %d probes near sides (+far): law %d, false reports %d, lost vs truth %d"
          % (name, w, probes, law, false, lost))
    return sv, pts


def guide_ends(name, H, V):
    """Probes hugging the guide-end lines (sheet / extension boundary): lazy == matrix
    (principal and count_on_sheet), and the on_sheet label agrees with the param rule."""
    frame = am._Frame(H, V)
    sv = ci.ChordIntersect.from_frame(frame)
    sv.prepare_grid()
    rng = random.Random(12)
    bad = n = 0
    for _ in range(400):
        end = rng.choice(("h0", "h1", "v0", "v1"))
        a = rng.uniform(-0.2, 1.2)
        if end[0] == "h":
            lh = (0.0 if end == "h0" else sv.h_cum[-1]) - sv.h_arc
            lv = -sv.v_arc + a * sv.v_cum[-1]
            dvec = sv._r[0] if end == "h0" else sv._r[-1]
        else:
            lv = (0.0 if end == "v0" else sv.v_cum[-1]) - sv.v_arc
            lh = -sv.h_arc + a * sv.h_cum[-1]
            dvec = sv._q[0] if end == "v0" else sv._q[-1]
        P = sv.hv(lh, lv)
        nn = math.hypot(*dvec)
        for d in (-1e-6, -1e-8, -1e-10, 0.0, 1e-10, 1e-8, 1e-6):
            p = (P[0] + d * dvec[0] / nn, P[1] + d * dvec[1] / nn)
            n += 1
            mat, lazy, full = sv.solve_matrix(p), sv.solve(p), sv.solve(p, extensions="all")
            okp = (mat.principal is None) == (lazy.principal is None) and (
                mat.principal is None or (mat.principal.i, mat.principal.j, mat.principal.l_h, mat.principal.l_v)
                == (lazy.principal.i, lazy.principal.j, lazy.principal.l_h, lazy.principal.l_v))
            same = [(h.i, h.j, h.l_h, h.l_v, h.on_sheet) for h in mat.hits] == [(h.i, h.j, h.l_h, h.l_v, h.on_sheet) for h in full.hits]
            if not (okp and same and lazy.count_on_sheet == mat.count_on_sheet):
                bad += 1
    print("%-10s guide-end probes %d: lazy/all/matrix disagreements %d" % (name, n, bad))


if __name__ == "__main__":
    on_lattice("bow100", sin_guide(100.0, "h"), sin_guide(100.0, "v"))
    on_lattice("straight", [(-300.0 + 600.0 * k / 60, 0.0) for k in range(61)], [(0.0, -200.0 + 400.0 * k / 40) for k in range(41)])
    generic("thin", *thin_rig())
    generic("bow100", sin_guide(100.0, "h"), sin_guide(100.0, "v"))
    guide_ends("bow100", sin_guide(100.0, "h"), sin_guide(100.0, "v"))
    guide_ends("S1.5", *s_guides(1.5))
    if NAME == "chord_intersect":
        # prefilter ablation: v1 (exact side tests on every side, no prefilter) vs v3
        v1 = importlib.import_module("chord_intersect_v1_noprefilter")
        rng = random.Random(77)
        for label, (H, V) in (("bow100", (sin_guide(100.0, "h"), sin_guide(100.0, "v"))), ("thin", thin_rig())):
            frame = am._Frame(H, V)
            a, b = ci.ChordIntersect.from_frame(frame), v1.ChordIntersect.from_frame(frame)
            _, pts = generic(label + "*", H, V, n_edge=200, far=True)
            diff = sum(sorted(set(a._pairs_grid(p, "all"))) != sorted(set(b._pairs_grid(p, "all"))) for p in pts)
            print("ablation %s: %d probes, prefilter vs none differ on %d" % (label, len(pts), diff))
    if hasattr(ci, "EXACT_CALLS"):
        print("exact fallbacks taken: %d" % ci.EXACT_CALLS[0])
