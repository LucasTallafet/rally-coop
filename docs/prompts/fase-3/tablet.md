# Tarea `tablet` (lote 6, carril plataforma): mapa, perfil, cinta y cursor en el copiloto

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-6.md` §2, que manda sobre este prompt. Sale con `red-tramo` y `cambios-direccion` ya fusionadas.

## Por qué

Es la herramienta del copiloto (`01` §4 y §5.2; `05`, «Tablet», «Notas en la tablet» y «Mirar»). Con ella se mira la carretera que viene para cantarla. En este lote no hay notas ni diales (lote 7), así que el cursor solo se mueve y no ancla nada todavía.

## Qué hacer

1. **`--ventana <ancho>x<alto>`** en `parse_copilot_args`: por defecto 640 × 360, como hoy, y valores de 640 × 360 a 3840 × 2160. **Solo un argumento** (Lucas, 2026-11-08): no se guarda en `ajustes.txt` ni en ningún otro ajuste, y el copiloto sigue sin escribir `ajustes.txt` (el test de hoy se queda). Errores con su mensaje, como las demás opciones, y casos en el test de argumentos mal escritos.
2. **El panel de la tablet** en la ventana del copiloto, centrado abajo (como si la llevara en las rodillas), con la vista del asiento encima. De partida, el 60 % del ancho y el 55 % del alto; el sitio y el tamaño exactos los propones en el plan con las capas de `screen_layout` (sin solaparse con F1, F3 ni H a 640 × 360, 1280 × 720 y 1920 × 1080). **Siempre a la vista en este lote:** guardarla y sacarla necesitan los mensajes de acción por la red, que no existen. Deja el gancho: un `bool` de «tablet a la vista» que hoy siempre vale verdadero.
3. **Mapa** (la mayor parte del panel):
   - Vista cenital de la carretera alrededor de la `s` interpolada del host (`SampledState::s`, de `red-tramo`). Nunca de una proyección propia del copiloto.
   - **Orientación A** (por defecto): por la tangente del eje en esa `s`, suavizada (primer orden; τ en el plan, para que no tiemble con la interpolación ni salte en una recolocación). No depende del morro ni de la velocidad: un derrape o un trompo no la mueven.
   - **Orientación B:** por la dirección del punto del coche al punto del eje 40 m por delante (la orientación «a probar en la fase 3» de `01` §4, para que en las horquillas el mapa no dé media vuelta). Una tecla del copiloto la conmuta y lo dice en un mensaje.
   - **Flecha fija** en el tercio inferior, apuntando hacia arriba.
   - **Zoom en 3 niveles fijos según la velocidad** (`05`, «Tablet», decidido por Lucas el 2026-11-08), para ver unos 9 s por delante de la flecha: 100 m hasta 40 km/h, 200 m hasta 80 km/h y 400 m por encima. La velocidad es la de avance por la carretera (d`s`/dt de la `s` interpolada, suavizada). Con histéresis del 10 % en los cambios de nivel y la transición suavizada (~0,5 s), para que no parpadee.
   - **Marcas cada 50 m** de `s` a lo largo de la carretera y una **referencia de escala**.
   - **La línea de la carretera, por superficie:** color (asfalto gris, grava beige, tierra marrón claro, arena amarillo pálido, nieve blanca) **y** cinco trazos distintos, porque el color nunca va solo (`01` §4). El arcén no se distingue en el mapa.
   - **Nada de notas** (`CLAUDE.md`, «Cosas que no hacer»): en el mapa solo están la carretera, sus marcas, la flecha y el cursor.
4. **Perfil de altura** (franja de abajo): la altura del eje (`StageAxis::pose(s)`) desde la `s` del coche hasta la distancia del zoom, con el coche a la izquierda. La escala vertical se ajusta al rango de lo que se ve, con un mínimo de 10 m para que un tramo llano no parezca una sierra.
5. **Cinta de ruta** (franja lateral): los próximos 500 m en recta, con una marca de sentido (izquierda o derecha, sin la forma) en el `s_start` de cada cambio de dirección de `cambios-direccion`.
6. **Cursor** (`01` §5.2, sin notas):
   - por defecto, en el siguiente cambio de dirección por delante del coche (`next_direction_change`); cuando el coche lo pasa, salta al siguiente;
   - la cruceta arriba o abajo salta al cambio siguiente o al anterior (nunca por detrás del coche); mantenida, desliza el cursor por la carretera, más deprisa cuanto más tiempo se mantiene (de 20 m/s a 200 m/s en 2 s, de partida). Con teclado, las flechas ↑ y ↓ en el contexto del copiloto;
   - **el puntero:** mientras se mantiene la barra espaciadora (en el contexto del copiloto, como B en el lote 3), el ratón deja de mirar, se suelta el cursor del sistema, la cabeza se congela (`HeadLook::set_frozen`, `05`, «Mirar») y un clic en la carretera del mapa coloca el cursor en la `s` del eje más cercana al clic, entre lo que se ve. Al soltar la barra, vuelve a mirar desde donde estaba la cabeza;
   - el cursor se dibuja en el mapa y en la cinta.
7. **La geometría, sin raylib:** lo que se calcula (transformación del mapa, nivel de zoom, marcas, polilíneas por superficie, perfil, cinta y estado del cursor) en funciones puras de `rally_loop` o de un archivo sin raylib, para probarlo sin ventana y medirlo. El dibujo es una capa fina encima.
8. **Teclas:** la tabla del copiloto gana la orientación del mapa, el puntero (Espacio) y las flechas, con el contexto del copiloto. El test de teclas repetidas pasa y `docs/teclas.md` se regenera. Mando: la cruceta para el cursor (comprueba que no choca con nada del copiloto).
9. **F1 del copiloto:** una línea «CPU tablet» con la media y el máximo de la geometría y del dibujo de la tablet.

## Criterios (tests en `tests/platform/`, Release y Debug)

1. **Orientación A:** con la `s` en una recta y en la mitad de una curva de `lazadas`, el eje sale vertical bajo la flecha (±0,5°); con un rumbo del coche distinto del de la carretera (un trompo), el mapa no cambia.
2. **Orientación B:** en el vértice de la horquilla 1, el punto del eje 40 m por delante queda arriba (±0,5°).
3. **Zoom:** con velocidades fijas de 30, 60 y 120 km/h salen los niveles de 100, 200 y 400 m. Subiendo y bajando de forma alterna un 5 % alrededor de 40 y de 80 km/h, el nivel no cambia (histéresis).
4. **Marcas:** cada 50 m de `s`, en la carretera dibujada, dentro de 0,5 px de su posición a 1280 × 720.
5. **Superficies:** cada tramo de `[surface]` sale con su color y su trazo, y no hay dos superficies con el mismo trazo.
6. **Cinta:** en la salida de `lazadas` salen las marcas de los cambios 1 y 2 de `cambios-direccion` (a 100 y 390 m, a la derecha y a la izquierda) y ninguna más. Con el coche en s = 600, las de 730, 828,85 y 907,70 (de 600 a 1100 m), y ninguna más.
7. **Cursor:** el cursor por defecto; el salto cuando el coche lo pasa; siguiente y anterior; el deslizamiento (posición tras 1 s y 3 s mantenido); nunca por detrás del coche; el clic a 3 px de la carretera da la `s` del eje más cercana (±0,5 m a 1280 × 720); y el clic que cae donde dos brazos de las lazadas se ven cerca, la del brazo en el que se hizo.
8. **Puntero:** mientras se mantiene, `HeadLook` está congelada (bit a bit) y el ratón no mira; al soltar, sigue desde donde estaba.
9. **Ventana:** `--ventana 1280x720` y `1920x1080` se aceptan; `639x360`, `1280x`, `x720` y `4000x3000` se rechazan con su mensaje. Sin el argumento, 640 × 360. Ningún archivo nuevo junto al exe.
10. **Capas:** el panel no se solapa con F1, F3, H ni los mensajes a 640 × 360, 1280 × 720 y 1920 × 1080.
11. **Coste:** la geometría de la tablet en Release, en un recorrido de `lazadas` a 60 y a 120 km/h, en µs por fotograma (media y máximo). Se informa; sin límite (se fija con la cifra).

**Sin cambios:** la vista del conductor y sus capturas de referencia (el copiloto no sale en ellas), la simulación, la red y el formato de la grabación.

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| `SampledState::s` es continua salvo en una recolocación (lo garantiza `red-tramo`) | El test de `red-tramo` | Parada |
| El copiloto tiene el `StageTrack` y el `StageAxis` del tramo recibido | `CopilotWorld::world->stage()` (hoy lo usa `StageScene`) | Parada |
| Espacio, ↑, ↓ y la tecla de la orientación no chocan en el contexto del copiloto | El test de teclas repetidas | Elige otra tecla y dilo en el informe (no es una parada) |

## Paradas

Solo un criterio de resultado que falla (1, 2, 3, 6, 7, 8 o 9) o una premisa falsa que la tabla marca como parada. Lo demás, avisos.

## Para el informe (la prueba de Lucas)

- `rally.exe` en el tramo (K) y `rally.exe --copiloto --ventana 1280x720` en otra consola. Conduciendo `lazadas`:
  - ¿Se entiende la carretera que viene mirando solo la tablet? ¿Y en las horquillas, con la orientación A y con la B?
  - ¿Sirven la cinta y el perfil para saber lo que viene después de lo que cabe en el mapa?
  - ¿El zoom cambia en buen momento, sin molestar?
  - El cursor con las flechas y con el puntero (Espacio y clic): ¿es cómodo?
- F1 del copiloto: «CPU tablet».
