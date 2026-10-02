# Adenda al diseño de la fase 2

Aplicada el 2026-10-02 en el commit `074faeb`. Desde entonces, la fuente de verdad son `docs/01` a `05`.

Revisión del diseño cerrado el 2026-10-02 (`diseno-fase-2.md`), hecha antes de implementar. Corrige contradicciones y rellena huecos sin cambiar las decisiones de Lucas: coche de referencia, cinco superficies, física realista y estándar, tareas 2.1–2.11 y prueba de derrape medida.

Se aplica **junto con `diseno-fase-2.md`, en el mismo commit** (tarea 2.0). Una vez aplicada, la fuente de verdad son `docs/01` a `05`. Este archivo se queda en `docs/prompts/fase-2/` como registro del porqué.

---

## 0. Decisiones que confirma Lucas antes de aplicar

Cada una lleva marcada la opción por defecto. Si eliges otra, cambia la marca **antes** de pedir la tarea 2.0. Claude Code aplica la opción marcada y, si alguna línea tiene las dos marcadas o ninguna, para y pregunta.

| # | Decisión | Opción |
| --- | --- | --- |
| D1 | Ayuda de dirección con mando y teclado | [ ] Siempre activa, sin contar como ayuda · [x] Ajuste de cada jugador (activada o desactivada; activada por defecto), registrado con la pasada como las demás ayudas *(Lucas, 2026-10-02)* |
| D2 | "Potencia ajustable hasta Evo 2" | [x] Parámetro del archivo del coche (lo tocamos al ajustar) · [ ] Ajuste del jugador *(Lucas, 2026-10-02: la que mejor encaje; ver nota)* |
| D3 | Freno de mano en coches 4×4 | [x] Mientras se tira, desacopla el diferencial central y el embrague automático, como los coches de rally reales · [ ] Solo frena las traseras *(confirmada por Lucas, 2026-10-02)* |
| D4 | Marcha atrás y punto muerto | [ ] Automático: freno mantenido 0,3 s parado · [ ] Botón propio · [x] Secuencia R – N – 1ª en los dos modos: bajar de 1ª pasa a punto muerto; bajar de punto muerto con el coche parado engrana la marcha atrás; subir recorre el camino contrario *(Lucas, 2026-10-02)* |
| D5 | Criterio de la prueba a ciegas | [x] Cada jugador acierta al menos 10 de 12 y ninguna superficie se falla más de una vez (al azar se aciertan 3) · [ ] Otro umbral: ____ *(confirmada por Lucas, 2026-10-02)* |
| D6 | Carpeta de los coches | [x] `data/cars/` (la ruta aparece en el código, que va en inglés) · [ ] `data/coches/` *(confirmada por Lucas, 2026-10-02)* |
| D7 | Con qué configuración tiene que pasar la prueba de derrape | [x] Con la elegida para el juego; las otras tres solo se comparan jugando · [ ] Con las cuatro *(confirmada por Lucas, 2026-10-02)* |

**Nota D2.** Como ajuste del jugador, la potencia cambiaría el tiempo de referencia con el que se pagan las monedas (`01` §6.1, "t_ref se calcula con el coche elegido") y sería un selector de dificultad escondido. Como dato del coche, sirve para lo que se quería: encontrar en la 2.10 la potencia que da la dificultad buena, y que quede fija para todos.

---

## 1. Cambios a los documentos

Se aplican después de los de `diseno-fase-2.md`. Donde los dos tocan el mismo texto, manda esta adenda.

### 1.1 `docs/01-diseno-de-juego.md`

**§7.1, párrafo "Coche de la 1.0"** (añadido por el diseño): al final de la frase "La potencia es ajustable hasta valores de Evo 2 (~450 CV) para encontrar la dificultad adecuada", añade " (es un parámetro del archivo del coche, no un ajuste del jugador)". *(D2)*

**§7.1, párrafo "Dirección"**, añade al final:

> La ayuda es un ajuste de cada jugador: activada o desactivada, activada por defecto, y se registra con la pasada como las demás ayudas. Sin ella, la dirección a fondo da el ángulo máximo de las ruedas a cualquier velocidad. El ángulo de máximo agarre depende de la superficie que pisan las ruedas delanteras. *(D1)*

