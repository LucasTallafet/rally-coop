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
| Forma de desarrollo | Por lotes: un director y subagentes por carriles con dueño de rutas, revisores de diseño y de determinismo y verificador independientes, con modelo fijo; rama integracion con un lote sin promover como máximo; Lucas juega una vez por lote y promueve a master; las garantías (rutas, master, compilaciones, fusión) las imponen scripts y hooks que fallan cerrados (09-agentes.md) | Provisional hasta el cierre de la fase 3 |

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
| Trazado | Primitivas (recta, clotoide, arco) como esqueleto para `s`, `t`, notas y ritmo; malla con una capa de irregularidad determinista (eje, ancho, peralte, ondulación vertical, bordes); curvas con curvatura variable y asimétrica | Provisional hasta la fase 3 |
| Seguimiento | `s` incremental con ventana; índice espacial solo para adquirir; atajo si se ganan más de 30 m de trazado | Cerrada (umbral provisional) |
| Generador | Terreno primero, perfiles de ritmo de tramos reales, ceguera por diseño, semilla + 1 si falla | Provisional hasta la fase 4 |
| Validador | 4 capas: geometría, ritmo, visibilidad, piloto IA en los dos sentidos | Provisional hasta la fase 4 |
| Ceguera y render | Ocluyentes declarados, opacos hasta unos 200 m, calculados con el ajuste gráfico más bajo | Cerrada |
| Red | Host autoritativo en el PC de quien conduce; interpolación por paso con búfer de 60–150 ms | Cerrada |
| Cambio de host | Con el coche parado, por `SaveState`; sin física idéntica entre PCs | Cerrada |
| Fotogramas | Sincronización vertical a la frecuencia del monitor (60 fps en 60 Hz) y modo de 30 sin espera activa; física a 240 Hz | Cerrada |
| Modo de pantalla | Pantalla completa sin bordes para jugar y medir; la ventana es solo para desarrollo | Cerrada |
| Entrada | Zona muerta y curva en la plataforma (sin memoria), con el exponente de la curva del stick según la sensibilidad de cada jugador (1,5 / sensibilidad; el stick a fondo da siempre el tope); suavizado de dirección y acelerador digital en la simulación, por paso de 240 Hz; botones por posición; mando activo = último usado; acelerador y freno separados (frenada con el pie izquierdo); en PC, dirección con teclado por defecto o con ratón (en posición, opción de cada jugador) | Provisional hasta la fase 2 (velocidades) |
| Microrrelieve | `h` en `src/sim`, la misma función para la rueda y el dibujo (salvo en el modo a ciegas de la zona de pruebas, donde no se dibuja). Zona de pruebas: banda de λ 2–8 m, seis ondas planas por superficie sembradas con PCG32, RMS de 2/4/5/6/8 mm (asfalto, nieve, tierra, grava, arena), mezcla en 2 m entre superficies; ondulaciones de la grava de los baches de 0,8 m y 2 cm. El número de ondas y el reparto de amplitudes por longitud de onda también se ajustan | Provisional hasta la fase 2 (2.10) |
| Estilo visual | 3D en primera persona, low-poly con colores planos; ruido procedural solo en el suelo | Cerrada |
| Render | Sombras en 3 cascadas, vegetación por instancias, partículas, sin texturas fotográficas, sin cambiar el campo de visión | Cerrada |
| Voz | Discord al principio; llamada integrada con Opus después de la 1.0 | Cerrada |
| Implementación | Con Claude Code, guiada por `CLAUDE.md` y tests sin pantalla; por lotes en paralelo desde la P1b (`09-agentes.md`) | Cerrada |
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
| Ayudas | Configurables por cada jugador; registradas con cada pasada; en modo difícil, ABS y control de tracción bloqueados (la ayuda de dirección no) | Cerrada |
| Dirección | Ayuda ajustable por cada jugador (activada por defecto) y registrada con la pasada: limita el ángulo de las ruedas delanteras al de máximo agarre de la superficie respecto a su velocidad, calculado en la rueda exterior y solo hacia el centro (nunca gira más de lo pedido ni cruza el 0); sin límite en contravolante (girar hacia el lado de la trasera cuando esta pasa de su pico); desde unos 30 km/h, mezcla de 15 a 30, giro completo por debajo y en marcha atrás | Provisional hasta la fase 2 |
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
| 2026-10-02 | Manejo | Arcade con base de simulación | Realista; accesible gracias a las ayudas configurables | Coherencia con el diseño de la fase 2 (física realista y estándar) |
| 2026-10-02 | Ayudas (alcance) | Bloqueadas en modo difícil | ABS y control de tracción bloqueados en modo difícil; la ayuda de dirección no | Aclaración tras D1: la decisión se refería al ABS y al control de tracción; la ayuda de dirección compensa el mando, no la habilidad |
| 2026-10-02 | Prueba a ciegas | 4 superficies, 10 de 12 | 5 superficies, 13 de 15 | La arena se ajusta en la fase 2 (decisión "Superficies") |
| 2026-10-02 | Estimación 1.0 | ~220–390 h | ~230–410 h | Fase 2 ampliada en su diseño y en la revisión |
| 2026-10-03 | Microrrelieve | λ 1–4 m; amplitudes de 3 mm a 2 cm (prompt 2.2) | λ 2–8 m; RMS de 2 a 8 mm según la superficie | Los valores de partida daban pendientes de hasta 0,25 (golpes falsos a 100 km/h); los nuevos siguen los órdenes de magnitud de la ISO 8608 (asfalto ≈ clase B, arena ≈ clase D) |
| 2026-10-04 | Trazado | Primitivas geométricas exactas | Esqueleto de primitivas más capa de irregularidad desde la fase 3 | Validación de la 2.2: la zona, con rectas y arcos exactos, se siente simulada; el tramo a mano de la fase 3 usa el mismo formato y tendría el mismo problema |
| 2026-10-04 | Estimación 1.0 | ~230–410 h | ~236–420 h | Capa de irregularidad en la fase 3 (6–10 h) |
| 2026-10-04 | Física del coche (detalle) | Denominador mínimo de deslizamiento, mezcla de baja velocidad y ancla de parada con freno; relajación como filtro de α en la 2.5 | Deformación de la huella como estado (modelo transitorio de Pacejka) a cualquier velocidad, con límite de Coulomb; freno y rodadura como pares de Coulomb sobre la rueda; la relajación pasa de la 2.5 a la 2.4 | Los tres parches dejaban huecos a velocidad cero (ancla rota, coche que repta en llano); un solo mecanismo los cubre todos |
| 2026-10-04 | Entrada | Teclado o mando | Además, dirección con ratón opcional en PC; velocidades del teclado según la velocidad | Parada de la 2.4: con teclado, el coche era casi incontrolable a velocidad |
| 2026-10-04 | Estimación fase 2 | 61–109 h | 64–109 h | Tareas 2.4b (pantalla y asiento) y 2.5b (dirección); la ayuda de dirección pasa de la 2.8 a la 2.5b |
| 2026-10-04 | Estimación 1.0 | ~236–420 h | ~239–420 h | Ídem |
| 2026-10-05 | Física del coche (neumático) | Fricción combinada por deslizamiento normalizado (módulo y dirección) | Módulo por deslizamiento normalizado; dirección de la velocidad de deslizamiento al deslizar (mezcla entre ρ = 1,5 y 3) | Con la dirección normalizada, una rueda bloqueada a 30° en grava tenía un 23 % menos de fuerza lateral que con Coulomb (freno de mano, frenar girando) |
| 2026-10-05 | Física del coche (carcasa) | Amortiguamiento sobre la velocidad de deslizamiento hasta 3 m/s | Sobre la velocidad de deformación de la huella, hasta 10 m/s de v_ref | Serpenteo de la guiñada a baja velocidad (4,5 Hz, ζ ≈ 0,15 a 10 km/h); con 10 m/s, ζ ≥ 0,54 de 5 a 20 km/h |
| 2026-10-05 | Entrada | Stick sin límite de velocidad de giro (tope en 8 pasos) | Velocidad máxima de giro de las ruedas según la velocidad también con stick y ratón (2.5b) | Parada de la 2.5: 30 de 54 sucesos venían de la dirección (900°/s con stick, 248°/s con teclado) |
| 2026-10-05 | Estimación fase 2 | 64–109 h | 67–114 h | Tarea 2.6b (carretera de prueba): el manejo se ajusta y se juzga también en una carretera estrecha, no solo en la zona abierta |
| 2026-10-05 | Estimación 1.0 | ~239–420 h | ~242–425 h | Ídem |
| 2026-10-05 | Criterio de la fase 2 | — | 3 pasadas seguidas de la carretera de prueba sin recolocar por jugador, con la configuración elegida | Ídem |
| 2026-10-05 | Dirección | Ventana ± α_pico alrededor de la velocidad del eje delantero | Recorte solo hacia el centro, en la rueda exterior; contravolante sin recorte cuando la trasera pasa de su pico | La ventana simétrica giraba las ruedas sola con el volante centrado y cortaba el contravolante; con la guiñada como criterio, el teclado se pasaba del pico en cada cambio de lado (2.5b) |
| 2026-10-05 | Entrada | Teclado al tope en 0,25 s y de vuelta en 0,12 s a cualquier velocidad | Según la velocidad, de parado a 100 km/h: teclado al tope de 0,25 a 0,6 s y de vuelta de 0,12 a 0,3 s; stick y ratón de 0,10 a 0,35 s; un solo limitador, después de la ayuda | Paradas de la 2.4 y la 2.5 (2.5b) |
| 2026-10-06 | Cambio de marchas | Sin definir al reducir | Golpe de gas automático al reducir (punta-tacón), en automático y en secuencial | Sin él, cada reducción frena las traseras hasta C_max × G durante ~0,05 s; no es una ayuda: lo hace cualquier piloto |
| 2026-10-06 | Cambio automático | Subida por rpm según el acelerador (desde 4000 rpm con el acelerador cerrado) | Con el acelerador cerrado sube a 6500 rpm: al levantar el pie conserva la marcha | Con 4000 rpm, al levantar en la entrada de la curva subía 1ª → 2ª → 3ª y el coche salía en una marcha larga por debajo del turbo (2.6) |
| 2026-10-06 | Embrague automático | Muerde al llegar a las rpm de arranque | Muerde desde ~1200 rpm (bite_fraction 0,15): arranca a los ~0,1 s | Sin ello, ~0,35 s sin moverse al pisar a fondo; validado por Lucas en la parada de la 2.6 |
| 2026-10-14 | Forma de desarrollo | Una tarea cada vez; inicio, plan, respuesta, informe y parada por tarea con el consultor | Lotes en paralelo por carriles con revisión y verificación automáticas y una parada de Lucas por lote | El código sale en 1–3 h por tarea; el tiempo se iba en idas y vueltas de archivos y en paradas por tarea (la de la 2.7 estuvo 8 días abierta). Las garantías de propiedad de rutas, de `master` y de compilaciones las imponen scripts, no instrucciones a los agentes (segunda revisión del 2026-10-14) |
| 2026-10-14 | Plan de fases (regla) | No se pasa a la siguiente fase hasta cumplir su criterio | No se cierra una fase sin su criterio; la siguiente puede empezar en los carriles que no dependen de lo pendiente | Durante la 2.10 (15–28 h de pruebas de Lucas) el código está parado, y la infraestructura de la fase 3 (formato de tramo, seguimiento, red local, tablet) no depende del ajuste del coche; la carretera de prueba de la 2.6b queda congelada hasta la 2.11 |
| 2026-10-14 | Estimación fase 2 | 67–114 h | 76–128 h | Tareas P1a (5–8 h) y P1b (4–6 h): puesta en marcha del desarrollo por lotes |
| 2026-10-14 | Estimación 1.0 | ~242–425 h | ~251–439 h | Ídem |
| 2026-10-15 | Entrada (sensibilidad) | Escala continua sin definir (el prompt de la 2.8 la hacía una ganancia de 0,5 a 1,5 recortada al tope) | Exponente de la curva del stick = 1,5 / sensibilidad (de 3 a 1; 100 % = la curva de antes); el stick a fondo da siempre el tope; con ratón, los píxeles hasta el tope; el teclado no tiene sensibilidad | Con una ganancia menor que 1, el stick no llega al tope y la horquilla de 12 m de la carretera de prueba no se puede tomar; es la «linealidad» de los juegos de rally de simulación (revisión del lote 1) |
| 2026-10-15 | Forma de desarrollo (lotes) | Archivos generados y hashes de referencia sin regla de dueño; un lote se integra de una vez | Un archivo generado es del carril dueño de su fuente (docs/teclas.md, de plataforma; tools/telemetry/columns.md, de juego); el hash de la telemetría se parte en el de la simulación (estructura, sim) y el del CSV (juego); un lote puede ir en oleadas que salen de integracion con la anterior fusionada | La 2.8 tocaba cuatro carriles y ninguna rama podía pasar sus propios tests: cada cambio de teclas o de columnas rompía un test que comparaba con un archivo de otro dueño (revisión del lote 1) |
