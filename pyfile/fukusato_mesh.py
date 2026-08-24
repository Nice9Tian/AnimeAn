"""Garment-domain triangulation and topology for Fukusato mapping.

The paper operates on a triangulated garment, not on a rectangular image
grid.  This module builds that mesh entirely in Python with Shewchuk's
``triangle`` package.  Garment outlines, holes and manually drawn crease
segments are one planar straight-line graph; after triangulation, vertices on
crease segments are duplicated per local triangle fan so geodesics cannot
cross the fold while an open interior crease still closes at its tip.
"""

from __future__ import annotations

import math

import pydeps

_EPS = 1e-8


def signed_area(ring):
    return 0.5 * sum(a[0] * b[1] - b[0] * a[1]
                     for a, b in zip(ring, ring[1:] + ring[:1]))


def clean_ring(points):
    out = []
    for point in points or []:
        p = (float(point[0]), float(point[1]))
        if not out or math.hypot(p[0] - out[-1][0], p[1] - out[-1][1]) > _EPS:
            out.append(p)
    if len(out) > 1 and math.hypot(out[0][0] - out[-1][0],
                                  out[0][1] - out[-1][1]) <= _EPS:
        out.pop()
    return out


def point_in_ring(point, ring):
    x, y = point
    inside = False
    for a, b in zip(ring, ring[1:] + ring[:1]):
        if ((a[1] > y) != (b[1] > y)):
            cross = (b[0] - a[0]) * (y - a[1]) / (b[1] - a[1]) + a[0]
            if x < cross:
                inside = not inside
    return inside


def point_in_region(point, outer, holes=()):
    return point_in_ring(point, outer) and not any(point_in_ring(point, h) for h in holes)


def _segment_crossing_t(a, b, c, d):
    ax, ay = a
    bx, by = b
    cx, cy = c
    dx, dy = d
    rx, ry = bx - ax, by - ay
    sx, sy = dx - cx, dy - cy
    den = rx * sy - ry * sx
    if abs(den) <= _EPS:
        return None
    qx, qy = cx - ax, cy - ay
    t = (qx * sy - qy * sx) / den
    u = (qx * ry - qy * rx) / den
    if -_EPS <= t <= 1.0 + _EPS and -_EPS <= u <= 1.0 + _EPS:
        return min(1.0, max(0.0, t))
    return None


def clip_polyline_to_region(polyline, outer, holes=()):
    """Return segment pairs from ``polyline`` whose midpoint is in the garment."""
    boundaries = [outer] + list(holes)
    pieces = []
    for a, b in zip(polyline, polyline[1:]):
        ts = [0.0, 1.0]
        for ring in boundaries:
            for c, d in zip(ring, ring[1:] + ring[:1]):
                t = _segment_crossing_t(a, b, c, d)
                if t is not None:
                    ts.append(t)
        ts = sorted(set(round(t, 12) for t in ts))
        for t0, t1 in zip(ts, ts[1:]):
            if t1 - t0 <= _EPS:
                continue
            mid = (0.5 * (t0 + t1))
            probe = (a[0] + (b[0] - a[0]) * mid,
                     a[1] + (b[1] - a[1]) * mid)
            if not point_in_region(probe, outer, holes):
                continue
            p0 = (a[0] + (b[0] - a[0]) * t0,
                  a[1] + (b[1] - a[1]) * t0)
            p1 = (a[0] + (b[0] - a[0]) * t1,
                  a[1] + (b[1] - a[1]) * t1)
            pieces.append((p0, p1))
    return pieces


def _hole_seed(ring):
    """Find a point inside a hole ring without relying on its centroid."""
    # Earcut already ships with AnimeAn for vector fills. The centroid of any
    # triangle in its output is guaranteed to lie inside this simple ring,
    # including a C-shaped hole whose arithmetic centre lies in the opening.
    earcut = pydeps.ensure("mapbox_earcut")
    np = pydeps.ensure("numpy")
    if earcut is not None and np is not None:
        try:
            vertices = np.asarray(ring, dtype=np.float64).reshape(-1, 2)
            ends = np.asarray([len(ring)], dtype=np.uint32)
            indices = earcut.triangulate_float64(vertices, ends)
            if len(indices) >= 3:
                triangle = [ring[int(indices[index])] for index in range(3)]
                return (sum(point[0] for point in triangle) / 3.0,
                        sum(point[1] for point in triangle) / 3.0)
        except Exception:
            pass

    # Last-resort geometric probes for a runtime where the bundled library
    # could not be loaded.
    # Triangle accepts any interior seed.  Probe from every edge midpoint a
    # tiny distance toward the arithmetic centre; one succeeds for ordinary
    # simple polygons, including strongly concave ones.
    centre = (sum(p[0] for p in ring) / len(ring),
              sum(p[1] for p in ring) / len(ring))
    if point_in_ring(centre, ring):
        return centre
    for a, b in zip(ring, ring[1:] + ring[:1]):
        mid = ((a[0] + b[0]) * 0.5, (a[1] + b[1]) * 0.5)
        for amount in (1e-4, 1e-3, 1e-2, 0.05):
            p = (mid[0] + (centre[0] - mid[0]) * amount,
                 mid[1] + (centre[1] - mid[1]) * amount)
            if point_in_ring(p, ring):
                return p
    return ring[0]


