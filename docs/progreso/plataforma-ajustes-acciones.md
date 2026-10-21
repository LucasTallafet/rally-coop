# Fragmento de la tarea plataforma-ajustes-acciones (lote 1, oleada 3: 2.8 parte P2 y 2.9 parte P)

**Estado: lista (ver el informe de `mensajes/plataforma-ajustes-acciones-informe.md` para lo que Lucas prueba).**

## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-10-21 | 2 | | Tareas 2.8 (parte P2) y 2.9 (parte P) del lote 1: los ajustes y la máquina de acciones en el juego (solo `src/platform`, `tests/platform` y `docs/teclas.md`; ni una línea de `src/sim`, `src/io` ni `src/game`). **Ajustes:** `PlayerSettings` con `abs` (activado por defecto), `traction` (no / bajo / alto) y `suspension` (blanda / media / dura); claves `abs`, `control_traccion` y `suspension` en `ajustes.txt` (palabra o valor malo: aviso con la línea y el valor por defecto; `Key::words` pasa a 3 palabras, el aviso de 2 queda igual); `driver_aids(settings)` construye todo el `DriverAids` de `describe()`. **Teclas 1 / 2 / 3** (Conducir): ABS sí/no, control no > bajo > alto > no y suspensión blanda > media > dura > blanda; reconstruyen el mundo y empiezan otra grabación como F6 y F2, con mensaje; F3 gana la línea «ayudas: dirección sí, ABS sí, control alto, suspensión blanda» (nada de la superficie ni de la configuración). **Acciones:** `ActionDriver` (`rally_loop`, sin raylib; `rally_loop` enlaza `rally_game` y `rally_io`) liga la máquina, el `Recorder` y las teclas siguiendo `docs/progreso/grabacion-v7.md`: `MachineSetup{}` con `car_state_from(mundo)` tras `build()`, `header.actions_text`, `ensure_capacity` y `append_event` antes de `apply` con el mismo evento (en `now()` = `Recorder::steps()`), `advance` tras cada `SimWorld::step` y `finish(..., timeline_hash)`. `data/game/actions.txt` se busca desde el exe hacia arriba con `find_data_file` (de donde sale también `default_car_path`). **Teclas 7 / 8 / 9** (Depuración, hacen de copiloto): guardar la tablet, sacarla y cancelar; un rechazo da su motivo («la tablet no está guardada», «las manos están ocupadas»…) y la cancelación o el efecto sobre la tablet salen en los mensajes. **Anillo de progreso:** capa `ActionRing` de `screen_layout` (diámetro 11 % del alto, centro al 62 %, línea «acción · fase» debajo; sin solapes a 1280×720, 1360×768 y 1920×1080; exclusiva con la ayuda H). Huella `0x66c9e3a86632750e`, reservas de Jolt 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5: sin cambios. Tests: 421 → 433 en Release (más los de Debug). Sin validar a ojo (la prueba es la del lote). | Prueba del lote 1 (`prueba.md`); después, el lote 2 |

## Hallazgos

| Fecha | Prueba | Resultado | Decisión afectada |
| --- | --- | --- | --- |
| 2026-10-21 | `ApplyResult{}` | Un `ApplyResult` por defecto tiene `reject == Ok` y `ok()` verdadero: `ActionDriver::submit` sin acciones devuelve `UnknownAction` a propósito | Ninguna |
| 2026-10-21 | Grabación incompleta (guardado de emergencia) | Su pie lleva `actions_hash` 0: `io::replay` no compara el hash de una grabación incompleta | Ninguna |
| 2026-10-21 | La plataforma y la máquina | `submit` solo se llama al principio del fotograma (teclas) y lo que entra en el paso es `advance` más la copia del `Transition` a un anillo fijo (32): sin reservas, medido en un test (0 `operator new`) | Ninguna |

## Ediciones propuestas de `docs/07-entorno-y-portabilidad.md`

- §8, ajustes del jugador: «Desde la 2.8, las teclas 1, 2 y 3 cambian el ABS (sí / no), el control de tracción (no / bajo / alto) y la dureza de la suspensión (blanda / media / dura): reconstruyen el mundo y empiezan otra grabación, y se guardan en `ajustes.txt` con las claves `abs` (1 o 0; activado por defecto), `control_traccion` (`no`, `bajo` o `alto`) y `suspension` (`blanda`, `media` o `dura`).»
- §8, datos que se ajustan sin recompilar: «`data/game/actions.txt` se busca desde la carpeta del exe hacia arriba, como el coche; su texto va en la cabecera de cada grabación.»
- §8, teclas de depuración: «7, 8 y 9 hacen de copiloto (guardar la tablet, sacarla, cancelar la acción) hasta que haya tablet (fase 3); el anillo de progreso sale centrado, algo por debajo del centro.» La tabla de teclas no se edita a mano: está en `docs/teclas.md`.

## Ediciones propuestas de `docs/08-bitacora-de-desarrollo.md`

- Entrada «Ajustes y acciones en el juego (lote 1)»: el pegamento va en `ActionDriver`, sin raylib, para probarlo con la repetición real (el valor del test es la paridad entre lo que graba la plataforma y lo que repite `io::replay`: eventos en el mismo paso, tras el último paso, con el coche cancelando solo); se graban también los rechazos, porque el hash de la línea de tiempo los incluye; un `ApplyResult` por defecto es «aceptado», así que `submit` sin acciones devuelve un rechazo explícito; el ciclo de los ajustes (no > bajo > alto; blanda > media > dura); el anillo comparte sitio con la ayuda (H) y no se dibuja mientras está abierta; ABS activado por defecto en `ajustes.txt` pero desactivado en `DriverAids{}` (tests y grabaciones antiguas); el ABS por defecto no cambia la grabación de los tests.

## Pide de lo compartido

- `docs/prompts/fase-2/2.8.md` / `2.9.md`: nada.
- **Pregunta de Lucas para la prueba:** «¿Dónde quieres el anillo en pantalla hasta que exista la tablet?» (de partida, centrado y algo por debajo del centro).
