# Tarea `panel` (lote 7, carril plataforma): panel del salpicadero, pitidos y la tecla de la irregularidad

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-7.md` §2. Criterio de gusto: `docs/agentes/criterio-de-lucas.md` §1, §3 y §5: los valores sencillos los decides tú y los pones en el informe.

Sale con `notas` y `diales` fusionadas. El apartado 5 necesita además `irregularidad` fusionada: si no lo está cuando termines lo demás, entrega sin él y dilo (el director lo lanza después como arreglo).

## Por qué

Es la otra mitad de las notas: lo que ve y oye el conductor (`docs/01` §5.4). Sin el panel, las notas del copiloto no llegan a nadie.

## Qué hacer (en el juego, en el modo tramo)

1. **Panel del salpicadero** (`docs/01` §5.4): una fila de tres huecos, **curva · distancia · curva**.
   - Izquierdo: la nota siguiente (número o H, sentido, modificadores e iconos de peligro). Central: su distancia («y», metros o vacío). Derecho: la nota de después.
   - Cuando el coche pasa el ancla de la nota izquierda, la fila corre. Si el coche se recoloca por detrás, la nota vuelve.
   - Un peligro sin curva ocupa un hueco de nota, con su icono.
   - Una nota borrada se tacha medio segundo.
   - **Accesibilidad** (`docs/01` §5.4): el color nunca va solo. Cada nota lleva su número o letra, y la flecha está más curvada cuanto más cerrada es la curva. Los colores de la escala, los de `docs/01` §5.1.
   - **Dónde:** en la cabina, a la izquierda del volante, entre la puerta y el volante (Lucas, 2026-11-12). Es geometría del interior, como la pantalla de la marcha (`cockpit.h`, `append_display`), no una capa de la pantalla. Se tiene que leer con el asiento en cualquier posición de su ajuste y con la cabeza al frente.
   - Lo dibuja el host con la cola de notas del host (tarea `notas`). El copiloto no lo ve.
2. **Pitido por lado** (`docs/01` §5.4; Lucas: «pitidos, de momento»): al entrar una nota en el hueco izquierdo, un pitido corto por el canal de su lado; un peligro solo, por los dos; una nota borrada, otro tono. Sintético, por el mismo camino que el sonido de hoy (`AudioSnapshot`, `car_synth`), sin archivos de sonido. En el modo a ciegas (sin sonido de superficie) los pitidos siguen sonando.
3. **Las notas no cambian nada de la conducción ni de la simulación.**
4. **Vibración del mando:** a Lucas le parece demasiada. Baja la intensidad por defecto con una sola ganancia sobre lo que ya se calcula (de partida, ×0,6; el valor lo decides tú). No es un ajuste por jugador todavía: eso es de la 2.10.
5. **Tecla de la irregularidad** (`docs/04`, fase 3: «con una tecla para quitarla y comparar»): en el modo tramo, una tecla reconstruye el mundo con el mismo tramo sin irregularidad, y otra pulsación, con ella. Usa la función de `sim` que deja la tarea `irregularidad`. El coche vuelve a la salida, como al cambiar de modo con K. En pantalla se ve cuál de los dos se está jugando, y la grabación guarda el texto del tramo que se usó. En un tramo sin irregularidad, la tecla dice que no hay nada que quitar.
6. **El tramo por defecto del modo tramo pasa a ser `data/stages/lazadas_irregular.stage`** si existe; `--tramo` sigue mandando.

Fuera de esta tarea: el sonido rico de las notas y la voz (fase 6 y posterior), el copiloto bot (lote 8) y el resumen del tramo (fase 5).

## Contratos (de `mensajes/lote-7.md` §2)

- No se tocan `src/game`, `src/net` ni `src/sim`.
- Teclas nuevas, en su tabla y en `docs/teclas.md`.
- Capturas de `tests/platform/golden/`: las del interior del coche cambian **solo si** el panel vacío se ve en ellas. Dilo en el plan, con cuáles, antes de regenerar ninguna. Las de la zona con la cámara fuera del coche no cambian.
- La lógica del panel (qué va en cada hueco para una cola y una `s`, cuándo corre, cuándo se tacha, cuándo pita y por qué lado) va sin raylib, en `rally_loop`.

## Criterios (tests en `tests/platform/`, sin ventana; Release y Debug)

1. Los tres huecos para una cola de test, con el coche avanzando, saltando hacia delante y recolocándose por detrás.
2. Un pitido por nota y por su lado, exactamente uno, aunque el fotograma llegue tarde o lleguen dos notas en el mismo fotograma; un peligro solo, por los dos lados; el tono de borrado.
3. Una nota borrada se ve tachada 0,5 s (± un fotograma a 30 fps) y desaparece.
4. El panel se lee: el rectángulo del panel, proyectado desde el ojo nominal y desde los extremos del ajuste del asiento, cae entero dentro de la pantalla a 16:9, no lo tapa el volante recto y cada hueco mide al menos lo que tú fijes para que el número se lea a 1280×720 (pon la cifra en píxeles y cómo la has elegido).
5. La vibración: la salida con la ganancia nueva es la de antes × la ganancia, para las entradas de los tests de hoy.
6. La tecla de la irregularidad: el mundo que se construye tiene el hash del suelo del tramo sin irregularidad, y el de vuelta, el del tramo con ella; la descripción del mundo que se graba lleva el texto usado.
7. Coste: CPU del dibujo con el panel, frente a sin él, y el del audio con un pitido. Sin umbral: se informa.

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| A la izquierda del volante hay sitio visible en el 205 para una fila de tres huecos | `project_from_eye` con las medidas de `[cockpit]` del coche, a 16:9 y 4:3 | Propón el sitio más cercano que se lea (encima del salpicadero, a la izquierda) y enséñalo en una captura: lo decide Lucas en la prueba |
| El audio admite un sonido corto con canal propio sin tocar el motor ni la rodadura | `audio_snapshot.h` y `car_synth.h`: hoy hay sucesos por contador (cambios de marcha, soplado, impactos) | Dilo en el plan con lo que haya que añadir |
| La función de `sim` para quitar la irregularidad existe y es pura | La rama de `irregularidad` fusionada | Entrega sin el apartado 5 |

## Paradas

- A la interfaz de `notas` o de `irregularidad` le falta algo que necesitas.
- Un test de otro carril falla.
- Para cumplir algo hay que cambiar `docs/01` §5.

Lo demás, avisos.

## Para el informe (la prueba de Lucas)

- Capturas del panel con una nota, con una nota y distancia, con un peligro y con una tachada.
- Los valores elegidos (medidas, colores, tonos y duración de los pitidos, ganancia de la vibración).
- Preguntas para Lucas: ¿se lee el panel sin apartar la vista de la carretera? ¿Está bien a la izquierda del volante? ¿Se distingue el lado por el pitido? ¿La vibración ya está bien? Con la tecla de la irregularidad, ¿el tramo se siente una carretera real?
- Fragmento `docs/progreso/panel.md`: las ediciones de `docs/02` y de `docs/teclas.md`.
