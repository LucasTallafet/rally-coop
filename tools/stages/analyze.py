"""Analizador reducido de tramos reales: curvas por kilómetro, radios, rectas y pendientes.

    python analyze.py trazo1.gpx trazo2.geojson --out salida/

Escribe un JSON por tramo (<nombre>.json), la lista de cambios de dirección de cada tramo
(dentro del JSON y en <nombre>.cambios.csv, el guion para escribir un .stage a mano) y un
informe en Markdown que compara los tramos. Solo biblioteca estándar y sin red.

Qué NO da, porque los datos abiertos no tienen esa precisión: la irregularidad (centímetros),
el ancho ni el peralte. Esos valores salen de docs/01 §7.3 y de la prueba de Lucas.

Cambio de dirección: el mismo criterio que src/game/direction_changes.h. Un tramo de curvatura
de un solo signo con 5° o más de giro; dos curvas del mismo sentido con una recta en medio son
dos cambios; una clotoide de inflexión da dos, uno a cada lado de donde la curvatura pasa por 0.
En datos medidos hay ruido, así que el «signo» se decide con una histéresis (ver PARAMS).
"""

import argparse
import csv
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import geo  # noqa: E402

MIN_ANGLE_DEG = 5.0  # = kDirectionChangeMinAngle del juego
HAIRPIN_DEG = 150.0

# Perfiles de parámetros. step = paso del remuestreo (m); sigma = suavizado gaussiano de las
# posiciones y de la curvatura (m); k_on / k_off = histéresis de curvatura (1/m): un cambio
# se abre donde |kappa| supera k_on y se extiende hasta k_off (o hasta el cambio de signo);
# merge_gap = dos cambios del mismo signo con menos de esta recta entre ellos se unen (m).
PARAMS = {
    # Puntos sin ruido (un .stage convertido): sin suavizado, umbral del juego (1e-9) casi.
    "clean": dict(step=1.0, sigma=0.0, k_sigma=0.0, k_on=1 / 2000.0, k_off=1 / 4000.0,
                  merge_gap=0.0, noise_floor_deg=MIN_ANGLE_DEG, k_tail=1e-4, plateau_m=0.0, ele_sigma=0.0),
    # Trazados de mapa (OSM, GPX): puntos cada 10-20 m con ruido de metros. Valores medidos
    # con el caso sintético (test_stages.py, `measure`).
    "map": dict(step=5.0, sigma=14.0, k_sigma=5.0, k_on=1 / 350.0, k_off=1 / 800.0,
                merge_gap=40.0, noise_floor_deg=12.0, k_tail=0.0, plateau_m=40.0, ele_sigma=100.0),
}


def headings_and_curvature(xy, step, k_sigma):
    """Rumbo desenrollado (rad, horario desde el norte) y curvatura (1/m, + derecha)."""
    n = len(xy)
    theta = []
    prev = None
    for i in range(n):
        a = xy[max(i - 1, 0)]
        b = xy[min(i + 1, n - 1)]
        t = math.atan2(b[0] - a[0], b[1] - a[1])
        if prev is not None:
            while t - prev > math.pi:
                t -= 2 * math.pi
            while t - prev < -math.pi:
                t += 2 * math.pi
        theta.append(t)
        prev = t
    kappa = []
    for i in range(n):
        a = max(i - 1, 0)
        b = min(i + 1, n - 1)
        kappa.append((theta[b] - theta[a]) / ((b - a) * step) if b > a else 0.0)
    if k_sigma > 0:
        kappa = geo.smooth(kappa, k_sigma, step)
    return theta, kappa


