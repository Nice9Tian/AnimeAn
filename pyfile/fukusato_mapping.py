"""Faithful reproduction of Fukusato et al., "Interactive texture editing for
garment line drawings", Computer Animation and Virtual Worlds 33(6):e2117,
2022. doi:10.1002/cav.2117

THE PAPER'S PIPELINE, AND WHERE EACH PIECE LIVES HERE
-----------------------------------------------------
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

In AnimeAn:

  main_paint_view  = the MODELING PANEL. The garment drawing, the crease cut
                     lines ("Fukusato Cut"), the AFTER handles and the mapped
                     output all live here. The mesh is triangulated OVER THIS
                     BOARD and cut here - exactly the paper's domain.
  child_paint_view = the TEXTURE / UV SPACE. The pattern strokes live here,
                     and the BEFORE handles are placed here.

  The initial UV assignment is the PLANE PROJECTION the paper names: every
  panel point's UV is its own coordinate. Under that identity field the
  paper's two handle states collapse to something drawable without a
  drag-and-drop gesture:

    -  the BEFORE handle's UV projection p_i(t) equals its panel position, so
       drawing the handle in the CHILD board (the UV space) at the texture
       feature to grab IS p_i(t) - the paper's own "place/edit the curve
       handles on the UV space" function (Sec. 3, last sentence);
    -  the AFTER handle v'_i(t) is drawn on MAIN; its UV projection q_i(t)
       through the identity field is again its position.

  Pairs match BY DRAW ORDER: child stroke k is the before state of main
  stroke k. A pair drawn identically in both boards is an anchor. A single
  click (dot stroke) is a POINT handle, a drawn curve is a CURVE handle -
  both of Sec. 4.1's handle kinds. Handle samples falling outside the mesh
  are ignored after a warning, as the paper's implementation does (Sec. 6.3).

  Editing mode (Sec. 4.1): the Arrow tool's edit handles double as the
  paper's drag-and-drop. After a first successful run, releasing an edit
  drag re-runs the mapping and REPLACES the previous output layer, so
  dragging an after-handle behaves like the paper's real-time editing loop.

EMISSION (how "deformed UV coordinates" become strokes)
-------------------------------------------------------
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

DELIBERATE SUBSTITUTIONS (documented like the paper's own deferrals)
--------------------------------------------------------------------
  * Geodesic distances use multi-source Dijkstra over the mesh edge graph
    (with both cell diagonals, halving the sqrt(2) grid-metric anisotropy),
    not the heat method [28]. Monotone and cut-aware, which is all the
    weight blend needs.
  * Handle curves are the artist's own pen strokes, not kappa-Curves [26]
    through clicked points. The handle's shape never enters the deformation
    algebra - only its sample positions do (the paper says the same of its
    kappa-Curves).
  * The paper defers the initial UV estimate to Hashimoto et al. 2020 and
    also names plane projection; only plane projection is implemented, so
    "before" handles are exact UV positions rather than barycentric
    projections through a non-trivial field.
  * The paper is a live drag loop; here each run recomputes from the strokes
    present (one-shot). A sequence of paper edits composes successive MLS
    solves; re-running composes them into a single solve from the original
    state. The editing-mode re-run above narrows the gap in practice.
  * The mesh is a RECTANGLE over the content's bounding box, not a
    triangulation of the garment silhouette. The paper's models are garment
    meshes, so its geodesics also feel the garment BOUNDARY (going around a
    U-shaped sleeve is far); here only explicitly drawn crease cuts obstruct
    the geodesic - the silhouette does not. Draw a crease along a boundary
    that should block influence.
  * Stroke width is scaled by a single global magnification estimate (total
    panel edge length over total UV edge length). Rigid gives ~1 by
    construction; similarity makes the width follow the requested scale. The
    true magnification varies per triangle; a per-piece width is not
    attempted.
"""

import heapq
import math

import python_hooks

HANDLE_PROPERTY = "fukusato_line"
CUT_PROPERTY = "fukusato_cut"
FUKUSATO_TOOL = "fukusato_guide_mapping"
MAPPED_PROPERTY = "fukusato_mapped"
BACK_PROPERTY = "fukusato_mapped_back"
MAPPED_LAYER_NAME = "fukusato layer"

