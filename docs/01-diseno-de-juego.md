# Rally Coop — Diseño de juego

Versión final de trabajo · 1 de octubre de 2026 · Lucas

Juego de rally cooperativo en 3D para 2 jugadores, cada uno en su PC y en tiempo real: uno conduce y el otro canta las curvas. Se juega en primera persona, con una conducción del estilo de DiRT Rally y un estilo visual low-poly. Está pensado para parejas de amigos que buscan un reto juntos. El manejo es realista, en la línea de DiRT Rally; las ayudas configurables lo hacen accesible con mando. Uso personal y no comercial.

**Objetivo del proyecto:** la calidad es lo primero, desde el primer día. No hay fecha fija: el ritmo se mide con el plan de fases (ver `04-plan-de-fases.md`).

**Regla de alcance:** el motor y el juego solo implementan lo que este juego usa. Lo que no acerca al juego terminado se aplaza a la fase en la que hace falta. Cada fase termina con algo jugable y medido.

Las piezas marcadas **[post-1.0]** están diseñadas pero se construyen después de la versión 1.0.

---

## 1. Concepto

**Estilo visual:** 3D low-poly con colores planos. La calidad se busca en la iluminación, las sombras, la densidad de vegetación, la distancia de dibujado, las partículas (polvo y grava) y el interior del coche, no en texturas fotográficas.

Cada jugador tiene una mitad de la información y ninguno puede terminar el tramo solo. El conductor controla el coche pero ve poco de lo que viene; el copiloto ve la carretera por delante en su tablet, pero no controla el coche.

**Principio de diseño: la vista del conductor nunca basta.** Cada tramo se diseña para que la luna no enseñe lo suficiente: crestas ciegas, curvas tapadas por vegetación o muros, y más adelante barro, noche, niebla y lluvia. Si un tramo se puede correr solo mirando la luna, el tramo está mal diseñado. Lo sostienen tres piezas:

- **Tramos nuevos cada vez.** Se generan por semilla, así que el conductor no puede memorizarlos y el copiloto nunca sobra.
- **Ceguera garantizada.** El generador crea los puntos ciegos a propósito y un validador descarta los tramos en los que la luna basta.
- **Presión por la puntuación.** Solo puntúa el primer intento de cada pasada, y la puntuación premia ir rápido y limpio. Sin esa presión, lo óptimo sería ir despacio mirando la luna.

## 2. Roles

| Rol | Qué ve | Qué controla | Cómo se comunica |
| --- | --- | --- | --- |
| Conductor | Vista en primera persona por la luna; señales solo en peligros graves; pantalla de notas en el salpicadero | Volante, acelerador, freno, freno de mano, cambio (automático o secuencial), recolocar | Voz |
| Copiloto | Tablet con mapa, perfil de altura y cinta de ruta; vista por la luna desde el asiento derecho; la pantalla del salpicadero si levanta la vista | Tablet, cursor, dial de notas, dial de distancias, borrar notas; limpiaparabrisas [post-1.0] | Voz, dial o ambos |

- La tablet da unos 8–10 s de anticipación. Muestra la geometría de la carretera, nunca la nota de una curva: valorarla es la habilidad del copiloto.
- El juego es online entre 2 PCs. La voz va por Discord al principio y por una llamada integrada en el juego más adelante (ver `02-arquitectura.md`).

## 3. Sesión de juego

Una sesión son **dos pasadas del mismo tramo**:

1. Pasada 1: A conduce y B canta. Al cruzar la meta, resumen corto de la pasada.
2. El coche se coloca en la meta, girado. La meta de la pasada 1 es la salida de la pasada 2.
3. Pasada 2: el tramo en sentido contrario, con los roles cambiados. Al terminar, resumen de la pasada y resumen de la sesión.

