# Fukusato Mapping

This implementation follows Fukusato et al., *Interactive texture editing for
garment line drawings* (CAVW 2022), with manual crease authoring in place of
the upstream neural prediction.

## Workflow

1. Keep the texture artwork in ChildView and a closed garment line region in
   MainView.
2. Draw optional `Crease Line / 折角线` strokes on MainView. They are stored as
   independent crease assets and become exact constrained edges of the mesh.
3. Select `Fukusato Guide / 引导线` and draw a point or curve handle inside the garment.
   The stroke becomes a draggable overlay; the original location is the
   paper's `v_i(t)` and its current location is `v'_i(t)`.
4. Drag the handle to its target. Before accepting it, use **Fukusato Mapping
   > Weight Preview** to inspect the geodesic/Euclidean influence heatmap on
   both MainView and ChildView.
5. Click the check button at the handle's upper-right corner to solve and
   apply the deformation. Click x to cancel a new handle or cancel an edit.

**Triangle Topology** in the same menu draws the constrained garment mesh in
MainView and the current UV mesh in ChildView.

## Architecture

- `crease_line_tool.py`: persistent manual crease authoring. It has no MLS or
  texture-mapping dependency.
- `fukusato_mesh.py`: Python-only PSLG construction, constrained Delaunay
  triangulation with the bundled `triangle` package, holes, exact crease
  constraints, barycentric projection and topological cutting.
- `fukusato_mapping.py`: rigid/similarity MLS, cut-aware geodesic weights and
  exact piecewise-affine emission. Both texture strokes and odd-even vector
  fills (including nested holes and folded/overlapping UV sheets) are mapped.
- `fukusato_workflow.py`: paper interaction, confirmation transaction, menu,
  previews, persistence, history and output replacement.

The initial UV assignment is plane projection, one of the initializations
explicitly allowed by Section 3. Handles are projected through mesh
barycentric coordinates. Equation (1) is solved in its backward direction
(`after UV -> before UV`) and every mesh vertex UV is updated from the solve.