POLY_STEP = 4.0
HANDLE_COLOR = (230, 60, 190, 255)
CUT_COLOR = (255, 140, 0, 255)

# alpha  : Sec 4.2 blend. 0 = pure geodesic (cuts fully felt), 1 = Euclidean.
# beta   : weight falloff exponent, w = 1/d^(2*beta) (Schaefer's form; the
#          paper does not pin the falloff).
# grid   : cells per side of the panel mesh.
# samples: control points taken from the LONGEST handle; shorter handles get
#          proportionally fewer (arc-length spacing), a dot gets one.
# variant: "rigid" is the paper's shipped deformer (Sec. 7.2 names similarity
#          as future work; it is provided as the planned switch).
# rerun  : editing mode - re-run on an Arrow-tool handle-drag release.
#          DEFAULT OFF: the re-run rebuilds the output layer, which shifts
#          absolute layer indices while edit_tool's drag session still holds
#          the old ones - dragging anything but a handle stroke could then
#          write into the wrong layer. Turn it on when edits are limited to
#          the handle strokes themselves.
_OPTIONS = {"alpha": 0.0, "beta": 2.0, "grid": 32, "samples": 16,
            "variant": "rigid", "rerun": False}
_EPS = 1e-6

# Editing-mode session: name of the last output layer (replaced on re-run).
_SESSION = {"ran": False, "layer_name": None}

_last_run_handled = False


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
    for layer in structure["layers"]:
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
    power = 2.0 * beta
    n_v = len(mesh.P)
    weights = [[0.0] * len(samples) for _ in range(n_v)]
    for k, s in enumerate(samples):
        m = measures[k] if measures else 1.0
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

    # A region walled off from EVERY handle by closed cuts would keep weight
    # zero and stay put; fall back to euclidean weights there so it follows
    # the global fit instead of freezing.
    stranded = 0
    for v in range(n_v):
        if sum(weights[v]) > 0.0:
            continue
        stranded += 1
        px, py = mesh.P[v]
        for k, s in enumerate(samples):
            de = math.hypot(px - s[0], py - s[1])
            weights[v][k] = 1.0 / (max(de, _EPS) ** power)
    if stranded:
        print(f"[fukusato] {stranded} vertex/vertices fully enclosed by cuts; "
              "they fall back to euclidean weights")
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
        for entry in self.entries:
            bx0, by0, bx1, by1 = entry[0]
            for gx in range(int((bx0 - self.x0) / self.cell), int((bx1 - self.x0) / self.cell) + 1):
                for gy in range(int((by0 - self.y0) / self.cell), int((by1 - self.y0) / self.cell) + 1):
                    self.bins.setdefault((gx, gy), []).append(entry)

    def candidates(self, ax, ay, bx, by):
        if not self.bins:
            return
        x0, x1 = (ax, bx) if ax <= bx else (bx, ax)
        y0, y1 = (ay, by) if ay <= by else (by, ay)
        seen = set()
        for gx in range(int((x0 - self.x0) / self.cell), int((x1 - self.x0) / self.cell) + 1):
            for gy in range(int((y0 - self.y0) / self.cell), int((y1 - self.y0) / self.cell) + 1):
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
    pieces = []                    # (seg index, t0, t1, tri, entry)
    for k, (a, b) in enumerate(zip(pattern_polyline, pattern_polyline[1:])):
        for entry in index.candidates(a[0], a[1], b[0], b[1]):
            span = _clip_segment_to_uv_tri(a, b, entry[2], entry[4])
            if span is None:
                continue
            pieces.append((k, span[0], span[1], entry[1], entry))
    pieces.sort(key=lambda piece: (piece[0], piece[1], piece[2]))
    # A span lying exactly ON a shared triangle edge is claimed by both
    # triangles; the affine maps agree on the edge, so either copy is the
    # same geometry - drop the duplicate rather than emitting double ink.
    deduped_pieces = []
    for piece in pieces:
        if deduped_pieces:
            prev = deduped_pieces[-1]
            if (piece[0] == prev[0]
                    and abs(piece[1] - prev[1]) <= 1e-9
                    and abs(piece[2] - prev[2]) <= 1e-9):
                continue
        deduped_pieces.append(piece)
    pieces = deduped_pieces

    def seg_point(k, t):
        a = pattern_polyline[k]
        b = pattern_polyline[k + 1]
        return (a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t)

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
            if run[1] == tri or tri in neighbours[run[1]]:
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


