# Tarea `copiloto-ventana` (lote 3, oleada 2, carril plataforma): host en el juego y ventana del copiloto

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-3.md` §2.5–§2.8, que mandan sobre este prompt. Usas `rally_net` (`src/net`, ya fusionada) **tal como está**. Si le falta algo, para y dilo: es del carril de juego.

## Por qué

El criterio de la fase 3 empieza por «una persona canta mirando solo la tablet y otra termina el tramo» (`04`). Antes de la tablet hace falta la segunda instancia: el copiloto ve el coche del conductor en su propio PC, interpolado, sin simular. En la fase 3 es el mismo PC, por `127.0.0.1`. Esta tarea monta el host en el juego y la ventana del copiloto con lo que ya se dibuja.

## CMake

En `src/platform/CMakeLists.txt` (tuyo), `rally_platform` enlaza `rally_net` **sin `if(TARGET …)`**. `src/CMakeLists.txt` añade `net` después de `platform`, así que un `if(TARGET rally_net)` dentro de la plataforma daría falso. Enlazar por nombre un target que se define después es válido en CMake.

## Parte H · el juego hace de host

- **Al arrancar** (no en `--golden` ni en los `--bench-*`), `net::Host::start(net::kDefaultPort)`.
  - Si falla (puerto ocupado: otra instancia ya hace de host), mensaje «red: puerto 29470 ocupado; sin copiloto», y el juego sigue igual.
- **En cada `build()` del mundo** (todas las teclas que reconstruyen, Retroceso, F5 y el arranque): `set_world(io::serialize_world(desc), texto del .car, world.zone_hash())`. Busca el sitio único en `demo_world`, donde ya se construye y empieza la grabación.
- **Tras los pasos de cada fotograma:** `after_steps(world, pasos desde el build, recolocado)`.
  - Los pasos desde el build son los de la grabación en curso (`Recorder::steps()`) o un contador equivalente que vuelve a 0 en cada `build()`. Dilo en el plan.
  - «Recolocado» es verdadero si en esos pasos entró el botón de recolocar (`kButtonReset` pulsado).
- **`service()`,** una vez por fotograma.
- **Mensajes** «red: copiloto conectado» y «red: copiloto desconectado».
- **Con un copiloto conectado,** F3 añade una línea de red (RTT, estados enviados) y F1 la CPU de la red por fotograma (`service` más el envío). **Sin copiloto, la pantalla es la de hoy.**
- **No cambia la simulación:** la red solo lee. Una grabación hecha con un copiloto conectado da «hash OK» en `rally_replay run`.

## Parte C · `rally.exe --copiloto [dirección[:puerto]]`

Por defecto, `127.0.0.1:29470`.

- **No simula, no graba y no escribe `ajustes.txt`.** Lo lee, por la pantalla, pero es la misma carpeta que la del host. Tampoco suena: sin `AudioOutput`, o en silencio.
- **Ventana de 640 × 360,** siempre encima (`FLAG_WINDOW_TOPMOST`) y en la esquina inferior derecha del monitor, para que el conductor la vea junto a la suya en un solo monitor de 1360 × 768. F11 la pasa a pantalla completa sin bordes, como en el juego.
- **Al recibir un mundo** (`take_world()`):
  - lo construye con el mismo código que el host: `io::parse_world` + `parse_car` (con las claves `Full`) + `SimWorld::build` + `seal`. **Nunca llama a `step`.**
  - comprueba `zone_hash()` contra el recibido. Si no coincide, mensaje «la zona no coincide con la del conductor; se dibuja la recibida», como manda `02` §7, y sigue.
- **Dibuja la zona (o el recinto) y el coche en la pose de `buffer().sample(ahora)`,** con lo que ya existe: el dibujo de la zona y el del coche de la cámara de persecución. La interpolación de pose de la plataforma no hace falta: la pose ya llega interpolada.
  - Cámara de persecución por defecto. Tab: persecución / toda la zona (la cámara que ya existe).
- **Teclas del copiloto:**
  - **Z** (red simulada: no / media / mala, `lote-3.md` §2.7);
  - **Tab, F1, F3, H, F11 y Esc.**
  - Las demás no hacen nada.
  - Z no se repite con ninguna tecla de ningún contexto (test de teclas). La ayuda H del copiloto solo enseña las suyas. `docs/teclas.md` coincide con la tabla generada.
- **F3 del copiloto:** estado (`status()`), RTT, estados recibidos y perdidos, retardo efectivo, si extrapola, paso del host, generación del mundo y red simulada.
- **Sin conexión** se ve el último mundo, o fondo con «sin conexión, reintentando», y reintenta cada segundo. Al volver el host, se reconecta solo.
- **Capas de la pantalla del copiloto** sin solaparse a 640 × 360 y a 1920 × 1080. Test de `screen_layout`; si añades capas, al estilo de las que hay.

## Criterios

1. **Tests** (`tests/platform/`):
   - lectura de los argumentos: `--copiloto`, `--copiloto 127.0.0.1`, `--copiloto 127.0.0.1:29471` y uno mal escrito con su error;
   - el modo copiloto no escribe `ajustes.txt` (la lógica de guardar, con la instancia en modo copiloto);
   - capas a 640 × 360 y a 1920 × 1080;
   - teclas.
2. **Medido a mano** en este PC (i3-8100, RX 580) por el carril, con dos instancias. Todas las cifras van en el informe:
   - **Host en ventana a 60 Hz, con el copiloto conectado y sin él, 30 s cada uno por la carretera:** CPU de simulación por paso, CPU de render y CPU de red por fotograma con F1. La de red, ≤ 0,05 ms de media; la simulación y el render, dentro del ruido (± 25 %, `progreso.md`, P1a).
   - **Copiloto:** a 60 fps estables, y el coche sin saltos visibles a ojo con la red simulada en «no» y en «media».
3. **Repetición:** una grabación hecha con un copiloto conectado da «hash OK».
4. **Capturas de referencia:** al final, `tools/ci/golden.ps1 -Regenerar`, porque la ayuda cambió en el lote 1 y en `abs-sonido`. Di cuáles cambian. Solo debería cambiar `interior_ayuda.png`; si cambia otra, explica por qué antes de guardarla.

**Sin cambios:** nada de `src/sim`, `src/io`, `src/game` ni `src/net`. La huella, las reservas, la telemetría, el CSV y el `.rrec`, iguales.

## Para el informe y la prueba

**Lo que prueba Lucas** (`lote-3.md` §4.3–§4.5):
- el juego en ventana (F11) y, en otra consola de la misma carpeta, `rally.exe --copiloto`;
- 2 minutos por la zona y la carretera, recolocando (R), cambiando el ABS (1) y la zona (K);
- Z en el copiloto: «media» y «mala»;
- cerrar y volver a abrir el copiloto;
- F1 con el copiloto y sin él.

**Preguntas:**
1. ¿El coche del copiloto se mueve suave y sigue al tuyo?
2. ¿Salta limpio al recolocar y cambia de mundo con el tuyo?
3. ¿Notas algo en tu ventana con el copiloto abierto?
4. ¿El tamaño y el sitio de la ventana del copiloto te sirven para la tablet que viene?
