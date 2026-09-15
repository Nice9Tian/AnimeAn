# Fukusato MLS workflow (archived)

Retired from the shipping app. Nothing here is imported, deployed or given a
button any more - the Tools dock has two pages again (Painting, Mapping) and
`pyfile/initalize.py` no longer loads these modules.

## What is here

| file | was |
| --- | --- |
| `fukusato_mapping.py` | the MLS solver core (rigid / similarity variants, geodesic weight field) |
| `fukusato_mesh.py` | garment mesh + region/ring point tests |
| `fukusato_workflow.py` | the tool layer: handle strokes, `fk_*` option hooks, `run_mapping` |
| `crease_line_tool.py` | the "Crease Line / 折角线" tool (`fukusato_cut`), built on the solver core |
| `fukusato_mapping.md` | paper workflow and architecture notes |
| `test_fukusato_mapping.py` | the solver's unit suite |

## What stayed behind in the live tree

`auto_mapping.MAPPING_OUTPUT_PROPERTIES` still lists the literals
`"fukusato_mapped"` and `"fukusato_mapped_back"`. A project saved while the
tool still shipped can carry layers with those properties, and they must keep
being excluded from region-detection walls and from pattern pickup.

## To bring it back

1. Move the four `.py` files back into `pyfile/`.
2. Re-add them to `PYTHON_FILE_MODULES` in `pyfile/initalize.py` (after
   `auto_mapping`, in the order mapping → mesh → crease_line_tool → workflow).
3. Re-add the three tool entries and the two `register_hooks()` calls in
   `pyfile/extra_tools.py`.
4. Re-add the `fukusato_line` / `fukusato_cut` / `fukusato_guide_mapping`
   branches in `pyfile/toolcontrol.py::options_for_extra_tool`.
5. Re-add the `fukusato` page in `MainWindow::createToolDocks`
   (`m_fukusatoToolsPanel`, `addPage`, and the `panelForPage` entry).
6. Re-add the four `copy_if_different` pairs in `CMakeLists.txt` (both the
   exe-dir list and the `ANIMEAN_DEPLOY_DIR` list).

Git history holds the exact removed hunks - see the commit that created this
directory.
