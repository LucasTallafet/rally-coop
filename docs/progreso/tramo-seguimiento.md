| 2026-10-27 | 3 | | Tarea `tramo-seguimiento` (lote 4, carril sim): seguimiento de `s` y `t` sobre el tramo, sin Jolt y fuera de `SimWorld`. `sim/stage_track.h`: constantes de §2.3 (`kStageEntryLength` 20 m, `kStageRunoffLength` 150 m, arcén 1 m, franja 60 m, atajo 30 m, parciales cada 1000 m salvo a < 200 m de la meta, salida a 0,10 m) y `kMaxSplits` 64. **`StageTrack`**: `StageAxis` + prolongaciones rectas con los valores de los extremos + muestras del eje a ≤ 0,5 m + índice (celdas de 10 m en un vector ordenado por (celda, muestra), con intervalos de `s` a < 60 m de la celda para adquirir y a < núcleo máximo + 1 m para `on_any_road`) + validación al cargar («s = …: …»: la malla no se pliega, |κ|·(núcleo interior + 0,5) < 1; núcleos de puntos con \|Δs\| > 50 m a ≥ 1,5 m, prolongaciones incluidas). Proyección por piezas: recta y arco exactos (los arcos de más de π se parten al cargar), clotoide con los mínimos de las muestras y Newton protegido por bisección; `on_road` = pie perpendicular y \|t\| ≤ semiancho + arcén. **`StageRun`** con la máquina de §2.4 (atajo primero; después `reset_s`, salida, parciales y meta) y estado POD `StageRunState` (608 B, sin reservas). Cambios de interfaz aprobados en la revisión del plan: `placed(StagePlace why, s, x, z)` (Start solo antes del primer paso, `RALLY_CHECK`; Button suma `resets`), `Step::reset_s` sin atajo = punto de recolocación actual, `distance` en planta al punto del eje e `index_bytes()`. Tests 517 → 539 (22 nuevos en `tests/sim/test_stage_track.cpp`): por el eje y a ±2 m a 10/50/120 km/h, \|s − s_real\| ≤ 1,7·10⁻⁹ m y \|t − t_real\| ≤ 8·10⁻¹⁴ m (≤ 1 mm), sin retrocesos ni saltos (Δs ≤ d/(1 − κ·t) + 1 mm); 2000 puntos de la franja frente a fuerza bruta 4,5·10⁻⁶ m (≤ 1 mm, 0 empates); arco de 200° 4,2·10⁻⁶ m; atajo entre brazos: ganancia 88,85 m, fuera 6,19 m, margen 52,66 m; corte de la horquilla con margen −0,5 m (sin atajo) y +0,5 m (atajo) a 3,40 y 3,90 m de su inicio; `on_any_road` da la calzada ancha con el eje estrecho más cerca (3,40 frente a 3,8 m); 0 `operator new` en 100 000 pasos; coste en Release 0,74 µs por paso en carretera (≤ 2) y 2,95 µs fuera (≤ 6), adquisición 3,2 µs; en Debug 12,9, 45,7 y 44,8 µs (sin límite). `lazadas.stage`: carga 6 ms, 140 KiB de índice y muestras; tramo sintético de 30 km: carga 0,14 s, 3,1 MiB, 0,69 y 1,66 µs por paso. Huella `0x66c9e3a86632750e`, reservas 1990, telemetría `0xfe3c612ddcaa6805`, `kSimVersion` 5 y `kExpectedStageHash` `0x5876c61a309019a0`: sin cambios (nada entra en `SimWorld`). Sin validar a ojo (no se conduce). | `tramo-fisica` |

Utilidades nuevas: ninguna compartida. Busqué proyección sobre un eje en `src/sim` (`zone_axis.h` tiene `AxisBuilder`/`segment_point`, sin proyección; `test_road.cpp` proyecta sobre su propio eje muestreado, en su espacio anónimo y para otra geometría) y rejillas espaciales (no hay). Reutiliza `StageAxis::pose`, `rmath`, `RALLY_CHECK` y el estilo de `RoadTimer`/`RoadRunState`; en los tests, `Pcg32`, `global_new_count` y `io::read_file`.

Hallazgos:

| Fecha | Prueba | Resultado | Decisión afectada |
| --- | --- | --- | --- |
| 2026-10-27 | Criterio 1 de `tramo-seguimiento` («\|Δs\| ≤ lo que avanza el punto en planta + 1 mm») | Con la letra no se puede cumplir: un punto paralelo al eje a t recorre Δs·(1 − κ·t), y por dentro de la horquilla de R 6 a t = +2 m avanza 2/3 de `s` (a 120 km/h, d = 0,139 m y Δs = 0,208 m). Se probó con la cota exacta Δs ≤ d/(1 − max(κ(s_prev)·t, κ(s)·t)) + 1 mm, aprobada en la revisión del plan | Ninguna; texto corregido del criterio para el consultor (abajo) |
| 2026-10-27 | Validación de §2.6 frente a un arco que se cierra sobre sí mismo (revisión del plan) | Un arco de R < 7,96 m que dé la vuelta entera se pisa con \|Δs\| < 50 m (2πR < 50 m) y la regla de los núcleos no lo ve. Con las prolongaciones de 20 y 150 m no puede pasar sin que la frenada choque con el lazo, pero la regla no lo dice | Para el consultor: si la validación debe rechazar un giro de más de 2π en una pieza, o medir la separación también dentro de la misma pasada |

Ediciones propuestas (integrador):

