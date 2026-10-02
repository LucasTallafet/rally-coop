# Rally Coop — Registro de decisiones

Versión final de trabajo · 1 de octubre de 2026

**Cerrada:** no se reabre salvo que una prueba demuestre que falla. **Provisional:** se confirma o se cambia en la fase indicada, con datos. Cada cambio se anota aquí con su fecha y su motivo.

## Proyecto

| Tema | Decisión | Estado |
| --- | --- | --- |
| Enfoque | Uso personal, no comercial; sin hoja de ruta online | Cerrada |
| Objetivo | Calidad primero desde el día 1; sin fecha fija, avance medido con el plan | Cerrada |
| Público | Parejas de amigos que buscan un reto juntos; modo 1 jugador después de la 1.0 | Cerrada |
| Alcance 1.0 | Fases 1 a 6 (la 0 se omitió): cooperativo completo, montaña, un coche, seco de día, generador con validador, dos pasadas, monedas | Cerrada |
| Regla de alcance | Solo se implementa lo que este juego usa; lo demás se aplaza a su fase | Cerrada |
| Pruebas | Las decisiones de diseño se validan jugando con otra persona; copiloto bot de desarrollo desde la fase 3 | Cerrada |

## Técnicas

| Tema | Decisión | Estado |
| --- | --- | --- |
| Motor | Propio en C++: raylib (backend SDL) + Jolt + ENet; se descartan Godot y otros motores | Cerrada |
| Plataforma | Windows 10/11 de 64 bits | Cerrada |
| Requisitos mínimos | 4 núcleos a 3 GHz, 8 GB, GTX 1060 6 GB o RX 580, 8 GB de disco; 1080p a 60 fps | Cerrada |
| Física del coche | Controlador propio en `VehicleConstraint` de Jolt con fuerzas explícitas del neumático (Pacejka simplificada, fricción combinada, relajación, sensibilidad a la carga, rodadura), subpasos de rueda y tratamiento de baja velocidad; coche definido en datos; otro motor de física solo si falla la prueba de derrape medida | Provisional hasta la fase 2 |
| Reproducibilidad | Jolt determinista en un hilo, `rmath`, PCG, sin fast-math; nada se añade ni se quita durante una pasada; mismo tiempo en el mismo PC desde la fase 2; idéntica entre PCs en la fase 9 | Cerrada |
| Conjunto de instrucciones | SSE4.2 como máximo en todo el código (Jolt incluido): sin AVX, AVX2, AVX-512, LZCNT, TZCNT, F16C ni FMA | Cerrada |
| Precisión | Doble precisión en Jolt, origen en el centro, render relativo a la cámara, posición por red en mm con enteros de 32 bits | Cerrada |
| Carga del tramo | Generado y validado entero; física entera con franja de terreno de ±60 m; gráficos por ventana; sentido contrario sin recargar | Provisional hasta la fase 3 (medida de memoria) |
| Seguimiento | `s` incremental con ventana; índice espacial solo para adquirir; atajo si se ganan más de 30 m de trazado | Cerrada (umbral provisional) |
| Generador | Terreno primero, perfiles de ritmo de tramos reales, ceguera por diseño, semilla + 1 si falla | Provisional hasta la fase 4 |
| Validador | 4 capas: geometría, ritmo, visibilidad, piloto IA en los dos sentidos | Provisional hasta la fase 4 |
| Ceguera y render | Ocluyentes declarados, opacos hasta unos 200 m, calculados con el ajuste gráfico más bajo | Cerrada |
| Red | Host autoritativo en el PC de quien conduce; interpolación por paso con búfer de 60–150 ms | Cerrada |
| Cambio de host | Con el coche parado, por `SaveState`; sin física idéntica entre PCs | Cerrada |
| Fotogramas | Sincronización vertical a la frecuencia del monitor (60 fps en 60 Hz) y modo de 30 sin espera activa; física a 240 Hz | Cerrada |
| Modo de pantalla | Pantalla completa sin bordes para jugar y medir; la ventana es solo para desarrollo | Cerrada |
| Entrada | Zona muerta y curva en la plataforma (sin memoria); suavizado de dirección y acelerador digital en la simulación, por paso de 240 Hz; botones por posición; mando activo = último usado; acelerador y freno separados (frenada con el pie izquierdo) | Provisional hasta la fase 2 (velocidades) |
| Estilo visual | 3D en primera persona, low-poly con colores planos; ruido procedural solo en el suelo | Cerrada |
| Render | Sombras en 3 cascadas, vegetación por instancias, partículas, sin texturas fotográficas, sin cambiar el campo de visión | Cerrada |
| Voz | Discord al principio; llamada integrada con Opus después de la 1.0 | Cerrada |
| Implementación | Con Claude Code, guiada por `CLAUDE.md` y tests sin pantalla | Cerrada |
| Telemetría y grabación | Grabación de entradas por paso (RLE, con el coche y los ajustes completos en la cabecera); `rally_replay` la repite, escribe la telemetría CSV y mide; gráficas en pantalla y scripts de Python; huella con un coche congelado en `tests/data` | Cerrada |
| Datos del coche | `data/cars/`, texto `clave = valor` con unidades en la clave, lector propio con `std::from_chars`; sin dependencias | Cerrada |

