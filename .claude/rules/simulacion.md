---
paths:
  - "src/sim/**"
  - "src/gen/**"
  - "src/net/**"
  - "tests/**"
---

# Reglas de simulación y reproducibilidad

Jolt da el mismo resultado si las llamadas que modifican la simulación se hacen exactamente en el mismo orden. Todo el código de estas carpetas sigue estas reglas.

## Jolt
- Compilado con `JPH_DOUBLE_PRECISION` y `JPH_CROSS_PLATFORM_DETERMINISTIC`, con `JobSystemSingleThreaded`.
- El tramo entero (carretera y franja de terreno de ±60 m) se añade antes de la salida, en lote (`AddBodiesPrepare` / `AddBodiesFinalize`) y en un orden fijo que depende solo de la descripción del tramo.
- Durante una pasada no se añaden ni se quitan cuerpos ni restricciones.
- Los cuerpos se crean en orden fijo; si un identificador debe coincidir entre PCs, usa `CreateBodyWithID`.
- Los callbacks de contacto solo leen; los efectos (daños, pinchazos) se aplican después del paso, en orden fijo.

## Matemática y aleatorios
- Toda función matemática pasa por `rmath` (`rmath::sin`, `rmath::sqrt`, ...). Nunca `std::sin` directo en simulación o generación.
- Aleatorios solo con el PCG propio, sembrado desde la semilla del tramo. Nunca `<random>`, `rand()` ni direcciones de memoria.
- Nada de iterar sobre contenedores sin orden (`unordered_map`, `unordered_set`) si el resultado afecta a la simulación.

## Tiempo y entradas
- La simulación avanza solo por pasos de 1/240 s. Nunca lee el reloj del sistema ni el delta del fotograma.
- Las entradas se registran por número de paso. El registro incluye recolocaciones, acciones y cambios de host.
- Las acciones empiezan y terminan en pasos de física confirmados por el host.

## Coordenadas
- Posiciones en doble precisión; origen en el centro del tramo.
- Por red, posición en enteros de 32 bits en milímetros respecto al origen del tramo.

## Tests obligatorios
- Reproducibilidad: la misma secuencia de entradas da el mismo tiempo y el mismo estado final, dos veces seguidas.
- Seguimiento: `s` nunca salta ni retrocede recorriendo un tramo con horquillas a 10, 50 y 120 km/h; una bajada campo a través entre lazadas se detecta como atajo.
- Generador: la misma semilla da el mismo hash de tramo.
- Cambio de host: `SaveState` + restauración continúa la pasada sin saltos visibles.
