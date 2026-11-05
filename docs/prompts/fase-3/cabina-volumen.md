# Tarea `cabina-volumen` (lote 5, carril plataforma): el ojo nunca a menos de 8 cm de la cabina

Reglas comunes: `docs/prompts/fase-2/comun.md`; su §13 vale dentro del lote. Contratos: `mensajes/lote-5.md` §2.7, que manda sobre este prompt.

## Por qué

Hallazgo del lote 4 (`mirar-cabeza`): con el asiento 15 cm a la izquierda, el cuello a 90° y F9 al límite, el ojo queda a 0,02 m de la puerta, y a 0,01 m del techo. Lucas decidió «un volumen seguro de cabina por coche, metido 8 cm hacia dentro, al que se ciñe el ojo».

## Qué hacer

1. **Volumen** por coche, en ejes del coche, con las medidas de `[cockpit]` (relativas al ojo nominal; `platform/cockpit.h`, `cockpit_point`), sin claves nuevas en los `.car`:
   - la cara interior de la puerta de su lado (`door_left` para el conductor);
   - el techo (`roof_above`) y su final por detrás (`roof_behind`);
   - el plano del parabrisas, por su base (`windshield_base_ahead`, `windshield_base_below`) y su borde superior (`windshield_top_ahead`, `windshield_top_above`);
   - cada uno, 0,08 m hacia dentro. Por abajo y hacia el centro del coche, sin límite (el asiento ya lo limita).
   - El del copiloto, el del conductor reflejado en x, como su ojo (`copiloto-asiento`).
2. **El ojo final** (asiento + cuello de `head_view` + F9) se lleva al punto más cercano del volumen, en el conductor y en el copiloto. Si ya está dentro, no cambia.
3. Función pura en `rally_loop` (sin raylib), con su test.

## Criterios (tests en `tests/platform/`, Release y Debug, sin ventana)

1. **Holgura:** en los cuatro `.car`, con el asiento en las 27 combinaciones de ±15 / 0 cm, el cuello a −90°, 0 y +90° de guiñada y +15° / −25° de cabeceo, y F9 en los topes de cada eje (`platform/head_motion.h`), con los dos signos: distancia del ojo a cada plano ≥ 0,08 m − 1·10⁻⁶ m. Escribe la menor.
2. **Dentro, igual bit a bit:** con el ojo nominal (asiento 0, mirando al frente, F9 quieto) y en 1000 posiciones dentro del volumen (PCG32), el ojo sale igual bit a bit.
3. **Continuo:** moviendo el ojo 1 mm por paso desde dentro hacia la puerta y el techo, el ojo final nunca salta más de 1 mm entre pasos.
4. El test de hoy de la holgura con la puerta (0,17 m ≥ 0,05 m) sigue pasando.

**Sin cambios:** nada de `src/sim` ni de los `.car`; las capturas de referencia (el ojo nominal está dentro: `golden.ps1` igual; si cambia alguna, parada).

## Premisas

| Premisa | Cómo se comprueba | Si es falsa |
| --- | --- | --- |
| El ojo nominal de los cuatro coches está dentro del volumen con 8 cm | Criterio 2 | Parada, con la cifra de cada coche: algún `[cockpit]` estaría mal (tres son «copiado, sin verificar») |

## Paradas

Solo: la premisa falsa, o una captura de referencia que cambia.

## Para el informe

- La holgura mínima por coche y por plano, y en qué combinación.
- **A ojo, en la prueba de Lucas:** asiento a tope a la izquierda y arriba, cuello a 90° y F9: la cámara no se acerca a la puerta ni al techo y no salta.
