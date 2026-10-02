# Rally Coop — Bitácora de desarrollo

Del 1 al 2 de octubre de 2026: del arranque del proyecto al cierre de la fase 1.

Recoge lo que no cabe en los demás documentos: cómo se llegó a cada decisión, qué problemas aparecieron y cómo se resolvieron, qué problemas se esperan en las fases siguientes y qué ideas quedan para el futuro. Las fuentes de verdad siguen siendo `01` a `05` y `progreso.md`. Esta bitácora explica el porqué y no los sustituye: si hay contradicción, manda `05-decisiones.md`.

---

## 1. Estado al cierre de la fase 1

| Dato | Valor |
| --- | --- |
| Fase actual | 1 · Base del motor: **terminada** el 2026-10-02; la siguiente es la 2 · Coche |
| Horas reales de la fase 1 | 6 h, frente a 60 previstas |
| Tests | 92, en Release y Debug, sin ventana ni mando |
| Huella de la simulación | `0xfb7852dd64be99e5` (igual en Debug y Release) |
| Reservas de Jolt en el test de 1000 pasos | 1864 (exactas) |
| Repositorio | `github.com/LucasTallafet/rally-coop` (privado), rama `master` |
| Equipo de pruebas | Windows 10, i3-8100 (4 núcleos, 3,6 GHz, AVX2), RX 580, Intel UHD 630 integrada, monitor de 1360×768 a 60 Hz |

Lo que funciona hoy: una caja de Jolt que se conduce como un coche simplificado, en primera persona, con teclado o mando, a 240 Hz de física, con interpolación, ritmo de fotogramas estable en pantalla completa sin bordes, render relativo a la cámara válido a 20 km del origen, vibración en los impactos, contador de rendimiento y una escena de medida con 20 000 árboles.

---

## 2. Cronología

| Fecha | Paso | Horas | Resultado |
| --- | --- | --- | --- |
| 01-10 | Fase 0 | 0 | **Omitida.** Sin DiRT Rally 2.0 y con la idea decidida, se pasa directamente al motor; sus mediciones se trasladan a la fase 3 |
| 01-10 | Tarea 1 · Base de compilación | 1,5 | CMake con presets, dependencias fijadas por SHA256, Jolt determinista en doble precisión, SSE4.2, reglas automáticas de compilación, rmath, PCG32, 14 tests |
| 01-10 | Tarea 2 · Bucle de paso fijo | 1 | Reloj de 240 Hz con acumulador entero, interpolación, sincronización vertical a la frecuencia del monitor, modo de 30 sin espera activa |
| 01-10 | Tarea 3 · Mundo de Jolt | 0,5 | `SimWorld` construido en lote y sellado, entrada cuantizada, huella, cero reservas propias por paso |
| 02-10 | Tarea 4 · Primera persona | 0,5 | Render relativo a la cámara (validado a 20 km), caja manejada como un coche simplificado |
| 02-10 | Tarea 4b · Ritmo de fotogramas (añadida) | 1 | Ajuste del dt al refresco del monitor; pantalla completa sin bordes; gráfica de fotogramas |
| 02-10 | Tarea 5 · Mando | 0,5 | Mandos por SDL3, suavizado por paso en la simulación, gatillos digitales, vibración por impacto |
| 02-10 | Tarea 6 · Contador y cierre | ~1 | Contador de CPU, GPU, draw calls y memoria; escena de bosque a 1080p; tabla de cierre de la fase |

Evolución de los tests: 14 → 28 → 39 → 42 → 63 → 82 → 92.

---

## 3. Decisiones tomadas en este periodo

### 3.1 Registradas en `05-decisiones.md`

