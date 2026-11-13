## Fila del registro

| 2026-11-13 | 3 | | Tarea `bordes-dibujo` (lote 7). Los bordes entre superficies cerca del ojo salen de la geometría. Causa de los «polígonos vistos» de la prueba del lote 6: a menos de 30 m solo se dibujaba la rejilla cercana con un índice de color por celda de 0,25 m y altura por vértice cada 0,25 m (dientes en arcén–cuneta, escalera en la franja de arena y silueta dentada); la malla del núcleo ya tenía los bordes exactos pero el shader la recortaba. Ahora `StageDraw` dibuja la malla del núcleo y las baldosas también junto al ojo (radio de recorte `kStageNearClipRadius` = 0) y ya no usa `NearGrid` (la zona sigue con ella). Sin costura a 30 m: no hay corte. Coste: el microrrelieve h de la rueda (rms 2–8 mm) ya no se ve en el tramo (en una zona sin bordes, diferencia máxima de 1 nivel de color y 0 píxeles con más de 2). Tests (ctest en Release) 715 → 718 (`test_stage_edges.cpp`: con la rejilla de 0,25 m fallan 420 de 2000 puntos a más de 5 cm de un borde; con la malla, 0). Dibujo del tramo en `--bench-stage` (CPU, media / p99): 60 km/h 1,604 / 5,663 ms → 0,127 / 0,161; 120 km/h 3,065 / 10,053 → 0,128 / 0,174; triángulos dibujados 874 k → 760 k. Huella `0x66c9e3a86632750e`, reservas 1990, telemetría `0xfe3c612ddcaa6805`, `kSimVersion` 5 y hash del suelo `0xacdcd1b800bb9d7d`: sin cambios. Capturas golden: cambia `tramo_salida.png`; las otras cinco, iguales byte a byte. Sin validar a ojo (la prueba del lote lleva las preguntas). | Siguiente: `diales` |

## Hallazgos

| Fecha | Qué | Cómo se resolvió |
| --- | --- | --- |
| 2026-11-13 | Los dientes del arcén–cuneta, la escalera de la arena y la silueta dentada de la prueba del lote 6 salen todos de la rejilla cercana (con solo la rejilla salen idénticos; el recorrido de la silueta lo fija la altura por vértice cada 0,25 m con la mayor entre núcleo y terreno) | Cerca se dibuja la malla del núcleo y las baldosas, sin rejilla (camino (ii) del prompt) |
| 2026-11-13 | Con la rejilla, 420 de 2000 puntos a menos de 30 m del ojo y a más de 5 cm de un borde tenían un color distinto del del triángulo de arriba (la rejilla mezcla los colores de los tres vértices de su triángulo) | `test_stage_edges.cpp`: con la malla, 0 de 2000 |
| 2026-11-13 | Los escalones del aviso (a) de `formato-2` (hasta 523 mm en s 633–635, 747 y 946), que la rejilla suavizaba en 0,25 m, se ven ahora tal como son: son de `formato-2`, no de esta tarea | Capturas `s633`, `s747` y `s946` en el informe. La respuesta de Lucas a «¿quedan escalones?» se clasifica con ellas |
| 2026-11-13 | La carga de la tarea de dibujo bajó: la rejilla era la mayor parte de la CPU (1,5 de 1,6 ms a 60 km/h y 2,9 de 3,1 a 120) | Quitada del tramo |
| 2026-11-13 | Fotogramas lentos puntuales en la primera ejecución de `--bench-stage` tras compilar (p99 del fotograma de 29–46 ms), que no se repiten en la segunda (17,5 ms) | Ruido de la máquina; se repitió la medida |

## Ediciones de `docs/02`, `07` y `08` (las aplica el integrador)

**`docs/02` §6.6, párrafo «Dibujo del tramo (lote 5, `platform/stage_draw`)»**, sustituir por:

> **Dibujo del tramo (lote 5, `platform/stage_draw`; bordes en el lote 7).** `StageDraw` dibuja el tramo con la `StageGround` que calcula con la misma función que la física y comprueba su hash. El terreno (una malla por baldosa) y el núcleo (una malla por tramo de ~64 m de `s`) se dibujan a cualquier distancia, también junto al ojo: el color es el del triángulo (superficie y aspecto: muro, roca; cuneta y talud, tierra x 0,82; el resto, el de su superficie) y los bordes entre superficies están donde están en la geometría, sin costura. El núcleo gana al terreno con desplazamiento de polígonos (el terreno queda 5 mm bajo su borde). Hasta el lote 6 cerca del ojo (30 m) se dibujaba la rejilla en anillo de `ZoneDraw` con un color por celda de 0,25 m y salían dientes en los bordes; `StageDraw` ya no la usa (la zona de pruebas sí). El microrrelieve h de la rueda (rms 2–8 mm) ya no se dibuja en el tramo; las líneas de salida, parciales y meta siguen con h. Solo se dibuja la franja de ±60 m del terreno; trozos a menos de 600 m del ojo. La colisión de las baldosas es por triángulo (tres esquinas con colisión), no por celda con cuatro. El trozo de cada triángulo sale de su `section` y el aspecto (`StageTriangle::look`) decide el color (`platform/stage_look.h`).

