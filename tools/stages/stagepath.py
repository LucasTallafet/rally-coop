"""Puntos de un trazado a partir de primitivas (rectas, arcos y clotoides) y del `.stage`.

Sirve para dos cosas: comprobar el analizador con un trazado cuyas curvas se conocen (el caso
sintético y `lazadas.stage`) y escribir tramos de prueba. Convención del juego: rumbo en
sentido horario desde el norte, R > 0 gira a la derecha.
"""

import math
import random


class Primitive:
    """Tramo de curvatura lineal: de k0 a k1 (1/m, + derecha) en `length` metros."""

    def __init__(self, length, k0, k1):
        self.length = length
        self.k0 = k0
        self.k1 = k1


def _inv(r):
    return 0.0 if r == "inf" else 1.0 / float(r)


def parse_stage_plan(text):
    """Primitivas y heights de un .stage (formato 1 y 2): [plan] y [profile].

    Devuelve (primitives, start_heading_rad, pvis) con pvis = [(s, altura)].
    """
    prims = []
    pvis = []
    heading = 0.0
    section = ""
    for raw in text.splitlines():
        line = raw.split("#", 1)[0].strip()
        if not line:
            continue
        if line.startswith("["):
            section = line.strip("[]").strip()
            continue
        if section == "":
            if "=" in line:
                key, value = [x.strip() for x in line.split("=", 1)]
                if key == "start_heading_deg":
                    heading = math.radians(float(value))
            continue
        parts = line.split()
        if section == "plan":
            kind = parts[0]
            if kind == "straight":
                prims.append(Primitive(float(parts[1]), 0.0, 0.0))
            elif kind == "arc":
                k = _inv(parts[2])
                prims.append(Primitive(float(parts[1]), k, k))
            elif kind == "spiral":
                prims.append(Primitive(float(parts[1]), _inv(parts[2]), _inv(parts[3])))
            else:
                raise ValueError(f"primitiva desconocida en [plan]: {kind}")
        elif section == "profile" and parts[0] == "pvi":
            pvis.append((float(parts[1]), float(parts[2])))
    return prims, heading, pvis


def integrate(prims, heading=0.0, step=1.0):
    """Puntos del eje cada `step` m (x este, y norte), con el rumbo exacto por primitiva.

    Devuelve (xy, s). Curvatura lineal dentro de cada primitiva; el rumbo se integra por
    la regla del trapecio exacta (la curvatura es lineal) y la posición, por el punto medio.
    """
    x = y = 0.0
    s_total = 0.0
    xy = [(x, y)]
    s = [0.0]
    for p in prims:
        n = max(1, int(round(p.length / step)))
        h = p.length / n
        for i in range(n):
            k_a = p.k0 + (p.k1 - p.k0) * (i / n)
            k_b = p.k0 + (p.k1 - p.k0) * ((i + 1) / n)
            d_theta = 0.5 * (k_a + k_b) * h
            mid = heading + 0.5 * d_theta
            x += math.sin(mid) * h
            y += math.cos(mid) * h
            heading += d_theta
            s_total += h
            xy.append((x, y))
            s.append(s_total)
    return xy, s


def pvi_heights(pvis, s_values):
    """Altura por interpolación lineal entre PIV (sin curvas verticales: aproximación)."""
    out = []
    j = 0
    for s in s_values:
        while j < len(pvis) - 2 and pvis[j + 1][0] < s:
            j += 1
        (s0, h0), (s1, h1) = pvis[j], pvis[j + 1]
        w = 0.0 if s1 == s0 else min(max((s - s0) / (s1 - s0), 0.0), 1.0)
        out.append(h0 + (h1 - h0) * w)
    return out


def subsample(xy, rng, step_min=10.0, step_max=20.0, noise=0.0):
    """Un punto cada step_min-step_max m, con ruido uniforme de ±`noise` m en cada eje.

    Conserva el primero y el último, como un trazado de mapa.
    """
    out = [xy[0]]
    acc = 0.0
    want = rng.uniform(step_min, step_max)
    for i in range(1, len(xy)):
        acc += math.dist(xy[i], xy[i - 1])
        if acc >= want or i == len(xy) - 1:
            out.append(xy[i])
            acc = 0.0
            want = rng.uniform(step_min, step_max)
    if noise > 0:
        out = [(p[0] + rng.uniform(-noise, noise), p[1] + rng.uniform(-noise, noise)) for p in out]
    return out


def synthetic_plan():
    """Caso sintético: rectas, arcos y clotoides de R 30, 60, 120 y 300 y una horquilla de
    R 8. Devuelve (primitivas, esperado) con esperado = [(s_inicio, angulo_deg, radio_min)].

    Cada curva es clotoide de entrada + arco + clotoide de salida; los ángulos se eligen
    para que haya de todo (cerradas, medias, abiertas, a ambos lados y una horquilla).
    """
    prims = []
    expected = []
    s = 0.0

    def straight(length):
        nonlocal s
        prims.append(Primitive(length, 0.0, 0.0))
        s += length

    def curve(radius, angle_deg, spiral):
        """radius con signo; angle_deg > 0 (valor absoluto). Clotoide L con L/(2R) por lado."""
        nonlocal s
        k = 1.0 / radius
        total = math.radians(angle_deg)
        spiral_angle = spiral / (2.0 * abs(radius))
        arc_len = (total - 2.0 * spiral_angle) * abs(radius)
        if arc_len < 0:
            raise ValueError("clotoide demasiado larga para el ángulo")
        expected.append((s, math.copysign(angle_deg, radius), abs(radius)))
        prims.append(Primitive(spiral, 0.0, k))
        prims.append(Primitive(arc_len, k, k))
        prims.append(Primitive(spiral, k, 0.0))
        s += 2 * spiral + arc_len

    straight(120)
    curve(120, 60, 30)
    straight(200)
    curve(-60, 90, 30)
    straight(150)
    curve(300, 25, 40)
    straight(250)
    curve(-30, 110, 25)
    straight(160)
    curve(60, 45, 20)
    straight(180)
    curve(-8, 180, 6)  # horquilla: arco de R 8 casi puro (clotoides cortas)
    straight(220)
    curve(-120, 35, 30)
    straight(150)
    return prims, expected


def noisy_track(prims, seed, noise=2.0, step_min=10.0, step_max=20.0):
    """Trazado de mapa fabricado: puntos cada step_min-step_max m y ruido de ±noise m."""
    rng = random.Random(seed)
    xy, _ = integrate(prims, 0.0, 0.5)
    return subsample(xy, rng, step_min, step_max, noise)
