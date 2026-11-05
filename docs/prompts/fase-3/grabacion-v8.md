# Tarea `grabacion-v8` (lote 5, carril juego): formato 8 con el tramo, protocolo 2 y el tramo en el CSV

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-5.md` §2.1, §2.3, §2.4 y §2.5, que mandan sobre este prompt. Sale de `integracion` con `tramo-ajustes` ya fusionada (frenada de 250 m).

## Por qué

El juego graba cada mundo que construye, y el copiloto construye el suyo con lo que manda el host. Hoy `io::serialize_world` no conoce `stage_text` (`sim_world.h`), así que ni se puede grabar ni mandar un mundo con tramo. Esta tarea lo hace posible; `tramo-modo` lo usa en el juego.

## Qué hacer

1. **Formato 8** (`src/io/recording.*`), exactamente como §2.3: el texto del tramo en el mundo, `stage_ground_hash` en la cabecera, `StageResult` en el pie (`kFooterBytes` 72), `Recorder::finish` con su `StageResult`, la lectura de los formatos 1–7 sin tramo y el comentario del formato al día (versión 8 en la lista).
2. **`io::replay`:** construye el mundo con tramo, compara `stage_ground_hash` como `zone_hash` («el tramo ha cambiado»; `stage_mismatch`) y deja en el resultado el `StageResult` de la pasada repetida, para que un test lo compare con el del pie.
3. **Protocolo 2** (`src/net/`), como §2.4: `kProtocolVersion` 2, `WorldInfo::stage_ground_hash`, codificación en ese orden, rechazo del 1 con su texto. `Host::set_world` recibe también `stage_ground_hash` (lo llama `DemoWorld::publish_world`; el cambio de esa llamada lo hace `tramo-modo`: aquí, el parámetro con valor por defecto 0 para no tocar `src/platform`).
4. **CSV** (`src/io/telemetry_csv.*`), como §2.5: las seis columnas al final, de `SimWorld::stage_run()`. `tools/telemetry/columns.md` regenerado y `kExpectedCsvHash` nuevo (las 64 filas sintéticas con las columnas del tramo a 0, salvo que el test del hash ya construya un mundo; si es así, dilo).
5. **Grabación de test** `tests/data/recordings/stage_lazadas.rrec`: el conductor de test (`tests/sim/common/stage_driver.h`, ya por `rmath`) por `lazadas.stage` desde `stage_spawn(track, -5.0)` hasta pasada la meta, con el 205 de `tests/data/cars/reference.car`, ayudas por defecto. Va con su test en ctest (`replay.stage_v8`: `rally_replay run` da «hash OK» y el tiempo de la pasada). Escribe en el informe cómo se generó, para que el integrador pueda regenerarla.

Reutiliza la escritura campo a campo de `recording.cpp`, `validate_events` y el patrón de `zone_mismatch`. Si escribes una utilidad nueva, el plan dice qué buscaste y por qué no sirve lo que hay.

## Criterios (tests en `tests/io/` y `tests/net/`, Release y Debug)

1. **Ida y vuelta:** un mundo con tramo, `serialize_world` y `parse_world`, da el mismo `stage_text` byte a byte; con una grabación entera (cabecera, eventos y pie con `StageResult`), `serialize` y `parse` dan lo mismo.
2. **Lectura robusta:** truncar en cada byte de la grabación nueva da error y nunca lee fuera; una longitud del texto del tramo mayor que lo que queda da error con el byte.
3. **Repetición:** `stage_lazadas.rrec` da «hash OK» y el `StageResult` del pie; cambiando un byte del texto del tramo en la cabecera de una copia en memoria, la repetición avisa «el tramo ha cambiado» y no compara.
4. **Formatos anteriores:** las grabaciones de `tests/data/recordings/` y de `docs/manejo/sesiones/2026-10-14/` siguen dando lo de hoy (el test de los formatos 1–6 y `actions_v7`, sin cambios).
5. **Red:** por 127.0.0.1, un mundo con tramo llega al copiloto con su texto y `stage_ground_hash` exactos; el copiloto del protocolo 1 se rechaza con su texto; el tamaño del mensaje del mundo con `lazadas`, en el informe.
6. **CSV:** 139 columnas con los nombres de §2.5; en `stage_lazadas.rrec`, `stage_s` crece sin saltos fuera de las recolocaciones y `stage` = 1 en todas las filas; en una grabación de la zona, las seis a 0. `columns.md` coincide con el generado (el test de hoy).
7. **Sin reservas** en `append`, `append_event` ni en el paso de la repetición (los tests de hoy, también con tramo).

**Sin cambios:** `kSimVersion`, la huella, las reservas de Jolt, el hash de la telemetría (`CarTelemetry` no se toca), `kExpectedStageHash` y el estado del coche de la red (50 B).

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| Construir el mismo mundo con tramo dos veces da el mismo `state_hash` tras los mismos pasos | El test de `tramo-fisica` («the same inputs on the stage give the same state hash twice») | Parada: no se puede grabar |
| `parse_world` del formato 8 devuelve una descripción que construye el mismo suelo | `stage_ground_hash` del mundo repetido = el de la cabecera | Parada |

## Paradas

Solo: una premisa falsa, o una grabación de hoy que deja de dar lo mismo. Lo demás son avisos.

## Para el informe

- Tamaño de `stage_lazadas.rrec`, su tiempo de pasada y cómo regenerarla.
- Tamaño del mensaje del mundo con `lazadas`.
- Lo que `tramo-modo` tiene que cambiar en `src/platform` (las llamadas a `set_world`, `finish` y la cabecera): dilo, no lo hagas.
- Nada que probar jugando.
