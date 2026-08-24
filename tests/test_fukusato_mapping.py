import math
import pathlib
import sys
import unittest
from types import SimpleNamespace
from unittest import mock


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "pyfile"))

import fukusato_mapping as mapping
import auto_mapping
from fukusato_mesh import GarmentMesh
import fukusato_workflow as workflow
import python_hooks
import script_store


class GarmentMeshTests(unittest.TestCase):
    def test_constrained_triangulation_respects_hole_and_crease(self):
        outer = [(0, 0), (100, 0), (100, 100), (0, 100)]
        hole = [(40, 40), (60, 40), (60, 60), (40, 60)]
        mesh = GarmentMesh.triangulate(
            outer, [hole], [[(5, 25), (95, 25)]], max_area=80)

        self.assertIsNone(mesh.locate(50, 50))
        self.assertIsNotNone(mesh.locate(20, 20))
        copies = {}
        for index, point in enumerate(mesh.P):
            copies.setdefault(point, []).append(index)
        duplicated = [ids for ids in copies.values() if len(ids) > 1]
        self.assertTrue(duplicated)
        graph = mesh.edge_graph()
        self.assertTrue(all(b not in graph[a]
                            for ids in duplicated
                            for a in ids for b in ids if a != b))

    def test_barycentric_uv_projection(self):
        mesh = GarmentMesh([(0, 0), (10, 0), (0, 10)], [(0, 1, 2)],
                           base_uv=[(100, 20), (120, 20), (100, 50)])
        uv = mesh.uv_at((2.5, 5.0))
        self.assertAlmostEqual(uv[0], 105.0)
        self.assertAlmostEqual(uv[1], 35.0)

    def test_concave_hole_seed_stays_inside_the_hole(self):
        outer = [(0, 0), (100, 0), (100, 100), (0, 100)]
        c_hole = [(20, 20), (80, 20), (80, 35), (35, 35),
                  (35, 65), (80, 65), (80, 80), (20, 80)]
        mesh = GarmentMesh.triangulate(outer, [c_hole], max_area=80)
        self.assertIsNone(mesh.locate(25, 50))
        self.assertIsNotNone(mesh.locate(55, 50))

    def test_boundary_to_boundary_crease_disconnects_the_mesh(self):
        mesh = GarmentMesh.triangulate(
            [(0, 0), (100, 0), (100, 100), (0, 100)],
            creases=[[(0, 50), (100, 50)]], max_area=80)
        graph = mesh.edge_graph()
        unseen = set(range(len(graph)))
        components = []
        while unseen:
            todo = [unseen.pop()]
            component = set(todo)
            while todo:
                vertex = todo.pop()
                for neighbour in graph[vertex]:
                    if neighbour in unseen:
                        unseen.remove(neighbour)
                        component.add(neighbour)
                        todo.append(neighbour)
            components.append(component)
        self.assertEqual(len(components), 2)

    def test_empty_mesh_lookup_is_safe(self):
        mesh = GarmentMesh([], [])
        self.assertIsNone(mesh.locate(0, 0))


class WeightTests(unittest.TestCase):
    def test_pure_geodesic_does_not_cross_disconnected_component(self):
        mesh = GarmentMesh(
            [(0, 0), (10, 0), (0, 10),
             (100, 0), (110, 0), (100, 10)],
            [(0, 1, 2), (3, 4, 5)])
        sample = [(2.0, 2.0)]
        geodesic = mapping.build_weights(
            mesh, mesh.edge_graph(), sample, alpha=0.0, beta=2.0)
        self.assertTrue(all(weight > 0.0 for weight in geodesic[0]))
        self.assertEqual(geodesic[3], [0.0])
        query = complex(*mesh.P[3])
        self.assertEqual(
            mapping.mls_deform([2 + 2j], [4 + 2j], geodesic[3], query),
            query)

        blended = mapping.build_weights(
            mesh, mesh.edge_graph(), sample, alpha=0.01, beta=2.0)
        self.assertGreater(blended[3][0], 0.0)

    def test_handle_measures_are_applied_and_validated(self):
        mesh = GarmentMesh([(0, 0), (10, 0), (0, 10)], [(0, 1, 2)])
        rows = mapping.build_weights(
            mesh, mesh.edge_graph(), [(2, 2), (2, 2)], 1.0, 2.0,
            measures=[0.25, 0.75])
        self.assertAlmostEqual(rows[0][1] / rows[0][0], 3.0)
        with self.assertRaises(ValueError):
            mapping.build_weights(
                mesh, mesh.edge_graph(), [(2, 2)], 1.0, 2.0,
                measures=[])
        with self.assertRaises(ValueError):
            mapping.build_weights(
                mesh, mesh.edge_graph(), [(2, 2)], 1.0, 2.0,
                measures=[-1.0])


