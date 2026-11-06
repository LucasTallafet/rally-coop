# Fragmento: tramo-ajustes (lote 5, sim)

## Fila del registro
| Tarea | Qué se hizo | Tests | Huella y reservas | Validado a ojo | Horas |
| --- | --- | --- | --- | --- | --- |
| lote 5 · tramo-ajustes | Frenada tras la meta de 150 a 250 m (`kStageRunoffLength`): `s_end` = L + 250 en `lazadas` (1400 m) y la frenada entera recorrida a 15 m/s en 4217 pasos, siempre en carretera, Δs de 0,0610 a 0,0647 m por paso y 0 pasos-rueda sin carga; colisión hasta 59 m y ninguna a 61 m en L+155…L+245 (40 de 40 rayos). `data/stages/lazadas.stage` = la de `tests/` byte a byte (2887 B), con su test. Conductor de test por `rmath` (`atan2`, `hypot` nueva con su test bit a bit en double y float, `abs`, `exp`, `log`, `sqrt`). Test del microrrelieve del mundo: sin él, `state_hash` 0xbc6f4b7b3162022b frente a 0x06270f27dd850330. Centro de curvatura de un arco (respuesta del consultor, opción A): el desborde de pies a < 1 cm del vértice de las cuñas se cuenta en `StageGround::center_overflows()` y no en `candidate_overflows()`; ninguna altura cambia (2 en `hairpin_flat`, la misma muestra (12, −200) compartida por dos baldosas, a 0 m del vértice; 0 en `lazadas`). | Release 599 → 602 (3 nuevos); Debug 599/599 | Sin cambios: 0x66c9e3a86632750e, 1990, kSimVersion 5, telemetría 0xfe3c612ddcaa6805, `kExpectedStageHash` 0x5876c61a309019a0. Suelo 0x8b9c9097177e30a0 → 0xdd438cd676e8c121 y estado 0x681749b496265c69 → 0x06270f27dd850330, los dos por el suelo más largo (con 150 m y el conductor por `rmath` salen los de antes); iguales en Debug y Release | Sin validar a ojo (nada que probar jugando) | |

## Hallazgos
| Hallazgo | Detalle |
| --- | --- |
| Centro de curvatura de un arco: punto singular del terreno del formato 1 | Todos los abanicos de un arco llegan por dentro hasta el centro (1/κ); una muestra que cae justo ahí tiene un pie en cada uno y desborda. Con la frenada de 250 m el origen se mueve 50 m y una muestra cae en el centro de la horquilla R 12 de `hairpin_flat`. Su altura no cambia (todos los pies valen −0,005 m); el desborde se cuenta aparte. |
| El hash de estado cambia por el suelo, no por el conductor | Con `kStageRunoffLength` = 150 y el conductor ya por `rmath`, el suelo da 0x8b9c9097177e30a0 y el estado 0x681749b496265c69 (los del lote 4): `rmath` es bit a bit `std::` y la opción A no toca alturas. |

## Ediciones propuestas
- `docs/02-arquitectura.md`, punto «Prolongaciones» (§ del seguimiento, l. 175): quitar la frase final «Hasta el lote 5, 150 m.».
- `docs/08-bitacora-de-desarrollo.md` §4, fila nueva: «| Centro de curvatura de un arco (lote 5, `tramo-ajustes`) | Una muestra justo en el centro tiene un pie en cada abanico del arco y desborda los que guarda; salió en `hairpin_flat` al alargar la frenada a 250 m | Punto singular del terreno del formato 1: el desborde se cuenta aparte (`center_overflows()`, a < 1 cm del vértice) y no cambia ninguna altura; lo arregla el formato 2 |»

## Lo compartido
Nada.
