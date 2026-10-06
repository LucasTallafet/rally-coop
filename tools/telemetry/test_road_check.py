"""Tests de road_check.py. Solo biblioteca estándar. Lo lanza ctest:

    python test_road_check.py synthetic
    python test_road_check.py recordings <road_run.csv> <road_off.csv> <zona.json>

`synthetic`: pasadas y salidas sobre una telemetría hecha a mano (una carretera recta de 100 m).
`recordings`: las dos grabaciones de test de la carretera (tests/data/recordings), repetidas
con `rally_replay run --telemetry`: la del conductor automático no se sale; la que entra en la
horquilla a 13 m/s se sale una vez, en la horquilla.
"""

import io
import math
import sys
import unittest
from contextlib import redirect_stdout

import road_check
from telemetry import STEPS_PER_SECOND, Telemetry

ARGS: list[str] = []


def straight_road(length=100.0) -> road_check.Road:
    axis = [{"s": float(s), "x": 0.0, "z": -float(s), "heading": 0.0, "width": 6.0, "bank": 0.0,
             "height": 0.0} for s in range(-20, int(length) + 101)]
    pieces = [{"name": "lead_in", "s0": -20.0, "length": 20.0},
              {"name": "start", "s0": 0.0, "length": length},
              {"name": "run_off", "s0": length, "length": 100.0}]
    return road_check.Road(axis=axis, pieces=pieces, length=length, limit=4.6)


def telemetry(rows: list[dict]) -> Telemetry:
    tel = Telemetry(meta={"recording": "a mano", "road_surface": "gravel"})
    tel.names = ["time", "x", "z", "vx", "vz", "beta", "pressed", "road_time", "road_resets",
                 "road_runs"]
    for name in tel.names:
        tel.columns[name] = [float(r.get(name, 0.0)) for r in rows]
    return tel


def drive(steps, lateral=lambda i: 0.0, reset_at=(), start=10, finish=None):
    """Coche a 10 m/s hacia −z desde s = −1; road_time desde el paso `start` hasta `finish`."""
    rows = []
    resets = 0
    runs = 0
    for i in range(steps):
        s = -1.0 + 10.0 * i / STEPS_PER_SECOND
        pressed = 8 if i in reset_at else 0
        in_run = i >= start and (finish is None or i < finish)
        if in_run and i in reset_at:
            resets += 1
        if finish is not None and i == finish:
            runs = 1
        rows.append({"time": i / STEPS_PER_SECOND, "x": lateral(i), "z": -s, "vz": -10.0,
                     "pressed": pressed,
                     "road_time": (i - start) / STEPS_PER_SECOND if in_run else -1.0,
                     "road_resets": resets if in_run else 0, "road_runs": runs})
    return telemetry(rows)


class Synthetic(unittest.TestCase):
    def run_check(self, tel, road=None):
        with redirect_stdout(io.StringIO()):
            return road_check.report(tel, road or straight_road())

    def test_complete_run_time_is_the_footer_time(self):
        runs = self.run_check(drive(3000, finish=2410))
        self.assertEqual(len(runs), 1)
        self.assertTrue(runs[0].complete)
        self.assertEqual(runs[0].steps, 2400)
        self.assertEqual(runs[0].exits, [])

    def test_one_exit_merges_short_gaps_and_ignores_resets(self):
        # Fuera del arcén de 1000 a 1100 y de 1120 a 1200 (una salida: 20 pasos de hueco < 0,2 s).
        def lateral(i):
            return 6.0 if 1000 <= i < 1100 or 1120 <= i < 1200 else 0.0
        runs = self.run_check(drive(3000, lateral=lateral, finish=2410))
        self.assertEqual(len(runs[0].exits), 1)
        e = runs[0].exits[0]
        self.assertEqual((e.start, e.end), (1000, 1199))
        self.assertAlmostEqual(e.max_distance, 6.0)
        # Un paso de recolocar fuera de la calzada no es una salida.
        runs = self.run_check(drive(3000, lateral=lambda i: 9.0 if i == 1500 else 0.0,
                                    reset_at={1500}, finish=2410))
        self.assertEqual(runs[0].exits, [])
        self.assertEqual(runs[0].resets, 1)

    def test_unfinished_run(self):
        runs = self.run_check(drive(1000))
        self.assertEqual(len(runs), 1)
        self.assertFalse(runs[0].complete)


class Recordings(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.road = road_check.load_road(ARGS[2])

    def check(self, path):
        tel = road_check.load(path)
        out = io.StringIO()
        with redirect_stdout(out):
            runs = road_check.report(tel, self.road)
        print(out.getvalue())
        return runs

    def test_clean_run_has_no_exit(self):
        runs = self.check(ARGS[0])
        self.assertEqual(len(runs), 1)
        self.assertTrue(runs[0].complete)
        self.assertEqual(runs[0].resets, 0)
        self.assertEqual(runs[0].exits, [])
        self.assertIsNotNone(runs[0].hairpin_min_kmh)

    def test_hairpin_overshoot_is_one_exit(self):
        runs = self.check(ARGS[1])
        self.assertGreaterEqual(len(runs), 1)
        exits = [e for r in runs for e in r.exits]
        self.assertEqual(len(exits), 1)
        self.assertEqual(exits[0].piece, "hairpin")
        self.assertGreater(exits[0].end, exits[0].start)


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    if len(sys.argv) >= 2 and sys.argv[1] == "synthetic":
        suite = unittest.TestLoader().loadTestsFromTestCase(Synthetic)
    elif len(sys.argv) == 5 and sys.argv[1] == "recordings":
        ARGS = sys.argv[2:]
        suite = unittest.TestLoader().loadTestsFromTestCase(Recordings)
    else:
        print(__doc__.strip(), file=sys.stderr)
        sys.exit(2)
    result = unittest.TextTestRunner(verbosity=2).run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)
