# Fragmento: replay-run (lote 5, arreglo de herramientas)

## Fila del registro
| Tarea | Qué se hizo | Tests | Huella y reservas | Validado a ojo | Horas |
| --- | --- | --- | --- | --- | --- |
| lote 5 · replay-run | `rally_replay run`: el CSV de `--telemetry` lleva las seis columnas del tramo con sus valores (`CsvObserver` pasa `world.stage_run()`); un tramo cambiado es un aviso («el tramo ha cambiado; no se compara el hash», código 0) y no un FALLO; con tramo en el mundo imprime la pasada repetida con el mismo formato que `info` (`pasada: 1:28,93 (21345 pasos), 1 recolocaciones, 0 atajos`). El reloj m:ss,cc ya truncaba (una sola función, `format_clock`). Solo `tools/replay/main.cpp` y `tests/tools/`. | 10 nuevos en `tests/tools/CMakeLists.txt` (3 de preparación de copias): `replay.run_stage_pass`, `replay.run_no_stage_pass`, `replay.run_stage_csv` + `replay.run_stage_csv_columns`, `replay.run_stage_changed`, `replay.info_stage_no_finish`, `replay.info_stage_invalid`; scripts `check_stage_csv.py` y `make_rrec_variant.py` | Sin cambios | Nada que probar jugando | |

## Hallazgos
| Hallazgo | Detalle |
| --- | --- |
| «tramo: no válido» no se alcanza | `parse_world` ya rechaza un tramo que no compila, así que `info` imprime `mundo: no válido (descripción del mundo: tramo no válido: ...)` y nunca llega a la rama `tramo: no válido` de `print_stage` (queda como defensa). El test comprueba lo que de verdad sale y que no salgan `pasada:` ni `hash del suelo:`. |
| Copias para los tests | `make_rrec_variant.py` (Python estándar, como los demás de `tests/tools`) cambia bytes de `stage_lazadas.rrec` sin variar ninguna longitud: `pvi 200 6.0 40` a `7.0` (suelo distinto), `format = 1` a `9` (tramo no válido) y los pasos del pie a -1 (sin meta). Sin Python 3.12 esos tests no se añaden (aviso en el configure). |

## Ediciones propuestas
- `docs/07-entorno-y-portabilidad.md`, línea ~234: quitar la frase condicional «; `rally_replay` las escribe a 0 hasta `replay-tramo`» (queda «... `stage_resets`, `stage_shortcuts`)»). En la línea de `run <archivo>`: añadir «con tramo, también la pasada repetida (`pasada: m:ss,cc (N pasos), R recolocaciones, A atajos`)» y «un tramo cambiado es un aviso, no un FALLO».

## Lo compartido
- Nada.
