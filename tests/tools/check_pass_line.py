"""Comprueba la línea de la pasada de `rally_replay` (lote 6, replay-desacople). Solo biblioteca
estándar. Los tests no copian la cifra de `stage_lazadas.rrec` (la calcula otro carril): comprueban
una relación.

    python check_pass_line.py --run <rally_replay> <grabacion.rrec>
    python check_pass_line.py <salida_info.txt> [<salida_run.txt>]

El primer modo lanza `rally_replay info` y `rally_replay run` y comprueba las dos salidas; el
segundo lee salidas ya guardadas. En cada una, la línea
`pasada: m:ss,cc (N pasos), R recolocaciones, A atajos` tiene que llevar el tiempo que
corresponde a los pasos: `m:ss,cc` es `N / 240` truncado a la centésima (regla del lote 5:
21 345 pasos son 88,9375 s = 1:28,93). Con dos salidas, la línea tiene que ser idéntica en las
dos. Sale con 1 y un mensaje si algo no cuadra.
"""

import re
import subprocess
import sys

STEPS_PER_SECOND = 240
PASS_LINE = re.compile(
    r"^pasada: +(\d+):(\d{2}),(\d{2}) \((\d+) pasos\), (\d+) recolocaciones, (\d+) atajos$",
    re.MULTILINE)


def read_text(source):
    if isinstance(source, tuple):
        exe, command, rrec = source
        # text=True traduce los \r\n de la consola de Windows a \n (si no, `$` no casa).
        out = subprocess.run([exe, command, rrec], capture_output=True, check=False,
                             text=True, encoding="utf-8", errors="replace")
        if out.returncode != 0:
            sys.exit(f"rally_replay {command} salió con {out.returncode}")
        return out.stdout
    with open(source, encoding="utf-8") as f:
        return f.read()


def check(source, label):
    matches = list(PASS_LINE.finditer(read_text(source)))
    if len(matches) != 1:
        sys.exit(f"{label}: se esperaba una línea de pasada con meta y hay {len(matches)}")
    m = matches[0]
    minutes, seconds, cents, steps = (int(m.group(i)) for i in range(1, 5))
    if seconds >= 60:
        sys.exit(f"{label}: segundos fuera de rango en «{m.group(0)}»")
    # Aritmética entera: pasos / 240 s, truncado a la centésima.
    expected = steps * 100 // STEPS_PER_SECOND
    shown = (minutes * 60 + seconds) * 100 + cents
    if shown != expected:
        sys.exit(f"{label}: «{m.group(0)}» no cuadra: {steps} pasos son {expected} centésimas "
                 f"y la línea dice {shown}")
    return m.group(0)


def main(sources):
    lines = [check(s, s if isinstance(s, str) else f"rally_replay {s[1]}") for s in sources]
    if len(lines) == 2 and lines[0] != lines[1]:
        sys.exit(f"la pasada difiere entre info y run:\n  info: {lines[0]}\n  run:  {lines[1]}")
    print(f"pasada correcta: {lines[0]}")


if __name__ == "__main__":
    args = sys.argv[1:]
    if len(args) == 3 and args[0] == "--run":
        main([(args[1], "info", args[2]), (args[1], "run", args[2])])
    elif 1 <= len(args) <= 2:
        main(args)
    else:
        sys.exit(__doc__)
