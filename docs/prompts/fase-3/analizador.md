# Tarea `analizador` (lote 7, carril herramientas): analizador reducido de tramos reales

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-7.md` §2. Es la última tarea del lote en prioridad: si el uso no da, pasa al lote 8.

## Por qué

El tramo de 3–5 km del lote 8 se escribe a mano, y hoy no hay de dónde sacar cifras para que sea creíble: cuántas curvas por kilómetro, de qué radio, con qué rectas entre ellas y con qué pendiente. Lucas aprobó el 2026-11-11 un analizador reducido de 3–5 tramos reales. El banco de 20–40 tramos y el perfil de ritmo siguen en la fase 4 (`docs/04`).

**Lo que no puede dar, y no se le pide:** las amplitudes de la capa de irregularidad (centímetros), el ancho ni el peralte. El trazado de un mapa abierto tiene una precisión de metros, y un modelo de alturas abierto, una celda de unos 30 m. Esos valores salen de `docs/01` §7.3 y de la prueba de Lucas.

## Qué hacer (Python, en `tools/stages/`, con sus tests junto al código, como en `tools/telemetry/`)

1. **Entrada:** un trazado en GPX o GeoJSON (una línea de puntos con latitud, longitud y, si la trae, altura). La herramienta trabaja sin red.
2. **Análisis:**
   - proyecta a metros, remuestrea a paso fijo y suaviza lo justo para quitar el ruido del dibujo sin comerse las curvas (el paso y el suavizado los propones tú, con su prueba);
   - curvatura κ(s) y **cambios de dirección con el mismo criterio que el juego** (`src/game/direction_changes.h`: tramos de curvatura de un solo signo, de 5° o más);
   - por tramo: longitud; cambios de dirección por kilómetro; distribución del radio mínimo de cada cambio, de su ángulo y de su longitud; distribución de las rectas entre cambios; número de horquillas (≥ 150°); y, si hay alturas, distribución de la pendiente y desnivel acumulado.
3. **Salida:** un JSON por tramo con esas cifras y un informe en Markdown que compare los tramos en una tabla. Además, para cada tramo, la lista de sus cambios de dirección (s, ángulo, radio mínimo), que sirve de guion para escribir un `.stage` a mano.
4. **Descarga, aparte y opcional:** un script que, dados el punto de salida y el de meta, baja las carreteras de la zona de OpenStreetMap (Overpass), sigue la carretera entre los dos puntos y escribe el GPX; y que añade alturas de un modelo abierto si puede. **Si la red no está disponible o el modo automático lo rechaza, no insistas:** entrega la herramienta con el caso sintético y deja en el informe la orden exacta para que la lance Lucas.
5. **Tramos** (Lucas: «los más aclamados por la comunidad andaluza y española; luego algunos europeos o latinoamericanos»). Por este orden, hasta 5:
   - **Medina Azahara** (Rallye Sierra Morena, tramo de calificación, 3,10 km, carretera CO-3314, Córdoba): salida en 37°53′25″ N, 4°51′43″ O; meta en 37°54′05″ N, 4°52′14″ O (del libro de ruta del 41.º Rallye Sierra Morena). Es el caso real de prueba: su longitud tiene que salir 3,10 km ± 5 %;
   - **Espiel** (Sierra Morena, 4,8 km) y **Villaviciosa** (Sierra Morena, 27,05 km);
   - **Col de Turini** (Rallye Monte-Carlo, La Bollène-Vésubie–Sospel);
   - **Fafe** (Rally de Portugal, 11,18 km, tierra);
   - **El Cóndor** (Rally Argentina).

   De todos menos el primero, el consultor no tiene las coordenadas de salida y meta. Búscalas si tienes acceso a la web y cita de dónde salen; si no, pídeselas a Lucas en el informe. **No las inventes.**

## Contratos

- **Sin dependencias nuevas: solo la biblioteca estándar de Python,** como `telemetry.py` y los tests de la zona (`tools/telemetry/requirements.txt` solo pide matplotlib para las gráficas, y aquí no hay gráficas). Si te hace falta otra cosa, es una pregunta para Lucas (`CLAUDE.md`), no una decisión tuya.
- No toca `src/` ni los tests de otros carriles.
- Los trazados descargados no se guardan en git si pesan más de 200 KB cada uno o si su licencia no lo permite: en ese caso, se guarda el JSON de resultados y la orden que los regenera. Los datos de OpenStreetMap llevan su atribución en el informe.

## Criterios

1. **Caso sintético** (el que decide): un trazado generado con rectas, arcos y clotoides de radios conocidos (30, 60, 120 y 300 m, y una horquilla de R 8), con ruido de ±2 m en los puntos y un punto cada 10–20 m, como un mapa real. El analizador recupera el número de cambios de dirección exacto, sus ángulos a ±5° y sus radios mínimos a ±20 % (±40 % en la horquilla). Si no llega, informa de lo que da y con qué paso de puntos: es el límite del método, no una parada.
2. El mismo criterio de cambios de dirección que el juego: para `lazadas.stage` convertido a puntos sin ruido, salen los seis cambios de `mensajes/lote-6/prompts/cambios-direccion.md` (100 m derecha 78,8°; 390 izquierda 121,8°; 520 derecha 121,8°; 730 derecha 180°; 828,85 izquierda 180°; 907,70 derecha 51,6°), a ±1°.
3. Medina Azahara, si hay red: longitud de 3,10 km ± 5 %.
4. Los tests corren con `ctest`, registrados en `tests/tools/CMakeLists.txt` como `zone_json.python` (con `Python3_Interpreter_FOUND`), sin red ni archivos de fuera del repositorio.

## Paradas

- Hace falta una dependencia nueva.
- Un test de otro carril falla.

Lo demás, avisos.

## Para el informe

- La tabla comparativa de los tramos analizados, y qué cifras recomiendas para un tramo de 3–5 km de montaña con bosque y tierra (prioridad de Lucas: montaña, bosque y tierra).
- La precisión medida en el caso sintético.
- Lo que no se pudo descargar y la orden para hacerlo.
- Fragmento `docs/progreso/analizador.md`: la edición de `docs/07` (cómo se usa la herramienta).
