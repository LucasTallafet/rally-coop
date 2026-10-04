"""Tests de los scripts de telemetría (ctest: telemetry_py.*). Solo biblioteca estándar.

    python test_telemetry.py synthetic
        drift_check con CSV sintéticos generados aquí (criterio 5 de la tarea 2.3)
    python test_telemetry.py recording <telemetría.csv> <zona.json>
        carga un CSV de rally_replay y drift_check sobre él (la vuelta automática no hace maniobras)
"""

from __future__ import annotations

import json
import math
import sys
import tempfile
import unittest
from pathlib import Path

import drift_check
from telemetry import STEPS_PER_SECOND, load, to_csv

CENTER = (-96.0, 540.0)
RADIUS = 40.0
ZONE = {"circle": {"center": list(CENTER), "radius": RADIUS}}
DEG = math.pi / 180.0


def row(i, x, z, vx, vz, ax, az, beta_deg, throttle, brake=0.0, handbrake=0.0, steer=0.0):
    return {
        "step": i + 1,
        "time": f"{(i + 1) / STEPS_PER_SECOND:.6f}",
        "x": f"{x:.4f}",
        "y": "0.5000",
        "z": f"{z:.4f}",
        "vx": f"{vx:.4f}",
        "vy": "0.0000",
        "vz": f"{vz:.4f}",
        "speed": f"{math.hypot(vx, vz):.4f}",
        "beta": f"{beta_deg * DEG:.5f}",
        "ax": f"{ax:.3f}",
        "ay": "0.000",
        "az": f"{az:.3f}",
        "steer": f"{steer:.5f}",
        "throttle": f"{throttle:.5f}",
        "brake": f"{brake:.5f}",
        "handbrake": f"{handbrake:.5f}",
        "surface": "gravel",
    }


def circle_rows(profile, seconds, speed=15.0):
    """Recorrido por el círculo; profile(t) da (β°, acelerador, freno de mano, radio de a)."""
    rows = []
    omega = speed / RADIUS
    for i in range(int(round(seconds * STEPS_PER_SECOND))):
        t = (i + 1) / STEPS_PER_SECOND
        beta, throttle, handbrake, accel_radius = profile(t)
        theta = omega * t
        x = CENTER[0] + RADIUS * math.cos(theta)
        z = CENTER[1] + RADIUS * math.sin(theta)
        vx, vz = -speed * math.sin(theta), speed * math.cos(theta)
        a = speed * speed / accel_radius
        ax, az = -a * math.cos(theta), -a * math.sin(theta)
        rows.append(row(i, x, z, vx, vz, ax, az, beta, throttle, handbrake=handbrake))
    return rows


def sustained(sweep_deg, handbrake_touch=False):
    speed = 15.0
    drift_s = sweep_deg * DEG / (speed / RADIUS)
    lead = 1.0

    def profile(t):
        drifting = lead <= t < lead + drift_s
        handbrake = 0.0
        if handbrake_touch and abs(t - (lead + drift_s / 2)) < 0.05:
            handbrake = 0.5
        return (-30.0 if drifting else 0.0), 0.6, handbrake, RADIUS

    return circle_rows(profile, lead + drift_s + 1.0, speed)


def progressive(drop, spin_after=None):
    released = 2.0

    def profile(t):
        if t < released:
            return -30.0, 0.8, 0.0, RADIUS
        u = min(1.0, (t - released) / 0.4)
        beta = -30.0 * (1.0 - drop * u)
        if spin_after is not None and spin_after <= t - released < spin_after + 0.3:
            beta = -100.0
        return beta, 0.0, 0.0, RADIUS - 6.0 * u

    return circle_rows(profile, released + 2.5)


def recovery(recovery_s, peak_deg=30.0, speed=28.0):
    """En recta (lejos del círculo): β sube a −peak en 0,3 s, se mantiene y cae a 0 de golpe
    para que |β| suavizado baje de 5° justo recovery_s después de t0."""
    lead = steps(0.5)
    ramp = steps(0.3)
    t0 = lead + ramp + drift_check.SMOOTH_STEPS - 1  # primer paso con la media llena del pico
    drop = t0 + steps(recovery_s) - 20  # con 3 de 24 pasos a 30°, la media es 3,75° (< 5°)
    total = t0 + steps(3.0)
    rows = []
    for i in range(total):
        if i < lead:
            beta = 0.0
        elif i < lead + ramp:
            beta = -peak_deg * (i - lead + 1) / ramp
        elif i < drop:
            beta = -peak_deg
        else:
            beta = 0.0
        steer = -0.3 if lead <= i < t0 + steps(0.6) else 0.0
        z = -speed * (i + 1) / STEPS_PER_SECOND
        rows.append(row(i, 0.0, z, 0.0, -speed, 0.0, 0.0, beta, 0.7, steer=steer))
    return rows


def steps(seconds):
    return int(round(seconds * STEPS_PER_SECOND))


def analyze(rows):
    """Escribe el CSV como rally_replay, lo carga y lo analiza."""
    text = to_csv({"format": "1", "sim_version": "1", "recording": "synthetic.rrec",
                   "blind_surface": "gravel"}, rows)
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "synthetic.csv"
        path.write_text(text, encoding="utf-8")
        return drift_check.analyze(load(path), ZONE)


def show(report):
    return "\n".join(report.lines())


