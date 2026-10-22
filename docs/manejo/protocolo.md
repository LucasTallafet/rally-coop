# Protocolo de pruebas de manejo (tarea 2.10)

Para Lucas y su compañero. Cada sesión deja una carpeta en `docs/manejo/sesiones/AAAA-MM-DD-N/` que Claude Code convierte en cambios de parámetros con `docs/prompts/fase-2/2.10.md`.

**Regla de oro:** decid lo que sentís con vuestras palabras y dejad que la telemetría diga el porqué. No propongáis parámetros mientras jugáis: describid el síntoma ("al levantar en el círculo se me cruza de golpe"), dónde ("círculo, sentido horario") y en qué intento.

---

## 1. Preparación (10 min)

1. `master` actualizado y compilado en Release. Apuntad el commit que enseña el overlay.
2. Steam cerrado, pantalla completa sin bordes (`--fullscreen`), auriculares, cada uno con su mando habitual (apuntad el modelo).
3. Crear la carpeta de la sesión: `python tools/telemetry/new_session.py` (crea `notas.md` y `hoja.csv` a partir de las plantillas).
4. **Ajustes base** para todo lo que se compara: ayuda de dirección activada, cambio automático, control de tracción desactivado, ABS desactivado (como arranca el juego desde el lote 3), suspensión media. Cada uno usa su sensibilidad de dirección, pero se apunta.
5. Papeles: uno **conduce** y el otro **anota** (y maneja las teclas del modo a ciegas). Se cambian en cada bloque.
6. Calentamiento: 5 minutos libres por la zona con la configuración del primer bloque.

**Duración máxima: 90 minutos por sesión**, con una pausa a mitad. Con el cansancio se conduce peor y las notas empeoran.

**Cada intento es una grabación.** Antes de cada intento, Retroceso (reiniciar el mundo): empieza un archivo nuevo y el overlay enseña su nombre. Quien anota escribe ese nombre en la hoja. Al final de la sesión, copiad los `.rrec` de `grabaciones/` (junto al exe) a la carpeta de la sesión. No borréis nada de `grabaciones/`.

---

## 2. Recorrido de la zona

El recorrido completo (unos 4 min) pasa por todo lo que se valora:

1. Salida en la recta de asfalto: a fondo hasta ~150 km/h y frenada fuerte antes del final.
2. Baches de asfalto y ondulaciones de grava a ~100 km/h.
3. Eslalon de grava, ida.
4. Horquilla de grava, sin maniobrar (con freno de mano si queréis).
5. Círculo de grava: una vuelta derrapando en cada sentido.
6. Explanada: derrape a 100 km/h y recogida.
7. Cresta a ~80 km/h.
8. Rampa: parar a mitad con el freno, soltar y arrancar en cuesta.
9. Una vuelta cronometrada a la pista ciega en grava.

---

## 3. Bloques

Elegid 2 o 3 bloques por sesión. Orden recomendado: sesión 1, bloques A, B y D con el 205; sesión 2, bloque C; después, repetir A y B tras cada ajuste, y E al final.

### A. Prueba de derrape medida (criterio de la fase 2)

Con la configuración elegida (al principio, el 205). Cada conductor:

| Maniobra | Dónde | Intentos | Cómo |
| --- | --- | --- | --- |
| Sostenido | Círculo de grava | 3 por sentido | Entrar a ~50 km/h, provocar el derrape y sostenerlo **solo con acelerador y dirección** al menos media vuelta. Sin tocar el freno ni el freno de mano |
| Progresivo | Círculo de grava | 3 | Con el derrape sostenido, **soltar el acelerador de golpe** y mantener la dirección 2 s |
| Recogida | Explanada | 3 | A ≥ 100 km/h, cruzar el coche (golpe de dirección o freno de mano) y **recogerlo con contravolante** |

Apuntad en la hoja si el intento fue "limpio" (hicisteis la maniobra como se pide). Al final del bloque se puede ver al momento si pasa: `python tools/telemetry/check_session.py <carpeta>`.

### B. Superficies a ciegas (criterio de la fase 2)

