# Tarea `tramo-modo` (lote 5, carril plataforma): conducir el tramo en el juego y verlo en el copiloto

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-5.md` §2.1, §2.3, §2.4, §2.6 («Modo tramo») y §2.8, que mandan sobre este prompt. Sale de `integracion` con `tramo-dibujo` y `grabacion-v8` ya fusionadas.

## Por qué

Es lo que Lucas prueba en este lote: conducir `lazadas` con su tiempo, grabarlo y que lo vea el copiloto. Con esa prueba se decide si el terreno del formato 1 basta hasta el formato 2.

## Qué hacer

1. **Modo tramo** como en §2.6:
   - K recorre zona → tramo → recinto (`WorldMode`, `describe`, el texto de K en su tabla y en `docs/teclas.md`, regenerado);
   - `--tramo <archivo>` y, sin él, `data/stages/lazadas.stage` con `find_data_file`; el error de carga, con su línea, en los mensajes, y el juego se queda en el modo en que estaba;
   - el mundo: `stage_text`, el coche en `stage_spawn(track, kGameStageSpawnS)` con `kGameStageSpawnS = -5.0`, parado, las ayudas de los ajustes;
   - `kContextStage` en `kContextAll`; G, M, U, Y solo en la zona. El test de contextos de hoy (`action_valid_in`) cubre el nuevo.
2. **Dibujo:** `StageDraw::build` con `world.stage_ground_hash()` al construir el mundo; si falla, el mensaje con los dos hashes y sin tramo dibujado (no aborta).
3. **En pantalla** (§2.6): indicador mínimo con el tiempo, mensajes por cambio de contador (parcial, atajo, recolocación, meta) y la línea del tramo en F3. El tiempo sale de `StageRunState` (pasos / 240), m:ss,cc. Sin penalización.
4. **Grabación:** `RecordingSession` pone `stage_ground_hash` en la cabecera y `StageResult` en el pie al terminar; `DemoWorld::publish_world` manda `stage_ground_hash` al copiloto (lo que dejó preparado `grabacion-v8`).
5. **Copiloto** (`copilot_app`): construye el mundo del tramo, compara `stage_ground_hash` (F3: «tramo: hash OK» o «tramo: distinto»), dibuja con `StageDraw` y su Tab pasa por «todo el tramo» (la cámara de «toda la zona» encuadrando la caja del eje con sus prolongaciones).
6. **`--golden`** con una grabación de formato 8 con tramo: construye y dibuja el tramo. Fila nueva en `tests/platform/golden/comandos.txt`, `tramo_salida.png`, con `tests/data/recordings/stage_lazadas.rrec`, desde el asiento a pocos segundos de la salida. Regenera las capturas con `golden.ps1 -Regenerar` y di cuáles cambian (la de la ayuda, por el texto de K, y la nueva).

Reutiliza `ZoneModes`, `RecordingSession`, las capas de `screen_layout`, los mensajes de `Overlay`, `find_data_file` y el indicador de la carretera de prueba (su pasada y sus recolocaciones). Si escribes una utilidad nueva, el plan dice qué buscaste y por qué no sirve lo que hay.

## Criterios (tests en `tests/platform/`, Release y Debug, sin ventana)

1. **K:** zona → tramo → recinto → zona, y un tramo que no se puede leer deja el modo como estaba con el mensaje de su línea (lógica sin raylib, como `ZoneModes`).
2. **Descripción:** en el modo tramo, `describe` da `stage_text` con el texto del archivo, `test_zone` falso y la salida en `stage_spawn(track, -5.0)`; un mundo construido con ella tiene `stage() != nullptr`.
3. **Grabar y repetir:** una pasada del bucle de la partida por el tramo (con las entradas de un test, como el de la 2.1) da una grabación de formato 8 que `io::replay` repite con «hash OK» y el mismo `StageResult`.
4. **Textos:** el del tiempo (0, 59,999 s, 61 s, 10 min), el de cada mensaje y la línea de F3 con valores fijos; ninguno se sale de su capa a 1280×720, 1360×768 y 1920×1080 (las comprobaciones de `screen_layout` de hoy).
5. **Mensajes por contador:** dos recolocaciones en el mismo fotograma dan un mensaje que dice 2 (o dos mensajes), nunca ninguno; un atajo y la meta, uno cada uno.
6. **Copiloto:** con un `net::WorldInfo` como el que manda el host con tramo (construido en el test, sin ENet), lo que hace el copiloto al recibirlo construye el tramo y su hash coincide; con el hash cambiado, «distinto». Si esa parte de `copilot_app` no se puede probar sin ventana, sácala a una función pura y pruébala.
7. **Teclas:** `docs/teclas.md` coincide con la tabla (test de hoy); ninguna tecla repetida (test de hoy).

**A ojo, en la prueba de Lucas** (va a tu informe con su A/B): conducir `lazadas` frente a la carretera de prueba de la 2.6b, en el mismo ejecutable con K.

**Rendimiento:** F1 en el tramo no se puede medir sin teclas (`criterios-revision.md` §3, 4b): va a la prueba de Lucas. Las cifras del dibujo, las de `tramo-dibujo`.

**Sin cambios:** nada de `src/sim`, `src/io`, `src/net` ni `src/game`; la huella y los demás hashes; la zona, la carretera de prueba y el recinto se dibujan y se conducen igual (las capturas de referencia de hoy, iguales salvo la de la ayuda).

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| `DemoWorld` puede construir un mundo sin zona ni recinto (solo el tramo) sin cambiar lo que hace con la zona | Las capturas de referencia de hoy, iguales salvo la ayuda | Parada |
| El copiloto recibe el tramo entero en el mensaje del mundo (protocolo 2) | Criterio 6 | Parada |

## Paradas

Solo: una premisa falsa. Lo demás son avisos.

## Para el informe

- Lo que tiene que mirar Lucas, con el A/B (`mensajes/lote-5.md` §4, puntos 1, 2 y 5).
- Las capturas que cambian.
- Cuánto tarda en cargar el tramo al pulsar K (medido con el reloj de la plataforma, fuera de la simulación).
