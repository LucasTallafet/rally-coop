# Rally Coop — Requisitos

Versión final de trabajo · 1 de octubre de 2026

## 1. Requisitos mínimos del jugador

| Componente | Mínimo |
| --- | --- |
| Sistema operativo | Windows 10 de 64 bits |
| Procesador | 4 núcleos a 3 GHz o similar, de 64 bits |
| Memoria | 8 GB de RAM |
| Gráficos | GeForce GTX 1060 6 GB o Radeon RX 580 |
| Almacenamiento | 8 GB libres |
| Red | Conexión entre las dos casas (Tailscale o puerto abierto) |
| Controles | Mando (recomendado) o teclado y ratón; auriculares recomendados |

## 2. Objetivos de rendimiento y presupuestos

| Partida | Objetivo en el equipo mínimo |
| --- | --- |
| Fotogramas | 1080p a 60 fps estables, calidad media-alta, en el caso más caro (bosque denso; en post-1.0, de noche con lluvia y niebla) |
| Física | 240 Hz fijos en un hilo |
| Memoria del juego | ≤ 3 GB, para dejar sitio al sistema y a Discord |
| Draw calls | ≤ 2 000 por fotograma (medido: ~0,58 µs de CPU cada una con raylib) |
| Disco | ≤ 1 GB instalado; el estilo low-poly apenas ocupa y el grueso es el audio |
| Física del tramo cargada entera | Del orden de decenas de MB en tramos largos; se mide en la fase 3 |
| Espera de validación antes de una pasada | Que no se note: se valida mientras se eligen coche y ajustes. Objetivo a fijar con la medida de la fase 4 |
| Red | Estado del coche a 30 Hz; < 5 KB/s por sentido sin voz |
| Latencia de nota (de que el copiloto envía a que el conductor la ve) | La de la red más el búfer de interpolación (60–150 ms) |
| Simulación del coche | ≤ 0,5 ms por paso de 240 Hz (coche y mundo de pruebas) en un i3-8100 |

**Cómo se mide:** contador en pantalla desde la fase 1 (ms por fotograma, draw calls, memoria) y medida en un PC con GTX 1060 o RX 580 (o una gráfica de rendimiento parecido) al final de cada fase.

### Riesgos de rendimiento a vigilar

| Pieza | Riesgo | Medida |
| --- | --- | --- |
| Validación con piloto IA | Segundos de espera en CPU lenta | Zonas de riesgo, 2 hilos, durante la selección, caché |
| Física entera en doble precisión | Memoria y 5–10 % más de física | Franja de colisión de ±60 m; medir en la fase 3 |
| Neumático de calidad a 240 Hz | Más cálculo por rueda y paso | Un solo coche; ≤ 0,5 ms por paso con `rally_replay run <grabación> --bench` y el contador (F1). Peor caso: 8 pasos por fotograma en el modo de 30 fps (4 ms). |
| Bosque denso opaco | Lo más caro para la gráfica | Fase 1, RX 580 a 1080p: 20 000 árboles (1 M triángulos) con instancias persistentes = 0,50 ms de CPU y 1,65 ms de GPU, sin descarte por cámara y sin sombras. Fase 6: descarte por celdas, LOD/impostores en lo lejano y solo las cascadas cercanas dibujan vegetación |
| Carga de gráficos en segundo plano | Necesita un núcleo libre | Ventana configurable |
| Interior con objetos sueltos [post-1.0] | Segundo mundo físico | < 10 objetos, dormidos |

## 3. Requisitos funcionales de la 1.0

### Juego
- [ ] Dos jugadores en dos PCs, en tiempo real, entre casas.
- [ ] Sesión de dos pasadas: la segunda en sentido contrario con roles cambiados.
- [ ] Tramos generados por semilla, entorno de montaña, superficies asfalto, grava, tierra, nieve y arena, seco de día.
- [ ] Tablet con mapa, perfil de altura, cinta de ruta y cursor libre; sin notas dibujadas.
- [ ] Dial principal, secundario y de distancias, con mando y con ratón; borrar última y vaciar cola.
- [ ] Panel del salpicadero curva · distancia · curva con sonido por lado.
- [ ] Puntuación por curva con racha, monedas por minuto de tramo, medallas como tramos de rendimiento, primer intento.
- [ ] Recolocación, atajos, parciales y daños con integridad.
- [ ] Ayudas configurables por jugador y registradas con cada pasada.
- [ ] Resumen de la pasada y de la sesión.
- [ ] Acciones con tiempo (como mínimo, guardar y sacar la tablet).
- [ ] Un coche con interior detallado.

### No funcionales
- [ ] La misma secuencia de entradas da el mismo tiempo en el mismo PC.
- [ ] La misma semilla da el mismo tramo en los dos PCs (hash al conectar).
- [ ] El cambio de host entre pasadas no se nota.
- [ ] Ningún tramo aceptado es imposible de completar en ninguno de los dos sentidos.
- [ ] Lo que el render enseña nunca es más que lo que el cálculo de ceguera supone.

## 4. Fuera de alcance

Uso comercial; servidores; clanes, ligas o pase de batalla; móvil; otras plataformas que no sean Windows; volante con force feedback (el modelo de neumático deja el par de autoalineación listo para más adelante).
