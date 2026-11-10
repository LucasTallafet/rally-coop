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
| `tramo-ajustes` | 5 | sim | `tramo-fisica` |
| `cabina-volumen` | 5 | plataforma | `mirar-cabeza` |
| `f3-ejes` | 5 | plataforma | `cabina-volumen` |
| `grabacion-v8` | 5 | juego | `tramo-ajustes` |
| `tramo-dibujo` | 5 | plataforma | `tramo-ajustes`, `f3-ejes` |
| `replay-tramo` | 5 | herramientas | `grabacion-v8` |
| `tramo-modo` | 5 | plataforma | `tramo-dibujo`, `grabacion-v8` |
| `replay-desacople` | 6 | herramientas | — |
| `red-tramo` | 6 | juego | — |
| `formato-2` | 6 | sim | `replay-desacople`, `red-tramo` (para fusionarse) |
| `cambios-direccion` | 6 | juego | `red-tramo` |
| `tablet` | 6 | plataforma | `red-tramo`, `cambios-direccion` |
| `tramo-dibujo-2` | 6 | plataforma | `formato-2` |

Siguientes (lote 7, sin prompt todavía): la capa de irregularidad del trazado (antes del tramo de verdad); el modelo de nota, los diales, el panel del salpicadero, la velocidad de referencia y el copiloto bot de desarrollo; el tramo de 3–5 km y el dibujo en un hilo de fondo.
