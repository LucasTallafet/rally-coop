"""Prueba de aceptación del derrape (docs/02-arquitectura.md §4.3) sobre la telemetría.

    python drift_check.py <telemetría.csv> <zona.json>

La zona sale de `rally_replay zone --json <zona.json>` (centro y radio del círculo). Escribe una
línea por maniobra encontrada (sostenido, progresivo, recogida) con PASA o NO PASA, sus cifras y
el motivo si no pasa; después, los candidatos descartados con su motivo. Solo biblioteca
estándar.

Reglas (§4.3, «Implementación en drift_check.py»):
- β sin definir con velocidad horizontal < 3 m/s: esos pasos cortan las ventanas, no cuentan
  para trompo y no entran en la media móvil.
- β suavizado: media móvil hacia atrás de 24 pasos (0,1 s) sobre β desenrollado, solo con los
  pasos definidos seguidos; se devuelve a (−180°, 180°]. Trompo = |β suavizado| > 90°.
- Radio de la trayectoria = |v|³ / |v × a| en el plano (x, z), con v y a suavizados igual.
- Sostenido: ventana seguida con |β| > 15°, freno y freno de mano aplicados a 0 y el centro de
  masas a 40 ± 8 m del centro del círculo; pasa si barre ≥ 180° alrededor del centro (ángulo
  neto desenrollado). Se informa si dura ≥ 0,5 s.
- Progresivo: los 0,5 s antes de soltar cumplen el sostenido (salvo el barrido) con el
  acelerador aplicado ≥ 10 %; soltar (t_r) es el primer paso con el acelerador < 10 %. Pasa si
  |β(t_r + 0,5)| ≤ 0,75·|β(t_r)|, r(t_r + 0,5) < r(t_r) y no hay trompo en [t_r, t_r + 2 s].
- Recogida: en cada intervalo seguido con |β| ≥ 20°, t0 es el paso de |β| máximo; hace falta
  ≥ 95 km/h al empezar el intervalo. Contravolante: dirección aplicada con el signo de β y
  |dirección| ≥ 5 % durante ≥ 0,2 s seguidos entre el inicio del intervalo y t0 + 2 s. Tiempo de
  recogida: primer paso desde t0 a partir del cual |β| < 5° se mantiene 0,5 s seguidos, menos t0;
  pasa si es < 2 s, con contravolante y sin trompo desde t0 hasta el final de esa ventana.
"""

from __future__ import annotations

import json
import math
import sys
from dataclasses import dataclass, field
from pathlib import Path

from telemetry import STEPS_PER_SECOND, Telemetry, load

DEG = 180.0 / math.pi

SMOOTH_STEPS = 24  # 0,1 s
BETA_MIN_SPEED = 3.0  # m/s horizontal
SPIN_DEG = 90.0

SUSTAINED_BETA_DEG = 15.0
RING_TOLERANCE = 8.0  # m
SUSTAINED_SWEEP_DEG = 180.0
MIN_REPORT_S = 0.5

THROTTLE_RELEASED = 0.10
PROGRESSIVE_BEFORE_S = 0.5
PROGRESSIVE_AFTER_S = 0.5
PROGRESSIVE_DROP = 0.25
PROGRESSIVE_NO_SPIN_S = 2.0

RECOVERY_BETA_DEG = 20.0
RECOVERY_SPEED_KMH = 95.0
COUNTERSTEER_MIN = 0.05
COUNTERSTEER_S = 0.2
RECOVERED_BETA_DEG = 5.0
RECOVERED_HOLD_S = 0.5
RECOVERY_LIMIT_S = 2.0


def steps(seconds: float) -> int:
    return int(round(seconds * STEPS_PER_SECOND))


def wrap_deg(angle: float) -> float:
    """A (−180, 180]."""
    a = math.fmod(angle + 180.0, 360.0)
    if a <= 0.0:
        a += 360.0
    return a - 180.0


def is_spin(beta_deg: float | None) -> bool:
    return beta_deg is not None and abs(beta_deg) > SPIN_DEG


def fmt(value: float, decimals: int = 1) -> str:
    """Número con coma decimal, como en los documentos."""
    return f"{value:.{decimals}f}".replace(".", ",")


@dataclass
class Signals:
    time: list[float]
    speed_h: list[float]  # m/s horizontal
    beta: list[float | None]  # grados suavizados; None sin definir
    radius: list[float]  # m; inf en línea recta
    dist: list[float]  # m al centro del círculo
    angle: list[float]  # rad alrededor del centro
    throttle: list[float]
    brake: list[float]
    handbrake: list[float]
    steer: list[float]


