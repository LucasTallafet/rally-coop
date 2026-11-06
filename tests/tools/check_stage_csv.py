"""Comprueba el CSV de `rally_replay run --telemetry` de una grabación con tramo (lote 5,
replay-run): las seis columnas del tramo llevan valores, no ceros. Solo biblioteca estándar.

    python check_stage_csv.py <telemetria.csv>

`stage` vale 1 en todas las filas, `stage_s` no salta más de 1 m entre pasos seguidos (a
240 Hz y menos de 80 m/s son 0,33 m) salvo en el paso en que `stage_resets` cambia, y el
coche llega cerca de la meta (el tramo de test mide 1150 m).
"""

import sys

MAX_STEP_JUMP_M = 1.0
MIN_FINAL_S = 1100.0


def main(path):
    with open(path, encoding="utf-8") as f:
        lines = [line.rstrip("\n") for line in f if not line.startswith("#")]
    names = lines[0].split(",")
    rows = [dict(zip(names, line.split(","))) for line in lines[1:]]
    assert len(rows) > 1000, f"pocas filas: {len(rows)}"
    for column in ("stage", "stage_s", "stage_t", "stage_on_road", "stage_resets",
                   "stage_shortcuts"):
        assert column in names, f"falta la columna {column}"
    assert all(r["stage"] == "1" for r in rows), "stage no vale 1 en todas las filas"
    s = [float(r["stage_s"]) for r in rows]
    resets = [int(r["stage_resets"]) for r in rows]
    jumps = 0
    for i in range(1, len(rows)):
        if resets[i] != resets[i - 1]:
            jumps += 1
            continue
        assert abs(s[i] - s[i - 1]) <= MAX_STEP_JUMP_M, (
            f"stage_s salta {s[i] - s[i - 1]:.3f} m en la fila {i} sin recolocación")
    assert max(s) >= MIN_FINAL_S, f"stage_s máximo {max(s):.1f} m: no llega a la meta"
    assert max(resets) == 1, f"recolocaciones esperadas 1, hay {max(resets)}"
    print(f"CSV del tramo correcto: {len(rows)} filas, {jumps} recolocaciones, "
          f"stage_s hasta {max(s):.1f} m")


if __name__ == "__main__":
    main(sys.argv[1])
