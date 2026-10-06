# Telemetría

La partida solo graba entradas (`grabaciones/*.rrec` junto al exe). La telemetría se saca después con `rally_replay`, que repite la grabación paso a paso:

```powershell
$replay = "build/windows-release/tools/replay/Release/rally_replay.exe"
& $replay run grabaciones/20261004-120000.rrec --telemetry vuelta.csv
& $replay zone --json zona.json
```

El CSV tiene una fila por paso de 1/240 s, con unidades SI. Sus columnas, unidades y convenciones de signo están en [`columns.md`](columns.md), que se genera con `rally_replay columns --md`. Ocupa unos 3,4 MB por minuto con 33 columnas.

| Script | Uso | Necesita |
| --- | --- | --- |
| `telemetry.py` | `load("vuelta.csv")`: la cabecera en `.meta` y las columnas por nombre | Biblioteca estándar |
| `plot.py` | `python plot.py vuelta.csv beta yaw_rate speed [--from 10 --to 40] [--out vuelta.png]` | matplotlib |
| `compare.py` | `python compare.py a.csv b.csv beta speed [--out ab.png]` | matplotlib |
| `drift_check.py` | `python drift_check.py vuelta.csv zona.json`: prueba de derrape de `docs/02-arquitectura.md` §4.3 | Biblioteca estándar |
| `road_check.py` | `python road_check.py pasada.csv zona.json`: por cada pasada de la carretera de prueba (tarea 2.6b), tiempo, recolocaciones, salidas de la calzada, velocidad mínima en la horquilla y \|β\| máximo por curva | Biblioteca estándar |

- **Unidades en pantalla:** los scripts muestran km/h y grados.
- **matplotlib:** `python -m pip install --user -r tools/telemetry/requirements.txt`.
- **Salida de `drift_check.py`:** una línea por maniobra encontrada (sostenido, progresivo o recogida), con PASA o NO PASA, sus cifras y el motivo si no pasa. Después, los candidatos descartados y por qué.

Los tests (`telemetry_py.*` en ctest) están en `test_telemetry.py` y `test_plots.py`. `test_zone_json.py` es de la zona de pruebas (tarea 2.2).
