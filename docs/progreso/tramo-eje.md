| 2026-10-22 | 3 | | Tarea `tramo-eje` (lote 3, carril sim): formato de tramo 1 (`.stage`) y geometría del eje, sin Jolt, sin dibujo y sin seguimiento. `sim/stage_axis.h`: `StageAxis::parse` (errores «línea N: …», `std::from_chars`, al estilo de `parse_car`) y `pose(s)` con posición, altura, rumbo, curvatura, pendiente, curvatura vertical, peralte, semianchos y superficie. Planta exacta en rectas y arcos (las fórmulas de `zone_axis.h`) y, en las clotoides, rumbo en forma cerrada y posición por Gauss–Legendre de 5 puntos desde nodos cada metro como mucho, calculados al cargar. Alzado por PIV con parábolas, peralte y semianchos lineales, superficie por tramos; `hash()` FNV-1a de los datos leídos (`Fnv1a64::add_u64` y `add_f64`, nuevas). Tramo de test `tests/sim/stages/lazadas.stage` (1150 m: curva peraltada, cresta, clotoide de inflexión de R −40 a R +40, cambio de superficie y de ancho y dos horquillas seguidas cuyos brazos pasan a 13,35 m). Tests 435 → 452 (17 nuevos en `tests/sim/test_stage_axis.cpp`): rectas y arcos frente a `AxisBuilder` 0 m y 0 rad (≤ 1·10⁻⁹ y 1·10⁻¹²); clotoides frente a Simpson de 200 000 intervalos 2,4·10⁻¹² m (≤ 1 mm); juntas 1,0·10⁻⁹ m y 1,7·10⁻¹⁰ rad (≤ 1·10⁻⁶ y 1·10⁻⁸); círculo de 4 arcos 1,2·10⁻¹⁴ m (≤ 1·10⁻⁹); curva vertical +5 % → −7 % baja 0,6 m en el PIV; 38 casos de error con su línea; 0 `operator new` en 100 000 `pose`; `pose` 0,09 µs por llamada en Release (≤ 2) y 2,6 µs en Debug; `kExpectedStageHash` = `0x5876c61a309019a0`, igual en Debug y Release; lazadas 13,35 m (8–15). Huella 0x66c9e3a86632750e, reservas 1990, telemetría 0xfe3c612ddcaa6805, `kSimVersion` 5: sin cambios (`StageAxis` no entra en `SimWorld`). Sin validar a ojo (no se conduce). | Lote 4: seguimiento de `s` y `t` sobre `StageAxis` y la tablet |

Utilidades nuevas: `Fnv1a64::add_u64` y `add_f64` (hoy el double se parte a mano en dos `add_u32` en `sim_world.cpp`, `test_zone.cpp` y `test_telemetry.cpp`; mismos bytes, ningún hash cambia; esos usos no se tocan) y la cuadratura de Gauss–Legendre de 5 puntos, local a `stage_axis.cpp` (no había ninguna en el repositorio). `trim` y `fail` se copian de `car_data.cpp` (están en su espacio anónimo y `fail` devuelve `CarParseResult`).

Hallazgos: ninguno.

Ediciones propuestas (integrador):

- `docs/02-arquitectura.md` §6.1, al final de la sección (después del párrafo «Esqueleto e irregularidad»), añadir:

  ````markdown
  **Formato 1 (`.stage`, lote 3).** Lo lee `sim/stage_axis.h` (`StageAxis::parse`; `kStageFormatVersion = 1`). Es solo el esqueleto: la irregularidad, los arcenes, el terreno y los objetos son del formato 2 y siguientes, y el número de versión es el gancho (no se reservan claves). Texto `clave = valor` y secciones `[nombre]`, comentarios con `#`, claves en inglés, unidades SI, números con `std::from_chars`:

  ```text
  format = 1                 # la primera clave del archivo
  name = <texto>
  start_x = <m>              # mundo: x al este, z al sur (zone_axis.h)
  start_z = <m>
  start_heading_deg = <°>    # rumbo de salida: 0 = norte (−z), + a la derecha
  start_height = <m>         # altura del eje en s = 0 (igual a la del primer pvi)

  [plan]                     # planta, en orden; s empieza en 0
  straight <L>
  arc <L> <R>                # R con signo: + derecha, − izquierda
  spiral <L> <R0> <R1>       # curvatura lineal de 1/R0 a 1/R1; inf = recta

  [profile]                  # alzado: puntos de intersección vertical
  pvi <s> <altura> <L_curva> # rasante recta entre PIV; parábola de L_curva centrada en cada PIV

  [bank]                     # peralte, lineal entre puntos
  bank <s> <%>               # dy/dt de la calzada en %, + sube hacia la derecha (como RoadPoint::bank)

  [width]                    # semianchos de la calzada, lineales entre puntos
  width <s> <izquierda> <derecha>

  [surface]                  # superficie de la calzada desde s hasta la línea siguiente
  surface <s> asphalt|gravel|dirt|snow|sand
  ```

  Reglas (cada una que no se cumple es un error «línea N: …» y el tramo no se carga):
  - `format` es la primera clave y vale 1. Las cinco secciones están, una vez cada una y no vacías; las cinco claves de la cabecera también. Una clave, sección o primitiva desconocida, que falta o repetida es un error, y también un número de valores distinto del de su primitiva.
  - L > 0. |R| ≥ 5 m; R = 0 no vale. `inf` solo vale como R0 o R1 de `spiral`; `nan` e `inf` en otro sitio son números mal escritos.
  - El primer `pvi` va en s = 0, con la altura de `start_height`, y el último en s = longitud (±1·10⁻⁶ m: la longitud es la suma de las L), los dos con L_curva = 0. L_curva ≥ 0. Las s crecen y ninguna curva vertical se solapa con la anterior (tocarse vale).
  - En `[bank]`, `[width]` y `[surface]`, la primera línea va en s = 0, las s crecen y ninguna pasa de la longitud (±1·10⁻⁶ m). El último valor sigue hasta el final. Los semianchos son > 0.

  Geometría (`StageAxis::pose(s)`, con s acotada a [0, longitud]; se calcula todo al cargar y `pose` no reserva memoria):
  - Rumbo θ(u) = θ₀ + κ₀·u + (κ₁ − κ₀)·u²/(2L) en cada primitiva, continuo en todo el tramo y sin envolver a [0, 2π). Rectas y arcos, exactos con las fórmulas de `zone_axis.h`; clotoides, por Gauss–Legendre de 5 puntos desde nodos de posición cada metro como mucho.
  - Intervalos semiabiertos: en una junta vale ya la primitiva nueva; la curva vertical de un PIV ocupa [s − L_curva/2, s + L_curva/2); con L_curva = 0, la pendiente nueva vale desde la s del PIV; una superficie nueva vale justo en su s.
  - El peralte del archivo va en %; el de la pose, en tanto por uno (dy/dt).
  - `hash()`: FNV-1a de los datos leídos, campo a campo (todo menos `name`), para comprobar que los dos PC tienen el mismo tramo.
  ````

Pide de lo compartido: nada.
