"""Dos grabaciones superpuestas: las mismas columnas de dos CSV de telemetría frente al tiempo.

    python compare.py <a.csv> <b.csv> <columna> [<columna> ...] [--out <imagen.png>]
                      [--from <s>] [--to <s>]

Un panel por columna con las dos curvas (en km/h y grados donde toca). Sin --out abre una
ventana; con --out guarda la imagen sin abrirla. Necesita matplotlib (requirements.txt).
"""

from __future__ import annotations

import sys

from plot import draw, parse_args
from telemetry import load


def main(argv: list[str]) -> int:
    args = parse_args(argv[1:], 2)
    first, second = load(args.csv0), load(args.csv1)
    labels = [first.meta.get("recording", args.csv0), second.meta.get("recording", args.csv1)]
    if labels[0] == labels[1]:
        labels = [args.csv0, args.csv1]
    draw([first, second], labels, args)
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv))
