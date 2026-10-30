"""Independent check of the combinatorial merge. Integer guides and integer / half-integer points, so points land
EXACTLY on cell edges and nodes all the time and every node is exactly representable. Truth = exact enumeration with
Fractions: closed cells, guide ends open, one root per distinct parameter point (t = 1 of cell i is t = 0 of the next
cell, across zero-length segments too), keyed with its orient. Compared with solve(extensions="all") and solve_matrix.
Usage: py adversarial_exact.py <path to pyfile dir> [rigs] [seed]"""
import random, sys, os, types, collections
from fractions import Fraction as Fr
sys.path.insert(0, sys.argv[1])
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import chord_intersect as ci

nrigs = int(sys.argv[2]) if len(sys.argv) > 2 else 300
rng = random.Random(int(sys.argv[3]) if len(sys.argv) > 3 else 1)

def guide(n, spread):
    p = [(rng.randint(-spread, spread), rng.randint(-spread, spread))]
    for _ in range(n):
        p.append((p[-1][0] + rng.randint(-spread, spread), p[-1][1] + rng.randint(-spread, spread)))
    return p

def norm_param(k, t, nseg, lens):
    """cell k, parameter t -> canonical position along the guide: the node index when t is 0 or 1 (skipping zero-length
    segments), otherwise (k, t). Outside the guide (extensions) t runs on."""
    if t == 0 or t == 1:
        node = k + (1 if t == 1 else 0)
        while node > 0 and lens[node - 1] == 0:
            node -= 1
        return ('n', node)
    return ('e', k, t)

def truth(h, v, c):
    n_h, n_v = len(h) - 1, len(v) - 1
    r = [(h[i + 1][0] - h[i][0], h[i + 1][1] - h[i][1]) for i in range(n_h)]
    q = [(v[j + 1][0] - v[j][0], v[j + 1][1] - v[j][1]) for j in range(n_v)]
    lr = [abs(x) + abs(y) for x, y in r]
    lq = [abs(x) + abs(y) for x, y in q]
    # the real first / last segment of each guide (a zero-length end segment is culled)
    i_first = next(i for i in range(n_h) if lr[i]); i_last = next(i for i in reversed(range(n_h)) if lr[i])
    j_first = next(j for j in range(n_v) if lq[j]); j_last = next(j for j in reversed(range(n_v)) if lq[j])
    out = set()
    for i in range(n_h):
        for j in range(n_v):
            den = r[i][0] * (-q[j][1]) - r[i][1] * (-q[j][0])
            if den == 0:
                continue
            dx, dy = c[0] - (h[i][0] + v[j][0]), c[1] - (h[i][1] + v[j][1])
            t = Fr(dx * (-q[j][1]) - dy * (-q[j][0])) / den
            u = Fr(dx * r[i][1] - dy * r[i][0]) / den
            if (t < 0 and i != 0) or (t > 1 and i != n_h - 1) or (u < 0 and j != 0) or (u > 1 and j != n_v - 1):
                continue
            o = 1 if -den >= 0 else -1
            out.add((norm_param(i, t, n_h, lr), norm_param(j, u, n_v, lq), o))
    return out

ok_rigs = skipped = points = mismatches = 0
kinds = collections.Counter()
examples = []
while ok_rigs < nrigs:
    h, v = guide(rng.randint(2, 7), 6), guide(rng.randint(2, 7), 6)
    r = [(h[i + 1][0] - h[i][0], h[i + 1][1] - h[i][1]) for i in range(len(h) - 1)]
    q = [(v[j + 1][0] - v[j][0], v[j + 1][1] - v[j][1]) for j in range(len(v) - 1)]
    if any(a[0] * b[1] - a[1] * b[0] == 0 for a in r for b in q):
        skipped += 1            # parallel or zero-length: those cells are culled, the mesh has holes by design
        continue
    ok_rigs += 1
    s = ci.ChordIntersect(h, v, (0.0, 0.0), 0.0, 0.0)
    xs = [a[0] + b[0] for a in h for b in v]; ys = [a[1] + b[1] for a in h for b in v]
    for x2 in range(2 * min(xs) - 4, 2 * max(xs) + 5, rng.choice((1, 2, 3))):
        for y2 in range(2 * min(ys) - 4, 2 * max(ys) + 5, rng.choice((1, 2, 3))):
            c = (Fr(x2, 2), Fr(y2, 2))
            want = truth(h, v, c)
            p = (x2 / 2.0, y2 / 2.0)
            full = s.solve(p, extensions="all")
            matrix = s.solve_matrix(p)
            points += 1
            wn = collections.Counter(o for _, _, o in want)
            for name, res in (("grid", full), ("matrix", matrix)):
                gn = collections.Counter(hit.orient for hit in res.hits)
                if gn != wn:
                    mismatches += 1
                    kinds[(name, res.count - len(want))] += 1
                    if len(examples) < 6:
                        examples.append((name, h, v, p, sorted(map(str, want)), [(x.i, x.j, x.t, x.u, x.orient) for x in res.hits]))
print("rigs", ok_rigs, "(skipped with parallel cells:", skipped, ") points", points, "mismatches", mismatches, dict(kinds))
for e in examples:
    print(e)
