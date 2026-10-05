"""Test de plot.py, compare.py y tire_curves.py (ctest: telemetry_py.plot,
telemetry_py.compare y telemetry_py.tire_curves). Necesita matplotlib; escribe PNG sin abrir
ventana.

    python test_plots.py plot <telemetría.csv> <carpeta>
    python test_plots.py compare <telemetría.csv> <carpeta>
    python test_plots.py tire_curves <curvas.csv> <carpeta>
"""

from __future__ import annotations

import sys
from pathlib import Path

import compare
import plot
import tire_curves

COLUMNS = ["beta", "yaw_rate", "speed", "steer", "throttle"]


def main(argv: list[str]) -> int:
    if len(argv) != 4 or argv[1] not in ("plot", "compare", "tire_curves"):
        print(__doc__, file=sys.stderr)
        return 2
    command, csv, folder = argv[1], argv[2], Path(argv[3])
    folder.mkdir(parents=True, exist_ok=True)
    out = folder / f"{command}.png"
    if out.exists():
        out.unlink()
    if command == "tire_curves":
        return check_tire_curves(csv, out)
    if command == "plot":
        code = plot.main(["plot.py", csv, *COLUMNS, "--out", str(out), "--from", "10", "--to", "40"])
    else:
        code = compare.main(["compare.py", csv, csv, *COLUMNS, "--out", str(out)])
    if code != 0 or not out.exists() or out.stat().st_size < 10_000:
        print(f"FALLO: {out} no se escribió bien", file=sys.stderr)
        return 1
    # Una columna que no existe es un error con la lista de columnas.
    try:
        plot.main(["plot.py", csv, "no_existe", "--out", str(folder / "x.png")])
    except KeyError as error:
        if "columna desconocida: no_existe" not in str(error):
            return 1
    else:
        print("FALLO: una columna desconocida no dio error", file=sys.stderr)
        return 1
    print(f"imagen correcta: {out} ({out.stat().st_size} bytes)")
    return 0


def check_tire_curves(csv: str, out: Path) -> int:
    curves = tire_curves.load(csv)
    # Cinco superficies, dos direcciones y tres cargas.
    if len(curves) != 5 * 2 * 3:
        print(f"FALLO: {len(curves)} curvas en {csv}", file=sys.stderr)
        return 1
    # μ efectivo del pico lateral del asfalto a F_z0: el del archivo (1,10).
    peak = max(curves[("asphalt", "lateral", 1.0)].mu)
    if abs(peak - 1.10) > 0.01:
        print(f"FALLO: pico lateral del asfalto {peak}", file=sys.stderr)
        return 1
    code = tire_curves.main(["tire_curves.py", csv, "--out", str(out)])
    if code != 0 or not out.exists() or out.stat().st_size < 10_000:
        print(f"FALLO: {out} no se escribió bien", file=sys.stderr)
        return 1
    print(f"imagen correcta: {out} ({out.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv))