def _find_layer_by_name(scene, name):
    if not name:
        return -1
    structure = scene.get_structure()
    for layer in structure["layers"]:
        if layer.get("name") == name:
            return layer["index"]
    return -1


def _stroke_style(stroke):
    color = stroke.get("color") or {}
    return ((int(color.get("r", 0)), int(color.get("g", 0)),
             int(color.get("b", 0)), int(color.get("a", 255))),
            max(0.5, float(stroke.get("width", 3.0))))


# ---------------------------------------------------------------------------
# run
# ---------------------------------------------------------------------------

def _bounds(point_lists, margin_ratio=0.12, min_margin=32.0):
    xs = [p[0] for pts in point_lists for p in pts]
    ys = [p[1] for pts in point_lists for p in pts]
    if not xs:
        return None
    x0, x1 = min(xs), max(xs)
    y0, y1 = min(ys), max(ys)
    span = max(x1 - x0, y1 - y0, 1.0)
    m = max(min_margin, span * margin_ratio)
    return x0 - m, y0 - m, x1 + m, y1 + m


def perform_mapping(replace=False):
    animean = _animean()
    child = _scene_model("child")
    main = _scene_model("main")
    child_frame = max(child.current_frame(), 0)
    main_frame = max(main.current_frame(), 0)

    before_handles = _collect(child, child_frame, HANDLE_PROPERTY)
    after_handles = _collect(main, main_frame, HANDLE_PROPERTY)
    # The crease lines are part of the garment drawing, so they live on the
    # panel (paper Sec. 3). Cuts drawn in child (the UV space) are accepted
    # too: under plane projection both boards share coordinates, and a
    # texture-space crease is the same set of points.
    cuts = ([c["points"] for c in _collect(main, main_frame, CUT_PROPERTY)]
            + [c["points"] for c in _collect(child, child_frame, CUT_PROPERTY)])
    tool_properties = (HANDLE_PROPERTY, CUT_PROPERTY, MAPPED_PROPERTY,
                       BACK_PROPERTY, FUKUSATO_TOOL,
                       "auto_mapping", "auto_mapping_2", "auto_mapped",
                       "auto_mapped_back", "auto_mapped_seal",
                       "auto_mapped_guide", "auto_mapped_guide_h",
                       "auto_mapped_guide_v",
                       "h_center_line", "v_center_line", "mapping_area")
    pattern = _collect(child, child_frame, None, exclude=tool_properties)

    if not before_handles or not after_handles:
        print("[fukusato] draw at least one handle pair: the BEFORE state in "
              "child_paint_view (on the texture) and the AFTER state in "
              "main_paint_view (on the garment). Pairs match BY ORDER; a dot "
              "is a point handle, a stroke is a curve handle.")
        return False
    if len(before_handles) != len(after_handles):
        print(f"[fukusato] handle count mismatch: child has {len(before_handles)}, "
              f"main has {len(after_handles)}. Draw one after-state per before-state.")
        return False
    if not pattern:
        print("[fukusato] child_paint_view has no pattern strokes to map.")
        return False

    alpha = float(_OPTIONS["alpha"])
    beta = float(_OPTIONS["beta"])
    variant = _OPTIONS["variant"]
    per_handle = int(_OPTIONS["samples"])

    # The modeling-panel mesh covers the garment content, the creases AND the
    # texture pattern's UV extent (the identity plane projection makes the
    # pattern's coordinates panel coordinates too). Handles deliberately do
    # NOT grow the box: the paper ignores handles placed outside the drawing
    # area after a warning (Sec. 6.3), and a box inflated by the handles
    # themselves could never reject one.
    garment = _collect(main, main_frame, None, exclude=tool_properties)
    box_sources = ([g["points"] for g in garment]
                   + [p["points"] for p in pattern] + cuts)
    if not garment:
        # No garment drawing on the panel yet: there is no "drawing area" to
        # be outside of, so the handles define the working area instead of
        # being rejected by it (Sec. 6.3's rejection presumes a drawing).
        box_sources += [h["points"] for h in before_handles]
        box_sources += [h["points"] for h in after_handles]
    box = _bounds(box_sources)
    mesh = Mesh(box[0], box[1], box[2], box[3], _OPTIONS["grid"])
    duplicated = 0
    for cut in cuts:
        duplicated += mesh.apply_cut(cut)
    print(f"[fukusato] panel mesh {mesh.n}x{mesh.n}, {len(cuts)} crease(s), "
          f"{duplicated} vertex/vertices duplicated by the cut")

    # Discretize Eq. (1): arc-length samples, count proportional to length,
    # a point handle contributing exactly one control point.
    longest = max(max(_length(h["points"]) for h in before_handles),
                  max(_length(h["points"]) for h in after_handles), 1.0)
    spacing = longest / max(1, per_handle - 1)
    p_pts = []                     # BEFORE, in UV space  (paper's p_i(t))
    q_pts = []                     # AFTER, projected UVs (paper's q_i(t))
    measures = []                  # Eq. (1)'s dt: unit measure PER HANDLE
    for k, (before, after) in enumerate(zip(before_handles, after_handles)):
        arc = max(_length(before["points"]), _length(after["points"]))
        count = 1 if arc < 1.0 else max(2, int(round(arc / spacing)) + 1)
        pp = _resample(before["points"], count)
        qq = _resample(after["points"], count)
        kept_p = []
        kept_q = []
        dropped = 0
        for p, q in zip(pp, qq):
            # Paper Sec. 6.3: handles placed outside the drawing area cannot
            # be projected and are ignored after a warning.
            if mesh.contains(p[0], p[1]) and mesh.contains(q[0], q[1]):
                kept_p.append(p)
                kept_q.append(q)
            else:
                dropped += 1
        p_pts.extend(kept_p)
        q_pts.extend(kept_q)
        # Each handle integrates over t in [0,1] regardless of its length, so
        # its total measure is 1 split over its kept samples.
        if kept_p:
            measures.extend([1.0 / len(kept_p)] * len(kept_p))
        drift = 0.0
        for p, q in zip(pp, qq):
            drift = max(drift, math.hypot(q[0] - p[0], q[1] - p[1]))
        kind = "point" if count == 1 else "curve"
        note = " (anchor)" if drift < 0.5 else ""
        if dropped:
            note += f", {dropped} sample(s) outside the drawing area ignored"
        print(f"[fukusato] handle {k + 1} ({kind}): {len(kept_p)} sample(s), "
              f"displacement {drift:.1f}px{note}")
    if not p_pts:
        print("[fukusato] every handle sample fell outside the drawing area; "
              "nothing to do.")
        return False

    q_c = [complex(p[0], p[1]) for p in q_pts]
    p_c = [complex(p[0], p[1]) for p in p_pts]

    # Weights on the modeling panel (Sec. 4.2): geodesic on the CUT mesh
    # blended with euclidean, seeded at the AFTER positions - the domain of
    # Eq. (1)'s fit, which is what keeps f(q_i) = p_i (see build_weights).
    adj = mesh.edge_graph()
    weights = build_weights(mesh, adj, q_pts, alpha, beta, measures)

    # Deform the UV field: plane-projection identity pulled through the
    # BACKWARD map of Eq. (1)  (src = q = after, dst = p = before).
    uv = []
    for v, (x, y) in enumerate(mesh.P):
        z = mls_deform(q_c, p_c, weights[v], complex(x, y), variant)
        uv.append((z.real, z.imag))

    # Stroke width follows the local texture magnification, which for the
    # backward field is panel-length over uv-length.
    num = den = 0.0
    for tri in mesh.tris:
        for a, b in ((tri[0], tri[1]), (tri[1], tri[2]), (tri[2], tri[0])):
            num += math.hypot(mesh.P[a][0] - mesh.P[b][0], mesh.P[a][1] - mesh.P[b][1])
            den += math.hypot(uv[a][0] - uv[b][0], uv[a][1] - uv[b][1])
    width_scale = (num / den) if den > 0.0 else 1.0

    neighbours = mesh.tri_neighbours()

    # Editing mode: a re-run replaces its previous output layer - but only
    # AFTER the new one emitted successfully, so a failing re-run never
    # destroys the last good result. The old layer is located BY INDEX
    # before the new one is created: locating it by name afterwards could
    # find the new layer instead when the names collide.
    old_layer = -1
    if replace and _SESSION["layer_name"]:
        old_layer = _find_layer_by_name(main, _SESSION["layer_name"])

    layer = _create_mapped_layer(main)
    if layer < 0:
        print("[fukusato] could not create the output layer in main_paint_view.")
        return False
    image = main.image_at(main_frame, layer, True)
    if image is None:
        _discard_mapped_layer(main, layer)
        print("[fukusato] the output layer has no editable cell.")
        return False

    added = 0
    back_count = 0
    index = _UvIndex(mesh, uv)     # built once, shared by every stroke
    try:
        for entry in pattern:
            color, width = _stroke_style(entry["stroke"])
            for points, back in emit_pattern(mesh, uv, entry["points"], neighbours,
                                             index=index):
                obj = animean.vectorlogic.make_stroke_object(
                    points, color, max(0.5, width * width_scale),
                    image.stroke_count() + 1, False, False)
                obj.property = BACK_PROPERTY if back else MAPPED_PROPERTY
                image.add_stroke_object(obj)
                added += 1
                if back:
                    back_count += 1
    except Exception:
        _discard_mapped_layer(main, layer)
        animean.ui.refresh()
        raise

    if added == 0:
        _discard_mapped_layer(main, layer)
        animean.ui.refresh()
        print("[fukusato] nothing mapped: no pattern content lies under the "
              "garment's UV footprint; the empty layer was discarded.")
        return False

    if old_layer >= 0:
        # The new layer moved to index 0, shifting the old one down by one.
        shifted = old_layer + 1 if layer == 0 else old_layer
        if shifted != layer:
            _discard_mapped_layer(main, shifted)

    _SESSION["ran"] = True
    _SESSION["layer_name"] = main.layer_name(layer)

    animean.ui.refresh()
    try:
        animean.ui.history_commit("Fukusato Mapping", "main")
    except Exception:
        pass
    summary = (f"[fukusato] {variant} MLS (alpha={alpha:.2f}, beta={beta:.1f}) mapped "
               f"{added} stroke piece(s) into layer '{_SESSION['layer_name']}' "
               f"(frame {main_frame + 1} of main_paint_view, width x{width_scale:.2f})")
    if back_count:
        summary += f"; {back_count} piece(s) face BACK across a fold ('{BACK_PROPERTY}')"
    print(summary)
    return True


