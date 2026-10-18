# Fragmento de la tarea grabacion-v7 (lote 1, oleada 2: 2.9 parte G y 2.8 parte G)

**Estado: lista, con una petición al integrador (el recuento de columnas de un test de `tests/sim`, abajo).**

## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-10-16 | 2 | | Tareas 2.8 y 2.9, parte G (lote 1): formato 7 del `.rrec`, un solo salto para las dos. **Cabecera:** texto de `data/game/actions.txt` tras el hash de la zona (vacío = sin acciones). **Mundo:** u8 ABS, u8 control de tracción y u8 suspensión tras la calzada (1–6: `false`, `Off`, `Medium`). **Cuerpo:** tras los tramos, u32 eventos y por evento 11 B (u64 paso, u8 jugador, u8 tipo, u8 acción). **Pie:** u64 `timeline_hash` antes de `REND` (`kFooterBytes` 48 → 56). `parse` valida jugador, tipo, pasos no decrecientes, ninguno posterior al último y texto no vacío si hay eventos, y comprueba que los eventos caben antes de reservar. `io::replay` construye `ActionMachine(tabla de la cabecera, MachineSetup{} con setup.car = car_state_from del mundo recién construido)`, aplica los eventos de cada paso antes de `SimWorld::step` y llama a `advance` después; «hash OK» pide también el hash de acciones (sin acciones no se compara). `Recorder::append_event` (paso == `steps()`, sin reservas) y `ensure_capacity` reserva eventos; `finish` gana `actions_hash` con valor por defecto. `rally_io` enlaza `rally_game`. CSV: `abs_torque_cut_{fl,fr,rl,rr}` y `traction_torque_cut` (133 columnas), `columns.md` regenerado; `kExpectedCsvHash` = `0xdc7d3a061d9f3d6b` (FNV-1a de cabecera y 64 filas sintéticas fijas, igual en Release y Debug). Grabación nueva `tests/data/recordings/actions_v7.rrec` (3600 pasos, ABS, control alto, blanda, 10 peticiones y 2 cancelaciones, 3 rechazos, 1 cancelación por el coche; `rally_replay`: hash OK). Las 5 grabaciones de formato 6 y las 6 de `docs/manejo/sesiones/2026-10-14/` siguen dando «hash OK» (la de otra zona avisa, como antes). Tests de `rally_tests_io`: 11 → 29 casos (más `replay.actions_v7` en ctest). Huella `0x66c9e3a86632750e`, reservas de Jolt 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5: sin cambios (ni una línea de `src/sim`). Sin validar a ojo (la prueba es la del lote) | `plataforma-ajustes-acciones` (oleada 3) |

## Hallazgos

| Fecha | Prueba | Resultado | Decisión afectada |
| --- | --- | --- | --- |
| 2026-10-16 | Recuento de columnas en `tests/sim/CMakeLists.txt` | `replay.telemetry_first` y `_second` exigen `128 columnas` y las columnas nuevas de la 2.8 lo dejan en 133. Con ellos caen sus dependientes (`replay.telemetry_twice_identical`, `telemetry_py.recording`, `.plot`, `.compare`). Con `128` → `133` pasan los 57 tests de `replay` y `telemetry` | Ninguna; la línea es del carril sim |
| 2026-10-16 | `kTelemetryFormatVersion` | Se queda en 2: `tests/sim/test_telemetry.cpp` y `tools/telemetry/test_telemetry.py` comprueban `# format=2`. Las columnas se identifican por nombre y las nuevas van en medio del bloque de cada rueda y tras `lcp_failures` | Ninguna |
| 2026-10-16 | Máquina de la repetición | `MachineSetup{}` deja `car.stopped = true` aunque el coche salga lanzado: una acción con `car = stopped` pedida en el paso 0 se aceptaría en la repetición y se rechazaría en vivo. La repetición pone `setup.car = car_state_from(mundo recién construido)`; test con salida a 20 m/s | Contrato del lote §3.4 (MachineSetup): la plataforma debe construirla igual |
| 2026-10-16 | Reconstruir el mundo con las teclas | Una grabación nueva y una máquina nueva (aviso de 2.9-maquina). Las grabaciones que sacará el juego antes de la oleada 3 salen en formato 7 sin texto de acciones, sin eventos y con pie 0: son válidas | — |

