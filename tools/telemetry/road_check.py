"""Resumen de las pasadas por la carretera de prueba (tarea 2.6b) a partir de la telemetría.

    python road_check.py <telemetría.csv> <zona.json>

La zona sale de `rally_replay zone --json <zona.json>` (eje de la carretera cada metro, piezas y
anchos). Por cada pasada de la grabación (de la salida a la meta, o la que quede a medias al
final): tiempo, recolocaciones, salidas de la calzada, velocidad mínima en la horquilla y |β|
máximo en cada curva. Solo biblioteca estándar.

Reglas:
- Pasada: los pasos seguidos con `road_time` ≥ 0 (recolocar no la corta; volver a cruzar la
  salida empieza otra). Completa si `road_runs` sube en el paso siguiente; su tiempo es el
  `road_time` de su último paso más un paso (lo mismo que el pie de la grabación).
- Salida de la calzada: tramo de pasos seguidos con el centro de masas a más de calzada/2 +
  arcén (3 + 1,6 m) del eje, es decir, fuera del arcén. Dos tramos separados por menos de
  0,2 s son la misma salida; los pasos de recolocar no cuentan (son un salto).
- La distancia y la s de cada paso son las del eje muestreado cada metro, afinadas en el
  segmento entre las dos muestras más cercanas.
- β solo con la velocidad horizontal ≥ 3 m/s (sin definir por debajo, como drift_check.py).
"""

from __future__ import annotations

import json
import math
import sys
from dataclasses import dataclass, field

from telemetry import STEPS_PER_SECOND, Telemetry, load

DEG = 180.0 / math.pi
BETA_MIN_SPEED = 3.0  # m/s horizontal
EXIT_MERGE_S = 0.2
RESET_BIT = 1 << 3  # sim::kButtonReset

CORNER_NAMES = {
    "fast_right": "2 · derecha rápida",
    "tightening_50": "4 · izquierda que se cierra (R 50)",
    "tightening_35": "4 · izquierda que se cierra (R 35)",
    "s_right": "5 · S, derecha",
    "s_left": "5 · S, izquierda",
    "crest_left": "6 · izquierda tras la cresta",
    "hairpin": "8 · horquilla",
    "climb_left": "9 · subida con izquierda",
}


@dataclass
class Road:
    axis: list[dict]  # s, x, z, heading, width, bank, height
    pieces: list[dict]
    length: float
    limit: float  # distancia del eje a la que se sale de la calzada (fuera del arcén)

    def piece_at(self, s: float) -> dict:
        found = self.pieces[0]
        for p in self.pieces:
            if s >= p["s0"]:
                found = p
        return found

    def project(self, x: float, z: float, hint: int | None = None) -> tuple[float, float, int]:
        """s y distancia al eje de (x, z), y el índice de la muestra más cercana. Con `hint`
        (la del paso anterior) busca cerca de ella; si queda lejos, en todo el eje."""
        def nearest(lo: int, hi: int) -> int:
            return min(range(lo, hi),
                       key=lambda i: (self.axis[i]["x"] - x) ** 2 + (self.axis[i]["z"] - z) ** 2)

        n = len(self.axis)
        best = None
        if hint is not None:
            lo, hi = max(0, hint - 30), min(n, hint + 31)
            best = nearest(lo, hi)
            d = math.dist((x, z), (self.axis[best]["x"], self.axis[best]["z"]))
            if d > 10.0 or (best in (lo, hi - 1) and 0 < best < n - 1):
                best = None
        if best is None:
            best = nearest(0, n)
        best_s = self.axis[best]["s"]
        best_d = math.dist((x, z), (self.axis[best]["x"], self.axis[best]["z"]))
        for j in (best - 1, best + 1):
            if 0 <= j < n:
                a = self.axis[min(best, j)]
                b = self.axis[max(best, j)]
                dx, dz = b["x"] - a["x"], b["z"] - a["z"]
                t = ((x - a["x"]) * dx + (z - a["z"]) * dz) / (dx * dx + dz * dz)
                t = min(1.0, max(0.0, t))
                px, pz = a["x"] + t * dx, a["z"] + t * dz
                d = math.dist((x, z), (px, pz))
                if d < best_d:
                    best_d = d
                    best_s = a["s"] + t * (b["s"] - a["s"])
        return best_s, best_d, best


def load_road(zone_path: str) -> Road:
    with open(zone_path, encoding="utf-8") as f:
        zone = json.load(f)
    road = zone["road"]
    fields = road["axis_fields"]
    axis = [dict(zip(fields, row)) for row in road["axis"]]
    return Road(axis=axis, pieces=road["pieces"], length=road["length"],
                limit=0.5 * road["width"] + road["shoulder_width"])


@dataclass
class Exit:
    start: int  # paso (índice de la fila)
    end: int
    s: float
    max_distance: float
    piece: str