def _run(replace=False):
    try:
        perform_mapping(replace=replace)
    except Exception as error:
        import traceback
        print(f"[fukusato] error: {error!r}\n{traceback.format_exc()}")


# ---------------------------------------------------------------------------
# self test (headless; reproduces the paper's own sanity experiments)
# ---------------------------------------------------------------------------

def self_test():
    """Run from the Python Debug pane: fukusato_mapping.self_test()"""
    print("===== fukusato_mapping self test =====")

    # (1) rigid MLS reproduces a rigid motion exactly (deformer sanity)
    th = 0.3
    rot = complex(math.cos(th), math.sin(th))
    shift = complex(50.0, -20.0)
    center = complex(500.0, 500.0)
    src = [complex(200, 200), complex(800, 200), complex(800, 800),
           complex(200, 800), complex(500, 350)]
    dst = [center + (p - center) * rot + shift for p in src]
    worst = 0.0
    for qx in range(100, 950, 90):
        for qy in range(100, 950, 90):
            q = complex(qx, qy)
            w = [1.0 / max(abs(q - p), 1e-6) ** 4 for p in src]
            got = mls_deform(src, dst, w, q, "rigid")
            want = center + (q - center) * rot + shift
            worst = max(worst, abs(got - want))
    print(f"[1] rigid reproduction: max error {worst:.3e} px "
          f"({'PASS' if worst < 1e-6 else 'FAIL'})")

    # (2) the cut TEARS the field (paper E1): a handle below a crease drags
    # the texture down; across the crease the jump must survive, past the
    # crease's open end it must vanish.
    mesh = Mesh(0.0, 0.0, 1000.0, 1000.0, 40)
    dup = mesh.apply_cut([(180.0, 500.0), (740.0, 500.0)])
    adj = mesh.edge_graph()
    handle_before = [(450.0, 380.0)]
    handle_after = [(450.0, 300.0)]          # dragged up by 80 px
    anchors = [(60.0, 60.0), (940.0, 60.0), (940.0, 940.0), (60.0, 940.0)]
    p_pts = handle_before + anchors
    q_pts = handle_after + anchors
    # Seeded at the AFTER positions - the domain of the backward fit.
    weights = build_weights(mesh, adj, q_pts, 0.0, 2.0)
    q_c = [complex(*p) for p in q_pts]
    p_c = [complex(*p) for p in p_pts]
    uv = []
    for v, (x, y) in enumerate(mesh.P):
        z = mls_deform(q_c, p_c, weights[v], complex(x, y), "rigid")
        uv.append((z.real, z.imag))

    def disp_at(x, y):
        verts, bary = mesh.locate(x, y)
        ux = sum(b * uv[v][0] for v, b in zip(verts, bary))
        uy = sum(b * uv[v][1] for v, b in zip(verts, bary))
        return math.hypot(ux - x, uy - y)

    jump_across = abs(disp_at(450.0, 498.0) - disp_at(450.0, 502.0))
    jump_beyond = abs(disp_at(880.0, 498.0) - disp_at(880.0, 502.0))
    ratio = jump_across / max(jump_beyond, 1e-9)
    print(f"[2] tear: {dup} vertices duplicated; UV jump across the crease "
          f"{jump_across:.2f} px vs beyond its end {jump_beyond:.4f} px "
          f"(x{ratio:.0f}) ({'PASS' if jump_across > 1.0 and jump_beyond < 0.2 else 'FAIL'})")

    # (3) emission is exact and tears at the cut: a vertical texture line
    # crossing the crease must come out as >= 2 strokes with a gap.
    neighbours = mesh.tri_neighbours()
    runs = emit_pattern(mesh, uv, [(450.0, 400.0), (450.0, 620.0)], neighbours)
    fronts = [r for r in runs if not r[1]]
    gap = 0.0
    if len(fronts) >= 2:
        a_end = fronts[0][0][-1]
        b_start = fronts[1][0][0]
        gap = math.hypot(a_end[0] - b_start[0], a_end[1] - b_start[1])
    worst_aff = 0.0
    for points, _back in runs:
        for a, b in zip(points, points[1:]):
            worst_aff = max(worst_aff, 0.0 if math.isfinite(a[0] + b[0]) else 1.0)
    print(f"[3] emission: {len(runs)} piece(s) for a line crossing the crease, "
          f"gap at the tear {gap:.2f} px "
          f"({'PASS' if len(runs) >= 2 and gap > 0.5 else 'FAIL'})")

    # (4) Fig. 10: asked for a uniform scale, the rigid deformer produces an
    # UNEVEN distortion (that is the figure's point - "remains difficult to
    # uniformly scale"), while similarity reproduces it exactly. Measured as
    # the paper's reference experiment does: the spread of the texture area
    # gain over a probe grid inside the scaled quad.
    mesh2 = Mesh(0.0, 0.0, 1000.0, 1000.0, 24)
    adj2 = mesh2.edge_graph()
    c = (500.0, 500.0)
    quad = [(300.0, 300.0), (700.0, 300.0), (700.0, 700.0), (300.0, 700.0)]
    s = 1.5
    grown = [(c[0] + (p[0] - c[0]) * s, c[1] + (p[1] - c[1]) * s) for p in quad]
    w2 = build_weights(mesh2, adj2, quad, 1.0, 2.0)
    results = {}
    for variant in ("rigid", "similarity"):
        # Backward: after = grown quad, before = original quad.
        qc = [complex(*p) for p in grown]
        pc = [complex(*p) for p in quad]
        uv2 = []
        for v, (x, y) in enumerate(mesh2.P):
            z = mls_deform(qc, pc, w2[v], complex(x, y), variant)
            uv2.append((z.real, z.imag))
        gains = []
        for gx in range(7):
            for gy in range(7):
                px = 360.0 + gx * 280.0 / 6.0
                py = 360.0 + gy * 280.0 / 6.0
                tri = mesh2.tri_of(px, py)
                (ua, ub, uc_), (xa, xb, xc), area2 = _tri_geometry(mesh2, uv2, tri)
                if abs(area2) < 1e-9:
                    continue
                panel2 = ((xb[0] - xa[0]) * (xc[1] - xa[1])
                          - (xb[1] - xa[1]) * (xc[0] - xa[0]))
                gains.append(panel2 / area2)
        results[variant] = (min(gains), sum(gains) / len(gains), max(gains))
    r = results["rigid"]
    m = results["similarity"]
    r_spread = r[2] / max(r[0], 1e-9) if r[0] > 0 else float("inf")
    m_spread = m[2] / max(m[0], 1e-9)
    ok = (m_spread < 1.05 and abs(m[1] - s * s) < 0.1 and
          (r_spread > 1.5 or r_spread < 0.0))
    print(f"[4] fig.10 uniform x{s} scale request, texture area gain over a "
          f"7x7 probe grid:\n"
          f"    rigid      min/mean/max = {r[0]:.3f}/{r[1]:.3f}/{r[2]:.3f} "
          f"(spread x{r_spread:.2f} - uneven, the figure's point)\n"
          f"    similarity min/mean/max = {m[0]:.3f}/{m[1]:.3f}/{m[2]:.3f} "
          f"(uniform ~{s * s:.2f})\n"
          f"    ({'PASS' if ok else 'FAIL'})")
    print("===== done =====")