## Ediciones propuestas de `docs/02-arquitectura.md`

- En la sección del formato `.rrec` (grabaciones), sustituir la descripción por la del formato 7: «Formato 7 (lote 1, tareas 2.8 y 2.9): cabecera con el texto de `data/game/actions.txt` tras el hash de la zona (u32 longitud + bytes; vacío = sin acciones); descripción del mundo con u8 ABS, u8 control de tracción y u8 suspensión tras la calzada (1–6: sin ABS, sin control, suspensión media); cuerpo con los tramos y, detrás, u32 número de eventos de acción y por evento u64 paso, u8 jugador, u8 tipo (0 petición, 1 cancelación) y u8 acción (índice en el texto de la cabecera); pie con u64 hash de la línea de tiempo (`ActionMachine::timeline_hash`) antes de `REND` (56 bytes). Se leen los formatos 1–6. La repetición construye la máquina con la tabla de la cabecera y `MachineSetup{}` con el coche tal como sale, aplica los eventos de cada paso antes de simularlo y avanza la máquina después; «hash OK» pide que coincidan el hash del estado y el de las acciones (sin acciones, solo el del estado).»
- En el módulo `src/io`: «`rally_io` enlaza `rally_game` (la repetición de las acciones).»

## Ediciones propuestas de `docs/07-entorno-y-portabilidad.md`

- En `rally_replay` / formatos: «`rally_replay run` repite también las acciones del formato 7 y compara el hash de su línea de tiempo; el CSV de telemetría lleva 133 columnas (con `abs_torque_cut_<rueda>` y `traction_torque_cut`).» Para generar la grabación de formato 7 de los tests: `RALLY_WRITE_ACTIONS_REC=<ruta> rally_tests_io -tc="actions_recording: a session*"`.

## Ediciones propuestas de `docs/08-bitacora-de-desarrollo.md`

- Entrada «Formato 7 de las grabaciones»: un solo salto de formato para la 2.8 y la 2.9; el estado de la máquina no se guarda (la repetición lo reconstruye con los eventos y el hash de la línea de tiempo detecta cualquier diferencia); el paso de un evento es el que se va a simular, así que `Recorder::append_event` exige `step == steps()` y un desfase de la plataforma salta en el acto; la repetición arranca la máquina con el estado del coche tal como sale del `build()` porque el formato no guarda `MachineSetup`; las columnas nuevas del CSV se añaden sin subir `kTelemetryFormatVersion`.

## Pide de lo compartido (al integrador y a la oleada 3)

- **Integrador (una línea):** `tests/sim/CMakeLists.txt`, en `replay.telemetry_first` y `replay.telemetry_second`: `128 columnas` → `133 columnas`. Sin ella fallan 6 tests de ctest (verificado: con el cambio pasan).
- **`tools/replay/main.cpp` (carril herramientas):** `rally_replay info` no muestra aún el texto de acciones, el número de eventos, el hash de acciones del pie ni el ABS, el control y la suspensión del mundo (los campos ya están en `Recording` y en `WorldDesc::aids`). `run` ya imprime «hash OK» con acciones.
- **Plataforma (oleada 3):** construir la máquina con `MachineSetup{}` y `setup.car = car_state_from(world)` justo tras `build()`; poner `header.actions_text` con el texto de `data/game/actions.txt`; por fotograma, `ensure_capacity(pasos)` antes de `append_event` y de los pasos; cada petición o cancelación, `append_event({recorder.steps(), ...})` y `machine.apply` con el mismo evento; `advance(car_state_from(world))` tras cada `SimWorld::step`; al terminar, `finish(hash, tiempo, carretera, machine.timeline_hash())` (con texto de acciones el hash nunca vale 0). Sin texto de acciones, pie a 0.
