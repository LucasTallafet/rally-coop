# Rally Coop — Arquitectura

Versión final de trabajo · 1 de octubre de 2026

Motor propio en C++ sobre librerías pequeñas enlazadas de forma estática. El motor solo hace lo que este juego usa: un coche, un tramo, dos vistas (luna y tablet) y una conexión entre dos PCs. Todo es código que se puede escribir, compilar y probar sin editor visual.

## 1. Stack

| Pieza | Elección | Notas |
| --- | --- | --- |
| Lenguaje y compilación | C++20, CMake, MSVC | Solo Windows 10/11 de 64 bits |
| Ventana, render, entrada | raylib con backend SDL (OpenGL 3.3) | SDL da vibración, DualSense, mandos genéricos y conexión en caliente |
| Física | Jolt Physics | `JPH_DOUBLE_PRECISION`, `JPH_CROSS_PLATFORM_DETERMINISTIC`, un hilo (`JobSystemSingleThreaded`); instrucciones hasta SSE4.2 (sin AVX, AVX2, LZCNT, TZCNT ni F16C) |
| Red | ENet sobre UDP | Entre casas: Tailscale o puerto abierto |
| Audio | miniaudio (el que usa raylib) | Mezcla por canales propia |
| Voz integrada [post-1.0] | Opus + miniaudio | Supresión de ruido ligera (p. ej. RNNoise) |
| Herramientas | Python 3 | Analizador de tramos reales; prototipos de referencia |

## 2. Módulos

```
Juego
├── Sesión            dos pasadas, puntuación y monedas, resumen
├── Copiloto          tablet, cursor, diales, notas, copiloto bot
├── Conductor         panel del salpicadero, cámara, señales
├── Acciones          máquina de estados con duraciones en datos
└── Red               host autoritativo, mensajes, cambio de host
Simulación (reproducible)
├── Coche             controlador propio sobre VehicleConstraint de Jolt
├── Tramo             primitivas, malla de colisión, microrrelieve h(s,t)
├── Seguimiento       s y t incrementales, atajos, recolocación
└── rmath             toda la matemática de simulación y generación
Generación
├── Generador         terreno primero, trazado por muestreo, ceguera por diseño
├── Validador         geometría, ritmo, visibilidad, piloto IA
└── Perfiles de ritmo datos producidos por el analizador (Python)
Plataforma
├── Bucle             paso fijo 240 Hz, acumulador, interpolación
├── Render            cámara relativa, sombras en cascada, vegetación, shaders de superficie
├── Audio             canales, mezcla, voz
└── Recursos          glTF low-poly, datos
```

La simulación no depende del render ni del audio. Lo que se dibuja o suena lee el estado de la simulación, nunca al revés.

## 3. Bucle principal

- Paso fijo de física a **240 Hz** con acumulador y tope de pasos por fotograma (evita la espiral de retraso).
- El render dibuja el coche interpolado entre los dos últimos pasos de física.
- **Sincronización vertical a la frecuencia del monitor (60 fps en 60 Hz)** por defecto; modo de 30 fps para equipos justos, sin espera activa.
- La entrada se lee una vez por fotograma y se aplica a todos los pasos de física de ese fotograma.
- **Entrada del conductor.** La plataforma solo da forma, sin depender del tiempo del fotograma: zona muerta del eje X del stick (0,10 interior, 0,95 exterior) y curva |x|^1,5; gatillos con zona muerta de 0,05; acelerador = gatillo derecho y freno = gatillo izquierdo, en ejes separados (frenada con el pie izquierdo); botones por posición: freno de mano = botón de abajo de la cara derecha, recolocar = el de arriba, bajar y subir marcha = hombro izquierdo y derecho. Teclas y botones del mando: una sola tabla en `src/platform/keys.cpp` (`rally_loop`, sin raylib), de la que salen el mapeo, la ayuda en pantalla (H) y la tabla de `07` §8 (un test las compara). Teclado y mando se mezclan por eje (gana el de mayor magnitud) y `SimInput` lleva el objetivo de cada eje y un bit por eje si viene de un control digital (teclado o gatillos de 0 o 1, como los de Switch Pro, detectados por gatillo porque nunca dan un valor intermedio). Mando activo = el último que ha movido un eje o pulsado un botón; si se desconecta, sus ejes pasan a 0 en ese fotograma. Los mandos se leen con SDL3 (raylib los abre y cierra al conectar). Freno de mano, subir marcha, bajar marcha y recolocar son bits de `SimInput` (8 bytes: dirección, acelerador y freno en `int16` y un `uint16` de botones); las pulsaciones se detectan en la simulación, por paso, con los botones del paso anterior (`ControlState::pressed`).
- **Suavizado en la simulación**, por paso de 240 Hz (`sim/input_smoothing.h`): la dirección aplicada sigue al objetivo con un limitador (teclado: al tope en 0,25 s y de vuelta en 0,12 s; stick: 1/30 s) y el acelerador y el freno digitales igual (0,20 s y 0,10 s); los analógicos se aplican tal cual. El freno de mano aplicado sube y baja en 0,05 s. Con objetivo 0 se usa el modo del último objetivo distinto de 0. Por paso y no por fotograma, la respuesta es igual a 30 y a 60 fps y queda en la grabación de entradas. Los valores aplicados son estado de la simulación fuera del `SaveState` de Jolt y viajan en el cambio de host, como los botones del paso anterior. Velocidades provisionales hasta la fase 2.
- **Vibración por impacto:** tras cada paso, la simulación da el cambio de velocidad del cuerpo controlado que no explican la gravedad, el amortiguamiento ni la entrada (solo se lee). La plataforma toma el pico de los pasos del fotograma y, por encima de 1 m/s, vibra 150 ms con fuerza proporcional. Apoyado son 0,04 m/s y conduciendo o deslizando lo mismo; caer desde 3 m da 4,3, aterrizar del salto 3,1 y chocar contra un muro a 13 m/s, 8,5.
- **Texto en pantalla por capas con sitio fijo** (`platform/screen_layout`, en `rally_loop`, probado sin ventana): indicador mínimo, «H: ayuda», mensajes, ayuda de teclas (H), depuración (F3), rendimiento (F1) y gráficas (T). La fuente por defecto de raylib solo se ve limpia en múltiplos de 10 px: texto a 10 × max(1, alto / 540) px; la geometría escala con alto / 1080. El ancho del texto se mide sin raylib con la tabla de glifos de raylib (`platform/ui_text`), que el juego compara con `MeasureText` al arrancar. Un texto que se sale de su capa se avisa en pantalla.
- **Ajustes del jugador** en `ajustes.txt` junto al exe (`platform/player_settings`): `clave = valor` con `std::from_chars`; una línea mal escrita avisa en pantalla con su número y usa el valor por defecto (no aborta: es un archivo del jugador). Se guarda tras 0,5 s sin cambios y al salir, con escritura atómica (`io/atomic_file`: temporal y `MoveFileExW`). Desde la 2.4b, el asiento: desplazamiento del ojo del conductor en cm (± 15), solo para la cámara del ojo.
- Ventana o pantalla completa sin bordes a la resolución nativa del monitor (F11 o Alt+Intro; pantalla completa de escritorio de SDL, sin cambiar el modo de pantalla). La sincronización vertical y el ajuste del dt valen en los dos. El juego se juega en pantalla completa sin bordes: en ventana, con AMD en Windows 10, hay tearing y mucho más ruido de presentación, y eso queda fuera de alcance.