# ---------------------------------------------------------------------------
# hooks + tool handlers
# ---------------------------------------------------------------------------

def _fukusato_button(cell, stroke, message):
    global _last_run_handled
    _last_run_handled = True
    _run()


def _handle_released(cell, stroke, message):
    """Editing mode (paper Sec. 4.1): after the first run, releasing an
    Arrow-tool handle drag re-runs the mapping in place."""
    if not _OPTIONS["rerun"] or not _SESSION["ran"]:
        return
    # Handle events are shared plumbing (the Connect tool's buttons release
    # through the same pipeline); only the Arrow's drags mean an edit here.
    if message.get("base_tool") != "arrow":
        return
    if message.get("phase") != "release":
        return
    _run(replace=True)


def _tool_option_changed(cell, stroke, message):
    hook = message.get("hook")
    if not str(hook).startswith("fk_"):
        return
    try:
        _apply_option(hook, message.get("value"))
    except (TypeError, ValueError) as error:
        # python_hooks.dispatch has no per-hook guard: an exception here would
        # abort every later hook of the same event.
        print(f"[fukusato] ignored bad option value for {hook}: {error}")


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
    elif hook == "fk_rerun":
        _OPTIONS["rerun"] = str(value).lower() == "on"
        print(f"[fukusato] editing-mode re-run {'ON' if _OPTIONS['rerun'] else 'OFF'}")


