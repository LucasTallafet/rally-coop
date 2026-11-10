## Fila del registro

| Lote 6 · `red-tramo` | `s`, `t` y «en carretera» del host en el estado del coche (57 B, protocolo 3). `CarState` gana `s` (i32, mm), `t` (i16, cm, acotado) y un u8 de indicadores (bit 0 = en carretera, el resto a 0 y rechazado en `decode`); salen de `SimWorld::stage_run()`. `StateBuffer::sample` da `s` y `t` interpolados por paso y extrapolados con la pendiente de la pareja anterior del mismo recolocado (a 8 pasos o más), y `on_road` del estado más cercano. El atajo del tramo sube ahora el contador de recolocaciones. `replay.stage_v8` ya no copia los pasos. (cifras y tests: ver el informe) | Horas: | Validado a ojo: sin validar a ojo (lo prueba `tablet`) | Siguiente paso: `cambios-direccion` |

## Hallazgos

| Hallazgo | Dónde | Qué |
| --- | --- | --- |
| Un atajo del tramo no subía el contador de recolocaciones | `Host::after_steps` | El `StageRun` recoloca el coche en `stage_spawn(exit_s)` en el mismo paso sin que el botón lo diga, y el copiloto habría interpolado la pose (y ahora `s`) a través del salto. Arreglado en esta tarea: el host suma 1 cuando cambia `StageRunState::shortcuts` (desde la primera lectura tras enviar el mundo). |

## Ediciones propuestas

**`docs/02` §7, párrafo «Sincronía» (y «Implementación (`src/net`, lote 3)»):** sustituir «El estado del coche (50 B) no cambia.» por: «Protocolo 3 (lote 6): el estado del coche son 57 B: detrás del contador de recolocaciones lleva `s` (i32, mm, redondeado), `t` (i16, cm, redondeado y acotado a ±32 767) y un u8 de indicadores (bit 0 = en carretera; los demás, reservados a 0, y `decode` los rechaza: usarlos exige subir el protocolo). Salen de `SimWorld::stage_run()` del host, nunca de una proyección del copiloto: las notas se anclan a esta `s`. Sin tramo valen 0. Un copiloto del protocolo 2 se rechaza con «protocolo 2, este juego usa el 3». A 30 Hz son 1,7 KB/s de carga útil (~2,9 KB/s con las cabeceras IPv4/UDP y ENet), dentro de los < 5 KB/s de `03`.» Y en «Implementación»: «estado del coche de 50 B» pasa a «de 57 B (50 B hasta el protocolo 2)»; añadir: «`StateBuffer::sample` interpola `s` y `t` por paso y, al extrapolar, las sigue con la pendiente de la pareja anterior del mismo recolocado (a 8 pasos o más; sin ella, quietas); el contador de recolocaciones sube también con un atajo del tramo.»

**Propuesta para `docs/05` (fila del 2026-10-22, Red, columna «En la fase 3»):** «protocolo 1 con estado del coche de 50 B» pasa a «protocolo 1 con estado del coche de 50 B (protocolo 3, lote 6: 57 B con `s`, `t` y «en carretera»)». Es del consultor.

## Pide de lo compartido

Nada.