def find_changes(s, kappa, theta, p):
    """Cambios de dirección: lista de dicts con s_start, s_end, angle (rad, + derecha) y
    radius_min (m)."""
    n = len(s)
    k_on, k_off = p["k_on"], p["k_off"]
    sign = [0] * n
    for i in range(n):
        if kappa[i] > k_off:
            sign[i] = 1
        elif kappa[i] < -k_off:
            sign[i] = -1
    # Núcleos: tramos contiguos del mismo signo (por encima de k_off) con algún |kappa| >= k_on.
    runs = []
    i = 0
    while i < n:
        if sign[i] == 0:
            i += 1
            continue
        j = i
        while j + 1 < n and sign[j + 1] == sign[i]:
            j += 1
        if max(abs(kappa[m]) for m in range(i, j + 1)) >= k_on:
            runs.append([i, j, sign[i]])
        i = j + 1
    # Se extienden hasta el paso por 0 de la curvatura: toman las colas de las clotoides.
    for run in runs:
        a, b, sg = run
        while a > 0 and kappa[a - 1] * sg > p["k_tail"]:
            a -= 1
        while b < n - 1 and kappa[b + 1] * sg > p["k_tail"]:
            b += 1
        run[0], run[1] = a, b
    # Con ruido, el signo de la curvatura baila en las rectas y salen cambios pequeños que no
    # existen: los de menos de min_angle (el 5° del juego en puntos limpios; más en un mapa) se
    # descartan antes de unir. Luego se unen los del mismo signo con una recta de menos de
    # merge_gap entre ellos (las colas se extendieron hasta kappa = 0 y pueden tocarse).
    min_angle = math.radians(max(p["noise_floor_deg"], MIN_ANGLE_DEG))
    runs = [r for r in runs if abs(theta[r[1]] - theta[r[0]]) >= min_angle]
    merged = []
    for run in runs:
        if merged:
            last = merged[-1]
            if run[2] == last[2] and s[run[0]] - s[last[1]] <= p["merge_gap"]:
                last[1] = max(last[1], run[1])
                continue
            if run[0] <= last[1]:
                run[0] = last[1] + 1
        merged.append(run)
    out = []
    win = int(round(p["plateau_m"] / (s[1] - s[0]))) if p["plateau_m"] > 0 else 0
    for idx, (a, b, sg) in enumerate(merged):
        theta_a, theta_b = theta[a], theta[b]
        if win >= 3:
            # El rumbo antes y después del cambio es una meseta (recta): se promedia en una
            # ventana que no entre en el cambio vecino, y así el ruido de los extremos no
            # mueve el ángulo.
            lo = merged[idx - 1][1] + 1 if idx > 0 else 0
            hi = merged[idx + 1][0] - 1 if idx + 1 < len(merged) else n - 1
            pre = range(max(lo, a - win), a + 1)
            post = range(b, min(hi, b + win) + 1)
            if len(pre) >= 3:
                theta_a = sum(theta[m] for m in pre) / len(pre)
            if len(post) >= 3:
                theta_b = sum(theta[m] for m in post) / len(post)
        angle = theta_b - theta_a
        if abs(angle) < min_angle:
            continue
        ks = sorted(abs(kappa[m]) for m in range(a, b + 1))
        k_peak = ks[int(0.9 * (len(ks) - 1))]
        core = [k for k in ks if k >= 0.75 * k_peak]
        k_core = sum(core) / len(core) if core else k_peak
        out.append({
            "s_start": s[a],
            "s_end": s[b],
            "angle": angle,
            "radius_min": 1.0 / k_core if k_core > 0 else float("inf"),
        })
    return out


def distribution(values):
    """n, mínimo, p10, mediana, p90, máximo y media; None si no hay valores."""
    if not values:
        return None
    v = sorted(values)

    def pct(q):
        pos = q * (len(v) - 1)
        lo = int(math.floor(pos))
        hi = min(lo + 1, len(v) - 1)
        return v[lo] + (v[hi] - v[lo]) * (pos - lo)

    return {
        "n": len(v),
        "min": v[0],
        "p10": pct(0.10),
        "median": pct(0.50),
        "p90": pct(0.90),
        "max": v[-1],
        "mean": sum(v) / len(v),
    }


