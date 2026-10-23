# Teclas de la demo

Referencia de teclas y mandos de la demo. La tabla de abajo la genera `keys_markdown` desde las tablas de cada componente (`src/platform/drive_keys.cpp`, `window_keys.cpp` y las de las partes de la demo, `demo_*_keys.cpp`), que `src/platform/key_tables.cpp` junta por bloques y por su `order`: es la misma tabla que usa el juego y la ayuda en pantalla (H).

El test `keys: docs/teclas.md matches the generated table` comprueba que coinciden, y `keys: no key is repeated within a context` que ninguna tecla se repite entre tablas. Para cambiarlas, se cambia la tabla del componente y se copia aquí lo que imprime ese test al fallar; la tabla no se edita a mano. El resto de la referencia de la demo (indicadores en pantalla, opciones de arranque) está en `docs/07-entorno-y-portabilidad.md` §8.

<!-- teclas: generado desde las tablas de src/platform/key_tables.cpp (keys_markdown); no editar a mano -->

**Conducir**

| Tecla / mando | Acción |
| --- | --- |
| W o Arriba · gatillo derecho | Acelerador |
| S o Abajo · gatillo izquierdo | Freno |
| A/D o Izquierda/Derecha · stick izquierdo | Girar |
| Espacio · botón de abajo de la cara derecha (A Xbox, Cruz PlayStation, B Nintendo) | Freno de mano: ruedas traseras; mientras se tira, sin par motor |
| Q · hombro izquierdo (LB, L1, L) | Bajar marcha: caja R, N, 1 a 5: de 1 a N (en automático, solo parado) y de N a R parado (menos de 2 km/h); en secuencial, una marcha (no si pasaría del limitador) |
| E · hombro derecho (RB, R1, R) | Subir marcha: de R a N y de N a 1 (no si pasaría del limitador); en secuencial, una marcha; en automático, desde 1 cambia solo |
| R · botón de arriba de la cara derecha (Y Xbox, Triángulo PlayStation, X Nintendo) | Recolocar: en la zona, al punto de recolocación más cercano, parado y con su orientación; en el recinto, a la salida mirando al frente |
| X | Recentrar volante (ratón): también el botón central del ratón; solo con la dirección con ratón |
| F6 | Ayuda de dirección: la activa o la quita; limita las ruedas delanteras al ángulo de máximo agarre desde 30 km/h; reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt |
| F2 | Caja: automática / secuencial: reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt |
| F7 | Dirección: teclado / ratón: con ratón, el desplazamiento horizontal es el volante (cursor capturado; se suelta en pausa o al perder el foco y el volante vuelve al centro); A/D no giran; no reconstruye el mundo; se guarda en ajustes.txt |
| 4/5 | Sensibilidad del stick: menos / más: 10 % por pulsación, de 50 a 150 % (100 % es la curva de siempre; menos, más fina al centro; más, más lineal); solo el stick (el ratón y el teclado no la usan); no reconstruye el mundo; se guarda en ajustes.txt |
| 1 | ABS: sí / no: frena cada rueda cerca de su máximo agarre y no actúa por debajo de 6 km/h ni con el freno de mano; reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt |
| 2 | Control de tracción: no / bajo / alto: limita el deslizamiento de las ruedas motrices quitando par al motor; reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt |
| 3 | Suspensión: blanda / media / dura: la misma altura en parado en las tres; reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt |

**Cámara y asiento**

| Tecla / mando | Acción |
| --- | --- |
| Tab | Cámara: ojo del conductor (el del archivo del coche más el asiento), persecución y fija (recinto) o toda la zona, por turnos |
| F9 | Movimiento de la cabeza: sí / no: el ojo se mueve con las aceleraciones del coche (solo la cámara del ojo); se guarda en ajustes.txt |
| Re Pág/Av Pág | Asiento: arriba / abajo: 1 cm por pulsación, se repite al mantener; solo la vista del ojo, no la simulación ni la grabación; límite ± 15 cm en cada eje; se guarda en ajustes.txt junto al exe |
| Inicio/Fin | Asiento: adelante / atrás |
| Insert/Supr | Asiento: a los lados: derecha / izquierda |

