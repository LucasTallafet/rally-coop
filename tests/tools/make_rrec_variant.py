"""Deriva de stage_lazadas.rrec una copia modificada para los tests de rally_replay
(lote 5, replay-run). Solo biblioteca estándar, y sin cambiar ninguna longitud.

    python make_rrec_variant.py <modo> <entrada.rrec> <salida.rrec>

Modos:
  changed-stage  el texto del tramo cambia (pvi 200 6.0 40 pasa a 7.0): el suelo ya no es el de la
                 cabecera.
  invalid-stage  el texto del tramo no es válido (format = 1 pasa a 9).
  no-finish      el pie dice que la pasada no llegó a la meta (pasos = -1).
"""

import struct
import sys


def replace_once(data, old, new):
    assert len(old) == len(new)
    assert data.count(old) == 1, f"{old!r} aparece {data.count(old)} veces"
    return data.replace(old, new)


def main(mode, source, target):
    with open(source, "rb") as f:
        data = f.read()
    if mode == "changed-stage":
        data = replace_once(data, b"pvi 200 6.0 40", b"pvi 200 7.0 40")
    elif mode == "invalid-stage":
        data = replace_once(data, b"format = 1", b"format = 9")
    elif mode == "no-finish":
        # Pie: ... pasos(i64) recolocaciones(u32) atajos(u32) "REND"
        assert data.endswith(b"REND")
        offset = len(data) - 4 - 16
        data = data[:offset] + struct.pack("<q", -1) + data[offset + 8:]
    else:
        raise SystemExit(f"modo desconocido: {mode}")
    with open(target, "wb") as f:
        f.write(data)


if __name__ == "__main__":
    main(*sys.argv[1:4])