def slope_stats(s, ele, step, p):
    """Pendiente (%) cada `step` m con la altura suavizada, y desnivel acumulado."""
    z = geo.smooth(ele, p["ele_sigma"], step) if p["ele_sigma"] > 0 else list(ele)
    slopes = []
    ascent = descent = 0.0
    for i in range(1, len(z)):
        dz = z[i] - z[i - 1]
        slopes.append(100.0 * dz / (s[i] - s[i - 1]))
        if dz > 0:
            ascent += dz
        else:
            descent -= dz
    return {
        "slope_percent": distribution([abs(x) for x in slopes]),
        "slope_signed_percent": distribution(slopes),
        "ascent_m": ascent,
        "descent_m": descent,
        "height_min_m": min(z),
        "height_max_m": max(z),
    }


def analyze(xy, ele, params, name="tramo"):
    """Análisis de un trazado ya proyectado. `ele` puede ser None."""
    p = dict(params)
    rxy, rele, s = geo.resample(xy, ele, p["step"])
    sxy = geo.smooth_xy(rxy, p["sigma"], p["step"])
    theta, kappa = headings_and_curvature(sxy, p["step"], p["k_sigma"])
    changes = find_changes(s, kappa, theta, p)
    length = s[-1]
    km = length / 1000.0
    deg = math.degrees
    straights = []
    prev_end = 0.0
    for c in changes:
        straights.append(c["s_start"] - prev_end)
        prev_end = c["s_end"]
    straights.append(length - prev_end)
    entries = [{
        "s_start": round(c["s_start"], 1),
        "s_end": round(c["s_end"], 1),
        "angle_deg": round(deg(c["angle"]), 1),
        "direction": "derecha" if c["angle"] > 0 else "izquierda",
        "radius_min_m": round(c["radius_min"], 1),
        "length_m": round(c["s_end"] - c["s_start"], 1),
    } for c in changes]
    res = {
        "name": name,
        "length_m": round(length, 1),
        "input_points": len(xy),
        "params": p,
        "direction_changes": len(changes),
        "direction_changes_per_km": round(len(changes) / km, 2) if km > 0 else None,
        "hairpins": sum(1 for c in changes if abs(deg(c["angle"])) >= HAIRPIN_DEG),
        "radius_min_m": distribution([c["radius_min"] for c in changes]),
        "angle_deg": distribution([abs(deg(c["angle"])) for c in changes]),
        "change_length_m": distribution([c["s_end"] - c["s_start"] for c in changes]),
        "straights_m": distribution(straights),
        "share_straight": round(sum(straights) / length, 3) if length > 0 else None,
        "changes": entries,
    }
    if rele is not None:
        res["height"] = slope_stats(s, rele, p["step"], p)
    return res


def analyze_file(path, params, name=None):
    pts = geo.read_track(path)
    xy, ele = geo.project(pts)
    base = name or os.path.splitext(os.path.basename(path))[0]
    res = analyze(xy, ele, params, base)
    res["source_file"] = os.path.basename(path)
    return res


def _f(d, key, fmt="{:.0f}"):
    return "-" if d is None else fmt.format(d[key])


