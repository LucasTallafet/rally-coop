## Fila del registro

| 2026-10-24 | 3 | | Arreglo `copiloto-asiento` (lote 3, carril plataforma): la ventana del copiloto arranca en la cámara del asiento derecho (Tab: asiento, persecución, toda la zona o fija). Ojo = el del conductor del `.car` (sin asiento del jugador ni cabeza) con la x cambiada de signo: **+0,37 m** a la derecha del eje en los cuatro `.car`. Pose interpolada del `StateBuffer`; 55° vertical y planos sin cambios. Interior de `cockpit_draw`: el volante gira con la dirección recibida (`SampledState::steer` × 30° × 13 = 390° a fondo) y la pantalla marca marcha (acotada a R..5), rpm y presión recibidas (turbo solo si el coche lo tiene). **Opción C de Lucas:** al frente no entran ni el volante ni la pantalla (quedan a 47,7°–63,8° a la izquierda, con ±42,8° de campo); la tecla **B** (nueva, solo en la ventana del copiloto) mantenida gira la cabeza 25° a la izquierda en 0,18 s (curva suave) y los deja en imagen; al soltar vuelve. Ayuda del copiloto de 7 a 8 teclas. Función `next_camera_view` compartida con la cámara del conductor. Sin vibración, sonido ni ayudas. Tests 511 → 518 (7 nuevos en `test_copilot_seat.cpp`; uno modificado, la ayuda de 8 teclas). Huella `0x66c9e3a86632750e`, reservas Jolt 1990, telemetría `0xfe3c612ddcaa6805`, `kSimVersion` 5: sin cambios. Sin validar a ojo. |

## Hallazgos

| Fecha | Hallazgo |
| --- | --- |
| 2026-10-24 | Con el ojo del copiloto mirando al frente, el interior (relativo al ojo del conductor) queda 0,74 m a su izquierda: pantalla a 47,7°–53,8° y aro del volante a 51,5°–63,8° frente a ±42,8° de campo. Por eso la opción C (B mantenida, 25°). Con 25° las esquinas de la pantalla y el centro del volante quedan dentro de la imagen en los cuatro `.car` (test). |
| 2026-10-24 | Todas las letras y los dígitos 1–9 de la tabla de teclas están ocupados en el contexto de la zona. B solo vale en el recinto (bosque de medida) y el copiloto lee las teclas con zona + copiloto, así que B está libre allí (contextos disjuntos); la fila de B del copiloto lleva `kContextCopilot` solo, como Z. |
| 2026-10-24 | `append_display` indexa su tabla de dígitos con la marcha sin comprobar y `decode` acepta cualquier marcha de 8 bits: `copilot_cockpit` la acota a [−1, 5] (test con 7 y −3). Si algún día otro dibujo usa la marcha de red, tiene que acotarla igual. |
| 2026-10-24 | Rendimiento (F1) del copiloto en el asiento: no medido con la ventana (no se envían teclas ni se abren ventanas siempre encima en el escritorio). Por construcción, el interior añade 1 draw call (un lote de rlgl, de 16 a 17 según `docs/progreso.md`); pasa a la prueba de Lucas. |
| 2026-10-24 | No se hizo `golden.ps1 -Regenerar`: las capturas de referencia son de la vista del conductor, que no cambia (`DemoCamera` solo cambia con `copilot_seat`, falso en el juego). |
| 2026-10-24 | Sobre el plan: el revisor de diseño devolvió «Necesita consultor» por la pregunta de dónde mira el copiloto; Lucas eligió C. No se pidió segunda revisión porque la opción C estaba ya descrita en la primera (coste y tecla); los 10 cambios se aplican. |

## Ediciones propuestas

- `docs/02` (plataforma, ventana del copiloto): «La ventana del copiloto arranca en la cámara del asiento derecho: el ojo del conductor del `.car` reflejado en el eje lateral del coche, sin asiento del jugador ni movimiento de cabeza, con el interior del conductor (volante con la dirección recibida, pantalla con marcha, rpm y presión recibidas). B mantenida gira la cabeza 25° a la izquierda para ver la pantalla y el volante (`platform/copilot_view`). Tab: asiento, persecución, toda la zona.»
- `docs/07` §8: la tecla B del copiloto y el texto nuevo de Tab en `--copiloto` (la tabla de `docs/teclas.md` ya está al día).
- `docs/08` (bitácora): por qué la mirada al salpicadero es una tecla y no un ángulo fijo: con un ángulo fijo se pierde campo de carretera a la derecha (solo ~22° con 21°) y con el ojo al frente el volante y la pantalla caen fuera; el campo de visión no se cambia (decisión «Render»). Y por qué B: no queda ninguna libre en la zona.

## Pide de lo compartido

Nada.
