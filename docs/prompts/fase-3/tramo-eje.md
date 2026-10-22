# Tarea `tramo-eje` (lote 3, carril sim): formato de tramo 1 y geometría del eje

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-3.md` §2.1, §2.3 y §2.4, que mandan sobre este prompt.

## Por qué

La fase 3 necesita un tramo escrito a mano (`04`, fase 3), y todo lo que viene después se calcula sobre el esqueleto de su eje (`02` §5 y §6.1):
- el seguimiento de `s` y `t` (lote 4);
- la tablet, el perfil de altura y las notas;
- la construcción de la calzada.

Esta tarea solo hace el formato y la geometría del eje: **sin Jolt, sin dibujo, sin seguimiento y sin irregularidad.**

## Qué hacer

1. **Lector del formato 1** (`lote-3.md` §2.3) con `std::from_chars`, al estilo de `parse_car` (`src/sim/car_data.h`):
   - devuelve un error «línea N: …» y no aborta;
   - una clave, sección o primitiva desconocida, una que falta o una repetida es un error.
2. **`StageAxis`** (`lote-3.md` §2.4):
   - **Planta.** Rumbo en forma cerrada: θ(u) = θ₀ + κ₀·u + (κ₁ − κ₀)·u²/(2L). Posición por integración numérica en las clotoides; en rectas y arcos, exacta y con la misma convención que `zone_axis.h` (x al este, z al sur, rumbo 0 hacia −z, + a la derecha).
     - **Recomendado:** al cargar, nodos de posición cada metro (Gauss–Legendre de 5 puntos por tramo de 1 m como mucho). En `pose`, integración desde el nodo anterior con Gauss–Legendre de 5 puntos.
     - Búsqueda de la primitiva por bisección sobre `s0`.
   - **Alzado.** Rasante recta entre PIV. Parábola de longitud L centrada en cada PIV, con la pendiente continua en sus extremos.
   - **Peralte y semianchos** lineales entre puntos. **Superficie** por tramos: una `s` justo en el límite es ya de la nueva.
   - **`hash()`:** FNV-1a (`sim/fnv1a.h`) de los datos leídos (primitivas, PIV, peralte, anchos y superficies), campo a campo.
3. **Tramo de test** `tests/sim/stages/lazadas.stage`, de 1–1,5 km, escrito a mano. Tiene que llevar:
   - dos horquillas seguidas (lazadas) cuyos ejes pasan a entre 8 y 15 m uno de otro: el seguimiento del lote 4 las necesita (`02` §5, tests);
   - una clotoide de inflexión (de R −40 a R +40);
   - una cresta con su PIV;
   - curvas con peralte, un cambio de superficie y un cambio de ancho.

   La cabecera del archivo, en comentarios, dice qué es cada parte.
4. **`02` §6.1:** el texto del formato 1, para el integrador, en tu fragmento `docs/progreso/tramo-eje.md`. Es la tabla de `lote-3.md` §2.3, con sus reglas.

Reutiliza `rmath`, `fnv1a.h`, el estilo de errores de `car_data` y, en los tests, `zone_axis.h` (`AxisBuilder`, `segment_point`) como referencia de rectas y arcos. Si escribes una utilidad nueva, el plan dice qué buscaste y por qué no sirve lo que hay.

## Criterios (tests en `tests/sim/`, Release y Debug)

1. **Rectas y arcos frente a `AxisBuilder`/`segment_point`.** La misma planta (por ejemplo, recta de 100, arco de 80 a R +60, arco de 50 a R −35, recta de 40) da posición ≤ 1·10⁻⁹ m y rumbo ≤ 1·10⁻¹² rad en 1000 puntos.
2. **Clotoide.**
   - Rumbo igual a la forma cerrada (≤ 1·10⁻¹²).
   - Posición frente a una integración de referencia del propio test (Simpson compuesto con 200 000 intervalos) ≤ 1 mm en dos casos: 300 m de `inf` a R +30, y 120 m de R −40 a R +40 (la curvatura pasa por 0).
3. **Juntas.** En cada junta del tramo de test, el salto entre `pose(s0 − 1e-9)` y `pose(s0)` es ≤ 1·10⁻⁶ m en posición y ≤ 1·10⁻⁸ rad en rumbo. La curvatura salta exactamente lo que dice el archivo.
4. **Círculo cerrado.** Cuatro arcos de 90° y R 50 vuelven al inicio con ≤ 1·10⁻⁹ m.
5. **Alzado.**
   - Pendientes +5 % y −7 % con L = 40 m en un PIV de altura h: la altura en el PIV es h − 0,6 m, que es h + (g₂ − g₁)·L/8.
   - La pendiente es continua en los extremos de la curva (≤ 1·10⁻¹²).
   - La curvatura vertical es −0,003 1/m dentro de la curva y 0 fuera.
6. **Peralte, anchos y superficies:** valores exactos en los puntos, interpolación lineal entre ellos y la superficie nueva justo en su `s`.
7. **Errores:** un caso por regla de `lote-3.md` §2.3, al menos 14, cada uno con su número de línea esperado:
   - `format` que falta;
   - `format = 2`;
   - clave, sección o primitiva desconocidas;
   - clave repetida;
   - número mal escrito;
   - L ≤ 0;
   - |R| < 5 m;
   - R = 0;
   - primer PIV fuera de s = 0;
   - curvas verticales solapadas;
   - `s` que no crece;
   - ancho ≤ 0;
   - superficie desconocida.
8. **Sin reservas:** 100 000 llamadas a `pose` dan 0 `operator new` (contador de `tests/common`).
9. **Coste:** 1 000 000 de llamadas a `pose` con `s` repartidas por el tramo de test dan ≤ 2 µs por llamada de media en Release. Lo informas también en Debug, sin límite.
10. **`kExpectedStageHash`:** FNV-1a de los campos de 10 000 poses equidistantes de `lazadas.stage`, igual en Debug y en Release. Lo creas tú y se actualiza como la huella.
11. **Lazadas:** entre dos puntos del eje con |Δs| > 50 m, la distancia mínima está entre 8 y 15 m. El test la mide.

**Sin cambios:** `kSimVersion`, la huella, las reservas de Jolt y el hash de la telemetría. No tocas `SimWorld`, `test_road`, `test_zone` ni nada de lo que mide la 2.6b.

## Regímenes

No hay física. Los casos que cubre el plan, cada uno con su test:
- `s` < 0 y `s` > longitud (acotadas);
- `s` justo en una junta;
- tramo de una sola primitiva;
- clotoide con curvatura que cambia de signo;
- PIV sin curva (L = 0, rasante quebrada);
- última línea de peralte, ancho o superficie antes del final.

## Para el informe

- Coste por llamada (Release y Debug) y memoria de un tramo de 5 km estimada con la de los nodos.
- Lo que el lote 4 (seguimiento) necesitará de `StageAxis` y no está: dilo, no lo hagas.
- Nada que probar jugando: no hay A/B.
