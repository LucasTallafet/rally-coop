# Tarea `formato-2` (lote 6, carril sim, **riesgo alto**): banda de entorno cosida al núcleo

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-6.md` §2, que manda sobre este prompt. **Riesgo alto** (`mensajes/lote-6.md` §3): parte A con las premisas comprobadas en cifras, y revisión prevista del consultor tras la parte A, que no cuenta como fallo.

## Por qué

En la prueba del lote 5, Lucas notó botes y mucha pérdida de tracción fuera de la calzada (`mensajes/lote-5/prueba/conclusiones-consultor.md`). En la banda de 0–4 m fuera del borde, la rueda pierde el contacto el 3,4–5,2 % del tiempo y el p99 del salto de carga por paso es de 1,1–2,1 kN, frente al 0,33 % y 0,82 kN en la calzada. El peor punto es el paso del arcén al terreno (1–2 m fuera), donde el lote 4 midió escalones de hasta 5,4 cm. Entre los brazos de las lazadas, la rueda pierde el contacto el 10–15 % del tiempo.

Es el terreno del formato 1 (`docs/02` §6.6): una rejilla de 0,75 m que nunca coincide con el borde de la malla, más un empalme de 4 m hasta un terreno natural que, entre pasadas apiladas, hace paredes de más de 1000 %. El formato 2 lleva el entorno cercano a la misma malla que el núcleo: cuneta, talud y, donde no cabe el talud, muro, con los vértices del borde del arcén compartidos.

## Qué se construye (el resultado; el cómo lo propones tú en la parte A)

1. **Banda de entorno en la malla del núcleo,** a cada lado de cada pasada, con las secciones del núcleo:
   - **cuneta** en coseno (de partida, 1,5 m de ancho y 0,25 m de fondo; constantes en `stage_ground.h`, como `kStageShoulderWidth`);
   - **talud** desde el borde exterior de la cuneta hasta el terreno natural, con la pendiente máxima del material (de partida, tierra: 1:1,5 = 33,7°; una constante por material, aunque hoy solo se use la tierra). Sube si el terreno natural está más alto (desmonte) y baja si está más bajo (terraplén);
   - **muro** donde no cabe el talud (pasadas apiladas): la pendiente que haga falta, **como mucho 80°**. Superficie física de tierra (`Surface::Dirt`) y aspecto de muro para el dibujo (decisión de Lucas, 2026-11-08; aviso aceptado: al rozarlo, la vibración, el sonido y la telemetría dicen «tierra»).
2. **La banda sale de los vértices del borde del núcleo,** nunca de los semianchos de `StageAxis`. Es el gancho de la capa de irregularidad (lote 7): cuando esta mueva los bordes, la banda los seguirá sin rehacerse.
3. **Fuera de la banda,** la rejilla de baldosas sigue como hoy, pero las reglas que hoy se aplican junto al borde del núcleo (10 cm bajo la malla, la banda de ±1,06 m con e(s) y el empalme de 4 m) pasan al **borde exterior de la banda**. Bajo la banda, la rejilla queda bajo la malla.
4. **Un solo generador de suelo:** el terreno junto al borde del formato 1 desaparece. Los tests que miden sus avisos (escalón de 38,6 mm, ondulación de 7,2 mm, salto de zona) se sustituyen por los de este prompt; se quitan los que solo tenían sentido para el formato 1, cada uno nombrado en el informe con su motivo.
5. **`StageTriangle` gana dos campos,** que entran en `hash()`: `std::uint32_t section` (el índice de su sección en `sections()`) y `std::uint8_t look` (calzada, arcén, cuneta, talud o muro; un `enum class StageLook : std::uint8_t`). Los usa `tramo-dibujo-2` (plataforma), que sale con esta rama ya fusionada: el dibujo saca el trozo de `section` y el color del muro de `look`. **Hoy el dibujo supone 5 vértices por sección** (`platform/stage_chunks.cpp`, `min(a, b, c) / kStageSectionVertices`), así que tus vértices pueden ir en el orden que te convenga.
6. **Un tramo imposible no carga.** Si en alguna sección el muro necesita más de 80°, `StageGround::build` (o la validación al cargar, como `StageTrack::build`) falla con «s = …: no cabe el talud entre pasadas». Lo que se carga cumple todo esto.

**El archivo no cambia:** `.stage` sigue en `format = 1`, sin claves nuevas. Las cunetas y las pendientes son constantes hasta que el tramo de verdad (lote 7) pida variarlas.

## Contratos (de `mensajes/lote-6.md` §2.1; no se negocian en el plan)

- **Cambian:** `kExpectedStageGroundHash` y `kExpectedStageStateHash` (los dos, iguales en Debug y Release) y **`tests/data/recordings/stage_lazadas.rrec`, que regeneras tú en tu rama** con su generador (`RALLY_WRITE_STAGE_REC`, `tests/io/test_stage_v8.cpp`). Con el suelo nuevo, las entradas viejas sacan el coche de la carretera y `rally_replay run` avisa de que el tramo ha cambiado: no vale `rally_replay rewrite`. El integrador no la toca. La pasada de la grabación nueva (tiempo, recolocaciones = 1 y atajos = 0), en el informe.
- **No cambian:** `kExpectedStageHash` (el esqueleto), `kSimVersion`, la huella, las reservas de Jolt, el hash de la telemetría, el formato de la grabación (8), el CSV y el protocolo de red. **Las interfaces que usa la tablet** (`StageAxis::pose`, `StageTrack::pose`, `s_begin`, `s_end`, `core_half_width`, `project`, `acquire`) solo pueden ganar cosas, nunca cambiar ni perderlas.
- **No tocas** la zona, la carretera de prueba de la 2.6b (congelada hasta la 2.11) ni nada fuera de tus rutas. `tramo_salida.png` (captura de referencia de plataforma) cambiará: la regenera `tramo-dibujo-2`.
- Todo al cargar, sin hilos, con `rmath`. Nada en el paso.

## Premisas (la parte A las comprueba con cifras antes de construir)

| # | Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- | --- |
| P1 | El muro de ≤ 80° cabe entre los brazos de `lazadas` | Con los datos del tramo: los brazos van a 13,35 m entre ejes, y el desnivel entre puntos enfrentados llega a ~13,5 m (Δs = 2d + 28,85 m al 8 %). Entre el brazo de entrada y el central, el peor punto es el extremo del brazo de entrada (s ≈ 660), donde la calzada aún mide ~2,9 m de semiancho (pasa de 3,0 a 2,6 entre 650 y 700): quedan 13,35 − (2,9 + 1) − (2,6 + 1) ≈ 5,85 m entre arcenes y, con 2 cunetas de 1,5 m, ~2,85 m para el muro. Hacen falta ~4,7 = **~78°** (tan 80° = 5,67). Entre el brazo central y el de salida, ~73°. **La cifra exacta, sección a sección**, con el peor punto y su `s` | Parada: el margen es estrecho (~78° frente a 80°). El consultor decide entre un tope mayor, una cuneta más estrecha donde no cabe, o marcar el caso como de un tramo de test más extremo que los reales (`claude/consultor-forma-de-trabajo.md` §3.1, regla 10) |
| P2 | La banda no se pliega en el interior de las curvas cerradas | En la horquilla de R 6, el borde interior del arcén queda a 6 − 2,6 − 1 = 2,4 m del centro de curvatura, y la cuneta mide 1,5 m. Propón cómo se cierra el interior (recorte de la banda más un abanico hacia el centro, u otra cosa) y comprueba que ningún triángulo queda invertido ni con área nula en `lazadas` y `hairpin_flat` | Parada prevista |
| P3 | El suelo sigue siendo una función y(x, z) | Ninguna cara de más de 80° ni con la normal hacia abajo. El dibujo orienta cada triángulo hacia arriba (`stage_mesh`), `StageSurface::query` toma la altura mayor bajo un punto, y en Jolt las caras traseras de una malla no chocan | Parada |
| P4 | El paso no pasa del presupuesto | Paso con la banda frente a la carretera de la 2.6b **en la misma pasada** (`rally_stage_measure`, alternando como en el lote 4), en `lazadas` y en el sintético de 30 km. Límite: ≤ 0,5 ms por paso en el i3-8100 (`03`, «Simulación del coche»); el cociente frente a la 2.6b se informa | Parada solo si pasa de 0,5 ms |
| P5 | Memoria y carga razonables | MB de Jolt y tiempo de carga en `lazadas` y en el sintético de 30 km. Referencia: «decenas de MB» (`03`) y las cifras de `05`, «Carga del tramo» (30 km: 2,5 s y 17 MB) | Aviso con las cifras |
| P6 | La precisión de Jolt basta con un solo origen | Error de cuantización de la malla (21 bits sobre su caja) en `lazadas` y en los 30 km. Con 5 km (el tramo de verdad) se espera ~2,4 mm; los trozos con origen propio son de la fase 4 | Aviso con las cifras |

**Parte A:** el diseño que propones (cómo se construyen la cuneta, el talud, el muro, el cierre del interior de las curvas y la costura con la rejilla), las seis premisas con sus cifras y una tabla con los tests que se quitan y los que se añaden. Va a tu informe, y paras con «Revisión del consultor tras la parte A». No es un fallo.

## Criterios (parte B, tests en `tests/sim/`, Release y Debug)

**De resultado (deciden la tarea):**

1. **Salidas guionizadas** del 205 (el conductor de `tests/sim/common/stage_driver.h`) por el arcén, la cuneta y el talud, a 30, 60 y 90 km/h, en la recta de salida, en la curva de R 80 y por el interior de las dos horquillas (donde Lucas salió hasta 40 m en el lote 5). Para cada salida: % de rueda sin contacto y p99 del salto de carga por paso, en la banda de 0–4 m fuera del borde y en el resto de la banda.
   - **Referencia en la misma pasada:** el mismo recorrido con la malla 4 veces más densa (`kStageDenseMesh`, que también densifica la banda). Comparte toda la geometría salvo la discretización, así que separa lo que es geometría (la cuneta hace botar, y debe) de lo que son defectos de la malla o de las costuras.
   - **Umbral:** lo fija el consultor tras la parte A con tu cifra. El objetivo de partida es no peor que la referencia en más de un 10 %.
2. **Reproducibilidad:** `kExpectedStageGroundHash` y `kExpectedStageStateHash` iguales en Debug y Release; `stage_lazadas.rrec` regenerada da «hash OK».

**Invariantes del suelo (cada uno mide una sola cosa; en `lazadas`, `hairpin_flat` y el sintético de 30 km):**

3. La calzada y el arcén de cada pasada son siempre la superficie de arriba en su núcleo: rayos verticales en 10 000 puntos de los núcleos, ninguna cara del entorno por encima (0 mm).
4. Sin escalón entre arcén y cuneta: vértices compartidos, 0 mm salvo la cuantización de la malla (≤ 0,3 mm, la medida del lote 4).
5. Ninguna cara de más de 80°, invertida ni de área nula.
6. Cada cara cumple la pendiente de su material, salvo las de muro (≤ 80°), según su `look`.
7. La rejilla nunca asoma por encima de la banda (como el test del núcleo del lote 4, ahora con la banda).
8. Cada triángulo lleva la `section` cuya `s` está dentro del intervalo de sus vértices, y un `look` coherente con su posición (los de calzada y arcén, en el núcleo).
9. Un tramo sintético con dos pasadas que no caben ni con 80° no carga, y el error lleva la `s`.

**De diagnóstico** («Lista con avisos» si fallan por poco; decide el consultor):

10. Escalón máximo en cada costura del entorno (banda con banda, banda con rejilla) y dónde.
11. La tabla del lote 5 (rueda sin contacto y p99 del salto de carga por bandas de distancia al borde), repetida con las salidas guionizadas, junto a la del formato 1 por el mismo recorrido, medida en la parte A sobre el suelo de hoy, antes de cambiarlo (con un solo generador, después ya no se puede medir).

## Paradas

Solo: un criterio de resultado (1 o 2) que falla, o una premisa falsa que la tabla marca como parada (P1, P2, P3; P4 si pasa de 0,5 ms). Más la parada prevista tras la parte A. Lo demás son avisos.

## Para el informe

- La parte A: diseño, premisas con cifras y tests que se quitan y se añaden.
- Parte B: los criterios con sus cifras, los hashes nuevos y la pasada de `stage_lazadas.rrec`.
- Ediciones de `docs/02` §6.6 en tu fragmento: el formato 2 sustituye al terreno del formato 1, con sus cifras y sus avisos.
- **Para la prueba de Lucas:** sal del arcén a propósito en recta, en curva y en las horquillas, a varias velocidades. ¿Se nota todavía algún bote que no sea la cuneta? ¿La cuneta castiga lo justo? ¿Se lee el muro entre los brazos?
