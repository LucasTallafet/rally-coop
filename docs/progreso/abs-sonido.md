| Fecha | Tarea | Qué se hizo | Tests antes → después | Huella y reservas | Validado a ojo | Horas | Siguiente paso |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 2026-10-22 | Lote 3 · `abs-sonido` | `PlayerSettings::abs = false` por defecto (la clave se sigue guardando). Sonido de la rueda bloqueada: nivel por rueda con la raíz de la velocidad de deslizamiento (1 a 34 m/s, 0,84 a 14, 0,52 a 5,5, 0,39 a 3), en asfalto reparto tono/ruido según ρ (tono 0,25·skid·w, ruido 0,25·skid·(1−w), w = clamp((3−ρ)/2, 0, 1)) y tono de 0,12 a 0,05; el modelo de la 2.7 se queda para el A/B (tecla 6, Depuración, empieza en el nuevo, no se guarda ni va en la grabación). Funciones puras `skid_level` y `asphalt_tone_share`; la ganancia del tono va en el objetivo suavizado (sin clic al cambiar de modelo). Rueda bloqueada en asfalto (4 ruedas, ρ 10, 30 m/s) frente al motor a 1000 rpm y carga 0: nuevo ×1,23 (0,0316 / 0,0256), 2.7 ×3,28 (0,0842). En tierra, v_desl 28 / 3 m/s: nuevo ×2,58, 2.7 ×1,00. Tono a ciegas con ρ 1,5: 0,0012 frente a 0,0070 con vista (nuevo) y 0,0223 (2.7). `Key::Six` añadida a la lista de teclas | 436 → 442 (Release y Debug) | Sin cambios: 0x66c9e3a86632750e, 1990, telemetría 0xfe3c612ddcaa6805, kSimVersion 5 | Sin validar a ojo (A/B con la tecla 6 en el informe) | | Lote 3: según el lote |

**Hallazgos**

| Dónde | Hallazgo |
| --- | --- |
| Tests | El criterio 5 se mide con ρ 1,5 (con ρ ≥ 3 el modelo nuevo no da tono ni con vista): amplitud a 820, 850 y 880 Hz (Goertzel local al test, por el vibrato), con vista > 3 × a ciegas. El test de igualdad entre superficies a ciegas corre con los dos modelos. |
| Teclas | `Key::Six` no existía en `keys.h`; añadida. Tecla 6, orden 190 del bloque Depuración (antes de la 7). |

**Ediciones propuestas**

- `docs/07-entorno-y-portabilidad.md` línea 211: cambiar «`abs` (1 o 0; activado por defecto)» por «`abs` (1 o 0; desactivado por defecto desde el lote 3)». Añadir, en el bloque de teclas de depuración: «6: sonido del deslizamiento, nuevo / el de la 2.7 (A/B del lote 3; empieza siempre en el nuevo; no se guarda ni va en la grabación)».
- `docs/08-bitacora-de-desarrollo.md`, fila nueva (2026-10-22): «Sonido de la rueda bloqueada. En la prueba del lote 1, la rueda bloqueada en asfalto sonaba como un turbo: un tono de 850 Hz ~3 veces más fuerte que el motor, al máximo de 149 a 11 km/h. Modelo nuevo: nivel con la raíz de la velocidad de deslizamiento hasta 20 m/s; en asfalto, tono cerca del pico y ruido con la rueda bloqueada; tono de 0,12 a 0,05. Medido: rueda bloqueada ×1,23 el motor (antes ×3,28). Los valores son de partida y se juzgan con el A/B de la tecla 6; en la fase 6 todo el sonido se sustituye por sonido grabado.»

Pide de lo compartido: `docs/manejo/protocolo.md` (ajustes base: ABS desactivado) lo cambia el integrador, como dice el prompt.
