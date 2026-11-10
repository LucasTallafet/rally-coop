# Fragmento de progreso: replay-desacople (lote 6)

## Fila del registro

| 2026-11-10 | 3 | | Tarea `replay-desacople` (lote 6). Los tests de `rally_replay` dejan de copiar las cifras de `stage_lazadas.rrec` que calcula otro carril. `replay.info_stage_v8` y `replay.run_stage_pass` comprueban la forma del hash del suelo (`0x` y 16 cifras hexadecimales, escritas una a una: la regex de CMake no admite `{n}`) y de la línea de la pasada; siguen exactos el formato (8), el nombre, 1150,0 m y los 2887 bytes (salen de `lazadas.stage`). La relación, en `tests/tools/check_pass_line.py` (solo biblioteca estándar; `--run` lanza `info` y `run`): `m:ss,cc` es pasos/240 truncado a la centésima, en aritmética entera, y la línea es igual en `info` y en `run`. Tests nuevos: `replay.pass_line_relation`, `replay.pass_line_fake_ok` (control positivo, 1:00,99 con 14 639 pasos: redondeado saldría 1:01,00) y los negativos `replay.pass_line_rejects_time`, `_steps` y `_differing`, que piden el mensaje del script y no `WILL_FAIL`. Todos dentro del `if` de Python 3.12. Sin cambios en `src/` ni en `tools/`: huella, reservas, telemetría y `kSimVersion` sin cambios | Tarea `formato-2` |

## Filas de hallazgos

| 2026-11-10 | Regex de ctest en tests de herramientas (lote 6, replay-desacople) | El motor de regex de CMake/ctest no admite `{n}`; en la salida de `rally_replay` por la consola de Windows el mensaje con tilde (`línea`) no casa con `PASS_REGULAR_EXPRESSION` y en una tubería llegan `\r\n` (con `re.MULTILINE`, `$` no casa: `text=True` en `subprocess`) | Mensajes de los scripts en ASCII cuando un test los busca; `[0-9a-f]` repetido en vez de `{16}` |

## Ediciones de `docs/02`, `07` y `08`

Ninguna.

## Pide de lo compartido

Nada. El criterio nuevo de `criterios-revision.md` («un test no copia una cifra que calcula otro carril») lo aplica el integrador.
