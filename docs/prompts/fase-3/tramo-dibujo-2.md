# Tarea `tramo-dibujo-2` (lote 6, carril plataforma): el dibujo del tramo con la banda del formato 2

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-6.md` §2.1, que manda sobre este prompt. Sale con `formato-2` ya fusionada, en el mismo carril que `tablet` (antes o después, según el orden del lote).

## Por qué

`formato-2` añade a la malla del núcleo una banda de entorno (cuneta, talud y muro), y a cada `StageTriangle` su sección (`section`) y su aspecto (`look`). Hoy el dibujo saca la sección de cada triángulo con `min(a, b, c) / kStageSectionVertices` (`platform/stage_chunks.cpp`), es decir, supone 5 vértices por sección. Con la banda, esa cuenta da secciones equivocadas, los triángulos caen en trozos que no son los suyos y el dibujo los quita a 600 m del ojo. Ningún test lo detecta: `test_stage_draw.cpp` solo cuenta que cada triángulo sale una vez. Además, el muro va de color de roca (decisión de Lucas, 2026-11-08), y eso solo lo sabe el `look`.

## Qué hacer

1. **`StageChunks::build`:** el trozo de cada triángulo sale de `sections()[t.section]`. `kStageSectionVertices` deja de usarse en plataforma.
2. **Colores por `look`:** el muro, de roca (un gris frío, distinto del asfalto); la cuneta y el talud, con el color de su superficie (tierra), y si se confunden con el terreno lejano, con un matiz algo más oscuro (propónlo en el plan). La calzada y el arcén, como hoy. `StagePalette` gana lo que haga falta.
3. **`tramo_salida.png`:** regenérala con `golden.ps1 -Regenerar` sobre esta rama y di qué capturas cambian (solo debería cambiar esa).

## Criterios (tests en `tests/platform/`, Release y Debug)

1. **Cada triángulo, en su trozo:** para cada triángulo de `lazadas`, la `s` de su sección cae dentro de los límites en `s` de su trozo, y su caja en planta dentro de la del trozo. **Falla cuando debe:** con la cuenta de hoy (`min / 5`), el test falla con la banda del formato 2; lo compruebas y lo restauras (el verificador lo repite).
2. Los tests de hoy de `test_stage_draw.cpp` pasan: cada triángulo sale una vez y los vértices dibujados son los de `StageGround`, exactos en float.
3. **Muro de roca:** cada triángulo con `look` de muro sale con el color de roca, y ninguno más.
4. `StageSurface::query` coincide con `probe_ground` de la física en 3000 puntos hasta ±62 m del eje, ahora con la banda: sin huecos distintos ni superficies distintas, y la diferencia de y, dentro de la cuantización de Jolt (el test de hoy del lote 5, que ya debería pasar; si no pasa, es una parada).
5. **Coste:** `--bench-stage` con la banda frente a la medida del lote 5 (en el informe de `tramo-dibujo`), en la misma máquina: CPU del suelo, media y p99. Se informa; sin límite.

**Sin cambios:** la simulación, la red y la tablet.

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| `formato-2` da `section` y `look` en cada triángulo | Lo dice su contrato; lo lees en `stage_ground.h` | Parada |
| Ninguna cara pasa de 80° (`formato-2`), así que el dibujo puede seguir orientando los triángulos hacia arriba | Normal con y > 0 en todos los triángulos de `lazadas` | Parada |

## Paradas

Solo los criterios 1, 3 o 4 que fallan, o una premisa falsa. Lo demás, avisos.

## Para el informe (la prueba de Lucas)

- ¿Se lee el muro entre los brazos de las lazadas? ¿Se distinguen la cuneta y el talud del terreno?
- Las cifras de `--bench-stage`.
