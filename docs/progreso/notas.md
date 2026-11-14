Fragmento de `notas` (lote 7, carril juego). Rama `lote-7/notas`.

## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-13 | 3 · lote 7 `notas` | | Notas del copiloto sin pantalla. `src/game/notes.*`: modelo de nota (ancla `s`, grado 6–1/H y lado, o peligro solo; modificadores se cierra/se abre/no cortes/larga; peligros cresta/salto/cuidado; distancia ninguna/«y»/metros 0–990/«300+», con la forma usada, rueda o contador), seis órdenes (curva, peligro, modificador, distancia, borrar, vaciar pendientes) y la cola del host (256 notas, sin reservas; la nota siguiente y la de después son una comparación con la `s` del coche, así que una recolocación hacia atrás las hace reaparecer; nota nueva por identificador y notas borradas con contador y anillo, sin avisos que se pierdan). Protocolo de red 3 → 4: `src/net/notes_wire.*` (orden de 14 B, estado de notas de 25 + 4·n B, anclas en mm), `Host::take_note_order` y `publish_notes`, `Copilot::send_note_order` y `notes()`; canal fiable en los dos sentidos; un copiloto del protocolo 3 se rechaza con «protocolo 3, este juego usa el 4». `src/io/notes_log.*`: registro CSV (UTF-8 con BOM) con una fila por orden, la distancia elegida y la real hasta el cambio de dirección siguiente. Tests 715 → 740 (+25: 10 de juego, 12 de red, 3 de io), Release y Debug en verde. Huella `0x66c9e3a86632750e`, reservas de Jolt 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5 sin cambios; `.rrec` 8 y CSV de telemetría sin tocar. Coste en el host con 10 órdenes/s: 0,007–0,010 ms por fotograma de media (0,002–0,003 sin órdenes). Sin validar a ojo (no tiene pantalla). | `diales` (conecta la cola en `DemoWorld::update_network`, ver «Pide de lo compartido») |

## Hallazgos

| Fecha | Dónde | Hallazgo | Qué hacer |
| --- | --- | --- | --- |
| 2026-11-13 | `01` §5.3 y código | El prompt dice que el registro es «CSV en UTF-8 con BOM, como el de la telemetría», pero el CSV de telemetría no lleva BOM. Se hizo con BOM, como pide el prompt; la telemetría no se toca | Quien lea el registro con Python usa `encoding="utf-8-sig"` |
| 2026-11-13 | `src/net` | `set_impairment` (red simulada) solo actúa sobre los estados del coche en el copiloto; el canal fiable no pasa por ella. El test de la red «mala» comprueba que las órdenes llegan todas y en orden mientras los estados se pierden (87 perdidos, 228 tarde), no que se pierdan paquetes de órdenes | Si se quiere probar pérdida de verdad en el canal 0, hace falta un simulador a nivel de ENet (no está en el alcance) |
| 2026-11-13 | `src/net/host.cpp` | La generación del mundo sube en cada `send_world`, también al reconectar un copiloto sin mundo nuevo. Una orden en vuelo en ese momento se descarta y se cuenta (`LinkStats::orders_dropped`) | El registro de notas es «una fila por orden recibida por la cola»: las descartadas por generación no llegan a la cola y no tienen fila; el contador las deja a la vista |
| 2026-11-13 | Diseño | «Vaciar» quita solo las notas con ancla por delante del coche. Tras un vaciado y una recolocación hacia atrás, una nota pasada puede reaparecer | Pregunta para Lucas en la prueba del lote |

## Ediciones propuestas

**`docs/02-arquitectura.md` §7, tabla de mensajes**, sustituir las dos filas de notas por:

| Mensaje | Sentido | Canal | Frecuencia |
| --- | --- | --- | --- |
| Orden de nota (14 B: tipo, generación, clase, ancla `s` en mm, grado, lado, marca, clase de distancia, metros, forma de la distancia). Clases: curva, peligro solo, modificador, distancia, borrar la última, vaciar las pendientes | Copiloto → host | Fiable | Evento |
| Estado de las notas (25 + 4·n B: generación, revisión, resultado de la última orden, la última nota, anclas de todas las notas en mm) | Host → copiloto | Fiable | Cuando cambia la cola |

**`docs/02-arquitectura.md` §7, párrafo «Sincronía»**, tras «Un copiloto del protocolo 2 se rechaza con «protocolo 2, este juego usa el 3».», añadir:

> Protocolo 4 (lote 7): dos mensajes de notas por el canal fiable (`net/notes_wire.h`, tipos 5 y 6). El copiloto manda órdenes (14 B, con la generación del mundo que tiene: el host descarta y cuenta las de otro mundo y las de un copiloto que no es el aceptado) y el host le devuelve el estado de las notas cuando cambia la cola: qué anclas tienen nota, la última y cuántas hay por delante (`notes_ahead`). El copiloto no lleva su propia cuenta. Las anclas viajan en mm (i32): quien compare un ancla con otra `s` usa al menos `kNoteAnchorStepM` = 1 mm. La cola (`game::NoteQueue`, 256 notas) no vive en `net::Host`: la aplica quien consume `take_note_order`, que es quien conoce el paso y la `s` del coche para el registro. Un copiloto del protocolo 3 se rechaza con «protocolo 3, este juego usa el 4». El estado del coche (57 B) no cambia. Con 256 notas el estado de notas son 1049 B, y solo sale cuando cambia la cola.

**`docs/02-arquitectura.md`, módulos de `src/game` / `src/io`**: añadir «`game/notes`: modelo de nota, órdenes y cola del host (puro, sin red); `io/notes_log`: registro de notas por pasada (CSV UTF-8 con BOM; columnas `step,order,result,note_id,anchor_s,note,distance,distance_source,real_distance_m,car_s,erased`). Las notas no entran en el .rrec, en `state_hash()` ni en la telemetría».

**`docs/07`:** nada.

**`docs/08` (bitácora), entrada nueva:** «Lote 7, `notas`. La cola de notas vive fuera de `net::Host` para que el registro conozca el paso y la `s` en el momento de aplicar, y para que el copiloto bot del lote 8 cante sin red. Una nota no se gasta: pasada es `s_nota < s_coche`, y por eso reaparece al recolocarse. Los sucesos (nota nueva, nota borrada) son identificadores y contadores, no avisos. Los metros son un entero de 0 a 990 para servir a las dos formas de elegir la distancia; la forma va en el registro. `ClearPending` solo quita las de por delante del coche».

## Pide de lo compartido

- **Para `diales` (y el director):** esta tarea no conecta nada en `DemoWorld::update_network` (`src/platform/demo_world.cpp`). `diales` tiene que montar, por mundo: una `game::NoteQueue` (`reset(longitud del eje)` en cada `reset()` del mundo), un `io::NotesLog` por pasada (junto a `grabaciones/`, con `notes_log_header(nombre del tramo)` y `direction_changes(axis)`), y por fotograma: `host.service()`, `while (host.take_note_order(o)) { applied = queue.apply(o, car_s); log.append(step, car_s, o, applied, changes); }` y `host.publish_notes(queue)`. Sin eso el registro de notas de la fase 3 se queda sin dueño.
- Nada más (`docs/01`, `05`: sin cambios).
