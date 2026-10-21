# anillo-ayuda (arreglo del lote 1)

## Registro
| Tarea | Rama | Horas |
|---|---|---|
| anillo-ayuda | lote-1/anillo-ayuda | |

## Hallazgos
| Hallazgo | Detalle |
|---|---|
| Anillo oculto con la ayuda | `draw_2d` solo dibujaba el anillo en la rama sin ayuda y `layers_exclusive` lo declaraba exclusivo con ella. Ahora se dibuja siempre y el último, con fondo propio (`Overlay::panel`) si la ayuda está abierta; `layers_overdraw(Help, ActionRing)` es la única pareja que se solapa a propósito. |
| Cobertura | Los tests no enlazan raylib: cubren el contrato de `screen_layout` (no exclusivo, sin solape con indicador, aviso, mensajes, depuración, rendimiento ni gráficas, dentro de la pantalla, a 1280x720, 1360x768 y 1920x1080). Que `draw_2d` lo llame hay que verlo a ojo. |

## Ediciones propuestas (integrador)
- `docs/08-bitacora-de-desarrollo.md` (~línea 113): sustituir «El anillo comparte sitio con la ayuda (H) y no se dibuja con ella abierta» por «El anillo se dibuja siempre, encima de la ayuda y con fondo propio (`layers_overdraw`)».
- `docs/07-entorno-y-portabilidad.md` (~línea 211): tras «el anillo de progreso sale centrado, algo por debajo del centro» añadir «también con la ayuda abierta, encima de ella».

## Pedido a lo compartido
Nada.