- Al revés, las curvas se ven distintas y las crestas se vuelven badenes. El generador valida el tramo en los dos sentidos.
- Riesgo a medir en la fase 4: si la pasada 2 sale siempre mejor que la 1 porque los dos recuerdan el tramo, se ofrece una segunda pasada con semilla nueva como opción.
- Solo puntúa el primer intento de cada pareja en cada pasada de una semilla. Repetir una semilla ya corrida es práctica: está permitido, pero no da monedas.

## 4. Tablet del copiloto

| Zona | Qué muestra | Para qué sirve |
| --- | --- | --- |
| Mapa principal | Vista cenital orientada según la carretera; flecha fija en el tercio inferior; zoom en 2 o 3 niveles fijos según la velocidad (unos 8–10 s por delante); referencia de escala; marcas cada 50 m; línea coloreada por superficie; iconos de peligro; cursor | Forma de las curvas, distancias, cambios de superficie |
| Perfil de altura (franja inferior) | Corte lateral de la carretera que viene | Crestas, badenes y saltos |
| Cinta de ruta (franja lateral) | Los próximos 400–600 m en recta, con una marca de sentido en cada cambio de dirección de más de unos 5°, sin su forma | Lo que viene después de lo que cabe en el mapa |

- **Giro del mapa:** sigue la tangente de la carretera en el punto del trazado donde está el coche, suavizada. No depende del morro ni de la velocidad, así que no da bandazos en un derrape ni en un trompo. **A probar en la fase 3:** orientarlo por un punto 30–50 m por delante, para que no dé media vuelta en las horquillas justo cuando el copiloto lee lo que viene.
- **Colores de superficie:** asfalto gris, grava beige, tierra marrón claro, arena amarillo pálido, barro marrón [post-1.0], nieve blanca. Como el color nunca va solo, la línea lleva además un trazo distinto por superficie (continuo, discontinuo, punteado).
- **La cinta y el mapa no opinan.** Marcan los cambios de dirección con un criterio solo geométrico. No indican cuáles merecen nota.
- **Las notas cantadas no se dibujan en la tablet.** Solo aparecen en la pantalla del salpicadero. En el mapa solo está el cursor, que marca dónde se anclará la siguiente nota.
- A medir en la fase 3: si el copiloto pierde la cuenta de lo que ha cantado, se añade una marca neutra de "aquí hay nota", sin decir cuál.

## 5. Notas del copiloto

### 5.1 Escala

Las notas se definen por velocidad de paso en unas condiciones de referencia, grava seca, no por ángulo ni por marcha. La nota de una curva describe su geometría; el conductor adapta la velocidad a la superficie real.

| Nota | Color | Curva | Marcha orientativa (grava seca) |
| --- | --- | --- | --- |
| 6 | Azul | Casi recta | A fondo |
| 5 | Cian | Abierta | 5ª |
| 4 | Verde | Media | 4ª |
| 3 | Amarillo | Cerrada | 3ª |
| 2 | Naranja | Muy cerrada | 2ª |
| 1 | Rojo | Lentísima | 1ª |
| H | Morado | Horquilla (unos 180°) | 1ª y freno de mano |

### 5.2 Cursor libre

- La nota se ancla a un punto de la carretera (su distancia `s` a lo largo del trazado), no a un momento. Aparece en el sitio correcto aunque la red vaya con retraso.
- El cursor puede ir a **cualquier punto** de la carretera, no solo a las curvas que el juego considera importantes. Así se puede cantar, por ejemplo, un quiebro suave sobre una cresta ("6 derecha sobre cresta").
- Por defecto apunta al siguiente cambio de dirección sin nota por delante del coche. Al enviar una nota, salta al siguiente. Si el coche pasa el cursor, salta al siguiente por delante del coche.
- Los botones de hombro saltan al cambio de dirección siguiente o anterior. Mantener un hombro desliza el cursor a lo largo de la carretera, más rápido cuanto más tiempo se mantiene. Con ratón, un clic en cualquier punto de la carretera coloca el cursor ahí.