1. Quien anota arranca con `--blind-seed <número>`, elegido por él y sin decirlo. Apunta la semilla en `notas.md`, sección privada.
2. Modo a ciegas activado: sin color, sonido ni vibración de superficie y sin nombres en el overlay.
3. 15 pruebas por conductor (cada superficie sale 3 veces; unos 30 minutos). En cada una, quien anota pulsa la tecla de la siguiente prueba ("Prueba k/15"), y el conductor da 2 vueltas a la pista ciega y dice **qué superficie cree que es** (asfalto, grava, tierra, nieve o arena) y **su seguridad** (1 = adivino, 2 = bastante seguro, 3 = seguro).
4. Quien anota no da pistas ni reacciona. La respuesta correcta no se dice hasta el final.
5. Al terminar, `check_session.py` saca la superficie real de cada grabación y la matriz de aciertos.

**Pasa** (decisión D5): cada conductor acierta al menos 13 de 15 (como mucho 2 fallos) y ninguna superficie se falla más de una vez. Al azar se acertarían unas 3.

### C. Configuraciones (elegir el coche del juego)

Las cuatro configuraciones con el mismo recorrido, con las superficies visibles. Se arranca con `--hide-config`: el overlay no dice qué configuración se lleva. Quien anota las cambia con F6 (siempre en el mismo orden) y las llama A, B, C y D según ese orden; lo apunta en la sección privada de `notas.md`. El conductor no sabe cuál lleva, aunque el carácter de algunas se adivine.

Orden (para que el cansancio y el aprendizaje no favorezcan siempre a la misma):

| | 1.ª | 2.ª | 3.ª | 4.ª |
| --- | --- | --- | --- | --- |
| Conductor 1 | A | B | C | D |
| Conductor 2 | C | A | D | B |

Con cada configuración: 1 vuelta de reconocimiento al recorrido completo, 3 vueltas cronometradas a la pista ciega en grava y la hoja de puntuación (§4) recién bajado del coche, sin hablarlo con el otro.

### D. Ajustes ("cada ajuste se nota")

Con el 205, una vuelta corta a la parte de la zona que toca, cambiando un ajuste cada vez:

| Ajuste | Dónde notarlo |
| --- | --- |
| Ayuda de dirección sí/no | Círculo, explanada y eslalon |
| ABS sí/no | Frenada fuerte en la recta, frenando y girando |
| Control de tracción: desactivado/bajo/alto | Salida en la nieve y en la horquilla de grava |
| Cambio automático/secuencial | Recorrido completo |
| Suspensión blanda/media/dura | Baches, cresta, eslalon de asfalto |
| Sensibilidad 0,7 / 1 / 1,3 | Eslalon |

Respuesta por ajuste: **¿Se nota?** (no / poco / claro) y **¿el efecto va en el sentido esperado?** (sí / no).

### E. Entrada y dirección (cierra decisiones provisionales)

En la horquilla y en el eslalon, con teclado y con mando:
- Velocidad de giro con teclado (al tope en 0,25 s, vuelta en 0,12 s), con stick (1/30 s) y del acelerador y el freno digitales: **lenta / bien / rápida**.
- Ayuda de dirección: ¿se nota cuándo empieza (15–30 km/h)? ¿Os quita el volante en algún momento? **bien / cambiar**, y dónde.

### F. Otros criterios de la fase 2

Cada conductor, con la configuración elegida:
- **Horquilla sin maniobrar:** 3 intentos; vale si no se da marcha atrás, no se recoloca y no se sale de la calzada (lo comprueba `check_session.py`).
- **Baches sin golpes falsos:** ¿algún golpe que no venga de algo que se ve? Apuntad dónde.
- **Turbo:** ¿hay que anticipar el acelerador al salir de la horquilla? (sí / no / exagerado).

### G. Carretera

La carretera de prueba de la zona (tarea 2.6b): 992 m de punto a punto, con una calzada de 6 m, arcén de arena, cuneta y árboles. **Y** lleva a su salida y **U** cambia la calzada entre grava y asfalto.

Cada conductor, con cada configuración que se esté probando: **3 pasadas** seguidas, desde la salida parado hasta la meta, en grava (y en asfalto, si la sesión lo pide). En cada pasada se apunta:
- **Tiempo:** el del indicador al cruzar la meta.
- **Salidas** de la calzada: dónde (curva o punto kilométrico) y qué pasó (arena, cuneta, árbol).
- **Recolocaciones**, si las hubo: la pasada sigue y queda marcada como «con recolocación».
- **«¿Se recoge el coche con poco sitio?»:** de 1 (nunca, me salgo) a 5 (siempre, con margen).

