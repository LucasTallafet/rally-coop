"""Curvas de la telemetría frente al tiempo, elegidas por nombre (columns.md).

    python plot.py <telemetría.csv> <columna> [<columna> ...] [--out <imagen.png>]
                   [--from <s>] [--to <s>]

Un panel por columna, en km/h y grados donde toca (telemetry.DISPLAY). Sin --out abre una
ventana; con --out guarda la imagen sin abrirla. Necesita matplotlib (requirements.txt).
"""

from __future__ import annotations

import argparse
import sys

from telemetry import Telemetry, load


def parse_args(argv: list[str], files: int) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    for i in range(files):
        parser.add_argument(f"csv{i}", metavar="telemetría.csv")
    parser.add_argument("columns", nargs="+", metavar="columna")
    parser.add_argument("--out", help="imagen de salida (sin ventana)")
    parser.add_argument("--from", dest="start", type=float, default=None, help="desde (s)")
    parser.add_argument("--to", dest="end", type=float, default=None, help="hasta (s)")
    return parser.parse_args(argv)


def pyplot(headless: bool):
    import matplotlib

    if headless:
        matplotlib.use("Agg")
    import matplotlib.pyplot as plt

    return plt


def window(tel: Telemetry, start: float | None, end: float | None) -> slice:
    times = tel.time
    first = 0 if start is None else next((i for i, t in enumerate(times) if t >= start), len(times))
    last = len(times) if end is None else next((i for i, t in enumerate(times) if t > end), len(times))
    return slice(first, last)


def draw(telemetries: list[Telemetry], labels: list[str], args: argparse.Namespace) -> None:
    for tel in telemetries:
        for name in args.columns:
            tel[name]  # columna desconocida: error con la lista antes de abrir nada
    plt = pyplot(args.out is not None)
    fig, axes = plt.subplots(
        len(args.columns), 1, sharex=True, squeeze=False, figsize=(11, 2.4 * len(args.columns))
    )
    for ax, name in zip(axes[:, 0], args.columns):
        unit = ""
        for tel, label in zip(telemetries, labels):
            part = window(tel, args.start, args.end)
            values, unit = tel.display(name)
            ax.plot(tel.time[part], values[part], linewidth=0.9, label=label)
        ax.set_ylabel(f"{name} ({unit})" if unit else name)
        ax.grid(True, linewidth=0.3)
        if len(telemetries) > 1:
            ax.legend(loc="upper right", fontsize="small")
    axes[-1, 0].set_xlabel("tiempo (s)")
    fig.suptitle(" · ".join(labels))
    fig.tight_layout()
    if args.out:
        fig.savefig(args.out, dpi=110)
        print(f"imagen: {args.out}")
    else:
        plt.show()


def main(argv: list[str]) -> int:
    args = parse_args(argv[1:], 1)
    tel = load(args.csv0)
    draw([tel], [tel.meta.get("recording", args.csv0)], args)
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv))