## Diseño de juego

| Tema | Decisión | Estado |
| --- | --- | --- |
| Roles | El conductor conduce y ve poco; el copiloto ve la tablet y canta | Cerrada |
| Sesión | Dos pasadas, la segunda en sentido contrario con roles cambiados | Provisional hasta la fase 4 |
| Tablet | Geometría sin notas; mapa por la tangente con zoom fijo; 8–10 s de anticipación | Provisional hasta la fase 3 |
| Giro del mapa en horquillas | Se prueba la tangente frente a un punto 30–50 m por delante | Provisional hasta la fase 3 |
| Cinta de ruta | Marca cambios de dirección de más de unos 5° con criterio solo geométrico | Provisional hasta la fase 3 |
| Notas en la tablet | No se dibujan; solo en la pantalla del salpicadero | Provisional hasta la fase 3 |
| Cursor | Libre sobre todo el trazado; salta entre cambios de dirección o se desliza | Provisional hasta la fase 3 |
| Escala de notas | De 6 a H por velocidad de paso en grava seca | Provisional hasta la fase 3 |
| Diales | Principal de 14 (dos gestos con mando), secundario de 8, distancias de 8 en el gatillo izquierdo | Provisional hasta la fase 3 |
| Panel del conductor | Curva · distancia · curva; solo lo que pone el copiloto | Provisional hasta la fase 3 |
| Notas | El juego nunca las corrige, filtra ni une; borrar última o vaciar cola | Cerrada |
| Densidad de curvas | Máximo fijado con las notas por minuto medidas en la fase 3 | Provisional hasta la fase 3 |
| Puntuación | Por curva con racha, solo el primer intento, sin puntuar las notas | Provisional hasta la fase 4 |
| Monedas | Por minuto de tramo, con tiempo de referencia del coche elegido; medallas como rangos | Provisional hasta la fase 4 |
| Tienda | Abre con la personalización, después de la 1.0; coches nuevos de uno en uno | Cerrada |
| Ayudas | Configurables por cada jugador; registradas con cada pasada; bloqueadas en modo difícil | Cerrada |
| Dirección | Ayuda ajustable por cada jugador (activada por defecto) y registrada con la pasada: limita el ángulo de las ruedas delanteras al de máximo agarre de la superficie respecto a su velocidad; desde unos 30 km/h, mezcla de 15 a 30, giro completo por debajo y en marcha atrás | Provisional hasta la fase 2 |
| Freno de mano y marcha atrás | Freno de mano en las traseras; en 4×4 desacopla el diferencial central y el embrague. Caja R – N – 1ª–5ª en los dos modos: bajar de 1ª da punto muerto y bajar de N parado da marcha atrás; se sale en 1ª | Provisional hasta la fase 2 |
| Cambio automático | Retrasa el cambio en derrape como mucho 0,5–1 s; nunca contra el limitador | Cerrada |
| Acciones | Todas con fases de duración en datos; la duración es la regla | Cerrada |
| Salidas de pista | Recolocación en el punto más avanzado en carretera con penalización; sin rebobinado | Cerrada |
| Daños | Integridad que baja con impactos sobre un umbral; a 0, fin de la pasada | Cerrada |
| Señales | Solo en peligros graves; ninguna en difícil | Cerrada |
| Modo 1 jugador | Después de la 1.0; el único bot es el copiloto; da monedas, menos cuanto más fiable es el bot | Cerrada |
| Fuera del coche | Salir, intercambio a mitad de tramo, pinchazos, ventanas, viseras, luna, inventario 3D, refrigerante, gasolina en difícil; después de la 1.0 | Provisional hasta su fase |
| Modo recce | Para el futuro | Cerrada |
| Coche de la 1.0 | Diseño propio inspirado en el Peugeot 205 T16 Evo 1 (Grupo B, 4×4 con motor central, turbo con retardo real); potencia ajustable hasta ~Evo 2 en el archivo del coche; sin nitro ni ayudas artificiales | Provisional hasta la fase 2 (pruebas de manejo) |
| Configuraciones de prueba | 205 T16, tipo Quattro S1, tipo Escort Mk2 y tipo Delta Integrale, como archivos de datos del mismo modelo | Provisional hasta la fase 2 |
| Superficies | Asfalto, grava, tierra, nieve y arena en la 1.0; barro después | Cerrada |

## Cambios