def trailing_mean(values: list[float], n: int) -> list[float]:
    out = []
    total = 0.0
    for i, v in enumerate(values):
        total += v
        if i >= n:
            total -= values[i - n]
        out.append(total / min(i + 1, n))
    return out


def smooth_beta(raw_rad: list[float], speed_h: list[float]) -> list[float | None]:
    out: list[float | None] = []
    window: list[float] = []  # β desenrollado (grados) de los últimos pasos definidos seguidos
    previous = 0.0
    for beta, speed in zip(raw_rad, speed_h):
        if speed < BETA_MIN_SPEED:
            window.clear()
            out.append(None)
            continue
        deg = beta * DEG
        if window:
            deg = previous + wrap_deg(deg - previous)
        previous = deg
        window.append(deg)
        if len(window) > SMOOTH_STEPS:
            window.pop(0)
        out.append(wrap_deg(sum(window) / len(window)))
    return out


def signals(tel: Telemetry, zone: dict) -> Signals:
    cx, cz = zone["circle"]["center"]
    vx, vz, ax, az = tel["vx"], tel["vz"], tel["ax"], tel["az"]
    speed_h = [math.hypot(a, b) for a, b in zip(vx, vz)]
    svx, svz = trailing_mean(vx, SMOOTH_STEPS), trailing_mean(vz, SMOOTH_STEPS)
    sax, saz = trailing_mean(ax, SMOOTH_STEPS), trailing_mean(az, SMOOTH_STEPS)
    radius = []
    for a, b, c, d in zip(svx, svz, sax, saz):
        cross = abs(a * d - b * c)
        v = math.hypot(a, b)
        radius.append(v**3 / cross if cross > 1e-9 else math.inf)
    xs, zs = tel["x"], tel["z"]
    return Signals(
        time=tel.time,
        speed_h=speed_h,
        beta=smooth_beta(tel["beta"], speed_h),
        radius=radius,
        dist=[math.hypot(x - cx, z - cz) for x, z in zip(xs, zs)],
        angle=[math.atan2(z - cz, x - cx) for x, z in zip(xs, zs)],
        throttle=tel["throttle"],
        brake=tel["brake"],
        handbrake=tel["handbrake"],
        steer=tel["steer"],
    )


@dataclass
class Maneuver:
    kind: str  # "sostenido", "progresivo", "recogida"
    passed: bool
    time: float  # s, inicio (sostenido), t_r (progresivo) o t0 (recogida)
    figures: str
    reasons: list[str] = field(default_factory=list)

    def line(self) -> str:
        verdict = "PASA" if self.passed else "NO PASA"
        text = f"{self.kind.upper():<11} {verdict:<8} t={fmt(self.time, 2)} s  {self.figures}"
        if self.reasons:
            text += "  motivo: " + "; ".join(self.reasons)
        return text


@dataclass
class Report:
    maneuvers: list[Maneuver] = field(default_factory=list)
    discarded: list[str] = field(default_factory=list)

    def of(self, kind: str) -> list[Maneuver]:
        return [m for m in self.maneuvers if m.kind == kind]

    def lines(self) -> list[str]:
        out = [m.line() for m in self.maneuvers]
        if not self.maneuvers:
            out.append("ninguna maniobra encontrada")
        out.append(f"descartados: {len(self.discarded) if self.discarded else 'ninguno'}")
        out += ["  " + d for d in self.discarded]
        return out


def in_ring(s: Signals, i: int, radius: float) -> bool:
    return abs(s.dist[i] - radius) <= RING_TOLERANCE


def drifting(s: Signals, i: int) -> bool:
    """|β| > 15° con freno y freno de mano a 0 (sin mirar dónde)."""
    b = s.beta[i]
    return b is not None and abs(b) > SUSTAINED_BETA_DEG and s.brake[i] == 0.0 and s.handbrake[i] == 0.0


def end_reason(s: Signals, i: int, radius: float) -> str:
    if i >= len(s.time):
        return "fin de los datos"
    if s.beta[i] is None:
        return "β sin definir (< 3 m/s)"
    if s.handbrake[i] != 0.0:
        return "freno de mano"
    if s.brake[i] != 0.0:
        return "freno"
    if abs(s.beta[i]) <= SUSTAINED_BETA_DEG:
        return "|β| ≤ 15°"
    return f"fuera de 40 ± 8 m del centro ({fmt(s.dist[i])} m)"


def sweep_deg(s: Signals, first: int, last: int) -> float:
    total = 0.0
    for i in range(first + 1, last + 1):
        total += wrap_deg((s.angle[i] - s.angle[i - 1]) * DEG)
    return abs(total)


