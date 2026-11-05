# Tarea `tramo-dibujo` (lote 5, carril plataforma): el tramo en pantalla, con la geometría de la física

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-5.md` §2.1 y §2.6 (`StageDraw`), que mandan sobre este prompt. Sale de `integracion` con `tramo-ajustes` y `f3-ejes` ya fusionadas.

## Por qué

Para conducir `lazadas` hay que verla, y lo que se ve tiene que ser lo que pisa la rueda (`01` §7.3). La física del tramo (`sim/stage_ground.h`) ya calcula la malla del núcleo y las baldosas del terreno; esta tarea las dibuja, con el microrrelieve cerca del ojo como la zona de pruebas. Meter el tramo en el juego (K, salida, tiempo, copiloto) es de `tramo-modo`, la tarea siguiente del carril.

## Qué hacer

1. **`StageDraw`** (`src/platform/stage_draw.{h,cpp}`) con la interfaz de §2.6:
   - `build` llama a `sim::StageGround::build(track)` (la misma función y los mismos argumentos por defecto que `SimWorld::build` con `StageGroundVariant::Design`) y compara su `hash()` con el esperado: si no coincide, false con los dos valores;
   - **lejos:** el núcleo con un color por superficie (`surface_color`; la arena del arcén con el suyo) y las baldosas del terreno (tierra), en trozos que se dibujan solo si caen cerca del ojo. Las muestras sin colisión (`kStageNoCollision`) no se dibujan;
   - **cerca del ojo:** la calzada y el arcén con el microrrelieve de la rueda (`stage_relief_frame` y `stage_relief_height`), sombreado con su normal y sin escalón al pasar a lo lejano, como la rejilla cercana de `ZoneDraw` (radio y desvanecido: los suyos, salvo que midas que no valen);
   - **líneas** de salida, de cada parcial (`kSplitSpacing`, `kSplitFinishGap`) y de meta, sobre el núcleo;
   - estadísticas de CPU como `ZoneDraw` (último, media, máximo, desde `reset_totals`).
2. **`--bench-stage`:** como `--bench-road` (pantalla completa sin bordes, escena a 1920×1080 en una RenderTexture, calentamiento y medida, archivo `rendimiento_NNN.txt` junto al exe). Carga `data/stages/lazadas.stage` y recorre su eje con la cámara del ojo a 60 km/h, de s = −5 m a la meta, sin simular el coche. En la misma ejecución, la carretera de prueba de la 2.6b como referencia.
3. Sin raylib en lo que se pueda probar sin ventana (los trozos, qué trozos tocan, la malla cercana): en `rally_loop` o con funciones puras, como `screen_layout`.

Reutiliza `ZoneDraw` (shader de recorte por distancia, rejilla cercana en anillo, `surface_color`), `tree_instances` si te hace falta instanciar, y `camera_space` (render relativo a la cámara). Si escribes una utilidad nueva, el plan dice qué buscaste y por qué no sirve lo que hay. **No dupliques la geometría del tramo:** viene de `StageGround`.

## Criterios (tests en `tests/platform/`, Release y Debug, sin ventana)

1. **Misma geometría:** los vértices y triángulos que se dibujan lejos son los de `StageGround` (mismo número y mismas posiciones, en float relativo al origen del suelo), y `build` rechaza un hash distinto.
2. **Lo cercano coincide con lo que pisa la rueda:** en 1000 puntos de la calzada y el arcén de `lazadas` (PCG32, semilla fija) dentro del radio cercano, la altura dibujada frente a la superficie del núcleo + `stage_relief_height`. **Medir e informar** (media, máximo y dónde), sin límite: el consultor lo fija después con la cifra.
3. **Sin escalón** entre lo cercano y lo lejano en el borde del radio: medir e informar el salto máximo.
4. **Trozos:** cada trozo del núcleo y cada baldosa entra en exactamente un trozo; un trozo a más de la distancia de dibujo no se dibuja y uno dentro sí (con la distancia que elijas, escrita en el informe).
5. **Sin reservas por fotograma** en `draw` (contador de `operator new`, como en `perf stats`), salvo la primera vez que entra un trozo, si las hay: dilo.

**Rendimiento** (`--bench-stage`, Release, RX 580, monitor de Lucas; en el informe, con la carretera de la 2.6b medida en la misma pasada):
- CPU del dibujo del tramo (media, p99, máximo), GPU, draw calls, triángulos dibujados, fotograma y memoria;
- **criterio de resultado** (`03`): fotograma medio ≤ 16,7 ms a 1920×1080 y draw calls ≤ 2000. Si no se cumple, primero reduce lo lejano (por ejemplo, baldosas a menos resolución con la distancia) sin tocar lo cercano; si aun así no, parada.

**Sin cambios:** nada de `src/sim`, `src/io`, `src/net` ni `src/game`; la huella y los demás hashes; las capturas de referencia de hoy (el juego aún no dibuja el tramo: `golden.ps1` igual).

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| `StageGround::build(track)` en la plataforma da el mismo hash que en `SimWorld::build` | Criterio 1, con un `SimWorld` de `lazadas` en el test | Parada: no se puede dibujar lo mismo que se pisa |
| `stage_relief_frame` y `stage_relief_height` se pueden llamar fuera del paso con los datos de un `TestZone` de ruido como el de `SimWorld` | Un test que da la misma h que la rueda en un punto (`probe` del test de `tramo-fisica`, si lo hay) | Parada |
| Las baldosas de `lazadas` (108 baldosas, ~0,86 M de triángulos a 0,75 m) caben en el presupuesto de la GPU dibujadas enteras | `--bench-stage` (el bosque de 1 M de triángulos daba 1,65 ms de GPU en la RX 580, fase 1) | No es parada: se reduce lo lejano (criterio de rendimiento) |

## Paradas

Solo: una premisa marcada como parada que es falsa, o el criterio de rendimiento sin cumplir tras reducir lo lejano. Lo demás son avisos.

## Para el informe

- Las cifras de los criterios 2–4 y del rendimiento.
- Capturas de `--bench-stage` (desde el asiento, en una horquilla y en una recta).
- Lo que `tramo-modo` necesita y no está: dilo, no lo hagas.
- Nada que probar jugando todavía (lo prueba Lucas con `tramo-modo`).