### 5.3 Diales

**Dial principal (14 posiciones).** Mitad izquierda para curvas a la izquierda y mitad derecha para las de la derecha, del 6 arriba a la H abajo. El centro cancela.

- **Mando:** dos gestos con el stick izquierdo tras pulsar el botón del dial: un golpe a izquierda o derecha elige el lado, que se muestra en grande; después, una rueda de 7 notas con el 6 arriba y la H abajo. Sin pulsar el botón, el stick nunca elige lado.
- **Ratón:** mantener el botón, apuntar a la flecha y soltar.

**Dial secundario (8 posiciones).** Se abre con el stick derecho. Añade información a la última nota o coloca un peligro solo.

| Tipo | Opciones |
| --- | --- |
| Peligros | Cresta, salto, cuidado |
| Modificadores | Se cierra, se abre, no cortes, larga, y (enlaza con la siguiente) |

"Larga" une la nota con la siguiente curva del mismo sentido, que queda cubierta por ella.

**Dial de distancias (8 valores).** y, 30, 50, 80, 100, 150, 200, 300+. La distancia se pega a la última nota, como el "100" de "3 izquierda 100 4 derecha".

- **Mando:** mantener el gatillo izquierdo abre la rueda en el stick izquierdo, con la "y" arriba y las distancias crecientes en el sentido de las agujas del reloj; soltar el gatillo confirma. Los gatillos están libres porque el copiloto no acelera.
- **Ratón:** mantener el botón derecho abre la rueda; soltar confirma.
- El copiloto estima los metros con las marcas cada 50 m del mapa y con la cinta. Estimar bien es parte de su habilidad.

**Borrar.** Un botón con dos gestos:

- **Pulsación:** borra la última nota enviada, con su distancia.
- **Mantener 1 s:** vacía todas las notas pendientes.
- Una nota borrada no desaparece en silencio: se tacha medio segundo en el panel y suena un aviso de anulado.

### 5.4 Qué ve y oye el conductor

La pantalla del salpicadero muestra una fila de tres huecos, con la forma de una nota real: **curva · distancia · curva**.

| Hueco | Qué muestra |
| --- | --- |
| Izquierdo | La siguiente nota: número o H, sentido, modificadores e iconos de peligro |
| Central | Metros hasta la curva siguiente, o "y" si enlaza; vacío si el copiloto no puso distancia |
| Derecho | La nota de después |

- Cuando el coche pasa el punto de la nota izquierda, la fila corre. Las notas que no caben esperan ocultas.
- Un peligro sin curva ocupa un hueco de nota, con su icono.
- Cada nota suena por su lado en los auriculares al entrar en el hueco izquierdo.
- **El juego nunca modifica una nota:** no corrige el número, no ajusta los metros, no descarta notas absurdas, no las une ni las separa, y no añade ninguna barra de distancia automática. El ancla solo sirve para ordenarlas, saber cuándo se ha pasado una y hacerla reaparecer si el coche se recoloca por detrás.

**Accesibilidad:** el color nunca va solo. Cada nota lleva su número o letra, la flecha está más curvada cuanto más cerrada es la curva, y los peligros tienen icono propio.

### 5.5 Resumen del tramo

Se muestra al terminar cada pasada, con los datos del equipo y sin señalar culpables.

- **Tiempo perdido por zona** frente al perfil de referencia, con los parciales.
- **Notas:** cada nota del dial frente a la curva del juego más cercana a su ancla. Una nota de diferencia se muestra como tendencia ("cantáis más abiertas las curvas que se cierran"); dos o más se marcan. También se marcan las curvas de nota 4 o más cerradas que se quedaron sin nota. Solo se comparan las notas enviadas con el dial.
- **Paso por curva:** velocidad real frente a la de referencia con la superficie real.
- **Ayudas usadas** por el conductor en esa pasada.

## 6. Puntuación, monedas y tienda