@dataclass
class Run:
    first: int  # índice de la primera fila
    last: int   # índice de la última fila
    complete: bool
    steps: int  # pasos de la pasada (completa) o hasta el final
    resets: int
    exits: list[Exit] = field(default_factory=list)
    hairpin_min_kmh: float | None = None
    beta_max_deg: dict[str, float] = field(default_factory=dict)


def find_runs(tel: Telemetry) -> list[Run]:
    road_time = tel["road_time"]
    runs_col = tel["road_runs"]
    resets_col = tel["road_resets"]
    n = len(tel)
    out: list[Run] = []
    i = 0
    while i < n:
        if road_time[i] < 0.0:
            i += 1
            continue
        first = i
        while i + 1 < n and road_time[i + 1] >= 0.0 and road_time[i + 1] > road_time[i] - 1e-9:
            i += 1
        last = i
        complete = last + 1 < n and runs_col[last + 1] > runs_col[last]
        steps = round(road_time[last] * STEPS_PER_SECOND) + (1 if complete else 0)
        out.append(Run(first=first, last=last, complete=complete, steps=steps,
                       resets=int(resets_col[last])))
        i += 1
    return out


def analyse(tel: Telemetry, road: Road, run: Run) -> None:
    xs, zs = tel["x"], tel["z"]
    vx, vz = tel["vx"], tel["vz"]
    beta = tel["beta"]
    pressed = tel["pressed"]
    hairpin = next((p for p in road.pieces if p["name"] == "hairpin"), None)
    merge = round(EXIT_MERGE_S * STEPS_PER_SECOND)
    current: Exit | None = None
    gap = 0
    hint = None
    for i in range(run.first, run.last + 1):
        s, d, hint = road.project(xs[i], zs[i], hint)
        reset = int(pressed[i]) & RESET_BIT
        speed = math.hypot(vx[i], vz[i])
        piece = road.piece_at(s)["name"]
        if hairpin is not None and hairpin["s0"] <= s <= hairpin["s0"] + hairpin["length"]:
            kmh = 3.6 * speed
            run.hairpin_min_kmh = kmh if run.hairpin_min_kmh is None else min(run.hairpin_min_kmh,
                                                                              kmh)
        if piece in CORNER_NAMES and speed >= BETA_MIN_SPEED:
            run.beta_max_deg[piece] = max(run.beta_max_deg.get(piece, 0.0), abs(beta[i]) * DEG)
        out = d > road.limit and not reset
        if out:
            if current is not None and gap <= merge:
                current.end = i
                current.max_distance = max(current.max_distance, d)
            else:
                current = Exit(start=i, end=i, s=s, max_distance=d, piece=piece)
                run.exits.append(current)
            gap = 0
        else:
            gap += 1


def format_time(steps: int) -> str:
    seconds = steps / STEPS_PER_SECOND
    return f"{int(seconds // 60)}:{seconds % 60:06.3f}"


def report(tel: Telemetry, road: Road) -> list[Run]:
    runs = find_runs(tel)
    total_resets = sum(1 for p in tel["pressed"] if int(p) & RESET_BIT)
    print(f"grabación: {tel.meta.get('recording', '?')}, carretera de "
          f"{tel.meta.get('road_surface', '?')}; recolocaciones en el archivo: {total_resets}")
    if not runs:
        print("pasadas: ninguna (el coche no cruza la línea de salida)")
        return runs
    t0 = tel.time[0] if len(tel) else 0.0
    for k, run in enumerate(runs, start=1):
        analyse(tel, road, run)
        state = "completa" if run.complete else "a medias (sin meta)"
        mark = " con recolocación" if run.resets > 0 else ""
        print(f"pasada {k}: {state}{mark}, tiempo {format_time(run.steps)} ({run.steps} pasos), "
              f"recolocaciones: {run.resets}")
        print(f"  salidas de la calzada (centro de masas a más de {road.limit:.1f} m del eje): "
              f"{len(run.exits)}")
        for e in run.exits:
            start = tel.time[e.start] - t0
            duration = (e.end - e.start + 1) / STEPS_PER_SECOND
            print(f"    en t = {start:.2f} s, s = {e.s:.0f} m ({CORNER_NAMES.get(e.piece, e.piece)}): "
                  f"{duration:.2f} s, hasta {e.max_distance:.2f} m del eje")
        if run.hairpin_min_kmh is not None:
            print(f"  velocidad mínima en la horquilla: {run.hairpin_min_kmh:.1f} km/h")
        else:
            print("  velocidad mínima en la horquilla: no llega")
        for piece in CORNER_NAMES:
            if piece in run.beta_max_deg:
                print(f"  |β| máximo en {CORNER_NAMES[piece]}: {run.beta_max_deg[piece]:.1f}°")
    return runs


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print(__doc__.strip().splitlines()[2].strip(), file=sys.stderr)
        return 2
    sys.stdout.reconfigure(encoding="utf-8")
    tel = load(argv[1])
    road = load_road(argv[2])
    report(tel, road)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
