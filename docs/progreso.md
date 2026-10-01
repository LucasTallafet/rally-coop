# Rally Coop — Progreso

Se actualiza al cerrar cada sesión de trabajo. Sirve para ver la línea de desarrollo real frente a la prevista.

## Por fase

| Fase | Horas previstas | Horas reales | Estado | Inicio | Fin |
| --- | --- | --- | --- | --- | --- |
| 0 · Pruebas del concepto | 0 | 0 | Omitida | | |
| 1 · Base del motor | 60 | 0 | En curso | 2026-10-01 | |
| 2 · Coche | 130–160 | 0 | Sin empezar | | |
| 3 · Corte vertical | 110–130 | 0 | Sin empezar | | |
| 4 · Generador | 120–150 | 0 | Sin empezar | | |
| 5 · Entre casas | 60–80 | 0 | Sin empezar | | |
| 6 · Calidad visual y sonora | 80–120 | 0 | Sin empezar | | |

## Registro de sesiones

Más reciente arriba.

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-10-01 | 1 | | Tarea 1, base de compilación: CMake con presets windows-debug/release (VS 2022, `/MT`); Jolt v5.6.0 (doble precisión, determinista, SSE4.2), SDL 3.4.16 y raylib 6.0 (backend SDL), doctest v2.5.3, por FetchContent con SHA256. Targets `rally_sim`/`rally_platform`/`rally`/`rally_tests`; comprobaciones al configurar (JPH_* solo desde Jolt, sin `/fp:fast` ni `/fp:contract`, sin AVX, simulación sin raylib) y `fp_guard.h`. `rmath`, PCG32 con valores de pcg-c, caja de Jolt bit a bit en dos pasadas, test de reglas con `cmake -P`. `.clang-format`. Ventana de 1280×720 que se cierra con Esc. 14/14 tests en Release y Debug | Tarea 2: bucle con paso fijo de 240 Hz, acumulador, interpolación y tope de pasos |

## Hallazgos de las pruebas

Resultados que confirman o cambian decisiones provisionales. Cada hallazgo que cambie algo se refleja también en `05-decisiones.md`.

| Fecha | Prueba | Resultado | Decisión afectada |
| --- | --- | --- | --- |
| | | | |