### 3.1 Ajuste del dt al refresco del monitor

Con sincronización vertical, los fotogramas duran un número entero de refrescos, pero el dt se mide al volver de la presentación y no en el refresco: lleva ruido. En ventana con la RX 580 a 60 Hz, un 16 % de los dt se alejan más de 1 ms del periodo y algunos hasta 2,3 ms, en parejas (+x, −x): instantes que llegan tarde. En pantalla completa sin bordes, solo un 0,3 % pasan de 1,5 ms. Con el acumulador a pelo, un fotograma de 16,2 ms da 3 pasos y el siguiente, de 17,1 ms, da 5. Lo que se ve en pantalla avanza `pasos hechos + alpha`, es decir, el dt usado; si ese dt varía un ±5 %, la velocidad aparente también, y en primera persona se nota como tirones. `FixedStepClock` (en `rally_loop`) lo corrige así:

- **Ajuste.** Con sincronización vertical, `FramePacer` le pasa el periodo de un refresco, con la frecuencia exacta como fracción de SDL (60000/1001 en 59,94 Hz). Si el dt medido está dentro de la ventana alrededor de un múltiplo k ≥ 1 del periodo, se usa k × periodo exacto. La ventana es proporcional: el menor de 2,5 ms y el 40 % del periodo (2,5 ms a 60 y 144 Hz; 1,67 ms a 240 Hz, donde 2,5 ms pasaría del medio periodo de 2,08 ms). Así siempre es menos de medio periodo, el múltiplo nunca es ambiguo y queda margen entre ventanas vecinas. Con 2,5 ms, el registro de la RX 580 en ventana (desvíos de hasta 2,36 ms) se ajusta entero; con 1 ms quedaban 295 de 1726 fotogramas sin ajustar, y con 2 ms, 17. Un fotograma perdido (~33 ms a 60 Hz) se ajusta a 2 periodos y da 8 pasos: el ajuste quita el ruido, no esconde los fotogramas perdidos. Fuera de la ventana, el dt pasa tal cual.
- **Deuda.** La diferencia entre lo medido y lo usado se acumula y se devuelve poco a poco: deuda/16 por fotograma, como mucho 0,1 ms, así que el dt usado varía menos de un 0,6 %. Si pasa de 4 ms, se paga entera en ese fotograma. El tiempo de juego no se separa del real más de eso: `tiempo real − deuda − tiempo de juego` es constante. Si un monitor dice 60 Hz y va a 59,95, la deuda se queda estable en ~0,3 ms y cada ~250 fotogramas sale uno de 5 pasos, sin salto visible.
- **Fase centrada.** Al activar el ajuste, la fracción del acumulador se coloca en medio paso, una sola vez. Con fotogramas de un número entero de pasos (60 Hz, 30 fps), alpha se queda cerca de 0,5 y el ruido no cruza el borde entre pasos: siempre 4 pasos a 60 Hz con hasta ±1,5 ms de ruido en el dt. Es un desplazamiento fijo del origen del tiempo de juego (menos de medio paso, 2,1 ms), no deuda: si se devolviera, alpha volvería al borde. Un fotograma fuera de la ventana no vuelve a centrar: suele ser un instante tardío que el siguiente compensa, y centrar sobre él deja alpha cerca del borde (con el registro de la RX 580 y 2 ms de ventana, 39 fotogramas de 3 o 5 pasos en 29 s frente a 19).
- **Qué se nota y qué no.** Lo que mueve la imagen a saltos son los fotogramas sin ajustar, que aplican el dt medido con su ruido. Un fotograma ajustado de 3 o 5 pasos avanza el tiempo de juego igual de suave: solo cambia en qué fotograma cae un paso. Pasa cuando el monitor no va a la frecuencia que dice (la RX 580 da 60 Hz y va a 60,015): alpha deriva, cruza el borde cada ~17 s y en el cruce hay unos fotogramas de 3 y 5 pasos de vaivén. Es lo correcto si el tiempo de juego no debe separarse del real.
- El acumulador y la deuda son enteros (ns × 240; un paso son 10⁹ unidades y 1/60 s, 4 × 10⁹ exactos). El tope de pasos por fotograma descarta también la deuda.
- Es solo del bucle de la plataforma: la simulación sigue viendo solo números de paso.
- Diagnóstico: `FrameHistory` guarda los últimos 240 fotogramas y la gráfica del contador (`frame_graph`) muestra su duración medida, el tiempo aplicado y los pasos, separa los fotogramas con pasos distintos de los previstos en perdidos (≥ 1,5 periodos) y ruido, y cuenta los sin ajustar. `FrameLog` escribe cada fotograma en `fotogramas_NNN.csv` junto al exe (tecla L en la demo, o `--frame-log` al arrancar; `--fullscreen` arranca en pantalla completa). `tests/data/frames_rx580_60hz.txt` es uno de esos registros y el test lo repite.

## 4. Física

### 4.1 Configuración de Jolt