`check_session.py` pasa `road_check.py` a las grabaciones de la carretera. Por pasada, da el tiempo, las salidas (centro de masas a más de 4,6 m del eje, es decir, fuera del arcén), las recolocaciones, la velocidad mínima en la horquilla y el |β| máximo en cada curva. Si el tiempo o las salidas no cuadran con lo apuntado, manda la telemetría.

Criterio de la fase 2: con la configuración elegida, cada jugador hace 3 pasadas seguidas sin recolocar, y los dos dicen que el coche se recoge con poco sitio.

---

## 4. Hoja de puntuación por configuración (bloque C)

Del 1 al 5: **1** no funciona o frustra; **2** flojo; **3** correcto; **4** bueno; **5** como en un buen juego de rally.

| # | Criterio | Qué preguntarse |
| --- | --- | --- |
| 1 | Diversión | ¿Me lo he pasado bien? |
| 2 | Previsible en el límite | ¿Sabía lo que iba a hacer antes de que lo hiciera? |
| 3 | Derrape | ¿Entra, se sostiene y sale cuando quiero? |
| 4 | Recoger un error | ¿Lo salvo con contravolante y acelerador sin trompear de golpe? |
| 5 | Motor y turbo | ¿Anticipar el acelerador es interesante o frustrante? |
| 6 | Frenada y entrada en curva | ¿Frena recto y gira al soltar? |
| 7 | Horquilla | ¿Sale sin maniobrar? ¿El freno de mano hace lo que espero? |
| 8 | Baches y saltos | ¿La suspensión trabaja y aterriza sin golpes raros? |
| 9 | Peso | ¿Noto la transferencia de carga al frenar, acelerar y girar? |
| 10 | Ganas de repetir | ¿Quiero otra vuelta? |

Y además: **¿Lo elegirías para el juego?** (sí / quizá / no), el mejor tiempo de la pista ciega y una frase libre: lo mejor y lo peor.

---

## 5. Qué se guarda

| Qué | Dónde | ¿En git? |
| --- | --- | --- |
| Grabaciones `.rrec` de cada intento (llevan dentro el coche, los ajustes y la superficie real) | `docs/manejo/sesiones/<sesión>/` | Sí (son KB) |
| `hoja.csv` (una fila por medida) | Carpeta de la sesión | Sí |
| `notas.md`: commit, mandos, sensibilidades, síntomas con sus palabras y la sección privada (semilla a ciegas y letras de las configuraciones) | Carpeta de la sesión | Sí (la sección privada no se lee hasta el final de la sesión) |
| Telemetría CSV | `build/telemetry/<sesión>/`, regenerada con `rally_replay` | **No**: se regenera cuando haga falta |
| Capturas (opcional) | Carpeta de la sesión | Sí, si pesan poco |

Datos de telemetría que mira Claude Code en cada maniobra: β y velocidad de guiñada, radio de la trayectoria, deslizamientos y cargas por rueda, entradas aplicadas (dirección, acelerador, freno, freno de mano), RPM, presión del turbo y marcha, y la actividad del ABS y del control de tracción.

---

## 6. Plantilla de `notas.md`

```markdown
# Sesión AAAA-MM-DD-N

- Commit: <del overlay>
- Conductor 1: <nombre>, mando <modelo>, sensibilidad <valor>
- Conductor 2: <nombre>, mando <modelo>, sensibilidad <valor>
- Bloques: <A, B...>
- Configuración de los bloques A, B, D, E y F: <archivo>

## Síntomas (con vuestras palabras: qué, dónde, en qué intento)

- 

## Lo que ha gustado

- 

## Privado (lo escribe quien anota; se lee al final)

- Semilla a ciegas: 
- Letras del bloque C: A = , B = , C = , D = 
```

## 7. Columnas de `hoja.csv`

`sesion,bloque,conductor,config,intento,grabacion,maniobra,limpio,respuesta,seguridad,criterio,puntuacion,tiempo_s,notas`

- `maniobra`: `sostenido_h`, `sostenido_a` (horario y antihorario), `progresivo`, `recogida`, `horquilla`, `ciega`, `carretera` (bloque G), `ajuste:<nombre>=<valor>`, `entrada:<qué>`, `config`.
- `respuesta`: la superficie dicha (bloque B), "no/poco/claro" (D), "lenta/bien/rápida" (E), "sí/quizá/no" (elegir configuración), 1–5 a «¿Se recoge el coche con poco sitio?» (G; las salidas, en `notas`).
- `criterio` y `puntuacion`: una fila por criterio de §4 (1–10).
- `limpio`: `s` o `n`.
