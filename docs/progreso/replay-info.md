| 2026-10-22 | | | Tarea `replay-info` (lote 2, carril herramientas): `rally_replay info` enseña ahora las ayudas de la descripción del mundo con cualquier formato (ayuda de dirección, caja, ABS, control de tracción y suspensión; las que el formato no guardaba salen con su valor por defecto y la nota «formato anterior a la N: no existía») y, con formato 7, los nombres de las acciones (`action =` del texto de la cabecera), «eventos: N» con los primeros 10 (paso, jugador, tipo, acción con su nombre) y el hash de la línea de tiempo del pie. Solo lee con `io::parse` y `game::parse_actions`; nada nuevo en `src/io`, ni una línea de `src/sim`, `src/game` ni `src/io`. Tests nuevos en `tests/tools` (2): `replay.info_actions_v7` (regex: ayudas sí / automática / sí / alto / blanda, las 3 acciones, `eventos: 12` con «y 2 más», hash `0xf3fdf0407b175af8`) y `replay.info_zone_lap_no_actions` (formato 6: ayudas por defecto y ni «acciones», «eventos» ni «línea de tiempo»). Huella 0x66c9e3a86632750e, reservas 1990, telemetría 0xfe3c612ddcaa6805, `kSimVersion` 5: sin cambios. Sin validar a ojo (herramienta de consola). | |

Utilidades nuevas: `print_actions` (`parse_actions` y `Recording::events` dan los datos; no había función que los imprimiera). Los nombres de control de tracción y suspensión se reutilizan de `rally_loop` (`platform/player_settings.h`), que no lleva raylib ni SDL; solo el de la caja es texto local (no existe función).

Hallazgos: ninguno.

Ediciones propuestas (integrador):

- `docs/07-entorno-y-portabilidad.md` §8, la línea de `info`. Sustituir
  `- `info <archivo>`: dice, entre otras cosas, la superficie real de la pista ciega.`
  por
  `- `info <archivo>`: cabecera y pie; dice, entre otras cosas, la superficie real de la pista ciega y las ayudas de la descripción del mundo (dirección, caja, ABS, control de tracción y suspensión). Con formato 7, además, los nombres de las acciones, el número de eventos con los 10 primeros (paso, jugador, tipo, acción) y el hash de la línea de tiempo.`

Pide de lo compartido: nada.
