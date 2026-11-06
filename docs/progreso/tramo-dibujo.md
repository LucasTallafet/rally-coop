# Fragmento: tramo-dibujo (lote 5, plataforma)

## Fila del registro
| Tarea | Qué se hizo | Tests | Huella y reservas | Validado a ojo | Horas |
| --- | --- | --- | --- | --- | --- |
| lote 5 · tramo-dibujo | `StageDraw` (`src/platform/stage_draw.{h,cpp}`): dibuja el tramo con la geometría de la física. `build` calcula `StageGround::build(track)` (misma función y argumentos que `SimWorld::build` con `Design`) y compara su hash con el de la física (si no coincide, false con los dos valores). Lejos: una malla por baldosa y una por tramo de ~64 m de s del núcleo (color por superficie, arcén en arena), solo los trozos a menos de 600 m del ojo; el núcleo con desplazamiento de polígonos gana al terreno (5 mm más bajo en el borde). Cerca: la rejilla en anillo de `ZoneDraw` extraída a `NearGrid` (`ZoneDraw` la usa igual), con la altura del suelo del tramo (`StageSurface`: núcleo + baldosas, colisión por triángulo como Jolt) y el microrrelieve de la rueda (`stage_relief_frame` y `stage_relief_height`). Líneas de salida y meta (lazadas no tiene parciales). `--bench-stage`: lazadas de s = −5 m a la meta a 60 km/h y, en la misma ejecución, la carretera de prueba. Lazadas, RX 580, 1920×1080: fotograma 16,73 ms (60 Hz), GPU 1,09 ms, 133 draw calls, 676 k triángulos, CPU del dibujo 1,50 ms de media (rejilla cercana 1,36). | de 626 a 637 (`stage_draw: …`, 11 nuevos) | Sin cambios (`kSimVersion` 5, huella `0x66c9e3a86632750e`, 1990 reservas, hashes) | Nada que probar jugando todavía (lo prueba Lucas con `tramo-modo`) | |

## Hallazgos
| Hallazgo | Detalle |
| --- | --- |
| La colisión de las baldosas es por triángulo, no por celda | Jolt quita cada triángulo si le falta una esquina con colisión (triángulo 0: (x,y),(x,y+1),(x+1,y+1); triángulo 1: (x,y),(x+1,y+1),(x+1,y)). El comentario de `src/sim/stage_ground.h` líneas 61–63 dice «cuatro esquinas»; es de sim: se propone cambiarlo a «las tres esquinas de cada triángulo». El dibujo y `StageSurface` lo hacen por triángulo. |
| `StageSurface` coincide con `probe_ground` | 3000 puntos hasta ±62 m del eje: 0 huecos distintos y 0 superficies distintas; diferencia de y máxima 0,23 mm en el núcleo y 0,82 mm en el terreno (cuantización de Jolt). |
| Rejilla cercana y la `s` del microrrelieve | El dibujo usa la `s` de la proyección más cercana (`acquire`) en vez de la `s` de la pasada del coche. En 1000 puntos de calzada, arcén y horquillas, 53 dan una h distinta de la de la rueda con la `s` del punto, con diferencia máxima 1,5e-12 mm: es cero a efectos prácticos. |
| La rejilla cercana cuesta 1,4 ms de media en el tramo | 4 µs por celda (`acquire` + marco + 3 ruidos + consulta del suelo) por 331 celdas nuevas de media a 60 km/h (máx. 1199 en un fotograma). p99 del dibujo 5,0 ms. Pasa del límite de 1 ms de `ZoneDraw` (que el prompt no impone). Si molesta: trocear el llenado en varios fotogramas o dar la `s` de partida con el triángulo del núcleo. No tocado. |
| `rally.exe --bench-road` de la rama frente a `integracion` | Mismo ejecutable construido en 3bec5ff: CPU del suelo media 0,325 frente a 0,306 ms, p99 0,572 frente a 0,587 ms, rejilla 0,269 frente a 0,248 ms. Un pico aislado de 21 ms en la rama (4491 celdas en un fotograma, un tirón del sistema) que no está en la otra pasada. |

## Ediciones propuestas
- `docs/02-arquitectura.md`, donde diga que el dibujo del tramo es del lote 5: «`StageDraw` (`platform/stage_draw`) dibuja el tramo con la `StageGround` que calcula con la misma función que la física y comprueba su hash; la rejilla cercana (`NearGrid`, compartida con `ZoneDraw`) lleva la altura del suelo del tramo (`StageSurface`) y el microrrelieve de la rueda. Solo se dibuja la franja de ±60 m del terreno; trozos a menos de 600 m del ojo.»
- `docs/02-arquitectura.md` §6.6 (y el comentario de `src/sim/stage_ground.h` líneas 61–63): la colisión de las baldosas es por triángulo (tres esquinas), no por celda con cuatro.
- `docs/07-entorno-y-portabilidad.md`: `rally.exe --bench-stage` (lazadas más la carretera de referencia; `rendimiento_tramo_NNN.txt` junto al exe).
- `docs/08-bitacora-de-desarrollo.md`: el núcleo y el terreno se solapan 5 mm en el borde del arcén; se resuelve con desplazamiento de polígonos (el núcleo gana), no recortando el terreno, para que «mismo número de triángulos que la física» se cumpla.

## Lo compartido
- `tramo-modo` necesita y no está: que `DemoWorld` construya el `StageDraw` con el `SimWorld` del modo tramo (hoy solo lo hace la medida), un `StageDraw` en el copiloto con el `stage_ground_hash` del host, y la `s` de arranque de la rejilla cercana si se quiere bajar su coste. `DemoContext::stage_draw` ya existe como gancho.
- La decisión de qué hacer con la rejilla cercana en `StageDraw` (1,4 ms) la toma el consultor con la cifra.
