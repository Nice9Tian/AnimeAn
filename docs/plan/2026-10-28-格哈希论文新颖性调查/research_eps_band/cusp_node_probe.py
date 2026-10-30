"""What does the solver report for a query EXACTLY on a cusp corner node, against the regular points around it?
Integer rigs, so the node is exactly representable. Usage: py cusp_node_probe.py <pyfile dir>"""
import collections, math, random, sys, types
sys.path.insert(0, sys.argv[1])
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import chord_intersect as ci

rng = random.Random(7)


def guide(n):
    p = [(rng.randint(-6, 6), rng.randint(-6, 6))]
    for _ in range(n):
        p.append((p[-1][0] + rng.randint(-6, 6), p[-1][1] + rng.randint(-6, 6)))
    return p


def sgn(a, b):
    c = a[0] * b[1] - a[1] * b[0]
    return (c > 0) - (c < 0)


seen = 0
tally = collections.Counter()
shown = 0
for _ in range(4000):
    H, V = guide(rng.randint(2, 6)), guide(rng.randint(2, 6))
    rs = [(b[0] - a[0], b[1] - a[1]) for a, b in zip(H, H[1:])]
    qs = [(b[0] - a[0], b[1] - a[1]) for a, b in zip(V, V[1:])]
    if any(sgn(a, b) == 0 for a in rs for b in qs):
        continue
    s = ci.ChordIntersect(H, V, (0.0, 0.0), 0.0, 0.0)
    for a in range(1, len(H) - 1):
        for b in range(1, len(V) - 1):
            cells = [sgn(rs[a - 1], qs[b - 1]), sgn(rs[a - 1], qs[b]), sgn(rs[a], qs[b - 1]), sgn(rs[a], qs[b])]
            if abs(sum(cells)) != 2:                 # 3:1 = exactly one odd cell = cusp corner
                continue
            node = (float(H[a][0] + V[b][0]), float(H[a][1] + V[b][1]))
            res = s.solve(node, extensions="all")
            at_node = [h.orient for h in res.hits]
            around = set()
            for dx, dy in ((1e-3, 3e-4), (-1e-3, 7e-4), (4e-4, -1e-3), (-6e-4, -9e-4)):
                r = s.solve((node[0] + dx, node[1] + dy), extensions="all")
                around.add((sum(h.orient for h in r.hits), r.count))
            w = {x[0] for x in around}
            if len(w) != 1:
                tally["neighbours disagree on sum"] += 1
                continue
            w = w.pop()
            seen += 1
            # the rows at the node that sit at the corner parameter point itself
            corner = []
            for h in res.hits:
                on_h = (h.i == a and h.t == 0.0) or (h.i == a - 1 and h.t == 1.0)
                on_v = (h.j == b and h.u == 0.0) or (h.j == b - 1 and h.u == 1.0)
                if on_h and on_v:
                    corner.append(h.orient)
            # local degree W = signed angle of the corner's link / 2 pi, round r+, q+, r-, q-
            ring = [rs[a], qs[b], (-rs[a - 1][0], -rs[a - 1][1]), (-qs[b - 1][0], -qs[b - 1][1])]
            ang = sum(math.atan2(ring[k][0] * ring[(k + 1) % 4][1] - ring[k][1] * ring[(k + 1) % 4][0],
                                 ring[k][0] * ring[(k + 1) % 4][0] + ring[k][1] * ring[(k + 1) % 4][1]) for k in range(4))
            W = round(ang / (2 * math.pi))
            tally["|W|=%d, sum - law = %+d" % (abs(W), sum(at_node) - w)] += 1
            tally["|W|=%d, sum - law + W = %+d" % (abs(W), sum(at_node) - w + W)] += 1
            tally["corner rows %s" % sorted(corner)] += 1
            tally["matrix equals grid"] += (sorted(at_node) == sorted(x.orient for x in s.solve_matrix(node).hits))
            if shown < 2:
                shown += 1
                print("example H", H, "V", V, "node", node, "corner", (a, b), "cell orients", cells)
                print("  hits at node:", [(h.i, h.j, h.t, h.u, h.orient) for h in res.hits])
                print("  neighbours (sum, count):", sorted(around))
print("cusp nodes probed", seen)
for k in sorted(tally):
    print(" ", k, tally[k])
