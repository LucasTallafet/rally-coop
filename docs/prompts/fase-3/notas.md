# Tarea `notas` (lote 7, carril juego): modelo de nota, cola del host, mensajes de red y registro

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-7.md` §2, que manda sobre este prompt. Criterio de gusto: `docs/agentes/criterio-de-lucas.md` §5.

## Por qué

Las notas son el centro del juego (`docs/01` §5): el copiloto las ancla a un punto de la carretera y el conductor las ve en el salpicadero. Hoy no existe nada de eso: la red solo lleva el estado del coche del host al copiloto (`net/protocol.h`, protocolo 3). Esta tarea pone el modelo, sin pantalla: los diales (tarea `diales`) y el panel (tarea `panel`) salen después y usan lo que quede fusionado.

## Qué hacer

1. **Modelo de nota** en `src/game/` (puro, sin red ni plataforma; `cmake/CheckSimRules.cmake` revisa `src/game`):
   - una nota tiene un ancla `s` (la que elige el copiloto con el cursor), un identificador, y o bien una curva (grado del 6 al 1 o H, y lado) o bien un peligro solo (`docs/01` §5.3 y §5.4);
   - modificadores del dial secundario: se cierra, se abre, no cortes, larga; y peligros: cresta, salto, cuidado. Se añaden a la última nota o van solos;
   - distancia pegada a la nota: ninguna, «y» (enlaza), metros, o «300+». **Los metros son un entero cualquiera de 0 a 990,** no solo los ocho valores de la rueda: hay dos formas de elegir la distancia (decisión de Lucas, 2026-11-12, `mensajes/lote-7.md` §2.3) y el modelo sirve a las dos.
2. **Órdenes del copiloto** (lo único que cambia la cola): nota de curva, peligro solo, modificador a la última, distancia a la última, borrar la última, vaciar las pendientes (`docs/01` §5.3, «Borrar»).
3. **La cola, en el host, que es la autoridad.** Aplica las órdenes en el orden en que llegan. De la cola y de la `s` del coche salen, sin más estado:
   - la nota siguiente y la de después (las de ancla por delante del coche), para el panel;
   - **una nota no «se gasta»:** si el coche se recoloca por detrás de un ancla, la nota vuelve a ser la siguiente (`docs/01` §5.4). Que una nota esté pasada es una comparación con la `s`, no un campo;
   - los sucesos que el panel tiene que enseñar una vez: nota nueva en el hueco izquierdo (para el pitido por su lado) y nota borrada (se tacha medio segundo). Van como contadores o con identificador, no como un aviso que se pueda perder.
4. **El juego nunca modifica una nota** (`docs/01` §5.4): ni corrige, ni ajusta metros, ni descarta, ni une. Una nota con un ancla fuera del tramo se acota al tramo y nada más.
5. **Red, protocolo 4:**
   - del copiloto al host, por el canal fiable, las órdenes;
   - del host al copiloto, por el canal fiable, lo que el copiloto necesita para su interfaz: qué anclas tienen nota (el cursor salta al siguiente cambio de dirección sin nota, `docs/01` §5.2), cuántas hay pendientes y cuál fue la última. El copiloto no lleva su propia cuenta: muestra lo que dice el host;
   - `net::Host` y `net::Copilot` ganan las funciones para mandar y recoger esos mensajes. Sin `enet.h` en los encabezados, como hoy;
   - un copiloto del protocolo 3 se rechaza con «protocolo 3, este juego usa el 4».
6. **Registro de notas** (`docs/04`, fase 3): un archivo de texto por pasada, que escribe `src/io` cuando la plataforma se lo pide. Una fila por orden recibida, con: el paso de la simulación, el ancla `s`, la nota como texto (por ejemplo «3 izquierda se cierra»), la distancia elegida, la **distancia real** hasta el cambio de dirección siguiente (de `game::direction_changes`: del final del cambio más cercano al ancla al principio del siguiente), la `s` del coche en ese paso y qué orden fue. Los borrados también se registran. CSV en UTF-8 con BOM, como el de la telemetría.
7. **Texto de una nota** («3 izquierda larga 80», «H derecha», «cuidado»): una función pura, en español, para el registro y para los tests.

Fuera de esta tarea: dibujar nada, el sonido, las teclas, el `SaveState` (fase 5) y el resumen del tramo (`docs/01` §5.5, fase 5).

## Contratos (de `mensajes/lote-7.md` §2)

- Protocolo de red 3 → 4. El estado del coche (57 B) no cambia.
- No cambian `kSimVersion`, la huella, el formato `.rrec` ni el CSV de telemetría. Las notas no entran en la grabación ni en `state_hash()`.
- Los nombres exactos de los tipos y las funciones se cierran en la revisión de tu plan. `diales` y `panel` salen después de tu fusión y usan lo que quede.

## Criterios (tests en `tests/game/`, `tests/net/` y `tests/io/`, Release y Debug)

1. Cada orden hace lo que dice `docs/01` §5.3, incluidos los casos límite: modificador o distancia sin nota previa (no hace nada y se registra), borrar con la cola vacía, vaciar, dos notas en la misma ancla (las dos se guardan, en orden de llegada).
2. La nota siguiente y la de después, para una `s` del coche que avanza, que salta hacia delante (atajo) y que vuelve atrás (recolocación): la nota reaparece.
3. Los sucesos de nota nueva y de nota borrada se ven exactamente una vez cada uno, aunque el panel pregunte tarde.
4. Ida y vuelta por la red en local (host y copiloto en el mismo proceso, como los tests de hoy): 200 órdenes mezcladas llegan todas y en orden; el copiloto ve las anclas con nota que tiene el host; con la red simulada «mala» (250 ± 80 ms, 20 % de pérdida en los estados), las órdenes siguen llegando todas, porque van por el canal fiable.
5. Mensajes mal formados (tamaño que no cuadra, tipo desconocido, grado fuera de rango): se rechazan sin leer fuera del mensaje.
6. El registro de una pasada de test: las filas, la distancia real frente a una calculada a mano en `lazadas` (los seis cambios de dirección de `cambios-direccion`), y el texto de cada nota.
7. Cero reservas de memoria al aplicar una orden y al consultar la cola, con una capacidad fija de notas que propones tú (de partida, 256; al llenarse, la orden se rechaza y se registra).
8. Coste: lo que añade `Host::service` con 10 órdenes por segundo, medido, en ms por fotograma. Sin umbral: se informa.

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| El canal fiable de hoy (el 0) sirve en los dos sentidos | Lee `host.cpp` y `copilot.cpp`: hoy el copiloto solo manda el saludo | Dilo en el plan con lo que haya que cambiar |
| La `s` del coche que usa la cola es la de `SimWorld::stage_run()` del host | `net/car_state.cpp`, `car_state_from` | Parada: las notas se anclan a esa `s` (`mensajes/lote-6.md` §2.2) |
| `game::direction_changes` da lo que necesita la distancia real | `src/game/direction_changes.h` | Propón en el plan lo que falte, sin cambiar lo que ya usa la tablet |

## Paradas

- Una premisa de la tabla marcada como parada.
- Un test de otro carril falla: no lo toques, «Necesita consultor».
- Para cumplir algo hay que cambiar `docs/01` §5.

Lo demás, avisos.

## Para el informe

- La interfaz que queda para `diales` y `panel`, con un ejemplo de uso de cada lado.
- El formato del registro, con tres filas de ejemplo.
- Fragmento `docs/progreso/notas.md`: la edición de `docs/02` §7 (protocolo 4 y mensajes de notas) y la de `docs/07` si procede.
