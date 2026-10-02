# Rally Coop — Plan de fases

Versión final de trabajo · 1 de octubre de 2026

La 1.0 son las fases 1 a 6 (la fase 0 se omitió): el juego cooperativo completo para jugar con un amigo, en un entorno, con un coche, seco de día. Sin fecha fija: el avance se mide con las horas reales de cada fase frente a las previstas (`progreso.md`).

**Reglas del plan**
- Cada fase termina con algo jugable y medido, y no se pasa a la siguiente hasta cumplir su criterio.
- Las decisiones de diseño se validan siempre jugando con otra persona.
- La fase 3 comprueba la idea con el juego real antes de invertir en el generador.
- Al terminar las fases 1 y 2 se rehace la estimación con horas reales.

## Resumen

| Fase | Contenido | Horas previstas |
| --- | --- | --- |
| 0 · Pruebas del concepto | Omitida (ver `05-decisiones.md`) | 0 |
| 1 · Base del motor | Bucle, render básico, entrada, Jolt configurado, tests | ~6 (real) |
| 2 · Coche | Coche inspirado en un Grupo B 4×4, neumático Pacejka con cinco superficies, motor con turbo, transmisión, ayudas, telemetría, cuatro configuraciones de prueba | 61–109 |
| 3 · Corte vertical | Tramo a mano, seguimiento, tablet, diales, panel, red local, registro de notas | 45–75 |
| 4 · Generador | Analizador, generador, validador con piloto IA, puntuación, dos pasadas | 50–90 |
| 5 · Entre casas | Red real, cambio de host, resumen, menús | 20–40 |
| 6 · Calidad visual y sonora | Interior, sombras, vegetación, partículas, sonido; medida en GTX 1060 | 50–90 |
| **Total 1.0** | | **~230–410** |

Revisado al cerrar la fase 1 (2026-10-02): el código de infraestructura se comprime mucho con Claude Code; las horas de ajuste jugando, pruebas con otra persona, Blender, sonido y datos reales no. Se revisa de nuevo al cerrar la fase 2.

## Fase 0 · Pruebas del concepto (~30 h)

**Omitida el 2026-10-01.** Sus preguntas se responden en la fase 3 con el juego real. Se conserva como referencia: si el corte vertical sale dudoso, estas pruebas son la forma barata de aislar el problema.

**Objetivo:** saber si cantar y conducir así es divertido antes de escribir el motor.

**Prueba A · DiRT Rally 2.0 con mapa propio (un fin de semana)**
1. En `Documents\My Games\DiRT Rally 2.0\hardwaresettings\hardware_settings_config.xml`, la línea `udp` con `enabled="true"` y `extradata="3"`; puerto 20777; en `ip`, la IP del PC del copiloto.
2. El copiloto recorre a solas y despacio un tramo que el conductor no conozca; un script en Python graba las posiciones (el trazado).
3. El conductor corre ese tramo con las notas del juego quitadas.
4. El script dibuja el trazado orientado por la carretera, con marcas cada 50 m y el coche moviéndose. El copiloto canta por Discord sin ver la pantalla del conductor.
5. Se prueban las dos orientaciones del mapa: por la tangente y por un punto 30–50 m por delante, sobre todo en horquillas.

**Prueba B · Prototipo 2D a dos jugadores (otro fin de semana)**
- Prototipo desechable para probar la mecánica de cantar y conducir en un fin de semana. El juego es 3D; de este prototipo no se reutiliza nada salvo el formato de tramo y lo aprendido.
- Python con pygame, dos procesos conectados por UDP.
- Conductor: vista cenital girada con el coche, solo un cono de unos 40 m visible; coche 2D sencillo que derrapa.
- Copiloto: mapa con 8–10 s por delante, cursor libre, dial principal, dial de distancias y borrar.
- Carretera de primitivas en el formato de texto del juego. Fuera de la carretera la hierba frena; los árboles son choque y recolocación.
- Panel del conductor: curva · distancia · curva. Notas no dibujadas en el mapa.

**Terminado cuando** sabéis: si engancha; qué echa en falta el copiloto; si el dial de dos gestos y el de distancias se usan bien con prisa; cuántas notas por minuto aguanta el copiloto con el dial (techo de densidad de curvas); cuánto se equivoca al estimar metros; si pierde la cuenta de lo que ha cantado sin notas en el mapa; qué orientación del mapa funciona en horquillas; y qué decisiones provisionales se confirman.

## Fase 1 · Base del motor (~6 h reales)

CMake y presets; raylib con backend SDL; ventana y bucle con paso fijo de 240 Hz, acumulador, interpolación y tope de pasos; sincronización vertical a la frecuencia del monitor y modo de 30; cámara en primera persona con render relativo a la cámara; entrada por mando y teclado con suavizado de dirección; vibración; contador de rendimiento. Jolt con doble precisión, determinista entre plataformas y en un hilo; flags sin fast-math ni contracción FMA; `rmath` como envoltorio de `std::`; PCG. Banco de tests sin pantalla con CTest.

