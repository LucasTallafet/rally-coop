"""Lectura de trazados (GPX y GeoJSON), proyección a metros, remuestreo y suavizado.

Solo biblioteca estándar. Sin red. Los trazados descargados son datos no fiables: se leen con
xml.etree (sin entidades externas) y json, y se comprueba su forma antes de usarlos.
"""

import json
import math
import xml.etree.ElementTree as ET

EARTH_RADIUS_M = 6371008.8


class TrackError(ValueError):
    """El archivo no tiene la forma esperada."""


def _local(tag):
    return tag.rsplit("}", 1)[-1]


def read_gpx(text):
    """Puntos (lat, lon, ele|None) del primer trkseg con puntos, o de la primera ruta rte."""
    root = ET.fromstring(text)
    segments = []
    for seg in root.iter():
        name = _local(seg.tag)
        if name in ("trkseg", "rte"):
            pts = []
            for p in seg:
                if _local(p.tag) not in ("trkpt", "rtept"):
                    continue
                try:
                    lat = float(p.attrib["lat"])
                    lon = float(p.attrib["lon"])
                except (KeyError, ValueError) as e:
                    raise TrackError("punto GPX sin lat/lon válidos") from e
                ele = None
                for c in p:
                    if _local(c.tag) == "ele" and c.text and c.text.strip():
                        ele = float(c.text)
                pts.append((lat, lon, ele))
            if pts:
                segments.append(pts)
    if not segments:
        raise TrackError("el GPX no tiene puntos de traza")
    # Varios segmentos de una misma traza se encadenan.
    return [p for seg in segments for p in seg]


def _geojson_lines(obj):
    kind = obj.get("type")
    if kind == "FeatureCollection":
        for f in obj.get("features", []):
            yield from _geojson_lines(f)
    elif kind == "Feature":
        yield from _geojson_lines(obj.get("geometry") or {})
    elif kind == "LineString":
        yield obj["coordinates"]
    elif kind == "MultiLineString":
        yield from obj["coordinates"]
    elif kind == "GeometryCollection":
        for g in obj.get("geometries", []):
            yield from _geojson_lines(g)


def read_geojson(text):
    """Puntos (lat, lon, ele|None) de las líneas del GeoJSON, encadenadas (lon, lat, alt)."""
    pts = []
    for line in _geojson_lines(json.loads(text)):
        for c in line:
            if len(c) < 2:
                raise TrackError("coordenada GeoJSON con menos de dos valores")
            pts.append((float(c[1]), float(c[0]), float(c[2]) if len(c) > 2 else None))
    if not pts:
        raise TrackError("el GeoJSON no tiene líneas")
    return pts


def read_track(path):
    with open(path, encoding="utf-8") as f:
        text = f.read()
    if str(path).lower().endswith((".geojson", ".json")):
        return read_geojson(text)
    return read_gpx(text)


def project(points):
    """Proyección equirectangular local (x este, y norte, en m) alrededor del primer punto.

    Devuelve (xy, ele): ele es None si algún punto no trae altura.
    """
    lat0 = math.radians(points[0][0])
    lon0 = points[0][1]
    kx = math.radians(1.0) * EARTH_RADIUS_M * math.cos(lat0)
    ky = math.radians(1.0) * EARTH_RADIUS_M
    xy = [((p[1] - lon0) * kx, (p[0] - points[0][0]) * ky) for p in points]
    ele = [p[2] for p in points]
    if any(e is None for e in ele):
        ele = None
    return xy, ele


def cumulative(xy):
    s = [0.0]
    for i in range(1, len(xy)):
        s.append(s[-1] + math.dist(xy[i], xy[i - 1]))
    return s


def dedupe(xy, ele=None, min_dist=0.05):
    """Quita puntos repetidos (el remuestreo necesita longitud de cuerda > 0)."""
    out_xy = [xy[0]]
    out_e = [ele[0]] if ele else None
    for i in range(1, len(xy)):
        if math.dist(xy[i], out_xy[-1]) >= min_dist:
            out_xy.append(xy[i])
            if ele:
                out_e.append(ele[i])
    return out_xy, out_e


