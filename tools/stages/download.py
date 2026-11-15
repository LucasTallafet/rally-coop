"""Descarga opcional de un tramo: baja las carreteras de la zona de OpenStreetMap (Overpass),
sigue la carretera más corta entre la salida y la meta y escribe un GPX; con --elevation añade
alturas de Open-Meteo (modelo Copernicus DEM GLO-90, celdas de unos 90 m).

    python download.py --name medina_azahara --start 37.89028,-4.86194 --end 37.90139,-4.87056 \
        --out datos/medina_azahara.gpx [--elevation]

Necesita red. El resto de las herramientas funcionan sin ella. Solo biblioteca estándar.
Los datos son de OpenStreetMap (© colaboradores de OpenStreetMap, ODbL): el informe del
analizador lleva la atribución. Un GPX de más de 200 KB o de licencia dudosa no va a git:
se guarda el JSON de resultados y esta orden.

La salida y la meta se piden como latitud,longitud en grados decimales (37°53′25″ N =
37,89028; 4°51′43″ O = -4,86194). La ruta sigue las vías con highway=* que se pueden
conducir (no peatonales ni escaleras): cuando la salida o la meta no cae sobre un nodo,
se toma el nodo más cercano, así que conviene comprobar la longitud con el libro de ruta.
"""

import argparse
import heapq
import json
import math
import sys
import urllib.error
import urllib.parse
import urllib.request

OVERPASS_URL = "https://overpass-api.de/api/interpreter"
# Open-Meteo: Copernicus DEM GLO-90, celdas de unos 90 m (resolución baja para pendientes).
ELEVATION_URL = "https://api.open-meteo.com/v1/elevation"
USER_AGENT = "rally-stage-tools/1.0"
# Vías por las que no corre un coche.
EXCLUDED = "footway|path|steps|cycleway|pedestrian|bridleway|corridor|construction|proposed"


def haversine(a, b):
    r = 6371008.8
    la1, lo1, la2, lo2 = map(math.radians, (a[0], a[1], b[0], b[1]))
    h = math.sin((la2 - la1) / 2) ** 2 + math.cos(la1) * math.cos(la2) * math.sin((lo2 - lo1) / 2) ** 2
    return 2 * r * math.asin(math.sqrt(h))


def bbox_query(start, end, pad_deg, ref=None):
    s = min(start[0], end[0]) - pad_deg
    n = max(start[0], end[0]) + pad_deg
    w = min(start[1], end[1]) - pad_deg
    e = max(start[1], end[1]) + pad_deg
    ref_filter = f'["ref"="{ref}"]' if ref else ""
    return (
        f'[out:json][timeout:90];way["highway"]["highway"!~"^({EXCLUDED})$"]{ref_filter}'
        f"({s:.5f},{w:.5f},{n:.5f},{e:.5f});(._;>;);out body;"
    )


def build_graph(data):
    """Grafo no dirigido de un JSON de Overpass: (nodes {id: (lat, lon)}, adj {id: [(id, m)]})."""
    nodes = {}
    for el in data.get("elements", []):
        if el.get("type") == "node":
            nodes[el["id"]] = (el["lat"], el["lon"])
    adj = {}
    for el in data.get("elements", []):
        if el.get("type") != "way":
            continue
        ids = [i for i in el.get("nodes", []) if i in nodes]
        for a, b in zip(ids, ids[1:]):
            d = haversine(nodes[a], nodes[b])
            adj.setdefault(a, []).append((b, d))
            adj.setdefault(b, []).append((a, d))
    return nodes, adj


def nearest_node(nodes, adj, point):
    best = None
    for i, p in nodes.items():
        if i not in adj:
            continue
        d = haversine(p, point)
        if best is None or d < best[0] or (d == best[0] and i < best[1]):
            best = (d, i)
    if best is None:
        raise ValueError("la zona no tiene carreteras")
    return best[1], best[0]


def shortest_path(adj, src, dst):
    """Dijkstra por longitud. Devuelve la lista de ids de nodo, o None si no hay camino."""
    dist = {src: 0.0}
    prev = {}
    heap = [(0.0, src)]
    while heap:
        d, u = heapq.heappop(heap)
        if u == dst:
            break
        if d > dist.get(u, math.inf):
            continue
        for v, w in adj.get(u, ()):
            nd = d + w
            if nd < dist.get(v, math.inf):
                dist[v] = nd
                prev[v] = u
                heapq.heappush(heap, (nd, v))
    if dst not in dist:
        return None
    path = [dst]
    while path[-1] != src:
        path.append(prev[path[-1]])
    return path[::-1]


