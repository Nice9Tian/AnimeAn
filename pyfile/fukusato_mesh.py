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

import auto_mapping as _shared
import pydeps

_EPS = 1e-8

# One home for the polygon predicates (see auto_mapping's docstrings for the
# boundary cases they were hardened against). Re-exported under the names this
# package always used.
signed_area = _shared._signed_area
point_in_ring = _shared._point_in_ring


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


def point_in_region(point, outer, holes=()):
    return point_in_ring(point, outer) and not any(point_in_ring(point, h) for h in holes)


def clip_polyline_to_region(polyline, outer, holes=()):
    """Return segment pairs from ``polyline`` whose midpoint is in the garment.

    The crossing/run machinery is auto_mapping's exact segment clipper: one
    2x2 solve per boundary edge and one midpoint parity test per run. For an
    outer ring with strictly interior, disjoint holes the odd-even parity over
    [outer]+holes equals "inside outer and outside every hole".
    """
    boundaries = [list(outer)] + [list(hole) for hole in holes]
    pieces = []
    for a, b in zip(polyline, polyline[1:]):
        for t0, t1, inside in _shared._clip_runs(a, b, boundaries):
            if not inside:
                continue
            pieces.append(((a[0] + (b[0] - a[0]) * t0, a[1] + (b[1] - a[1]) * t0),
                           (a[0] + (b[0] - a[0]) * t1, a[1] + (b[1] - a[1]) * t1)))
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

    # Fallback for a runtime where the bundled wheel could not be loaded:
    # auto_mapping's interior-point prober (centroid, then edge midpoints
    # nudged inward along the edge normal - hardened against densified rings
    # whose naive midpoints sit exactly ON the boundary).
    return _shared._ring_interior_point(ring)


class GarmentMesh:
    def __init__(self, points, triangles, base_uv=None, outer=None, holes=()):
        # The crease topology lives entirely in the duplicated vertex fans
        # (see _split_cut_vertex_fans); the mesh carries no separate cut-edge
        # record because nothing consults one.
        self.P = [tuple(map(float, p)) for p in points]
        self.tris = [list(map(int, tri)) for tri in triangles]
        self.base_uv = ([tuple(map(float, p)) for p in base_uv]
                        if base_uv is not None else list(self.P))
        self.outer = clean_ring(outer) if outer else []
        self.holes = [clean_ring(h) for h in holes]
        self._adj = None
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
        return cls(points, tris, base_uv=base_uv, outer=outer, holes=holes)

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

    def uv_at(self, point, uv=None):
        located = self.locate(point[0], point[1])
        if located is None:
            return None
        vertices, bary = located
        field = uv if uv is not None else self.base_uv
        return (sum(w * field[v][0] for v, w in zip(vertices, bary)),
                sum(w * field[v][1] for v, w in zip(vertices, bary)))

    def edge_graph(self):
        # Derived purely from P/tris, which never change after construction -
        # memoized because weight previews ask for it on every refresh.
        if self._adj is None:
            adj = [dict() for _ in self.P]
            for tri in self.tris:
                for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
                    distance = math.hypot(self.P[a][0] - self.P[b][0],
                                          self.P[a][1] - self.P[b][1])
                    previous = adj[a].get(b)
                    if previous is None or distance < previous:
                        adj[a][b] = adj[b][a] = distance
            self._adj = adj
        return self._adj

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
