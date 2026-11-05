# Tarea `tramo-ajustes` (lote 5, carril sim): frenada de 250 m, el tramo del juego y dos arreglos del lote 4

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-5.md` §2.1, que manda sobre este prompt.

## Por qué

- **Frenada tras la meta, de 150 a 250 m** (decisión de Lucas, 2026-11-05). Con ABS, las frenadas medidas desde 100 km/h son 35,8 / 53,8 / 84,7 m en asfalto, grava y nieve (`progreso.md`, 2.8); a 150 km/h, escalando con v², 81 / 121 / 191 m. Con 150 m, en nieve se sale de la recta, y a 60 m más se acaba la colisión.
- **El juego necesita su tramo** en `data/stages/` (lo carga `tramo-modo`). Hasta el lote 7, el de test.
- Dos avisos del lote 4: el conductor de test usa `std::atan2` y `std::hypot` (revisor de determinismo), y ningún test detecta el microrrelieve del mundo apagado salvo `kExpectedStageStateHash` (verificador).

## Qué hacer

1. **`kStageRunoffLength` = 250,0** en `src/sim/stage_track.h`. Actualiza los tests que dependen del valor (por ejemplo, el de las prolongaciones) con el valor nuevo, no con otro umbral.
2. **`data/stages/lazadas.stage`:** copia byte a byte de `tests/sim/stages/lazadas.stage`, y un test que lee los dos y comprueba que son iguales (así `kExpectedStageHash` cubre los dos). La de `tests/` no se toca.
3. **`tests/sim/common/stage_driver.h`:** `std::atan2` (l. 178) y `std::hypot` (l. 203) pasan por `rmath`. Si `rmath` no tiene alguna, se añade con el mismo envoltorio que las demás (`rmath.h`), sin cambiar las que hay.
4. **Test del microrrelieve del mundo:** el mismo recorrido por `lazadas` con el microrrelieve del mundo encendido y apagado (`MicroRelief::None`, el camino de `sim_world.cpp` que apagó el verificador) da `state_hash` distinto, y encendido da el de siempre. Que falle si el microrrelieve deja de aplicarse.

## Criterios (tests en `tests/sim/`, Release y Debug)

1. `StageTrack::s_end()` = L + 250 m en `lazadas`; las prolongaciones llevan el coche hasta ahí sin saltos (el test de hoy, con el valor nuevo).
2. La colisión llega hasta 60 m de la recta de frenada entera (el test de la franja de hoy, con el valor nuevo).
3. `data/stages/lazadas.stage` = `tests/sim/stages/lazadas.stage`, byte a byte.
4. El test del microrrelieve del punto 4.
5. **Huellas:** `kExpectedStageHash` sin cambios; `kExpectedStageGroundHash` nuevo, igual en Debug y Release; `kExpectedStageStateHash`, nuevo si cambia (con el motivo: el conductor por `rmath` o el suelo más largo; lo mides por separado, aplicando primero un cambio y luego el otro), igual en Debug y Release.
6. Presupuestos del lote 4 con 250 m, medidos y comparados con la carretera de la 2.6b en la misma pasada: paso en `lazadas` y en el tramo sintético de 30 km, carga y memoria (`05`, «Carga del tramo»). Se informan; solo paran si un presupuesto de `lote-4.md` §2.6 deja de cumplirse.

**Sin cambios:** `kSimVersion`, la huella, las reservas de Jolt, el hash de la telemetría, el formato de la grabación y todo lo que mide la 2.6b.

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| Alargar la frenada solo cambia el suelo de Jolt y lo que se mide con él | `kExpectedStageHash` igual y los tests de `tramo-seguimiento` pasan con 250 m | Parada: qué test y por qué |
| `rmath` da el mismo resultado que `std::` en este PC | El test de `rmath` de hoy (bit a bit frente a `std::`) | Parada |

## Paradas

Solo: un presupuesto del lote 4 que deja de cumplirse, o una premisa falsa. Lo demás son avisos («Lista con avisos»).

## Para el informe

- Los hashes nuevos y el motivo de cada uno.
- Las cifras del criterio 6.
- Nada que probar jugando.