- `docs/02-arquitectura.md` §5, sustituir la lista por:

  ```markdown
  - **Incremental en cada paso:** se busca solo en la ventana de `s_prev − 20 m` a `s_prev + d + 20 m` (d, lo que se ha movido el origen del coche en planta). Proyección exacta en rectas y arcos (los de más de π se parten al cargar) y, en clotoides, Newton protegido por bisección desde los mínimos de unas muestras del eje a ≤ 0,5 m (`sim/stage_track.h`, `StageTrack::project`). Así dos lazadas de una horquilla que pasan a pocos metros nunca compiten. Si el mínimo cae en un borde de la ventana que no es un extremo del tramo, se adquiere con el índice.
  - **Prolongaciones:** recta de 20 m antes de s = 0 y de 150 m tras la meta, con el rumbo, la pendiente, el peralte, los semianchos y la superficie de cada extremo. Fuera de [−20, L + 150], `s` se queda en el extremo y el coche no está en carretera.
  - **Índice espacial** (celdas de 10 m en planta, en un vector ordenado, con los intervalos de `s` del eje a menos de 60 m de cada celda y, aparte, los que pasan a menos del núcleo más ancho): para adquirir la posición (al salir de la ventana, tras un vuelo largo) y, fuera de la carretera, para ver en cada paso si el coche ha vuelto a la calzada de cualquier pasada (`on_any_road`, que mira cada mínimo local, no solo el eje más cercano).
  - **En carretera** mientras el pie es perpendicular y `|t|` ≤ semiancho de ese lado + 1 m de arcén. La altura no cuenta. El punto de recolocación (`reset_s`) es el máximo de `s` en los pasos en carretera.
  - **Atajo:** al volver a la carretera de cualquier pasada en `s_ret`, si `s_ret − exit_s` supera en más de 30 m a lo recorrido fuera desde `exit_s` (la `s` del último paso en carretera), se recoloca en `exit_s` y cuenta un atajo. Si no, sigue desde `s_ret` (cortar por la cuneta, o caer a una pasada anterior, vale).
  - **Pasada:** empieza con `reset_s` > 0,10 m; un parcial al pasar cada múltiplo de 1000 m de `reset_s` que quede a ≥ 200 m de la meta, y la meta al llegar a L. Un atajo no cuenta parciales ni meta. La simulación cuenta pasos, recolocaciones y atajos; la penalización la aplica quien enseña el tiempo.
  - **Al cargar** (`StageTrack::build`), error «s = …: …» si la malla se plegaría en una curva (|R| ≤ semiancho interior + arcén + 0,5 m) o si los núcleos de dos puntos con |Δs| > 50 m quedan a menos de 1,5 m.
  - Todo lo que necesita "dónde está el coche" lee `s` y `t`: giro del mapa, microrrelieve, notas, puntuación por curva, recolocación.
  - **Tests** (`tests/sim/test_stage_track.cpp`): `lazadas.stage` recorrido a 10, 50 y 120 km/h por el eje y a ±2 m, sin saltos ni retrocesos de `s`; la línea recta de una lazada a la siguiente es un atajo y la vuelta a una pasada anterior no; un corte de curva 0,5 m por debajo del margen vale y 0,5 m por encima no.
  ```

- `docs/08-bitacora-de-desarrollo.md` §4 (problemas encontrados), añadir:

  ```markdown
  - **Seguimiento: la cota de «s no salta» por dentro de una curva** (lote 4, `tramo-seguimiento`). Un punto paralelo al eje a una distancia t avanza en planta Δs·(1 − κ·t): por dentro de la horquilla de R 6 a 2 m, 2/3 de lo que avanza `s`. La cota «Δs ≤ lo que avanza el punto + 1 mm» fallaba siempre; la correcta es Δs ≤ d/(1 − κ·t) + 1 mm con la κ mayor de los dos extremos del paso (en una clotoide, la del inicio sola se queda corta 0,09 mm a 120 km/h).
  - **Seguimiento: el eje más cercano no es siempre el de la calzada que se pisa** (lote 4). Con anchos distintos (núcleos de 4 y 1,5 m a 1,5 m de separación), un punto a 3,8 m del eje ancho está más cerca del estrecho. `on_any_road` mira cada mínimo local de la distancia, no solo el más cercano.
  ```

Texto corregido del criterio 1 del prompt (para el consultor, `docs/prompts/fase-3/tramo-seguimiento.md`):

> `s` nunca retrocede y nunca salta: |Δs| ≤ d / (1 − max(κ(s_prev)·t, κ(s)·t)) + 1 mm por paso, con d lo que avanza el punto en planta (un punto a t del eje avanza Δs·(1 − κ·t)).

Lo que necesita `tramo-fisica` y no está (no lo hago):
- Un hash de `StageRunState` campo a campo para `state_hash()` (§2.5): hay relleno de 3 bytes tras `on_road`, así que no vale hashear los bytes de la estructura.
- `StageRun` guarda un puntero al `StageTrack`: si `SimWorld` lo guarda por valor y se mueve o se copia, el puntero queda colgando (guardarlo en un `std::unique_ptr` o construir el `StageRun` después de fijar el `StageTrack`).
- `step` recibe el origen del coche en planta, no un punto de contacto; el test del coche en el aire sobre la calzada (sigue en carretera) es de `tramo-fisica`.
- `placed(StagePlace::Start, …)` al construir, con la `s` de `stage_spawn`; tras un atajo, `placed(StagePlace::Shortcut, exit_s, …)` antes del siguiente `step`.
- Las muestras del eje (`sample_*`, a ≤ 0,5 m) y las piezas son privadas: si la malla las quiere reutilizar, hará falta exponerlas.

Pide de lo compartido: nada.
