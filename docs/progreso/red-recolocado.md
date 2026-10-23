## Fila del registro

| 2026-10-23 | 3 | | Arreglo `red-recolocado` (lote 3, carril juego): el recolocado deja de ser un bit frágil. **Parte 1:** el byte de indicadores es un contador de recolocaciones (u8, módulo 256); el host suma 1 en cada `after_steps` con `reset_since_last` (también en pasos que la cadencia no envía; vuelve a 0 con cada mundo enviado) y lo manda en todos los estados; `StateBuffer` no mezcla nunca dos estados adyacentes con contadores distintos: muestra el anterior hasta el paso del nuevo y desde ahí el nuevo, se hayan perdido los paquetes de en medio o no. **Parte 2:** el estado pasa de 47 a 50 B (protocolo 1): dirección aplicada (i16, error ≤ 1/32767; medido 1,53e-05) y presión del turbo (u8, 0,01 bar; medido 0,0050 bar), detrás de las rpm y antes del contador. `CarState` gana `steer`, `boost_bar` y `reset_count` (se va `reset`, que nada fuera de `src/net` leía); `StateBuffer::sample(now, Pose&, SampledState&)` da la dirección interpolada y marcha, rpm, turbo, superficies y compresión del estado más cercano (con un recolocado en medio, todo del estado que se muestra); `sample(now, Pose&)` queda igual. Nuevo `tests/net/test_reset_counter.cpp` (7 casos) y `test_reset_bit.cpp` reescrito (4 subcasos: suma 1 y el contador se mantiene en los estados siguientes, dos recolocados en pasos omitidos = 2, vuelta 255 → 0, mundo nuevo = 0). Red media (100 ± 30 ms, 5 %) con recolocado cada segundo durante 60 s: 59 recolocados, 5 con el primer estado perdido, ninguna muestra a más de 0,000 m de las dos posiciones en 3420 muestras. Mutaciones que fallan: no sumar el contador (1 caso), comparar con el último recibido en vez del par (6), ignorar el contador (6), `reset(gen)` sin vaciar el anillo (2), regla del más cercano en el par con recolocado (1). Sin cambios: error de interpolación ≤ 1 cm sin red, ≤ 3 / ≤ 8 cm con red media, sin reservas en `push` y `sample`. Huella `0x66c9e3a86632750e`, reservas de Jolt 1990, telemetría `0xfe3c612ddcaa6805`, `kSimVersion` 5 y formatos: sin cambios. Pendiente de la prueba de Lucas. | prueba del lote 3 |

## Hallazgos

| Fecha | Hallazgo |
| --- | --- |
| 2026-10-23 | El «sucesor» de `test_reset_bit.cpp` para el criterio 1 del mensaje se reparte en dos archivos: `test_reset_bit.cpp` (lado del host, por ENet real) y `test_reset_counter.cpp` (lado del búfer, con estado perdido y red simulada). |
| 2026-10-23 | Varias pulsaciones de R en un mismo fotograma cuentan como un solo recolocado: `DemoWorld` pasa un `bool` por fotograma a `after_steps`. No importa: el búfer solo mira si el contador cambia. |
| 2026-10-23 | `docs/05-decisiones.md` línea 138 dice «estado del coche de 47 B»: ahora son 50 B (la línea 141 ya habla de contador). Lo actualiza el consultor. |

## Ediciones propuestas

- `docs/02` §7, tabla de mensajes (línea 298): «indicador de recolocación» → «contador de recolocaciones, dirección aplicada, presión del turbo».
- `docs/02` §7, implementación (línea 313): «estado del coche de 47 B» → «estado del coche de 50 B»; añadir: «El recolocado viaja como contador (u8, módulo 256) en todos los estados: el copiloto no mezcla nunca dos estados con contadores distintos, así que perder un paquete no deja ver el coche cruzando el mapa. `StateBuffer::sample` da además la dirección (interpolada) y marcha, rpm, turbo, superficies y compresión del estado más cercano.»
- `docs/08` (bitácora): por qué un contador y no un bit (un bit en un solo estado no fiable se pierde con el paquete: 1 de cada 20 recolocados con 5 % de pérdida) y por qué se compara el par que se interpola y no el último recibido.

## Pide de lo compartido

- Consultor: `docs/05-decisiones.md` línea 138, «47 B» → «50 B».
- `copiloto-asiento` (carril plataforma): usar `sample(now, Pose&, SampledState&)`; la dirección es la aplicada de `CarTelemetry` ([−1, 1], + derecha) y el turbo va en bar sobre la atmosférica.
