"""Carga la telemetría en CSV que escribe `rally_replay run <grabación> --telemetry <salida.csv>`.

Solo biblioteca estándar. Las columnas, sus unidades y sus signos están en `columns.md`.

    from telemetry import load
    tel = load("vuelta.csv")
    tel.meta["recording"], tel["beta"][100], tel.time[-1]
"""

from __future__ import annotations

import csv
import math
from dataclasses import dataclass, field
from pathlib import Path

STEPS_PER_SECOND = 240

# Columnas que no son números.
TEXT_COLUMNS = {"surface"}

# Conversión para mostrar: (factor, unidad). Lo que no está aquí se muestra tal cual.
_RAD = (180.0 / math.pi, "°")
_RAD_S = (180.0 / math.pi, "°/s")
_M_S = (3.6, "km/h")
DISPLAY = {
    "heading": _RAD,
    "beta": _RAD,
    "yaw_rate": _RAD_S,
    "vx": _M_S,
    "vy": _M_S,
    "vz": _M_S,
    "v_forward": _M_S,
    "v_right": _M_S,
    "v_up": _M_S,
    "speed": _M_S,
    "x": (1.0, "m"),
    "y": (1.0, "m"),
    "z": (1.0, "m"),
    "ax": (1.0, "m/s²"),
    "ay": (1.0, "m/s²"),
    "az": (1.0, "m/s²"),
    "a_forward": (1.0, "m/s²"),
    "a_right": (1.0, "m/s²"),
    "time": (1.0, "s"),
    "lap_time": (1.0, "s"),
    "impact": (1.0, "m/s"),
}


@dataclass
class Telemetry:
    meta: dict[str, str] = field(default_factory=dict)  # líneas "# clave=valor" de la cabecera
    names: list[str] = field(default_factory=list)  # columnas en el orden del archivo
    columns: dict[str, list] = field(default_factory=dict)
    path: str = ""

    def __getitem__(self, name: str) -> list:
        try:
            return self.columns[name]
        except KeyError:
            raise KeyError(f"columna desconocida: {name} (hay: {', '.join(self.names)})") from None

    def __len__(self) -> int:
        return len(self.columns[self.names[0]]) if self.names else 0

    @property
    def time(self) -> list[float]:
        return self["time"]

    def display(self, name: str) -> tuple[list[float], str]:
        """Valores de una columna en la unidad de los scripts (km/h, grados) y su unidad."""
        factor, unit = DISPLAY.get(name, (1.0, ""))
        return [v * factor for v in self[name]], unit


def parse(text: str, path: str = "") -> Telemetry:
    tel = Telemetry(path=path)
    lines = text.splitlines()
    first = 0
    while first < len(lines) and lines[first].startswith("#"):
        entry = lines[first][1:].strip()
        if "=" in entry:
            key, value = entry.split("=", 1)
            tel.meta[key.strip()] = value.strip()
        first += 1
    reader = csv.reader(lines[first:])
    try:
        tel.names = next(reader)
    except StopIteration:
        raise ValueError(f"{path}: no hay fila de nombres") from None
    for name in tel.names:
        tel.columns[name] = []
    converters = [str if name in TEXT_COLUMNS else float for name in tel.names]
    for row_number, row in enumerate(reader, start=first + 2):
        if not row:
            continue
        if len(row) != len(tel.names):
            raise ValueError(
                f"{path}:{row_number}: {len(row)} valores y {len(tel.names)} columnas"
            )
        for name, convert, value in zip(tel.names, converters, row):
            tel.columns[name].append(convert(value))
    return tel


def load(path: str | Path) -> Telemetry:
    path = Path(path)
    return parse(path.read_text(encoding="utf-8"), str(path))


def to_csv(meta: dict[str, str], rows: list[dict]) -> str:
    """CSV con el mismo formato que rally_replay (para los tests): cabecera y filas."""
    names = list(rows[0].keys()) if rows else []
    out = ["# rally telemetry"]
    out += [f"# {key}={value}" for key, value in meta.items()]
    out.append(",".join(names))
    for row in rows:
        out.append(",".join(str(row[name]) for name in names))
    return "\n".join(out) + "\n"