**§7.1, tabla de ajustes**, fila nueva después de "Control de tracción": `| Ayuda de dirección | Activada / desactivada | Limita el ángulo de las ruedas delanteras al de máximo agarre (ver "Dirección") |` *(D1)*

**§7.2 Cambio de marchas**, añade al final:

> - **Punto muerto y marcha atrás:** la caja va R – N – 1ª – … – 5ª, en automático y en secuencial. Bajar de 1ª pasa a punto muerto (N); bajar de N con el coche parado (menos de 2 km/h) engrana la marcha atrás (R); subir recorre el camino contrario. En N el motor gira libre y no empuja. En R, el acelerador empuja hacia atrás y el freno frena. En automático, los botones de marcha solo sirven para elegir R, N o marcha adelante (desde 1ª, el automático hace el resto), y bajar a N solo se puede parado. Al construir el mundo o recolocar, el coche sale en 1ª. *(D4)*
> - **Freno de mano:** bloquea las ruedas traseras. En los coches 4×4, mientras se tira, se desacoplan el diferencial central y el embrague, como en los coches de rally reales: así las delanteras no luchan contra las traseras bloqueadas. *(D3)*

### 1.2 `docs/02-arquitectura.md`

**§3, viñeta "Entrada del conductor"**: sustituye "acelerador = gatillo derecho − gatillo izquierdo" por "acelerador = gatillo derecho y freno = gatillo izquierdo, en ejes separados (frenada con el pie izquierdo)". Al final de la viñeta, añade: "Freno de mano, subir marcha, bajar marcha y recolocar son bits de `SimInput`; las pulsaciones se detectan en la simulación, por paso."

**§4.3, lista de viñetas** (la que deja el diseño):

- Sustituye la viñeta **Ruedas** por:
  > - **Ruedas:** la velocidad de giro de cada rueda se integra con 4 subpasos por paso de física (960 Hz). Los subpasos solos no bastan: la rigidez efectiva de la rueda crece como 1/v y por debajo de unos 2–3 m/s oscilaría también a 960 Hz. Por eso el deslizamiento longitudinal usa un denominador mínimo (como el SDK de PhysX) y el par de freno nunca invierte el giro de la rueda dentro de un subpaso. La fuerza que recibe el cuerpo es la media de los subpasos.
- En la viñeta **Neumático**, cambia "relajación de primer orden" por "relajación de primer orden con actualización exponencial (estable a cualquier velocidad)" y añade al final: "La carga de cada rueda sale de la suspensión del paso anterior."
- Añade tras la viñeta **Transmisión**:
  > - **Chasis:** frenos con par máximo y reparto por eje; freno de mano en las traseras; barras estabilizadoras (`VehicleAntiRollBar` de Jolt); resistencia aerodinámica (CdA, sin carga aerodinámica); geometría de dirección (ángulo máximo y Ackermann). Disposición por datos: 4×4 con diferencial central o propulsión trasera.
  > - **Embrague automático interno** para arrancar desde parado: el jugador no tiene embrague ni puede calar, pero entre el motor y las ruedas hay un acoplamiento que patina al arrancar. Con el freno de mano, en los 4×4 se desacoplan el diferencial central y el embrague.
  > - **Estado del coche dentro de Jolt:** el `VehicleController` propio guarda todo su estado (giro de las ruedas, deslizamientos relajados, motor, turbo, marcha, temporizadores, ABS y control de tracción) en su `SaveState`. Así entra en el `SaveState` de Jolt, en la huella y en el cambio de host. Fuera de Jolt solo quedan los valores aplicados de la entrada (`ControlState`) y los botones del paso anterior.
  > - **Construcción:** el cuerpo del coche, la `VehicleConstraint` y su step listener se añaden en `build()`, antes de `seal()`. Cambiar de configuración o de ajustes reconstruye el mundo (antes de la pasada). Recolocar mueve el cuerpo y pone a cero velocidades y estado de las ruedas; nunca quita ni añade nada. El coche no se duerme.
  > - **Posición del ojo del conductor** en el archivo del coche: la usan la cámara y, en la fase 4, el cálculo de ceguera.
- En la viñeta **El coche se define en un archivo de datos**, cambia `data/coches/` por `data/cars/` y añade: "Texto `clave = valor` con unidades en el nombre de la clave cuando no son SI (`max_steer_deg`), leído con un lector propio y `std::from_chars` (no depende del idioma de Windows). Sin dependencias nuevas. Una clave desconocida o que falta es un error al cargar." *(D6)*