### 6.1 Puntuación de una pasada

Se puntúa cada curva del tramo que el coche atraviesa. La lista de curvas es interna; el jugador no la ve ni condiciona sus notas.

| Calidad de paso | Condición (velocidad frente a la de referencia con la superficie real) | Puntos base | Racha |
| --- | --- | --- | --- |
| Excelente | ≥ 95 %, sin salirse ni golpear | 100 | +0,2 |
| Buena | 85–95 %, limpia | 60 | +0,1 |
| Limpia | < 85 %, limpia | 30 | Se mantiene |
| Fallida | Salida, golpe sobre el umbral o recolocación | 0 | Vuelve a ×1 |

La racha empieza en ×1 y tiene tope en ×3. Al final se suma un bonus de tiempo frente al tiempo de referencia y se restan las recolocaciones:

```
P = Σ (puntos_base_i × racha_i) + bonus_tiempo − 150 × recolocaciones
monedas = redondeo( P / P_max × k × t_ref × D )
```

- **P / P_max:** rendimiento de la pasada. P_max es la puntuación con todas las curvas excelentes y la racha al máximo.
- **k × t_ref:** paga por minuto de tramo. t_ref es el tiempo de referencia en minutos y k las monedas por minuto. Un tramo de 10 minutos paga el doble que uno de 5 al mismo rendimiento, así que elegir tramos largos o revirados no da más monedas por hora.
- **t_ref se calcula con el coche elegido**, para que comprar un coche más rápido no dé más monedas.
- **D:** dificultad. De ×1,0 a ×1,3 según la densidad de curvas; ×1,5 en modo difícil [post-1.0].
- **No se puntúan las notas.** Castigaría a quien canta por voz y convertiría el dial en un examen. El copiloto puntúa a través del paso por curva, que es lo que sus notas hacen posible.
- **Medallas:** bronce desde 0,5 de rendimiento, plata desde 0,7 y oro desde 0,9. Se ven en el resumen y no cambian la fórmula.
- **Sesión de dos pasadas:** cada pasada se paga por separado, así que la sesión dura el doble y paga el doble, con las mismas monedas por hora. Bonus de equipo del 10 % si las dos pasadas llegan a plata.
- **Modo 1 jugador [post-1.0]:** misma fórmula. El nivel del copiloto bot entra en D: cuanto más fiable es el bot, menos paga. Con el bot más exigente paga como el modo a dos, nunca más.
- Todas las cifras son de partida y se ajustan jugando.

### 6.2 Economía

Las monedas se acumulan desde la 1.0. La tienda abre con la personalización [post-1.0]. Los precios se diseñan en horas de juego y se convierten a monedas con k.

| Qué | Precio orientativo |
| --- | --- |
| Accesorio pequeño (pegatina, colgante, color) | 20–40 min |
| Accesorio de personaje o interior | 1–2 h |
| Vinilo completo o llantas | 2–3 h |
| Coche nuevo | 5–8 h |

- La 1.0 tiene un solo coche. Cada coche nuevo es un manejo, un sonido y un interior que ajustar, así que llegan de uno en uno, después.
- El juego guarda en local el hash de cada semilla con sus parámetros y la versión del generador para saber qué pasadas ya se corrieron. Sin servidor, la partida guardada se puede editar; para un juego entre amigos no merece la pena protegerla.

## 7. Conducción

### 7.1 Manejo y ajustes

**Coche de la 1.0.** Diseño propio, sin marcas ni decoraciones reales, inspirado en un Grupo B de 1985: 4×4 con motor central transversal y turbo. Referencia de manejo: Peugeot 205 T16 Evo 1 (~350 CV, ~950 kg, reparto de par 33 % delante y 67 % detrás, autoblocantes en cada eje). La potencia es ajustable hasta valores de Evo 2 (~450 CV) para encontrar la dificultad adecuada (es un parámetro del archivo del coche, no un ajuste del jugador). La conducción es realista: el turbo tiene retardo de verdad (hay que anticipar el acelerador), y no hay nitro ni empujones artificiales.

