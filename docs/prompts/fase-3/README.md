# Fase 3 — Prompts

Prompts de las tareas de la fase 3 (corte vertical), en el orden en que entran en los lotes. Las reglas comunes son las de `docs/prompts/fase-2/comun.md` (su §13 dentro de un lote) hasta que la fase 3 tenga las suyas.

| Tarea | Lote | Carril | Depende de |
| --- | --- | --- | --- |
| `tramo-eje` | 3 | sim | — |
| `red-local` | 3 | juego | ENet en `cmake/` (paso 1 del lote 3) |
| `abs-sonido` | 3 | plataforma | — |
| `copiloto-ventana` | 3 | plataforma | `red-local`, `abs-sonido` |
| `tramo-seguimiento` | 4 | sim | `tramo-eje` |
| `f3-ancho` | 4 | plataforma | — |
| `tramo-fisica` | 4 | sim | `tramo-seguimiento` |
| `mirar-cabeza` | 4 | plataforma | `f3-ancho` |

Siguientes (lote 5, sin prompt todavía): formato 8 de la grabación con el tramo, modo «tramo» en el juego con su dibujo por ventanas, y la tablet (mapa, perfil, cinta y cursor) con el copiloto bot de desarrollo.
