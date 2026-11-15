"""Precisión del analizador con el caso sintético (rectas, arcos y clotoides de R 30, 60, 120,
300 y una horquilla de R 8, con ruido en los puntos).

    python measure.py [ruido_m] [semillas] [paso_min] [paso_max]

Imprime, por semilla y curva, cuántas veces el número de cambios no sale exacto y el error
máximo de ángulo y la razón radio medido / radio real. Sirve para elegir los parámetros de
PARAMS["map"] y para el informe.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import analyze as an  # noqa: E402
import stagepath as sp  # noqa: E402


def measure(noise=2.0, seeds=40, step_min=10.0, step_max=20.0, params=None):
    prims, exp = sp.synthetic_plan()
    p = params or an.PARAMS["map"]
    bad = 0
    n = len(exp)
    dang = [0.0] * n
    ratio = [[9.0, 0.0] for _ in range(n)]
    for seed in range(seeds):
        xy = sp.noisy_track(prims, seed, noise, step_min, step_max)
        r = an.analyze(xy, None, p)
        if r["direction_changes"] != n:
            bad += 1
            continue
        for i, (c, e) in enumerate(zip(r["changes"], exp)):
            dang[i] = max(dang[i], abs(abs(c["angle_deg"]) - abs(e[1])))
            q = c["radius_min_m"] / e[2]
            ratio[i] = [min(ratio[i][0], q), max(ratio[i][1], q)]
    return exp, bad, dang, ratio


def main(argv):
    noise = float(argv[1]) if len(argv) > 1 else 2.0
    seeds = int(argv[2]) if len(argv) > 2 else 40
    lo = float(argv[3]) if len(argv) > 3 else 10.0
    hi = float(argv[4]) if len(argv) > 4 else 20.0
    exp, bad, dang, ratio = measure(noise, seeds, lo, hi)
    print(f"ruido ±{noise} m, un punto cada {lo}-{hi} m, {seeds} semillas: "
          f"{bad} con número de cambios distinto de {len(exp)}")
    for e, d, r in zip(exp, dang, ratio):
        print(f"  R {e[2]:>5.0f} m, {abs(e[1]):>5.1f}°: error de ángulo máx. {d:4.1f}°, "
              f"radio medido/real {r[0]:.2f} a {r[1]:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