El manejo es realista: física de simulación estándar, sin atajos arcade. El peso, la transferencia de carga, el agarre de cada superficie y el retardo del turbo se notan. Lo que lo hace accesible con mando son las ayudas, que cada jugador activa o no (tabla de abajo), no una física simplificada. Se juega con mando o con teclado y ratón; el volante no se soporta de momento. Los ajustes se eligen antes de cada pasada y **cada jugador configura los suyos**.

| Ajuste | Opciones | Efecto |
| --- | --- | --- |
| ABS | Desactivado (por defecto) / activado | Evita el bloqueo de ruedas al frenar |
| Control de tracción | Desactivado / bajo / alto | Limita el patinaje al acelerar |
| Ayuda de dirección | Activada / desactivada | Limita el ángulo de las ruedas delanteras al de máximo agarre (ver "Dirección") |
| Cambio | Automático / secuencial manual | Ver 7.2 |
| Dureza de suspensión | Blanda / media / dura | Blanda absorbe baches y saltos; dura responde mejor en asfalto |
| Sensibilidad de dirección | Escala continua (50–150 %) | Con stick, cuánto gira el coche cerca del centro; el stick a fondo da siempre el giro a fondo. Con ratón, los píxeles hasta el tope. El teclado no tiene sensibilidad: su ritmo lo fija la decisión «Entrada» |

- Las ayudas usadas se guardan con cada pasada y se muestran en el resumen y en la tabla de tiempos.
- En el modo difícil [post-1.0], el ABS y el control de tracción quedan bloqueados en desactivado. La ayuda de dirección sigue a elección de cada jugador: compensa la falta de fuerza en el volante del mando, no la habilidad.

**Dirección.** Con mando o teclado no hay fuerza en el volante que avise del límite, así que una ayuda limita el ángulo de las ruedas delanteras respecto a la dirección real de avance del coche. El stick a fondo da el ángulo de máximo agarre y el contravolante está siempre disponible. La ayuda es un ajuste de cada jugador: activada o desactivada, activada por defecto, y se registra con la pasada como las demás ayudas. Sin ella, la dirección a fondo da el ángulo máximo de las ruedas a cualquier velocidad. El ángulo de máximo agarre depende de la superficie que pisan las ruedas delanteras.

- La ayuda actúa a partir de unos 30 km/h. Entre 15 y 30 km/h se mezcla de forma suave con el giro completo. Por debajo de 15 km/h, parado y marcha atrás, el stick a fondo da el giro completo. Umbrales provisionales hasta la fase 2.

**Mando del conductor:** stick izquierdo = dirección; gatillos = acelerador y freno; un botón = freno de mano; hombros = subir y bajar marcha; un botón = recolocar. Vibración según superficie y deslizamiento. Todo reasignable. **Teclado:** el giro crece poco a poco mientras se mantiene la tecla y vuelve solo al centro.

### 7.2 Cambio de marchas

Automático o secuencial manual, sin embrague ni calado. El automático cambia por umbrales de revoluciones con histéresis; en derrape retrasa el cambio como mucho 0,5–1 s y nunca contra el limitador. La ventaja del manual sale de reducir antes de la curva y sostener la marcha en un derrape, no de empeorar el automático a propósito. Al reducir, el motor iguala las revoluciones de la marcha nueva (punta-tacón automático), en los dos modos. Al levantar el pie, el automático conserva la marcha (no sube por debajo de unas 6500 rpm con el acelerador cerrado), para no salir de la curva en una marcha larga por debajo del turbo. El embrague automático muerde desde poco por encima del ralentí: el coche arranca en cuanto se pisa.

