## Fila del registro

| Lote 6 · `tablet` | La tablet del copiloto (`rally.exe --copiloto`): mapa cenital, perfil de altura, cinta de ruta y cursor, con la `s` y la `t` del host (`SampledState`), sin notas. `--ventana <ancho>x<alto>` (de 640×360 a 3840×2160; solo un argumento, no se guarda). Panel centrado abajo (60 % × 55 %; `Layer::Tablet`), siempre a la vista (gancho `tablet_visible_`). Mapa: orientación A (tangente del eje) y B (punto del eje 40 m por delante, tecla O) con rumbo suavizado (τ 0,08 s), flecha fija al 70 % del alto, zoom de 100/200/400 m con histéresis del 10 % y transición de ~0,5 s, marcas cada 50 m, escala, línea por superficie con color y trazo (asfalto continuo, grava rayas, tierra puntos, arena raya-punto, nieve doble línea); el eje se muestrea cada 0,5 m al cargar (`TabletRoad`) y se dibuja todo lo que cae en el rectángulo. Cursor (flechas ↑/↓ o cruceta: pulsar salta, mantener más de 0,3 s desliza de 20 a 200 m/s en 2 s) y puntero (Espacio mantenido: se suelta el ratón, `HeadLook::set_frozen`, clic coloca el cursor). F1 del copiloto: fila «CPU tablet». Geometría en `rally_loop` sin raylib (`tablet_view`, `tablet_geometry`, `tablet_cursor`, `pointer_mode`); el dibujo, una capa fina (`tablet_draw`). Tests 679 → 704 (+25, `tests/platform/test_tablet.cpp`, `test_copilot.cpp`, `test_screen_layout.cpp`). Huella `0x66c9e3a86632750e`, reservas Jolt 1990, telemetría `0xfe3c612ddcaa6805`, `kSimVersion` 5: sin cambios. Geometría de la tablet en Release: media ~0,1 ms, p99 ~0,3 ms por fotograma a 60 y a 120 km/h en `lazadas`. Validado a ojo por Lucas: pendiente (prueba del lote). | | |

## Hallazgos

| Hallazgo | Dónde | Qué |
| --- | --- | --- |
| F1 no cabe sin solapar con una tablet útil | `screen_layout` | A 640×360 F1 mide ~270 px de alto (el 75 % de la pantalla) y a 1080, ~640: ninguna tablet de tamaño útil se le escapa. `layers_overdraw(Tablet, Perf)`: F1 se dibuja encima, con su fondo. F3 acaba encima de la tablet (a 640×360 pasa a 2 páginas), H es exclusiva con ella y los mensajes quedan debajo. Pregunta a Lucas: (a) F1 encima, como está; (b) un F1 compacto en el copiloto (≤ 9 filas, sin gráfica, ~1 h). |
| El cursor tarda 2,3 s en llegar a 200 m/s | `tablet_cursor` | Umbral de 0,3 s para deslizar + 2 s de rampa (el contrato dice 2 s). Pulsar salta al instante. |
| El mapa solo dibujaba una ventana de `s` | revisión del plan | Corregido: el eje entero se muestrea cada 0,5 m al cargar y cada fotograma se transforman solo los bloques cuyo círculo toca la vista (también brazos de al lado y de detrás, y hasta el final de la prolongación). |
| Rayas que resbalan al cambiar el zoom | revisión del plan | Corregido: la fase del trazo se ancla al ppm nominal del nivel de destino. |
| `--ventana` sin `--copiloto` | `copilot_args` | Es un error («solo vale con --copiloto»). El prefijo del mensaje de `main` pasa de «--copiloto:» a «argumentos del copiloto:». |

## Ediciones propuestas

**`docs/07` §8 (referencia rápida de la demo actual), opciones de arranque:** añadir «`rally.exe --copiloto [dirección[:puerto]] [--ventana <ancho>x<alto>]`: la ventana del copiloto; por defecto 640×360, de 640×360 a 3840×2160; solo es un argumento (no se guarda en `ajustes.txt`). La tablet (mapa, perfil, cinta y cursor) se dibuja abajo y centrada: O cambia la orientación del mapa, ↑/↓ o la cruceta mueven el cursor y Espacio mantenido con el clic lo coloca en el mapa. F1 enseña «CPU tablet».»

**`docs/02` (platform, ventana del copiloto):** añadir un párrafo «Tablet (lote 6)»: «La tablet lee solo `SampledState::s`, `t` y `on_road` del host y el `StageTrack` recibido; nunca una proyección propia. La geometría (rumbo suavizado, zoom de 3 niveles con histéresis, polilíneas por superficie sobre una tabla del eje cada 0,5 m, perfil, cinta y cursor) está en `rally_loop` sin raylib (`platform/tablet_view`, `tablet_geometry`, `tablet_cursor`, `pointer_mode`) y `platform/tablet_draw` la pinta.»

**`docs/08`:** (1) la orientación B de `01` §4 (30–50 m) queda en 40 m y la A es la de partida; se decide con la prueba del lote 6. (2) F1 no cabe junto a una tablet útil a 640×360: dibujo por encima o F1 compacto en el copiloto. (3) El filtro del rumbo (τ 0,08 s) retrasa ~6° en una horquilla de R 6 al límite de adherencia; a probar.

## Pide de lo compartido

Nada. Para `docs/05`: la fila «Tablet» sigue provisional; no propongo cambio hasta la prueba.
