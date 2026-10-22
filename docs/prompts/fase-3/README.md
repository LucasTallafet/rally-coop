# Fase 3 — Prompts

Prompts de las tareas de la fase 3 (corte vertical), en el orden en que entran en los lotes. Las reglas comunes son las de `docs/prompts/fase-2/comun.md` (su §13 dentro de un lote) hasta que la fase 3 tenga las suyas.

| Tarea | Lote | Carril | Depende de |
| --- | --- | --- | --- |
| `tramo-eje` | 3 | sim | — |
| `red-local` | 3 | juego | ENet en `cmake/` (paso 1 del lote 3) |
| `abs-sonido` | 3 | plataforma | — |
| `copiloto-ventana` | 3 | plataforma | `red-local`, `abs-sonido` |

Siguientes (lote 4, sin prompt todavía): física del tramo (malla de la calzada y franja de terreno, con su medida de coste) y seguimiento de `s` y `t`. Después, la tablet y el panel.
