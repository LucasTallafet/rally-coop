"""Comprueba el JSON de la zona de pruebas (rally_replay zone --json) contra las medidas de la
tabla de la tarea 2.2. Solo biblioteca estándar. Lo lanza ctest:

    python test_zone_json.py <zone.json>

Las medidas se copian aquí a propósito: el test no lee las constantes de C++, así que un
cambio en la zona que no se haya pasado a la tabla salta.
"""

import json
import math
import sys
import unittest

ZONE_PATH = None


def polygon_bounds(ring):
    xs = [p[0] for p in ring]
    zs = [p[1] for p in ring]
    return min(xs), max(xs), min(zs), max(zs)


def ring_distance(a, b):
    """Distancia mínima entre los vértices de dos anillos (aproximada, muestreo de 1 m)."""
    return min(math.dist(p, q) for p in a for q in b)


class ZoneJson(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with open(ZONE_PATH, encoding="utf-8") as f:
            cls.zone = json.load(f)
        cls.areas = {a["name"]: a for a in cls.zone["areas"]}

    def test_all_areas_with_their_surfaces(self):
        expected = {
            "straight": "asphalt",
            "bumps_asphalt": "asphalt",
            "bumps_gravel": "gravel",
            "circle": "gravel",
            "plain": "gravel",
            "slalom": "gravel",
            "hairpin": "gravel",
            "ramp": "asphalt",
            "crest": "gravel",
            "patch_dirt": "dirt",
            "patch_snow": "snow",
            "patch_sand": "sand",
            "blind_track": self.zone["blind_surface"],
        }
        self.assertEqual(set(self.areas), set(expected))
        for name, surface in expected.items():
            self.assertEqual(self.areas[name]["surface"], surface, name)
        self.assertEqual(self.zone["ground"]["background"], "dirt")

    def test_rectangle_sizes(self):
        sizes = {
            "straight": (12, 1000),
            "bumps_asphalt": (12, 150),
            "bumps_gravel": (12, 150),
            "circle": (120, 120),
            "plain": (80, 400),
            "slalom": (20, 325),
            "ramp": (8, 130),
            "patch_dirt": (120, 120),
            "patch_snow": (120, 120),
            "patch_sand": (120, 120),
        }
        for name, (width, length) in sizes.items():
            x0, x1, z0, z1 = polygon_bounds(self.areas[name]["polygon"][0])
            self.assertAlmostEqual(x1 - x0, width, places=6, msg=name)
            self.assertAlmostEqual(z1 - z0, length, places=6, msg=name)

    def test_circle(self):
        circle = self.zone["circle"]
        self.assertAlmostEqual(circle["radius"], 40.0, delta=0.05)
        x0, x1, z0, z1 = polygon_bounds(self.areas["circle"]["polygon"][0])
        cx, cz = circle["center"]
        self.assertAlmostEqual(cx, (x0 + x1) / 2)
        self.assertAlmostEqual(cz, (z0 + z1) / 2)

    def test_hairpin_axis(self):
        axis = self.zone["hairpin_axis"]
        self.assertAlmostEqual(axis["width"], 6.0)
        kinds = [s["type"] for s in axis["segments"]]
        self.assertEqual(kinds, ["straight", "arc", "straight"])
        arc = axis["segments"][1]
        self.assertAlmostEqual(arc["radius"], 10.0, delta=0.1)
        self.assertAlmostEqual(abs(arc["turn"]), math.pi, places=9)
        self.assertAlmostEqual(axis["segments"][0]["length"], 100.0)
        self.assertAlmostEqual(axis["segments"][2]["length"], 100.0)
        # Los puntos del eje más allá del final de las rectas (en la dirección de la primera)
        # son los del arco y quedan a 10 m del centro.
        center = arc["center"]
        dx = math.sin(axis["segments"][0]["heading"])
        dz = -math.cos(axis["segments"][0]["heading"])
        on_arc = [p for p in axis["points"]
                  if (p[0] - center[0]) * dx + (p[1] - center[1]) * dz > 1e-9]
        self.assertGreater(len(on_arc), 25)
        for p in on_arc:
            self.assertAlmostEqual(math.dist(p, center), 10.0, delta=0.1)

    def test_blind_track_closed_loop(self):
        axis = self.zone["blind_track_axis"]
        self.assertTrue(axis["closed"])
        self.assertAlmostEqual(axis["width"], 7.0)
        self.assertGreater(axis["length"], 750.0)
        self.assertLess(axis["length"], 850.0)
        points = axis["points"]
        # Polígono cerrado de 1 m entre puntos: la suma de los lados es la longitud.
        perimeter = sum(math.dist(points[i], points[(i + 1) % len(points)])
                        for i in range(len(points)))
        self.assertAlmostEqual(perimeter, axis["length"], delta=1.0)
        # Exterior e interior de la calzada.
        self.assertEqual(len(self.areas["blind_track"]["polygon"]), 2)

    def test_finish_line_on_the_blind_track(self):
        line = self.zone["finish_line"]
        points = self.zone["blind_track_axis"]["points"]
        nearest = min(math.dist(p, line["center"]) for p in points)
        self.assertLess(nearest, 0.6)
        self.assertAlmostEqual(math.hypot(*line["forward"]), 1.0)
        self.assertGreaterEqual(line["half_length"], 3.5)

    def test_respawns(self):
        respawns = self.zone["respawns"]
        names = [r["name"] for r in respawns]
        self.assertEqual(len(names), len(set(names)))
        self.assertGreaterEqual(len(respawns), 12)
        self.assertEqual(names[0], "straight")
        half = -self.zone["ground"]["origin"][0]
        for r in respawns:
            x, z = r["position"]
            self.assertLess(abs(x), half, r["name"])
            self.assertLess(abs(z), half, r["name"])

    def test_ramp_and_crest(self):
        self.assertAlmostEqual(self.zone["ramp"]["grade"], 0.15, delta=0.003)
        self.assertAlmostEqual(self.zone["ramp"]["height"], 0.15 * 40.0, delta=0.01)
        self.assertAlmostEqual(self.zone["crest"]["height"], 1.2, delta=0.05)
        self.assertAlmostEqual(self.zone["crest"]["length"], 25.0)

    def test_slalom_cones(self):
        cones = self.zone["slalom_cones"]
        self.assertEqual(len(cones), 6)
        for a, b in zip(cones, cones[1:]):
            self.assertAlmostEqual(math.dist(a, b), 25.0)

    def test_areas_apart(self):
        straight_group = {"straight", "bumps_asphalt", "bumps_gravel"}
        names = sorted(self.areas)
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                if a in straight_group and b in straight_group:
                    continue
                # Solo se miran de cerca las que tienen las cajas a menos de 40 m.
                ax0, ax1, az0, az1 = polygon_bounds(self.areas[a]["polygon"][0])
                bx0, bx1, bz0, bz1 = polygon_bounds(self.areas[b]["polygon"][0])
                gap = math.hypot(max(0.0, bx0 - ax1, ax0 - bx1), max(0.0, bz0 - az1, az0 - bz1))
                if gap > 40.0:
                    continue
                d = ring_distance(densify(self.areas[a]["polygon"][0]),
                                  densify(self.areas[b]["polygon"][0]))
                self.assertGreaterEqual(d, 29.9, f"{a} y {b}")


def densify(ring, step=2.0):
    """Puntos del contorno cada `step` metros como mucho."""
    out = []
    for i, p in enumerate(ring):
        q = ring[(i + 1) % len(ring)]
        n = max(1, math.ceil(math.dist(p, q) / step))
        for k in range(n):
            t = k / n
            out.append((p[0] + t * (q[0] - p[0]), p[1] + t * (q[1] - p[1])))
    return out


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("uso: python test_zone_json.py <zone.json>", file=sys.stderr)
        sys.exit(2)
    ZONE_PATH = sys.argv.pop(1)
    unittest.main()
