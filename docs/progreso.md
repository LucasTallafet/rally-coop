# Rally Coop — Progreso

Se actualiza al cerrar cada sesión de trabajo. Sirve para ver la línea de desarrollo real frente a la prevista.

## Por fase

| Fase | Horas previstas | Horas reales | Estado | Inicio | Fin |
| --- | --- | --- | --- | --- | --- |
| 0 · Pruebas del concepto | 0 | 0 | Omitida | | |
| 1 · Base del motor | 60 | 3 | En curso | 2026-10-01 | |
| 2 · Coche | 130–160 | 0 | Sin empezar | | |
| 3 · Corte vertical | 110–130 | 0 | Sin empezar | | |
| 4 · Generador | 120–150 | 0 | Sin empezar | | |
| 5 · Entre casas | 60–80 | 0 | Sin empezar | | |
| 6 · Calidad visual y sonora | 80–120 | 0 | Sin empezar | | |

## Registro de sesiones

Más reciente arriba.

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-10-01 | 1 | 0,5 | Tarea 3, mundo de Jolt en el bucle: `SimWorld` (src/sim) posee el PhysicsSystem, el asignador temporal y el sistema de trabajos de un hilo; `build(descripción)` crea los cuerpos en el orden de la descripción y los añade en un lote (AddBodiesPrepare/AddBodiesFinalize, con copia del array porque Jolt lo reordena), `seal()` optimiza la broad phase, `step(SimInput)` da un paso de 1/240 s. `RALLY_CHECK` (`sim/check.h`) aborta también en Release al construir tras `seal()`, dar un paso sin sellar o si `Update` devuelve error; en el juego muestra el cuadro de error. `SimInput` de 6 bytes sin relleno (ejes int16, botones en bits); el teclado se traduce en `platform/input`. Salto por flanco de pulsación; `ButtonLatch` (`rally_loop`) retiene los botones de los fotogramas sin pasos (más de 240 fps sin sincronización vertical) hasta el primero con pasos. Los botones del paso anterior son estado de la simulación fuera del SaveState de Jolt: viajarán con el estado del juego en el cambio de host. Capas de colisión compartidas en `sim/layers.h`. Demo de la caja (`platform/box_demo`, sustituye a la del cuadrado): recinto de 30 × 30 m, caja controlada y dos sueltas, cámara fija elevada, sombreado plano por cara; flechas/WASD, Espacio, R reinicia. Tests: mismos BodyID para la misma descripción, estado de sellado, empuje y salto, 60 s de entradas PCG32 idénticas bit a bit en dos pasadas, huella FNV-1a del SaveState (igual en Debug y Release), reservas por paso (0 nuestras con operator new global contado; exactamente 2 de Jolt), test de muerte por ctest (`rally_check_abort`). Regla de la huella en CLAUDE.md. 39/39 tests en Release y Debug. Validado a ojo: sin tirones a 60 y 30 fps y el salto mide lo mismo en los tres modos. Tarea cerrada | Tarea 4 de la fase 1 |
| 2026-10-01 | 1 | 1 | Tarea 2, bucle con paso fijo: `rally_loop` (sin raylib ni SDL) con `FixedStepClock` (acumulador entero en ns × 240, total exacto de floor(tiempo/paso), tope de 24 pasos que descarta los pasos sobrantes y conserva la fracción, alpha en [0, 1)) e `interpolate` (posición lineal en doble, nlerp por el arco corto). `FramePacer`: sincronización vertical a la frecuencia del monitor; modo de 30 con intervalo frecuencia/30 si es múltiplo o temporizador de alta resolución de Windows (timeBeginPeriod(1) si falta) sin espera activa; si el controlador ignora el intervalo, lo detecta y pasa al temporizador. Demo temporal del cuadrado (F, I, V, Espacio). Decisión «Fotogramas» precisada. 28/28 tests en Release y Debug. Hallazgo: la ventana va en la Intel UHD 630, no en la RX 580, y su controlador OpenGL ocupa un núcleo esperando en SwapBuffers con sincronización vertical (99 % de CPU dentro de EndDrawing; 0 % con nuestro temporizador sin sincronización); el controlador ignora el intervalo 2 y se usa el temporizador. El exe exporta AmdPowerXpressRequestHighPerformance y NvOptimusEnablement para usar la gráfica dedicada; la demo muestra GL_RENDERER. Teclas de diagnóstico P (pausa) y C (captura del búfer antes de presentar, `platform/screenshot`, se conserva tras la demo) e intervalo real de SDL en pantalla. Validado a ojo: sin tirones a 60 y 30 fps con interpolación, tirones visibles sin ella. Tarea cerrada | Tarea 3 de la fase 1 |
| 2026-10-01 | 1 | 1,5 | Tarea 1, base de compilación: CMake con presets windows-debug/release (VS 2022, `/MT`); Jolt v5.6.0 (doble precisión, determinista, SSE4.2), SDL 3.4.16 y raylib 6.0 (backend SDL), doctest v2.5.3, por FetchContent con SHA256. Targets `rally_sim`/`rally_platform`/`rally`/`rally_tests`; comprobaciones al configurar (JPH_* solo desde Jolt, sin `/fp:fast` ni `/fp:contract`, sin AVX, simulación sin raylib) y `fp_guard.h`. `rmath`, PCG32 con valores de pcg-c, caja de Jolt bit a bit en dos pasadas, test de reglas con `cmake -P`. `.clang-format`. Ventana de 1280×720 que se cierra con Esc. Errores fatales en un cuadro de mensaje (`show_fatal_error`), porque en Release no hay consola. SSE4.2 como máximo pasa a decisión cerrada. 14/14 tests en Release y Debug | Tarea 2: bucle con paso fijo de 240 Hz, acumulador, interpolación y tope de pasos |

## Hallazgos de las pruebas

Resultados que confirman o cambian decisiones provisionales. Cada hallazgo que cambie algo se refleja también en `05-decisiones.md`.

| Fecha | Prueba | Resultado | Decisión afectada |
| --- | --- | --- | --- |
| 2026-10-01 | Reservas de memoria por paso (fase 1, tarea 3) | Jolt 5.6.0 reserva 2 bloques por paso en el árbol de la broad phase cuando hay cuerpos en movimiento; aceptado (tamaño constante, sin efecto en la reproducibilidad); nuestro código, 0 reservas por paso | Ninguna |
| 2026-10-01 | Fallo visual con la demo del bucle (fase 1, tarea 2) | Tearing intermitente con sincronización vertical en la RX 580 (ventana, 60 Hz, intervalo real 1). Capturas del búfer limpias → fallo de presentación, no de dibujo. Sin causa clara; desapareció sin cambios. Si reaparece con escenas reales: esperar al compositor con DwmFlush() | Ninguna |
| 2026-10-01 | CPU del hilo principal con la demo del bucle (fase 1, tarea 2), 3 s por modo | Intel UHD 630 con sincronización vertical = 99 % de un núcleo en EndDrawing (espera activa del controlador); RX 580 = 3–5 % (3,1, 4,2 y 4,7 % en tres medidas). Modo de 30: 40 % en la Intel y 0,5–1 % en la RX 580; sin sincronización vertical, 0 % en las dos. Los dos controladores ignoran el intervalo 2 en ventana y el modo de 30 pasa al temporizador. Sin pedir la gráfica dedicada desde el exe, OpenGL elige la Intel aunque el monitor esté en la RX 580 | Ninguna; se mide siempre con la gráfica dedicada |
