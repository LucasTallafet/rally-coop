# Fragmento: replay-tramo (lote 5, herramientas)

## Fila del registro
| Tarea | Qué se hizo | Tests | Huella y reservas | Validado a ojo | Horas |
| --- | --- | --- | --- | --- | --- |
| lote 5 · replay-tramo | `rally_replay info` con una grabación de formato 8 con tramo enseña el tramo (`tramo: <nombre>, <longitud> m, texto de N bytes`, de `StageTrack::build` sobre el texto del mundo), el `hash del suelo` de la cabecera y la `pasada` del pie (m:ss,cc con las centésimas truncadas, pasos, recolocaciones, atajos; «sin meta» si no la hay). Sin tramo, ni una línea más. Solo `tools/replay/main.cpp` y `tests/tools/CMakeLists.txt`. | 5 nuevos: `replay.info_stage_v8` (valores exactos), `replay.info_no_stage_v6`, `replay.info_no_stage_v8` (+ 2 de preparación: copia y `rewrite` de `zone_lap.rrec` al formato 8 sin tramo) | Sin cambios | Nada que probar jugando | |

## Hallazgos
| Hallazgo | Detalle |
| --- | --- |
| `run` sigue sin las columnas del tramo | `tools/replay/main.cpp` `CsvObserver::after_step` llama a `writer_.append(world.telemetry())` sin `world.stage_run()`: `run --telemetry` escribe las seis columnas a 0. El prompt dice que `run` no necesita cambios; el contrato §2.5 y `docs/07` línea 234 («a 0 hasta `replay-tramo`») suponían que sí. No tocado. |
| `stage_mismatch` sale como FALLO | La lista de avisos de `run` (`incomplete \|\| version_mismatch \|\| zone_mismatch`) no incluye `result.stage_mismatch`: un tramo cambiado da «FALLO» y código 1. No tocado; sin segunda grabación con otro tramo no hay test. |
| m:ss,cc trunca | 21 345 pasos = 88,9375 s se enseña como 1:28,93 (redondeado sería 1:28,94, como en el fragmento de `grabacion-v8`). `tramo-modo` debería usar la misma regla. |

## Ediciones propuestas
- `docs/07-entorno-y-portabilidad.md`, línea 234: si el director decide no hacer lo de `run`, cambiar «`rally_replay` las escribe a 0 hasta `replay-tramo`» por «`rally_replay run` las escribe a 0 (pendiente)». Si se hace, la frase se quita.

## Lo compartido
- Nada nuevo. Pendiente de decisión: pasar `world.stage_run()` a `writer_.append` y `|| result.stage_mismatch` en el aviso de `run` (dos líneas en `tools/replay/main.cpp`, mi ruta, pero fuera del prompt).