**§4.3, "Prueba de aceptación (fase 2)"**: sustituye el bloque que deja el diseño por:

> - **Prueba de aceptación (fase 2)**, con mando, con la configuración elegida para el juego, medida con la telemetría por `tools/telemetry/drift_check.py`. Ángulo de deriva β = atan2(v lateral, v longitudinal) del centro de masas en ejes del coche, con media móvil de 0,1 s. Trompo = |β| > 90°.
>   - **Sostenido** (círculo de grava de 40 m de radio): una ventana continua con |β| > 15°, freno y freno de mano a 0 y el coche a 40 ± 8 m del centro, en la que la posición barre ≥ 180° alrededor del centro.
>   - **Progresivo** (mismo círculo): desde un derrape sostenido ≥ 0,5 s, se suelta el acelerador (aplicado < 10 %). En los 0,5 s siguientes, |β| baja al menos un 25 % y el radio de la trayectoria es menor que al soltar; en los 2 s siguientes, sin trompo.
>   - **Recogida** (explanada de grava): con ≥ 95 km/h y |β| ≥ 20° en su máximo (t0), y contravolante (dirección hacia el exterior del giro durante ≥ 0,2 s), |β| < 5° durante 0,5 s seguidos antes de t0 + 2 s, sin trompo.
>
>   Si no pasa tras el ajuste (tarea 2.10), se reconsidera cambiar de motor de física. Referencia: el paso límite por un radio de 40 m en grava es de ~60 km/h (√(μ·g·r), μ ≈ 0,7).

**§10, lista**: sustituye las dos viñetas que añade el diseño (Telemetría y Grabación de entradas) por:

> - **Grabación de entradas** desde la fase 2: cabecera con versión de formato, versión de la simulación, commit, el archivo del coche y los ajustes completos (no solo su hash: así una grabación antigua se puede repetir después de ajustar el coche) y la descripción del mundo; un `SimInput` por paso, comprimido por repetición (RLE, sin dependencias); al final, el número de pasos, el tiempo del recorrido y el hash del estado final. Sirve para "mismo tiempo dos veces" y como test de regresión del manejo.
> - **`rally_replay`**: programa de consola que solo enlaza la simulación. Repite una grabación, comprueba el hash final, escribe la telemetría y mide los ms por paso. En git se guardan las grabaciones (KB) y la telemetría se regenera.
> - **Telemetría** desde la fase 2: CSV por paso escrito por `rally_replay` (cargas, deslizamientos, fuerzas y giro por rueda, β, velocidad de guiñada, RPM, presión del turbo, marcha, entradas y valores aplicados), gráficas en pantalla durante la partida y scripts de Python en `tools/telemetry` (curvas, comparación de dos grabaciones y la prueba de derrape).
> - **Test de huella con un coche congelado** (`tests/data/cars/reference.car`), nunca con los de `data/cars/`: ajustar el coche no mueve la huella. La huella solo cambia cuando cambia el código de la simulación.

### 1.3 `docs/03-requisitos.md`

**§2, tabla "Riesgos de rendimiento"**, fila "Neumático de calidad a 240 Hz", columna Medida: "Un solo coche; ≤ 0,5 ms por paso con `rally_replay run <grabación> --bench` y el contador (F1). Peor caso: 8 pasos por fotograma en el modo de 30 fps (4 ms)."

### 1.4 `docs/04-plan-de-fases.md`

**Sección "Fase 2 · Coche"**, tabla de tareas (la que deja el diseño):

- 2.1 → "Entrada del coche (`SimInput` con dirección, acelerador, freno, freno de mano, marchas y recolocar), grabación de entradas y `rally_replay`" · horas **3–4**.
- 2.2 → "Zona de pruebas: círculo de grava de 40 m, explanada de grava de 400 × 80 m (recogida a 100 km/h), eslalon, horquilla, recta con baches y ondulaciones, rampa del 15 %, cresta, parches de asfalto, tierra y nieve, pista ciega con la superficie elegida al construir y recorrido cronometrado; superficie por material; `h(x, z)`" · horas **4–6**.
- 2.4 → "Vehículo base: `VehicleConstraint` y controlador propio, suspensión y barras, frenos, dirección, resistencia aerodinámica, subpasos de rueda, baja velocidad, coche desde archivo de datos y recarga en caliente" · horas **7–11**.
- 2.7 → añade al final ", modo a ciegas (sin color, sonido ni vibración de superficie)".