class GarmentMesh:
    def __init__(self, points, triangles, cut_edges=(), boundary_vertices=(),
                 base_uv=None, outer=None, holes=()):
        self.P = [tuple(map(float, p)) for p in points]
        self.tris = [list(map(int, tri)) for tri in triangles]
        self.cut_edges = {frozenset(map(int, edge)) for edge in cut_edges}
        self.boundary_vertices = {int(v) for v in boundary_vertices}
        self.base_uv = ([tuple(map(float, p)) for p in base_uv]
                        if base_uv is not None else list(self.P))
        self.outer = clean_ring(outer) if outer else []
        self.holes = [clean_ring(h) for h in holes]
        self._build_index()

    @classmethod
    def triangulate(cls, outer, holes=(), creases=(), max_area=None):
        triangle = pydeps.ensure("triangle")
        if triangle is None:
            raise RuntimeError("the bundled 'triangle' package is unavailable")
        outer = clean_ring(outer)
        cleaned_holes = [clean_ring(h) for h in holes]
        holes = [hole for hole in cleaned_holes if len(hole) >= 3]
        if len(outer) < 3:
            raise ValueError("garment outline needs at least three points")

        vertices = []
        vertex_map = {}
        segments = []
        markers = []

        def vid(point):
            key = (round(float(point[0]), 8), round(float(point[1]), 8))
            if key not in vertex_map:
                vertex_map[key] = len(vertices)
                vertices.append((float(point[0]), float(point[1])))
            return vertex_map[key]

        def add_segment(a, b, marker):
            ia, ib = vid(a), vid(b)
            if ia != ib:
                segments.append((ia, ib))
                markers.append((int(marker),))

        for ring in [outer] + holes:
            for a, b in zip(ring, ring[1:] + ring[:1]):
                add_segment(a, b, 1)
        for crease_id, crease in enumerate(creases, 2):
            clean = [tuple(map(float, p)) for p in crease or []]
            for a, b in clip_polyline_to_region(clean, outer, holes):
                add_segment(a, b, crease_id)

        span = max(max(p[0] for p in outer) - min(p[0] for p in outer),
                   max(p[1] for p in outer) - min(p[1] for p in outer), 1.0)
        if max_area is None:
            max_area = (span / 32.0) ** 2 * 0.5
        payload = {
            "vertices": vertices,
            "segments": segments,
            "segment_markers": markers,
        }
        if holes:
            payload["holes"] = [_hole_seed(h) for h in holes]
        result = triangle.triangulate(payload, f"pDq20a{float(max_area):.8f}")
        if "triangles" not in result or not len(result["triangles"]):
            raise RuntimeError("constrained garment triangulation produced no triangles")

        points = [tuple(map(float, p)) for p in result["vertices"]]
        tris = [list(map(int, tri)) for tri in result["triangles"]]
        output_segments = result.get("segments", [])
        output_markers = result.get("segment_markers", [])
        cut_edges = set()
        boundary_vertices = set()
        for segment, marker_value in zip(output_segments, output_markers):
            marker = int(marker_value[0] if hasattr(marker_value, "__len__") else marker_value)
            edge = frozenset((int(segment[0]), int(segment[1])))
            if marker == 1:
                boundary_vertices.update(edge)
            elif marker >= 2:
                cut_edges.add(edge)

        points, tris, base_uv = _split_cut_vertex_fans(
            points, tris, cut_edges, boundary_vertices)
        return cls(points, tris, (), boundary_vertices, base_uv,
                   outer=outer, holes=holes)

    @classmethod
    def from_dict(cls, data):
        return cls(data["vertices"], data["triangles"],
                   base_uv=data.get("base_uv"), outer=data.get("outer"),
                   holes=data.get("holes") or [])

    def to_dict(self):
        return {
            "vertices": [list(p) for p in self.P],
            "triangles": [list(t) for t in self.tris],
            "base_uv": [list(p) for p in self.base_uv],
            "outer": [list(p) for p in self.outer],
            "holes": [[list(p) for p in h] for h in self.holes],
        }

    def contains(self, x, y):
        return (point_in_region((x, y), self.outer, self.holes)
                if self.outer else self.locate(x, y) is not None)

    def _build_index(self):
        if not self.tris:
            self._x0 = self._y0 = 0.0
            self._bins = {}
            self._cell = 1.0
            return
        xs = [p[0] for p in self.P]
        ys = [p[1] for p in self.P]
        self._x0, self._y0 = min(xs), min(ys)
        span = max(max(xs) - self._x0, max(ys) - self._y0, 1.0)
        self._cell = span / max(4, int(math.sqrt(len(self.tris) / 2.0)))
        self._bins = {}
        for index, tri in enumerate(self.tris):
            pts = [self.P[v] for v in tri]
            x0, x1 = min(p[0] for p in pts), max(p[0] for p in pts)
            y0, y1 = min(p[1] for p in pts), max(p[1] for p in pts)
            for gx in range(math.floor((x0 - self._x0) / self._cell),
                            math.floor((x1 - self._x0) / self._cell) + 1):
                for gy in range(math.floor((y0 - self._y0) / self._cell),
                                math.floor((y1 - self._y0) / self._cell) + 1):
                    self._bins.setdefault((gx, gy), []).append(index)

    @staticmethod
    def _bary(point, a, b, c):
        den = ((b[1] - c[1]) * (a[0] - c[0])
               + (c[0] - b[0]) * (a[1] - c[1]))
        if abs(den) <= 1e-14:
            return None
        w0 = ((b[1] - c[1]) * (point[0] - c[0])
              + (c[0] - b[0]) * (point[1] - c[1])) / den
        w1 = ((c[1] - a[1]) * (point[0] - c[0])
              + (a[0] - c[0]) * (point[1] - c[1])) / den
        return w0, w1, 1.0 - w0 - w1

    def locate(self, x, y):
        gx = math.floor((x - self._x0) / self._cell)
        gy = math.floor((y - self._y0) / self._cell)
        best = None
        for tri_index in self._bins.get((gx, gy), ()):
            tri = self.tris[tri_index]
            bary = self._bary((x, y), *(self.P[v] for v in tri))
            if bary is None:
                continue
            low = min(bary)
            if low >= -1e-8:
                return tuple(tri), bary
            if best is None or low > best[0]:
                best = (low, tuple(tri), bary)
        return (best[1], best[2]) if best and best[0] >= -1e-5 else None

    def tri_of(self, x, y):
        located = self.locate(x, y)
        if located is None:
            return -1
        target = tuple(located[0])
        return next((i for i, tri in enumerate(self.tris) if tuple(tri) == target), -1)

    def uv_at(self, point, uv=None):
        located = self.locate(point[0], point[1])
        if located is None:
            return None
        vertices, bary = located
        field = uv if uv is not None else self.base_uv
        return (sum(w * field[v][0] for v, w in zip(vertices, bary)),
                sum(w * field[v][1] for v, w in zip(vertices, bary)))

    def edge_graph(self):
        adj = [dict() for _ in self.P]
        for tri in self.tris:
            for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
                distance = math.hypot(self.P[a][0] - self.P[b][0],
                                      self.P[a][1] - self.P[b][1])
                previous = adj[a].get(b)
                if previous is None or distance < previous:
                    adj[a][b] = adj[b][a] = distance
        return adj

    def tri_neighbours(self):
        by_edge = {}
        for index, tri in enumerate(self.tris):
            for edge in (frozenset((tri[0], tri[1])),
                         frozenset((tri[1], tri[2])),
                         frozenset((tri[2], tri[0]))):
                by_edge.setdefault(edge, []).append(index)
        result = [set() for _ in self.tris]
        for members in by_edge.values():
            if len(members) == 2:
                a, b = members
                result[a].add(b)
                result[b].add(a)
        return result


