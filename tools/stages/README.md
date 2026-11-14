# Analizador de tramos reales (`tools/stages`)

Solo biblioteca estándar de Python (3.12 o posterior). Da curvas por kilómetro, radios, rectas y pendientes de un trazado GPX o GeoJSON, y la lista de sus cambios de dirección (el guion para escribir un `.stage` a mano). **No da irregularidad, ancho ni peralte.**

## Uso

```
python tools/stages/analyze.py tramo1.gpx tramo2.geojson --out salida/
```

Escribe `salida/<nombre>.json`, `<nombre>.cambios.csv` (s, ángulo, sentido, radio mínimo) e `informe.md` con la tabla comparativa. `--profile clean` es para puntos sin ruido (un `.stage` convertido); `map` (por defecto) para trazados de mapa.

Descarga opcional (necesita red; OpenStreetMap vía Overpass y alturas de Open-Meteo, celdas de ~90 m):

```
python tools/stages/download.py --name medina_azahara --start 37.89028,-4.86194 --end 37.90139,-4.87056 --ref CO-3314 --elevation --out tools/stages/datos/medina_azahara.gpx
```

Las coordenadas van en grados decimales (latitud,longitud). `--ref` limita a las vías con ese `ref` de OSM. Comprueba siempre la longitud con el libro de ruta.

`python tools/stages/measure.py [ruido_m] [semillas] [paso_min] [paso_max]` mide la precisión con el caso sintético.

## Límites

Con ±2 m de ruido y un punto cada 10-20 m, el número de cambios sale exacto, los ángulos a unos ±6° y los radios entre 0,5 y 1,7 veces el real (peor en R 300 y en la horquilla). Los giros de menos de 12° no se distinguen del ruido en el perfil `map`. Ver el informe del lote 7.

Datos de OpenStreetMap: © colaboradores de OpenStreetMap (ODbL). Alturas: Open-Meteo (Copernicus DEM GLO-90).
