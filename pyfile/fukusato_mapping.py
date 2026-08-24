"""Faithful reproduction of Fukusato et al., "Interactive texture editing for
garment line drawings", Computer Animation and Virtual Worlds 33(6):e2117,
2022. doi:10.1002/cav.2117

THE PAPER'S PIPELINE, AND WHERE EACH PIECE LIVES HERE
-----------------------------------------------------
The active paper-facing UI and garment-mesh pipeline live in
``fukusato_workflow.py`` and ``fukusato_mesh.py``.  This module owns the MLS,
geodesic-weight and exact-emission core.  The former rectangular two-board
workflow was removed; only the ``Mesh`` grid remains, as the emission core's
headless test fixture.

The paper edits the UV COORDINATES of a garment line drawing on the modeling
panel. Concretely (Sec. 3):

  (a) the input drawing carries UV coordinates assigned in advance ("using,
      for example, a plane projection method");
  (b) "To handle garment wrinkles, the 2D garment models are cut along the
      crease lines in the input drawings" - the triangulated model ON THE
      MODELING PANEL is cut, with the two sides topologically separated;
  (c) the user places curve/point handles on the drawing; the system projects
      them into UV space by BARYCENTRIC coordinates -> p_i(t);
  (d) the user drags a handle to v'_i(t); its UV coordinates q_i(t) are
      computed the same way;
  (e) the UV field is deformed with Schaefer's MLS, RIGID variant, fitted in
      the direction written in Eq. (1):
          argmin_M  sum_i integral w_i(t) |(q_i(t)-q*)M - (p_i(t)-p*)|^2 dt
      i.e. M maps the AFTER handle (q) onto the BEFORE handle (p) - the
      BACKWARD map. Every mesh vertex's UV is pulled through it, so the
      texture content that used to sit under the handle re-appears under the
      handle's new position. The integral is discretized by sampling
      "several points uniformly distributed on each curve segment" (Sec 4.1);
  (f) w_i(t) starts as a GEODESIC distance on the cut panel mesh and is
      linearly blended with the Euclidean distance by alpha (Sec. 4.2):
          w_i = (1-alpha) w_i^g + alpha w_i^e.

In AnimeAn's active workflow:

  main_paint_view  = the MODELING PANEL. The garment drawing, separately
                     authored crease cuts, curve/point handles and mapped
                     output all live here. ``fukusato_mesh.GarmentMesh`` is a
                     constrained Delaunay triangulation of the detected
                     garment silhouette (including holes), and duplicates the
                     local triangle fans along exact crease constraints.
  child_paint_view = the TEXTURE / UV SPACE. Pattern strokes live here; the
                     same mesh and current UV field are shown by Weight Preview
                     and Triangle Topology overlays.

  The initial UV assignment is the PLANE PROJECTION explicitly allowed by the
  paper: every panel vertex starts at its own 2D coordinate. The user draws a
  point or curve handle on MAIN, then drags that overlay from its BEFORE state
  to its AFTER state. Both states are projected barycentrically through the
  garment mesh. A check/x pair is the transaction boundary: the deformation
  and output replacement happen only on check; x discards the pending handle
  (or rolls an edit of an accepted handle back).

  A single click is a POINT handle and a drawn stroke is a CURVE handle. Curve
  samples are uniformly spaced by arc length as required by Sec. 4.1. Multiple
  accepted handles form one MLS solve, and editing an accepted handle creates
  another pending transaction rather than changing the artwork immediately.

EMISSION (how "deformed UV coordinates" become vector artwork)
----------------------------------------------------------------
The paper renders a raster texture: every panel point x shows the texture at
UV(x). The vector analogue emitted here: for every mesh triangle, its three
deformed UVs span a triangle in texture space; the pattern strokes clipped
to that UV triangle are carried onto the panel triangle by the affine map
between them. Both the clipping and the map are exact (the UV field is
piecewise affine by construction), so there are no probes, tolerances or
depth caps. Pieces are chained across triangles that share an edge; a cut
edge is NOT shared (its endpoints were duplicated), so a stroke crossing a
crease breaks into separate strokes whose images separate - the tear the
paper's Sec. 3 cut exists to produce. Pieces whose triangle flips
orientation (det J < 0, texture seen from the back at a fold-over) carry
property "fukusato_mapped_back" instead, so scripts can restyle or hide the
back side; geometry is unchanged.

``fukusato_workflow`` applies the same triangle-by-triangle affine map to
odd-even vector fills after triangulating their outlines and nested holes.

IMPLEMENTATION CHOICES (documented like the paper's own deferrals)
------------------------------------------------------------------
  * Geodesic distances use multi-source Dijkstra over the constrained
    triangle edge graph, not the heat method [28]. They are monotone,
    silhouette-aware and cannot cross a duplicated crease fan.
  * Handle curves are the artist's own pen strokes, not kappa-Curves [26]
    through clicked points. The handle's shape never enters the deformation
    algebra - only its sample positions do (the paper says the same of its
    kappa-Curves).
  * The paper defers the initial UV estimate to Hashimoto et al. 2020 and
    also names plane projection; only plane projection is implemented, so
    "before" handles are exact UV positions rather than barycentric
    projections through a non-trivial field.
  * Stroke width is scaled by a single global magnification estimate (total
    panel edge length over total UV edge length). Rigid gives ~1 by
    construction; similarity makes the width follow the requested scale. The
    true magnification varies per triangle; a per-piece width is not
    attempted.

The toolbar entries are routed through ``fukusato_workflow``; this module has
no hooks or entry points of its own.
"""