def _split_cut_vertex_fans(points, triangles, cut_edges, boundary_vertices):
    """Duplicate crease vertices for each locally disconnected triangle fan."""
    original_points = list(points)
    original_tris = [list(t) for t in triangles]
    incident = {v: [] for v in range(len(points))}
    edge_to_tris = {}
    cut_degree = {v: 0 for v in range(len(points))}
    for ti, tri in enumerate(original_tris):
        for v in tri:
            incident[v].append(ti)
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            edge_to_tris.setdefault(frozenset((a, b)), []).append(ti)
    for edge in cut_edges:
        for v in edge:
            cut_degree[v] = cut_degree.get(v, 0) + 1

    plans = []
    for vertex, degree in cut_degree.items():
        # An open interior tip remains shared.  A tip that reaches the garment
        # boundary must part it, and an interior chain/crossing must split.
        if degree == 0 or (degree == 1 and vertex not in boundary_vertices):
            continue
        members = incident.get(vertex, [])
        links = {t: set() for t in members}
        for edge, edge_members in edge_to_tris.items():
            if vertex not in edge or edge in cut_edges:
                continue
            for a in edge_members:
                for b in edge_members:
                    if a != b and a in links and b in links:
                        links[a].add(b)
        components = []
        unseen = set(members)
        while unseen:
            todo = [unseen.pop()]
            component = set(todo)
            while todo:
                t = todo.pop()
                for other in links[t]:
                    if other in unseen:
                        unseen.remove(other)
                        component.add(other)
                        todo.append(other)
            components.append(component)
        if len(components) > 1:
            plans.append((vertex, components[1:]))

    points = list(original_points)
    triangles = [list(t) for t in original_tris]
    base_uv = list(original_points)
    for vertex, components in plans:
        for component in components:
            duplicate = len(points)
            points.append(original_points[vertex])
            base_uv.append(original_points[vertex])
            for ti in component:
                triangles[ti] = [duplicate if v == vertex else v for v in triangles[ti]]
    return points, triangles, base_uv
