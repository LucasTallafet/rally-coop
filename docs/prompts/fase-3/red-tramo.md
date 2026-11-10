# Tarea `red-tramo` (lote 6, carril juego): `s`, `t` y «en carretera» en el estado del coche, protocolo 3

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-6.md` §2.2, que manda sobre este prompt.

## Por qué

La tablet (`tablet`, oleada 2) gira el mapa, coloca el cursor y dibuja la cinta según la `s` del coche, y las notas del lote 7 se anclan a esa `s`. Tiene que ser **la del host**, la que calcula su `StageRun`: el seguimiento es incremental, y en una horquilla una proyección propia del copiloto, desde la pose interpolada, podría elegir el otro brazo. Así, una nota quedaría anclada en un sitio distinto en cada PC.

## Qué hacer

1. **Estado del coche de 50 a 57 B** (`net/car_state.h`, `kCarStateBytes`). Detrás del contador de recolocaciones, en little-endian:
   - `s`: i32, en mm, redondeado al más cercano (de −20 m a L + 250 m; sobra rango);
   - `t`: i16, en cm, redondeado y acotado a [−32 767, 32 767] cm (la franja con colisión es de ±60 m);
   - indicadores: u8, bit 0 = en carretera (`StageRunState`); los demás bits a 0, reservados para la pasada (sin usar en este lote).

   Sin tramo (`SimWorld::stage_run()` nulo), los tres valen 0. `CarState` gana `double s`, `double t` y `bool on_road`; `car_state_from` los saca de `world.stage_run()`, solo leyendo.
2. **Protocolo 3** (`net/protocol.h`, `kProtocolVersion`). Un copiloto del protocolo 2 se rechaza con «protocolo 2, este juego usa el 3», como hoy entre el 1 y el 2. El mensaje del mundo no cambia.
3. **`StateBuffer::sample(now, Pose&, SampledState&)`:** `SampledState` gana `double s`, `double t` y `bool on_road`. `s` y `t` se interpolan por paso, como la posición, y se extrapolan igual (hasta 100 ms; después, quietos). `on_road` sale del estado más cercano, como la marcha. **Con un recolocado en medio, todo del estado que se muestra:** es la regla que ya cumple el búfer para la pose, y ahora vale también para `s`.
4. **`replay.stage_v8`** (`tests/io/CMakeLists.txt`) copia los pasos de `stage_lazadas.rrec` (`pasos: 21976 .*hash OK`). `formato-2` regenera esa grabación con el suelo nuevo, y los pasos cambian. Pasa a `pasos: [0-9]+ .*hash OK`, sin tocar el `FAIL_REGULAR_EXPRESSION`. La cifra exacta la comprueba `test_stage_v8` frente al pie de la grabación, que es relacional. Es el criterio nuevo de `criterios-revision.md`: un test no copia una cifra que calcula otro carril. **Esta rama se fusiona antes que `formato-2`** (`mensajes/lote-6.md` §3).
5. `docs/02` §7 (en tu fragmento): el estado del coche con `s`, `t` y los indicadores, 57 B, protocolo 3. Con 57 B a 30 Hz salen ~1,7 KB/s, dentro de los < 5 KB/s de `03`.

## Criterios (tests en `tests/net/`, Release y Debug)

1. **Ida y vuelta:** 10 000 estados PCG32 con `s` en [−20, 31 400] m y `t` en [−60, 60] m. Error máximo de `s` ≤ 0,5 mm y de `t` ≤ 5 mm (la mitad del paso de cada cuantización). Los campos de hoy, con los errores de hoy.
2. **`decode` rechaza** los tamaños de 0 a 56 y de 58, y un byte de indicadores con algún bit distinto del 0 puesto. Si te parece demasiado estricto para el gancho de la pasada, dilo en la revisión del plan: se puede aceptar con los bits reservados ignorados, pero entonces el test lo dice.
3. **Sin tramo:** `s`, `t` y los indicadores, a 0.
4. **Con tramo:** en un mundo con `lazadas`, conduciendo con el conductor de `tests/sim/common/stage_driver.h` (si el carril juego puede usarlo sin depender de otro carril; si no, entradas fijas que recorran la horquilla), el `s` que sale de `sample` frente al `s` del host en el mismo paso: error ≤ 1 cm sin red simulada y, con red media (100 ± 30 ms, 5 %), ≤ 3 cm interpolando y ≤ 8 cm extrapolando (los límites de la posición en el lote 3, que `s` hereda porque se interpola igual), y **sin retroceder nunca** salvo en una recolocación.
5. **Recolocado:** con un recolocado entre dos estados (y con el estado del recolocado perdido), ninguna muestra de `s` queda entre la `s` de antes y la de después (los tests de `test_reset_counter.cpp`, ampliados a `s`).
6. **Protocolo:** el 2 se rechaza con su mensaje (el test que hoy rechaza el 1, con los números nuevos).
7. **Sin reservas** en `push` y `sample` (el contador de hoy), y el host no cambia la simulación: `state_hash` igual con host y sin él (el test de hoy).

8. Ningún test de `tests/io` ni de `tests/net` copia una cifra del suelo del tramo (`grep` de `21976`, `21345` y `0xdd43` en el informe).

**Sin cambios:** la simulación, el formato de la grabación, el CSV y el tamaño del mensaje del mundo. `revisor-determinismo` revisa esta rama (toca `src/net`).

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| `SimWorld::stage_run()` da `s`, `t` y si está en carretera en cada paso, también tras una recolocación | Leer `StageRunState` en los tests de hoy de `tests/sim/test_stage_world.cpp` | Parada: «Necesita consultor», porque el arreglo sería de sim |
| El host manda el estado del paso que cruza el múltiplo de 8, con la `s` de ese paso | El test de hoy de la cadencia | Parada |

## Paradas

Solo un criterio de resultado que falla (1, 4, 5 o 7) o una premisa falsa. Lo demás, avisos.

## Para el informe

- Las cifras de los criterios 1 y 4.
- Nada que probar jugando (la prueba es la de `tablet`).
