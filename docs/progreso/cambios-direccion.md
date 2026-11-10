## Fila del registro

| Lote 6 · `cambios-direccion` | `src/game/direction_changes.{h,cpp}`: `DirectionChange {s_start, s_end, angle}`, `direction_changes(const sim::StageAxis&)` (al cargar; solo |ángulo| ≥ 5°), `next_direction_change` y `previous_direction_change` (búsqueda binaria, sin reservas, −1 si no hay). Criterio geométrico exacto sobre `StageAxis::primitives()`: cada primitiva se parte donde la curvatura cruza 0, un cambio es una racha de trozos del mismo signo, una recta lo cierra y la clotoide de inflexión da dos. Umbral de recta |κ| ≤ 1e-9 1/m. `lazadas.stage` da los seis cambios de la tabla del prompt (cifras en el informe). | Horas: | Validado a ojo: sin validar a ojo (lo prueba `tablet`) | Siguiente paso: `tablet` |

## Hallazgos

| Hallazgo | Dónde | Qué |
| --- | --- | --- |
| Sin hallazgos | | La premisa (las primitivas dan rumbo y curvatura de `pose()`) se comprueba en el test: |Δrumbo de `pose` − ángulo| ≤ 1e-9 rad en `lazadas` y en el sintético de 30 km. |

## Ediciones propuestas

Ninguna.

## Pide de lo compartido

Nada.