def check_sustained(s: Signals, radius: float, report: Report) -> None:
    n = len(s.time)
    i = 0
    while i < n:
        if not drifting(s, i):
            i += 1
            continue
        # Tramo derrapando sin frenos; dentro, las ventanas en el anillo.
        run_end = i
        while run_end < n and drifting(s, run_end):
            run_end += 1
        any_in_ring = False
        j = i
        while j < run_end:
            if not in_ring(s, j, radius):
                j += 1
                continue
            any_in_ring = True
            first = j
            while j < run_end and in_ring(s, j, radius):
                j += 1
            last = j - 1
            duration = (last - first + 1) / STEPS_PER_SECOND
            if duration < MIN_REPORT_S:
                report.discarded.append(
                    f"sostenido descartado (t={fmt(s.time[first], 2)} s): "
                    f"{fmt(duration, 2)} s (< {fmt(MIN_REPORT_S)} s)"
                )
                continue
            betas = [abs(s.beta[k]) for k in range(first, last + 1)]
            sweep = sweep_deg(s, first, last)
            speed = sum(s.speed_h[first : last + 1]) / (last - first + 1) * 3.6
            mean_dist = sum(s.dist[first : last + 1]) / (last - first + 1)
            passed = sweep >= SUSTAINED_SWEEP_DEG
            reasons = []
            if not passed:
                reasons.append(f"barrido {fmt(sweep, 0)}° (< 180°)")
                reasons.append("termina: " + end_reason(s, last + 1, radius))
            report.maneuvers.append(
                Maneuver(
                    "sostenido",
                    passed,
                    s.time[first],
                    f"barrido {fmt(sweep, 0)}°  |β| medio {fmt(sum(betas) / len(betas))}° "
                    f"máx {fmt(max(betas))}°  {fmt(duration, 2)} s  {fmt(speed, 0)} km/h  "
                    f"a {fmt(mean_dist)} m del centro",
                    reasons,
                )
            )
        duration = (run_end - i) / STEPS_PER_SECOND
        if not any_in_ring and duration >= MIN_REPORT_S:
            mean_dist = sum(s.dist[i:run_end]) / (run_end - i)
            report.discarded.append(
                f"sostenido descartado (t={fmt(s.time[i], 2)} s, {fmt(duration, 2)} s): fuera de "
                f"40 ± 8 m del centro (a {fmt(mean_dist)} m de media)"
            )
        i = run_end


def check_progressive(s: Signals, radius: float, report: Report) -> None:
    n = len(s.time)
    before = steps(PROGRESSIVE_BEFORE_S)
    after = steps(PROGRESSIVE_AFTER_S)
    no_spin = steps(PROGRESSIVE_NO_SPIN_S)
    for tr in range(1, n):
        if not (s.throttle[tr] < THROTTLE_RELEASED <= s.throttle[tr - 1]):
            continue
        # Candidato: suelta derrapando en el anillo.
        if not (drifting(s, tr - 1) and in_ring(s, tr - 1, radius)):
            continue
        held = 0
        k = tr - 1
        while (
            k >= 0
            and held < before
            and drifting(s, k)
            and in_ring(s, k, radius)
            and s.throttle[k] >= THROTTLE_RELEASED
        ):
            held += 1
            k -= 1
        if held < before:
            report.discarded.append(
                f"progresivo descartado (t={fmt(s.time[tr], 2)} s): derrape previo de "
                f"{fmt(held / STEPS_PER_SECOND, 2)} s (< {fmt(PROGRESSIVE_BEFORE_S)} s)"
            )
            continue
        check = tr + after
        if check >= n or s.beta[check] is None or s.beta[tr] is None:
            report.discarded.append(
                f"progresivo descartado (t={fmt(s.time[tr], 2)} s): sin β definido a los "
                f"{fmt(PROGRESSIVE_AFTER_S)} s de soltar"
            )
            continue
        b0, b1 = abs(s.beta[tr]), abs(s.beta[check])
        r0, r1 = s.radius[tr], s.radius[check]
        spin_at = next(
            (k for k in range(tr, min(n, tr + no_spin + 1)) if is_spin(s.beta[k])), None
        )
        reasons = []
        if b1 > (1.0 - PROGRESSIVE_DROP) * b0:
            reasons.append(f"|β| baja un {fmt(100.0 * (1.0 - b1 / b0), 0)} % (< 25 %)")
        if not r1 < r0:
            reasons.append(f"el radio no baja ({fmt(r0)} → {fmt(r1)} m)")
        if spin_at is not None:
            reasons.append(f"trompo a los {fmt((spin_at - tr) / STEPS_PER_SECOND, 2)} s")
        if tr + no_spin >= n:
            reasons.append("faltan datos para los 2 s sin trompo")
        report.maneuvers.append(
            Maneuver(
                "progresivo",
                not reasons,
                s.time[tr],
                f"|β| {fmt(b0)}° → {fmt(b1)}° ({fmt(-100.0 * (1.0 - b1 / b0), 0)} %)  "
                f"radio {fmt(r0)} → {fmt(r1)} m  {fmt(s.speed_h[tr] * 3.6, 0)} km/h",
                reasons,
            )
        )