| Fecha | Decisión | Antes | Después | Motivo |
| --- | --- | --- | --- | --- |
| 2026-10-01 | Recompensa | Medallas por tiempo y tabla local | Monedas por puntuación en el primer intento | Repetir la semilla dejaba al copiloto sin trabajo |
| 2026-10-01 | Física del coche | `WheeledVehicleController` de Jolt | Controlador propio con neumático de calidad | Calidad del manejo |
| 2026-10-01 | Carga del tramo | Por trozos alrededor del coche | Física entera; gráficos por ventana | Validar el tramo entero y simplificar la reproducibilidad |
| 2026-10-01 | Requisitos | Gráfica integrada, < 100 MB | GTX 1060, ≤ 1 GB | Más vegetación, sombras y distancia de dibujado manteniendo el estilo low-poly |
| 2026-10-01 | Alcance 1.0 | Incluía barro y limpiaparabrisas | Después de la 1.0 | Calidad antes que cantidad |
| 2026-10-01 | Fase 0 | Pruebas del concepto antes de escribir el motor | Omitida; sus mediciones pasan al corte vertical (fase 3) | Se desarrolla el juego en cualquier caso; la idea se comprueba con el juego real en la fase 3 |
| 2026-10-01 | Conjunto de instrucciones | AVX2 por defecto de Jolt | SSE4.2 | Cubrir los requisitos mínimos (4 núcleos a 3 GHz) sin excluir CPUs anteriores a 2013 |
| 2026-10-01 | Fotogramas | 60 fps con sincronización vertical | Frecuencia del monitor con sincronización vertical | En 144/165 Hz limitar a 60 no tiene divisor entero y da más tirones; el requisito de 60 fps se mide en 60 Hz |
| 2026-10-02 | Entrada | Sin definir | Suavizado en la simulación por paso; forma de la entrada en la plataforma | La respuesta tiene que ser igual a 30 y 60 fps y quedar grabada en las repeticiones |
| 2026-10-02 | Modo de pantalla | Sin definir | Pantalla completa sin bordes | En ventana, con AMD en Windows 10, tearing y ruido de presentación fuera de nuestro control (276 fotogramas desviados frente a 5) |
| 2026-10-02 | Estimación 1.0 | 560–700 h | ~220–390 h | Fase 1 real ~6 h frente a 60; se comprime el código, no el ajuste ni las pruebas |
| 2026-10-02 | Coche de la 1.0 | Sin definir | Inspirado en el Peugeot 205 T16 Evo 1 (Grupo B 4×4, motor central, turbo) | Elección de Lucas; manejo de referencia con cifras reales |
| 2026-10-02 | Superficies | Asfalto, grava, tierra y nieve | Más arena | Variedad de manejo; ~4–6 h más en la fase 2 |
| 2026-10-02 | Física del coche | Controlador propio con neumático de calidad | Concretado: fuerzas explícitas, Pacejka simplificada, subpasos de rueda, baja velocidad, turbo, diferenciales; prueba de derrape medible | Solución realista y estándar, decidida antes de implementar |
| 2026-10-02 | Estimación fase 2 | 50–90 h | 60–110 h | Arena, turbo y cuatro configuraciones de prueba |
| 2026-10-02 | Marcha atrás y punto muerto | Sin definir | Secuencia R – N – 1ª en automático y secuencial; marcha atrás solo parado | Elección de Lucas |
| 2026-10-02 | Dirección | Ayuda sin ajuste | Ayuda activable por cada jugador, activada por defecto y registrada como ayuda | Elección de Lucas: cada uno decide cómo conduce |
| 2026-10-02 | Entrada | Acelerador = gatillo derecho − gatillo izquierdo | Acelerador y freno en ejes separados; freno de mano, marchas y recolocar como bits | Frenada con el pie izquierdo y marcha atrás definida |
| 2026-10-02 | Física del coche (detalle) | Subpasos de rueda | Subpasos más denominador mínimo de deslizamiento y freno que no invierte el giro; barras, frenos, aerodinámica, embrague automático; estado del coche en el SaveState del controlador | Revisión antes de implementar: estabilidad a baja velocidad y elementos que faltaban |
| 2026-10-02 | Prueba de derrape | Criterio en texto | Definiciones medibles de sostenido, progresivo y recogida; explanada de grava para la recogida a 100 km/h | El círculo de 40 m no permite 100 km/h (límite ~60 km/h); el script tiene que poder decidir solo |
| 2026-10-02 | Telemetría y grabación | CSV en la partida | La partida graba entradas; rally_replay regenera la telemetría; huella con coche congelado | Guardar en git solo grabaciones; ajustar el coche no mueve la huella |
| 2026-10-02 | Estimación fase 2 | 60–110 h | 61–109 h | Zona de pruebas ampliada, rally_replay y chasis completo; el mínimo pasa a ser la suma de las tareas (antes, por debajo de ella) |
