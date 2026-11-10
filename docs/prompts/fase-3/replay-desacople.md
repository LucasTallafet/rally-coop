# Tarea `replay-desacople` (lote 6, carril herramientas): los tests de `rally_replay` no copian cifras del suelo

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-6.md` §2, que manda sobre este prompt.

## Por qué

`formato-2` (carril sim, este lote) cambia el suelo del tramo: cambian `kExpectedStageGroundHash` y la pasada de `stage_lazadas.rrec`, que el carril sim regenera. Dos tests de `tests/tools/CMakeLists.txt` copian esas cifras a mano:

- `replay.info_stage_v8`: `hash del suelo: +0xdd438cd676e8c121` y `pasada: +1:28,93 \(21345 pasos\), 1 recolocaciones, 0 atajos`;
- `replay.run_stage_pass`: `pasada: +1:28,93 [(]21345 pasos[)], 1 recolocaciones, 0 atajos`.

Son de herramientas y el integrador no puede tocarlos, así que la fusión de `formato-2` fallaría. Las cifras exactas ya las fija su dueño: el hash, `kExpectedStageGroundHash` (`tests/sim`); la recolocación, `tests/io/test_stage_v8.cpp`. Criterio nuevo de `docs/agentes/criterios-revision.md` (lo aplica el integrador en el paso 1): **un test no copia una cifra que calcula otro carril; comprueba la forma o una relación.**

## Qué hacer

1. **`replay.info_stage_v8`:** la regex comprueba la forma y deja las cifras libres: `hash del suelo: +0x[0-9a-f]{16}\n` y `pasada: +[0-9]+:[0-9]{2},[0-9]{2} \([0-9]+ pasos\), [0-9]+ recolocaciones, [0-9]+ atajos\n`. Se quedan exactos el formato (8), el nombre del tramo, la longitud (1150,0 m) y los bytes del texto (2887): salen de `lazadas.stage`, que `formato-2` no toca (contrato del lote).
2. **`replay.run_stage_pass`:** lo mismo con la línea de la pasada, y sigue pidiendo `hash OK` y que no salgan `FALLO` ni `aviso`.
3. **La relación, en un script nuevo** `tests/tools/check_pass_line.py` (solo biblioteca estándar, como `check_stage_csv.py`): lee la salida de `rally_replay info` y la de `run` sobre `stage_lazadas.rrec` y comprueba que:
   - `m:ss,cc` es `pasos / 240` truncado a la centésima (la regla del lote 5: 21 345 pasos = 1:28,93);
   - la línea de la pasada es igual en `info` y en `run`.

   Dos tests de ctest con su salida en un archivo, o un solo test que lance `rally_replay` dos veces: lo que sea más sencillo con el CMake que ya hay. Sin Python ≥ 3.12 no se añaden, como los demás.
4. **Un test que el script falla cuando debe:** con una línea fabricada en la que el tiempo no corresponde a los pasos (por ejemplo, `1:28,94 (21345 pasos)`), sale con un código distinto de 0.

## Criterios (en `tests/tools/`, Release y Debug)

1. Con `stage_lazadas.rrec` de hoy pasan los tests nuevos y los cambiados.
2. **Falla cuando debe:** cambia a mano, en una copia temporal de la salida, el tiempo o los pasos, y el script falla; restáuralo. Lo apuntas en el informe (el verificador lo repite).
3. Ningún test de `tests/tools/` contiene ya `0xdd438cd676e8c121`, `21345` ni `1:28,93` como valor esperado de `stage_lazadas.rrec` (sí en un comentario que explique la regla, si hace falta).
4. `make_rrec_variant.py` y sus tests, sin cambios: no dependen del suelo (cambian el texto del tramo y el pie, y fallan claro si no encuentran lo que buscan).

**Sin cambios:** nada fuera de `tests/tools/`. Ni una línea de `tools/replay/`.

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| `info` y `run` imprimen la pasada en el mismo formato | Los dos tests de hoy, con sus regex | Parada: qué línea difiere |

## Paradas

Solo una premisa falsa. Lo demás, avisos.

## Para el informe

- La lista de tests cambiados y nuevos, con su regex.
- Nada que probar jugando.