def report_markdown(results, notes=""):
    lines = ["# Informe del analizador de tramos", ""]
    lines.append(
        "Cifras sacadas de trazados abiertos (precisión de metros en planta; de decenas de "
        "metros en altura). **No dan irregularidad, ancho ni peralte**: esos valores salen "
        "de `docs/01` §7.3 y de la prueba. Cambio de dirección: tramo de curvatura de un "
        "solo signo con 5° o más (el criterio de `src/game/direction_changes.h`)."
    )
    lines.append("")
    lines.append("| Tramo | Long. (km) | Cambios | Cambios/km | Horquillas | Radio mín. mediana (m) "
                 "| Radio mín. p10 (m) | Ángulo mediana (°) | Recta mediana (m) | Recta p90 (m) "
                 "| Pendiente mediana (%) | Pendiente p90 (%) | Desnivel + / - (m) |")
    lines.append("|" + " --- |" * 13)
    for r in results:
        h = r.get("height")
        slope_med = _f(h["slope_percent"], "median", "{:.1f}") if h else "-"
        slope_p90 = _f(h["slope_percent"], "p90", "{:.1f}") if h else "-"
        gain = f"{h['ascent_m']:.0f} / {h['descent_m']:.0f}" if h else "-"
        lines.append(
            f"| {r['name']} | {r['length_m'] / 1000:.2f} | {r['direction_changes']} "
            f"| {r['direction_changes_per_km']} | {r['hairpins']} "
            f"| {_f(r['radius_min_m'], 'median')} | {_f(r['radius_min_m'], 'p10')} "
            f"| {_f(r['angle_deg'], 'median')} | {_f(r['straights_m'], 'median')} "
            f"| {_f(r['straights_m'], 'p90')} | {slope_med} | {slope_p90} | {gain} |"
        )
    lines.append("")
    if notes:
        lines.append(notes)
        lines.append("")
    lines.append("## Parámetros del análisis")
    lines.append("")
    for r in results:
        p = r["params"]
        lines.append(
            f"- {r['name']}: paso {p['step']} m, suavizado {p['sigma']} m, histéresis de "
            f"curvatura 1/{1 / p['k_on']:.0f} a 1/{1 / p['k_off']:.0f}, unión de rectas "
            f"menores de {p['merge_gap']} m, {r['input_points']} puntos de entrada."
        )
    lines.append("")
    lines.append("## Atribución")
    lines.append("")
    lines.append(
        "Los trazados descargados con `download.py` vienen de OpenStreetMap "
        "(© colaboradores de OpenStreetMap, licencia ODbL, https://www.openstreetmap.org/copyright). "
        "Las alturas, si las hay, de Open-Meteo (modelo Copernicus DEM GLO-90). Los trazados "
        "propios o sintéticos no llevan atribución."
    )
    lines.append("")
    return "\n".join(lines)


def write_outputs(results, out_dir, notes=""):
    os.makedirs(out_dir, exist_ok=True)
    for r in results:
        with open(os.path.join(out_dir, r["name"] + ".json"), "w", encoding="utf-8") as f:
            json.dump(r, f, ensure_ascii=False, indent=2)
        with open(os.path.join(out_dir, r["name"] + ".cambios.csv"), "w", encoding="utf-8",
                  newline="") as f:
            w = csv.writer(f)
            w.writerow(["s_start_m", "s_end_m", "angle_deg", "direction", "radius_min_m"])
            for c in r["changes"]:
                w.writerow([c["s_start"], c["s_end"], c["angle_deg"], c["direction"],
                            c["radius_min_m"]])
    with open(os.path.join(out_dir, "informe.md"), "w", encoding="utf-8") as f:
        f.write(report_markdown(results, notes))


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("tracks", nargs="+", help="trazados .gpx o .geojson")
    ap.add_argument("--out", required=True, help="carpeta de salida")
    ap.add_argument("--profile", choices=sorted(PARAMS), default="map")
    ap.add_argument("--step", type=float)
    ap.add_argument("--sigma", type=float)
    args = ap.parse_args(argv)
    params = dict(PARAMS[args.profile])
    if args.step:
        params["step"] = args.step
    if args.sigma is not None:
        params["sigma"] = args.sigma
        params["k_sigma"] = args.sigma
    results = [analyze_file(t, params) for t in args.tracks]
    write_outputs(results, args.out)
    for r in results:
        print(f"{r['name']}: {r['length_m'] / 1000:.2f} km, {r['direction_changes']} cambios "
              f"({r['direction_changes_per_km']}/km), {r['hairpins']} horquillas")
    print(f"escrito: {os.path.join(args.out, 'informe.md')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
