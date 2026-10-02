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
- **Entrada del conductor.** La plataforma solo da forma, sin depender del tiempo del fotograma: zona muerta del eje X del stick (0,10 interior, 0,95 exterior) y curva |x|^1,5; gatillos con zona muerta de 0,05; acelerador = gatillo derecho − gatillo izquierdo; botones por posición (salto = botón de abajo de la cara derecha). Teclado y mando se mezclan por eje (gana el de mayor magnitud) y `SimInput` lleva el objetivo de cada eje y un bit por eje si viene de un control digital (teclado o gatillos de 0 o 1, como los de Switch Pro, detectados porque nunca dan un valor intermedio). Mando activo = el último que ha movido un eje o pulsado un botón; si se desconecta, sus ejes pasan a 0 en ese fotograma. Los mandos se leen con SDL3 (raylib los abre y cierra al conectar).
- **Suavizado en la simulación**, por paso de 240 Hz (`sim/input_smoothing.h`): la dirección aplicada sigue al objetivo con un limitador (teclado: al tope en 0,25 s y de vuelta en 0,12 s; stick: 1/30 s) y el acelerador digital igual (0,20 s y 0,10 s); el analógico se aplica tal cual. Con objetivo 0 se usa el modo del último objetivo distinto de 0. Por paso y no por fotograma, la respuesta es igual a 30 y a 60 fps y queda en la grabación de entradas. Los valores aplicados son estado de la simulación fuera del `SaveState` de Jolt y viajan en el cambio de host, como los botones del paso anterior. Velocidades provisionales hasta la fase 2.
- **Vibración por impacto:** tras cada paso, la simulación da el cambio de velocidad del cuerpo controlado que no explican la gravedad, el amortiguamiento ni la entrada (solo se lee). La plataforma toma el pico de los pasos del fotograma y, por encima de 1 m/s, vibra 150 ms con fuerza proporcional. Apoyado son 0,04 m/s y conduciendo o deslizando lo mismo; caer desde 3 m da 4,3, aterrizar del salto 3,1 y chocar contra un muro a 13 m/s, 8,5.
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

- Fricción combinada con su dirección real (elipse de fricción), no solo un límite al tamaño del impulso. El controlador de ruedas estándar de Jolt limita cada eje por separado.
- Relajación del neumático: la fuerza tarda unos centímetros de rodadura en crecer.
- Sensibilidad a la carga y curvas propias por superficie.
- Transmisión sin embrague, automático con espera limitada en derrape y secuencial manual; ABS y control de tracción.
- `VehicleCollisionTester` propio que desplaza el contacto según la media de `h(s, t)` sobre la huella del neumático.
- Referencia de estudio: el SDK de vehículos de PhysX es de código abierto (BSD-3); su neumático y su transmisión se pueden estudiar y adaptar citándolo.
- **Prueba de aceptación (fase 2):** en un círculo de grava de 40 m de radio y con mando, se sostiene un derrape durante media vuelta solo con acelerador y dirección; al levantar el pie el coche cierra la trayectoria sin trompear de golpe; un derrape a 100 km/h se recoge con contravolante. Si no pasa tras el ajuste, se reconsidera cambiar de motor de física.

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
2. Perfil de altura desde el MDT02: crestas, badenes y radios verticales.
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

No necesita física idéntica entre PCs: tras el cambio, el PC nuevo es la autoridad.

1. Coche parado (entre pasadas en la 1.0; a mitad de tramo [post-1.0], con los dos jugadores fuera).
2. El host congela la simulación en el paso `N` y envía por canal fiable el `SaveState` de Jolt y el estado del juego (daños, notas, puntuación, acciones, inventario).
3. El otro PC restaura (su mundo es el mismo porque nada se añade ni se quita) y responde que está listo.
4. Se intercambian los papeles y la simulación sigue desde el paso `N + 1`. El registro de entradas marca el cambio.

## 8. Render (objetivo: GTX 1060)

- Render relativo a la cámara.
- Sombras del sol en 3 cascadas con textura de profundidad propia (raylib no trae cascadas).
- **Estilo low-poly con colores planos:** color por vértice y paletas, casi sin texturas. Excepción: el suelo lleva ruido procedural en el shader para que se note la velocidad y se distinga cada superficie (triplanar completo solo cerca de la cámara).
- **Dónde va el margen de la GTX 1060:** iluminación y sombras, densidad de vegetación y distancia de dibujado, partículas de polvo y grava, niebla atmosférica y el interior del coche. No en texturas fotográficas.
- **Origen de los modelos:** interior del coche en Blender; exterior a partir de assets CC0 (Kenney, Quaternius) adaptados; árboles, rocas, postes y vallas generados por código con variación por semilla; objetos sueltos con herramientas de IA de imagen a 3D, limpiados y reducidos en Blender.
- Vegetación por instancias, con ocluyentes declarados.
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
