Fragmento de `panel` (lote 7, carril plataforma). Rama `lote-7/panel`.

## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-15 | 3 · lote 7 `panel` | | Panel del salpicadero curva · distancia · curva como geometría del interior a la izquierda del volante (`note_panel` lógica sin raylib, `panel_geometry` triángulos: dígitos de 7 segmentos, flecha más curvada cuanto más cerrada, iconos de modificadores y peligros, tachadura 0,5 s); pitidos por lado (`note_beeps`, estéreo, contadores en `AudioSnapshot`; también a ciegas); vibración ×0,6 (`kRumbleGain`); tecla U (en el tramo) que juega el mismo tramo sin su irregularidad y con ella (`StageSelection::flat`, `toggle_irregularity`), con el indicador del tramo y el texto usado en la grabación; tramo por defecto `lazadas_irregular.stage` si existe; 5b líneas blancas sobre la calzada real; 5c `--bench-stage --tramo`. Huella, reservas, telemetría y `kSimVersion` sin cambios. Sin validar a ojo | Prueba de Lucas con el copiloto; `tecla-ab` no hace falta |

## Hallazgos

| Fecha | Dónde | Hallazgo | Qué hacer |
| --- | --- | --- | --- |
| 2026-11-15 | Premisa 1 del prompt | Al ras de la pantalla de la marcha el panel no cabe con el asiento en todos los extremos (el volante lo tapa con el asiento abajo, atrás o a la derecha, y con el asiento arriba sale por −27,5°). Subido 12 cm sobre la pantalla (con un pie al salpicadero) cae entero y sin volante en los siete casos | Lucas decide el sitio en la prueba (`kPanelRaise`) |
| 2026-11-15 | Audio | El stream pasa a 2 canales; el motor mono va a los dos | Probar con auriculares |

## Ediciones propuestas

**`docs/02-arquitectura.md`, tras el párrafo de los diales:**

> **Panel de notas del salpicadero (lote 7).** `platform/note_panel` (sin raylib) deduce de `NoteQueue::view(s)` qué va en cada hueco (curva · distancia · curva), tacha 0,5 s la nota borrada y cuenta los sucesos de sonido: una nota viva que entra en el hueco izquierdo da un pitido por su lado, un peligro sin curva uno por los dos con otro tono, un borrado un tono por fotograma. `platform/panel_geometry` lo dibuja como triángulos del interior (plano de la pantalla de la marcha, 12 cm más arriba y con pie), derivado de `[cockpit]`; sin interior no hay panel. `platform/note_beeps` suena en el hilo de audio con contadores de `AudioSnapshot`, en un stream estéreo (motor mono a los dos canales). La vibración lleva la ganancia `kRumbleGain` (0,6) en la salida de `RumbleMixer`. La tecla U (modo tramo) reconstruye el mundo con `sim::stage_text_without_irregularity`; la selección guarda el texto del archivo y el que se juega.

**`docs/07`:** nada.

**`docs/08`, entrada nueva:** «Lote 7, `panel`. Al ras de la pantalla de la marcha el aro del volante tapa el panel con el asiento en varios extremos del ajuste y, arriba, el salpicadero sale de la imagen; se subió 12 cm con un pie. Los pitidos suenan al mostrarse una nota viva en el hueco izquierdo (no al llegar a la cola), y una nota tachada retrasa la entrada de la siguiente hasta que acaba la tachadura. El A/B de la irregularidad reconstruye mundo y mallas (segundos): no es instantáneo».

**`docs/teclas.md`:** regenerada (U en el tramo; texto de K).

## Pide de lo compartido

Nada. `docs/01` y `05` sin cambios.
