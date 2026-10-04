# Fase 2 — Reglas comunes a todas las tareas

Cada prompt `2.X.md` da por leído este archivo. Si algo de un prompt choca con esto, manda el prompt; si choca con `docs/05-decisiones.md`, para y pregunta.

## 1. Al empezar la sesión

1. `CLAUDE.md` ya lo tienes cargado. Lee además `docs/progreso.md` (fase actual y las tres últimas filas del registro) y la sección "Fase 2 · Coche" de `docs/04-plan-de-fases.md`.
2. Comprueba que las tareas de las que depende la tuya están cerradas en `docs/progreso.md` (las dependencias están en `docs/prompts/fase-2/README.md`). Si no lo están, para y dímelo.
3. Comprueba que `master` está limpio y al día con `origin`. Trabaja en una copia de `.claude/worktrees/` con la rama `phase2/2.X-<nombre-corto>`.

## 2. Plan primero

Antes de escribir código, un plan corto (una pantalla como mucho):
- archivos nuevos y archivos que cambian, por módulo (`src/sim`, `src/platform`, `src/game`, `tools`, `tests`, `data`);
- tipos y funciones públicas nuevas, con su firma;
- tests que vas a escribir, con el valor esperado de cada uno;
- si la huella cambia y por qué;
- riesgos y preguntas abiertas: cualquier valor que no esté en el prompt ni en los documentos y que vayas a elegir tú.

Espera mi visto bueno si la tarea toca más de un módulo (casi todas). No empieces a escribir mientras esperas.

## 3. Reglas de la simulación (además de `CLAUDE.md` y `.claude/rules/simulacion.md`)

- **240 Hz fijos.** Las ruedas, con 4 subpasos dentro del paso, nunca con otro paso de Jolt.
- **Nada se añade ni se quita de Jolt tras `seal()`.** El coche, su `VehicleConstraint` y el step listener se añaden en `build()`. Cambiar de coche, de ajustes o de superficie de la pista ciega reconstruye el mundo entero. Recolocar es mover y poner a cero, nunca quitar y añadir.
- **Todo el estado del coche** va en `SaveState` / `RestoreState` del `VehicleController` propio. Fuera de Jolt solo quedan `ControlState` y los botones del paso anterior. Si añades estado y no lo guardas, el test de `SaveState` → `RestoreState` → mismos pasos tiene que fallar: escríbelo.
- **Matemática solo por `rmath`.** Si te falta una función (`tanh`, `hypot`...), añádela a `rmath` con su test bit a bit frente a `std::`. Aleatorios, solo PCG32. Sin reloj en `src/sim` ni `src/gen`; en `src/game` tampoco (cuenta pasos).
- **Sin reservas de memoria dentro del paso.** El test con el contador de `operator new` tiene que seguir dando 0 propias por paso, y las de Jolt, su total exacto.
- **Los datos se leen al cargar, nunca dentro del paso**, con `std::from_chars` (independiente del idioma de Windows). Una clave desconocida o una que falta aborta la carga con el número de línea (`RALLY_CHECK`).
- **La telemetría y todo lo que se dibuja o suena solo leen.** Nada de la plataforma escribe en la simulación salvo `SimInput` y la descripción del mundo antes de `build()`.

## 4. Huella, reservas y versión de la simulación

- El test de huella (`sim_world: state fingerprint...`) usa desde la 2.4 el coche congelado `tests/data/cars/reference.car`, nunca los de `data/cars/`. Ese archivo no se toca al ajustar el manejo (2.10). Solo lo cambian las tareas que cambian el código de la simulación (por ejemplo, al añadir claves nuevas), y entonces la huella cambia de todos modos.
- Si tu tarea cambia el resultado de la simulación **a propósito**: actualiza `kExpectedFingerprint` y `kExpectedJoltAllocations` y sube en 1 `kSimVersion` (constante que crea la 2.1). En el commit, una línea: `Fingerprint: 0x... -> 0x...; Jolt allocations: N -> M; sim version: A -> B`.
- Si la huella cambia **sin querer**, no la actualices: busca la causa.
- La huella tiene que ser igual en Debug y en Release.

