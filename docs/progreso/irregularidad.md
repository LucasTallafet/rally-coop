<!-- Fragmento de la tarea `irregularidad` (lote 7). Estado: parada prevista tras la parte A
(«Revisión del consultor tras la parte A»); se completa en la parte B. -->

## Fila del registro

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-13 | 3 | | **Lote 7, `irregularidad` (parte 0 y parte A; parada prevista).** Parte 0: los tres tests de la costura fijan cuántos casos aviso excluyen y su peor valor, por tramo (154/23/4 lados; 523,07 mm; 0,0 mm; 94/60/34 puntos al azar en Release y 8/4/3 en Debug), y fallan con todo en aviso. Parte A: capa de irregularidad (`sim/stage_irregularity`, 7 canales de 4 ondas con PCG, escala por s con paso suave y apagado en curvas de R < 30 m para d, ω y ε), formato de tramo 2 con `[irregularity]` opcional, `StageTrack::road`/`road_edges` (borde real en «en carretera», microrrelieve, salida y recolocación), `stage_text_without_irregularity` y `data/stages/lazadas_irregular.stage`. P1: `lazadas` en formato 2 con la sección a cero da `0xacdcd1b800bb9d7d` y `0x3d08dcb86b41ad6f`. Carga ×2,43 (1,47 s; 3110 secciones, 534 603 triángulos, 8,7 MB), paso ×1,27 (0,056 ms). Tests 715 → (parte B). Huella, reservas, telemetría y `kSimVersion` sin cambios. Sin validar a ojo | Respuesta del consultor a la parte A; después, parte B |

## Hallazgos

| Fecha | Prueba | Resultado | Decisión afectada |
| --- | --- | --- | --- |
| 2026-11-13 | `irregularidad`, parte A: la onda corta del borde en las horquillas | Con la onda corta del arcén (±12 cm, λ 4–8 m), `lazadas` no carga en s 739,7 («no cabe el talud entre pasadas»): en la horquilla de R 6 quedan 0,9 m entre el arcén interior y el centro de la curva. El sintético de 30 km, igual (s 4689,7, R 8–10). Se apaga también ε donde |κ| > 1/30 (como d y ω) y carga, con el muro entre los brazos a 78,50° (antes 77,92°) | `05`, Capa de irregularidad (límites): el apagado en curvas cerradas alcanza también los bordes |
| 2026-11-13 | `irregularidad`, parte A: coste de la onda corta del borde | La cuerda de 1 cm en planta con ε'' ≤ 0,25 m⁻¹ (cota) pide secciones cada ~0,6 m: 3110 frente a 1199 en `lazadas` (la onda corta sola, 3048); carga ×2,43, Jolt ×2,28 | Aviso de P5 (> ×2): la decide el consultor |
| 2026-11-13 | `irregularidad`, parte A: escala entre puntos | La escala por s va con un paso suave entre puntos, no lineal: con lineal, d', z' y β' saltan en cada punto (1,4° de quiebro en el borde con 25 cm en 10 m) y esos saltos no están en `breaks()` | Prompt de `irregularidad` («lineal entre puntos»): cambio técnico aprobado por la revisión del plan |

## Ediciones de `docs/02`, `07` y `08`

### `docs/02-arquitectura.md` §6.1, tras el párrafo «Formato 1 (`.stage`, lote 3)…» y su lista de reglas y de geometría (antes de «### 6.2»), añadir:

> **Formato 2 (lote 7): la capa de irregularidad.** `format = 2` admite la sección opcional `[irregularity]` (en un `format = 1` es un error); sin ella, un formato 2 es el formato 1. `kStageFormatVersion = 2` es el mayor que se lee, y `StageAxis::hash()` empieza por el formato del archivo (un formato 1 da el hash de siempre) y, en el formato 2, acaba con la sección. Una línea por palabra, cada componente opcional (amplitud 0 si falta) y como mucho una vez:
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
> A ≥ 0 y 1 m ≤ λmin ≤ λmax. Siete canales (d, ω y ε de cada lado, β y z), cada uno `σ(s)·G(s)·W(s)` (`sim/stage_irregularity.h`): W, 4 ondas `a·sin(k·s + φ)` con `Pcg32(seed, canal)`, λ log-uniforme una por cada cuarto de [log λmin, log λmax], φ uniforme y a ∝ λ con Σa = A (el pico nunca pasa de A y las ondas cortas, que son las que curvan, llevan menos); σ, la escala, con un paso suave (3u² − 2u³) entre dos puntos, no lineal, para que d', z' y β' no salten (una rampa son dos puntos), constante fuera; G, el apagado en curvas (d, ω y ε), un paso suave de 45 m sobre la distancia en s al conjunto donde |κ del esqueleto| > 1/30 m⁻¹. Se calcula al cargar; evaluar no reserva.
>
> El esqueleto no cambia (`s`, `t`, `pose`, `project`, la tablet y las notas). La calzada real va sobre la normal del esqueleto (`StageTrack::road`, `road_edges`): centro en t = d, calzada de d − (w_izq + ω_izq) a d + (w_der + ω_der), arcenes de 1 + ε, y el plano Y = y0 + (peralte + β)·t con y0 = y + z − β·d (el centro queda a y + z + peralte·d). Lo usan la malla, «en carretera», el microrrelieve, `e(s)`, la salida y la recolocación (en el centro real, con el rumbo de la línea central). Validación al cargar («s = …: …», muestreando cada canal a ≤ λmin/16): |d''| ≤ 0,01 m⁻¹, |z''| ≤ 0,0033 m⁻¹, |β'| ≤ 1,5 %/m, el eje del esqueleto dentro de la calzada y arcenes ≥ 0,5 m; las validaciones de siempre, con el núcleo real. `stage_text_without_irregularity(texto)` da el mismo tramo sin la sección (la tecla de comparar): su suelo es el del esqueleto.

### `docs/02-arquitectura.md` §6.6, «Física del tramo», al final de la línea **Cifras**, añadir:

> `lazadas_irregular.stage` (el mismo esqueleto con la irregularidad): 3110 secciones (la onda corta del borde pide una cada ~0,6 m por la cuerda de 1 cm), 534 603 triángulos, carga de 1,47 s y 8,7 MB; muro más empinado, 78,5°; paso 0,056 / 0,109 ms (media / p99) frente a 0,044 / 0,096 en `lazadas`.

(La parte B completa estas cifras y añade los tests.)

## Pide de lo compartido

- **Plataforma (`panel` o `tecla-ab`):** `platform/stage_mesh.cpp`, `build_stage_lines` (líneas de salida, parciales y meta), usa los semianchos del esqueleto: con la irregularidad, las líneas quedan desplazadas hasta |d| + |ω| ≈ 0,6 m sobre la calzada real. Que use `StageTrack::road(s)` (centro y semianchos reales, `pose.half_width_*` ya reales desde el eje).
- **Plataforma:** `rally.exe --bench-stage` mide solo `data/stages/lazadas.stage`; para medir el dibujo de `lazadas_irregular` (534 603 triángulos frente a 205 902) tendría que aceptar `--tramo`.