def route_between(data, start, end):
    """Puntos (lat, lon) de la carretera entre salida y meta, y la distancia al nodo elegido
    en cada extremo."""
    nodes, adj = build_graph(data)
    a, da = nearest_node(nodes, adj, start)
    b, db = nearest_node(nodes, adj, end)
    path = shortest_path(adj, a, b)
    if path is None:
        raise ValueError("no hay carretera continua entre la salida y la meta en la zona")
    return [nodes[i] for i in path], da, db


def gpx_text(name, points):
    """GPX 1.1 con una traza. points = [(lat, lon, ele|None)] o [(lat, lon)]."""
    esc = name.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;")
    lines = ['<?xml version="1.0" encoding="UTF-8"?>',
             '<gpx version="1.1" creator="rally-stage-tools" xmlns="http://www.topografix.com/GPX/1/1">',
             f"<trk><name>{esc}</name><trkseg>"]
    for p in points:
        ele = p[2] if len(p) > 2 else None
        if ele is None:
            lines.append(f'<trkpt lat="{p[0]:.7f}" lon="{p[1]:.7f}"/>')
        else:
            lines.append(f'<trkpt lat="{p[0]:.7f}" lon="{p[1]:.7f}"><ele>{ele:.1f}</ele></trkpt>')
    lines.append("</trkseg></trk></gpx>")
    return "\n".join(lines) + "\n"


def _get(url, data=None):
    req = urllib.request.Request(url, data=data, headers={"User-Agent": USER_AGENT})
    with urllib.request.urlopen(req, timeout=120) as resp:
        return json.loads(resp.read().decode("utf-8"))


def fetch_overpass(start, end, pad_deg=0.01, ref=None):
    q = bbox_query(start, end, pad_deg, ref)
    return _get(OVERPASS_URL, urllib.parse.urlencode({"data": q}).encode("ascii"))


def fetch_elevations(points):
    """Alturas (m) de Open-Meteo para [(lat, lon)], en lotes de 100."""
    out = []
    for i in range(0, len(points), 100):
        chunk = points[i:i + 100]
        lat = ",".join(f"{p[0]:.6f}" for p in chunk)
        lon = ",".join(f"{p[1]:.6f}" for p in chunk)
        res = _get(f"{ELEVATION_URL}?latitude={lat}&longitude={lon}")
        out.extend(res["elevation"])
    return out


def parse_latlon(text):
    lat, lon = (float(x) for x in text.split(","))
    return lat, lon


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--name", required=True)
    ap.add_argument("--start", required=True, type=parse_latlon, help="lat,lon de la salida")
    ap.add_argument("--end", required=True, type=parse_latlon, help="lat,lon de la meta")
    ap.add_argument("--out", required=True, help="GPX de salida")
    ap.add_argument("--ref", help="solo vías con este ref de OSM (p. ej. CO-3314)")
    ap.add_argument("--pad", type=float, default=0.01, help="margen del recuadro, grados")
    ap.add_argument("--elevation", action="store_true", help="añade alturas de Open-Meteo")
    args = ap.parse_args(argv)
    try:
        data = fetch_overpass(args.start, args.end, args.pad, args.ref)
        pts, da, db = route_between(data, args.start, args.end)
        out = [(p[0], p[1], None) for p in pts]
        if args.elevation:
            ele = fetch_elevations(pts)
            out = [(p[0], p[1], e) for p, e in zip(pts, ele)]
    except (urllib.error.URLError, OSError, ValueError, KeyError) as e:
        print(f"descarga fallida: {e}", file=sys.stderr)
        return 1
    with open(args.out, "w", encoding="utf-8") as f:
        f.write(gpx_text(args.name, out))
    length = sum(haversine(a, b) for a, b in zip(pts, pts[1:]))
    print(f"{args.name}: {len(pts)} puntos, {length / 1000:.2f} km; el nodo de salida está a "
          f"{da:.0f} m y el de meta a {db:.0f} m de las coordenadas dadas -> {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
