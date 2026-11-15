## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-14 | 3 | | **Lote 7, `irregularidad` (partes 0, A y B).** Parte 0: los tres tests de la costura fijan cuántos casos aviso excluyen y su peor valor, por tramo (154/23/4 lados; 523,07 mm; 0,0 mm; 94/60/34 puntos al azar en Release y 8/4/3 en Debug), y fallan con todo en aviso. Capa de irregularidad del trazado (`sim/stage_irregularity`): 7 canales (eje, ancho y borde de cada lado, peralte y ondulación) de 4 ondas sembradas con PCG, escala por s con paso suave y apagado de eje, ancho y borde en curvas de R < 30 m. Formato de tramo 2 con `[irregularity]` opcional; la calzada real (`StageTrack::road`/`road_edges`) en «en carretera», microrrelieve, banda, `e(s)`, salida y recolocación; validación al cargar con lo medido × 1,02; `stage_text_without_irregularity`; `data/stages/lazadas_irregular.stage`. Cuerda de 5 cm en la línea exterior del arcén. Con la sección a cero, `lazadas` da los mismos suelo y estado (`0xacdcd1b800bb9d7d`, `0x3d08dcb86b41ad6f`). `lazadas_irregular`: suelo `0x02a6755fb2bab28c`, pasada `0x3f7fa54fb79fa258`; 1565 secciones y 268 858 triángulos (×1,31), 4,80 MB (×1,26), carga 0,69 s (×1,05), paso 0,047 ms (×1,05). Criterio 3: +1,64 puntos sin contacto como mucho (límite +2,5) y p99 del salto de carga ×1,17 como mucho (límite ×1,30). Tests 715 → (ver el informe). Huella, reservas, telemetría y `kSimVersion` sin cambios. Sin validar a ojo | Prueba de Lucas con la tecla de `panel` |

## Hallazgos

| Fecha | Prueba | Resultado | Decisión afectada |
| --- | --- | --- | --- |
| 2026-11-13 | `irregularidad`, parte A: la onda corta del borde en las horquillas | Con la onda corta del arcén (±12 cm, λ 4–8 m), `lazadas` no carga en s 739,7 («no cabe el talud entre pasadas»): en la horquilla de R 6 quedan 0,9 m entre el arcén interior y el centro de la curva. El sintético de 30 km, igual (s 4689,7, R 8–10). Se apaga también ε donde \|κ\| > 1/30 (como d y ω), y carga | `05`, capa de irregularidad (límites): el apagado en curvas cerradas alcanza también los bordes (aceptado por el consultor; la fila de `05` la escribe él al cerrar el lote) |
| 2026-11-14 | `irregularidad`, parte B: qué pide las secciones | Con la cuerda de 1 cm en todas las líneas, 3110 secciones (×2,59). Con 5 cm en la línea exterior del arcén, 2734 (×2,28). Lo que queda lo piden el alzado y el alabeo del peralte añadido y, sobre todo, la costura banda con banda entre los brazos de las lazadas (s 580–980: 1852 secciones frente a 660), donde el eje medio se mueve con los bordes de las dos pasadas. Con la escala a 0 entre los brazos (s 640–960, el uso que el prompt daba a la escala): 1565 (×1,31). El borde de 6–10 m solo bajaba a 2430 con la irregularidad entre los brazos | La escala por s: apagar la irregularidad entre los brazos de unas lazadas |
| 2026-11-14 | `irregularidad`, parte B: la cuerda de 5 cm y los puntos al azar de la costura | Donde actúa la onda corta del borde, la malla entre secciones queda hasta 5 cm por dentro del final analítico de la banda: un punto «2 cm dentro del final» puede quedar fuera de la malla (las dos sondas dan la rejilla, margen 0,0 exacto, s 1210,6). El test cuenta esos 5 cm; con ellos, 0 puntos con la rejilla encima y un margen mínimo de 3,85 mm fuera de los avisos | Ninguna (invariante 7 de la costura, sin cambios) |
| 2026-11-14 | `irregularidad`, parte B: los picos de la recta de meta de `lazadas` | Los dos mayores saltos de carga de la recta de meta (1649 N a 60 km/h y 4459 N a 120 km/h) caen en s 1398,9 y 1399,2: al final de la frenada (s_end = 1400), donde acaba la malla del tramo. No están en la calzada del tramo; el p99 no los nota | Ninguna |

## Ediciones de `docs/02`, `07` y `08`

### `docs/02-arquitectura.md` §6.1, tras el párrafo «Formato 1 (`.stage`, lote 3)…» y sus listas de reglas y de geometría (antes de «### 6.2»), añadir:

> **Formato 2 (lote 7): la capa de irregularidad.** `format = 2` admite la sección opcional `[irregularity]`; en un `format = 1` es un error. Sin ella, un formato 2 es el formato 1. `kStageFormatVersion = 2` es el mayor que se lee. `StageAxis::hash()` empieza por el formato del archivo, así que un formato 1 da el hash de siempre; en el formato 2, acaba con la sección. Una línea por palabra, cada componente opcional (sin ella no hay ondas) y como mucho una vez:
>
> ```text
> [irregularity]
> seed <entero de 64 bits>      # obligatoria si hay sección
> axis   <A m> <λmin> <λmax>    # d(s): el centro de la calzada se aparta del eje
> width  <A m> <λmin> <λmax>    # ω(s): onda larga de cada semiancho (ancho variable)
> edge   <A m> <λmin> <λmax>    # ε(s): onda corta del borde exterior de cada arcén
> bank   <A %> <λmin> <λmax>    # β(s): peralte añadido
> height <A m> <λmin> <λmax>    # z(s): ondulación vertical
> scale  <s> <factor ≥ 0>       # escala de todas las amplitudes; primera en s = 0, s crecientes
> ```
>
> A ≥ 0 y 1 m ≤ λmin ≤ λmax. Hay siete canales (d, ω y ε de cada lado, β y z), cada uno `σ(s)·G(s)·W(s)` (`sim/stage_irregularity.h`):
> - **W:** 4 ondas `a·sin(k·s + φ)` con `Pcg32(seed, canal)`. λ log-uniforme, una por cada cuarto de [log λmin, log λmax]; φ uniforme; a ∝ λ con Σa = A. El pico nunca pasa de A, y las ondas cortas, que son las que curvan, llevan menos (presupuesto de curvatura; no es el espectro de ISO 8608).
> - **σ, la escala:** entre dos puntos, un paso suave (3u² − 2u³), no lineal, para que d', z' y β' no salten en los puntos. Cada tramo entre dos puntos es su propia rampa (una rampa son dos puntos); antes del primero y tras el último, constante.
> - **G, el apagado en curvas** (d, ω y ε): un paso suave de 45 m sobre la distancia en s al conjunto donde \|κ del esqueleto\| > 1/30 m⁻¹. En las curvas de radio menor de 30 m no se desvía el eje, no cambia el ancho y no se rompen los bordes; el peralte y la ondulación no se apagan.
>
> Se calcula al cargar; evaluar no reserva memoria.
>
> El esqueleto no cambia (`s`, `t`, `pose`, `project`, la tablet y las notas). La calzada real va sobre la normal del esqueleto (`StageTrack::road`, `road_edges`):
> - centro en t = d;
> - calzada de d − (w_izq + ω_izq) a d + (w_der + ω_der);
> - arcenes de 1 + ε;
> - plano Y = y0 + (peralte + β)·t, con y0 = y + z − β·d (el centro queda a y + z + peralte·d).
>
> La usan la malla, «en carretera», el microrrelieve, `e(s)`, la salida y la recolocación (en el centro real, con el rumbo de la línea central).
>
> **Validación al cargar.** Cada canal se muestrea a ≤ λmin/16, y lo medido × 1,02 (lo que ese paso puede subestimar) se compara con su límite:
> - \|d''\| ≤ 0,01 m⁻¹;
> - \|z''\| ≤ 0,0033 m⁻¹;
> - \|β'\| ≤ 1,5 %/m.
>
> Además, el eje del esqueleto queda dentro de la calzada y los arcenes miden ≥ 0,5 m. El error dice la s, lo medido, el límite y qué bajar («s = …: la desviación del eje curva 0.0112 m⁻¹ y el límite es 0.0100 m⁻¹: baja 'axis' o alarga su onda»). No se usa la cota analítica, que supone las ondas en fase y es hasta 3 veces lo medido. Las validaciones de siempre usan el núcleo real.
>
> `stage_text_without_irregularity(texto)` da el mismo tramo sin la sección (la tecla de comparar): su suelo es el del esqueleto.

### `docs/02-arquitectura.md` §6.6, «Física del tramo»: en la frase «Las secciones, como en el lote 4, más las que pide la cuerda del final de la banda (1 cm en planta, 2 mm en alzado; en un muro, planta × pendiente ≤ 2 mm).», añadir al final:

> Con la irregularidad, las cotas analíticas de sus ondas entran en la cuerda de cada tramo entre marcas. La onda corta del borde exterior del arcén tiene su propia cuerda en planta, 5 cm (`kStageChordEdge`: separa el arcén de la cuneta, fuera de la calzada; es el error de borde que admite el dibujo). Las demás líneas siguen con 1 cm, y el alzado con 2 mm.

### `docs/02-arquitectura.md` §6.6, al final de la línea **Cifras**, añadir:

> `lazadas_irregular.stage` (el mismo esqueleto con la irregularidad, apagada entre los brazos de las lazadas): 1565 secciones, 268 858 triángulos, carga de 0,69 s y 4,8 MB; muro más empinado, 77,9°. Paso de 0,047 ms de media y 0,096 ms de p99, frente a 0,045 / 0,099 ms en `lazadas`. Validación: \|d''\| = 0,0043, \|z''\| = 0,0026 m⁻¹ y \|β'\| = 0,12 %/m medidos.

## Pide de lo compartido

- **Plataforma (`panel`, apartado 5; `mensajes/lote-7-anexo-panel.md`).** `platform/stage_mesh.cpp`, `build_stage_lines`, dibuja las líneas de salida, parciales y meta con los semianchos del esqueleto. Con la irregularidad quedan desplazadas hasta ~0,6 m sobre la calzada real. Que use `StageTrack::road(s)` (`pose.half_width_*` ya son los reales desde el eje).
- **Plataforma (`panel`).** `rally.exe --bench-stage` solo mide `data/stages/lazadas.stage`. Para medir el dibujo de `lazadas_irregular` (268 858 triángulos, frente a 205 902) tendría que aceptar `--tramo`.