Título "Fase 2 · Coche (~61–109 h)". En la tabla Resumen, fase 2: **61–109**; Total 1.0: **~235–415**. (Las tareas suman 61–99; el resto es el mismo margen alto que dejaba el diseño. Ver §2: en el diseño, las tareas sumaban 58–95 y el título decía 55–105.)

**"Terminado cuando"** de la fase 2:
- La primera viñeta pasa a: "pasa la prueba de derrape medida (`02` §4.3: sostenido, progresivo y recogida) con la configuración elegida". *(D7)*
- La de las superficies pasa a: "asfalto, grava, tierra y nieve se distinguen a ciegas por el manejo: cada jugador acierta al menos 10 de 12 pruebas y ninguna superficie se falla más de una vez (la arena, en la fase 4)". *(D5)*
- La de la reproducibilidad pasa a: "la misma secuencia de entradas da el mismo tiempo dos veces, también la partida frente a `rally_replay`".

### 1.5 `docs/05-decisiones.md`

**Tabla Técnicas**, fila "Telemetría y grabación" (la añade el diseño), texto nuevo: "Grabación de entradas por paso (RLE, con el coche y los ajustes completos en la cabecera); `rally_replay` la repite, escribe la telemetría CSV y mide; gráficas en pantalla y scripts de Python; huella con un coche congelado en `tests/data`" · Cerrada.

**Tabla Técnicas**, fila nueva: `| Datos del coche | data/cars/, texto clave = valor con unidades en la clave, lector propio con std::from_chars; sin dependencias | Cerrada |`

**Tabla Diseño de juego**:
- Fila "Dirección", texto nuevo: "Ayuda ajustable por cada jugador (activada por defecto) y registrada con la pasada: limita el ángulo de las ruedas delanteras al de máximo agarre de la superficie respecto a su velocidad; desde unos 30 km/h, mezcla de 15 a 30, giro completo por debajo y en marcha atrás" · Provisional hasta la fase 2. *(D1)*
- Fila nueva: `| Freno de mano y marcha atrás | Freno de mano en las traseras; en 4×4 desacopla el diferencial central y el embrague. Caja R – N – 1ª–5ª en los dos modos: bajar de 1ª da punto muerto y bajar de N parado da marcha atrás; se sale en 1ª | Provisional hasta la fase 2 |` *(D3, D4)*
- Fila "Coche de la 1.0" (la añade el diseño): cambia "potencia ajustable hasta ~Evo 2" por "potencia ajustable hasta ~Evo 2 en el archivo del coche". *(D2)*

**Tabla Cambios**, filas nuevas (después de las del diseño):

```
| 2026-10-02 | Marcha atrás y punto muerto | Sin definir | Secuencia R – N – 1ª en automático y secuencial; marcha atrás solo parado | Elección de Lucas |
| 2026-10-02 | Dirección | Ayuda sin ajuste | Ayuda activable por cada jugador, activada por defecto y registrada como ayuda | Elección de Lucas: cada uno decide cómo conduce |
| 2026-10-02 | Entrada | Acelerador = gatillo derecho − gatillo izquierdo | Acelerador y freno en ejes separados; freno de mano, marchas y recolocar como bits | Frenada con el pie izquierdo y marcha atrás definida |
| 2026-10-02 | Física del coche (detalle) | Subpasos de rueda | Subpasos más denominador mínimo de deslizamiento y freno que no invierte el giro; barras, frenos, aerodinámica, embrague automático; estado del coche en el SaveState del controlador | Revisión antes de implementar: estabilidad a baja velocidad y elementos que faltaban |
| 2026-10-02 | Prueba de derrape | Criterio en texto | Definiciones medibles de sostenido, progresivo y recogida; explanada de grava para la recogida a 100 km/h | El círculo de 40 m no permite 100 km/h (límite ~60 km/h); el script tiene que poder decidir solo |
| 2026-10-02 | Telemetría y grabación | CSV en la partida | La partida graba entradas; rally_replay regenera la telemetría; huella con coche congelado | Guardar en git solo grabaciones; ajustar el coche no mueve la huella |
| 2026-10-02 | Estimación fase 2 | 55–105 h | 61–109 h | Zona de pruebas ampliada, rally_replay y chasis completo; el mínimo pasa a ser la suma de las tareas (antes, por debajo de ella) |
```

