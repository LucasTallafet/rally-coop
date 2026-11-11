## Fila del registro

| Lote 6 · `tramo-dibujo-2` | El dibujo del tramo con la banda del formato 2 (opción A del consultor). Cada triángulo del núcleo va al trozo de `sections()[t.section]` (se quita la suposición de 5 vértices por sección; antes, con 87 vértices por sección, casi todo caía en el último trozo: 194 119 de 205 902 triángulos y la distancia de dibujo no recortaba). Aspecto por `look`: muro de roca (100, 112, 138), cuneta y talud con la tierra x 0,82; calzada y arcén como antes. Una sola fuente (`platform/stage_look.h`) para la malla lejana (`StageLookStyle` en `build_core_mesh`) y la rejilla cercana: `StageHit` gana `look`, `StageNearSampler` escribe un índice de color nuevo en `texel.surface` (5 roca, 6 tierra de cuneta y talud) y `NearGrid` gana colores extra (índices >= 5; `ZoneDraw` no pasa ninguno). `tramo_salida.png` regenerada (la única captura que cambia; las otras 5, iguales byte a byte, zona incluida). Tests 710 → 715 (+5, `test_stage_draw.cpp`). Huella `0x66c9e3a86632750e`, reservas Jolt 1990, telemetría `0xfe3c612ddcaa6805`, `kSimVersion` 5: sin cambios. `--bench-stage` (RX 580, 1920x1080): CPU del dibujo 1,59 / 5,66 ms a 60 km/h y 3,03 / 9,82 ms a 120 km/h (lote 5: 1,51 / 5,01 a 60). Validado a ojo por Lucas: pendiente (prueba del lote). | | |

## Hallazgos

| Hallazgo | Dónde | Qué |
| --- | --- | --- |
| La cuenta vieja daba trozos de 94 % del tramo | `stage_chunks` | Con `min(a,b,c) / 5` y 87 vértices por sección, el índice es 17,4 veces mayor y se recorta a la última sección: un solo trozo con 194 119 de 205 902 triángulos y la caja de todo el tramo. No desaparece nada (la caja contiene sus triángulos): se pierde el recorte por distancia y se paga en coste. El test del intervalo k falla con esa cuenta (comprobado a mano y restaurado). |
| La roca de la primera propuesta cae sobre el asfalto | `stage_look.h` | Con el sombreado del muro (0,5 a 0,906) la roca (112, 118, 128) daba (94, 99, 107) a 80°, igual que el asfalto (96, 99, 106). Elegida (100, 112, 138): B - R = 38 frente a 10 del asfalto. Sombreada: de (50, 56, 69) a (90, 101, 124). En llano: asfalto (88, 90, 97), grava (181, 167, 130), tierra (150, 113, 77), tierra de cuneta (123, 92, 62). `lazadas` no tiene asfalto. El color lo juzga Lucas. |
| `StageSurface::query` frente a `probe_ground` con la banda | `test_stage_draw` | 3000 puntos hasta +-62 m: huecos distintos 0, superficies distintas 0. Diferencia de y máxima: 3,53 mm en el núcleo (lote 5: 0,23), 0,86 mm en el terreno (0,82) y 0,42 mm en 56 puntos de muro. El núcleo es 15 veces mayor que en el lote 5; no hay umbral y no es parada, pero lo dejo dicho (la cuneta tiene muchos más triángulos por celda). |
| Premisa de 80° | `test_stage_draw` | Menor ny/|n| = 0,209 (en un triángulo de arcén) frente a cos 80° = 0,1736: todos los triángulos tienen la normal hacia arriba. |
| Triángulos por aspecto en `lazadas` | `test_stage_draw` | Calzada 4792, arcén 4792, cuneta 191 430, talud 3662, muro 1226 (205 902 en total). |
| Hay muro y cuneta dentro de la rejilla cercana | `test_stage_draw` | Texels a menos de 27 m del ojo con el coche en el eje (de 36 625): s 633, roca 4278 y cuneta o talud 7959; s 747, 3933 y 9811; s 841, 5876 y 8869; s 946, 2657 y 9914. La pregunta 3 se puede contestar de cerca. |
| Coste | `--bench-stage` | Ver tabla del informe. Rejilla cercana 1,45 / 5,50 ms (lote 5: 1,36 / 4,83) a 60 km/h y 2,88 / 9,67 ms a 120 km/h; GPU 2,2 ms; triángulos 874 k (lote 5: 677 k); 19,4 MB de búferes (10,3); 133,5 draw calls. A 120 km/h el p99 del dibujo (9,8 ms) no pasa de 16,7 ms: no se esperan cuadros perdidos por el dibujo. `core_at` recorre uno a uno los triángulos de su celda de 4 m; con 87 vértices por sección hay muchos más que en el lote 5, pero no ha hecho falta tocarlo. Un índice por (s, t) o una rejilla de 1 m lo abarataría. |

## Ediciones propuestas

**`docs/02` (platform, dibujo del tramo):** añadir «Con la banda del formato 2 (lote 6), el trozo de cada triángulo sale de `StageTriangle::section`, no de sus índices de vértice. El aspecto (`StageTriangle::look`) decide el color: el muro, roca; la cuneta y el talud, tierra x 0,82; el resto, el de su superficie. La rejilla cercana usa los mismos colores con índices de color nuevos (`platform/stage_look.h`): `NearGrid` acepta colores extra y la zona no los usa.»

**`docs/08`:** (1) la roca del muro, (100, 112, 138), sale de que un gris casi neutro se confunde con el asfalto con el sombreado del muro; se decide con la prueba. (2) `core_at` de la rejilla cercana sigue siendo lineal por celda; si el coste molesta, un índice por sección.

## Preguntas para `prueba.md`

1. Con el coche entre los brazos de las lazadas (s 730–860), ¿se lee el muro como roca de cerca y de lejos, sin salto de color a 30 m del coche? ¿La roca (gris azulado) se distingue de la grava y la tierra?
2. ¿Se distinguen la cuneta y el talud (tierra más oscura) del terreno lejano? ¿Molesta la franja oscura a los lados de la calzada?
3. Botes en la ladera entre los brazos (s 633, 747, 841 y 946), avisos de `formato-2`: ¿hay botes o saltos al conducir por ahí?
4. El coste: fila «CPU» de F1 en `lazadas` a 60 y a 120 km/h (lote 5: ~1,5 ms de media; ahora ~1,6 y ~3,0 ms en `--bench-stage`).

## Pide de lo compartido

Nada.
