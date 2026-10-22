# Tarea `abs-sonido` (lote 3, carril plataforma): ABS desactivado por defecto y sonido de la rueda bloqueada

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-3.md` §2.7, §2.8 y §8, que mandan sobre este prompt. Diagnóstico: `lote-3.md` §6.

## Parte 1 · ABS desactivado por defecto

- **`PlayerSettings::abs = false`** (`src/platform/player_settings.h`). Cambia también su comentario: el juego arranca sin ABS desde el lote 3; antes iba como en los ajustes base del protocolo.
- **Tests de `tests/platform/test_player_settings.cpp`** que den por hecho el ABS activado: se actualizan. Lo nuevo que tienen que comprobar:
  - `PlayerSettings{}.abs` es falso;
  - `parse_settings("")` da el ABS desactivado;
  - `format_settings` → `parse_settings` conserva `abs` en los dos valores;
  - la línea de F3 dice «ABS no» por defecto.
- **La clave `abs` se sigue guardando,** como las demás ayudas (decisión vetable, `lote-3.md` §8.1).
- **`docs/manejo/protocolo.md` no es tuyo:** lo cambia el integrador en el paso 1.
- **La simulación no cambia:** `DriverAids` ya tiene `abs = false` por defecto, y los tests y las grabaciones no leen `PlayerSettings`.

## Parte 2 · Sonido de la rueda bloqueada (`src/platform/car_synth.*`)

**Hoy** (`CarSynth::set_targets`):
- por rueda, `skid = clamp(ρ − 1, 0, 1) · min(v_desl / 3, 1)`;
- en asfalto (sin modo a ciegas), todo va al tono de 850 Hz con ganancia 0,12; en las demás superficies, al ruido filtrado a 500 Hz con 0,30.

Con la rueda bloqueada, el tono queda al máximo de 149 a 11 km/h: suena unas 3 veces más fuerte que el motor y parece un turbo.

**Modelo nuevo.** Las constantes van con nombre en `car_synth.h`, comentadas como «valor de partida, se juzga de oído (lote 3)»:
1. **Por la velocidad de deslizamiento:** `skid = clamp(ρ − 1, 0, 1) · min(sqrt(v_desl / 20 m/s), 1)`. Valores: 1 a 34 m/s, 0,84 a 14 m/s, 0,52 a 5,5 m/s y 0,39 a 3 m/s, frente al 1 de hoy en los tres últimos.
2. **En asfalto, sin modo a ciegas:** parte tono y parte ruido según ρ.
   - tono: `0,25·skid·w`; ruido: `0,25·skid·(1 − w)`, con `w = clamp((3 − ρ) / 2, 0, 1)`;
   - así, tono cerca del pico (ρ de 1 a 2) y ruido con la rueda bloqueada (ρ ≥ 3).
   - En las demás superficies y a ciegas, todo al ruido, como hoy.
3. **Ganancia del tono:** de 0,12 a 0,05.
4. **El modelo de la 2.7 se queda, tal cual, para el A/B.** Se elige con un indicador atómico, como el volumen (el callback corre en otro hilo).
   - **Tecla 6** (bloque Depuración): «Sonido del deslizamiento: nuevo / el de la 2.7».
   - Mensaje «6: deslizamiento nuevo» o «6: deslizamiento de la 2.7».
   - Empieza siempre en el nuevo. No se guarda en `ajustes.txt` ni va en la grabación.
   - Actualiza `docs/teclas.md` con lo que imprime su test.

Si te sale más limpio con una función pura `skid_level(ρ, v_desl, modelo)` y `asphalt_tone_share(ρ, modelo)` para los tests, mejor. No toques `car_feedback`: ρ y v_desl ya llegan en la instantánea.

## Criterios (tests en `tests/platform/`, Release y Debug)

1. **Nivel por rueda**, con los dos modelos, en la tabla (ρ, v_desl) → nivel:
   - (10, 34) → 1;
   - (10, 14) → 0,837;
   - (10, 5,5) → 0,524;
   - (10, 3) → 0,387;
   - (1,5, 34) → 0,5;
   - (0,9, 30) → 0.

   Con el de la 2.7: 1, 1, 1, 1, 0,5 y 0. Tolerancia 1·10⁻³.
2. **Reparto en asfalto:** w = 1 con ρ ≤ 1; 0,75 con ρ = 1,5; 0,5 con ρ = 2; 0 con ρ ≥ 3. Con el de la 2.7, todo al tono.
3. **Rueda bloqueada en asfalto,** con los canales por separado (`buses`): 4 ruedas, ρ = 10, v_desl = 30 m/s, coche a 30 m/s, 1 s renderizado. Se compara el nivel eficaz del canal de deslizamiento con el del motor solo a 1000 rpm y carga 0:
   - modelo nuevo: ≤ 1,5 veces el del motor;
   - modelo de la 2.7: ≥ 2,5 veces. Comprueba que la medida ve el problema; el diagnóstico dio ~3.

   Da las dos cifras en el informe.
4. **Baja con la velocidad:** en tierra, 4 ruedas con ρ = 10:
   - modelo nuevo: el nivel eficaz del canal a v_desl = 28 m/s es ≥ 2,4 veces el de 3 m/s;
   - modelo de la 2.7: ≤ 1,05 veces.
5. **Modo a ciegas:** ningún tono con ninguno de los dos modelos, como hoy.
6. **Sin reservas** en el callback de audio: el test de 10 s que ya existe, con el modelo nuevo y con el de la 2.7.
7. **Tecla 6:** no se repite en ningún contexto (test de teclas); `docs/teclas.md` coincide con la tabla generada.

**Sin cambios:** nada de `src/sim`, `src/io` ni `src/game`. La huella, las reservas, la telemetría, el CSV y el `.rrec`, iguales. La vibración no cambia: usa `rho_max`, no el sonido.

## Para el informe y la prueba

**A/B de Lucas en el mismo ejecutable,** con la tecla 6 y sin ABS (tecla 1):
- frenada a fondo desde ~130 km/h hasta parar en el asfalto de la zona, 2 veces con cada modelo;
- lo mismo en la pista ciega de tierra.

Preguntas:
1. ¿Ha desaparecido el «turbo»?
2. ¿Se oye que la rueda va bloqueada, y baja con la velocidad?
3. Con ABS (tecla 1), ¿la rueda «canta» un poco al límite?
4. ¿Algún nivel que subir o bajar?

Y la comprobación de la parte 1: abrir el juego con un `ajustes.txt` sin la clave `abs` da «ABS no» en F3.
