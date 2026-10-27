# Fragmento `f3-ancho` (lote 4, carril plataforma)

## Fila del registro

| 2026-10-27 | 3 | | `f3-ancho` (lote 4, carril plataforma): el panel F3 mide cada línea con las cifras cambiadas por «8» (`debug_text_width`; se dibuja el texto real) y tiene un ancho mínimo de 56 caracteres «8» de la fuente del momento (`kDebugMinChars`, `debug_min_content_width`; recortado al área si no cabe, como en el copiloto a 640x360). 335 px de contenido a 1360x768 (349 con relleno, 54,9 % del área de 636) y 670 px a 1920x1080 (690 de 714). `WidthHold` sin cambios. `layer_content_width(Debug)` incluye el mínimo. Fila de F3 de `docs/teclas.md` y de la tabla: «por páginas (F3: página 1, 2... y oculto)». Arreglo c) (decisión de Lucas): las líneas «motor» y «ejes» (`debug_lines.{h,cpp}`, en `rally_loop`) llevan campos de longitud fija: signo siempre en los pares, rpm con 4 cifras, cifras con ceros delante y recortadas al campo, y los estados (embrague: cerrado/arranque, «, abierto»; «SIN CONVERGER») rellenados con espacios y espacios duros hasta la medida del más largo. `debug_text_width` mide además el «-» como «+» (más ancho), para que un par que cambia de signo no mueva el panel. Test nuevo `debug_lines` (todos los estados, rpm 0-9999, pares +/-/0, fuentes 10 y 20: ambas líneas miden lo mismo). Tests 516 → 522 en Release y Debug; huella `0x66c9e3a86632750e`, reservas de Jolt 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5 sin cambios. Sin validar a ojo. | |

## Hallazgos

| Fecha | Fase | Horas | Hallazgo |
| --- | --- | --- | --- |
| 2026-10-27 | 3 | | En el juego, el mínimo pedido (≤ 60 % a 1360x768) no hace que F3 se quede quieto: las líneas «motor» y «ejes» miden 438-522 px a 768 (el 60 % son 381) y cambian de ancho con el embrague abierto, las rpm bajo 1000 o el signo de un par. A 1080p el panel ya ocupa toda el área. Lucas eligió c): hecho (ver la fila). |
| 2026-10-27 | 3 | | La fuente por defecto de raylib no tiene «…» (U+2026): el texto de una tecla con «…» rompe `keys: every label and text is drawable…`. |

## Ediciones propuestas de docs 02, 07 y 08

- `docs/08`, sección de F3 / bitácora: «El ancho del F3 se mide con 8 en cada cifra y tiene un mínimo de 56 caracteres (`kDebugMinChars`). Solo actúa donde las líneas son cortas (copiloto); en el juego mandan motor y ejes».

Pide de lo compartido: nada.