| Decisión | Qué se decidió | Por qué |
| --- | --- | --- |
| **Fase 0 omitida** | Las pruebas del concepto pasan al corte vertical (fase 3), con un registro de notas y las dos orientaciones del mapa | La idea se va a desarrollar en cualquier caso; la fase 3 ya tenía el criterio de "si no es divertido, se corrige antes del generador". Riesgo asumido: ~60 h de motor antes de comprobar la idea, aunque es infraestructura que se reutiliza en cualquier caso |
| **SSE4.2 como máximo** | Jolt y nuestro código sin AVX, AVX2, LZCNT, TZCNT, F16C ni FMA | No excluir CPUs de los requisitos mínimos (Sandy/Ivy Bridge, AMD FX). LZCNT es el caso peligroso: en una CPU antigua se ejecuta como otra instrucción y da resultados erróneos sin avisar |
| **Fotogramas** | Sincronización vertical a la frecuencia del monitor (no limitar a 60) y modo de 30 sin espera activa | En 144/165 Hz, limitar a 60 no tiene divisor entero y da más tirones que no limitar. El requisito de 60 fps se mide en un monitor de 60 Hz |
| **Entrada** (provisional hasta la fase 2) | Zona muerta y curva en la plataforma; suavizado de dirección y acelerador digital en la simulación, por paso; botones por posición; mando activo = último usado | Si el suavizado fuera por fotograma, la dirección respondería distinto a 30 y a 60 fps, y no quedaría en las repeticiones |

### 3.2 Decisiones técnicas de implementación (en el código y en `CLAUDE.md`)

- **Generador de Visual Studio**, no Ninja: compila desde cualquier terminal, sin Developer PowerShell.
- **Runtime estático `/MT`**: un único `.exe` sin DLL, cómodo para pasárselo al compañero.
- **Dependencias por URL + SHA256** con FetchContent: inmutables, igual que un commit.
- **doctest** como framework de tests: una cabecera, rápido de compilar.
- **Biblioteca de simulación sin raylib**: `rally_sim` y `rally_loop` no pueden enlazar raylib ni SDL, y el configure falla si lo hacen. Los tests corren sin ventana.
- **Reglas convertidas en comprobaciones automáticas**, no en promesas:
  - `CheckBuildRules.cmake`: definiciones `JPH_*` solo desde Jolt; sin `/fp:fast`, `/fp:contract` ni `/arch:AVX*`; simulación sin raylib.
  - `fp_guard.h`: los mismos flags vigilados con `#error`.
  - `CheckSimRules.cmake`: nada de matemática fuera de `rmath`, `<random>`, `rand()`, `unordered_*`, ni reloj en sim y gen. Excepción por línea con `// rmath-allow: <motivo>`.
  - `VerifyJoltVersionID()` en un test y al arrancar.
  - **Test de huella** (FNV-1a del SaveState tras 60 s de entradas pseudoaleatorias): si cambia sin querer, falla; si el cambio es intencionado, se actualiza y se dice en el commit.
  - **Reservas de memoria**: 0 propias por paso, contando `operator new`; las de Jolt, con su total exacto.
  - **Test de muerte**: añadir un cuerpo después de `seal()` aborta, también en Release.
- **Mundo sellado**: `build()` en lote y en orden fijo, `seal()`, y después ningún cuerpo nuevo.
- **Entrada cuantizada**: `SimInput` de 6 bytes con enteros (`int16` en los ejes y bits para los botones). La grabación de entradas de la fase 2 será exacta byte a byte.
- **Caja controlada como coche simplificado**: solo gira en el eje vertical; acelerador y dirección relativos a la caja. Con controles en ejes del mundo, la primera persona desorientaba.
- **El exe pide la gráfica dedicada** (exportaciones `AmdPowerXpressRequestHighPerformance` y `NvOptimusEnablement`, que tienen que estar en el `.exe`).
- **Errores fatales en un cuadro de mensaje**: en Release no hay consola.
- **Pantalla completa sin bordes** como modo de juego: en ventana, el tearing y el ruido de presentación con AMD en Windows 10 quedan fuera de nuestro control. *Pendiente de pasar a `05-decisiones.md`* (§7).
- **Mandos leídos directamente con SDL3**: raylib los abre (conexión en caliente), pero sus datos se leen con SDL para que ejes, botones, vibración, nombre y tipo salgan siempre del mismo mando.
- **Draw calls contadas interceptando los punteros de glad** de raylib: es la única forma de contar tanto los lotes de rlgl como las llamadas directas.
- **Vegetación con VBO persistente**, no `DrawMeshInstanced` de raylib (§4).

---

## 4. Problemas encontrados y cómo se resolvieron

