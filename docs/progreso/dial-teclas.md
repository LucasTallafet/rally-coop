Fragmento de `dial-teclas` (lote 7, arreglo, carril plataforma). Rama `lote-7/dial-teclas`.

## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-16 | 3 · lote 7 `dial-teclas` | | Arreglo de las teclas de los diales del copiloto (revisión de controles, P1, P2, P5 y P9). El test de choques ya ve la ventana del copiloto: `kCopilotKeyContext = kContextCopilot` y las seis filas del juego que esa ventana lee (Tab, F3, F1, H, F11 y Esc; Mirar ya la tenía) llevan `kContextCopilot`. La horquilla pasa al 7 con el principal abierto y H queda solo para la ayuda (se quita `CopilotAction::PickHairpin`); H no hace nada con un dial abierto. Con un dial abierto el cursor no se mueve con ↑/↓ ni con la cruceta, y una dirección que se mantenía al abrirse no cuenta al cerrarse hasta soltarla (`CursorHoldGate`). Una flecha ya pulsada al cerrar S, D o un dial del mando no abre el principal hasta soltarla y volver a pulsarla (`KeyDialFilter`). Tests: ver el informe. Huella `0x66c9e3a86632750e`, reservas 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5 sin cambios. Sin validar a ojo. | Prueba de Lucas |

## Hallazgos

| Fecha | Dónde | Hallazgo | Qué hacer |
| --- | --- | --- | --- |
| 2026-11-16 | Criterio 3 del prompt | «Abrir la ayuda ya no cancela el dial» no es posible: con la ayuda abierta los diales están tapados y `update_dials` los cancela a propósito. Lo que cambia es que H ya no abre la ayuda con un dial abierto (revisión del plan: `!dials_.open()`), así que H nunca cancela un dial | Ninguna |
| 2026-11-16 | Teclado, diales | Soltar ← con S pulsada abre el secundario en el acto, y ← con → a la vez confirma el principal (`main_left = left && !right`) | Lote 8 si molesta |
| 2026-11-16 | Teclado | Esc no se lee con contexto (la tecla de salida es la de raylib); lleva `kContextCopilot` por el test de choques y porque la ayuda del copiloto lo exige | Ninguna |

## Ediciones propuestas

**`docs/02`:** nada.

**`docs/07`:** nada (`docs/teclas.md` regenerada).

**`docs/08` (bitácora), tras la entrada de `diales`:** «Lote 7, `dial-teclas`. El test de choques no veía la ventana del copiloto porque las filas del juego no llevaban su contexto: un choque real (H de la ayuda frente a H de la horquilla) pasó los tests. Ahora las seis filas que la ventana lee llevan `kContextCopilot` y la ventana se lee solo con él. La horquilla pasó al 7 y H quedó solo para la ayuda (decisión de Lucas, 2026-11-16): con el 1–6 y el 7 la mano no cambia de fila. Una tecla «tomada» hasta soltarla resuelve dos casos con el mismo patrón: la flecha que estaba pulsada al cerrar otro dial y la dirección del cursor que estaba pulsada con un dial abierto». Y en la entrada de `diales` (l. 148), donde dice «El teclado usa H para la horquilla y para la ayuda … Se resuelve por modo», sustituir por «La horquilla es el 7 (antes H, que chocaba con la ayuda)».

**`docs/teclas.md`:** regenerada (una fila menos en «Tablet del copiloto»; textos de las filas «Dial principal» y «Elegir 7 / 8»).

## Pide de lo compartido

Nada.