### 1.6 `docs/08-bitacora-de-desarrollo.md`

**§5 "Fase 2 · Coche"**, añade después de la línea del diseño:

> - **Estabilidad de la rueda a baja velocidad.** Con un neumático explícito, la rigidez efectiva de la rueda crece como 1/v: a 2 m/s oscila incluso a 960 Hz. Solución estándar: denominador mínimo en el deslizamiento y freno que no invierte el giro. Test de barrido de velocidades.
> - **Freno de mano en un 4×4.** Si el diferencial central sigue acoplado, las delanteras arrastran a las traseras y la horquilla no sale. Se desacopla mientras se tira.
> - **Grava frente a tierra a ciegas** es la pareja más difícil de distinguir. Personalidades distintas desde los valores de partida: la grava, flotante y progresiva; la tierra, con más agarre y caída más brusca.
> - **El ajuste mueve la huella** si el test usa el coche del juego: por eso el test usa un coche congelado.

**§8**, en la fila de la fase 2: 61–109 (total ~235–415), con la nota "ampliada el 2026-10-02 en la revisión del diseño: zona de pruebas, `rally_replay` y chasis completo".

### 1.7 `docs/progreso.md`

Fase 2: horas previstas **61–109**.

---

## 2. Por qué (resumen de la revisión)

**Contradicciones**
1. `02` §3 seguía diciendo "acelerador = gatillo derecho − gatillo izquierdo", y el diseño separa acelerador y freno.
2. La recogida a 100 km/h no cabe en el círculo de 40 m, cuyo límite en grava es ~60 km/h, y la zona de pruebas no tenía otro sitio de grava.
3. "Distinguir a ciegas" necesita una pista cuya superficie no se vea ni se oiga, y la zona solo tenía parches visibles.
4. "Mismo tiempo dos veces" necesita un tiempo, y la zona no tenía un recorrido cronometrado.
5. Si el test de huella usa el coche del juego, cada ajuste de la 2.10 la rompe.
6. "Progresivo ... sin pasar a trompo en menos de 0,5 s" admite dos lecturas. Un script tiene que poder decidir sin interpretar.
7. Las horas de las tareas 2.1–2.11 suman 58–95, y el título de la fase dice 55–105: el mínimo queda por debajo de la suma de las tareas.

**Huecos de física**
1. **Estabilidad de la rueda.** Con una rigidez longitudinal de ~50 kN por unidad de deslizamiento, un radio de 0,31 m y una inercia de 1,2 kg·m², la tasa efectiva es ≈ 4000/v s⁻¹. A 960 Hz, el límite explícito (2) se pasa por debajo de ~2 m/s, y la mezcla de baja velocidad empieza justo ahí. Hace falta el denominador mínimo y el freno que no invierte el giro.
2. **Elementos que faltaban:** frenos y reparto, barras estabilizadoras (el principal mando del equilibrio junto con los diferenciales), resistencia aerodinámica (sin ella, la velocidad punta solo la da el limitador), geometría de dirección y un embrague automático para arrancar sin calar.
3. **Freno de mano en 4×4** con viscoacoplador: sin desacoplarlo, la horquilla no sale.
4. **Dónde vive el estado del coche:** en el `SaveState` del controlador, para que la huella y el cambio de host lo cubran.
5. **Configuraciones y ajustes** que cambian masas, suspensión o geometría: el mundo se reconstruye. No es "añadir durante una pasada", porque se elige antes.

**Para la fase 3 (no cambia nada ahora)**
- **Fuera de la carretera:** las cinco superficies son de calzada. El terreno de la franja de ±60 m necesita una superficie propia (hierba o tierra blanda, poco agarre y mucha rodadura). En la zona de pruebas, el fondo es tierra; se decide en el diseño de la fase 3.

**Herramientas**
1. **Grabación comprimida** sin dependencias nuevas: RLE.
2. **`rally_replay`:** sin él, la telemetría solo se puede sacar jugando y no se puede regenerar.
3. **Python:** matplotlib es una dependencia de herramienta; se pregunta en la 2.3.
