# Fragmento: cabina-volumen (lote 5, plataforma)

## Fila del registro
| Tarea | Qué se hizo | Tests | Huella y reservas | Validado a ojo | Horas |
| --- | --- | --- | --- | --- | --- |
| lote 5 · cabina-volumen | Volumen seguro de cabina por coche, sacado de `[cockpit]` (puerta, techo inclinado como el dibujado, final del techo, parabrisas), 8 cm hacia dentro; el ojo final (asiento + cuello + F9) se lleva al punto más cercano (`platform/cabin_volume`; el copiloto, reflejado). Holgura mínima medida: 0,080 m a la puerta y al techo en los cuatro coches; final del techo 0,317 m y parabrisas 0,375 m (no actúan). | 604 en Release, 5 nuevos (`cabin_volume: ...`) | Sin cambios: 0x66c9e3a86632750e, 1990, kSimVersion 5 | Sin validar a ojo (lo juzga Lucas en la prueba del lote) | |

## Hallazgos
| Hallazgo | Detalle |
| --- | --- |
| El techo dibujado es un plano inclinado | `cockpit.cpp` lo dibuja del borde superior del parabrisas al final del techo; el volumen usa ese plano y no uno horizontal en `roof_above` (con el horizontal, la holgura al techo dibujado bajaba a 0,05–0,06 m con el asiento adelantado). |
| El copiloto ve el interior sin reflejar | Su puerta dibujada es `door_right` (1,04 m), a 0,30 m de su ojo; el volumen reflejado supone 0,31 m (§2.7). Su holgura mínima medida con la mirada a 90° es 0,139 m, así que no se nota; cambiar el espejo es del consultor. |
| Un coche sin `[cockpit]` no tiene cabina | `tests/data/cars/reference.car` (el de las capturas de referencia) no define `[cockpit]`: con las medidas a cero el volumen salía degenerado y el recorte movía el ojo (`ojo_zona.png` y `carretera.png` cambiaban). Ahora `cabin_volume` devuelve un volumen abierto si las medidas de la puerta, el techo y el parabrisas son cero; test con `reference.car`, bit a bit. `golden.ps1` sin `-Regenerar`: 5 capturas iguales. |
| Los cuatro `.car` dan las mismas cifras | Los `[cockpit]` son iguales en los cuatro coches; la premisa (el ojo nominal está dentro con 8 cm) se cumple. |

## Ediciones propuestas
- `docs/02`, donde se describe la cámara del interior (ojo con cuello y F9): añadir «El ojo final se recorta al volumen seguro de la cabina (`platform/cabin_volume`): puerta, techo inclinado, final del techo y parabrisas de `[cockpit]`, 8 cm hacia dentro. Dentro no cambia y es continuo.»
- `docs/08`, bitácora: «cabina-volumen: el techo del volumen es el plano inclinado que se dibuja, no `roof_above` plano. Recorte en x y proyección exacta sobre tres semiplanos en (y, z): 1-Lipschitz, sin saltos.»

## Lo compartido
Nada.
