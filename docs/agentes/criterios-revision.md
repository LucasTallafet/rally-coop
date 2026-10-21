# Criterios de revisión

Los usan `revisor-diseno`, `verificador` y `revisor-determinismo` (`docs/09-agentes.md`). Salen de lo que el consultor revisaba a mano de la 2.1 a la 2.7. Los mantiene el consultor: un agente no los cambia.

## 1. Antes de que el carril planifique (revisión de la tarea)

Lee el prompt de la tarea, el lote, `05-decisiones.md` y el código que va a tocar. Contesta:

1. **¿Choca con una decisión cerrada de `05`?** → **Necesita consultor**, con la cita de la decisión.
2. **¿Pide algo de otra fase o algo genérico «por si acaso»?** → que se quite, salvo que sea un gancho estructural de coste casi nulo que evita rehacer trabajo; si lo es, dilo.
3. **¿El prompt da por existentes cosas que el código no tiene, o al revés?** Por ejemplo, un campo que la telemetría no lleva o una tecla ya usada. → Dilo con la ruta y la línea, y cómo sacarlo sin tocar lo que no es de la tarea (en la 2.7, la aceleración vertical se calculó en la plataforma, sin columna nueva).
4. **¿Algún valor del prompt sale de un supuesto que no vale para todos los casos?** En la 2.7: «un 4 cilindros» para cuatro coches, cuando uno es de 5. → Corrígelo con el dato.
5. **¿Los criterios de aceptación son alcanzables y miden lo que dicen?** Calcula los umbrales cuando se pueda (en la 2.1, el 3,5× no era alcanzable con tramos de 12 B). Un test del coche entero solo comprueba límites; lo de un componente se prueba con el componente solo.
6. **¿Hay regímenes sin cubrir?** En la física del coche, y en todo lo que lee la simulación por paso: parado, arranque, baja velocidad, crucero, saturación, en el aire, aterrizaje y recolocar. Un único mecanismo y un test por régimen.
7. **¿Hay sucesos más cortos que un fotograma** (cambios, golpes, pulsaciones) que pasan de la simulación a la plataforma o al audio? → Que viajen como contadores, no como un sí o un no por fotograma.
8. **¿Toca el modo a ciegas?** → Nada que dependa de la superficie puede salir por la pantalla, el sonido ni la vibración, salvo por la física del coche. Ojo con los canales que llevan la superficie de forma indirecta: el microrrelieve en la suspensión, por ejemplo.
9. **Teclas:** libres en `07` §8 y no reservadas en los prompts siguientes (`comun.md` §6).
10. **Coste:** cuánto añade a ms por paso (≤ 0,5 ms), a draw calls, a CPU de render y a memoria en el equipo mínimo (`03`). Si no se sabe, que el plan lo mida.
11. **¿Toca algo con lo que se mide el criterio de una fase abierta?** Por ejemplo, la carretera de prueba de la 2.6b hasta la 2.11. → Que no lo cambie: lo nuevo va al lado (`09-agentes.md` §2).
12. **¿Choca con otra tarea del lote?** Un mismo formato, una misma interfaz, las mismas grabaciones de test. → Que use los números de versión y las interfaces del contrato del lote. Si el contrato no los fija, **Necesita consultor**.
12b. **¿Cambia una fuente de un archivo generado o de un hash de referencia?** (tablas de teclas → `docs/teclas.md`; tabla del CSV → `columns.md` y `kExpectedCsvHash`; `CarTelemetry` → `kExpectedTelemetryHash`). → Que la misma rama actualice el generado o el hash; si no es suyo, **Necesita consultor** (`09` §4).
13. **¿Reutiliza lo que ya existe?** Antes de aprobar, busca en `src/` y `tests/` funciones o tipos que ya hagan lo que la tarea pide (`rmath`, `fnv1a`, `input_shaping`, `atomic_file`, `PlayerSettings`, `screen_layout`, las tablas de teclas, los mundos de `car_worlds.h`…). Si el prompt o la tarea llevan a duplicar algo, dilo con la ruta y la línea. Nada genérico nuevo «por si acaso».
14. **¿Se juzga conduciendo?** → Que el informe diga cómo hace Lucas el A/B en el mismo ejecutable (una tecla, el archivo del coche de `master` al lado), o por qué no se puede.

**Formato de `<tarea>-revision.md`:**
- veredicto: **Adelante**, **Adelante con cambios** o **Necesita consultor**;
- los cambios numerados, cada uno con su motivo y, si hay, su cifra;
- lo que no cambia.

## 2. Después del plan del carril

1. ¿Aplica todos los cambios de la revisión?
2. ¿Están las cifras calculadas? Comprueba al menos dos a mano: fórmulas, signos y unidades.
3. ¿La tabla de regímenes está completa?
4. ¿Cambia algo que no le toca (rutas, decisiones, umbrales)? **Los umbrales y los valores esperados no se relajan sin una razón física explicada.**
5. ¿Hay preguntas del carril? Contéstalas si son técnicas y caben en las reglas. Si son de gusto, de prioridad o de diseño, van a Lucas o al consultor.
6. **Cada utilidad nueva del plan** (función, tipo o archivo de ayuda) lleva una línea: qué buscó en el código y por qué no sirve lo que hay. Sin esa línea, **Aprobado con cambios**.

**Formato de `<tarea>-revision-plan.md`:** **Aprobado**, **Aprobado con cambios** o **Necesita consultor**, con los cambios numerados.

## 3. Verificación (verificador)

1. `tools/ci/comprobar.ps1` en Release y en Debug, con los tests completos y no por partes.
2. **Cada test nuevo mide lo que dice:**
   - rompe a propósito lo que mide (un signo, un campo del SaveState) y comprueba que falla;
   - después, déjalo como estaba.
3. **Un test que deja de medir** (por ejemplo, el coche ya parado antes del pulso) se arregla o se quita explicándolo; nunca se queda pasando sin medir.
4. **Rendimiento:**
   - `rally_replay run tests/data/recordings/bench_lap.rrec --bench 10` (≤ 0,5 ms por paso de media);
   - si la tarea dibuja, las draw calls y la CPU de render de la escena que diga el lote.

   Compara con la fila anterior de `progreso.md`.
5. **Informe en `<tarea>-verificacion.md`:** **Pasa** o **No pasa**, con las cifras.

## 4. Determinismo (revisor-determinismo)

Se activa si la rama toca `src/sim`, `src/gen`, `src/net`, `src/game` o `src/io`, o si cambia la huella, el hash de la telemetría o `tests/data/recordings`.

1. **Matemática** solo por `rmath`; aleatorios solo con PCG32; nada de `<random>`, `rand()`, `unordered_*` ni reloj en `src/sim`, `src/gen` ni `src/net`.
2. **Nada se añade ni se quita de Jolt** tras `seal()`. Cero reservas nuestras en el paso (test del contador de `operator new`).
3. **Todo estado nuevo de la simulación entra en el SaveState,** también el de `src/game` que lo necesite (acciones, cambio de host). Para cada campo nuevo, comprueba que existe un test que falla si se quita.
4. **Huella, reservas de Jolt, hash de la telemetría y `kSimVersion`:**
   - si cambian, el informe del carril tiene que decir por qué;
   - la huella tiene que ser igual en Debug y en Release;
   - las grabaciones de `tests/data/recordings` tienen que estar regeneradas.
5. **Nada de la simulación lee la plataforma, el render ni el audio.**
6. **Informe en `<tarea>-determinismo.md`:** **Pasa** o **No pasa**, con la línea de cada problema.