import heapq
import math

FUKUSATO_TOOL = "fukusato_guide_mapping"
MAPPED_PROPERTY = "fukusato_mapped"
BACK_PROPERTY = "fukusato_mapped_back"
MAPPED_LAYER_NAME = "fukusato layer"

POLY_STEP = 4.0

# alpha  : Sec 4.2 blend. 0 = pure geodesic (cuts fully felt), 1 = Euclidean.
# beta   : weight falloff exponent, w = 1/d^(2*beta) (Schaefer's form; the
#          paper does not pin the falloff).
# grid   : cells per side of the panel mesh.
# samples: control points taken from the LONGEST handle; shorter handles get
#          proportionally fewer (arc-length spacing), a dot gets one.
# variant: "rigid" is the paper's shipped deformer (Sec. 7.2 names similarity
#          as future work; it is provided as the planned switch).
_OPTIONS = {"alpha": 0.0, "beta": 2.0, "grid": 32, "samples": 16,
            "variant": "rigid"}
_EPS = 1e-6


def options():
    return dict(_OPTIONS)


def _animean():
    import animean_python
    return animean_python


def _scene_model(view_name):
    """Resolve a view's SceneModel (same lookup auto_mapping/repulsion use)."""
    import __main__

    model = getattr(__main__, f"{view_name}_model", None)
    if model is not None:
        return model

    wanted = f"{view_name}_paint_view"
    for info in _animean().get_scene():
        if info.get("sceneName") == wanted:
            return info["scene"]
    raise RuntimeError(f"scene for view '{view_name}' is not registered")


# ---------------------------------------------------------------------------
# stroke collection
# ---------------------------------------------------------------------------

def _stroke_polylines(stroke):
    result = []
    for polyline in stroke.get("polylines") or []:
        pts = [(float(p["x"]), float(p["y"])) for p in polyline]
        if len(pts) >= 2:
            result.append(pts)
    if not result:
        # A dot stroke (the paper's POINT handle) flattens to nothing; its
        # raw input points still carry the position.
        raw = [(float(p["x"]), float(p["y"])) for p in (stroke.get("raw_points") or [])]
        if raw:
            result.append(raw)
    return result


def _collect(scene, frame, wanted_property=None, exclude=()):
    """Flattened polylines of the frame's strokes, filtered by property.

    wanted_property=None means "everything not in `exclude`" (the pattern).
    """
    out = []
    structure = scene.get_structure()
    if frame < 0 or frame >= structure["frame_count"]:
        return out
    layers = structure["layers"]
    # A mapped result is flattened into one output image. AnimeAn paints
    # layer 0 last (on top), while strokes inside one image paint in append
    # order, so pattern content has to be collected bottom-to-top. Handle
    # pairing keeps the historical top-to-bottom order.
    if wanted_property is None:
        layers = reversed(layers)
    for layer in layers:
        if not layer["visible"] or layer["type"] == "fill" or layer.get("internal"):
            continue
        cell = scene.cell_to_dict(layer["index"], frame, True, POLY_STEP)
        for stroke in cell["image"]["strokes"]:
            prop = stroke.get("property") or ""
            if wanted_property is not None:
                if prop != wanted_property:
                    continue
            elif prop in exclude:
                continue
            for poly in _stroke_polylines(stroke):
                out.append({"points": poly, "stroke": stroke})
    return out


def _length(points):
    return sum(math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(points, points[1:]))


