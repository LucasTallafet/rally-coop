# Fragmento: tramo-modo (lote 5, plataforma)

## Fila del registro
| Tarea | Qué se hizo | Tests | Huella y reservas | Validado a ojo | Horas |
| --- | --- | --- | --- | --- | --- |
| lote 5 · tramo-modo | El tramo en el juego y en el copiloto. K recorre zona → tramo → recinto (`WorldKind`, `kContextStage`; G, M, U e Y no valen en el tramo); `--tramo <archivo>` arranca en él y, sin él, K lee `data/stages/lazadas.stage` con `find_data_file`; un tramo que no se lee o no vale deja el modo como estaba con su línea en los mensajes. El mundo sale con `stage_text` y el coche parado en `stage_spawn(track, -5,0)` (`kGameStageSpawnS`). `StageScene` guarda el tramo y su `StageDraw` (solo se reconstruyen si cambia el texto) y los usan el juego y el copiloto. En pantalla: «tramo \<nombre\> · m:ss,cc» (tras la meta, «meta … · recol. N · atajos N»), mensajes por cambio de contador (parcial, recolocación, atajo; la meta en su hueco, 30 s) y dos líneas de F3. El reloj **trunca** a la centésima con la cuenta de `rally_replay info` (21345 pasos → `1:28,93`). La grabación lleva `stage_ground_hash` y `StageResult`; el host manda el hash al copiloto, que construye el tramo, compara y dice «tramo: hash OK / distinto» en F3. Tab en el tramo: asiento, persecución, todo el tramo (caja del eje encuadrada con el aspecto de la ventana) y fija sobre la salida. La línea «motor» de F3 pasa a dos (`motor` y `embrague`). Capturas: cambian `interior_ayuda.png` (K) e `interior_f3_t.png` (línea del motor) y nace `tramo_salida.png`. Carga de `lazadas`: mundo 137 ms + dibujo 144 ms. | de 647 a 661 | Sin cambios (`kSimVersion` 5, huella `0x66c9e3a86632750e`, 1990 reservas, telemetría `0xfe3c612ddcaa6805`) | Pendiente: lo prueba Lucas (informe) | |

## Hallazgos
| Hallazgo | Detalle |
| --- | --- |
| Un `WorldMode` con `zone`/`stage` y un tipo `WorldKind` | El mundo del tramo no tiene cuerpos: seis sitios leían `desc.bodies[0]` en la rama «no zona». Ahora comprueban `kind() == Arena` (revisión del plan). |
| El indicador no cabe con el coche y el reloj | A 1280×720 el indicador mide como mucho 1148 px y la línea con el nombre del coche, el reloj y el volante pasaba de 1430. En el tramo no se dice el coche (F8 lo dice al cambiarlo) y el nombre del tramo se recorta a 16 cifras de ancho. |
| F3 en 1920×1080 solo tiene 694 px de contenido | La línea del tramo del contrato (744 px) no cabía: son dos líneas («tramo: s … / L m, t … m, en carretera» y «recolocación en s = … m · recol. N · atajos N»). |
| `lazadas` no tiene parciales | El primero iría en s = 1000 > 1150 − 200: Lucas no verá ninguno. Los mensajes de parcial se prueban con un estado hecho a mano. |
| Estado de la carga | Con `lazadas` el mundo tarda 137 ms y las mallas 144 ms: no hace falta hilo de fondo. |
| Comentarios viejos | `src/io/telemetry_csv.h:39` y `src/sim/sim_world.h:102` siguen diciendo cosas del lote 4 (rutas de otros carriles; no pasé por ellos). |

## Ediciones propuestas
- `docs/02-arquitectura.md`, donde diga que el tramo en el juego es del lote 5: «K recorre zona → tramo → recinto. El modo tramo (`platform/stage_mode`, `stage_scene`, `stage_hud`, `stage_overview`) construye un mundo solo con `stage_text` y el coche parado en `stage_spawn(track, -5)`; `StageScene` dibuja con `StageDraw` (un solo dibujo para el juego y para el copiloto). El reloj del tramo cuenta pasos de la simulación entre 240 y se trunca a la centésima. El copiloto construye el mundo con `build_copilot_world`, compara `stage_ground_hash` y dibuja el recibido.»
- `docs/07-entorno-y-portabilidad.md`: `rally.exe --tramo <archivo .stage>` (arranca en el modo tramo; sin él, K carga `data/stages/lazadas.stage`) y la captura de referencia `tramo_salida.png` (`stage_lazadas.rrec`, paso 1200).
- `docs/08-bitacora-de-desarrollo.md`: (1) el reloj trunca a la centésima, como el cronometraje (decisión del consultor, 2026-11-06); (2) el indicador del tramo no dice el coche y F3 usa dos líneas porque no caben (1148 px y 694 px a 1280×720 y 1920×1080); (3) un mundo de tramo no tiene cuerpos: todo código que lea `desc.bodies[0]` tiene que comprobar que es el recinto; (4) la línea «motor» de F3 se parte en dos (como «ejes»), con la captura de F3 regenerada una sola vez.

## Lo compartido
- Los comentarios de `src/io/telemetry_csv.h:39` y `src/sim/sim_world.h:102` (decisiones §3 del consultor): no son de mi carril; los corrige quien pase por esos archivos.
- Nada más.
