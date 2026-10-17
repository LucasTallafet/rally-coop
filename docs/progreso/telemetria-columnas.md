Fragmento de `progreso.md` — tarea mínima `telemetria-columnas` (la pide Lucas; no está en `mensajes/lote-1.md`). Rama `lote-1/telemetria-columnas`.

## Fila del registro

| 2026-10-17 | 2 | | Tarea `telemetria-columnas`. Una línea de `tests/sim/CMakeLists.txt` (104): el test `replay.telemetry_first` / `replay.telemetry_second` exigía `telemetría: .*128 columnas.*hash OK`; ahora `telemetría: .*[0-9]+ columnas.*hash OK`, porque el número de columnas lo fijan `tools/telemetry/columns.md` y `kExpectedCsvHash` (carril juego, `grabacion-v7`, que lo sube a 133). Sin tocar `src/`. Huella `0x66c9e3a86632750e`, reservas de Jolt 1990, telemetría `0xfe3c612ddcaa6805` y `kSimVersion` 5: sin cambios. Con 128 columnas sigue cumpliendo. Nada que probar jugando. | |

## Hallazgos

Ninguno. No hay otra línea de `tests/sim` que fije «128 columnas».

## Ediciones propuestas de `docs/02`, `07` y `08`

Ninguna.

## Pide de lo compartido

Nada. Se fusiona antes que `grabacion-v7`.