- **Punto muerto y marcha atrás:** la caja va R – N – 1ª – … – 5ª, en automático y en secuencial. Bajar de 1ª pasa a punto muerto (N); bajar de N con el coche parado (menos de 2 km/h) engrana la marcha atrás (R); subir recorre el camino contrario. En N el motor gira libre y no empuja. En R, el acelerador empuja hacia atrás y el freno frena. En automático, los botones de marcha solo sirven para elegir R, N o marcha adelante (desde 1ª, el automático hace el resto), y bajar a N solo se puede parado. Al construir el mundo o recolocar, el coche sale en 1ª.
- **Freno de mano:** bloquea las ruedas traseras. En los coches 4×4, mientras se tira, se desacoplan el diferencial central y el embrague, como en los coches de rally reales: así las delanteras no luchan contra las traseras bloqueadas.

### 7.3 Superficies y relieve

- Asfalto, grava, tierra, nieve y arena en la 1.0 (barro [post-1.0]). Cada una con su agarre, su resistencia a la rodadura, su color, su ruido procedural y su sonido. La arena tiene mucha resistencia a la rodadura y poco agarre lateral: frena el coche y castiga perder las vueltas. En la montaña de la 1.0 aparece en tramos sueltos (lechos de río, zonas arenosas). La vibración del mando cambia con la superficie y con el deslizamiento.
- **Relieve:** forma de la carretera en la malla (trazado, peralte, crestas, badenes, saltos), una capa de irregularidad de escala media más un microrrelieve `h(s, t)` que nota la rueda. En la 1.0, una banda de ruido sencilla; el espectro ISO 8608 en dos bandas (física y fina) llega después.
- **Irregularidad:** una carretera de rally no es una sucesión de rectas y arcos perfectos. Sobre el trazado de primitivas, cada tramo lleva desviaciones deterministas del eje (de centímetros a algo más de un metro, en longitudes de 15–150 m), ancho variable, peralte y bombeo que cambian, ondulación vertical de decímetros en 10–60 m y bordes irregulares. Las curvas se cierran y se abren: su curvatura no es constante ni simétrica.
- Lo que se ve coincide con lo que se pisa: el render desplaza la carretera cerca de la cámara con la misma función que usa la rueda.

### 7.4 Sensación de velocidad

En primera persona la velocidad se percibe por el detalle que pasa junto al coche. Se refuerza con ruido procedural en el suelo, objetos frecuentes en el borde (postes, piedras, árboles), vibración de cámara según la superficie y un ligero desenfoque en los bordes. El campo de visión no cambia con la velocidad.

### 7.5 Señales

Solo en peligros graves (salto, cresta muy ciega, estrechamiento peligroso), unos 100 m antes. El generador las coloca leyendo los mismos datos que la tablet, para que nunca se contradigan. En modo difícil no hay señales.

### 7.6 Salidas de pista, recolocación y atajos

- **Recolocar:** si el coche se sale, vuelca o se atasca, el conductor pulsa recolocar. El coche aparece parado en el centro de la carretera, mirando hacia delante, en el punto más avanzado que alcanzó circulando por ella, con una penalización fija de tiempo. Sin rebobinado.
- **Atajos:** mientras el coche está fuera de la carretera, el juego acumula lo que recorre. Al volver, si ha ganado más de 30 m de trazado respecto a lo recorrido, es un atajo: se le recoloca en el punto donde salió, con la misma penalización. Cortar una curva por la cuneta, ganando menos, está permitido, porque es parte del rally.
- **Parciales:** puntos de tiempo intermedio cada cierta distancia.
- Penalizaciones y umbral de 30 m provisionales; el modo difícil las aumenta.

### 7.7 Daños

El coche tiene una integridad que baja con cada impacto por encima de un umbral. A 0 queda inservible y la pasada termina. En modo normal el umbral es alto; el modo difícil lo baja y añade la dirección desalineada.

## 8. Acciones con coste de tiempo

