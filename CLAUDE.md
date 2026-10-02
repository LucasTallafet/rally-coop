# Rally Coop

Juego de rally cooperativo en 3D para 2 jugadores en 2 PCs: uno conduce en primera persona, con conducción del estilo de DiRT Rally, y el otro canta las curvas desde una tablet. Estilo visual low-poly con colores planos. Motor propio en C++20 con raylib (backend SDL), Jolt Physics y ENet. Solo Windows 10/11 de 64 bits. Proyecto personal de Lucas; la calidad es lo primero.

## Documentos de referencia

Léelos cuando la tarea toque su tema; no los cargues todos por defecto.

- `docs/01-diseno-de-juego.md`: qué es el juego y cómo se juega.
- `docs/02-arquitectura.md`: módulos, física, red, render, generador.
- `docs/03-requisitos.md`: requisitos mínimos y presupuestos de rendimiento.
- `docs/04-plan-de-fases.md`: fases, contenido y criterio de terminado de cada una.
- `docs/05-decisiones.md`: decisiones cerradas y provisionales. Manda sobre cualquier otro documento si hay contradicción.
- `docs/progreso.md`: fase actual, horas y hallazgos.

## Cómo trabajamos

- Antes de escribir código en una tarea nueva, lee la fase actual en `docs/progreso.md` y su criterio en `docs/04-plan-de-fases.md`. Propón un plan corto y espera mi visto bueno si la tarea toca más de un módulo.
- Una tarea por sesión. Termina con: tests pasando, resumen de lo hecho y la fila nueva en el registro de `docs/progreso.md` (las horas las pongo yo). Tras el commit, git push a origin.
- Si algo choca con una decisión cerrada de `docs/05-decisiones.md`, para y avísame; no la cambies por tu cuenta.
- Si una prueba cambia una decisión provisional, propón la edición de `docs/05-decisiones.md` con fecha y motivo.
- Regla de alcance: implementa solo lo que pide la fase actual. Nada genérico "por si acaso". Lo marcado [post-1.0] no se toca hasta su fase.
- Yo valoro el manejo, el aspecto y la diversión jugando. Tú compruebas todo lo que se pueda comprobar con tests.

## Idioma y estilo

- Documentos, comentarios y mensajes para mí: español.
- Identificadores, nombres de archivo de código y mensajes de commit: inglés.
- C++20. Sin excepciones ni reservas de memoria dentro del paso de simulación.
- Formato con `.clang-format` del repositorio, con clang-format 23.1.2 fijado (`python -m pip install --user clang-format==23.1.2`). Comprobar: `clang-format --dry-run --Werror` sobre `src/` y `tests/`.
- Unidades SI en toda la simulación: metros, segundos, radianes, kilogramos. Velocidades en m/s internamente; km/h solo en la interfaz.

## Compilar y probar

Requisitos: Visual Studio 2022 con "Desarrollo para el escritorio con C++" (MSVC ≥ 19.30) y CMake ≥ 3.25 en el PATH. Funciona desde cualquier terminal, sin Developer PowerShell (generador "Visual Studio 17 2022"). El primer configure descarga las dependencias (unos 90 s).

```
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

- Debug: los mismos comandos con `windows-debug`. Salidas en `build/<preset>/`; ejecutable en `build/windows-release/src/Release/rally.exe`.
- Dependencias en `cmake/Dependencies.cmake`, fijadas por URL y SHA256: Jolt v5.6.0, SDL 3.4.16 (estática), raylib 6.0 (backend SDL), doctest v2.5.3. Runtime estático `/MT`: un solo `.exe` sin DLL.
- Objetivo SSE4.2: sin AVX, LZCNT, TZCNT, F16C ni FMA en Jolt y sin `/arch` en nuestro código.
- Targets: `rally_sim` (src/sim, enlaza Jolt y nunca raylib), `rally_platform` (src/platform, raylib), `rally` (ejecutable), `rally_loop` (src/platform, reloj de paso fijo, interpolación y forma de la entrada, sin raylib ni SDL), `rally_tests` (doctest, solo `rally_sim` y `rally_loop`).
- El configure falla si un target nuestro define `JPH_*` (llegan solo de Jolt), si lo que compila simulación usa `/fp:fast` o `/fp:contract`, si algo usa `/arch:AVX*` o si la simulación o los tests enlazan raylib o SDL (`cmake/CheckBuildRules.cmake`). `src/sim/fp_guard.h` hace lo mismo con `#error`.
- `cmake/CheckSimRules.cmake` (test `sim_rules.*` en ctest) revisa src/sim, src/gen y src/net: matemática fuera de `rmath`, `<random>`, `rand()`, `unordered_*`, y reloj solo en sim y gen. Excepción por línea: `// rmath-allow: <motivo>`.
- Nombres de test sin corchetes ni `;`: rompen el descubrimiento de doctest en CMake.
- Todo test debe poder correr sin ventana ni mando.
- Antes de dar una tarea por terminada, compila en Release y pasa todos los tests.
- El exe pide la gráfica dedicada (AMD/NVIDIA); en PCs con gráfica integrada, comprobar en pantalla qué gráfica se usa.
- Mandos: Xbox, PS4, PS5 y Switch Pro, sin driver. El PS3 en Windows necesita el driver DsHidMini (no es trabajo de código). Probar con Steam cerrado: Steam Input reasigna los mandos. El overlay muestra el nombre y el tipo del mando activo.

## Reglas de la simulación (críticas)

La simulación tiene que dar exactamente el mismo resultado con las mismas entradas. Vale para todo lo que hay en `src/sim/` y `src/gen/`; el detalle está en `.claude/rules/simulacion.md`.

- Física a 240 Hz fijos; el render interpola, nunca simula.
- Nada se añade ni se quita de Jolt durante una pasada.
- Matemática solo a través de `rmath`; aleatorios solo con el PCG propio.
- Nada de la simulación lee el reloj, el número de fotograma ni datos del render o del audio.
- Sin `/fp:fast` ni contracción FMA.
- Si un cambio puede alterar el resultado de una simulación, ejecuta el test de reproducibilidad y dilo en el resumen.
- Si el test de huella falla por un cambio intencionado, actualiza la huella y dilo en el mensaje del commit.

## Estructura

- `src/platform/`: bucle, render, audio, entrada.
- `src/sim/`: coche, tramo, seguimiento (`s`, `t`), `rmath`.
- `src/gen/`: generador y validador.
- `src/game/`: sesión, copiloto, conductor, acciones, puntuación.
- `src/net/`: red y cambio de host.
- `tests/`, `tools/` (analizador en Python, visor de semillas), `data/` (perfiles, duraciones de acciones, tramos a mano).

## Cosas que no hacer

- No añadir dependencias nuevas sin preguntarme.
- No usar `<random>` ni el reloj del sistema en la simulación.
- No dibujar las notas del copiloto en la tablet: solo van en el panel del salpicadero.
- No hacer que el juego corrija, filtre o una las notas del copiloto.
- No añadir texturas fotográficas: el estilo es low-poly con colores planos y ruido procedural solo en el suelo.
- No reabrir el motor de física ni el motor gráfico: son decisiones cerradas.