**Terminado cuando:** un cuerpo de Jolt se mueve a 240 Hz sin tirones a 30 y 60 fps, responde igual con mando y teclado, el mando vibra, el contador muestra ms por fotograma, draw calls y memoria, y los tests corren con un comando.

## Fase 2 · Coche (~61–109 h)

Coche de la 1.0 inspirado en el Peugeot 205 T16 Evo 1 (ver `01` §7.1), con física realista y estándar: fuerzas explícitas del neumático con Pacejka simplificada, fricción combinada, relajación, sensibilidad a la carga y resistencia a la rodadura; cinco superficies (asfalto, grava, tierra, nieve y arena); subpasos de rueda y tratamiento de baja velocidad; motor con curva de par y turbo con retardo; cambio automático y secuencial; diferencial central con viscoacoplador y autoblocantes; freno de mano; ABS, control de tracción, dureza de suspensión y ayuda de dirección según la velocidad. Coche definido en datos, con cuatro configuraciones de prueba (205 T16, tipo Quattro S1, tipo Escort Mk2 y tipo Delta Integrale). Entrada con acelerador y freno separados. Telemetría y grabación de entradas. Interior provisional con medidas reales, movimiento de cabeza y sonido sintético provisional del motor y de la rodadura. Máquina de acciones con duraciones en datos y anillo de progreso.

**Tareas**

| # | Tarea | Horas |
| --- | --- | --- |
| 2.1 | Entrada del coche (`SimInput` con dirección, acelerador, freno, freno de mano, marchas y recolocar), grabación de entradas y `rally_replay` | 3–4 |
| 2.2 | Zona de pruebas: círculo de grava de 40 m, explanada de grava de 400 × 80 m (recogida a 100 km/h), eslalon, horquilla, recta con baches y ondulaciones, rampa del 15 %, cresta, parches de asfalto, tierra y nieve, pista ciega con la superficie elegida al construir y recorrido cronometrado; superficie por material; `h(x, z)` | 4–6 |
| 2.3 | Telemetría: CSV por paso, gráficas en pantalla, script de Python | 4–6 |
| 2.4 | Vehículo base: `VehicleConstraint` y controlador propio, suspensión y barras, frenos, dirección, resistencia aerodinámica, subpasos de rueda, baja velocidad, coche desde archivo de datos y recarga en caliente | 7–11 |
| 2.5 | Neumático: Pacejka, fricción combinada, carga, relajación, rodadura, cinco superficies | 8–13 |
| 2.6 | Motor y transmisión: par, turbo, cambios, diferencial central y autoblocantes | 6–10 |
| 2.7 | Conductor en el coche: interior provisional, cabeza, vibración por superficie y deslizamiento, sonido sintético, modo a ciegas (sin color, sonido ni vibración de superficie) | 5–8 |
| 2.8 | Ayudas y ajustes por jugador: dirección, ABS, control de tracción, suspensión, sensibilidad | 4–6 |
| 2.9 | Máquina de acciones con duraciones en datos y anillo de progreso | 3–4 |
| 2.10 | Ajuste y pruebas de manejo: prueba de derrape medida, las cinco superficies, las cuatro configuraciones con el compañero | 15–28 |
| 2.11 | Cierre: rendimiento, reproducibilidad, criterio y nueva estimación | 2–3 |

Las tareas suman 61–99 h; el resto, hasta 109, es margen.

**Antes de empezar:** probar los mandos de Xbox, PS5 y Switch Pro (gatillos digitales).

**Terminado cuando:**
- pasa la prueba de derrape medida (`02` §4.3: sostenido, progresivo y recogida) con la configuración elegida;
- una horquilla se toma sin maniobrar;
- cada ajuste se nota;
- los baches se notan sin golpes falsos;
- asfalto, grava, tierra, nieve y arena se distinguen a ciegas por el manejo: cada jugador acierta al menos 13 de 15 pruebas (tres por superficie) y ninguna superficie se falla más de una vez;
- el turbo obliga a anticipar el acelerador;
- la misma secuencia de entradas da el mismo tiempo dos veces, también la partida frente a `rally_replay`;
- la simulación cuesta ≤ 0,5 ms por paso en el equipo de pruebas;
- las cuatro configuraciones se han probado con el compañero y se ha elegido la del juego.

## Fase 3 · Corte vertical (~45–75 h)

Formato de tramo por primitivas y un tramo de 3–5 km escrito a mano con crestas, curvas tapadas y horquillas; malla de carretera con arcenes cosidos y franja de terreno con colisión; física entera cargada; gráficos por ventana en segundo plano; seguimiento de `s` y `t`, recolocación, atajos y parciales; velocidad de referencia y perfil de velocidad. Tablet (mapa, perfil, cinta, cursor libre), diales principal, secundario y de distancias, borrar; panel del salpicadero con sonido por lado. Segunda instancia conectada por localhost con ENet, host autoritativo, interpolación por número de paso. Copiloto bot de desarrollo. Registro de notas (paso, ancla `s`, nota, distancia elegida y distancia real a la curva siguiente) y las dos orientaciones del mapa (tangente y punto 30–50 m por delante) conmutables con una tecla.