## 5. Datos, telemetría y grabaciones

- Coches en `data/cars/*.car`: texto `clave = valor`, comentarios con `#`, unidades SI salvo que la clave diga otra (`_deg`, `_kmh`, `_rpm`, `_bar`). Cada valor nuevo que añadas lleva un comentario con su origen: "Evo 1, fuente", "estimado" o "valor de partida, se ajusta en 2.10".
- Cada tarea que añade estado al coche añade sus columnas a la telemetría (`CarTelemetry`) y a `tools/telemetry/columns.md`.
- Las grabaciones de prueba que sirvan de test van a `tests/data/recordings/`. Las de las sesiones de manejo, a `docs/manejo/sesiones/<fecha>/`.

## 6. Teclas

Antes de asignar una tecla, mira la tabla de `docs/07-entorno-y-portabilidad.md` §8 y la demo actual. Si añades o cambias teclas, actualiza esa tabla en el mismo commit. Nada de teclas de depuración en el mando.

## 7. Dependencias

Ninguna nueva sin preguntarme: tampoco paquetes de Python. Si una tarea la necesita, va en el plan como pregunta.

## 8. Puntos de parada

Si la tarea tiene un punto de parada humano, al terminar el código y los tests **no fusiones**. Escribe:

> Listo para probar: `<ruta del exe en la copia de trabajo>`. Comprueba: (lista del prompt). Preguntas: (lista del prompt).

Espera mi respuesta. Si lo que digo cambia algo, corrígelo en la misma copia y vuelve a parar.

## 9. Si algo no pasa

- No relajes un umbral ni cambies el valor esperado de un test para que pase. No desactives tests.
- Si un criterio no pasa, para y dame: el valor medido, el esperado, lo que has probado y la pregunta exacta que trae el prompt para ese riesgo.
- Si algo choca con una decisión cerrada de `docs/05-decisiones.md`, para y pregúntame.

## 10. Cierre de la tarea

1. Tests en verde en Release y en Debug (`ctest --preset windows-release` y `windows-debug`).
2. `clang-format --dry-run --Werror` sin diferencias sobre `src/`, `tests/` y el C++ de `tools/` (clang-format 23.1.2).
3. Fila nueva en el registro de `docs/progreso.md`, arriba del todo, con el estilo de las de la fase 1: qué se hizo, con cifras; tests antes → después; huella y reservas; validado a ojo por Lucas (o "sin validar a ojo", si la tarea no tiene parada). **La columna de horas, vacía.** "Siguiente paso": la próxima tarea según el README.
4. Si una prueba confirma o cambia una decisión provisional, propón la edición de `docs/05-decisiones.md` (fila y fila de Cambios) y aplícala solo si te digo que sí.
5. Commit en inglés, fusión en `master`, `git push`. Dime la rama y la copia de trabajo que se pueden borrar. Después de fusionar, en la copia principal: `cmake --build --preset windows-release` y `ctest --preset windows-release`; el número de tests de la fila de `progreso.md` es el de esa ejecución.

## 11. Resumen final (siempre, además de lo que pida cada prompt)

- Archivos nuevos y tocados, por módulo.
- Tests: número antes → después; los nuevos, con su valor medido frente al esperado.
- Huella y reservas de Jolt: antes → después, o "sin cambios".
- Rendimiento: ms por paso de la simulación en Release (media y máximo), medido con `rally_replay run tests/data/recordings/bench_lap.rrec --bench 10` cuando exista (desde la 2.4), y si no, con el contador (F1). Presupuesto: ≤ 0,5 ms.
- Las cifras de telemetría que pida el prompt.
- Decisiones que has tomado tú y no estaban en el prompt.
- Lo que queda pendiente o te preocupa.