**Zona**

| Tecla / mando | Acción |
| --- | --- |
| G | Superficie de la pista ciega: la siguiente; reconstruye el mundo |
| G | A ciegas: siguiente prueba: de la serie de 15 (--blind-seed) |
| M | Modo a ciegas sí / no: todas las superficies del mismo color, sin microrrelieve ni nombres; en pantalla, «Prueba k/15»; cada prueba sale en la pista ciega |
| U | Superficie de la carretera de prueba: grava / asfalto; reconstruye el mundo con el coche en la salida de la carretera |
| Y | Salida: recta / carretera: dónde sale el coche (también al reconstruir el mundo con G, F2, F6 o Retroceso); reconstruye el mundo |

**Grabación**

| Tecla / mando | Acción |
| --- | --- |
| C | Captura del fotograma: captura_NNN.png junto al exe |
| L | Registro por fotograma: fotogramas_NNN.csv junto al exe |

**Depuración**

| Tecla / mando | Acción |
| --- | --- |
| F3 | Panel de depuración: mando, entradas, impacto, coche, grabación, zona, cámara, asiento y ritmo de fotogramas; en el modo a ciegas, sin superficie ni configuración |
| F1 | Contador de rendimiento: CPU, GPU, draw calls, memoria y gráfica de fotogramas |
| T | Gráficas de la telemetría: últimos 10 s, por páginas: 1 · manejo (beta, guiñada, velocidad y entradas aplicadas), 2 · tren motriz, 3 · suspensión (carga y compresión por rueda) y ocultas; las páginas sin canales se saltan; en el modo a ciegas no hace nada |
| O | Recinto a 20 km del origen: y de vuelta |
| Z | Red simulada: no / media / mala: solo en la ventana del copiloto (rally.exe --copiloto): media son 100 ± 30 ms con un 5 % de pérdida y mala, 250 ± 80 ms con un 20 %; solo afecta a los estados que recibe |
| B | Bosque de medida (recinto): escena de medida del bosque, por turnos: apagado, uno a uno, DrawMeshInstanced y persistente |
| J | Dibujar en float del mundo: depuración del render relativo |
| F | Monitor / 30 fps: a la frecuencia del monitor o a 30 fps |
| V | Sincronización vertical |
| I | Interpolación |
| N | Ajuste del dt al refresco |
| 6 | Sonido del deslizamiento: nuevo / el de la 2.7: A/B del sonido de la rueda que desliza o va bloqueada; empieza siempre en el nuevo; no se guarda en ajustes.txt ni va en la grabación |
| 7 | Copiloto: guardar la tablet: hace de copiloto (no hay tablet hasta la fase 3): acción de 0,8 s con anillo de progreso; solo con la tablet fuera; se graba en la grabación |
| 8 | Copiloto: sacar la tablet: solo con la tablet guardada; el motivo de un rechazo sale en los mensajes |
| 9 | Copiloto: cancelar la acción: al preparar no cuesta nada; al ejecutar se pierde lo avanzado |

**General**

| Tecla / mando | Acción |
| --- | --- |
| H | Ayuda de teclas: por páginas: página siguiente y, tras la última, oculta |
| P | Pausa |
| F4 | Volumen: 100, 70, 40 y 0 %, por turnos; se guarda en ajustes.txt |
| F5 | Releer el archivo del coche: reconstruye el mundo con el coche en el punto de recolocación más cercano (en el recinto, en su salida); si el archivo tiene un error, lo muestra con su línea y sigue el coche anterior |
| F8 | Configuración siguiente: mid_awd_turbo, front_awd_turbo_5cyl, front_rwd_na y front_awd_turbo_groupa, por turnos, de la carpeta del coche; reconstruye el mundo; a ciegas o con --hide-config, la pantalla no dice cuál |
| Retroceso | Reiniciar el mundo: cierra la grabación y empieza otra |
| K | Zona de pruebas / recinto: el recinto es el de la fase 1; reconstruye el mundo y empieza otra grabación |
| F11 o Alt+Intro | Pantalla completa sin bordes |
| Esc | Salir |

<!-- fin de teclas -->