class EmissionTests(unittest.TestCase):
    def test_overlapping_uv_sheets_are_not_deduplicated(self):
        mesh = mapping.Mesh(0, 0, 2, 2, 2)
        # Four panel cells cover the same UV square. Two preserve and two flip
        # orientation; every sheet must receive the texture segment.
        uv = [(float(i % 2), float(j % 2))
              for j in range(3) for i in range(3)]
        runs = mapping.emit_pattern(
            mesh, uv, [(0.2, 0.25), (0.8, 0.25)], mesh.tri_neighbours())
        self.assertEqual(len(runs), 4)
        self.assertEqual(sum(1 for _points, back in runs if back), 2)

    def test_point_stroke_maps_to_every_overlapping_uv_sheet(self):
        mesh = mapping.Mesh(0, 0, 2, 2, 2)
        uv = [(float(i % 2), float(j % 2))
              for j in range(3) for i in range(3)]
        runs = mapping.emit_pattern(
            mesh, uv, [(0.25, 0.25)], mesh.tri_neighbours())
        self.assertEqual(len(runs), 4)
        self.assertTrue(all(len(points) == 1 for points, _back in runs))
        self.assertEqual(sum(1 for _points, back in runs if back), 2)

    def test_discontinuous_panel_images_are_not_chained(self):
        class Index:
            entries = [
                ((0, -1, 2, 1), 0,
                 ((0, -1), (2, -1), (0, 1)),
                 ((0, -1), (2, -1), (0, 1)), 4.0),
                ((0, -1, 2, 1), 1,
                 ((2, 1), (0, 1), (2, -1)),
                 ((12, 1), (10, 1), (12, -1)), 4.0),
            ]

            def candidates(self, _ax, _ay, _bx, _by):
                yield from self.entries

        runs = mapping.emit_pattern(
            SimpleNamespace(tris=[]), [], [(0, 0), (2, 0)],
            [{1}, {0}], index=Index())
        self.assertEqual(len(runs), 2)
        self.assertTrue(all(len(points) == 2 for points, _back in runs))

    def test_uv_index_clamps_queries_to_populated_bins(self):
        mesh = GarmentMesh([(0, 0), (10, 0), (0, 10)], [(0, 1, 2)])
        index = mapping._UvIndex(mesh, mesh.P)
        self.assertEqual(list(index.candidates(
            -1e12, -1e12, -1e12 + 1.0, -1e12 + 1.0)), [])

    def test_stroke_style_preserves_supported_pen_style(self):
        color, width, pen_style = mapping._stroke_style({
            "color": {"r": 1, "g": 2, "b": 3, "a": 4},
            "width": 0.1,
            "pen_style": 4,
        })
        self.assertEqual(color, (1, 2, 3, 4))
        self.assertEqual(width, 0.5)
        self.assertEqual(pen_style, 4)

    def test_rigid_mls_reproduces_rigid_motion(self):
        angle = 0.4
        rotation = complex(math.cos(angle), math.sin(angle))
        shift = complex(30, -12)
        source = [0j, 100 + 0j, 100 + 100j, 0 + 100j]
        target = [point * rotation + shift for point in source]
        query = 31 + 47j
        weights = [1.0 / max(abs(query - p), 1e-6) ** 4 for p in source]
        got = mapping.mls_deform(source, target, weights, query, "rigid")
        self.assertLess(abs(got - (query * rotation + shift)), 1e-8)


