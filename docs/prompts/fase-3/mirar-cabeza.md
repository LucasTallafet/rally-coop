# Tarea `mirar-cabeza` (lote 4, carril plataforma): mirar con la cabeza, conductor y copiloto

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-4.md` §2.1, §2.7 y §2.8, que mandan sobre este prompt. Sale de `integracion` con `f3-ancho` ya fusionada.

## Por qué

Decisión de Lucas del 2026-10-25 (`05`, Cambios): los dos jugadores giran la cabeza sin cambiar el campo de visión. El conductor mira hacia el vértice de una horquilla o por la ventanilla; el copiloto, al salpicadero y a su ventanilla. El stick derecho queda para mirar, y por eso los diales del lote 6 cambian de sitio (`01` §5.3).

Sustituye a la tecla B del copiloto, que nunca funcionó. La causa (`lote-4.md` §7): `action_down` mira las teclas en `kContextAll`, que no incluye `kContextCopilot`.

## Qué hacer

1. **`platform/head_look.*`** (sin raylib, en `rally_loop` o donde estén las piezas puras de la plataforma): estado y reglas de §2.7.
   - Entradas por fotograma: el stick ya con forma, el desplazamiento del ratón en píxeles, si se pulsó el botón central y el tiempo del fotograma.
   - Salidas: la guiñada (> 0 a la izquierda, como hoy en `copilot_view`) y el cabeceo (> 0 arriba).
   - `set_frozen(bool)`.
   - Una función pura para el ojo: el desplazamiento del ojo por el giro del cuello, con el pivote 0,10 m detrás, en ejes del coche.
2. **Mando:** `PadReading` y `PadState` ganan el stick derecho. Mover el stick derecho cuenta como usar el mando (mando activo). Zona muerta radial de 0,15, en `input_shaping`, con su test.
3. **Conductor** (`DriverInput`, `DemoCamera`):
   - stick derecho siempre;
   - ratón solo con la dirección por teclado (F7). En ese modo el cursor se captura como con la dirección por ratón (se suelta en pausa y sin foco) y el botón central devuelve la cabeza al frente;
   - con la dirección por ratón, el ratón sigue siendo el volante y no mira;
   - la mirada se suma al asiento y al movimiento de la cabeza de F9: primero el giro del cuello, después lo de F9.
4. **Copiloto** (`copilot_app`, `copilot_view`, `DemoCamera`): lee el mando (solo el stick derecho) y el ratón, con su ventana con el foco. El ratón captura el cursor con el foco y lo suelta al perderlo.
5. **Solo con el foco**, en las dos instancias (§2.7). Conducir no cambia.
6. **Quita la tecla B:** `CopilotAction::LookAtDash`, `look_step`, `look_yaw`, `kLookDashYaw`, `kLookSeconds` y sus tests. B sigue siendo el bosque de medida en el recinto.
7. **Teclas:**
   - `PadControl` gana el stick derecho;
   - una fila nueva «Mirar con la cabeza» en el bloque de cámara y asiento, en el juego y en el copiloto, con el stick y el ratón en su detalle (§2.8). La ayuda del copiloto la enseña en lugar de B;
   - `docs/teclas.md`, regenerado.
8. **Arreglo de raíz de B** (`lote-4.md` §7): `action_pressed`, `action_pressed_repeat`, `action_down` y `action_down_or_pressed` reciben siempre el contexto, sin valor por defecto, y cada llamada pasa el suyo. La comprobación «¿vale esta acción en este contexto?» sale a una función pura con su test.
9. **Documentos:** en tu fragmento, para el integrador, el párrafo «Cámara del asiento (lote 3)» de `02` §7 sin B y con la mirada, y lo que cambie en `07` §8.
10. **Capturas:** al final, `tools/ci/golden.ps1 -Regenerar`, y el informe dice cuáles cambian (la ayuda, por la fila nueva).

Reutiliza `input_shaping` (zona muerta y forma), `PadTracker`, la captura del cursor de `DriverInput` (`set_mouse`), `camera_space` (`attach`, `rotate`), `head_motion`, `copilot_eye` y las tablas de teclas. Si escribes una utilidad nueva, el plan dice qué buscaste y por qué no sirve lo que hay.

## Criterios (tests en `tests/platform/`, Release y Debug)

1. **Stick en posición:**
   - dentro de la zona muerta radial (0,15), 0;
   - a fondo, ±90° de guiñada, +15° y −25° de cabeceo;
   - lineal entre la zona muerta y el tope, en cada eje;
   - en diagonal, cada eje según su componente, con los mismos topes.
2. **Seguimiento τ = 0,05 s, exacto:** con el stick a fondo y soltándolo, a 30 y a 60 fps, los ángulos en los mismos instantes coinciden (≤ 1·10⁻⁹ rad). De 90° al soltar, por debajo de 1° en ≤ 0,25 s (cuenta: 0,05 · ln 90 = 0,225 s).
3. **Ratón:** 0,15° por píxel, con los mismos topes; el botón central vuelve al frente con el mismo seguimiento; con la dirección por ratón, el ratón del conductor no mira.
4. **Stick y ratón a la vez:** con el stick fuera de su zona muerta manda el stick; al soltarlo, vuelve a los ángulos del ratón.
5. **Congelar:** mientras está congelada, ni stick ni ratón cambian los ángulos (bit a bit); al descongelar, sigue desde donde estaba.
6. **Cuello:** con 90° a la izquierda, el ojo se desplaza 0,10 m a la izquierda y 0,10 m hacia atrás (≤ 1·10⁻⁹ m); a 0°, nada.
7. **Contextos de teclas:** cada acción de cada tabla vale en sus contextos y en ninguno más. En particular, las del copiloto en `kCopilotKeyContext` y no en el juego. Con la función pura del punto 8 de «Qué hacer».
8. **Mando activo:** mover solo el stick derecho hace activo ese mando (con dos mandos simulados).
9. **Sin reservas** por fotograma en `HeadLook` (contador de `operator new`).
10. **Teclas:** `docs/teclas.md` coincide con la tabla generada y ninguna tecla se repite en contextos que se cruzan (los dos tests de siempre). La ayuda del copiloto ya no tiene B.

**Sin cambios:** la simulación, las grabaciones, el campo de visión, el protocolo de red y `ajustes.txt`.

## Para el informe

- Lo que Lucas prueba (`lote-4.md` §4, puntos 1–3), con lo que hay que mirar y las preguntas 1–3. No hace falta A/B: con el stick quieto y sin ratón, la cámara es la de antes.
- Si capturar el cursor con la dirección por teclado molesta al pasar a la ventana del copiloto en el mismo PC, dilo: se suelta sin foco, y Alt+Tab basta.
- Las capturas de referencia que cambian.