Toda acción del juego tiene fases de preparar, ejecutar y recoger, cada una con su duración. **La duración es la regla y la animación solo la llena:** hasta que haya animaciones, un anillo de progreso. Las duraciones viven en un archivo de datos y se ajustan sin recompilar.

- **Recursos:** cada jugador tiene 2 manos (la tablet ocupa 2; el conductor tiene las 2 en el volante), una posición (asiento del conductor, del copiloto o fuera) y el estado del coche (en marcha o parado).
- **Interrupciones:** cancelar al preparar no cuesta nada; al ejecutar, se pierde lo avanzado. Si deja de cumplirse un requisito, la acción se cancela sola.
- **Cooperativas:** se dividen en tareas paralelas. Con dos jugadores el tiempo es el de la rama más larga.
- **Red:** el jugador pide la acción al host, que la confirma con el paso de física en que empieza y termina. Las repeticiones reproducen las paradas exactas.

| Acción | Quién | Requisitos | Duración | Mientras dura |
| --- | --- | --- | --- | --- |
| Guardar la tablet | Copiloto | — | 0,8 s | Sin mapa ni diales; solo voz |
| Sacar la tablet | Copiloto | Tablet guardada | 0,8 s | Sin mapa ni diales |
| Accionar el limpiaparabrisas [post-1.0] | Copiloto | Tablet bajada | 0,4 s + 0,4 s | Sin diales |
| Arrancar una lámina de la visera [post-1.0] | Cualquiera | Quedan láminas | 0,5 s | Conductor: una mano, dirección más lenta |
| Tapar su ventana [post-1.0] | Copiloto | Kit en la mano | 10 s | Sin tablet |
| Tapar la ventana del conductor [post-1.0] | Copiloto | Coche parado | 15 s | — |
| Salir del coche [post-1.0] | Cualquiera | — | 3 s | — |
| Entrar y abrocharse [post-1.0] | Cualquiera | Junto a su puerta | 4 s | — |
| Coger un objeto sujeto [post-1.0] | Cualquiera | A su alcance | 1,5 s | 1 o 2 manos |
| Cambiar una rueda, solo / entre dos [post-1.0] | — | Parado, gato y repuesto | ~45 s / ~30 s | 2 manos |
| Rellenar refrigerante [post-1.0] | Cualquiera | Parado, capó abierto | 20 s | 2 manos |
| Rellenar el depósito del limpiaparabrisas [post-1.0] | Cualquiera | Parado, capó abierto | 12 s | 2 manos |
| Repostar con la garrafa [post-1.0] | Cualquiera | Parado | 25 s | 2 manos |
| Rociar las viseras con líquido [post-1.0] | Copiloto | Luna rota | 1 s | Sin diales |

## 9. Después de la 1.0

### 9.1 Barro y limpiaparabrisas

Al pasar por un charco, el barro cubre parte de la luna. El copiloto acciona el limpiaparabrisas (acción con tiempo, sin tablet); el líquido es limitado y, en seco, las escobillas extienden el barro. El barro se seca y se desprende con la velocidad, para que un charco inevitable nunca deje al equipo ciego sin remedio.

### 9.2 Fuera del coche, averías e inventario

