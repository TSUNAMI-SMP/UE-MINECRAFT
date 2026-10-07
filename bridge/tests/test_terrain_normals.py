"""Check the actual JSON baker's cube face order against the production normal helper."""
import pathlib
import re
import unittest
ROOT = pathlib.Path(__file__).resolve().parents[2]


class TerrainNormalsTest(unittest.TestCase):
    def test_all_six_baker_faces_have_outward_cross_products(self):
        text = (ROOT / "unreal/UEBridge/Source/UEBridge/BridgeBlockGeometry.cpp").read_text()
        names = {"A": 0, "B": 0, "C": 0, "D": 1, "E": 1, "F": 1}
        expected = {"down": (0, -1, 0), "up": (0, 1, 0), "north": (0, 0, -1), "south": (0, 0, 1), "west": (-1, 0, 0), "east": (1, 0, 0)}
        for face, normal in expected.items():
            match = re.search(r'Pair.Key==TEXT\("' + face + r'"\)\)\s*\{(.*?)Normal=', text, re.S)
            self.assertIsNotNone(match, face)
            vertices = [(int(index), tuple(names[key.strip()] for key in expression.split(","))) for index, expression in re.findall(r"Face.Vertices\[(\d)\]=FVector\(([^)]+)\)", match.group(1))]
            self.assertEqual(list(range(4)), [index for index, unused in vertices])
            a, b, c = [position for unused, position in vertices[:3]]
            u, v = tuple(b[i] - a[i] for i in range(3)), tuple(c[i] - a[i] for i in range(3))
            cross = u[1] * v[2] - u[2] * v[1], u[2] * v[0] - u[0] * v[2], u[0] * v[1] - u[1] * v[0]
            self.assertEqual(normal, cross, face)
