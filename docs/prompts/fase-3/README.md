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
| `irregularidad` | 7 | sim | — |
| `notas` | 7 | juego | — |
| `bordes-dibujo` | 7 | plataforma | — |
| `diales` | 7 | plataforma | `notas`, `bordes-dibujo` |
| `panel` | 7 | plataforma | `diales`; su apartado 5, `irregularidad` |
| `analizador` | 7 | herramientas | — |

Siguientes (lote 8, sin prompt todavía): el tramo de 3–5 km a mano, con irregularidad; el copiloto bot de desarrollo; la velocidad de referencia y el perfil de velocidad; los objetos que castigan el atajar. El dibujo en un hilo de fondo, cuando una medida lo pida.
