# Fragmento de `formato-2` (lote 6, carril sim)

Estado: **parada en la parte B**: fallan los criterios 1a y 1b. Informe: `mensajes/formato-2-informe.md` de la copia de trabajo. Este fragmento se completa al cerrar la tarea.

## Fila del registro (provisional)

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-10 | 3 | | Tarea `formato-2` (lote 6), **parte B, parada por los criterios 1a y 1b**. Hecho: el formato 2 es el suelo del juego, con un solo generador. La malla lleva el núcleo y la banda de entorno: cuneta en coseno de 1,5 × 0,25 m, talud o muro de cabeza a cabeza hasta el eje medio con la otra parte del tramo, explanada de 5 m, rampa y abanico en los arcos cerrados. La rejilla sigue a la banda: bajo ella, siempre por debajo; junto a un final libre, la regla del borde de hoy. `StageTrack::build` valida y guarda la banda («s = …: no cabe el talud entre pasadas»; mínimo de 3,1 m entre núcleos), y `StageGround::build` la reutiliza. `section` y `look` entran en el hash. P1 77,92° en `lazadas`; P2 y P3 sin caras malas en los seis tramos de test; paso 0,046 / 0,120 ms (×0,89 / ×0,98 frente a la 2.6b); carga de `lazadas` 0,36 s y 2,1 MB; 30 km, 7,3 s y 41 MB. Fallan 1a (0–4 m, ×1,101 de la densa con límite ×1,10; p99 de 4–15 m, ×1,21) y 1b (4–15 m por fuera, hasta 7,3 % frente a 4,4 %, por el bote de la cuneta y no por la costura, que da 8,6 mm). `kExpectedStageGroundHash` `0xdd438cd676e8c121` → `0x56180e49eb1ff6a8`; `kExpectedStageStateHash` `0x06270f27dd850330` → `0x3d08dcb86b41ad6f`; `stage_lazadas.rrec` regenerada (1:28,94, 1 recolocación, 0 atajos). Huella `0x66c9e3a86632750e`, reservas 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5 sin cambios. Tests: Release 661/666 (los 2 de la parada y 3 de otros carriles que `integracion` ya arregla). Sin validar a ojo. | Respuesta del consultor a la parada de 1a y 1b |

## Hallazgos

| Fecha | Hallazgo | Cifra | Qué se hace |
| --- | --- | --- | --- |
| 2026-11-10 | El muro entre los brazos de `lazadas` cabe en 80° por poco | 77,92° en s 657,75 frente a 830,88 | Premisa P1 cumplida; 2,1° de margen |
| 2026-11-10 | La cuneta de 25 cm hace botar: la rueda pierde el contacto en ella y en la caída, a 2,5–5,5 m del borde | 1,5–2 m del borde: 95–100 % a 60 y 90 km/h; tabla del lote 5, 0–4 m: 27 % frente a 14 % con el formato 1 | Lo valora Lucas jugando (pregunta 2 de la prueba del lote) |
| 2026-11-10 | La costura banda–rejilla es pequeña junto a la explanada y grande en la rampa del exterior de la horquilla 1 | 8,6 mm por fuera de la R 80; 295 mm en s 747,6 | Aviso (criterio 10) |
| 2026-11-10 | El terreno natural del formato 1 sigue haciendo paredes entre pasadas lejanas | 3,68 (74,8°) a 30–60 m del borde, en s 463 | Aviso aceptado (respuesta a la parte A, §2.1) |
| 2026-11-10 | La carga con la banda | `lazadas` 0,36 s (formato 1: 0,13 s); 30 km 7,3 s (2,6 s) | Aviso aceptado (§2.5) |

## Edición propuesta de `docs/02` §6.6

Sustituir los párrafos «**Física del tramo (lote 4, `sim/stage_ground.h`).**», «**Límites del formato 1 (avisos aceptados)**» y «**Cifras (i3-8100, Release)**» por:

> **Física del tramo (formato 2, lote 6, `sim/stage_ground.h`).** Un cuerpo estático con un compuesto {malla, compuesto de baldosas}, creado en `build()` antes de los cuerpos de la descripción. Se calcula al cargar, sin hilos, con `rmath`, y lo mismo sirve para el dibujo. Un solo generador de suelo.
> - **Malla:** por sección, el núcleo (calzada y arcén en un plano con el peralte) y, a cada lado, la banda de entorno, desde el vértice del borde del arcén y con su dirección y su pendiente (de los vértices, no de los semianchos del eje: la capa de irregularidad los moverá). La banda tiene una cuneta en coseno de 1,5 m × 0,25 m (18 tramos) y una tira recta de la cabeza de la cuneta a su final F:
>   - hasta el eje medio con la otra parte del tramo más cercana, si está a ≤ 10 m del borde, de cabeza a cabeza (talud si ≤ 1:1,5, muro hasta 80° si no);
>   - una explanada en el plano del arcén hasta 5 m del borde, sin otra parte a menos de 15 m;
>   - una rampa continua entre las dos.
>
>   En el interior de un arco cuyo eje medio es su centro, todas sus secciones acaban en un solo vértice (abanico). K = 43 vértices por sección (`vertices_per_section()`). Cada triángulo lleva su sección y su aspecto (calzada, arcén, cuneta, talud, muro; el muro es de tierra en la física y de roca en el dibujo). Las secciones, como en el lote 4, más las que pide la cuerda del final de la banda (1 cm en planta, 2 mm en alzado; en un muro, planta × pendiente ≤ 2 mm). Las aristas interiores son inactivas (≤ 0,3° entre triángulos).
> - **Validación** (`StageTrack::build`, que guarda la banda para `StageGround::build`): «s = …: no cabe el talud entre pasadas» si la cuneta y un talud de 5 cm no caben antes del eje medio o del centro de curvatura, si el muro pasaría de 80° o si algún triángulo de la banda queda invertido o sin área. Dos núcleos necesitan ≥ 3,1 m.
> - **Rejilla:** baldosas de `HeightFieldShape` de 64 × 64 muestras cada 0,75 m que comparten el borde, solo donde pasa la franja (colisión a menos de 60,5 m del eje).
>   - Bajo la banda queda 10 cm por debajo de la menor altura de la banda en ±1,1 m, menos su variación a lo largo de s.
>   - Junto a un final libre (explanada, rampa o recortada), en ±1,06 m, a S − 5 mm − e(s), con S la última tira prolongada y e(s) la cota de interpolación en el final; de ahí empalma en 4 m hasta el terreno natural.
>   - El terreno natural es la media de las alturas de los finales de la banda con peso (1 − paso suave)/d⁸, como en el formato 1.
>   - La rejilla nunca asoma sobre la malla (margen mínimo 3,8 mm).
> - **Avisos aceptados:**
>   - entre pasadas lejanas (más de 15 m), el terreno natural deja paredes en la rejilla (3,68 en `lazadas`, a 40 m del eje en s 463);
>   - la costura banda–rejilla llega a 295 mm en la rampa del exterior de la horquilla 1;
>   - por fuera de las curvas, la rejilla junto al final mezcla finales a otra altura (≤ 1,5 mm);
>   - el muro dice «tierra» al rozarlo (vibración, sonido y telemetría).
> - **Cifras (i3-8100, Release):** `lazadas.stage` (1150 m), 1187 secciones, 99 483 triángulos, carga de 0,36 s (`StageTrack::build` 0,16 s) y 2,1 MB; muro más empinado, 77,9°. 30 km sintéticos, 7,3 s y 41 MB. Paso: ×0,89 la carretera de la 2.6b en la media y ×0,98 en el p99 en `lazadas`; ×1,23 y ×1,61 en el de 30 km. Cuantización de la malla con un solo origen: 0,35 mm en `lazadas` y 10,9 mm en 30 km (los trozos con origen propio son de la fase 4).

Y en «**Dibujo del tramo (lote 5, `platform/stage_draw`)**», añadir al final: «Desde el formato 2, el trozo de cada triángulo sale de su `section` (`tramo-dibujo-2`)».

## Edición propuesta de `docs/08`

En problemas encontrados, una entrada: «Formato 2, lote 6. La banda de entorno cosida al núcleo quitó el escalón arcén–terreno (vértices compartidos), pero la cuneta de 25 cm hace botar: el criterio 1b, que comparaba 4–15 m con el formato 1, no se cumple por el bote de la cuneta, no por la costura. La rejilla junto a un final libre necesitó e(s) en el final (5 m más allá del arcén) y sin el lado interior de las curvas cerradas, donde la hessiana crece sin límite».

## Lo que pido de lo compartido

Nada. `tramo-dibujo-2` usa `section` y `look` como dice el contrato.