def register_hooks():
    python_hooks.set_hook(_fukusato_button, extra=True, tool=FUKUSATO_TOOL)
    python_hooks.set_hook(_tool_option_changed, option=True, tool="extra")
    python_hooks.set_hook(_handle_released, handle=True)


def _set_draw_color(color):
    try:
        _animean().ui.set_draw_color(color)
    except Exception:
        pass


def activate_fukusato_line(name="fukusato_line", property_value=HANDLE_PROPERTY):
    register_hooks()
    _set_draw_color(HANDLE_COLOR)
    print("[fukusato] handle tool: draw (or dot) the BEFORE state on the "
          "texture in child_paint_view and the AFTER state on the garment in "
          "main_paint_view. Pairs match by order; identical pairs anchor.")
    return property_value


def activate_fukusato_cut(name="fukusato_cut", property_value=CUT_PROPERTY):
    register_hooks()
    _set_draw_color(CUT_COLOR)
    print("[fukusato] crease tool: draw the fold on the garment in "
          "main_paint_view. The panel mesh is cut and topologically "
          "separated there, so texture influence and the mapped pattern "
          "genuinely tear across it.")
    return property_value


def run_fukusato_mapping(name=FUKUSATO_TOOL, property_value=FUKUSATO_TOOL):
    global _last_run_handled
    register_hooks()
    if _last_run_handled:
        # the "extra" event hook already performed this click's mapping
        _last_run_handled = False
        return property_value
    _run()
    return property_value


register_hooks()
