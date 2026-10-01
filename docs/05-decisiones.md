# Rally Coop — Registro de decisiones

Versión final de trabajo · 1 de octubre de 2026

**Cerrada:** no se reabre salvo que una prueba demuestre que falla. **Provisional:** se confirma o se cambia en la fase indicada, con datos. Cada cambio se anota aquí con su fecha y su motivo.

## Proyecto

| Tema | Decisión | Estado |
| --- | --- | --- |
| Enfoque | Uso personal, no comercial; sin hoja de ruta online | Cerrada |
| Objetivo | Calidad primero desde el día 1; sin fecha fija, avance medido con el plan | Cerrada |
| Público | Parejas de amigos que buscan un reto juntos; modo 1 jugador después de la 1.0 | Cerrada |
| Alcance 1.0 | Fases 0 a 6: cooperativo completo, montaña, un coche, seco de día, generador con validador, dos pasadas, monedas | Cerrada |
| Regla de alcance | Solo se implementa lo que este juego usa; lo demás se aplaza a su fase | Cerrada |
| Pruebas | Las decisiones de diseño se validan jugando con otra persona; copiloto bot de desarrollo desde la fase 3 | Cerrada |

## Técnicas

| Tema | Decisión | Estado |
| --- | --- | --- |
| Motor | Propio en C++: raylib (backend SDL) + Jolt + ENet; se descartan Godot y otros motores | Cerrada |
| Plataforma | Windows 10/11 de 64 bits | Cerrada |
| Requisitos mínimos | 4 núcleos a 3 GHz, 8 GB, GTX 1060 6 GB o RX 580, 8 GB de disco; 1080p a 60 fps | Cerrada |
| Física del coche | Controlador propio en `VehicleConstraint` de Jolt con neumático de calidad (opción B reforzada); otro motor de física solo si falla la prueba de derrape | Provisional hasta la fase 2 |
| Reproducibilidad | Jolt determinista en un hilo, `rmath`, PCG, sin fast-math; nada se añade ni se quita durante una pasada; mismo tiempo en el mismo PC desde la fase 2; idéntica entre PCs en la fase 9 | Cerrada |
| Precisión | Doble precisión en Jolt, origen en el centro, render relativo a la cámara, posición por red en mm con enteros de 32 bits | Cerrada |
| Carga del tramo | Generado y validado entero; física entera con franja de terreno de ±60 m; gráficos por ventana; sentido contrario sin recargar | Provisional hasta la fase 3 (medida de memoria) |
| Seguimiento | `s` incremental con ventana; índice espacial solo para adquirir; atajo si se ganan más de 30 m de trazado | Cerrada (umbral provisional) |
| Generador | Terreno primero, perfiles de ritmo de tramos reales, ceguera por diseño, semilla + 1 si falla | Provisional hasta la fase 4 |
| Validador | 4 capas: geometría, ritmo, visibilidad, piloto IA en los dos sentidos | Provisional hasta la fase 4 |
| Ceguera y render | Ocluyentes declarados, opacos hasta unos 200 m, calculados con el ajuste gráfico más bajo | Cerrada |
| Red | Host autoritativo en el PC de quien conduce; interpolación por paso con búfer de 60–150 ms | Cerrada |
| Cambio de host | Con el coche parado, por `SaveState`; sin física idéntica entre PCs | Cerrada |
| Fotogramas | 60 fps con sincronización vertical y modo de 30; física a 240 Hz | Cerrada |
| Estilo visual | 3D en primera persona, low-poly con colores planos; ruido procedural solo en el suelo | Cerrada |
| Render | Sombras en 3 cascadas, vegetación por instancias, partículas, sin texturas fotográficas, sin cambiar el campo de visión | Cerrada |
| Voz | Discord al principio; llamada integrada con Opus después de la 1.0 | Cerrada |
| Implementación | Con Claude Code, guiada por `CLAUDE.md` y tests sin pantalla | Cerrada |

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
| Densidad de curvas | Máximo fijado con las notas por minuto medidas en la fase 0 | Provisional hasta la fase 3 |
| Puntuación | Por curva con racha, solo el primer intento, sin puntuar las notas | Provisional hasta la fase 4 |
| Monedas | Por minuto de tramo, con tiempo de referencia del coche elegido; medallas como rangos | Provisional hasta la fase 4 |
| Tienda | Abre con la personalización, después de la 1.0; coches nuevos de uno en uno | Cerrada |
| Ayudas | Configurables por cada jugador; registradas con cada pasada; bloqueadas en modo difícil | Cerrada |
| Dirección | Ayuda desde unos 30 km/h, mezcla de 15 a 30, giro completo por debajo y en marcha atrás | Provisional hasta la fase 2 |
| Cambio automático | Retrasa el cambio en derrape como mucho 0,5–1 s; nunca contra el limitador | Cerrada |
| Acciones | Todas con fases de duración en datos; la duración es la regla | Cerrada |
| Salidas de pista | Recolocación en el punto más avanzado en carretera con penalización; sin rebobinado | Cerrada |
| Daños | Integridad que baja con impactos sobre un umbral; a 0, fin de la pasada | Cerrada |
| Señales | Solo en peligros graves; ninguna en difícil | Cerrada |
| Modo 1 jugador | Después de la 1.0; el único bot es el copiloto; da monedas, menos cuanto más fiable es el bot | Cerrada |
| Fuera del coche | Salir, intercambio a mitad de tramo, pinchazos, ventanas, viseras, luna, inventario 3D, refrigerante, gasolina en difícil; después de la 1.0 | Provisional hasta su fase |
| Modo recce | Para el futuro | Cerrada |

## Cambios

| Fecha | Decisión | Antes | Después | Motivo |
| --- | --- | --- | --- | --- |
| 2026-10-01 | Recompensa | Medallas por tiempo y tabla local | Monedas por puntuación en el primer intento | Repetir la semilla dejaba al copiloto sin trabajo |
| 2026-10-01 | Física del coche | `WheeledVehicleController` de Jolt | Controlador propio con neumático de calidad | Calidad del manejo |
| 2026-10-01 | Carga del tramo | Por trozos alrededor del coche | Física entera; gráficos por ventana | Validar el tramo entero y simplificar la reproducibilidad |
| 2026-10-01 | Requisitos | Gráfica integrada, < 100 MB | GTX 1060, ≤ 1 GB | Más vegetación, sombras y distancia de dibujado manteniendo el estilo low-poly |
| 2026-10-01 | Alcance 1.0 | Incluía barro y limpiaparabrisas | Después de la 1.0 | Calidad antes que cantidad |
| 2026-10-01 | Fase 0 | Pruebas del concepto antes de escribir el motor | Omitida; sus mediciones pasan al corte vertical (fase 3) | Se desarrolla el juego en cualquier caso; la idea se comprueba con el juego real en la fase 3 |
