# Tarea `diales` (lote 7, carril plataforma): los diales del copiloto

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-7.md` §2. Criterio de gusto: `docs/agentes/criterio-de-lucas.md` §1 y §5: los valores sencillos (colores, tamaños, tiempos, textos) los decides tú y los pones en el informe.

Sale con `notas` fusionada: usas su interfaz tal como haya quedado.

## Por qué

Con la tablet del lote 6, el copiloto ya ve la carretera y mueve el cursor. Le falta cantar: los diales de `docs/01` §5.3, con los que elige la nota y la manda al host. Es la mitad del criterio de la fase 3 («una persona canta mirando solo la tablet, primero por voz y después solo con los diales»).

## Qué hacer (en la ventana del copiloto, `rally.exe --copiloto`)

1. **Dial principal** (`docs/01` §5.3): 14 posiciones, del 6 arriba a la H abajo, a cada lado.
   - **Mando:** mantener R1 abre las curvas a la derecha y L1 las de la izquierda, con el lado en grande. Una rueda de 7 notas en el stick izquierdo. Soltar el hombro confirma la nota a la que apunta el stick; con el stick en el centro, cancela. Sin hombro, el stick no elige nada.
   - **Ratón** (con el puntero fuera, Espacio mantenido, como hoy): mantener el botón sobre el dial, apuntar a la nota y soltar. Un clic en la carretera sigue colocando el cursor: el dial tiene su zona en la pantalla.
   - **Teclado** (Lucas, 2026-11-12: mando, ratón y teclado): de partida, ← o → mantenida elige el lado, y 1–6 o H, la nota; al soltar la flecha sin número, cancela. Propón otra cosa si choca con las teclas de hoy.
2. **Dial secundario** (8 posiciones): cresta, salto, cuidado, se cierra, se abre, no cortes, larga, y. Mando: gatillo derecho mantenido y stick izquierdo; soltar confirma. Ratón y teclado: los propones tú (`docs/01` lo dejaba para la fase 3).
3. **Distancia, en dos formas conmutables con una tecla** (decisión de Lucas, 2026-11-12; `mensajes/lote-7.md` §2.3). La forma activa se ve en pantalla y se escribe en el registro de notas:
   - **Rueda** (`docs/01` §5.3): y, 30, 50, 80, 100, 150, 200, 300+, con el gatillo izquierdo mantenido y el stick izquierdo; con ratón, botón derecho mantenido.
   - **Contador:** un display con los metros y tres botones, [−], [cantar] y [+]. Parte de **50 m** y cambia de **10 en 10**, de 0 a 990; mantenido, repite. [cantar] pega esa distancia a la última nota y el contador vuelve a 50. **Nunca se rellena con la distancia real** (`docs/01` §5.3: estimar es parte de la habilidad). Con mando, de partida: gatillo izquierdo mantenido, cruceta izquierda y derecha para [−] y [+], y soltar el gatillo canta. Con ratón, se pulsan los botones. Con teclado, lo propones tú.
4. **Borrar** (`docs/01` §5.3): una pulsación borra la última nota enviada; mantenido 1 s, vacía las pendientes. Mando y teclado: los propones tú.
5. **El cursor de la tablet, completo** (`docs/01` §5.2): por defecto apunta al siguiente cambio de dirección **sin nota** por delante del coche; al enviar una nota, salta al siguiente. Qué anclas tienen nota lo dice el host (tarea `notas`).
6. **Lo que el copiloto ve de sus notas:** las notas no se dibujan en la tablet (`docs/05`, «Notas en la tablet»). Sí ve, fuera del mapa, la última orden enviada y cuántas notas hay pendientes, para saber que el dial ha hecho efecto.
7. **Orientación del mapa por defecto: «hacia delante»** (`docs/05`, 2026-11-11). `MapOrientation::Ahead` pasa a ser el valor inicial; la tecla O sigue alternando.
8. **Mirar con la cabeza se congela** mientras un dial está abierto, como con el puntero (`HeadLook::set_frozen`).

Fuera de esta tarea: el panel del conductor y el sonido (tarea `panel`), el copiloto bot (lote 8) y la pantalla táctil (fuera de la 1.0).

## Contratos (de `mensajes/lote-7.md` §2)

- No se tocan `src/game`, `src/net` ni `src/sim`. Si a la interfaz de `notas` le falta algo, es «Necesita consultor».
- Las teclas nuevas entran en la tabla del copiloto y en `docs/teclas.md`, que regeneras tú. Una tecla repetida falla en su test.
- Capturas de `tests/platform/golden/`: no cambia ninguna (la ventana del copiloto no tiene captura de referencia). Si añades una de los diales, es tuya.
- La lógica de cada dial (qué posición elige un ángulo del stick, qué pasa al soltar, la repetición del contador) va sin raylib, en `rally_loop`, para probarla sin ventana.

## Criterios (tests en `tests/platform/`, sin ventana ni mando; Release y Debug)

1. Dial principal: para cada una de las 14 posiciones, el ángulo del stick que la elige y la orden que sale; centro = cancela; sin hombro, nada. Con una zona muerta y una histéresis que propones tú, sin posiciones que parpadeen entre dos notas vecinas con el stick quieto en la frontera ± 2°.
2. Lo mismo para el secundario y para la rueda de distancias.
3. Contador: parte de 50; [+] y [−] de 10 en 10, con tope en 0 y 990; mantenido, repite; [cantar] manda los metros y vuelve a 50; nunca lee la `s` del coche ni los cambios de dirección (compruébalo por construcción: su lógica no los recibe).
4. Borrar: pulsación corta y mantenida de 1 s dan las dos órdenes distintas, y nunca las dos.
5. El cursor: salta al siguiente cambio sin nota al enviar; con todos con nota, se queda en el último.
6. Las mismas órdenes salen con mando, con ratón y con teclado (tres tablas de entrada, una sola lógica).
7. La cabeza se congela con un dial abierto y se suelta al cerrarlo.
8. Coste: «CPU tablet» de F1 con un dial abierto, frente a sin él, medido. Sin umbral.

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| R1, L1, los dos gatillos y el stick izquierdo están libres en la ventana del copiloto | `copilot_keys.cpp` y `demo_camera_keys.cpp`: hoy el copiloto usa la cruceta (cursor) y el stick derecho (mirar) | Propón el cambio en el plan, con la tabla de teclas completa |
| La cruceta izquierda y derecha están libres para el contador | Lo mismo: hoy solo se usan arriba y abajo | Otro par de botones, y dilo |
| El clic del ratón puede distinguir el dial de la carretera | `copilot_app.cpp`, el puntero del lote 6 | Zona fija del dial en la pantalla, fuera del mapa |

## Paradas

- A la interfaz de `notas` le falta algo que necesitas.
- Un test de otro carril falla.
- Para cumplir algo hay que cambiar `docs/01` §5.

Lo demás, avisos.

## Para el informe (la prueba de Lucas)

- La tabla de controles completa del copiloto (mando, ratón y teclado) y una captura con cada dial abierto.
- Los valores que has elegido (zona muerta, tamaños, colores, tiempos de repetición).
- Preguntas para Lucas: ¿se elige bien la nota con prisa? ¿Rueda o contador para la distancia? ¿Qué mando, ratón o tecla estorba?
- Fragmento `docs/progreso/diales.md`: la edición de `docs/02` (copiloto) y de `docs/teclas.md`.