| Problema | Causa | Solución |
| --- | --- | --- |
| Un núcleo de la CPU al 99 % con sincronización vertical | La ventana iba en la Intel UHD 630, cuyo controlador OpenGL espera dando vueltas en SwapBuffers | Monitor conectado a la RX 580 y el exe pidiendo la gráfica dedicada: 3–5 % |
| OpenGL elegía la Intel aunque el monitor estuviera en la RX 580 | Windows tenía activas las dos gráficas | Exportaciones de AMD/NVIDIA en el `.exe` y el nombre de la gráfica en pantalla |
| Las dos gráficas ignoran el intervalo de sincronización 2 en ventana | Controladores | El modo de 30 detecta el caso y pasa a un temporizador de alta resolución |
| Tearing intermitente en la demo 2D | De presentación, no de dibujo: las capturas del búfer salían limpias | Diagnóstico con pausa (P) y captura (C); se dejó en observación |
| Tearing fuerte en primera persona | Windows duplicaba la pantalla entre la Intel y la RX 580 | Una sola pantalla activa |
| Tirones de 2–3 fotogramas con sincronización vertical | Ruido al medir el tiempo de fotograma: con ±1–2 ms, a veces salían 3 o 5 pasos en lugar de 4 | Ajuste del dt al periodo del monitor (ventana = el menor de 2,5 ms y el 40 % del periodo), con la diferencia devuelta poco a poco y fase centrada |
| Más ruido y tearing en ventana que en pantalla completa | Composición de Windows con AMD en ventana: 276 fotogramas desviados frente a 5 | Pantalla completa sin bordes como modo de juego |
| El monitor dice 60 Hz pero va a 60,015 | Hardware | Fotogramas de 5 pasos ocasionales en los cruces de borde; no se ven, porque se dibuja según el tiempo aplicado |
| Un toque corto de Espacio se perdía a más de 240 fps | Fotogramas sin ningún paso de física | `ButtonLatch`: las pulsaciones se guardan hasta el primer fotograma con pasos |
| El salto dependería de los fps si se detectara por fotograma | La misma entrada se aplica a 4 u 8 pasos | Flanco de pulsación detectado en la simulación, paso a paso |
| Jolt reserva 2 bloques por paso | Reconstrucción del árbol de la broad phase al moverse un cuerpo | Aceptado: tamaño constante y sin efecto en el resultado. Total exacto vigilado por un test |
| Jolt reordena el array de `AddBodiesPrepare` | Comportamiento interno de Jolt | Se le pasa una copia; los BodyID siguen el orden de la descripción |
| raylib pone los gatillos a −1 al conectar el mando | Valor inicial de raylib | Los valores negativos se recortan; si no, el mando frenaría a fondo al conectarlo |
| `GetGamepadName` de raylib daba el nombre de otro mando tras reconectar | Sus huecos no coinciden con la lista de SDL | Lectura directa con SDL3 |
| `DrawMeshInstanced` de raylib reserva 1,28 MB y crea un VBO en cada llamada | Implementación de raylib 6.0 | VBO persistente: 0,50 ms de CPU frente a 1,6 ms sin sincronización vertical (0,93 frente a 2,9 con ella; picos de hasta 13,9) |
| Los nombres de test con `[` o `;` rompían el descubrimiento de doctest | Listas de CMake | Regla en `CLAUDE.md` |
| "Dubious ownership" al usar git | El repositorio se creó desde un PowerShell de administrador | `safe.directory`; usar siempre PowerShell normal |
| Copias de trabajo bloqueadas al borrarlas | Sesiones de Claude Code o procesos de compilación aún abiertos | Cerrar sesiones y procesos y forzar (ver `07-entorno-y-portabilidad.md` §7) |
| Prompts largos cortados al pegarlos en la terminal | Consola de Windows | Pegar en dos partes |
| Se borraron las capturas del diagnóstico del tearing | Un `Remove-Item captura_*.png` sin mirar la carpeta | Regla en la memoria de Claude Code: *pendiente de pasar a `CLAUDE.md`* |
| Toda la fase 1 estaba solo en el disco local | No había remoto configurado | Repositorio privado en GitHub y push al cerrar cada tarea |

---

## 5. Problemas esperados y soluciones propuestas

### Fase 2 · Coche

Diseño cerrado el 2026-10-02 (ver `04-plan-de-fases.md`, fase 2).

