import math
import pathlib
import sys
import unittest


sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / "pyfile"))

import fukusato_mapping as mapping
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


if __name__ == "__main__":
    unittest.main()
