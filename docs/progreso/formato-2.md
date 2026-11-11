# Fragmento de `formato-2` (lote 6, carril sim)

Estado: **parada en la parte B, tercera vez**: con la rampa por longitud (propuesta (A)), fallan el invariante 7 con el barrido completo y 7b en `lazadas` (13 rampas de muro, hasta 523 mm) y `hairpin_flat` (1 lado enfrentado al extremo del tramo, 68 mm). Informe: `mensajes/formato-2-informe.md` de la copia de trabajo.

## Fila del registro (provisional)

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-10 | 3 | | Tarea `formato-2` (lote 6), **parte B, parada por los criterios 1a y 1b**. Hecho: el formato 2 es el suelo del juego, con un solo generador. La malla lleva el núcleo y la banda de entorno: cuneta en coseno de 1,5 × 0,25 m, talud o muro de cabeza a cabeza hasta el eje medio con la otra parte del tramo, explanada de 5 m, rampa y abanico en los arcos cerrados. La rejilla sigue a la banda: bajo ella, siempre por debajo; junto a un final libre, la regla del borde de hoy. `StageTrack::build` valida y guarda la banda («s = …: no cabe el talud entre pasadas»; mínimo de 3,1 m entre núcleos), y `StageGround::build` la reutiliza. `section` y `look` entran en el hash. P1 77,92° en `lazadas`; P2 y P3 sin caras malas en los seis tramos de test; paso 0,046 / 0,120 ms (×0,89 / ×0,98 frente a la 2.6b); carga de `lazadas` 0,36 s y 2,1 MB; 30 km, 7,3 s y 41 MB. Fallan 1a (0–4 m, ×1,101 de la densa con límite ×1,10; p99 de 4–15 m, ×1,21) y 1b (4–15 m por fuera, hasta 7,3 % frente a 4,4 %, por el bote de la cuneta y no por la costura, que da 8,6 mm). `kExpectedStageGroundHash` `0xdd438cd676e8c121` → `0x56180e49eb1ff6a8`; `kExpectedStageStateHash` `0x06270f27dd850330` → `0x3d08dcb86b41ad6f`; `stage_lazadas.rrec` regenerada (1:28,94, 1 recolocación, 0 atajos). Huella `0x66c9e3a86632750e`, reservas 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5 sin cambios. Tests: Release 661/666 (los 2 de la parada y 3 de otros carriles que `integracion` ya arregla). Sin validar a ojo. | Respuesta del consultor a la parada de 1a y 1b |
| 2026-11-11 | 3 | | `formato-2`, parte B, **segunda parada** (7b). PA se cumple: 40 tramos de cuneta (las caras vecinas bajo 5°, aristas inactivas en Jolt); 1a pasa frente a `BandDense` (×1,022/×0,967 y ×1,041/×1,015); `lazadas` 203 851 triángulos, 3,8 MB, 0,44 s; 30 km 70,7 MB, 8,5 s. Regla de la rejilla: e(s) por lado y solo la curvatura que deja la cuerda por encima. `integracion` fusionada; Release 708/709 y Debug 685/686 (solo 7b). `kExpectedStageGroundHash` → `0xfce2b56c48cd9d1c`. | Respuesta del consultor a la propuesta (A) |
| 2026-11-11 | 3 | | `formato-2`, parte B, **tercera parada**. La rampa va por la longitud del final en planta (L ≥ √(24·Δh) con el término de u, la mayor 19,6 m en `lazadas`), lleva el final enfrentado del borde de su tramo y se recalcula tras cada pasada del refinado; el mismo pie en los dos lados enfrentados; alcance del eje medio sin cambios (15 m). PC1 (≤ 0,17 m⁻¹), P1 77,92°, P2, P3 y 1a (×1,035/×0,973 y ×1,031/×1,007) se cumplen. Escalones de más de 40 mm en `lazadas`: de 91 a 13. Lo que queda son rampas enfrentadas con muro alto, cuyos finales se separan a alturas distintas en un hueco de menos de dos celdas, más un lado enfrentado al extremo del tramo en `hairpin_flat`. | Respuesta del consultor a la segunda vuelta de (A) (informe §0.3 y §6) |

## Hallazgos

