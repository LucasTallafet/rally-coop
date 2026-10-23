## Fila del registro

| 2026-10-24 | 3 | | Arreglo `f3-panel` (lote 3, carril plataforma): el panel de depuración (F3) se ajusta a lo que dibuja, a petición de Lucas tras la prueba. **Alto** = (líneas dibujadas + título) × línea + 2 × relleno, no el de `kDebugSamples`; **ancho** = el de la línea más ancha que se dibuja (con la fila de las barras de los ejes y el título) + relleno, ya sin llegar al panel T; histéresis de presentación `WidthHold`: crece al instante y baja como mucho una vez por segundo (el tiempo entra como argumento, `GetTime()` en el juego y el `now_s` del copiloto en su ventana). **Páginas** si no cabe en el área útil (hasta los mensajes y, si el anillo de acciones cae bajo su ancho, hasta encima del anillo): F3 → página 1 → 2 → oculto, por bloques enteros (red, mando, entradas, grabación, coche, zona, cámara, ritmo; copiloto: conexión, mundo, vista), título «DEPURACIÓN (F3 1/2)». **Fondo** de F3 con la mitad de opacidad (alfa 215 → 107; H y demás paneles igual). Mismo trato en el F3 del copiloto. Nuevo `debug_panel.{h,cpp}` (`DebugText`, `DebugPanel`, compartido por juego y copiloto); `screen_layout`: `plan_debug`, `debug_panel_rect`, `debug_title`, `WidthHold`; el rectángulo de la capa `Debug` es ahora el área máxima y el del panel dibujado se guarda aparte en `Overlay::set_debug_rect` (se borra en `begin()`, no toca el diseño). Tests 495 → 502 en Release (con formato) y Debug: alto exacto con 10/24/40 líneas, 2 páginas con 40 líneas a 1280×720 y 1360×768 (1 a 1080; 24, 27 y 29 caben en 1 a 1360×768), bloques enteros y todas las líneas una vez, nada fuera del área ni de la pantalla ni solapado a 640×360 (copiloto), 1280×720, 1360×768 y 1920×1080, ancho con la fila de barras, título, regla del anillo y `WidthHold`. Capturas regeneradas con `golden.ps1 -Regenerar`: solo cambia `interior_f3_t.png` (fondo más transparente, panel a la medida y sin aviso rojo). Huella `0x66c9e3a86632750e`, reservas de Jolt 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5 sin cambios. Pendiente de la prueba de Lucas. | prueba del lote 3 |

## Hallazgos

| Fecha | Hallazgo |
| --- | --- |
| 2026-10-24 | El criterio decía que con 40 líneas a 1360×768 salen 2 páginas: sale así porque el área de F3 llega hasta encima del anillo de acciones (capacidad 36 líneas con título). A 1920×1080 el anillo no cae bajo el área (el panel es estrecho), el área llega hasta los mensajes y 40 líneas caben en una página; el test lo recoge. |
| 2026-10-24 | A 1920×1080 las líneas «motor:» y «ejes:» (~90 caracteres) son más anchas que el área, así que el ancho seguirá siendo el máximo; donde estrecha de verdad es a 1360×768 (unos 470 px frente a 649). |
| 2026-10-24 | En 1280×720 (captura de referencia) F3 cabe entero en una página. La línea de red del copiloto conectado (juego) va tras el título, como antes. |
| 2026-10-24 | La comprobación de base (antes de cambiar nada) se lanzó a la vez que empezaba a editar y falló al compilar: esa ejecución compiló ya mis cambios a medias; no es un fallo de la base (con los cambios acabados pasa 502/502). |

## Ediciones propuestas

- `docs/02` (plataforma, texto en pantalla): «F3 es un panel a la medida: su alto y su ancho salen de las líneas que se dibujan en ese fotograma (`plan_debug`, `debug_panel_rect`); si no caben en el área (hasta los mensajes o el anillo de acciones) se reparten en páginas por bloques enteros (F3: 1, 2 y oculto). El ancho solo baja una vez por segundo (`WidthHold`, histéresis de presentación). El rectángulo de la capa `Debug` es el área máxima; el dibujado se fija por fotograma en `Overlay::set_debug_rect`.»
- `docs/08` (bitácora): por qué el ancho del panel tiene histéresis (las líneas cambian de ancho cada fotograma: pasos, ms, ojo) y por qué el límite de abajo depende de si el anillo cae bajo el área.

## Pide de lo compartido

- `docs/teclas.md` y `demo_screen_keys.cpp` (texto de F3): el consultor puede añadir «; por páginas si no cabe (F3: página siguiente, y oculta)». No lo he tocado: `docs/teclas.md` es generado y el texto de la tecla cambiaría la tabla.
