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
| stick derecho | Mirar con la cabeza: stick derecho, o el ratón (el botón central lo vuelve al frente): el conductor, con la dirección por teclado (F7); el copiloto, siempre; hasta 90° a los lados, 15° arriba y 25° abajo, sin cambiar el campo de visión; solo con la cámara del ojo y con la ventana con el foco; al soltar el stick vuelve al frente |
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
| U | Tramo: irregularidad con / sin: juega el mismo tramo sin su irregularidad y, otra pulsación, con ella; el coche vuelve a la salida y empieza otra grabación con el texto del tramo usado; el indicador del tramo dice cuál es; en un tramo sin irregularidad no hay nada que quitar |

**Grabación**

| Tecla / mando | Acción |
| --- | --- |
| C | Captura del fotograma: captura_NNN.png junto al exe |
| L | Registro por fotograma: fotogramas_NNN.csv junto al exe |

**Depuración**

| Tecla / mando | Acción |
| --- | --- |
| F3 | Panel de depuración: por páginas (F3: página 1, 2... y oculto): mando, entradas, impacto, coche, grabación, zona, cámara, asiento y ritmo de fotogramas; en el modo a ciegas, sin superficie ni configuración |
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
| K | Zona de pruebas / tramo / recinto: recorre los tres; el tramo es data/stages/lazadas_irregular.stage (o lazadas.stage si no está) o el de --tramo; el recinto es el de la fase 1; reconstruye el mundo y empieza otra grabación |
| F11 o Alt+Intro | Pantalla completa sin bordes |
| Esc | Salir |

**Tablet del copiloto**

| Tecla / mando | Acción |
| --- | --- |
| O | Orientación del mapa: carretera / hacia delante: solo en la ventana del copiloto: carretera orienta el mapa por la tangente del eje (no depende del morro ni de la velocidad); hacia delante, por la dirección del coche al punto del eje 40 m por delante (en las horquillas el mapa no da media vuelta) |
| Arriba/Abajo · cruceta arriba / abajo | Cursor: siguiente / anterior cambio de dirección: solo en la ventana del copiloto: salta al cambio de dirección siguiente o al anterior, nunca por detrás del coche; mantenida más de 0,3 s, desliza por la carretera de 20 a 200 m/s en 2 s; por defecto el cursor está en el siguiente cambio de dirección por delante del coche |
| Espacio | Puntero: mantener y pulsar en el mapa: solo en la ventana del copiloto: mientras se mantiene, el ratón deja de mirar (se suelta el cursor y la cabeza se queda quieta) y un clic en la carretera del mapa coloca el cursor en el punto más cercano del eje; al soltar, el ratón vuelve a mirar desde donde estaba la cabeza |
| Izquierda/Derecha · hombros izquierdo y derecho (LB / RB, L1 / R1, L / R) | Dial principal: mantener el lado y elegir la nota: solo en la ventana del copiloto: la curva a la izquierda (Izquierda o L1) o a la derecha (Derecha o R1); con el teclado se elige con 1-6 o 7 para la horquilla (la última pulsada), con el mando con el stick izquierdo (6 arriba, H abajo; el centro cancela) y con el ratón manteniendo el botón sobre la rueda; al soltar se canta la nota en el cursor |
| S · gatillo derecho | Dial secundario: mantener y elegir: solo en la ventana del copiloto: cresta, salto, cuidado, se cierra, se abre, no cortes, larga y «y»; con el teclado, S mantenida y 1-8 en ese orden; con el mando, el gatillo derecho y el stick izquierdo; con el ratón, el botón izquierdo sobre la rueda secundaria; al soltar se canta. Los modificadores y la «y» van a la última nota; cresta, salto y cuidado, solos en el cursor |
| D · gatillo izquierdo | Distancia: mantener y elegir (rueda o contador): solo en la ventana del copiloto: la distancia después de la última nota. Rueda: y, 30, 50, 80, 100, 150, 200 y 300+ (teclado: D mantenida y 1-8; mando: gatillo izquierdo y stick; ratón: botón derecho sobre la rueda). Contador: parte de 50 m y cambia de 10 en 10, de 0 a 990 (teclado: D mantenida y Izquierda y Derecha; mando: gatillo izquierdo y cruceta izquierda y derecha; ratón: los botones [-], [cantar] y [+]); al soltar se canta. Nunca se rellena con la distancia real |
| F · Back (Xbox), Share (PlayStation), - (Nintendo) | Forma de la distancia: rueda / contador: solo en la ventana del copiloto: cambia la forma de elegir la distancia; la activa se ve en pantalla y se escribe en el registro de notas |
| Retroceso · botón de la izquierda de la cara derecha (X Xbox, Cuadrado PlayStation, Y Nintendo) | Borrar: la última nota / las pendientes: solo en la ventana del copiloto: una pulsación borra la última nota enviada; mantenida 1 s vacía las pendientes (las de por delante del coche). Con el ratón, el botón «borrar» |
| 1/2 | Elegir 1 / 2: solo en la ventana del copiloto, con un dial mantenido: la nota 1 o 2 del dial principal; la opción 1 o 2 del secundario o de la rueda de distancias |
| 3/4 | Elegir 3 / 4: solo en la ventana del copiloto, con un dial mantenido: la nota 3 o 4; la opción 3 o 4 |
| 5/6 | Elegir 5 / 6: solo en la ventana del copiloto, con un dial mantenido: la nota 5 o 6; la opción 5 o 6 |
| 7/8 | Elegir 7 / 8: solo en la ventana del copiloto: con el secundario o la rueda de distancias mantenidos, la opción 7 u 8; con el lado del dial principal mantenido, el 7 elige la horquilla (H) y el 8 no hace nada |

<!-- fin de teclas -->