- **Salir del coche:** cualquiera, en cualquier momento. Se echa el freno de mano y el motor queda al ralentí. El coche no se mueve si hay alguien fuera o con el capó abierto. A pie, radio de unos 15 m alrededor del coche.
- **Intercambiar roles a mitad de tramo:** salir, rodear el coche y entrar por el otro lado (unos 10–12 s). La autoridad de red pasa al nuevo conductor con el coche parado.
- **Pinchazos:** solo tras un golpe moderado o fuerte en una rueda, nunca sin un golpe que se haya notado. Lento (20–60 s perdiendo presión, el coche tira a un lado) o reventón. El conductor lo nota en la dirección y la vibración; el copiloto ve un aviso en la tablet. Rodar en llanta es más lento y daña el coche. Repuesto (1 o 2) en un sitio fijo según el coche.
- **Ventanas laterales:** se rompen con golpes laterales o vuelcos. Por el hueco entra suciedad a la visera de ese lado. Se tapan con el kit de cinta y plástico.
- **Viseras con láminas:** cada jugador tiene láminas que arranca para limpiar la visera al momento: 5 en normal, 2 en difícil. La visera sucia del copiloto le deja ver peor la tablet.
- **Luna delantera:** intacta → agrietada (golpe frontal fuerte o piedra) → rota (otro golpe o vuelco). No se repara. Sin luna, el barro, la lluvia y la suciedad van directos a las viseras, y el líquido del limpiaparabrisas sirve para rociarlas.
- **Inventario 3D con física:** gato, ruedas de repuesto, refrigerante, líquido limpiaparabrisas, kit de cinta y plástico, y garrafa de gasolina solo en modo difícil en tramos muy largos. Cada objeto va sujeto con correa o red y solo se suelta tras un golpe fuerte; entonces rueda por el habitáculo y hay que encontrarlo. Los objetos sueltos nunca bloquean pedales ni volante.
- **Temperatura del motor:** un golpe frontal fuerte puede dañar el radiador. La temperatura sube; por encima de un umbral el motor recorta potencia y, si se ignora, pierde integridad. Rellenar refrigerante devuelve la temperatura, pero la fuga sigue.
- **Gasolina:** ilimitada en normal. En difícil, en tramos largos, hay consumo: el equipo elige cuánta carga (más gasolina pesa más) y la garrafa sirve de reserva.

### 9.3 Clima y día/noche

Clima base por tramo: seco, lluvia, niebla o nieve. La lluvia baja el agarre, deja gotas en la luna y reduce la visibilidad. Ciclo de día y noche; de noche el coche depende de los faros, que se ensucian o se rompen. Solo los faros proyectan sombra de noche.

### 9.4 Modo difícil

Quita ayudas a los dos jugadores por igual.

| Elemento | Afecta a | Normal | Difícil |
| --- | --- | --- | --- |
| Anticipación de la tablet | Copiloto | 8–10 s | 5–6 s |
| Iconos de peligro y colores de superficie | Copiloto | Sí | No |
| Señales | Conductor | Solo peligros graves | Ninguna |
| Barro | Los dos | Charcos visibles con tiempo | Más charcos, menos líquido |
| ABS y control de tracción | Conductor | Configurables | Desactivados (la ayuda de dirección no cambia) |
| Daños | Conductor | Umbral alto | Umbral bajo, acumulativos, dirección desalineada |
| Recolocación | Los dos | Penalización base | Penalización doble |
| Láminas de visera | Los dos | 5 | 2 |
| Gasolina | Los dos | Ilimitada | Consumo en tramos largos |

### 9.5 Otros

- **Modo 1 jugador:** el jugador siempre conduce; el único bot es el copiloto. Su error imita errores humanos sistemáticos (valorar más abiertas las curvas que se cierran, retrasarse en secuencias rápidas). No hay intercambio de roles; la segunda pasada al revés es opcional. Las acciones cooperativas se hacen con el tiempo de una persona sola.
- **Clasificatorias:** contrarreloj por parejas sobre un tramo diario o semanal por semilla, intento único, tabla local separada por dificultad, repeticiones y fantasma solo en repeticiones, verificación por re-simulación.
- **Editor de tramos y tramos insignia,** incluidos tramos reales ajustados por el analizador.
- **Personalización y tienda:** carrocería (colores, vinilos, números, llantas, faros auxiliares), interior (volante, pomo, colgantes, pegatinas, funda de la tablet) y personaje (casco, guantes, mono, relojes). Como el juego es en primera persona, la mayoría de accesorios van en el interior y las manos.
- **Modo recce:** pasada lenta de reconocimiento para escribir un libro de notas.
- **Voz integrada** en el juego.