- Doble precisión, con el origen en el centro del tramo y render relativo a la cámara (se resta la posición de la cámara en doble antes de pasar a float). Cuesta un 5–10 % de la física.
- Determinista entre plataformas y en un solo hilo: los callbacks de contacto llegan en orden fijo.
- Compilación sin `/fp:fast` y con `/fp:contract` desactivado.

### 4.2 Reglas de reproducibilidad

Jolt da el mismo resultado si las llamadas que modifican la simulación se hacen exactamente en el mismo orden ([documentación](https://github.com/jrouwe/JoltPhysics/blob/master/Docs/Architecture.md#deterministic-simulation)). Reglas del proyecto:

1. **Nada se añade ni se quita durante una pasada.** El tramo entero entra en Jolt antes de la salida, en lote y en orden fijo.
2. Toda la matemática de simulación y generación pasa por `rmath`. Al principio envuelve `std::`; si en la fase 9 hace falta, se sustituye por implementaciones propias.
3. Números aleatorios solo con PCG propio, nunca con las distribuciones de `<random>`.
4. Las entradas se graban por paso de física. Un test comprueba que la misma secuencia da el mismo tiempo dos veces (desde la fase 2).
5. Nada de la simulación lee el reloj del sistema, el número de fotograma ni datos del render.
6. Los identificadores de los cuerpos se crean en orden fijo; si hace falta, con `CreateBodyWithID`.
7. Idéntica entre PCs solo hace falta para las clasificatorias (fase 9): primero `_set_FMA3_enable(0)`, y si no basta, `rmath` propio.

### 4.3 Coche: controlador propio con neumático de calidad (opción B reforzada)

Se usa `VehicleConstraint` de Jolt (suspensión, contacto por cylinder cast) con un **`VehicleController` propio** que sustituye el neumático y la transmisión.

- **Fuerzas explícitas del neumático** aplicadas al cuerpo. `VehicleConstraint` solo se usa para la suspensión y el contacto (cylinder cast).
- **Neumático:** fórmula de Pacejka simplificada (B, C, D, E) longitudinal y lateral, combinadas por deslizamiento normalizado (elipse de fricción); el módulo sale del deslizamiento normalizado y la dirección pasa de la del vector normalizado (adherencia) a la de la velocidad de deslizamiento (deslizando), con una mezcla suave entre ρ = 1,5 y 3; el agarre baja al crecer la carga; deformación de la huella como estado (modelo transitorio: relajación de primer orden en desplazamiento, con integración exponencial), que vale a cualquier velocidad, parado incluido; resistencia a la rodadura = coeficiente × carga. Un juego de parámetros por superficie: asfalto, grava, tierra, nieve y arena. La carga de cada rueda sale de la suspensión del paso anterior.
- **Ruedas:** la velocidad de giro de cada rueda se integra con 4 subpasos por paso de física (960 Hz). Los subpasos solos no bastan: la rigidez efectiva de la rueda crece como 1/v y por debajo de unos 2–3 m/s oscilaría también a 960 Hz. Por eso el deslizamiento no se calcula dividiendo por la velocidad: sale de la deformación de la huella (κ_r = u/σ_x, α_r = atan(w/σ_y)). El freno y la rodadura son pares de Coulomb: nunca invierten el giro y bloquean la rueda mientras el par que la mueve no los supere. La fuerza que recibe el cuerpo es la media de los subpasos.
- **Implementación del coche base** (tarea 2.4; `sim/car_controller.*`):
  - Todo el trabajo va en `PostCollide`; `SolveLongitudinalAndLateralConstraints` no aplica nada. La fuerza media de los subpasos se aplica con `AddForce` en el punto de contacto y Jolt la integra en el mismo paso.
  - La carga de cada rueda es `GetSuspensionLambda() / dt` del paso anterior: incluye la barra estabilizadora y el tope.
  - Neumático (`sim/tire.*`, `step_tire`; tarea 2.5): por rueda, la deformación de la huella u y w (m) es estado. Con s_x = ω·r − v_x y s_y = −v_y, du/dt = s_x − (|v_x|/σ_x)·u y dw/dt = s_y − (|v_x|/σ_y)·w, integradas de forma exponencial, con σ_x y σ_y de la superficie. Deslizamiento relajado κ_r = u/σ_x y tan α_r = w/σ_y; normalizado ρ_x = κ_r/κ_pico, ρ_y = tan α_r/tan α_pico, ρ = |(ρ_x, ρ_y)|.
  - Curva normalizada única por superficie, la misma en las dos direcciones: f(ρ) = sin(C·atan(Bρ − E·(Bρ − atan Bρ))), con el pico en ρ = 1 y la asíntota en `slide_ratio`. El `.car` guarda μ pico, α pico, κ pico, `slide_ratio`, E, sensibilidad a la carga, σ_y, σ_x y rodadura por superficie; el lector calcula C = 2 − (2/π)·asin(slide_ratio) (la raíz entre 1 y 2) y B con Newton (30 iteraciones fijas); E ≥ 1 es un error al cargar. Fuerza elástica: módulo D·f(ρ), con D = μ(F_z)·F_z y μ(F_z) = μ pico·(1 − k·(F_z/F_z0 − 1)), F_z/F_z0 en [0, 3] y F_z0 = m·g/4; dirección mezclada (viñeta «Neumático»). |F| ≤ D por construcción.
  - Amortiguamiento de la carcasa (histéresis) sobre la velocidad de deformación de la huella: c_x·g·du/dt y c_y·g·dw/dt en el subpaso, con c = 2·ζ·√(k·F_z/g) y la rigidez de la huella en el origen, k = B·C·D/(κ_pico·σ_x) y B·C·D/(tan α_pico·σ_y). Parado es lo mismo que amortiguar el deslizamiento (du/dt = s_x), y en régimen estacionario (du/dt = 0) no cambia la curva ni la rigidez en curva; amortiguar el deslizamiento, c·s, sí la cambiaba a velocidad. g = clamp(1 − v_ref/`carcass_damping_fade`, 0, 1) con v_ref = max(|v| en el contacto, |ω|·r): 1 parado con la rueda quieta y 0 con el coche o la rueda por encima de la rampa, que llega hasta 10 m/s de v_ref (por encima, la relajación sola da ζ ≥ 0,4). Por debajo de la rampa, la fuerza al aterrizar no pasa por la relajación: el primer paso con carga ya lleva el amortiguamiento. Motivo: con la rigidez de la Pacejka, la guiñada del coche sobre la rigidez lateral de la huella (Cα/σ_y) serpenteaba a baja velocidad (4,5 Hz en asfalto, 2,4 Hz en grava, ζ ≈ 0,15 a 10 km/h), con la carcasa apagada a 3 m/s y solo la relajación para amortiguarla.
  - Velocidad del contacto: la del cuerpo en el punto de contacto (v + ω×r), salvo cuando Jolt no va a girar el cuerpo en el paso: `Body::AddRotationStep` no gira si |ω|·dt ≤ 1e-6 rad (|ω| < 0,24 mrad/s a 240 Hz) pero sí integra la traslación. Ahí el neumático usa la velocidad lineal. Si no, un cabeceo residual que nunca gira el coche hacía que v + ω×r diera cero en los contactos mientras el cuerpo se trasladaba (el coche parado se deslizaba ~4 µm/s). La condición es la misma expresión de Jolt (`PhysicsSystem.cpp:1641` y `Body.inl:92-93`), con la ω de antes del paso: apoyado, el paso frena la ω, y entre 1 y ~1,4 veces el umbral el neumático y Jolt pueden no coincidir durante un paso. Depende de Jolt 5.6.0: al actualizar Jolt, revisar esta condición (test de los dos lados del umbral).
  - Recorte de la huella: si ρ > ρ_max, u y w se escalan juntos hasta ρ_max = 1 + (ρ_alto − 1)·(1 − g). Parado y con la rueda quieta, ρ ≤ 1: límite de Coulomb en el pico, la huella desliza y no acumula (parada en rampa, sin «cuerda»). Con el coche o la rueda por encima de la rampa, ρ_alto es el deslizamiento normalizado de una rueda bloqueada que desliza a 75° (|(1/κ_pico, tan 75°/tan α_pico)|): la rueda entra en la rama descendente y una rueda bloqueada por debajo de 75° no llega al recorte, así que su fuerza va contra su velocidad de deslizamiento (con σ_x ≠ σ_y, el recorte desvía la dirección). Una rueda que patina con el coche parado desliza (v_ref = |ω|·r). La fuerza total (elástica y carcasa) se recorta a D. Sin carga (en el aire), u = w = 0.
  - El giro de la rueda es semi-implícito: ω' = ω + h·(T − r·F_x) / (I + h·r·∂F_x/∂ω), con ∂F_x/∂ω = ∂F_x/∂u·r·φ + c_x·r·g; ∂F_x/∂u es la derivada analítica de la curva con la dirección normalizada, nunca negativa (la rama descendente se trata de forma explícita), y 0 con la huella recortada o la fuerza en el límite. El freno y la rodadura (c_rr de la superficie × F_z × r) entran como pares de Coulomb en el mismo esquema: nunca invierten el giro y dejan la rueda en ω = 0 mientras el par que la mueve no los supere; frenando al límite, la rodadura no se suma a la fricción: la fuerza en el suelo está limitada por μ·F_z.
  - El tester de las ruedas hace el cast del cilindro con el radio de la condición de la rueda y pone el contacto bajo su centro, porque el punto más profundo del cilindro de Jolt salta de un borde al otro del ancho. La media de h se toma sobre 3 × 3 muestras de la huella.
  - Muelle y amortiguador de cada rueda con la masa suspendida de su esquina (carga estática / g): k = m·(2πf)² y c = 2·ζ·√(k·m), en modo `StiffnessAndDamping` de Jolt. Así la frecuencia del `.car` es la del coche (compresión de equilibrio g/(2πf)²). Sin masa no suspendida aparte.
  - Los anclajes de la suspensión se colocan en `build()` con esa rigidez para que el coche quede horizontal en equilibrio.
  - Prueba de estabilidad de las ruedas (barrido): los cambios de signo de ω·r − v se cuentan solo con la rueda girando (una rueda bloqueada por fricción estática tiene ω = 0 y no puede oscilar); con el coche parado y las ruedas bloqueadas todo el último segundo, |v| en el contacto < 1 cm/s.
- **Velocidad cero y baja velocidad:** sin casos especiales. A velocidad cero, la deformación de la huella es un muelle (rigidez B·C·D/σ de la superficie) con límite de Coulomb en el pico: sujeta el coche parado en pendiente con freno, y sin freno hasta lo que sujeta la rodadura. Cerca de parado se suma un amortiguamiento de la carcasa que desaparece a `carcass_damping_fade`. Sin ancla ni mezcla de modelos.
- **Motor:** curva de par según las RPM; turbo con presión de primer orden (constante de tiempo, umbral de RPM, válvula de descarga), que da el retardo real. Sin nitro.
- **Transmisión:** sin embrague ni calado; cambio automático (con espera limitada en derrape) y secuencial; diferencial central con reparto de par fijo y viscoacoplador; autoblocantes delantero y trasero. ABS y control de tracción.
- **Chasis:** frenos con par máximo y reparto por eje; freno de mano en las traseras; barras estabilizadoras (`VehicleAntiRollBar` de Jolt); resistencia aerodinámica (CdA, sin carga aerodinámica); geometría de dirección (ángulo máximo y Ackermann). Disposición por datos: 4×4 con diferencial central o propulsión trasera.
- **Embrague automático interno** para arrancar desde parado: el jugador no tiene embrague ni puede calar, pero entre el motor y las ruedas hay un acoplamiento que patina al arrancar. Con el freno de mano, en los 4×4 se desacoplan el diferencial central y el embrague.
- **Estado del coche dentro de Jolt:** el `VehicleController` propio guarda todo su estado (giro de las ruedas, deslizamientos relajados, motor, turbo, marcha, temporizadores, ABS y control de tracción) en su `SaveState`. Así entra en el `SaveState` de Jolt, en la huella y en el cambio de host. Fuera de Jolt solo quedan los valores aplicados de la entrada (`ControlState`) y los botones del paso anterior. Se divide en estado dinámico (se pone a cero al recolocar) y condición por rueda y del coche (radio efectivo; después daños, presión, pinchazos): persiste al recolocar y viaja en el cambio de host.
- **Construcción:** el cuerpo del coche, la `VehicleConstraint` y su step listener se añaden en `build()`, antes de `seal()`. Cambiar de configuración o de ajustes reconstruye el mundo (antes de la pasada). Recolocar mueve el cuerpo y pone a cero velocidades y estado de las ruedas; nunca quita ni añade nada. El coche no se duerme. Lo que pueda aparecer durante una pasada (gato, ruedas de repuesto y objetos del inventario, fase 8) se crea en `build()` desactivado o aparcado; aparecer es activar o mover, nunca añadir.
- **Posición del ojo del conductor** en el archivo del coche: la usan la cámara y, en la fase 4, el cálculo de ceguera.
- **El coche se define en un archivo de datos** (`data/cars/`): masas, inercias, geometría, suspensión, neumáticos, motor, turbo, transmisión y diferenciales. Cada configuración de prueba es un archivo. Texto `clave = valor` con unidades en el nombre de la clave cuando no son SI (`max_steer_deg`), leído con un lector propio y `std::from_chars` (no depende del idioma de Windows). Sin dependencias nuevas. Una clave desconocida o que falta es un error al cargar.
- **Microrrelieve:** `VehicleCollisionTester` propio que desplaza el contacto según la media de la altura sobre la huella. En la zona de pruebas de la fase 2, `h(x, z)` en coordenadas del mundo; desde la fase 3, `h(s, t)` sobre el tramo. Siempre la misma función para el render y para la rueda. En el modo a ciegas de la zona de pruebas, el dibujo no aplica h, para no delatar la superficie; la rueda sí.
- **Presupuesto:** ≤ 0,5 ms por paso de simulación (coche y mundo de pruebas) en el equipo de pruebas (i3-8100).
- Referencia de estudio: el SDK de vehículos de PhysX es de código abierto (BSD-3); su neumático y su transmisión se pueden estudiar y adaptar citándolo.
- **Prueba de aceptación (fase 2)**, con mando, con la configuración elegida para el juego, medida con la telemetría por `tools/telemetry/drift_check.py`. Ángulo de deriva β = atan2(v lateral, v longitudinal) del centro de masas en ejes del coche, con media móvil de 0,1 s. Trompo = |β| > 90°.
  - **Sostenido** (círculo de grava de 40 m de radio): una ventana continua con |β| > 15°, freno y freno de mano a 0 y el coche a 40 ± 8 m del centro, en la que la posición barre ≥ 180° alrededor del centro.
  - **Progresivo** (mismo círculo): desde un derrape sostenido ≥ 0,5 s, se suelta el acelerador (aplicado < 10 %). En los 0,5 s siguientes, |β| baja al menos un 25 % y el radio de la trayectoria es menor que al soltar; en los 2 s siguientes, sin trompo.
  - **Recogida** (explanada de grava): con ≥ 95 km/h y |β| ≥ 20° en su máximo (t0), y contravolante (dirección hacia el exterior del giro durante ≥ 0,2 s), |β| < 5° durante 0,5 s seguidos antes de t0 + 2 s, sin trompo.
  - **Implementación en `drift_check.py`** (2026-10-04, tarea 2.3, aprobada por Lucas). Valores aplicados de la telemetría (`columns.md`); posición y velocidad del centro de masas.
    - **β y radio:** con velocidad horizontal < 3 m/s, β no está definido. Esos pasos cortan las ventanas, no cuentan para trompo y no entran en la media móvil. El CSV escribe el β calculado y las gráficas en pantalla lo muestran como 0.
      - La media móvil va hacia atrás, de 24 pasos (0,1 s), sobre β desenrollado y solo con los pasos definidos seguidos.
      - El radio de la trayectoria es |v|³ / |v × a| en el plano (x, z), con v y a suavizados con la misma media.
    - **Sostenido:** freno y freno de mano aplicados exactamente a 0. El barrido es el valor absoluto del ángulo neto desenrollado de la posición alrededor del centro dentro de la ventana. Una ventana se informa (PASA o NO PASA) si dura ≥ 0,5 s.
    - **Progresivo:**
      - Soltar (t_r) es el primer paso con el acelerador aplicado < 10 %. Los 0,5 s anteriores cumplen las condiciones del sostenido salvo el barrido, con el acelerador ≥ 10 %.
      - La caída de |β| y el radio se miden en el mismo instante, t_r + 0,5 s, con los valores suavizados: |β(t_r + 0,5)| ≤ 0,75·|β(t_r)| y r(t_r + 0,5) < r(t_r).
      - Sin trompo en [t_r, t_r + 2 s].
    - **Recogida:**
      - t0 es el paso de |β| máximo de cada intervalo seguido con |β| ≥ 20°. Los 95 km/h se exigen al empezar el intervalo (primer paso con |β| ≥ 20°), no en t0.
      - Contravolante: dirección aplicada con el signo de β y |dirección| ≥ 5 % durante ≥ 0,2 s seguidos, entre el inicio del intervalo y t0 + 2 s.
      - Tiempo de recogida: el primer paso desde t0 a partir del cual |β| < 5° se mantiene 0,5 s seguidos, menos t0. Pasa si es < 2,0 s; la ventana de 0,5 s puede acabar después de t0 + 2 s.
      - Sin trompo desde t0 hasta el final de esa ventana.
    - **Candidatos descartados:** además de PASA y NO PASA, el script lista los candidatos que no llegan a evaluarse, con su motivo: velocidad al empezar < 95 km/h, ventana < 0,5 s, derrape fuera de 40 ± 8 m del centro o derrape previo < 0,5 s al soltar.

  Si no pasa tras el ajuste (tarea 2.10), se reconsidera cambiar de motor de física. Referencia: el paso límite por un radio de 40 m en grava es de ~60 km/h (√(μ·g·r), μ ≈ 0,7).
- **Carretera de prueba de la zona (tarea 2.6b, `sim/test_road`)**: el manejo se ajusta y se juzga también en una carretera estrecha, no solo en la zona abierta. Va desde constantes, en el mismo `HeightFieldShape` de la zona: sin piezas de colisión aparte bajo la calzada, por la junta de la 2.5.
  - **Trazado:** 992,5 m de punto a punto al oeste de la zona, con la salida en (−390, 300) hacia el norte. Antes de la salida, una entrada de 20 m, y tras la meta, una frenada de 100 m, sin cronometrar. Piezas: recta de 150 m, derecha R 150 · 40°, recta de 80 m, izquierda que se cierra (R 50 · 60° + R 35 · 30°), S de R 40 · 45° + 45°, cresta, izquierda R 60 · 50°, bajada del 7 %, horquilla a la derecha R 12 · 180°, subida del 5 % con izquierda R 90 · 60° y recta de 100 m.
  - **Sección:**
    - calzada de 6 m, de grava o de asfalto (elegible al construir; U en la demo);
    - arcén de arena de 1,6 m;
    - cuneta de tierra en U de 3,25 m y 0,4 m de fondo (0,75 m de fondo plano y laderas en coseno);
    - berma de 1 m;
    - talud de tierra (28 m rectos y redondeos de 1 m) hasta la altura 0 de la zona, a 30 m del borde de la cuneta.

    Son los anchos mínimos que la rejilla de 0,75 m deja en cualquier ángulo: al menos una celda de arena en la normal y más del 85 % del fondo de la cuneta. Con 1 m de arcén hay secciones sin arena, y una V de 1,5 m conserva el 32 % de su fondo.
  - **Relieve:**
    - eje a +5,55 m en la salida, para centrar el trazado en el 0 de la zona: +7,05 m en la cresta y −7,05 m en la horquilla;
    - curvas verticales de 20 m dentro de cada tramo con pendiente;
    - cresta de 1,5 m en 40 m, con tres parábolas (vaguadas de 6 m y R 40 m; cumbre de R 93 m, que despega a 109 km/h);
    - peralte del 4 % en las curvas 4 y 6 y del −3 % en la 2, con transiciones de 20 m centradas en los extremos del arco;
    - junto a la horquilla, la bajada y la subida se juntan: en trinchera las dos, gana la más baja (continua, sin más pendiente que cada una).
  - **Árboles:** 226 cilindros estáticos de Jolt (r 0,25 m), creados en `build()` detrás de los muros. Van a 8,6–13,6 m del eje, separados 6–15 m en la fila (4–8 m en el exterior de la horquilla y de la curva 4), con PCG32 de semilla fija; ninguno dentro del círculo de la horquilla. Entran en el hash de la zona.
  - **Cronómetro de punto a punto** (`RoadTimer`): cruzar la salida empieza una pasada; recolocar la marca y cuenta las veces; hacia atrás no cuenta. Va al pie de la grabación (formato 6) y a la telemetría (`road_time`, `road_resets`, `road_runs`).
  - **Recolocación:** puntos en s = −10 y cada 100 m. A menos de 40 m del eje, recolocar lleva al último con s ≤ la del coche: nunca hacia delante.
  - **`tools/telemetry/road_check.py`:** por pasada, el tiempo, las recolocaciones, las salidas (centro de masas a más de 4,6 m del eje), la velocidad mínima en la horquilla y el |β| máximo por curva.

### 4.4 Interior del coche [post-1.0]

Segundo `PhysicsSystem` en coordenadas del coche, al que se le aplican como fuerzas las aceleraciones del coche (lineal, centrífuga y de Coriolis). Menos de 10 objetos, dormidos hasta un golpe fuerte. No afecta al manejo. El host lo simula y envía las posiciones de los objetos sueltos; quien coge un objeto tiene su autoridad mientras lo sujeta.

## 5. Seguimiento del coche en el tramo

Coordenadas del trazado: `s` (distancia a lo largo de la carretera) y `t` (desplazamiento lateral).

- **Incremental en cada paso:** se busca solo en la ventana de `s_prev − 20 m` a `s_prev + v·Δt + 20 m`. Proyección exacta en rectas y arcos; 2–3 iteraciones de Newton en clotoides. Así dos lazadas de una horquilla que pasan a pocos metros nunca compiten.
- **Índice espacial** (celdas de 10 m con los intervalos de `s` que pasan por cada una): solo para adquirir la posición al empezar, tras una recolocación o tras un vuelo largo.
- **En carretera** mientras `|t|` es menor que medio ancho de calzada más el arcén. El punto de recolocación solo avanza en carretera.
- **Atajo:** al volver a la carretera, si la ganancia de `s` supera en más de 30 m a lo recorrido fuera, se recoloca en el punto de salida.
- Todo lo que necesita "dónde está el coche" lee `s` y `t`: giro del mapa, microrrelieve, notas, puntuación por curva, recolocación.
- **Tests:** un tramo real con lazadas, recorrido a 10, 50 y 120 km/h, sin saltos ni retrocesos de `s`; y una bajada en línea recta de una lazada a la siguiente, que debe detectarse como atajo.

## 6. Tramos

### 6.1 Formato

Un tramo es una lista de primitivas (recta, clotoide, arco) con superficies y parámetros de entorno. El primer tramo se escribe a mano en ese formato como archivo de texto; el generador produce el mismo formato. Un tramo se guarda como semilla + parámetros + versión del generador (pocos KB).

**Esqueleto e irregularidad.** Las primitivas son el esqueleto: sobre ellas se calculan `s`, `t`, las notas, el ritmo y la tablet. La malla visible y la de colisión se construyen con una capa de irregularidad: desplazamiento lateral del eje d(s), semianchos w_izq(s) y w_der(s), peralte θ(s) y ondulación vertical z(s). Son sumas de pocas ondas sembradas con PCG desde la semilla del tramo, con amplitudes y longitudes de onda en datos por entorno. Se calcula al cargar, nunca en el paso. El borde y la salida de pista usan d(s), w_izq(s) y w_der(s). Las curvas del generador son perfiles de curvatura κ(s) con entrada y salida asimétricas, no solo un arco con clotoides simétricas.

### 6.2 Analizador de tramos reales (herramienta Python, fuera del juego)

Fuentes:

| Fuente | Aporta | Licencia |
| --- | --- | --- |
| OpenStreetMap | Trazado en planta | ODbL, cita obligatoria |
| Mapas de los organizadores de rallies | Qué tramo de carretera es el tramo cronometrado | Solo referencia |
| MDT02 / MDT05 del PNOA-LiDAR (CNIG) | Altura del terreno, malla de 2 y 5 m | CC BY 4.0 |
| MDS normalizado del PNOA-LiDAR | Altura de vegetación y edificios | CC BY 4.0 |
| Copernicus DEM (30 m) | Paisaje de fondo | Libre con cita |

Pasos, sobre 20–40 tramos reales de montaña (asfalto y tierra):

1. Ajuste a primitivas: suavizado de la polilínea, curvatura a lo largo de `s` y división en tramos de curvatura cero, constante o lineal por mínimos cuadrados con programación dinámica.
2. Perfil de altura desde el MDT02: crestas, badenes y radios verticales; residuo vertical frente al perfil suavizado (amplitud y longitudes de onda de la ondulación de escala media).
3. Perfil de visibilidad desde el ojo del conductor con terreno + vegetación.
4. Distribuciones: radios y giros de curva, rectas, separación entre curvas, pares y tríos de curvas, pendientes, radios verticales, fracción ciega, combinaciones de cresta y curva.

Salida: un **perfil de ritmo** por entorno (en la 1.0, solo montaña), un archivo de datos que lee el generador.

### 6.3 Generador

1. **Terreno primero:** relieve grueso y trazado sobre él con coste por pendiente y movimiento de tierras.
2. **Trazado por muestreo** de las distribuciones del perfil; la densidad de curvas desplaza la distribución.
3. **Ceguera por diseño:** crestas, vegetación y muros con la frecuencia que miden los tramos reales.
4. Señales, parciales, peligros y perfiles de velocidad **en los dos sentidos**.

Si una semilla no valida, se prueba la siguiente de forma determinista (semilla + 1). Generador con PCG y `rmath`, mismos flags que la física.

### 6.4 Validador en 4 capas

| Capa | Comprueba | Coste |
| --- | --- | --- |
| 1 · Geometría | Sin autointersecciones; separación entre partes no consecutivas mayor que dos anchos de calzada con arcenes más un margen; pendientes y radios verticales dentro del percentil 99 real | Milisegundos |
| 2 · Ritmo | Distribuciones parecidas a las reales, por debajo de un umbral de distancia estadística | Milisegundos |
| 3 · Visibilidad | Fracción ciega dentro del rango real, calculada con los ocluyentes que el render garantiza | Menos de 1 s |
| 4 · Piloto IA | Corre el tramo con la física real al 90 % de la velocidad de referencia en los dos sentidos: vuelos, aterrizajes fuera de la carretera, curvas imposibles. Su tiempo da el tiempo de referencia de cada coche | Segundos |

Para que la capa 4 no haga esperar en equipos modestos: los dos sentidos en dos hilos a la vez, solo en las zonas de riesgo (crestas, saltos, secuencias cerradas) empezando unos cientos de metros antes, mientras se eligen coche y ajustes, y con caché de semillas ya validadas. El piloto IA es una herramienta interna, no un bot jugable.

### 6.5 Ceguera y render

La ceguera se calcula con geometría y el conductor ve el render; las dos cosas deben coincidir.

- **Ocluyentes declarados:** cada objeto que el cálculo cuenta como opaco tiene una forma de ocultación simple (muro, ladera, masa de bosque).
- **Masa de bosque:** borde de tarjetas solapadas sin transparencia hacia la carretera y relleno oscuro detrás, no árboles sueltos con huecos.
- **Distancia garantizada:** ningún ocluyente declarado se quita por LOD ni culling antes de unos 200 m.
- **Calidad mínima:** la ceguera se calcula con el ajuste gráfico más bajo, y ningún ajuste puede ocultar menos.
- **Herramienta:** el visor de semillas puede renderizar la vista del conductor cada 10 m y comparar la carretera visible con la calculada.

### 6.6 Carga

| Qué | Cómo se carga |
| --- | --- |
| Descripción y validación | Enteras, antes de la salida |
| Física: carretera y franja de terreno de unos ±60 m | Entera, antes de la salida, en lote. Más allá no hay colisión: la regla de atajos recoloca el coche |
| Gráficos: mallas, vegetación, terreno cercano | Por ventana alrededor del coche. Las mallas se calculan en un hilo de fondo y se suben a la gráfica en el hilo principal con un límite por fotograma (OpenGL solo admite subir desde el hilo de su contexto) |
| Paisaje lejano | Baja resolución, entero |

**Sentido contrario:** el mundo es el mismo y no se recarga. Los datos que dependen del sentido ya se calcularon al validar; la pasada 2 empieza al instante.

## 7. Red

- **Host autoritativo:** el PC de quien conduce simula el coche y envía el estado. El copiloto interpola por número de paso con un búfer de 60–150 ms ajustado a la variación de latencia.
- Posición en enteros de 32 bits en milímetros respecto al origen del tramo; rotación en cuaternión comprimido. Paquete de estado < 100 bytes.

| Mensaje | Sentido | Canal | Frecuencia |
| --- | --- | --- | --- |
| Estado del coche (paso, posición, rotación, velocidad, suspensiones, superficies, marcha, RPM, indicador de recolocación) | Host → otro | No fiable | 30 Hz |
| Nota (ancla `s`, sentido, nota, modificadores, distancia) | Copiloto → host | Fiable | Evento |
| Borrar última nota / vaciar cola | Copiloto → host | Fiable | Evento |
| Pedir acción / confirmar acción (paso de inicio y fin) | Ambos | Fiable | Evento |
| Cambio de host | Ambos | Fiable | Evento |
| Control de sesión (semilla, parámetros, versión, hash, ajustes, listo) | Ambos | Fiable | Evento |
| Estado de la luna [post-1.0] | Host → otro | No fiable | 10 Hz |
| Voz [post-1.0] | Ambos | No fiable | 50 paquetes/s |

**Mismo tramo en los dos PCs:** los dos generan desde la semilla; el host envía además la descripción del tramo (pocos KB) y se comparan hashes al conectar. Si no coinciden, se avisa y se juega con la del host.

**Vibración del copiloto:** se reconstruye en local desde la superficie y las suspensiones recibidas.

### 7.1 Cambio de host

No necesita física idéntica entre PCs: tras el cambio, el PC nuevo es la autoridad. Es un relevo, no una simulación en paralelo: en cada momento solo un PC simula el coche y nadie compara resultados. La física idéntica entre PCs solo hace falta para verificar clasificatorias (fase 9).

1. Coche parado (entre pasadas en la 1.0; a mitad de tramo [post-1.0], con los dos jugadores fuera).
2. El host congela la simulación en el paso `N` y envía por canal fiable el `SaveState` de Jolt y el estado del juego (daños, notas, puntuación, acciones, inventario) y el estado de la simulación que Jolt no guarda (botones del paso anterior, dirección y acelerador aplicados).
3. El otro PC restaura (su mundo es el mismo porque nada se añade ni se quita) y responde que está listo.
4. Se intercambian los papeles y la simulación sigue desde el paso `N + 1`. El registro de entradas marca el cambio.

## 8. Render (objetivo: GTX 1060)

- Render relativo a la cámara.
- Sombras del sol en 3 cascadas con textura de profundidad propia (raylib no trae cascadas).
- **Estilo low-poly con colores planos:** color por vértice y paletas, casi sin texturas. Excepción: el suelo lleva ruido procedural en el shader para que se note la velocidad y se distinga cada superficie (triplanar completo solo cerca de la cámara).
- **Dónde va el margen de la GTX 1060:** iluminación y sombras, densidad de vegetación y distancia de dibujado, partículas de polvo y grava, niebla atmosférica y el interior del coche. No en texturas fotográficas.
- **Origen de los modelos:** interior del coche en Blender; exterior a partir de assets CC0 (Kenney, Quaternius) adaptados; árboles, rocas, postes y vallas generados por código con variación por semilla; objetos sueltos con herramientas de IA de imagen a 3D, limpiados y reducidos en Blender.
- Vegetación por instancias, con ocluyentes declarados y con VBO de transformaciones persistente (`DrawMeshInstanced` de raylib reserva memoria y crea un VBO en cada llamada: descartado).
- Interior del coche modelado en Blender, low-poly pero detallado: es lo que más se ve en primera persona.
- Niebla y lluvia por shader [post-1.0]; de noche solo los faros proyectan sombra [post-1.0].
- Desenfoque ligero en los bordes, sin cambiar el campo de visión.
- Contador en pantalla desde el primer día: ms por fotograma, draw calls y memoria.

## 9. Audio y voz

- **1.0:** motor con capas en bucle según RPM y carga (sonidos grabados permitidos), rodadura por superficie, sonido de cada nota por su lado, mezcla por canales.
- **Voz integrada [post-1.0]:** Opus mono a unos 24 kbit/s (unos 3 KB/s por sentido) en paquetes de 20 ms por un canal no fiable de ENet con búfer de reproducción; captura con miniaudio; supresión de ruido ligera; auriculares recomendados para no necesitar cancelación de eco. El juego baja el motor mientras alguien habla y aplica un filtro de intercomunicador de casco.

## 10. Herramientas y tests

- **Tests sin pantalla** (CTest) desde la fase 1: reproducibilidad (mismas entradas, mismo tiempo), seguimiento de `s`, generador y validador, cambio de host, puntuación.
- **Visor de semillas:** el tramo entero con el render de la tablet, perfil de altura, métricas del validador y la comprobación por render de la ceguera.
- **Analizador** en Python en `tools/analyzer`.
- **Copiloto bot de desarrollo** desde la fase 3: coloca notas correctas a partir de la velocidad de referencia, para probar sin depender de otra persona.
- **Grabación de entradas** desde la fase 2 (`.rrec`, biblioteca `rally_io` en `src/io`, sin raylib ni SDL): cabecera con versión de formato, versión de la simulación (`kSimVersion`), commit (con «-dirty» si había cambios sin commit, calculado en cada build), el archivo del coche y los ajustes completos (no solo su hash: así una grabación antigua se puede repetir después de ajustar el coche) y la descripción del mundo; un `SimInput` por paso, comprimido por repetición (RLE, tramos de `u32` repeticiones + `SimInput`, 12 B, sin dependencias); al final, el número de pasos, el tiempo del recorrido, el hash del estado final (FNV-1a del SaveState más `ControlState`) y si está completa. Todo en little-endian, campo a campo. La partida graba desde que construye el mundo hasta que lo reinicia o sale, en `grabaciones/AAAAMMDD-HHMMSS.rrec` junto al exe. Si salta un `RALLY_CHECK`, el manejador guarda antes de mostrar el error lo grabado hasta el último paso terminado, marcado como incompleto (sin hash). Sirve para "mismo tiempo dos veces" y como test de regresión del manejo.
- **`rally_replay`** (`tools/replay`): programa de consola que solo enlaza la simulación y `rally_io`. `info` (cabecera y pie), `run` (repite y dice «hash OK» o el primer problema; con otra `kSimVersion` o incompleta, repite sin comparar el hash y avisa), `run --bench N` (ms por paso: media, p99 y máximo) y `rewrite` (pie con la versión actual, solo para grabaciones sintéticas de tests); desde la 2.3, también la telemetría. En git se guardan las grabaciones (KB) y la telemetría se regenera.
- **Telemetría** desde la fase 2: CSV por paso escrito por `rally_replay` (cargas, deslizamientos, fuerzas y giro por rueda, β, velocidad de guiñada, RPM, presión del turbo, marcha, entradas y valores aplicados), gráficas en pantalla durante la partida y scripts de Python en `tools/telemetry` (curvas, comparación de dos grabaciones y la prueba de derrape).
- **Test de huella con un coche congelado** (`tests/data/cars/reference.car`), nunca con los de `data/cars/`: ajustar el coche no mueve la huella. La huella solo cambia cuando cambia el código de la simulación.

## 11. Estructura del repositorio (propuesta)

```
rally-coop/
├── CLAUDE.md
├── .claude/rules/simulacion.md
├── CMakeLists.txt  CMakePresets.json
├── docs/            diseño, arquitectura, requisitos, plan, decisiones, progreso
├── external/        raylib, jolt, enet (submódulos o FetchContent)
├── src/
│   ├── platform/    bucle, render, audio, entrada
│   ├── sim/         coche, tramo, seguimiento, rmath
│   ├── gen/         generador y validador
│   ├── game/        sesión, copiloto, conductor, acciones, puntuación
│   └── net/         red y cambio de host
├── tests/
├── tools/           analizador (Python), visor de semillas
└── data/            perfiles de ritmo, duraciones de acciones, tramos a mano
```
