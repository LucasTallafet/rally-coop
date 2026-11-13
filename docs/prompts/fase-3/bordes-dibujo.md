# Tarea `bordes-dibujo` (lote 7, carril plataforma): bordes exactos entre superficies cerca del ojo

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-7.md` §2. Criterio de gusto: `docs/agentes/criterio-de-lucas.md` §4.

## Por qué

En la prueba del lote 6, Lucas vio «saltos del final de las superficies, con polígonos vistos, como de juego mal hecho». Mandó tres capturas (`D:\Proyectos\RallyCoop\Rally\mensajes\lote-7\capturas\poligonosVistos01.png`, `02` y `03`, en la copia principal; con el ojo en (−266, 6, 66), (12, 2, −212) y (4, 15, −190)):
- en la 01 y la 03, dientes de sierra regulares donde el arcén de arena toca la cuneta, uno por celda, a lo largo de todo el borde;
- en la 02, la franja de arena en escalera, y la silueta contra el cielo, dentada.

Lo que ha visto el consultor en el código: a menos de `kNearRadius` = 30 m del ojo no se dibuja la malla del núcleo, solo la rejilla cercana (`stage_draw.cpp`: las mallas se recortan con `far_radius`), y la rejilla toma **un índice de color por celda** de 0,25 m (`stage_near.cpp`: `texel.surface`). Un borde curvo pintado por celdas sale dentado. No son los escalones del aviso (a) de `formato-2`.

## Parte A: diagnóstico (antes de cambiar nada)

1. Reproduce las tres capturas (el ojo está arriba; la dirección, a ojo con la imagen) y guárdalas.
2. Atribuye cada defecto a su causa con una prueba, no por parecido: por ejemplo, un color de depuración por «quién dibuja este píxel» (rejilla cercana, malla del núcleo, baldosa lejana). Tres defectos por explicar: los dientes del borde arcén–cuneta, la escalera de la franja de arena y la silueta dentada contra el cielo.
3. Si alguno no sale de la rejilla cercana (por ejemplo, la silueta es el final de la franja de ±60 m o de la malla de la banda), dilo con su causa y su coste antes de seguir. Si su arreglo es de otra tarea o de la fase 6, queda como aviso.

## Qué tiene que quedar (el resultado; el cómo lo propones tú)

1. **Un borde entre dos superficies o aspectos se dibuja donde está en la geometría,** cerca y lejos: calzada–arcén, arcén–cuneta, cuneta–talud, talud–muro y final de la banda–terreno. Error en planta de como mucho 5 cm a menos de 30 m del ojo.
2. **Sin costura a 30 m:** ni de color ni de altura visible.
3. **Lo que se ve sigue coincidiendo con lo que se pisa** (`docs/01` §7.3): el microrrelieve que hoy enseña la rejilla cercana no se pierde.
4. **La zona de pruebas no cambia:** `ZoneDraw` comparte `NearGrid` y sus capturas tienen que salir iguales.

Dos caminos que ve el consultor, para que evalúes y propongas el tuyo:
- **(i)** la rejilla sigue dibujando, pero cada celda lleva una distancia con signo a los bordes (un campo que se interpola bien entre celdas) y el color se decide por píxel con esa distancia;
- **(ii)** cerca del ojo se dibuja también la malla del núcleo y de la banda, que ya tiene los bordes exactos, y la rejilla queda para el terreno de fuera. Tiene que resolver el punto 3 y que la malla y la rejilla no parpadeen una sobre otra.

## Contratos (de `mensajes/lote-7.md` §2)

- No se toca `src/sim`. Si te falta un dato de `StageGround` o de `StageTrack`, es «Necesita consultor», no un cambio tuyo.
- Capturas de `tests/platform/golden/`: cambia `tramo_salida.png` y la regeneras tú con `golden.ps1 -Regenerar`. Ninguna de la zona cambia.
- El dibujo en un hilo de fondo **no** es de esta tarea (decisión del lote, §6).

## Criterios (tests en `tests/platform/`, Release y Debug)

1. Sin ventana: para 2000 puntos a menos de 30 m de un ojo puesto en `lazadas` (s 100, 633, 740 y 841), a más de 5 cm de cualquier borde, el color que decide el dibujo es el del aspecto del triángulo de arriba (`StageSurface::query`). Hoy tiene que fallar: compruébalo antes de arreglarlo y pon cuántos puntos fallan.
2. Las tres capturas de la parte A, repetidas después: sin dientes. Van en el informe, antes y después.
3. Las capturas de la zona, iguales byte a byte.
4. Coste, medido antes y después en la misma ejecución, con `--bench-stage` en `lazadas` a 60 y a 120 km/h: CPU del dibujo y de la rejilla cercana, media y p99, triángulos y memoria de GPU. Referencia del lote 6: ~1,6 ms de media a 60 km/h y ~3,0 a 120; p99 de 9,8 ms a 120.
   - **Parada** si el p99 de la CPU del dibujo a 120 km/h pasa de 16,7 ms (un cuadro a 60 Hz, `docs/03`).
   - Aviso con la cifra si la media sube más de un 25 %.
5. Los tests de hoy de `test_stage_draw.cpp`, sin tocar.

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| Los dientes salen del índice de color por celda | Parte A, punto 2 | Para con el diagnóstico: el arreglo es otro |
| La textura de la rejilla admite el dato que necesite tu camino sin romper la zona | `near_grid.h`: RGBA32F con (altura, h, superficie, válido), los cuatro canales ocupados | Segunda textura o empaquetado solo para el tramo; la zona sigue como está |
| Con el camino (ii), el microrrelieve se sigue viendo | Captura de la calzada a 5 m, antes y después | Camino (i), o dilo como límite |

## Paradas

- La primera premisa es falsa.
- Cambia una captura de la zona.
- El p99 del criterio 4.
- Un test de otro carril falla.

## Para el informe (la prueba de Lucas)

- Las capturas antes y después, y la causa de cada defecto.
- Las cifras del criterio 4.
- Preguntas para Lucas: ¿quedan dientes o escalones en algún borde? ¿Se nota un cambio a 30 m del coche?
- Fragmento `docs/progreso/bordes-dibujo.md`: la edición de `docs/02` §6.6 («Dibujo del tramo»).
