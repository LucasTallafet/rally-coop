# Tarea `tramo-seguimiento` (lote 4, carril sim): `s` y `t` sobre el tramo, recolocación, atajos y parciales

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-4.md` §2.1, §2.3 y §2.4, que mandan sobre este prompt.

## Por qué

Todo lo que necesita saber «dónde está el coche» en el tramo lee `s` y `t` (`02` §5): la recolocación, los atajos, los parciales, el giro del mapa, el cursor y las notas de la tablet (lote 5) y la puntuación (fase 4). Esta tarea hace el seguimiento **sin Jolt**: recibe posiciones y devuelve decisiones. La física del tramo y su sitio en `SimWorld` son de `tramo-fisica`, la tarea siguiente del mismo carril.

## Qué hacer

1. **`StageTrack`** (`src/sim/stage_track.*`, contrato §2.4): envuelve un `StageAxis` y añade, al cargar:
   - las prolongaciones rectas de §2.3 (entrada de 20 m y frenada de 150 m), con los valores de los extremos;
   - el índice espacial: celdas de 10 m en planta con los intervalos de `s` del eje (prolongaciones incluidas) que pasan a menos de `kStageStripHalfWidth` de cada celda;
   - la validación de §2.6 («Validación al cargar»): la malla no se pliega y los núcleos de pasadas distintas no se tocan. Es geometría del eje, así que va aquí; `tramo-fisica` la usa.
2. **Proyección** (`project`, `acquire`, `on_any_road`):
   - en rectas y arcos, exacta;
   - en clotoides, Newton desde el punto anterior, con `heading` y `curvature` de `pose` como derivadas (2–3 iteraciones);
   - sin reservas de memoria: se llama en cada paso.
3. **`StageRun`**, la máquina de §2.4 con sus reglas, en el orden que dicen: atajo primero; después `reset_s`, parciales y meta. Estado POD (`StageRunState`) con un array fijo de parciales: sin reservas.
4. **Tramos de test**, en `tests/sim/stages/` si hace falta alguno más que `lazadas.stage` (que no se toca: lleva `kExpectedStageHash`). Por ejemplo, uno de 2–3 km para los parciales, o texto generado en el propio test.

Reutiliza `StageAxis::pose` (no dupliques la geometría), `rmath`, `fnv1a.h` y el estilo de `lap_timer` (`RoadTimer` y `RoadRunState`). Si escribes una utilidad nueva, el plan dice qué buscaste y por qué no sirve lo que hay.

## Criterios (tests en `tests/sim/`, Release y Debug)

Las trayectorias son de puntos que se mueven por el tramo a pasos de 1/240 s: no hay coche ni Jolt.

1. **Por el eje y paralelo a él** en `lazadas.stage`, a 10, 50 y 120 km/h, con t = 0, +2 y −2 m (en la horquilla de R 6, ±2 m quedan dentro):
   - |s − s_real| ≤ 1 mm y |t − t_real| ≤ 1 mm en todos los pasos;
   - `s` nunca retrocede y nunca salta: |Δs| ≤ d / (1 − max(κ(s_prev)·t, κ(s)·t)) + 1 mm por paso, con d lo que avanza el punto en planta (un punto paralelo al eje a t recorre Δs·(1 − κ·t): por dentro de la horquilla de R 6 a t = +2 m avanza 2/3 de `s`, y la cota «d + 1 mm» no se podía cumplir; corregido en la revisión del plan del lote 4);
   - en las lazadas, `s` nunca pasa al otro brazo (a 13,35 m).
2. **Prolongaciones:** salir desde s = −20 m y acabar en s = L + 150 m sin saltos; fuera de [s_begin, s_end], `s` queda en el extremo.
3. **Adquisición:** desde 2000 puntos repartidos por la franja (PCG32 de semilla fija), `acquire` da la `s` del punto del eje más cercano, frente a una búsqueda de fuerza bruta del test (muestreo cada 0,05 m y refino), con ≤ 1 mm.
4. **Atajo:** en línea recta, de un brazo de las lazadas al otro, por fuera de la carretera:
   - en el sentido de la marcha, `Step{true, exit_s}`, `shortcuts` = 1, y ni `reset_s` ni los parciales avanzan;
   - en el sentido contrario (de vuelta a una pasada anterior), no es atajo: sigue desde la `s` de la pasada a la que vuelve;
   - el test escribe las cifras: ganancia de `s`, recorrido fuera y margen.
5. **Corte de curva permitido:** por dentro de una curva, saliendo y volviendo con una ganancia menor que lo recorrido + 30 m, no hay atajo. Un caso justo por debajo y otro justo por encima del margen (±0,5 m).
6. **Salida y vuelta normal:** salir 10 m al lado y volver a la misma zona del tramo no es atajo, y `reset_s` no avanza mientras el punto está fuera.
7. **Recolocación:** `reset_s` es el máximo de `s` en carretera; no baja al ir marcha atrás ni avanza fuera. `placed()` deja `s`, `t` = 0 y en carretera.
8. **Pasada, parciales y meta:** empieza al pasar `kRunStartDistance`; un parcial en cada múltiplo de `kSplitSpacing` que cumple el hueco de la meta; la meta en L, con su paso. Ni los parciales ni la meta se cuentan dos veces.
9. **Validación:** `lazadas.stage` pasa. Un tramo con una curva más cerrada que su núcleo, y otro con dos brazos más cerca de lo permitido, dan su error con la `s`.
10. **Sin reservas:** 100 000 pasos de `StageRun::step` (en carretera, fuera y adquiriendo) dan 0 `operator new` (contador de `tests/common`).
11. **Coste en Release** (media de 1 000 000 de pasos): ≤ 2 µs por paso en carretera y ≤ 6 µs fuera de ella (con `on_any_road`). Lo informas también en Debug, sin límite.
12. **Estado:** `StageRunState` es POD; dos pasadas con la misma trayectoria dan el mismo estado bit a bit; cada campo cambia en algún test (que falla si el campo se quita).

**Sin cambios:** `kSimVersion`, la huella, las reservas de Jolt, el hash de la telemetría, `kExpectedStageHash` y `SimWorld`. No tocas `test_road`, `test_zone` ni nada de lo que mide la 2.6b.

## Regímenes

Cada uno con su test: parado; marcha atrás; en carretera a 10, 50 y 120 km/h; en el aire sobre la calzada (la altura no cuenta); fuera de la carretera; volviendo a otra pasada (atajo y no atajo); en las prolongaciones y en los extremos; recolocar antes de empezar la pasada y después de la meta; punto fuera de la franja (más de 60 m del eje).

## Para el informe

- Coste por paso (Release y Debug) y memoria del índice para `lazadas.stage` y para un tramo de 30 km (puede ser sintético, generado en el test).
- Lo que `tramo-fisica` necesitará y no está: dilo, no lo hagas.
- Nada que probar jugando: no hay A/B.