class Synthetic(unittest.TestCase):
    def test_sustained_200_passes_and_170_does_not(self):
        report = analyze(sustained(200.0))
        found = report.of("sostenido")
        self.assertEqual(len(found), 1, show(report))
        self.assertTrue(found[0].passed, show(report))
        print("\n[sostenido 200°]\n" + show(report))
        report = analyze(sustained(170.0))
        found = report.of("sostenido")
        self.assertEqual(len(found), 1, show(report))
        self.assertFalse(found[0].passed, show(report))
        self.assertIn("barrido", found[0].reasons[0])
        print("[sostenido 170°]\n" + show(report))

    def test_sustained_with_the_handbrake_touched_does_not_pass(self):
        report = analyze(sustained(200.0, handbrake_touch=True))
        found = report.of("sostenido")
        self.assertGreaterEqual(len(found), 1, show(report))
        self.assertFalse(any(m.passed for m in found), show(report))
        self.assertTrue(any("freno de mano" in r for m in found for r in m.reasons), show(report))
        print("\n[sostenido 200° con freno de mano]\n" + show(report))

    def test_progressive_needs_a_25_percent_drop_a_tighter_line_and_no_spin(self):
        cases = [(0.30, None, True), (0.15, None, False), (0.30, 1.5, False)]
        for drop, spin, expected in cases:
            report = analyze(progressive(drop, spin))
            found = report.of("progresivo")
            self.assertEqual(len(found), 1, show(report))
            self.assertEqual(found[0].passed, expected, show(report))
            print(f"\n[progresivo, caída {drop:.0%}, trompo {spin}]\n" + found[0].line())
        report = analyze(progressive(0.15))
        self.assertIn("baja un 15 %", report.of("progresivo")[0].reasons[0])
        # El salto a 100° a los 1,5 s pasa de 90° en la media de 0,1 s 21 pasos después.
        report = analyze(progressive(0.30, 1.5))
        self.assertIn("trompo a los 1,58 s", report.of("progresivo")[0].reasons[0])

    def test_recovery_in_1_6_s_passes_and_in_2_3_s_does_not(self):
        for seconds, expected in [(1.6, True), (2.3, False)]:
            report = analyze(recovery(seconds))
            found = report.of("recogida")
            self.assertEqual(len(found), 1, show(report))
            self.assertEqual(found[0].passed, expected, show(report))
            text = f"recogida en {seconds:.2f}".replace(".", ",")
            self.assertIn(text, found[0].figures)
            print(f"\n[recogida en {seconds} s]\n" + found[0].line())

    def test_beta_95_degrees_is_a_spin(self):
        self.assertTrue(drift_check.is_spin(95.0))
        self.assertTrue(drift_check.is_spin(-95.0))
        self.assertFalse(drift_check.is_spin(90.0))
        self.assertFalse(drift_check.is_spin(None))
        report = analyze(recovery(1.0, peak_deg=95.0))
        found = report.of("recogida")
        self.assertEqual(len(found), 1, show(report))
        self.assertFalse(found[0].passed)
        self.assertTrue(any(r.startswith("trompo") for r in found[0].reasons), show(report))
        print("\n[recogida con |β| = 95°]\n" + found[0].line())

    def test_a_slow_recovery_is_listed_as_discarded(self):
        report = analyze(recovery(1.0, speed=24.4))  # 88 km/h
        self.assertEqual(report.of("recogida"), [])
        self.assertTrue(
            any("88 km/h al empezar (< 95)" in d for d in report.discarded), show(report)
        )

    def test_beta_is_undefined_below_3_m_s(self):
        smoothed = drift_check.smooth_beta([1.0, 1.0, 3.0, 1.0], [5.0, 2.9, 5.0, 5.0])
        self.assertIsNotNone(smoothed[0])
        self.assertIsNone(smoothed[1])
        # Tras el hueco, la media vuelve a empezar: solo el paso definido.
        self.assertAlmostEqual(smoothed[2], drift_check.wrap_deg(3.0 / DEG))
        # Desenrollado: 179° y −179° promedian 180°, no 0°.
        wrapped = drift_check.smooth_beta([179 * DEG, -179 * DEG], [5.0, 5.0])
        self.assertAlmostEqual(abs(wrapped[1]), 180.0)


class Recording(unittest.TestCase):
    csv_path = ""
    zone_path = ""

    def test_loads_and_finds_no_maneuvers_on_the_blind_lap(self):
        tel = load(self.csv_path)
        self.assertEqual(tel.meta["format"], "2")
        # Las ruedas del coche en la pista ciega de grava (columnas de texto por rueda).
        self.assertIn("gravel", tel["surface_fl"])
        self.assertIn("recording", tel.meta)
        self.assertGreater(len(tel), 0)
        self.assertEqual(len(tel.names), len(set(tel.names)))
        self.assertEqual(tel["step"][0], 1.0)
        self.assertAlmostEqual(tel.time[-1], len(tel) / STEPS_PER_SECOND, places=5)
        beta_deg, unit = tel.display("beta")
        self.assertEqual(unit, "°")
        zone = json.loads(Path(self.zone_path).read_text(encoding="utf-8"))
        report = drift_check.analyze(tel, zone)
        print("\n" + show(report))
        self.assertEqual(report.maneuvers, [])
        lines = report.lines()
        self.assertEqual(lines[0], "ninguna maniobra encontrada")
        self.assertTrue(lines[1].startswith("descartados: "))


def main(argv):
    suite = argv[1] if len(argv) > 1 else ""
    if suite == "synthetic" and len(argv) == 2:
        tests = unittest.defaultTestLoader.loadTestsFromTestCase(Synthetic)
    elif suite == "recording" and len(argv) == 4:
        Recording.csv_path, Recording.zone_path = argv[2], argv[3]
        tests = unittest.defaultTestLoader.loadTestsFromTestCase(Recording)
    else:
        print(__doc__, file=sys.stderr)
        return 2
    result = unittest.TextTestRunner(stream=sys.stdout, verbosity=2).run(tests)
    return 0 if result.wasSuccessful() else 1


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv))