- **Estabilidad de la rueda a baja velocidad.** Con un neumático explícito, la rigidez efectiva de la rueda crece como 1/v: a 2 m/s oscila incluso a 960 Hz. Solución estándar: denominador mínimo en el deslizamiento y freno que no invierte el giro. Test de barrido de velocidades.
- **Freno de mano en un 4×4.** Si el diferencial central sigue acoplado, las delanteras arrastran a las traseras y la horquilla no sale. Se desacopla mientras se tira.
- **Grava frente a tierra a ciegas** es la pareja más difícil de distinguir. Personalidades distintas desde los valores de partida: la grava, flotante y progresiva; la tierra, con más agarre y caída más brusca.
- **El ajuste mueve la huella** si el test usa el coche del juego: por eso el test usa un coche congelado.
- **Ajustar el manejo es el gran coste en horas.** Claude Code escribe el neumático rápido, pero la sensación se ajusta jugando. Es la fase que dirá si la nueva estimación se sostiene.
- **`SimInput` crecerá** (freno separado del acelerador, freno de mano, marchas, recolocar). Cada cambio mueve la huella y el total de reservas: se actualizan a propósito, como hasta ahora.
- **Mandos sin validar.** Solo se ha probado el de PS4. El de **Switch Pro** es el único con gatillos digitales, y ese camino solo se ha probado con un mando simulado: hay que probarlo antes de la fase 2. También faltan Xbox y PS5.
- **Velocidades del suavizado.** El cambio de sentido con teclado tarda ~0,37 s de tope a tope y puede resultar lento para contravolantear. Es la primera constante que habría que tocar.
- **Vibración por superficie y por deslizamiento**: hoy solo hay vibración por impacto.
- **Estado de la simulación fuera del SaveState de Jolt** (`previous_buttons`, dirección y acelerador aplicados): crecerá con el coche, y todo tiene que quedar listo para el cambio de host de la fase 5.

### Fase 3 · Corte vertical

- **Presupuesto de draw calls: ≤ 2 000 por fotograma.** Cada una cuesta ~0,58 µs de CPU con raylib (20 000 sueltas = 11,6 ms). Nada de dibujar objetos sueltos a miles.
- **Medidas que venían de la fase 0**: registro de notas (paso, ancla `s`, nota, distancia elegida y real) y las dos orientaciones del mapa con una tecla. Con ellas se fijan el techo de densidad de curvas y las decisiones de tablet, diales y panel.
- **Memoria de la física del tramo entero**: decisión provisional hasta medirla en esta fase.

### Fase 4 · Generador

- **Conseguir y limpiar datos reales** (OpenStreetMap, PNOA-LiDAR) es trabajo manual que no se comprime.
- **Tiempo del validador con piloto IA** en una CPU de 4 núcleos: que no se note antes de cada pasada.

### Fase 5 · Entre casas

- **El cambio de host** tiene que llevar el estado de la simulación que no está en el SaveState de Jolt.
- **La variación de latencia** necesita el reloj en `src/net`: por eso la regla del reloj solo vigila `src/sim` y `src/gen`.

### Fase 6 · Calidad visual y sonora

- **Vegetación.** Con 20 000 árboles instanciados: 0,50 ms de CPU y 1,65 ms de GPU a 1080p, medidos sin sincronización vertical (con ella la GPU no sube su reloj y salían 0,93 y 5,5 ms). Se dibujan los 360°, pero no incluye las sombras. Para que quepa:
  - descarte por celdas fuera del campo de visión (se ve ~1/4 del anillo);
  - LOD o impostores para los árboles lejanos;
  - solo las cascadas de sombra cercanas dibujan árboles.
- **Precisión de profundidad.** Con el plano cercano a 0,05 m (interior del coche) y 2 km de distancia de dibujado, habrá parpadeo a lo lejos con un búfer de profundidad normal. Solución estándar: profundidad invertida con `glClipControl` y búfer de profundidad en float.
- **Medir a 1080p reales**: el monitor de pruebas es de 1360×768. `--bench-forest` ya dibuja en una RenderTexture de 1920×1080. Pendiente: medir sin sincronización vertical (§7).
- **Monitores de alta frecuencia en el equipo mínimo**: un ajuste de "frecuencia del monitor ÷ N" (72 fps en 144 Hz), además del modo de 30.
- **Tearing en ventana con AMD**: fuera de alcance. Si reapareciera en pantalla completa, la solución preparada es esperar al compositor con `DwmFlush()`.

