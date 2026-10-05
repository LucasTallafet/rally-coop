"""Curvas del neumático por superficie, del CSV de `rally_replay tire-curves`.

    rally_replay tire-curves data/cars/mid_awd_turbo.car --csv curvas.csv
    python tire_curves.py curvas.csv [--out <imagen.png>]

Dos paneles: fuerza lateral pura frente al ángulo de deriva y longitudinal pura frente al
deslizamiento, como μ efectivo (fuerza / F_z), con una línea por superficie a la carga estática
media por rueda (F_z0) y, finas, a 0,5 y 1,5·F_z0 (sensibilidad a la carga). Las curvas las
calcula el mismo código C++ que la simulación. Sin --out abre una ventana; con --out guarda la
imagen sin abrirla. Necesita matplotlib (requirements.txt).
"""

from __future__ import annotations

import argparse
import csv
import sys
from dataclasses import dataclass, field

# Color y nombre de cada superficie (el orden de Surface).
SURFACES = {
    "asphalt": ("asfalto", "#4a4a4a"),
    "gravel": ("grava", "#c08a3e"),
    "dirt": ("tierra", "#8b5a2b"),
    "snow": ("nieve", "#4f8fd8"),
    "sand": ("arena", "#d8c060"),
}
DIRECTIONS = {"lateral": "α (°)", "longitudinal": "κ"}


@dataclass
class Curve:
    slip: list[float] = field(default_factory=list)
    mu: list[float] = field(default_factory=list)  # fuerza / F_z


def load(path: str) -> dict[tuple[str, str, float], Curve]:
    """Curvas por (superficie, dirección, carga / F_z0)."""
    curves: dict[tuple[str, str, float], Curve] = {}
    with open(path, newline="", encoding="utf-8") as f:
        reader = csv.DictReader(f)
        expected = {
            "surface", "direction", "load_ratio", "load", "slip", "force", "force_over_peak"
        }
        if reader.fieldnames is None or set(reader.fieldnames) != expected:
            raise ValueError(f"{path}: no es un CSV de rally_replay tire-curves")
        for row in reader:
            if row["surface"] not in SURFACES or row["direction"] not in DIRECTIONS:
                raise ValueError(f"{path}: fila desconocida {row}")
            ratio = float(row["load_ratio"])
            curve = curves.setdefault((row["surface"], row["direction"], ratio), Curve())
            curve.slip.append(float(row["slip"]))
            curve.mu.append(float(row["force"]) / float(row["load"]))
    return curves


def draw(curves: dict[tuple[str, str, float], Curve], out: str | None) -> None:
    import matplotlib

    if out is not None:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    fig, axes = plt.subplots(1, 2, figsize=(13, 5))
    for ax, direction in zip(axes, DIRECTIONS):
        for key, (name, color) in SURFACES.items():
            for ratio, width, style in ((1.0, 2.0, "-"), (0.5, 0.8, ":"), (1.5, 0.8, "--")):
                curve = curves.get((key, direction, ratio))
                if curve is None:
                    continue
                label = name if ratio == 1.0 else None
                ax.plot(curve.slip, curve.mu, color=color, linewidth=width, linestyle=style,
                        label=label)
        ax.set_xlabel(DIRECTIONS[direction])
        ax.set_ylabel("fuerza / carga (μ efectivo)")
        ax.set_title(f"{direction}: F_z0 gruesa, 0,5·F_z0 punteada, 1,5·F_z0 a trazos")
        ax.grid(True, alpha=0.3)
        ax.set_ylim(bottom=0.0)
    axes[0].legend(loc="lower right")
    fig.tight_layout()
    if out is None:
        plt.show()
    else:
        fig.savefig(out, dpi=110)
        plt.close(fig)


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("csv", metavar="curvas.csv")
    parser.add_argument("--out", help="imagen de salida (sin ventana)")
    args = parser.parse_args(argv[1:])
    curves = load(args.csv)
    missing = [
        f"{key}/{direction}"
        for key in SURFACES
        for direction in DIRECTIONS
        if (key, direction, 1.0) not in curves
    ]
    if missing:
        print(f"error: faltan curvas en {args.csv}: {', '.join(missing)}", file=sys.stderr)
        return 2
    draw(curves, args.out)
    if args.out is not None:
        print(f"imagen escrita: {args.out}")
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv))
