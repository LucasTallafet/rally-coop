# Tarea `replay-tramo` (lote 5, carril herramientas): `rally_replay info` con el tramo

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-5.md` §2.3, que manda sobre este prompt. Sale de `integracion` con `grabacion-v8` ya fusionada.

## Qué hacer

`rally_replay info` (`tools/replay/`), con una grabación de formato 8 con tramo, enseña además:
- **tramo:** su nombre (`name` del `.stage`), longitud, número de bytes del texto y `stage_ground_hash` de la cabecera;
- **pasada** (del pie): tiempo m:ss,cc (o «sin meta»), recolocaciones y atajos.

Sin tramo, ni una línea más que hoy. Con formatos anteriores al 8, como hoy. Solo lee con `io::parse` y `io::parse_world` (y `sim::StageTrack::build` para el nombre y la longitud): nada nuevo en `src/io` ni en `src/sim`.

`rally_replay run` no necesita nada: el aviso «el tramo ha cambiado» lo da `io::replay` (`grabacion-v8`). Si lo enseña mal, dilo; no lo cambies en `src/io`.

## Criterios (tests en `tests/tools/`, en ctest)

1. `replay.info_stage_v8`: con `tests/data/recordings/stage_lazadas.rrec`, la salida tiene el nombre, la longitud (1150 m), el hash y el tiempo de la pasada, con valores exactos (no `.*`: en el lote 2, una regex del hash aceptaba cualquier valor).
2. `replay.info_zone_lap_no_actions` y `replay.info_actions_v7` de hoy, sin cambios.

**Sin cambios:** nada fuera de `tools/replay/**` y `tests/tools/**`.

## Paradas

Ninguna prevista: si algo de `src/io` no da lo que necesitas, avisa en el informe.

## Para el informe

La salida entera de `info` con `stage_lazadas.rrec`. Nada que probar jugando.