### Fase 9 · Clasificatorias

- **Simulación idéntica entre PCs.** Primero `_set_FMA3_enable(0)` para la biblioteca matemática de Windows y, si no basta, `rmath` propio. **Indicador temprano:** si el test de huella falla en otro PC con el mismo commit, ya hay un problema de determinismo entre PCs (ver `07-entorno-y-portabilidad.md` §4).

---

## 6. Ideas de futuro

Además de lo que ya recoge el plan después de la 1.0 (barro y clima, fuera del coche, tienda, clasificatorias, editor, modo 1 jugador, voz integrada), en este periodo han salido:

| Idea | Fase en la que encaja | Coste estimado |
| --- | --- | --- |
| Reasignación de controles y nombres de botón según el mando (A / ✕ / B) | 5 (menús) | 3–5 h |
| Ajuste de ritmo "frecuencia del monitor ÷ N" | 6 (ajustes gráficos) | 1–2 h |
| Gatillos adaptativos del DualSense (resistencia en el freno) | Después de la 1.0 | 4–6 h |
| Guía para jugadores: una sola pantalla activa, Steam cerrado o configurado, DsHidMini para el PS3 | 5 (antes de jugar entre casas) | 1 h |
| Informe automático de rendimiento al final de cada fase con `--bench-forest` (y más escenas) | Desde la fase 3 | 1–2 h por escena |

---

## 7. Pendientes inmediatos

- [x] Validar a ojo el contador (F1 y B) y fusionar la rama `worktree-phase1-task6-perf-counter` en `master`
- [x] `--bench-forest` sin sincronización vertical, y actualizar las cifras
- [x] `03-requisitos.md`: medida del bosque denso en la tabla de riesgos y el presupuesto de draw calls (≤ 2 000)
- [x] `02-arquitectura.md` §8: vegetación con VBO persistente, no `DrawMeshInstanced`
- [x] `04-plan-de-fases.md`, `progreso.md` y `05-decisiones.md`: nueva estimación de horas (§8)
- [x] `05-decisiones.md`: decisión "Modo de pantalla: pantalla completa sin bordes; la ventana es solo para desarrollo"
- [x] `CLAUDE.md`: regla de no borrar archivos de la carpeta del exe sin mirarla antes (hoy solo está en la memoria local de Claude Code)
- [ ] Probar los mandos de Xbox, PS5 y Switch Pro (este último, antes de la fase 2)
- [x] Añadir `07-entorno-y-portabilidad.md` y esta bitácora a `README.md` y a `CLAUDE.md`
- [ ] Subir los documentos al proyecto de claude.ai

---

## 8. Revisión de la estimación (aplicada al cerrar la fase 1)

La fase 1 costó ~6 h frente a 60. Claude Code comprime mucho el código de infraestructura, pero no lo que depende de ti: ajustar el manejo jugando, las pruebas con otra persona, modelar en Blender, el sonido y conseguir datos reales.

| Fase | Antes | Propuesta | Motivo |
| --- | --- | --- | --- |
| 1 · Base del motor | 60 | ~6 (real) | |
| 2 · Coche | 130–160 | 61–109 | El ajuste del neumático jugando no se comprime. Ampliada el 2026-10-02: arena, turbo y configuraciones de prueba; y en la revisión del diseño: zona de pruebas, `rally_replay` y chasis completo |
| 3 · Corte vertical | 110–130 | 45–75 | Las pruebas con el compañero no se comprimen |
| 4 · Generador | 120–150 | 50–90 | Datos reales y validación de semillas jugando |
| 5 · Entre casas | 60–80 | 20–40 | Casi todo es infraestructura |
| 6 · Calidad visual y sonora | 80–120 | 50–90 | Blender y sonido son trabajo manual |
| **Total 1.0** | **560–700** | **~230–410** | Se revisa de nuevo al cerrar la fase 2. Ampliada el 2026-10-02 con la fase 2 |
