Fragmento de `analizador` (lote 7, carril herramientas). Rama `lote-7/analizador`.

## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-14 | 3 · lote 7 `analizador` | | Analizador reducido de tramos reales en `tools/stages/` (Python, solo biblioteca estándar): lee GPX y GeoJSON, proyecta a metros, remuestrea a 5 m (Catmull-Rom), suaviza (gaussiana de 14 m) y da, con el criterio del juego (`direction_changes.h`: curvatura de un solo signo, 5° o más), los cambios de dirección por kilómetro, la distribución de radio mínimo, ángulo, longitud y rectas, las horquillas y, con alturas, la pendiente y el desnivel; JSON y CSV por tramo e informe comparativo. `download.py` (Overpass + Open-Meteo) sigue la carretera entre salida y meta y escribe el GPX. `lazadas.stage` convertido a puntos da los seis cambios (ángulos a ±0,1°, `s_start` a ±1 m). Caso sintético con ±2 m de ruido y un punto cada 10-20 m: número de cambios exacto en 40 de 40 semillas, ángulos a ±6° salvo R 300 (12°), radios de 0,46 a 1,66 del real (horquilla de R 8: 1,39 a 1,64): el ±20 % y el ±40 % no se alcanzan (límite del método). Medina Azahara (CO-3314): 3,10 km, 24 cambios (7,7/km), 3 horquillas. 7 tests nuevos (`stages_py.*`). Huella, reservas, hashes y `kSimVersion` sin cambios | Tramo de 3–5 km del lote 8; coordenadas de Espiel, Villaviciosa, Turini, Fafe y El Cóndor |

## Hallazgos

| Fecha | Dónde | Hallazgo | Qué hacer |
| --- | --- | --- | --- |
| 2026-11-14 | `tools/stages` | Con ruido de ±2 m cada 10-20 m, una sola sigma no sirve para R 300 y para la horquilla de R 8. Con sigma 14 m el recuento es exacto, pero el radio de la horquilla sale ×1,4–1,6 y el de una curva de 25° y R 300, ×0,5–0,8 | Usar los radios del analizador como orden de magnitud; los radios de un tramo a mano salen de `01` y de la prueba |
| 2026-11-14 | `tools/stages` | Los giros de menos de 12° no se distinguen del ruido de un trazado de mapa: el perfil `map` los descarta (el 5° del juego solo vale con puntos limpios) | Aviso en el informe; con trazados más limpios se baja `noise_floor_deg` |
| 2026-11-14 | Medina Azahara | El trazado de OSM sigue la CO-3314 (13 m y 12 m del punto de salida y meta del libro de ruta): 3,10 km; es una carretera muy revirada (7,7 cambios/km, radio mínimo mediana 47 m, 3 horquillas, pendiente mediana 7,5 %). El modelo de alturas es de celdas de ~90 m | Referencia de «montaña revirada» para el tramo del lote 8 |

## Ediciones propuestas

**`docs/07-entorno-y-portabilidad.md`, sección nueva «Analizador de tramos reales»:**

> `tools/stages/` (Python 3.12+, solo biblioteca estándar; los tests son `stages_py.*` en ctest). `python tools/stages/analyze.py trazo.gpx --out salida/` escribe un JSON, un CSV con la lista de cambios de dirección (s, ángulo, radio mínimo: el guion para un `.stage` a mano) y `informe.md` con la tabla comparativa. `python tools/stages/download.py --name X --start lat,lon --end lat,lon [--ref CO-3314] [--elevation] --out X.gpx` baja la carretera de OpenStreetMap (Overpass) y alturas de Open-Meteo; necesita red y los datos llevan atribución ODbL. `measure.py` mide la precisión con el caso sintético. No da irregularidad, ancho ni peralte. Los resultados de Medina Azahara están en `tools/stages/datos/`.

**`docs/08`:** entrada nueva: «Lote 7, `analizador`. El suavizado de la posición con puntos de mapa fija un compromiso: con 14 m el recuento de curvas es exacto pero las curvas cerradas salen más abiertas. Los giros de menos de 12° no se pueden distinguir del ruido; el criterio de 5° del juego se aplica solo con puntos limpios (`lazadas` convertido: seis cambios). La pendiente de un modelo de ~90 m solo vale como orden de magnitud.»

**`docs/02`:** nada.

## Pide de lo compartido

Nada. Pendiente de Lucas: las coordenadas de salida y meta de Espiel, Villaviciosa, Col de Turini, Fafe y El Cóndor (no se inventan).