**`docs/07` tabla de opciones de arranque (tras `--bench-stage`)**, tres filas nuevas:

| Opción de arranque | Qué hace |
| --- | --- |
| `--bench-speed <km/h>` | Velocidad del recorrido de `--bench-stage` (por defecto 60) |
| `--stage-shot <x> <y> <z> <rumbo°> <cabeceo°> <salida.png> [--stage-layers <máscara>]` | Diagnóstico del dibujo: una captura de lazadas desde un ojo fijo del mundo (rumbo 0 = norte, −z), sin simular el coche, y se cierra. Máscara de capas: 2 núcleo, 4 baldosas, 8 líneas (por defecto, todas) |

(Y en la fila de `--bench-stage`, quitar «de la salida a la meta a 60 km/h» por «a 60 km/h, o a `--bench-speed`».)

**`docs/08` (bitácora, problemas encontrados):** «Dientes de sierra en los bordes entre superficies cerca del ojo (lote 6, prueba de Lucas; lote 7, `bordes-dibujo`) | Un índice de color por celda de 0,25 m y una altura por vértice cada 0,25 m dibujan un borde curvo en dientes; no hay resolución de rejilla que lo arregle sin llegar a la del píxel | Cerca se dibuja la malla del núcleo, que ya tiene los bordes exactos; se pierde el microrrelieve h (mm). Si algún día h se quiere ver (baches de la capa de irregularidad), va como textura de relieve en el sombreado, no como rejilla de color».

## Para el consultor

1. **`docs/01` §7.3 (línea 223) deja de cumplirse en el dibujo del tramo:** «el render desplaza la carretera cerca de la cámara con la misma función que usa la rueda». Cotas del microrrelieve que ya no se ve (peor caso, 6 ondas alineadas con la luz): altura ≤ 3,46·rms (grava 2,1 cm, tierra 1,7, arena 2,8, asfalto 0,7); cambio de sombreado ≤ 2,1 niveles en grava, 1,5 en tierra, 3,4 en arena y 0,35 en asfalto, típico 0,4–0,7. Medido en capturas a 5 m antes y después, en una zona sin bordes: diferencia máxima 1 nivel y 0 píxeles con más de 2. Opciones: aceptar el límite y editar `01` §7.3, o recuperar h solo en el sombreado con un shader que lea h de una textura en anillo (mantiene el coste de la rejilla, ~1,4 ms de CPU de media).
2. **Tests de `test_stage_draw.cpp` que miden la rejilla del tramo, que ya no se dibuja** (el criterio 5 de la tarea prohíbe tocarlos y `criterios-revision` §3.3 no deja tests que han dejado de medir): `the near grid height is the ground of Jolt plus the relief of the wheel`, `the ring gives the same drawn height as the sampler...`, `the step between the near grid and the far mesh...`, la parte del anillo de `choosing chunks and moving the ring do not allocate`, `the relief with the nearest s against the relief of the wheel`, `the near grid color index is the look of the top triangle` y `rock texels in the near ring...`. Siguen midiendo `StageNearSampler` y `NearRing` (que existen y los usan estos tests), pero nada del juego los usa ya. Decidir si se quitan o se mantienen como medida de la rejilla.

## Utilidades nuevas (criterio 2.6 de `comun.md`: qué se buscó y por qué no sirve lo que hay)

- `--stage-shot` y `--stage-layers`: se buscó una forma de captura con ojo libre. `--golden` necesita una grabación y un paso (el ojo es el del coche) y `--bench-stage` mueve la cámara por el eje; ninguna admite un ojo fijo arbitrario ni quitar capas para atribuir un defecto.
- `--bench-speed`: `--bench-stage` tenía la velocidad fija (`kStageBenchSpeed`); el criterio 4 pide 60 y 120 km/h.
- `tests/platform/test_stage_edges.cpp`: archivo nuevo porque `test_stage_draw.cpp` no se toca (criterio 5) y es de bordes. Su fixture (`StageGround` de `lazadas`) cuesta unos segundos; el de `test_stage_draw.cpp` es privado a su archivo.
- `kStageNearClipRadius` en `stage_look.h` (sin raylib, para que los tests la lean).

Pide de lo compartido: nada. No se tocan `src/sim`, `data/` ni `docs/teclas.md`.
