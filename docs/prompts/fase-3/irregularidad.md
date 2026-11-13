# Tarea `irregularidad` (lote 7, carril sim, **riesgo alto**): capa de irregularidad del trazado

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-7.md` §2, que manda sobre este prompt. Criterio de gusto: `docs/agentes/criterio-de-lucas.md`. **Riesgo alto** (`mensajes/lote-7.md` §3): parte 0, parte A con las premisas comprobadas en cifras, y revisión prevista del consultor tras la parte A, que no cuenta como fallo.

## Por qué

Una carretera de rally no es una sucesión de rectas y arcos perfectos (`docs/01` §7.3, `docs/02` §6.1, `docs/05` «Trazado»). El criterio de la fase 3 pide que, con la irregularidad activada, el tramo se sienta una carretera real, y que Lucas y su compañero lo juzguen comparando con una tecla (`docs/04`, fase 3). El tramo de 3–5 km del lote 8 se escribe ya con ella.

El lote 6 dejó el gancho: la banda de entorno se construye desde los vértices del borde del núcleo, no desde los semianchos del eje (`docs/02` §6.6).

## Parte 0 (antes de tocar nada de la banda): tope de los tests con exclusiones

Reserva del verificador del lote 6 y criterio 17 de `docs/agentes/criterios-revision.md`: los tests de la costura (7b, el barrido del invariante 7 y el del 7 con puntos al azar) excluyen los casos aviso sin tope, y pasarían con todos los lados excluidos.

- Cada uno de los tres fija, por tramo, **cuántos lados excluye** y **su peor valor**, y falla si cualquiera de los dos empeora. Cifras de partida del informe de `formato-2` (compruébalas; si no coinciden con lo que miden hoy los tests, pon las medidas y dilo): 154 lados en `lazadas`, 23 en `hairpin_flat` y 4 en el peralte del 10 %; peor escalón, 523,1 mm; peor margen, 0,0 mm; 2 puntos al azar con la rejilla encima (s 951,4).
- «Falla cuando debe»: fuerza a mano que el criterio de aviso lo marque todo, y comprueba que los tres tests fallan. Restáuralo.
- Va en su propio commit, el primero de la rama, con `comprobar.ps1` en verde. No cambia ningún hash.

## Qué se construye (el resultado; el cómo lo propones tú en la parte A)

1. **El esqueleto no cambia.** `s`, `t`, `StageAxis::pose`, `StageTrack::pose`, `project`, `acquire`, los cambios de dirección y la tablet siguen sobre las primitivas. La irregularidad solo mueve la malla, la colisión y lo que depende del borde real.
2. **Cinco componentes, funciones de `s`, deterministas y calculadas al cargar** (`docs/02` §6.1):
   - desplazamiento lateral del eje de la calzada, d(s);
   - semiancho izquierdo y derecho, con una parte de onda larga (ancho variable) y otra corta (bordes irregulares);
   - peralte añadido;
   - ondulación vertical añadida, z(s).

   Son sumas de pocas ondas sembradas con el PCG propio desde la semilla que da el archivo. Matemática por `rmath`. Nada de esto se evalúa con reservas en el paso.
3. **El archivo.** Formato de tramo 2: `format = 2` admite una sección `[irregularity]` opcional, con la semilla, las amplitudes y longitudes de onda de cada componente, y una **escala por `s`** (puntos `s  escala`, lineal entre puntos, como `[bank]`), que multiplica todas las amplitudes. La escala sirve para dos cosas: apagar la irregularidad donde no cabe (entre los brazos de unas lazadas) y subirla en un trecho en mal estado (decisión de Lucas, 2026-11-12: el asfalto en mal estado es irregularidad, no una superficie nueva). Los nombres de las claves, en inglés, los propones tú.
4. **`format = 1` sigue cargando, y da exactamente el suelo de hoy.** Un tramo sin sección, o con todas las amplitudes a 0, da los mismos bits que antes (contrato, y criterio 1).
5. **Lo que depende del borde real usa el borde real:** «en carretera» (`StageProjection::on_road`, `on_any_road`), el marco del microrrelieve (`stage_relief_frame`), la validación de la banda (`stage_band_error`) y la salida y la recolocación del coche (`stage_spawn`: el coche sale centrado en la calzada real, a t = d(s)).
6. **Quitarla para comparar:** una función pura de `sim` que, dado el texto de un tramo, devuelve el texto del mismo tramo sin irregularidad (de partida: quita la sección; propón tú la forma). El mundo se construye con un texto o con el otro, y la grabación guarda el texto que se usó: el formato `.rrec` no cambia. La tecla es de plataforma (tarea `panel`).
7. **Un tramo de prueba nuevo, `data/stages/lazadas_irregular.stage`:** el esqueleto de `lazadas` con `format = 2` y su sección. Es el que juega Lucas. `lazadas.stage` no se toca.

## Diseño de partida (cifras calculadas por el consultor; la parte A las comprueba)

Una onda de amplitud A y longitud λ tiene una curvatura máxima de A·(2π/λ)². De ahí salen tres límites, que son la razón de las premisas P2–P4:

| Componente | Rango de `docs/01` §7.3 | Límite de partida | De dónde sale |
| --- | --- | --- | --- |
| Eje, d(s) | De centímetros a algo más de 1 m, en 15–150 m | d'' ≤ 0,01 m⁻¹ (como un radio de 100 m): A ≤ 5,7 cm con λ = 15 m, 23 cm con 30 m, 1 m desde λ ≈ 63 m | Sin límite, 1 m en 15 m sería un radio de 5,7 m: una horquilla que no está en el esqueleto, ni en las notas ni en la tablet |
| Ondulación, z(s) | Decímetros, en 10–60 m | z'' ≤ 0,0033 m⁻¹: A ≤ 8 mm con λ = 10 m, 7,5 cm con 30 m, 30 cm con 60 m | A 30 m/s, v²·z'' = 3 m/s² (0,3 g). Sin límite, 10 cm en 10 m darían 3,6 g: un salto en cada onda |
| Bordes (onda corta del semiancho) | «Bordes irregulares» | ±10–20 cm, en 2–8 m, solo en el borde exterior del arcén | Que la calzada no cambie por dentro: el borde entre calzada y arcén solo lleva la onda larga |
| Ancho (onda larga) | «Ancho variable» | ±0,3 m, en 40–150 m | Calzada de 6 m (`docs/01`) |
| Peralte | «Peralte y bombeo que cambian» | ±2 %, en 30–150 m, con cambio de ritmo ≤ 1,5 %/m | El límite de la explanada del lote 6 (`docs/02` §6.6: 13,2/t_F %/m) |

Además, de partida, **d(s) y la onda larga del ancho se apagan suavemente donde la curvatura del esqueleto pasa de 1/30 m⁻¹** (en las curvas cerradas manda el esqueleto), y la validación comprueba la geometría ya desplazada.

Los valores por defecto de `lazadas_irregular.stage` los eliges tú dentro de esos límites, de forma que la irregularidad **se note** conduciendo (criterio de Lucas: baches, bordes rotos, peralte que cambia y ondulaciones largas). Ponlos en el informe.

## Contratos (de `mensajes/lote-7.md` §2; no se negocian en el plan)

- No cambian: `kSimVersion`, la huella, las reservas de Jolt, el hash de la telemetría, `kExpectedStageHash`, `kExpectedStageGroundHash`, `kExpectedStageStateHash`, `stage_lazadas.rrec`, el formato `.rrec` (8), el CSV ni el protocolo de red. Todos se miden sobre `lazadas.stage`, que es `format = 1`.
- `StageAxis::hash()` de un tramo `format = 1` da lo mismo que hoy. En `format = 2`, los datos de la sección entran en el hash.
- Tests nuevos con hashes propios del tramo irregular: son tuyos (`tests/sim/`).
- Las interfaces del esqueleto que usan la tablet, los cambios de dirección y la red solo pueden ganar cosas.
- La grabación de test nueva, si la necesitas, la genera tu generador, como `stage_lazadas.rrec`.

## Premisas (la parte A las comprueba con cifras antes de construir)

| # | Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- | --- |
| P1 | Con amplitudes 0, el suelo sale idéntico bit a bit | `kExpectedStageGroundHash` y `kExpectedStageStateHash` de `lazadas` sin cambios, con el camino de código nuevo activo (sección presente y todo a 0) | Parada: el diseño mezcla la irregularidad con el esqueleto |
| P2 | Con el límite de d'', la malla no se pliega y la validación de hoy vale sobre la geometría desplazada | Mayor \|κ efectiva\|·(semiancho + arcén + 0,5) en `lazadas_irregular` y en el sintético de 30 km con la sección; 0 triángulos invertidos (P2 de `formato-2`) | Baja el límite o amplía el apagado en curvas, y dilo con la cifra |
| P3 | Con el límite de z'', la ondulación no hace saltar al coche en la calzada | El conductor de los tests por `lazadas_irregular` a 60, 90 y 120 km/h en las rectas: % de pasos-rueda sin contacto y p99 del salto de carga, en la calzada, frente a `lazadas` en la misma ejecución | Baja el límite. El umbral del criterio 3 lo fija el consultor con estas cifras |
| P4 | La banda cabe con la irregularidad | `stage_band_error` vacío en `lazadas_irregular`; P1 de `formato-2` (muro ≤ 80°; hoy 77,92° con 2,1° de margen en s 657,75) con la escala que hayas puesto entre los brazos (s ≈ 630–960) | Pon la escala a 0 en ese trecho, que para eso está. Si ni así carga, parada |
| P5 | Las secciones de la malla siguen cumpliendo la cuerda (1 cm en planta, 2 mm en alzado) sin dispararse en número | Secciones, triángulos, MB y carga de `lazadas_irregular` frente a `lazadas` (hoy: 0,58 s y 3,8 MB) | Aviso con la cifra si la carga pasa de ×2; parada si P7 falla |
| P6 | «En carretera» con el borde real cuesta casi nada en el paso | `bench` del paso en `lazadas_irregular` frente a `lazadas`, media y p99, en la misma ejecución; 0 reservas en el paso | Tabla precalculada al cargar. Parada solo si P7 falla |
| P7 | El paso sigue por debajo de 0,5 ms (`docs/03`) | La misma medida de P6 | Parada |
| P8 | Los tests de la costura (parte 0) no empeoran en `lazadas` | Sus cifras, sin cambios (el suelo de `lazadas` es idéntico) | Parada: P1 es falsa |
| P9 | En `lazadas_irregular`, la costura cumple lo mismo fuera de los casos aviso | 7b (≤ 40 mm) y el invariante 7 sobre `lazadas_irregular`, con sus exclusiones contadas | Informa de la lista. Lo decide el consultor tras la parte A: no iteres sobre la costura |

## Criterios (parte B, tests en `tests/sim/`, Release y Debug)

De resultado (deciden la tarea):
1. **Regresión cero:** P1 como test permanente.
2. **Reproducibilidad:** la misma semilla da el mismo suelo (hash) dos veces y en Debug y Release; otra semilla da otro. La pasada del conductor por `lazadas_irregular` da el mismo `state_hash` dos veces.
3. **La calzada se nota y no hace saltar:** el umbral lo fija el consultor tras la parte A, con las cifras de P3.
4. **El conductor de los tests completa `lazadas_irregular`** sin recolocaciones que no tenga en `lazadas`.
5. **Sin irregularidad es el mismo tramo:** el texto que devuelve la función del punto 6 da el mismo `StageGround::hash()` que `lazadas.stage`.

De diagnóstico (se informan; si fallan por poco, «Lista con avisos»):
6. La costura en `lazadas_irregular` (P9).
7. La carga y la memoria (P5).
8. El espectro de lo construido: amplitud y longitud de onda medidas de cada componente sobre la malla, frente a lo pedido en el archivo.

## Paradas

Solo estas:
- tras la parte A, la revisión prevista del consultor;
- P1, P7 o P8 son falsas;
- falla un criterio de resultado (1–5);
- falla un test de otro carril: no lo toques, «Necesita consultor».

Lo demás son avisos para el informe.

## Para el informe

- Parte 0: las cifras fijadas y la prueba de que los tests fallan cuando deben.
- Parte A: la tabla de premisas con sus cifras, el diseño que propones (cómo se evalúa cada componente y dónde se guarda), y la sección de `lazadas_irregular.stage`.
- Parte B: los criterios, los hashes nuevos del tramo irregular, la carga y el paso.
- Fragmento `docs/progreso/irregularidad.md`: la edición de `docs/02` §6.1 y §6.6 (formato 2 del archivo, límites y apagado en curvas) y de `docs/07` si cambia algo del formato.
- Para la prueba de Lucas: en qué `s` de `lazadas_irregular` se nota más cada componente, para que sepa dónde mirar.
