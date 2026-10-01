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
- Una tarea por sesión. Termina con: tests pasando, resumen de lo hecho y la fila nueva en el registro de `docs/progreso.md` (las horas las pongo yo).
- Si algo choca con una decisión cerrada de `docs/05-decisiones.md`, para y avísame; no la cambies por tu cuenta.
- Si una prueba cambia una decisión provisional, propón la edición de `docs/05-decisiones.md` con fecha y motivo.
- Regla de alcance: implementa solo lo que pide la fase actual. Nada genérico "por si acaso". Lo marcado [post-1.0] no se toca hasta su fase.
- Yo valoro el manejo, el aspecto y la diversión jugando. Tú compruebas todo lo que se pueda comprobar con tests.

## Idioma y estilo

- Documentos, comentarios y mensajes para mí: español.
- Identificadores, nombres de archivo de código y mensajes de commit: inglés.
- C++20. Sin excepciones ni reservas de memoria dentro del paso de simulación.
- Formato con `.clang-format` del repositorio (se crea en la fase 1).
- Unidades SI en toda la simulación: metros, segundos, radianes, kilogramos. Velocidades en m/s internamente; km/h solo en la interfaz.

## Compilar y probar

Los comandos se fijan en la fase 1. Cuando existan, actualiza esta sección. Previstos:

```
cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

- Todo test debe poder correr sin ventana ni mando.
- Antes de dar una tarea por terminada, compila en Release y pasa todos los tests.

## Reglas de la simulación (críticas)

La simulación tiene que dar exactamente el mismo resultado con las mismas entradas. Vale para todo lo que hay en `src/sim/` y `src/gen/`; el detalle está en `.claude/rules/simulacion.md`.

- Física a 240 Hz fijos; el render interpola, nunca simula.
- Nada se añade ni se quita de Jolt durante una pasada.
- Matemática solo a través de `rmath`; aleatorios solo con el PCG propio.
- Nada de la simulación lee el reloj, el número de fotograma ni datos del render o del audio.
- Sin `/fp:fast` ni contracción FMA.
- Si un cambio puede alterar el resultado de una simulación, ejecuta el test de reproducibilidad y dilo en el resumen.

## Estructura

- `src/platform/`: bucle, render, audio, entrada.
- `src/sim/`: coche, tramo, seguimiento (`s`, `t`), `rmath`.
- `src/gen/`: generador y validador.
- `src/game/`: sesión, copiloto, conductor, acciones, puntuación.
- `src/net/`: red y cambio de host.
- `tests/`, `tools/` (analizador en Python, visor de semillas), `prototypes/` (fase 0), `data/` (perfiles, duraciones de acciones, tramos a mano).

## Cosas que no hacer

- No añadir dependencias nuevas sin preguntarme.
- No usar `<random>` ni el reloj del sistema en la simulación.
- No dibujar las notas del copiloto en la tablet: solo van en el panel del salpicadero.
- No hacer que el juego corrija, filtre o una las notas del copiloto.
- No añadir texturas fotográficas: el estilo es low-poly con colores planos y ruido procedural solo en el suelo.
- No reabrir el motor de física ni el motor gráfico: son decisiones cerradas.
