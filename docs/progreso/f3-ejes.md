# Fragmento: f3-ejes (lote 5, plataforma)

## Fila del registro
| Tarea | Qué se hizo | Tests | Huella y reservas | Validado a ojo | Horas |
| --- | --- | --- | --- | --- | --- |
| lote 5 · f3-ejes | La línea «ejes» de F3 se parte en «ejes: del. / tras. N·m; central dw … rad/s, visco … N·m» (`debug_axles_line`) y «autobl.: … / … N·m; LCP NN[ SIN CONVERGER]» (`debug_lsd_line`), en el bloque «coche» (de 3 a 4 líneas). Cambia solo `interior_f3_t.png`. | 606/606 en Release y 603/603 en Debug (3 tests de `debug_lines`: ancho constante de las tres líneas con fuentes 10 y 20, campos, peor caso en 1280×720, 1360×768 y 1920×1080 dentro del contenido de F3 y en la fuente); `golden.ps1` sin `-Regenerar`: 5 capturas iguales | Sin cambios: 0x66c9e3a86632750e, 1990, kSimVersion 5 | Sin validar a ojo (Lucas, F3 a 1280×720) | |

## Hallazgos
| Hallazgo | Detalle |
| --- | --- |
| «Δω» no está en la fuente | U+0394/U+03C9 caen fuera de 32..255 y se dibujaban como «??». Ahora «dw» (desviación del prompt, que decía «central Δω»). Una etiqueta más larga no cabe a 1920×1080 (690 de 694 px). El test comprueba `in_default_font` de las tres líneas. |
| La línea «motor» también se corta | Peor caso 566 px frente a 563 de contenido a 1280×720 y 1132 frente a 694 a 1920×1080. Con todos los pares positivos y sin ningún «1» sale cortada por 3 px; en la captura cabe. No es de esta tarea: queda sin tocar. Decisión para Lucas: partir también «motor» (~0,5 h, cambia otra vez `interior_f3_t.png`) o dejarla. |
| Criterio 3 sin test nuevo | `DebugText` incluye raylib y no se enlaza en `rally_tests_platform`; vale la construcción (la línea nueva va sin `text.block()` entre medias) y los tests de `f3-panel` existentes. |

## Anchos (peor caso, px; contenido del F3 entre paréntesis)
| Línea | 1280×720 (fuente 10) | 1920×1080 (fuente 20) |
| --- | --- | --- |
| «ejes: +9999 / +9999 N·m; central dw +999.99 rad/s, visco +9999 N·m» | 345 (563) | 690 (694) |
| «autobl.: 9999 / 9999 N·m; LCP 99 SIN CONVERGER» | 254 (563) | 508 (694) |
| «ejes» antigua, en una línea | 606 (563) | 1212 (694) |

## Ediciones propuestas
- `docs/08`, bitácora: «f3-ejes: la línea de ejes de F3 se parte en dos (ejes / autoblocantes y LCP) para que quepa a 1280×720; «Δω» pasa a «dw» porque la fuente no lleva letras griegas.»

## Lo compartido
Nada.
