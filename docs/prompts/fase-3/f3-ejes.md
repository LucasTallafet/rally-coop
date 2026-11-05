# Tarea `f3-ejes` (lote 5, carril plataforma): la línea «ejes» del F3 en dos líneas

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Sale de `integracion` con `cabina-volumen` ya fusionada.

## Por qué

Hallazgo del lote 4 (`f3-ancho`): a 1280×720 la línea «ejes» sale cortada con «…» (`interior_f3_t.png`). `debug_axles_line` (`platform/debug_lines.cpp`) junta en ~95 caracteres los pares de los ejes, el central, el viscoacoplador, los autoblocantes y el LCP.

## Qué hacer

- Partirla en dos líneas, con los campos de longitud fija de hoy:
  - «ejes: <par del.> / <par tras.> N·m; central Δω … rad/s, visco … N·m»;
  - «autobl.: … / … N·m; LCP …» (con « SIN CONVERGER» igual que hoy).
- Las dos van juntas en el mismo bloque del F3 (no se separan entre páginas).
- Regenera las capturas con `golden.ps1 -Regenerar` y di cuáles cambian (`interior_f3_t.png`).

## Criterios (tests en `tests/platform/`, Release y Debug)

1. El test `debug_lines` de hoy, con las dos líneas: cada una mide lo mismo en todos sus estados (rpm de 0 a 9999, pares +/−/0, con y sin «SIN CONVERGER», fuentes 10 y 20).
2. A 1280×720, con la fuente de ese tamaño, las dos caben en el ancho del contenido del F3 sin recortar (`debug_text_width` frente al ancho del panel de `screen_layout`).
3. Los bloques del F3 siguen enteros y cada línea sale una vez (los tests de `f3-panel`).

**Sin cambios:** nada fuera de `src/platform/**`, `tests/platform/**` y las capturas.

## Paradas

Ninguna prevista.

## Para el informe

- Las dos líneas con un ejemplo y su ancho en píxeles a 1280×720 y 1920×1080.
- **A ojo, en la prueba de Lucas:** F3 en ventana a 1280×720, sin «…».
