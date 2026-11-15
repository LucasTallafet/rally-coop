Fragmento de `diales` (lote 7, carril plataforma). Rama `lote-7/diales`.

## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-15 | 3 · lote 7 `diales` | | Los diales del copiloto en `rally.exe --copiloto`. Dial principal (media rueda de 7 notas por lado, 6 arriba y H abajo), secundario (8 sectores), distancia en dos formas conmutables (rueda de 8 valores o contador de 0 a 990 de 10 en 10 desde 50) y borrar (corto: la última; 1 s: las pendientes), con mando, ratón y teclado sobre una sola lógica sin raylib (`copilot_wheel`, `copilot_dials`, `copilot_dial_input`, `copilot_dials_layout`). Dos capas nuevas junto a la tablet (`DialsLeft`, `DialsRight`) con el dial, lo elegido, la última nota que aplicó el host y las pendientes; el cursor salta al siguiente cambio sin nota; el mapa arranca «hacia delante»; la cabeza se congela con un dial abierto. Lado host que `notas` dejó sin dueño: `NotesSession` (cola por mundo, registro `grabaciones/<partida>-notas.csv` con la primera orden, estado publicado cada fotograma) conectada en `DemoWorld::update_network`. Tests 744 → 766 (+22), Release en verde. Huella `0x66c9e3a86632750e`, reservas 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5 sin cambios. Sin validar a ojo. | `panel` |

## Hallazgos

| Fecha | Dónde | Hallazgo | Qué hacer |
| --- | --- | --- | --- |
| 2026-11-15 | `01` §5.3 | H del teclado choca con la ayuda (H). Con el lado del dial principal mantenido, H es la horquilla y no abre la ayuda; sin lado, H abre la ayuda | Probar con teclado |
| 2026-11-15 | `src/net` | `NotesState` solo lleva anclas: un peligro suelto (cresta, salto, cuidado) cuenta como «nota» del cambio de dirección al que cae (ventana [s_start − 20 m, s_end]) y el cursor se salta esa curva | Pregunta para Lucas; si molesta, `NotesState` tendría que llevar el tipo |
| 2026-11-15 | Registro | La «y» del dial secundario sale con forma «rueda» aunque la activa sea el contador | Al leer «¿rueda o contador?» en el registro, ignorar las filas de «y» del secundario |
| 2026-11-15 | Diseño | Soltar el gatillo (o D) con el contador sin tocar [−] ni [+] canta 50 m | Pregunta para Lucas |

## Ediciones propuestas

**`docs/02-arquitectura.md` §2, tras «Tablet (lote 6)»**, párrafo nuevo:

> **Diales del copiloto (lote 7).** Mando, ratón y teclado se reducen a un `DialsInput` por fotograma (`platform/copilot_dial_input`) y una sola máquina (`platform/copilot_dials`, sin raylib) decide qué dial está abierto, qué posición elige el vector del stick o del puntero (`platform/copilot_wheel`: zona muerta 0,50 al entrar y 0,40 al seguir, histéresis de 4°) y qué `game::NoteOrder` sale al soltar. El contador de metros no recibe la `s` del coche ni los cambios de dirección: el juego nunca rellena la distancia. Los diales ocupan dos capas junto a la tablet (`copilot_dials_layout`, `dials_draw`). El cursor (`tablet_cursor`) salta al siguiente cambio sin nota usando `net::NotesState`. Lado host: `platform/notes_host` (`NotesSession`) aplica las órdenes a la `NoteQueue`, las registra (`io::NotesLog`) y publica el estado; todo fuera del paso de simulación.

**`docs/07`:** nada.

**`docs/08` (bitácora), entrada nueva:** «Lote 7, `diales`. Una sola lógica para tres entradas: lo que cambia entre mando, ratón y teclado es cómo se produce el `DialsInput`. El teclado usa H para la horquilla y para la ayuda: se resuelve por modo (con el lado mantenido gana la nota). El cursor salta tras enviar sin esperar al estado del host y olvida el salto cuando llega una revisión nueva. Un peligro va a la última nota si el cursor no se ha movido a mano desde que se envió; si no, solo, en el cursor».

**`docs/teclas.md`:** regenerada (diez filas nuevas en «Tablet del copiloto»).

## Pide de lo compartido

Nada. `docs/01` y `05` sin cambios.
