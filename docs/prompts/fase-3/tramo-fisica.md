# Tarea `tramo-fisica` (lote 4, carril sim): física del tramo y seguimiento en `SimWorld`

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-4.md` §2.1, §2.3, §2.5 y §2.6, que mandan sobre este prompt. Sale de `integracion` con `tramo-seguimiento` ya fusionada.

## Por qué

La fase 3 carga la física del tramo entera antes de la salida: carretera y franja de terreno de ±60 m, en lote (`02` §6.6, `05` «Carga del tramo»). Más allá no hay colisión y la regla de atajos recoloca. Esta tarea:
- **mide** si el diseño de §2.6 cabe en el equipo mínimo;
- **construye** la física del tramo;
- **mete** `StageRun` en `SimWorld`.

No dibuja nada ni cambia el juego: el dibujo y el modo «tramo» son del lote 5.

## Parte A — Medida (antes de construir del todo)

Un prototipo que basta para medir, en tests o en un ejecutable de medida de `tests/sim`, con dos variantes:
- **M (el diseño):** calzada y arcén en malla, terreno en baldosas de rejilla (§2.6);
- **R (referencia):** todo en rejilla de 0,75 m, calzada incluida, con la superficie por celda, como la carretera de la 2.6b.

Con las dos, sobre `lazadas.stage` y sobre un tramo sintético de 30 km (generado en el test: curvas de R 15–300 m, horquillas y lazadas cada ~2 km, subidas y bajadas):
1. coste por paso con el 205 a 50–90 km/h siguiendo el eje (conductor de test: dirección por el error de rumbo y de `t`, acelerador para una velocidad objetivo), media y p99;
2. `build()` + `seal()`;
3. memoria de las formas de Jolt (`GetStats().mSizeBytes`) y número de triángulos y de muestras;
4. en M, con baldosas de 0,75 m y de 1,5 m.

**Decisión:** la regla de §2.6. Las cifras van al informe y a una fila de hallazgos del fragmento. Si sale «Necesita consultor», paras ahí con las cifras.

## Parte B — Construcción

1. **Geometría pura** (`src/sim/stage_ground.*`): de un `StageTrack` salen los vértices y triángulos del núcleo, con su superficie, y las muestras de cada baldosa del terreno. Sin Jolt en su encabezado: el dibujo del lote 5 la usará tal cual, para que lo que se ve sea lo que se pisa.
2. **Terreno del formato 1:** los requisitos de §2.6. El plan elige el método (se recomienda el de §2.6) y explica cómo cumple cada requisito.
3. **Jolt:** un cuerpo estático con una `StaticCompoundShape` (malla o mallas del núcleo y baldosas), creado en `build()` antes de los cuerpos de la descripción. Cada forma lleva su `SurfaceMaterial` (`MicroRelief::Noise`).
   - Comprueba cómo trata el tester de las ruedas (`CarWheelTester`) las aristas interiores de la malla (`EActiveEdgeMode`, `mActiveEdgeCosThresholdAngle`): una arista entre dos triángulos casi coplanarios no puede dar una normal de arista.
4. **`SimWorld`** (§2.5): `WorldDesc::stage_text`, `stage_spawn`, `stage()`, `stage_run()`, `stage_ground_hash()` y `stage_ground_bytes()`; recolocación y atajos con `StageRun`; `StageRunState` en `state_hash()`; `probe_ground` también en el tramo.
5. **`kExpectedStageGroundHash`** (nuevo): FNV-1a de la geometría de colisión de `lazadas.stage` (vértices, triángulos con superficie y muestras de las baldosas), igual en Debug y en Release. Lo creas tú y se actualiza como la huella.
6. **`02` §6.6:** la física del tramo como quedó (núcleo, baldosas, cifras de la parte A), para el integrador, en tu fragmento.

Reutiliza lo que hay: `StageTrack` y `StageRun`; el tester y los materiales de `sim_world.cpp`; `ground_hash`; los mundos y el contador de `operator new` de `tests/common` y `tests/sim/common`; el conductor de test que ya usen las pruebas del coche, si sirve. Si escribes una utilidad nueva, el plan dice qué buscaste y por qué no sirve lo que hay.

## Criterios (tests en `tests/sim/`, Release y Debug)

1. **Geometría del núcleo:**
   - sus vértices están sobre el eje de `StageTrack::pose` y a ±semiancho y ±(semiancho + arcén), con el peralte, a ≤ 1 mm;
   - el error de cuerda, medido en el punto medio de cada tramo de malla, es ≤ 1 cm en planta y ≤ 2 mm en alzado.
2. **Superficies:** un rayo vertical (`probe_ground`) da la superficie de `[surface]` en la calzada, arena en el arcén y tierra fuera, en 200 puntos por cada una, también a 5 cm de cada borde.
3. **Sin juntas bajo la calzada:** el 205 en recta y en la curva peraltada de `lazadas.stage`, a 30, 60 y 90 km/h, sin dirección ni freno, durante 3 s:
   - ninguna rueda pierde la carga ni un solo paso;
   - el salto de carga entre pasos no pasa en más del 1 % de la carga estática el del mismo coche, a la misma velocidad, sobre una losa de la misma superficie con su microrrelieve (la referencia; el informe da las dos cifras).
4. **Terreno:** cada requisito de §2.6 con su test:
   - 2 cm bajo el núcleo;
   - borde sin escalón: medido en la rejilla y con el coche saliendo del arcén a 30 km/h en recta y en la curva peraltada (sin pérdida de carga ni impacto > 1 m/s);
   - llano lejos de otras pasadas;
   - entre los brazos de las lazadas, sin crestas ni hoyos (requisito 5), en al menos 20 rectas entre puntos enfrentados;
   - las baldosas vecinas dan la misma altura en su borde común;
   - sin colisión más allá de 60 m: un rayo a 61 m del eje no toca nada y a 59 m sí.
5. **Seguimiento en el mundo:**
   - recolocar con el botón deja el coche en `stage_spawn(reset_s)`, quieto y en carretera, en el mismo paso;
   - un atajo conducido recoloca en `exit_s`. En `lazadas.stage` no se puede conducir (el tramo sube y el talud entre brazos es muy empinado), así que va en un tramo de test llano con una horquilla de brazos largos: el coche cruza por el terreno de un brazo al otro;
   - el conductor de test completa `lazadas.stage` sin salirse: `start_step`, parciales (con un tramo de test que los tenga) y `finish_step`.
6. **Reproducibilidad:** la misma secuencia de entradas en el tramo da el mismo `state_hash` en dos pasadas y en Debug y Release. Para cada campo de `StageRunState`, un test que falla si se quita del hash.
7. **Sin reservas** en el paso, con el coche en el tramo (contador de `operator new`). Las de Jolt se cuentan aparte, como en la zona, y no cambian las 1990 de la huella.
8. **Presupuestos de §2.6:** los mides en Release y los escribes en el informe. En los tests, como mucho un límite holgado que no falle por ruido.
9. **Sin cambios:** huella, reservas (1990), hash de la telemetría, `kSimVersion`, `kExpectedStageHash` y todas las grabaciones de `tests/data/recordings` («hash OK»).

## Regímenes

Cada uno con su test: parado sobre la calzada con y sin peralte; arranque; crucero en recta y en curva; saliendo y volviendo del arcén; en el terreno; en el aire y aterrizando (la cresta de `lazadas.stage`); recolocar (botón y atajo); fuera de la franja (el coche cae: sin colisión, sin abortar); en las prolongaciones y pasada la meta.

## Para el informe

- Las cifras de la parte A, M frente a R, y la decisión.
- Memoria y `build()` + `seal()` de `lazadas.stage` y del tramo de 30 km; triángulos y muestras.
- Lo que el lote 5 necesitará de `stage_ground.h` para el dibujo por ventanas: dilo, no lo hagas.
- Nada que probar jugando: el tramo no se conduce hasta el lote 5. No hay A/B.
