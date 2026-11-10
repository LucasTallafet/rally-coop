# Tarea `cambios-direccion` (lote 6, carril juego): los cambios de dirección del eje, para la cinta y el cursor

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-6.md` §2.3, que manda sobre este prompt. Sale después de `red-tramo`, en el mismo carril.

## Por qué

`01` §4 y §5.2: la cinta de ruta marca «cada cambio de dirección de más de unos 5°, sin su forma», y el cursor de la tablet apunta por defecto al siguiente cambio de dirección por delante del coche y salta al siguiente o al anterior con la cruceta. **La cinta y el mapa no opinan:** el criterio es solo geométrico y no dice qué curvas merecen nota. Lo usa la tablet (oleada 2) y, en el lote 7, el copiloto bot. Va en `src/game` (el copiloto, `02` §2), sin raylib.

## Qué hacer

1. **`src/game/direction_changes.{h,cpp}`** (nombres en inglés; en `rally_game`):

   ```cpp
   struct DirectionChange {
       double s_start; // m: donde empieza (la curvatura sale de 0 o cambia de signo)
       double s_end;   // m: donde acaba
       double angle;   // rad, con signo: + derecha (como el rumbo y R en el .stage)
   };
   // Al cargar: reserva memoria. Ordenados por s_start; solo los de |angle| ≥ kDirectionChangeMinAngle.
   std::vector<DirectionChange> direction_changes(const sim::StageAxis& axis);
   // Sin reservas: el primero con s_start > s, o el último con s_start < s (−1 si no hay).
   int next_direction_change(const std::vector<DirectionChange>& changes, double s);
   int previous_direction_change(const std::vector<DirectionChange>& changes, double s);
   ```

   `kDirectionChangeMinAngle` = 5° en radianes. La firma exacta la cierras en el plan; lo que no cambia es que sea una función pura del esqueleto, calculada al cargar.
2. **Criterio geométrico:**
   - un cambio es un tramo de curvatura de un solo signo (|κ| por encima de un umbral pequeño que fijas en el plan con su motivo);
   - su ángulo es la diferencia de rumbo entre su principio y su final;
   - dos curvas seguidas del mismo sentido con una recta en medio son dos cambios (unirlas es la nota «larga», que la pone el copiloto, no el juego);
   - una clotoide de inflexión da dos cambios, uno a cada lado de donde la curvatura pasa por 0.
3. **Matemática por `rmath`:** `src/game` está dentro de `cmake/CheckSimRules.cmake`.

## Criterios (tests en `tests/game/`, Release y Debug)

1. **`lazadas.stage`** (lee `tests/sim/stages/lazadas.stage`; no la copies) da exactamente estos seis cambios, calculados con las primitivas del archivo (Δθ de una clotoide = L/(2R); de un arco, L/R). Tolerancias: `s_start` ±1 m (lo que mueva tu umbral de curvatura) y ángulo ±0,5°:

   | # | `s_start` | Sentido | Ángulo | De dónde sale |
   | --- | --- | --- | --- | --- |
   | 1 | 100 | derecha | 78,8° | clotoide 40 + arco R 80 de 70 m + clotoide 40 |
   | 2 | 390 | izquierda | 121,8° | clotoide 30 + arco R 40 de 40 m + media clotoide de inflexión (60 m) |
   | 3 | 520 | derecha | 121,8° | media clotoide de inflexión + arco R 40 de 40 m + clotoide 30 |
   | 4 | 730 | derecha | 180,0° | horquilla 1 |
   | 5 | 828,85 | izquierda | 180,0° | horquilla 2 |
   | 6 | 907,70 | derecha | 51,6° | clotoide 30 + arco R 100 de 60 m + clotoide 30 |

   Si te sale otra lista, para: es una premisa falsa (abajo).
2. **El umbral de 5°:** en un tramo sintético con una curva de 4,5° y otra de 5,5° (arco largo de radio grande), sale solo la segunda.
3. **Una recta en medio separa:** dos arcos del mismo sentido con 20 m de recta en medio dan dos cambios.
4. **`next` y `previous`** en los extremos (antes del primero, después del último, justo en un `s_start`) y sin reservas (el contador de `operator new` de `tests/common`).
5. **Coste:** `lazadas` y el sintético de 30 km (el de los tests de sim, si lo puedes construir sin depender de otro carril; si no, uno propio), en ms al cargar. Se informa; sin límite.

**Sin cambios:** nada fuera de `src/game` y `tests/game`. `revisor-determinismo` revisa esta rama (toca `src/game`).

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| `StageAxis::pose(s)` da la curvatura y el rumbo exactos de las primitivas | Los tests de hoy de `tests/sim/test_stage_axis.cpp` | Parada |
| La tabla del criterio 1 es la de `lazadas.stage` | La sacas de `[plan]` antes de programar y la comparas | Parada: «Necesita consultor» con la tabla que sale |

## Paradas

Solo el criterio 1 que falla o una premisa falsa. Lo demás, avisos.

## Para el informe

- La lista de `lazadas` con sus cifras y el umbral de curvatura que elegiste, con su motivo.
- Nada que probar jugando (la prueba es la de `tablet`).