class PaperPipelineTests(unittest.TestCase):
    def test_confirmed_curve_projects_by_barycentrics_and_interpolates(self):
        mesh = GarmentMesh.triangulate(
            [(0, 0), (200, 0), (200, 200), (0, 200)], max_area=80)
        guide = {
            "before": [(60, 80), (100, 80), (140, 80)],
            "after": [(75, 95), (115, 95), (155, 95)],
        }
        uv = workflow._solve_uv(mesh, [guide])
        # MLS interpolation: texture coordinates under each AFTER sample are
        # pulled back onto the corresponding BEFORE sample.
        for before, after in zip(guide["before"], guide["after"]):
            projected = mesh.uv_at(after, uv)
            self.assertLess(math.dist(projected, before), 1.0)

    def test_topology_browser_emits_each_unique_edge_once(self):
        mesh = GarmentMesh([(0, 0), (10, 0), (10, 10), (0, 10)],
                           [(0, 1, 2), (0, 2, 3)])
        items = workflow._topology_items(mesh, mesh.P, "test")
        self.assertEqual(len(items), 5)

    def test_vector_fill_triangulation_preserves_nested_holes(self):
        rings = [
            [(0, 0), (10, 0), (10, 10), (0, 10)],
            [(2, 2), (8, 2), (8, 8), (2, 8)],
            [(3, 3), (7, 3), (7, 7), (3, 7)],
            [(4, 4), (6, 4), (6, 6), (4, 6)],
        ]
        commands = []
        for ring in rings:
            commands.append({"type": "move", "to": {"x": ring[0][0], "y": ring[0][1]}})
            commands.extend({"type": "line", "to": {"x": p[0], "y": p[1]}}
                            for p in ring[1:] + ring[:1])
        triangles = workflow._fill_triangles({"commands": commands})
        area = sum(abs(workflow.signed_area(triangle)) for triangle in triangles)
        self.assertAlmostEqual(area, 100 - 36 + 16 - 4, places=6)

    def test_uv_triangle_clip_is_exact(self):
        clipped = workflow._clip_polygon_to_uv_triangle(
            [(-1, -1), (8, -1), (8, 8), (-1, 8)],
            [(0, 0), (10, 0), (0, 10)], 100.0)
        self.assertAlmostEqual(abs(workflow.signed_area(clipped)), 46.0, places=6)