def _resample(points, count):
    """Arc-length uniform resampling to `count` points (count=1: midpoint).

    This is Sec. 4.1's discretization of the integral in Eq. (1): "we sample
    several points uniformly distributed on each curve segment". Callers pass
    a count proportional to arc length so a long handle carries more control
    points than a short one; a POINT handle passes 1.
    """
    if not points:
        return []
    if count <= 1 or len(points) < 2:
        return [points[len(points) // 2]]
    cum = [0.0]
    for a, b in zip(points, points[1:]):
        cum.append(cum[-1] + math.hypot(b[0] - a[0], b[1] - a[1]))
    total = cum[-1]
    if total <= 1e-9:
        return [points[0]] if count <= 1 else [points[0], points[-1]]
    out = []
    j = 0
    for k in range(count):
        target = total * k / (count - 1)
        while j + 2 < len(cum) and cum[j + 1] < target:
            j += 1
        span = cum[j + 1] - cum[j]
        t = 0.0 if span <= 1e-12 else (target - cum[j]) / span
        out.append((points[j][0] + (points[j + 1][0] - points[j][0]) * t,
                    points[j][1] + (points[j + 1][1] - points[j][1]) * t))
    return out


# ---------------------------------------------------------------------------
# the modeling-panel mesh, cut along the crease lines  (paper Sec. 3)
# ---------------------------------------------------------------------------

class Mesh:
    """Regular triangulated grid over [x0,x1]x[y0,y1], cuttable along creases.

    Vertex (i, j) starts as index j*(n+1)+i; cutting appends duplicates, so
    triangles hold explicit vertex ids. Cell (i, j) is split by the diagonal
    (i,j)-(i+1,j+1) into the LOWER triangle [(i,j),(i+1,j),(i+1,j+1)] (id
    2*(j*n+i)) and the UPPER [(i,j),(i+1,j+1),(i,j+1)] (id 2*(j*n+i)+1), so
    point location stays O(1) after any number of cuts (duplicates keep the
    original positions; only connectivity changes).

    apply_cut() realizes the paper's "cut along the crease lines ... the two
    sides are topologically separated": the crease polyline is snapped onto a
    chain of mesh edges, every INTERIOR chain vertex whose triangle fan has
    triangles on both sides is duplicated, and the far side's triangles are
    retargeted to the copy. Chain endpoints stay shared, so an open crease
    tapers closed at its ends exactly like the reference construction.
    """

    def __init__(self, x0, y0, x1, y1, n):
        self.n = max(2, int(n))
        self.x0, self.y0 = float(x0), float(y0)
        self.hx = (float(x1) - self.x0) / self.n
        self.hy = (float(y1) - self.y0) / self.n
        if self.hx <= 0.0 or self.hy <= 0.0:
            raise ValueError("degenerate mesh bounds")
        self.P = [(self.x0 + i * self.hx, self.y0 + j * self.hy)
                  for j in range(self.n + 1) for i in range(self.n + 1)]
        self.tris = []
        for j in range(self.n):
            for i in range(self.n):
                a = self._grid_vid(i, j)
                b = self._grid_vid(i + 1, j)
                c = self._grid_vid(i + 1, j + 1)
                d = self._grid_vid(i, j + 1)
                self.tris.append([a, b, c])   # lower
                self.tris.append([a, c, d])   # upper
        # grid vertex id -> ids of the duplicates a previous cut created for
        # it. A later cut must split EVERY sheet passing through a grid
        # point, not just the original one (crossing creases).
        self._copies = {}

    def _grid_vid(self, i, j):
        return j * (self.n + 1) + i

    def cell_of(self, x, y):
        u = (x - self.x0) / self.hx
        v = (y - self.y0) / self.hy
        i = min(self.n - 1, max(0, int(math.floor(u))))
        j = min(self.n - 1, max(0, int(math.floor(v))))
        return i, j, min(1.0, max(0.0, u - i)), min(1.0, max(0.0, v - j))

    def contains(self, x, y):
        return (self.x0 <= x <= self.x0 + self.n * self.hx
                and self.y0 <= y <= self.y0 + self.n * self.hy)

    def tri_of(self, x, y):
        """Triangle id containing (x, y). O(1)."""
        i, j, u, v = self.cell_of(x, y)
        base = 2 * (j * self.n + i)
        return base if v <= u else base + 1

    def locate(self, x, y):
        """(vertex id triple, barycentric triple). O(1); positions of
        duplicates equal the originals, so the math is cut-agnostic."""
        i, j, u, v = self.cell_of(x, y)
        base = 2 * (j * self.n + i)
        if v <= u:
            return tuple(self.tris[base]), (1.0 - u, u - v, v)
        return tuple(self.tris[base + 1]), (1.0 - v, u, v - u)

    # -- cutting ------------------------------------------------------------

    def _snap_chain(self, polyline):
        """The crease polyline as a chain of grid (i, j) coords whose
        consecutive entries differ by one MESH edge (axis step or the main
        diagonal)."""
        step = 0.5 * min(self.hx, self.hy)
        samples = _resample(polyline, max(2, int(math.ceil(_length(polyline) / step)) + 1))
        snapped = []
        for x, y in samples:
            i = min(self.n, max(0, int(round((x - self.x0) / self.hx))))
            j = min(self.n, max(0, int(round((y - self.y0) / self.hy))))
            if not snapped or snapped[-1] != (i, j):
                snapped.append((i, j))
        chain = []
        for target in snapped:
            if not chain:
                chain.append(target)
                continue
            ci, cj = chain[-1]
            ti, tj = target
            while (ci, cj) != (ti, tj):
                di = 0 if ti == ci else (1 if ti > ci else -1)
                dj = 0 if tj == cj else (1 if tj > cj else -1)
                if di != 0 and dj != 0 and di == dj:
                    ci += di
                    cj += dj      # the main diagonal IS a mesh edge
                elif di != 0:
                    ci += di      # anti-diagonal moves go axis-first
                else:
                    cj += dj
                chain.append((ci, cj))
        return chain

    @staticmethod
    def _side_of_chain(points, pt):
        """Sign of `pt` against its nearest segment of the chain polyline."""
        best_d = math.inf
        best_s = 0.0
        for a, b in zip(points, points[1:]):
            ax, ay = a
            bx, by = b
            dx, dy = bx - ax, by - ay
            L2 = dx * dx + dy * dy
            if L2 <= 0.0:
                continue
            t = max(0.0, min(1.0, ((pt[0] - ax) * dx + (pt[1] - ay) * dy) / L2))
            px, py = ax + dx * t, ay + dy * t
            d = (pt[0] - px) ** 2 + (pt[1] - py) ** 2
            if d < best_d - 1e-12:
                best_d = d
                best_s = dx * (pt[1] - ay) - dy * (pt[0] - ax)
        return best_s

    def apply_cut(self, polyline):
        """Cut the mesh along `polyline`. Returns the number of duplicated
        vertices (0 = the crease did not separate anything).

        Interior chain vertices are split; endpoints are split too when the
        chain is CLOSED (a crease loop must isolate its inside - leaving the
        seam vertex shared would keep a pinhole) or when the endpoint lies on
        the mesh border (a crease running off the garment must part it, not
        taper). An open interior end stays shared so the tear tapers closed,
        as in the reference construction. Each grid point is processed
        together with every duplicate an earlier cut created for it, so
        crossing creases split all sheets, independent of the order the cuts
        are applied in.
        """
        chain = self._snap_chain(polyline)
        if len(chain) < 3:
            return 0
        closed = chain[0] == chain[-1]
        chain_pts = [(self.x0 + i * self.hx, self.y0 + j * self.hy) for i, j in chain]

        incident = {}
        for t, tri in enumerate(self.tris):
            for v in tri:
                incident.setdefault(v, []).append(t)

        def on_border(i, j):
            return i == 0 or j == 0 or i == self.n or j == self.n

        indices = list(range(1, len(chain) - 1))
        if closed:
            indices.append(0)     # the seam vertex, once (chain[-1] == chain[0])
        else:
            for k in (0, len(chain) - 1):
                if on_border(*chain[k]):
                    indices.append(k)

        duplicated = 0
        seen = set()
        for k in indices:
            grid_v = self._grid_vid(*chain[k])
            if grid_v in seen:
                continue          # a chain revisiting a vertex is cut once
            seen.add(grid_v)
            # Split every sheet through this grid point: the original vertex
            # and every duplicate an earlier cut left here.
            for v in [grid_v] + self._copies.get(grid_v, []):
                left = []
                right = []
                for t in incident.get(v, ()):
                    cx = sum(self.P[w][0] for w in self.tris[t]) / 3.0
                    cy = sum(self.P[w][1] for w in self.tris[t]) / 3.0
                    if self._side_of_chain(chain_pts, (cx, cy)) < 0.0:
                        right.append(t)
                    else:
                        left.append(t)
                if not left or not right:
                    continue      # this sheet's fan is entirely on one side
                copy = len(self.P)
                self.P.append(self.P[v])
                self._copies.setdefault(grid_v, []).append(copy)
                duplicated += 1
                for t in right:
                    self.tris[t] = [copy if w == v else w for w in self.tris[t]]
                    incident.setdefault(copy, []).append(t)
        return duplicated

    # -- graphs -------------------------------------------------------------

    def edge_graph(self):
        """Dijkstra adjacency from the (possibly cut) triangles.

        Triangle edges only connect vertices that still share a triangle, so
        a cut is a hard topological barrier, not a severed shortcut. The
        anti-diagonal of a cell is added purely to halve the sqrt(2) grid
        anisotropy - and only when the cell's two triangles still share their
        diagonal vertices, so it can never bridge a cut.
        """
        adj = [dict() for _ in range(len(self.P))]

        def connect(u, v):
            w = math.hypot(self.P[u][0] - self.P[v][0], self.P[u][1] - self.P[v][1])
            adj[u][v] = w
            adj[v][u] = w

        for tri in self.tris:
            connect(tri[0], tri[1])
            connect(tri[1], tri[2])
            connect(tri[2], tri[0])
        for j in range(self.n):
            for i in range(self.n):
                base = 2 * (j * self.n + i)
                lower = self.tris[base]
                upper = self.tris[base + 1]
                if lower[0] == upper[0] and lower[2] == upper[1]:
                    connect(lower[1], upper[2])
        return adj

    def tri_neighbours(self):
        """triangle id -> triangles sharing a (non-cut) edge with it."""
        by_edge = {}
        for t, tri in enumerate(self.tris):
            for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
                by_edge.setdefault(frozenset((a, b)), []).append(t)
        neigh = [set() for _ in self.tris]
        for members in by_edge.values():
            for t in members:
                for s in members:
                    if s != t:
                        neigh[t].add(s)
        return neigh


# ---------------------------------------------------------------------------
# geodesic distance on the cut mesh  (refs 27, 28 -> Dijkstra here)
# ---------------------------------------------------------------------------

def geodesic_from(mesh, adj, point):
    """Dijkstra seeded at the vertices of the triangle containing `point`,
    initialised with the true euclidean distance to it."""
    dist = [math.inf] * len(mesh.P)
    pq = []
    verts, _bary = mesh.locate(point[0], point[1])
    for v in verts:
        d0 = math.hypot(mesh.P[v][0] - point[0], mesh.P[v][1] - point[1])
        if d0 < dist[v]:
            dist[v] = d0
            heapq.heappush(pq, (d0, v))
    while pq:
        d, u = heapq.heappop(pq)
        if d > dist[u] + 1e-15:
            continue
        for v, w in adj[u].items():
            nd = d + w
            if nd < dist[v] - 1e-12:
                dist[v] = nd
                heapq.heappush(pq, (nd, v))
    return dist


# ---------------------------------------------------------------------------
# MLS, rigid + similarity  (Schaefer et al. 2006; Eq. (1) of the paper)
# ---------------------------------------------------------------------------
# Complex form, algebraically identical to the matrix one:
#     f(v) = q* + (v - p*) * S / |S|      (rigid)
#     f(v) = q* + (v - p*) * S / mu       (similarity)
#     S = sum_i w_i * q_hat_i * conj(p_hat_i),  mu = sum_i w_i |p_hat_i|^2
# The paper's Eq. (1) direction is realized by CALLING this with src = the
# after-handle UVs (q) and dst = the before-handle UVs (p): the fitted map
# then sends the after state onto the before state, and pulling every mesh
# vertex's UV through it makes the texture content follow the handles.

def mls_deform(src, dst, weights, query, variant="rigid"):
    """src/dst: lists of complex control points. weights: list of floats."""
    total = 0.0
    for w in weights:
        total += w
    if not (total > 0.0) or not math.isfinite(total):
        return query

    p_star = 0j
    q_star = 0j
    for w, p, q in zip(weights, src, dst):
        p_star += w * p
        q_star += w * q
    p_star /= total
    q_star /= total

    vp = query - p_star
    acc = 0j
    mu = 0.0
    for w, p, q in zip(weights, src, dst):
        ph = p - p_star
        qh = q - q_star
        acc += w * qh * ph.conjugate()
        mu += w * (ph.real * ph.real + ph.imag * ph.imag)

    if variant == "similarity":
        if mu <= 1e-18:
            return q_star + vp
        return q_star + vp * (acc / mu)

    scale = abs(acc)
    if scale <= 1e-18:
        return q_star + vp
    return q_star + vp * (acc / scale)


# ---------------------------------------------------------------------------
# weights: geodesic / euclidean blend  (paper Sec. 4.2)
# ---------------------------------------------------------------------------

def build_weights(mesh, adj, samples, alpha, beta, measures=None):
    """weights[vertex][sample] = (1-alpha)*w_geodesic + alpha*w_euclidean.

    `samples` are the AFTER handle positions on the panel - the domain side
    of Eq. (1)'s fit (M maps q onto p), which is what keeps the moving least
    squares interpolating: the weight must blow up where the DOMAIN control
    point sits, so f(q_i) = p_i. Seeding at the before positions instead
    loses that property with the error growing with drag distance (measured:
    a 250 px drag across a crease only moved the texture 22 px).

    `measures` (optional, one per sample) realizes Eq. (1)'s per-handle
    integral over t in [0,1]: every handle carries UNIT measure regardless
    of its arc length, so a sample from an N-sample curve weighs 1/N and a
    point handle's single sample weighs 1.
    """
    if measures is not None and len(measures) != len(samples):
        raise ValueError("measures must contain one value per handle sample")
    power = 2.0 * beta
    n_v = len(mesh.P)
    weights = [[0.0] * len(samples) for _ in range(n_v)]
    for k, s in enumerate(samples):
        m = measures[k] if measures is not None else 1.0
        if not math.isfinite(m) or m < 0.0:
            raise ValueError("handle sample measures must be finite and non-negative")
        geo = geodesic_from(mesh, adj, s) if alpha < 1.0 else None
        for v in range(n_v):
            px, py = mesh.P[v]
            de = math.hypot(px - s[0], py - s[1])
            we = 1.0 / (max(de, _EPS) ** power)
            if geo is None:
                weights[v][k] = m * we
                continue
            dg = geo[v]
            wg = 0.0 if not math.isfinite(dg) else 1.0 / (max(dg, _EPS) ** power)
            weights[v][k] = m * ((1.0 - alpha) * wg + alpha * we)

    # With alpha=0, a component topologically disconnected from every handle
    # intentionally has an all-zero row. mls_deform then leaves that component
    # unchanged. Substituting Euclidean weights here would silently cross the
    # very crease barrier that the paper's geodesic weighting is meant to
    # preserve; alpha>0 is the explicit control for allowing that influence.
    return weights


# ---------------------------------------------------------------------------
# exact emission: texture strokes through the deformed UV field
# ---------------------------------------------------------------------------

def _tri_geometry(mesh, uv, t):
    """(uv corners, panel corners, signed uv area) of triangle t."""
    va, vb, vc = mesh.tris[t]
    ua, ub, uc = uv[va], uv[vb], uv[vc]
    area2 = ((ub[0] - ua[0]) * (uc[1] - ua[1])
             - (ub[1] - ua[1]) * (uc[0] - ua[0]))
    return (ua, ub, uc), (mesh.P[va], mesh.P[vb], mesh.P[vc]), area2


def _clip_segment_to_uv_tri(a, b, corners, area2):
    """[t0, t1] of segment a->b inside the (oriented) uv triangle, or None."""
    sgn = 1.0 if area2 > 0.0 else -1.0
    t0, t1 = 0.0, 1.0
    dx, dy = b[0] - a[0], b[1] - a[1]
    for k in range(3):
        ex, ey = corners[k]
        fx, fy = corners[(k + 1) % 3]
        nx, ny = -(fy - ey) * sgn, (fx - ex) * sgn   # inward normal
        num = nx * (a[0] - ex) + ny * (a[1] - ey)
        den = nx * dx + ny * dy
        if abs(den) < 1e-15:
            if num < 0.0:
                return None
            continue
        t_hit = -num / den
        if den > 0.0:
            t0 = max(t0, t_hit)
        else:
            t1 = min(t1, t_hit)
        if t0 >= t1 - 1e-13:
            return None
    return (t0, t1)


def _affine_uv_to_panel(pt, corners_uv, corners_panel, area2):
    """Map a UV point through the triangle's exact affine (barycentric)."""
    ua, ub, uc = corners_uv
    w1 = ((ub[0] - pt[0]) * (uc[1] - pt[1]) - (ub[1] - pt[1]) * (uc[0] - pt[0])) / area2
    w2 = ((uc[0] - pt[0]) * (ua[1] - pt[1]) - (uc[1] - pt[1]) * (ua[0] - pt[0])) / area2
    w3 = 1.0 - w1 - w2
    xa, xb, xc = corners_panel
    return (w1 * xa[0] + w2 * xb[0] + w3 * xc[0],
            w1 * xa[1] + w2 * xb[1] + w3 * xc[1])


class _UvIndex:
    """Uniform spatial bin over the UV triangles' bounding boxes."""

    def __init__(self, mesh, uv):
        self.entries = []          # (bbox, tri id, corners_uv, corners_panel, area2)
        xs = []
        ys = []
        for t in range(len(mesh.tris)):
            corners_uv, corners_panel, area2 = _tri_geometry(mesh, uv, t)
            if abs(area2) < 1e-12:
                continue           # degenerate: collapsed by the deformation
            bx0 = min(c[0] for c in corners_uv)
            bx1 = max(c[0] for c in corners_uv)
            by0 = min(c[1] for c in corners_uv)
            by1 = max(c[1] for c in corners_uv)
            self.entries.append(((bx0, by0, bx1, by1), t, corners_uv, corners_panel, area2))
            xs += [bx0, bx1]
            ys += [by0, by1]
        if not self.entries:
            self.cell = 1.0
            self.bins = {}
            return
        self.x0 = min(xs)
        self.y0 = min(ys)
        spans = [(e[0][2] - e[0][0]) + (e[0][3] - e[0][1]) for e in self.entries]
        self.cell = max(1e-6, sum(spans) / (2.0 * len(spans)))
        self.bins = {}
        self.gx_min = self.gy_min = math.inf
        self.gx_max = self.gy_max = -math.inf
        for entry in self.entries:
            bx0, by0, bx1, by1 = entry[0]
            gx0 = math.floor((bx0 - self.x0) / self.cell)
            gx1 = math.floor((bx1 - self.x0) / self.cell)
            gy0 = math.floor((by0 - self.y0) / self.cell)
            gy1 = math.floor((by1 - self.y0) / self.cell)
            self.gx_min, self.gx_max = min(self.gx_min, gx0), max(self.gx_max, gx1)
            self.gy_min, self.gy_max = min(self.gy_min, gy0), max(self.gy_max, gy1)
            for gx in range(gx0, gx1 + 1):
                for gy in range(gy0, gy1 + 1):
                    self.bins.setdefault((gx, gy), []).append(entry)

    def candidates(self, ax, ay, bx, by):
        if not self.bins:
            return
        x0, x1 = (ax, bx) if ax <= bx else (bx, ax)
        y0, y1 = (ay, by) if ay <= by else (by, ay)
        gx0 = max(int(self.gx_min), math.floor((x0 - self.x0) / self.cell))
        gx1 = min(int(self.gx_max), math.floor((x1 - self.x0) / self.cell))
        gy0 = max(int(self.gy_min), math.floor((y0 - self.y0) / self.cell))
        gy1 = min(int(self.gy_max), math.floor((y1 - self.y0) / self.cell))
        if gx0 > gx1 or gy0 > gy1:
            return
        seen = set()
        for gx in range(gx0, gx1 + 1):
            for gy in range(gy0, gy1 + 1):
                for entry in self.bins.get((gx, gy), ()):
                    if entry[1] in seen:
                        continue
                    seen.add(entry[1])
                    bb = entry[0]
                    if bb[2] < x0 or bb[0] > x1 or bb[3] < y0 or bb[1] > y1:
                        continue
                    yield entry


def emit_pattern(mesh, uv, pattern_polyline, neighbours, index=None):
    """The polyline's image(s) on the panel: [(points, is_back), ...].

    Every polyline segment is clipped (exactly) against every UV triangle it
    touches; pieces are chained while consecutive pieces sit in the SAME or
    EDGE-ADJACENT triangles with the same orientation sign. Adjacency is by
    shared vertex ids, so a crease cut (duplicated ids) breaks the chain and
    the stroke genuinely tears; an orientation flip (fold-over) also breaks
    the chain so front and back become separate strokes.

    Pass a prebuilt _UvIndex when emitting many strokes through one field -
    building it dominates the cost otherwise.
    """
    if index is None:
        index = _UvIndex(mesh, uv)
    if not pattern_polyline:
        return []
    if len(pattern_polyline) == 1:
        point = pattern_polyline[0]
        claims = []
        for entry in index.candidates(point[0], point[1], point[0], point[1]):
            _bounds, tri, corners_uv, corners_panel, area2 = entry
            ua, ub, uc = corners_uv
            bary = (
                ((ub[0] - point[0]) * (uc[1] - point[1])
                 - (ub[1] - point[1]) * (uc[0] - point[0])) / area2,
                ((uc[0] - point[0]) * (ua[1] - point[1])
                 - (uc[1] - point[1]) * (ua[0] - point[0])) / area2,
            )
            bary = bary + (1.0 - bary[0] - bary[1],)
            if min(bary) < -1e-9:
                continue
            mapped = _affine_uv_to_panel(point, corners_uv, corners_panel, area2)
            back = area2 < 0.0
            # A point on a shared edge/vertex is claimed by several triangles
            # of one sheet. Their affine images agree, so one dot is enough;
            # overlapping UV sheets with different panel images still survive.
            if any(other_back == back and math.dist(mapped, other_point) <= 1e-9
                   for other_point, other_back, _other_tri in claims):
                continue
            claims.append((mapped, back, tri))
        return [([mapped], back) for mapped, back, _tri in claims]
    pieces = []                    # (seg index, t0, t1, tri, entry)
    for k, (a, b) in enumerate(zip(pattern_polyline, pattern_polyline[1:])):
        for entry in index.candidates(a[0], a[1], b[0], b[1]):
            span = _clip_segment_to_uv_tri(a, b, entry[2], entry[4])
            if span is None:
                continue
            pieces.append((k, span[0], span[1], entry[1], entry))
    pieces.sort(key=lambda piece: (piece[0], piece[1], piece[2]))

    def seg_point(k, t):
        a = pattern_polyline[k]
        b = pattern_polyline[k + 1]
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

    # A span lying exactly ON a triangle edge can be claimed by two triangles
    # whose affine images agree - a shared edge, and equally a CREASE edge,
    # whose duplicated fan vertices carry identical panel coordinates. Either
    # copy is the same geometry, so the duplicate is dropped rather than
    # emitting double ink. Equal t ranges alone are NOT enough: two folded
    # sheets may legitimately overlap the same UV span with DIFFERENT panel
    # images, and both must survive - the image comparison below, not any
    # adjacency test, is what decides (adjacency would wrongly re-admit the
    # crease-edge duplicate, whose triangles share no vertex ids). Opposite
    # ORIENTATIONS are never duplicates even when their images coincide: the
    # back-facing copy is a separately-tagged object (BACK_PROPERTY) that the
    # user styles or hides, exactly as the point-handle branch above keeps it.
    deduped_pieces = []
    for piece in pieces:
        if deduped_pieces:
            prev = deduped_pieces[-1]
            same_span = (piece[0] == prev[0]
                    and abs(piece[1] - prev[1]) <= 1e-9
                    and abs(piece[2] - prev[2]) <= 1e-9
                    and (prev[4][4] < 0.0) == (piece[4][4] < 0.0))
            if same_span:
                source0 = seg_point(piece[0], piece[1])
                source1 = seg_point(piece[0], piece[2])
                mapped = []
                for claim in (prev, piece):
                    entry = claim[4]
                    mapped.append((
                        _affine_uv_to_panel(source0, entry[2], entry[3], entry[4]),
                        _affine_uv_to_panel(source1, entry[2], entry[3], entry[4]),
                    ))
                if (math.dist(mapped[0][0], mapped[1][0]) <= 1e-9
                        and math.dist(mapped[0][1], mapped[1][1]) <= 1e-9):
                    continue
        deduped_pieces.append(piece)
    pieces = deduped_pieces

    runs = []
    open_runs = []                 # [points, tri, back, end_seg, end_t]
    for k, t0, t1, tri, entry in pieces:
        _bb, _t, corners_uv, corners_panel, area2 = entry
        back = area2 < 0.0
        p0 = _affine_uv_to_panel(seg_point(k, t0), corners_uv, corners_panel, area2)
        p1 = _affine_uv_to_panel(seg_point(k, t1), corners_uv, corners_panel, area2)
        attached = None
        for run in open_runs:
            if run[2] != back:
                continue
            if run[3] == k:
                gap = t0 - run[4]
            elif run[3] == k - 1 and run[4] >= 1.0 - 1e-9:
                gap = t0
            else:
                continue
            # Negative gap = this piece starts BEFORE the run's end (an
            # overlapping sheet, or an exact-on-edge double claim): it is a
            # different sheet, not this run's continuation.
            if abs(gap) > 1e-9:
                continue
            continuous_image = math.dist(p0, run[0][-1]) <= 1e-8
            if continuous_image and (run[1] == tri or tri in neighbours[run[1]]):
                attached = run
                break
        if attached is None:
            open_runs.append([[p0, p1], tri, back, k, t1])
        else:
            if math.hypot(p0[0] - attached[0][-1][0], p0[1] - attached[0][-1][1]) > 1e-9:
                attached[0].append(p0)
            attached[0].append(p1)
            attached[1] = tri
            attached[3] = k
            attached[4] = t1
        # Retire runs that can no longer be continued.
        still_open = []
        for run in open_runs:
            if run[3] < k - 1 or (run[3] == k - 1 and run[4] < 1.0 - 1e-9 and t0 > run[4] + 1e-9):
                runs.append(run)
            else:
                still_open.append(run)
        open_runs = still_open
    runs.extend(open_runs)

    out = []
    for points, _tri, back, _k, _t in runs:
        deduped = [points[0]]
        for p in points[1:]:
            if math.hypot(p[0] - deduped[-1][0], p[1] - deduped[-1][1]) > 1e-9:
                deduped.append(p)
        if len(deduped) >= 2:
            out.append((deduped, back))
    return out


# ---------------------------------------------------------------------------
# output layer
# ---------------------------------------------------------------------------

def _create_mapped_layer(scene, name=MAPPED_LAYER_NAME):
    """Fresh top-of-stack output layer; the user's selection is restored
    (same construction as auto_mapping's, see its docstring)."""
    saved_frame = scene.current_frame()
    saved_layer = scene.current_layer()
    saved_asset = scene.current_asset()

    layer_index = scene.add_layer()
    if layer_index < 0:
        scene.set_current_frame(saved_frame)
        scene.set_current_layer(saved_layer)
        scene.set_current_asset(saved_asset)
        return -1
    scene.set_layer_name(layer_index, name)

    if scene.move_layer(layer_index, 0):
        scene.remap_fill_source_layers_after_move(layer_index, 0)
        layer_index = 0
        if saved_layer >= 0:
            saved_layer += 1

    scene.set_current_frame(saved_frame)
    scene.set_current_layer(saved_layer)
    scene.set_current_asset(saved_asset)
    return layer_index


def _discard_mapped_layer(scene, layer_index):
    try:
        scene.delete_layer(layer_index)
        scene.remap_fill_source_layers_after_delete(layer_index)
    except Exception as error:
        print(f"[fukusato] could not roll back the empty layer: {error}")


def _stroke_style(stroke):
    color = stroke.get("color") or {}
    pen_style = int(stroke.get("pen_style", 1))
    if pen_style < 1 or pen_style > 5:
        pen_style = 1
    return ((int(color.get("r", 0)), int(color.get("g", 0)),
             int(color.get("b", 0)), int(color.get("a", 255))),
            max(0.5, float(stroke.get("width", 3.0))), pen_style)


# ---------------------------------------------------------------------------
# tool options (dispatched by fukusato_workflow._option_changed)
# ---------------------------------------------------------------------------

def _apply_option(hook, value):
    if hook == "fk_alpha":
        _OPTIONS["alpha"] = max(0, min(100, int(value))) / 100.0
        print(f"[fukusato] alpha -> {_OPTIONS['alpha']:.2f} "
              f"({'pure geodesic' if _OPTIONS['alpha'] == 0 else 'blended'})")
    elif hook == "fk_beta":
        _OPTIONS["beta"] = max(1, min(40, int(value))) / 10.0
        print(f"[fukusato] beta -> {_OPTIONS['beta']:.1f}")
    elif hook == "fk_grid":
        _OPTIONS["grid"] = max(8, min(64, int(value)))
        print(f"[fukusato] mesh grid -> {_OPTIONS['grid']}x{_OPTIONS['grid']}")
    elif hook == "fk_samples":
        _OPTIONS["samples"] = max(2, min(64, int(value)))
        print(f"[fukusato] samples on the longest handle -> {_OPTIONS['samples']}")
    elif hook == "fk_variant":
        _OPTIONS["variant"] = "similarity" if str(value).lower() == "similarity" else "rigid"
        print(f"[fukusato] MLS variant -> {_OPTIONS['variant']}")