**Terminado cuando:** una persona canta mirando solo la tablet, primero por voz y después solo con los diales, y otra termina el tramo; cada nota aparece en su sitio; los tests de `s` en horquillas pasan; y los dos quieren repetir. Con el registro de notas se fijan el techo de densidad de curvas, la orientación del mapa en horquillas y las decisiones de tablet, cursor, diales y panel que estaban pendientes de la fase 0. Si no es divertido, se corrige el diseño antes de construir el generador.

## Fase 4 · Generador (~50–90 h)

Banco de 20–40 tramos reales de montaña y analizador en Python con perfil de ritmo; generador con terreno primero, trazado por muestreo y ceguera por diseño; ocluyentes declarados; validador en 4 capas con piloto IA en los dos sentidos (zonas de riesgo, 2 hilos, caché); visor de semillas con comprobación por render; señales y parciales en los dos sentidos. Puntuación por curva, racha, monedas y medallas como rangos; sesión de dos pasadas con la segunda en sentido contrario; daños con integridad; sensación de velocidad. Regla del generador para colocar la arena (lechos de río, zonas arenosas, `01` §7.3).

**Terminado cuando:** de 10 semillas seguidas que pasan el validador, los dos jugadores aceptan al menos 8 como tramos de rally creíbles en los dos sentidos; un tramo de 30 km no tiembla al final; ningún tramo aceptado es imposible; la validación no se nota antes de salir; y la puntuación premia lo que se siente como una buena pasada.

## Fase 5 · Entre casas (~20–40 h)

Conexión por Tailscale con medida de la variación de latencia y búfer de interpolación ajustable; posición en enteros de 32 bits; hash del tramo al conectar; cambio de host entre pasadas con `SaveState`; vibración local del copiloto; resumen de pasada y de sesión; menús y sala mínimos (conexión por IP, semilla y parámetros, coche, ajustes por jugador, listo).

**Terminado cuando:** se juega una sesión completa de dos pasadas entre dos casas sin tirones visibles, el cambio de host no se nota, cada nota aparece en su sitio y el resumen muestra dónde se perdió el tiempo.

## Fase 6 · Calidad visual y sonora (~50–90 h)

Interior del coche modelado en Blender; sombras en 3 cascadas; vegetación y masas de bosque; shaders de superficie; partículas de polvo y grava; sonido de motor por capas y mezcla por canales; ajustes gráficos; medida en GTX 1060 o RX 580. Ajuste de ritmo de fotogramas: frecuencia del monitor ÷ N (p. ej. 72 fps en 144 Hz) además del modo de 30, para equipos que no sostienen la frecuencia del monitor.

**Terminado cuando:** 1080p a 60 fps estables en el equipo mínimo en el caso más caro, y el juego se ve y suena como queréis enseñarlo. **Aquí termina la 1.0.**

## Después de la 1.0 (orden provisional; horas a estimar al cerrar la 1.0)

- **Fase 7 · Barro, clima, noche y modo difícil:** charcos, limpiaparabrisas, líquido; lluvia, niebla, nieve; día y noche con faros; modo difícil.
- **Fase 8 · Fuera del coche:** salir, intercambio de roles a mitad de tramo con cambio de host, pinchazos, ventanas, viseras, luna, inventario 3D con física, refrigerante, gasolina en difícil. Personajes a pie creados al construir el mundo (nada se añade durante la pasada); autoridad por entidad (cada PC su personaje, el coche quien lo conduce); coche aparcado dormido mientras nadie conduce.
- **Fase 9 · Tienda, personalización y clasificatorias:** accesorios, tienda, coches nuevos de uno en uno; física idéntica entre PCs, tramo diario o semanal, repeticiones y fantasmas. La grabación incluye cada cambio de host con el estado transferido.
- **Fase 10 · Editor y tramos insignia:** editor visual, tramos reales jugables, segundo entorno (pueblo-campo).
- **Fase 11 · Modo 1 jugador:** copiloto bot con errores humanos sistemáticos y nivel configurable.
- **Fase 12 · Voz integrada y pulido continuo:** llamada en el juego, modo recce, más contenido.

## Lista de corte

Si las horas reales apuntan a un plan demasiado largo, se aplaza por este orden:

1. Resumen del tramo reducido a tiempo perdido por zona.
2. Cinta de ruta, a después de la 1.0.
3. Dial secundario reducido a peligros.
4. Número de tramos reales del banco reducido al mínimo que dé distribuciones estables.

Ya aplazados fuera de la 1.0: fotogramas de 30 a 240, conversor binario propio, ISO 8608 en dos bandas, tramos reales jugables, segundo entorno, barro y limpiaparabrisas.