class StateTests(unittest.TestCase):
    def test_legacy_rectangular_workflow_is_not_armed_on_import(self):
        functions = [hook["function"] for hook in python_hooks._HOOKS]
        self.assertNotIn(mapping._fukusato_button, functions)

    def test_namespaced_state_preserves_other_tools(self):
        class Scene:
            def __init__(self):
                self.value = '{"mapping_assets":{"h":1}}'

            def script_data(self):
                return self.value

            def set_script_data(self, value):
                self.value = value

        scene = Scene()
        script_store.write(scene, "fukusato_mapping", {"guides": []})
        self.assertEqual(script_store.read(scene, "mapping_assets"), {"h": 1})
        self.assertEqual(script_store.read(scene, "fukusato_mapping"), {"guides": []})

    def test_pattern_layers_flatten_bottom_to_top(self):
        class Scene:
            def get_structure(self):
                return {
                    "frame_count": 1,
                    "layers": [
                        {"index": 0, "visible": True, "type": "paint"},
                        {"index": 1, "visible": True, "type": "paint"},
                    ],
                }

            def cell_to_dict(self, layer, _frame, _poly, _step):
                return {"image": {"strokes": [{
                    "property": "",
                    "raw_points": [{"x": layer, "y": 0}],
                }]}}

        result = mapping._collect(Scene(), 0)
        self.assertEqual([entry["points"][0][0] for entry in result], [1.0, 0.0])

    def test_output_layer_deletion_accounts_for_failed_move_to_top(self):
        self.assertEqual(workflow._shifted_old_layers([0, 2, 5], 0), [6, 3, 1])
        self.assertEqual(workflow._shifted_old_layers([0, 2, 5], 7), [5, 2, 0])

    def test_fukusato_drag_preview_is_transient_until_release(self):
        class Scene:
            def __init__(self):
                self.value = ""

            def script_data(self):
                return self.value

            def set_script_data(self, value):
                self.value = value

        scene = Scene()
        script_store.write(scene, workflow.STORE_KEY, {
            "next_id": 2,
            "guides": [{
                "id": 1, "frame": 0,
                "before": [[0, 0], [10, 0]],
                "after": [[0, 0], [10, 0]],
                "accepted": True,
            }],
            "solutions": {},
        })
        history_commit = mock.Mock()
        runtime = SimpleNamespace(ui=SimpleNamespace(history_commit=history_commit))
        workflow._DRAG.update(id=None, origin=None, points=None, moved=False,
                              was_accepted=False)
        with (mock.patch.object(workflow, "_scene_model", return_value=scene),
              mock.patch.object(workflow, "_animean", return_value=runtime),
              mock.patch.object(workflow, "refresh_overlays") as refresh):
            handle = "fukusato-guide:1"
            workflow._drag_guide({
                "view": "main", "phase": "press", "handle": handle,
                "position": {"x": 0, "y": 0},
            })
            workflow._drag_guide({
                "view": "main", "phase": "move", "handle": handle,
                "position": {"x": 5, "y": 3},
            })
            persisted = workflow._state(scene)["guides"][0]
            self.assertTrue(persisted["accepted"])
            self.assertEqual(persisted["after"], [[0, 0], [10, 0]])
            transient = refresh.call_args.args[0]["guides"][0]
            self.assertFalse(transient["accepted"])
            self.assertEqual(transient["after"], [[5.0, 3.0], [15.0, 3.0]])

            workflow._drag_guide({"view": "main", "phase": "cancel"})
            self.assertTrue(workflow._state(scene)["guides"][0]["accepted"])

            workflow._drag_guide({
                "view": "main", "phase": "press", "handle": handle,
                "position": {"x": 0, "y": 0},
            })
            workflow._drag_guide({
                "view": "main", "phase": "move", "handle": handle,
                "position": {"x": 5, "y": 3},
            })
            workflow._drag_guide({
                "view": "main", "phase": "release", "handle": handle,
                "position": {"x": 5, "y": 3},
            })

        persisted = workflow._state(scene)["guides"][0]
        self.assertFalse(persisted["accepted"])
        self.assertEqual(persisted["rollback_after"], [[0, 0], [10, 0]])
        self.assertEqual(persisted["after"], [[5.0, 3.0], [15.0, 3.0]])
        history_commit.assert_called_once_with("Move Fukusato Guide", "main")

    def test_auto_mapping_cancel_restores_nearest_anchor_baseline(self):
        saved_assets = auto_mapping._MAPPING_ASSETS
        try:
            auto_mapping._MAPPING_ASSETS = {
                "main": {auto_mapping.NEAREST_PROPERTY: {"arc": [8.0, 9.0]}}}
            auto_mapping._NEAREST_DRAG.update(
                frame=object(), moved=True, offset=(1.0, 2.0),
                had_original=True, original_arc=[3.0, 4.0])
            with mock.patch.object(auto_mapping, "_push_nearest_handle"):
                auto_mapping._nearest_handle_event({
                    "view": "main", "phase": "cancel"})
            self.assertEqual(
                auto_mapping._assets_for("main")[auto_mapping.NEAREST_PROPERTY]["arc"],
                [3.0, 4.0])
            self.assertFalse(auto_mapping._NEAREST_DRAG["moved"])
            self.assertEqual(auto_mapping._NEAREST_DRAG["offset"], (0.0, 0.0))

            auto_mapping._MAPPING_ASSETS = {
                "main": {auto_mapping.NEAREST_PROPERTY: {"arc": [8.0, 9.0]}}}
            auto_mapping._NEAREST_DRAG.update(
                frame=object(), moved=True, offset=(1.0, 2.0),
                had_original=False, original_arc=None)
            with mock.patch.object(auto_mapping, "_push_nearest_handle"):
                auto_mapping._nearest_handle_event({
                    "view": "main", "phase": "cancel"})
            self.assertNotIn(
                auto_mapping.NEAREST_PROPERTY,
                auto_mapping._assets_for("main"))
        finally:
            auto_mapping._MAPPING_ASSETS = saved_assets

    def test_cancelled_accepted_guide_edit_is_history_tracked(self):
        class Scene:
            def __init__(self):
                self.value = ""

            def script_data(self):
                return self.value

            def set_script_data(self, value):
                self.value = value

        scene = Scene()
        script_store.write(scene, workflow.STORE_KEY, {
            "next_id": 2,
            "guides": [{
                "id": 1, "frame": 0,
                "before": [[0, 0], [10, 0]],
                "after": [[5, 3], [15, 3]],
                "rollback_after": [[0, 0], [10, 0]],
                "accepted": False,
            }],
            "solutions": {},
        })
        history_commit = mock.Mock()
        runtime = SimpleNamespace(ui=SimpleNamespace(history_commit=history_commit))
        with (mock.patch.object(workflow, "_scene_model", return_value=scene),
              mock.patch.object(workflow, "_animean", return_value=runtime),
              mock.patch.object(workflow, "refresh_overlays")):
            workflow._overlay_action({}, {}, {
                "overlay": {"id": "fukusato-guide:1", "action": "remove"}})

        guide = workflow._state(scene)["guides"][0]
        self.assertTrue(guide["accepted"])
        self.assertEqual(guide["after"], [[0, 0], [10, 0]])
        self.assertNotIn("rollback_after", guide)
        history_commit.assert_called_once_with(
            "Cancel Fukusato Guide Edit", "main")


if __name__ == "__main__":
    unittest.main()