def _catmull_rom(p0, p1, p2, p3, u):
    """Catmull-Rom centrípeta (alpha 0,5) de p1 a p2, u en [0,1]. Barry-Goldman."""

    def knot(a, b):
        return max(math.dist(a, b), 1e-9) ** 0.5

    t0 = 0.0
    t1 = t0 + knot(p0, p1)
    t2 = t1 + knot(p1, p2)
    t3 = t2 + knot(p2, p3)
    t = t1 + (t2 - t1) * u

    def lerp(a, b, ta, tb):
        w = (t - ta) / (tb - ta)
        return (a[0] + (b[0] - a[0]) * w, a[1] + (b[1] - a[1]) * w)

    a1 = lerp(p0, p1, t0, t1)
    a2 = lerp(p1, p2, t1, t2)
    a3 = lerp(p2, p3, t2, t3)
    b1 = lerp(a1, a2, t0, t2)
    b2 = lerp(a2, a3, t1, t3)
    return lerp(b1, b2, t1, t2)


def resample(xy, ele, step):
    """Remuestrea a paso fijo de longitud de arco (m). La curva pasa por los puntos con
    Catmull-Rom centrípeta; la altura se interpola linealmente por longitud de arco.

    Devuelve (xy, ele, s) con s = 0, step, 2 step...
    """
    xy, ele = dedupe(xy, ele)
    if len(xy) < 2:
        raise TrackError("hace falta al menos dos puntos distintos")
    n = len(xy)
    ext = [(2 * xy[0][0] - xy[1][0], 2 * xy[0][1] - xy[1][1])] + xy
    ext.append((2 * xy[-1][0] - xy[-2][0], 2 * xy[-1][1] - xy[-2][1]))
    # Curva densa (~0,5 m) con su longitud de arco, y a partir de ella el remuestreo exacto.
    dense = []
    dense_src = []  # posición fraccionaria en los puntos originales, para la altura
    for i in range(n - 1):
        chord = math.dist(xy[i], xy[i + 1])
        m = max(2, int(chord / 0.5))
        for k in range(m):
            u = k / m
            dense.append(_catmull_rom(ext[i], ext[i + 1], ext[i + 2], ext[i + 3], u))
            dense_src.append(i + u)
    dense.append(xy[-1])
    dense_src.append(float(n - 1))
    ds = cumulative(dense)
    total = ds[-1]
    count = int(total // step) + 1
    out_xy, out_e, out_s = [], [], []
    j = 0
    for k in range(count):
        s = k * step
        while j < len(ds) - 2 and ds[j + 1] < s:
            j += 1
        seg = ds[j + 1] - ds[j]
        w = 0.0 if seg <= 0 else (s - ds[j]) / seg
        w = min(max(w, 0.0), 1.0)
        out_xy.append(
            (dense[j][0] + (dense[j + 1][0] - dense[j][0]) * w,
             dense[j][1] + (dense[j + 1][1] - dense[j][1]) * w)
        )
        out_s.append(s)
        if ele:
            f = dense_src[j] + (dense_src[j + 1] - dense_src[j]) * w
            i0 = min(int(f), n - 2)
            fr = f - i0
            out_e.append(ele[i0] + (ele[i0 + 1] - ele[i0]) * fr)
    return out_xy, (out_e if ele else None), out_s


def _kernel(sigma_m, step):
    if sigma_m <= 0:
        return [1.0]
    half = max(1, int(math.ceil(3.0 * sigma_m / step)))
    w = [math.exp(-0.5 * (k * step / sigma_m) ** 2) for k in range(-half, half + 1)]
    return w


def smooth(values, sigma_m, step):
    """Suavizado gaussiano de una serie a paso fijo. Los extremos se prolongan por simetría
    de punto (v[-k] = 2 v[0] - v[k]): conserva la pendiente de la serie y no arrastra el
    principio ni el final hacia dentro."""
    w = _kernel(sigma_m, step)
    half = len(w) // 2
    n = len(values)
    if half == 0 or n < 2:
        return list(values)

    def ext(i):
        if i < 0:
            return 2 * values[0] - values[min(-i, n - 1)]
        if i >= n:
            return 2 * values[-1] - values[max(2 * (n - 1) - i, 0)]
        return values[i]

    tot = sum(w)
    return [sum(w[k] * ext(i + k - half) for k in range(len(w))) / tot for i in range(n)]


def smooth_xy(xy, sigma_m, step):
    xs = smooth([p[0] for p in xy], sigma_m, step)
    ys = smooth([p[1] for p in xy], sigma_m, step)
    return list(zip(xs, ys))