def longest_countersteer(s: Signals, first: int, last: int) -> int:
    best = run = 0
    for k in range(first, last + 1):
        b = s.beta[k]
        if b is not None and abs(s.steer[k]) >= COUNTERSTEER_MIN and s.steer[k] * b > 0.0:
            run += 1
            best = max(best, run)
        else:
            run = 0
    return best


def check_recovery(s: Signals, report: Report) -> None:
    n = len(s.time)
    hold = steps(RECOVERED_HOLD_S)
    i = 0
    while i < n:
        b = s.beta[i]
        if b is None or abs(b) < RECOVERY_BETA_DEG:
            i += 1
            continue
        start = i
        while i < n and s.beta[i] is not None and abs(s.beta[i]) >= RECOVERY_BETA_DEG:
            i += 1
        end = i  # primer paso fuera del intervalo
        t0 = max(range(start, end), key=lambda k: abs(s.beta[k]))
        speed_start = s.speed_h[start] * 3.6
        if speed_start < RECOVERY_SPEED_KMH:
            report.discarded.append(
                f"recogida descartada (t={fmt(s.time[start], 2)} s): {fmt(speed_start, 0)} km/h "
                f"al empezar (< {fmt(RECOVERY_SPEED_KMH, 0)})"
            )
            continue
        limit = min(n - 1, t0 + steps(RECOVERY_LIMIT_S))
        counter = longest_countersteer(s, start, limit)
        recovered_at = None
        run = 0
        for k in range(t0, n):
            bk = s.beta[k]
            if bk is not None and abs(bk) < RECOVERED_BETA_DEG:
                run += 1
                if run == hold:
                    recovered_at = k - hold + 1
                    break
            else:
                run = 0
        spin_end = recovered_at + hold - 1 if recovered_at is not None else limit
        spin_at = next((k for k in range(t0, spin_end + 1) if is_spin(s.beta[k])), None)
        reasons = []
        if counter < steps(COUNTERSTEER_S):
            reasons.append(f"sin contravolante (≥ {fmt(COUNTERSTEER_S)} s)")
        if recovered_at is None:
            reasons.append("no se recoge")
            recovery_text = "sin recoger"
        else:
            recovery = (recovered_at - t0) / STEPS_PER_SECOND
            recovery_text = f"recogida en {fmt(recovery, 2)} s"
            if recovery >= RECOVERY_LIMIT_S:
                reasons.append(f"recogida en {fmt(recovery, 2)} s (≥ {fmt(RECOVERY_LIMIT_S)} s)")
        if spin_at is not None:
            reasons.append(f"trompo (|β| {fmt(abs(s.beta[spin_at]))}°)")
        betas = [abs(s.beta[k]) for k in range(start, end)]
        report.maneuvers.append(
            Maneuver(
                "recogida",
                not reasons,
                s.time[t0],
                f"{fmt(speed_start, 0)} km/h al empezar, {fmt(s.speed_h[t0] * 3.6, 0)} en t0  "
                f"|β| máx {fmt(max(betas))}°  contravolante {fmt(counter / STEPS_PER_SECOND, 2)} s  "
                f"{recovery_text}",
                reasons,
            )
        )


def analyze(tel: Telemetry, zone: dict) -> Report:
    s = signals(tel, zone)
    radius = float(zone["circle"]["radius"])
    report = Report()
    check_sustained(s, radius, report)
    check_progressive(s, radius, report)
    check_recovery(s, report)
    report.maneuvers.sort(key=lambda m: m.time)
    return report


def main(argv: list[str]) -> int:
    if len(argv) != 3:
        print("uso: python drift_check.py <telemetría.csv> <zona.json>", file=sys.stderr)
        return 2
    tel = load(argv[1])
    zone = json.loads(Path(argv[2]).read_text(encoding="utf-8"))
    name = tel.meta.get("recording", argv[1])
    print(f"{name}: {len(tel)} pasos ({fmt(len(tel) / STEPS_PER_SECOND, 2)} s)")
    for line in analyze(tel, zone).lines():
        print(line)
    return 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8")
    sys.exit(main(sys.argv))