| Fecha | Hallazgo | Cifra | Qué se hace |
| --- | --- | --- | --- |
| 2026-11-10 | El muro entre los brazos de `lazadas` cabe en 80° por poco | 77,92° en s 657,75 frente a 830,88 | Premisa P1 cumplida; 2,1° de margen |
| 2026-11-10 | La cuneta de 25 cm hace botar: la rueda pierde el contacto en ella y en la caída, a 2,5–5,5 m del borde | 1,5–2 m del borde: 95–100 % a 60 y 90 km/h; tabla del lote 5, 0–4 m: 27 % frente a 14 % con el formato 1 | Lo valora Lucas jugando (pregunta 2 de la prueba del lote) |
| 2026-11-10 | La costura banda–rejilla es pequeña junto a la explanada y grande en la rampa del exterior de la horquilla 1 | 8,6 mm por fuera de la R 80; 295 mm en s 747,6 | Aviso (criterio 10) |
| 2026-11-10 | El terreno natural del formato 1 sigue haciendo paredes entre pasadas lejanas | 3,68 (74,8°) a 30–60 m del borde, en s 463 | Aviso aceptado (respuesta a la parte A, §2.1) |
| 2026-11-11 | Con 18 tramos, las aristas de la cuneta son activas en Jolt y la rueda toma su normal | Caras vecinas hasta 10,4°; 9481 aristas convexas de más de 5°; con 40, 70 (en el abanico de las R 6, ≤ 5,97°); exceso de pérdidas de 0–4 m, de 161 a 32 pasos-rueda | 40 tramos de cuneta (opción (i)) |
| 2026-11-11 | Una celda de 0,75 m no sigue el final de una rampa que cambia de altura en menos de una celda | 253 mm en lazadas s 747,6; 1,14 m en 30 km; 5,2 mm de rejilla sobre la malla en s 15 497 del 30 km | Parada: propuesta de rampa por longitud en planta (L ≥ √(24·Δh)) |
| 2026-11-11 | La explanada a 9 m del eje amplifica los quiebros del peralte | J ≈ 0,45 en espirales de 2 m; escalones de hasta 160 mm | Parada: propuesta de suavizar el peralte de la explanada |
| 2026-11-11 | Para la fase 4 (validador del generador): la explanada a 9 m del eje deja un escalón de más de 40 mm si el peralte cambia de ritmo de golpe | Δ(peralte') ≤ 13,2/t_F %/m en cada junta (≈ 1,5 %/m con t_F = 9 m); las espirales de 2,14 m del 30 km dan 2,5 %/m y 161 mm | Regla del validador (respuesta del consultor, §1.2) |
| 2026-11-11 | Para la fase 4: el coste de los 40 tramos de cuneta en un tramo largo | 30 km: 70,7 MB y 8,5 s de carga | Aviso aceptado |
| 2026-11-11 | Dos rampas enfrentadas que se retiran a alturas distintas dejan un hueco que la rejilla de 0,75 m no cubre | Hasta 523 mm de escalón y 7 mm de rejilla sobre la malla en `lazadas` | Parada: segunda vuelta de (A) |
| 2026-11-10 | La carga con la banda | `lazadas` 0,36 s (formato 1: 0,13 s); 30 km 7,3 s (2,6 s) | Aviso aceptado (§2.5) |

## Edición propuesta de `docs/02` §6.6

Sustituir los párrafos «**Física del tramo (lote 4, `sim/stage_ground.h`).**», «**Límites del formato 1 (avisos aceptados)**» y «**Cifras (i3-8100, Release)**» por:

> **Física del tramo (formato 2, lote 6, `sim/stage_ground.h`).** Un cuerpo estático con un compuesto {malla, compuesto de baldosas}, creado en `build()` antes de los cuerpos de la descripción. Se calcula al cargar, sin hilos, con `rmath`, y lo mismo sirve para el dibujo. Un solo generador de suelo.
> - **Malla:** por sección, el núcleo (calzada y arcén en un plano con el peralte) y, a cada lado, la banda de entorno, desde el vértice del borde del arcén y con su dirección y su pendiente (de los vértices, no de los semianchos del eje: la capa de irregularidad los moverá). La banda tiene una cuneta en coseno de 1,5 m × 0,25 m (40 tramos: las caras vecinas a menos de 5°, aristas inactivas en Jolt) y una tira recta de la cabeza de la cuneta a su final F:
>   - hasta el eje medio con la otra parte del tramo más cercana, si está a ≤ 10 m del borde, de cabeza a cabeza (talud si ≤ 1:1,5, muro hasta 80° si no);
>   - una explanada en el plano del arcén hasta 5 m del borde, sin otra parte a menos de 15 m;
>   - una rampa entre las dos, por la longitud del final en planta: L con la curvatura de la altura del final ≤ 0,25 m⁻¹ (≥ √(24·Δh)), llevando el final enfrentado del borde de su tramo; dos lados enfrentados lo son en el mismo pie, y dos tramos enfrentados a menos de sus dos L se unen.
>
>   En el interior de un arco cuyo eje medio es su centro, todas sus secciones acaban en un solo vértice (abanico). K = 87 vértices por sección (`vertices_per_section()`). Cada triángulo lleva su sección y su aspecto (calzada, arcén, cuneta, talud, muro; el muro es de tierra en la física y de roca en el dibujo). Las secciones, como en el lote 4, más las que pide la cuerda del final de la banda (1 cm en planta, 2 mm en alzado; en un muro, planta × pendiente ≤ 2 mm). Las aristas interiores son inactivas (≤ 0,3° entre triángulos).
> - **Validación** (`StageTrack::build`, que guarda la banda para `StageGround::build`): «s = …: no cabe el talud entre pasadas» si la cuneta y un talud de 5 cm no caben antes del eje medio o del centro de curvatura, si el muro pasaría de 80° o si algún triángulo de la banda queda invertido o sin área. Dos núcleos necesitan ≥ 3,1 m.
> - **Rejilla:** baldosas de `HeightFieldShape` de 64 × 64 muestras cada 0,75 m que comparten el borde, solo donde pasa la franja (colisión a menos de 60,5 m del eje).
>   - Bajo la banda queda 10 cm por debajo de la menor altura de la banda en ±1,1 m, menos su variación a lo largo de s.
>   - Junto a un final libre (explanada, rampa o recortada), en ±1,06 m, a S − 5 mm − e(s), con S la última tira prolongada y e(s) la cota de interpolación en el final de ese lado (½·R²·máx(0, λ_max) de la hessiana de S y, en las juntas, solo el quiebro que sube); en las rampas, más la cota de la curvatura de su final, extendida 1,5 + 1,5 m a lo largo del final en planta; de ahí empalma en 4 m hasta el terreno natural.
>   - El terreno natural es la media de las alturas de los finales de la banda con peso (1 − paso suave)/d⁸, como en el formato 1.
>   - La rejilla nunca asoma sobre la malla (margen mínimo 3,8 mm).
> - **Avisos aceptados:**
>   - la explanada sigue el plano del arcén hasta 9 m del eje: un cambio de ritmo del peralte de más de 13,2/t_F %/m en una junta (≈ 1,5 %/m) deja un escalón de más de 40 mm (límite del validador del generador, fase 4);
>   - entre pasadas lejanas (más de 15 m), el terreno natural deja paredes en la rejilla (3,68 en `lazadas`, a 40 m del eje en s 463);
>   - la costura banda–rejilla: pendiente de la respuesta a la parada de 7b (hoy, hasta 253 mm en las rampas de lazadas);
>   - por fuera de las curvas, la rejilla junto al final mezcla finales a otra altura (≤ 1,5 mm);
>   - el muro dice «tierra» al rozarlo (vibración, sonido y telemetría).
> - **Cifras (i3-8100, Release):** `lazadas.stage` (1150 m), 1187 secciones, 99 483 triángulos, carga de 0,36 s (`StageTrack::build` 0,16 s) y 2,1 MB; muro más empinado, 77,9°. 30 km sintéticos, 7,3 s y 41 MB. Paso: ×0,89 la carretera de la 2.6b en la media y ×0,98 en el p99 en `lazadas`; ×1,23 y ×1,61 en el de 30 km. Cuantización de la malla con un solo origen: 0,35 mm en `lazadas` y 10,9 mm en 30 km (los trozos con origen propio son de la fase 4).

Y en «**Dibujo del tramo (lote 5, `platform/stage_draw`)**», añadir al final: «Desde el formato 2, el trozo de cada triángulo sale de su `section` (`tramo-dibujo-2`)».

## Edición propuesta de `docs/08`

En problemas encontrados, una entrada: «Formato 2, lote 6. La banda de entorno cosida al núcleo quitó el escalón arcén–terreno (vértices compartidos), pero la cuneta de 25 cm hace botar: el criterio 1b, que comparaba 4–15 m con el formato 1, no se cumple por el bote de la cuneta, no por la costura. La rejilla junto a un final libre necesitó e(s) en el final (5 m más allá del arcén) y sin el lado interior de las curvas cerradas, donde la hessiana crece sin límite. Con 18 tramos, las aristas de la cuneta (hasta 10,4° entre caras) eran activas en Jolt y la rueda, un cilindro, tomaba su normal: 40 tramos las dejan bajo 5°. La rejilla de 0,75 m no puede seguir un final de banda que cambia de altura en menos de una celda (rampas comprimidas por dentro de las horquillas) ni los quiebros del peralte amplificados a 9 m del eje: son geometría de la banda, no de la regla de la rejilla».

## Lo que pido de lo compartido

Nada. `tramo-dibujo-2` usa `section` y `look` como dice el contrato.
