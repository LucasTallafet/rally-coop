# Informe del analizador de tramos

Cifras sacadas de trazados abiertos (precisión de metros en planta; de decenas de metros en altura). **No dan irregularidad, ancho ni peralte**: esos valores salen de `docs/01` §7.3 y de la prueba. Cambio de dirección: tramo de curvatura de un solo signo con 5° o más (el criterio de `src/game/direction_changes.h`).

| Tramo | Long. (km) | Cambios | Cambios/km | Horquillas | Radio mín. mediana (m) | Radio mín. p10 (m) | Ángulo mediana (°) | Recta mediana (m) | Recta p90 (m) | Pendiente mediana (%) | Pendiente p90 (%) | Desnivel + / - (m) |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| medina_azahara | 3.10 | 24 | 7.74 | 3 | 47 | 20 | 82 | 5 | 5 | 7.5 | 10.6 | 225 / 3 |

## Parámetros del análisis

- medina_azahara: paso 5.0 m, suavizado 14.0 m, histéresis de curvatura 1/350 a 1/800, unión de rectas menores de 40.0 m, 210 puntos de entrada.

## Atribución

Los trazados descargados con `download.py` vienen de OpenStreetMap (© colaboradores de OpenStreetMap, licencia ODbL, https://www.openstreetmap.org/copyright). Las alturas, si las hay, de Open-Meteo (modelo Copernicus DEM GLO-90). Los trazados propios o sintéticos no llevan atribución.
