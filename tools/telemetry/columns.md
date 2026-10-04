# Columnas de la telemetría

Generado por `rally_replay columns --md`; no se edita a mano (un test comprueba que coincide). CSV del formato 2, escrito por `rally_replay run <grabación> --telemetry <salida.csv>`: una fila por paso de 1/240 s.

## Cabecera

Líneas `# clave=valor` antes de la fila de nombres: `format` (versión del CSV), `sim_version` (`kSimVersion`), `recording` (nombre de la grabación, sin ruta) y `blind_surface` (superficie de la pista ciega: `asphalt`, `gravel`, `dirt`, `snow`, `sand`, o `none` sin zona de pruebas).

## Convenciones

- Unidades SI: m, s, m/s, m/s², rad, rad/s. Los scripts muestran km/h y grados.
- Números con punto decimal y precisión fija por columna; un valor que redondea a 0 se escribe sin signo.
- Mundo: +y arriba. Coche: adelante = −z local, derecha = +x local, arriba = +y local.
- Posición y velocidad son las del centro de masas del coche (no coincide con el origen del cuerpo, que está en el suelo entre los ejes).
- Ruedas (sufijos `_fl`, `_fr`, `_rl`, `_rr`: delante izquierda, delante derecha, detrás izquierda, detrás derecha): ejes del contacto, longitudinal hacia delante de la rueda y lateral hacia su derecha. La carga es la de la suspensión del paso anterior; las fuerzas, la media de los 4 subpasos. En el aire, `contact` = 0, `surface` = none y lo demás a 0 salvo `omega` y `brake_torque`.
- Rumbo: 0 mirando a −z del mundo y crece girando a la derecha (como `psi` de la zona de pruebas).
- Guiñada > 0 girando a la derecha, como `SimInput::steer` > 0.
- β = atan2(v_right, v_forward): > 0 cuando la velocidad apunta a la derecha del morro. Girando a la derecha con el coche deslizando, la velocidad queda a la izquierda del morro (β < 0). Sin suavizar; a baja velocidad no está definido (los scripts lo ignoran por debajo de 3 m/s de velocidad horizontal).
- Aceleración: dv/dt del centro de masas en el paso, sin la gravedad (apoyado y quieto, 0). Lateral > 0 hacia la derecha: girando a la derecha es > 0. En el paso que recoloca, 0.
- Bits de `buttons` y `pressed`: 0 freno de mano, 1 subir marcha, 2 bajar marcha, 3 recolocar, 4 dirección digital, 5 acelerador digital, 6 freno digital, 7 reservado (era el salto de la caja).

## Columnas

| Columna | Unidad | Signo o rango | Descripción |
| --- | --- | --- | --- |
| `step` | — | — | Paso desde que se construyó el mundo (1 tras el primero) |
| `time` | s | — | step / 240 |
| `x` | m | — | Centro de masas del coche en el mundo |
| `y` | m | + arriba | Centro de masas en el mundo |
| `z` | m | — | Centro de masas en el mundo |
| `vx` | m/s | — | Velocidad del centro de masas, ejes del mundo |
| `vy` | m/s | + arriba | Velocidad del centro de masas, ejes del mundo |
| `vz` | m/s | — | Velocidad del centro de masas, ejes del mundo |
| `v_forward` | m/s | + hacia el morro | Velocidad del centro de masas, ejes del coche |
| `v_right` | m/s | + hacia la derecha | Velocidad del centro de masas, ejes del coche |
| `v_up` | m/s | + hacia el techo | Velocidad del centro de masas, ejes del coche |
| `speed` | m/s | ≥ 0 | Módulo de la velocidad del centro de masas |
| `heading` | rad | 0 mirando a −z; + girando a la derecha | Rumbo del morro, en (−π, π] |
| `yaw_rate` | rad/s | + girando a la derecha | Velocidad de guiñada (−ω · arriba del coche) |
| `beta` | rad | + velocidad a la derecha del morro | Ángulo de deriva atan2(v_right, v_forward), en (−π, π]; sin suavizar |
| `ax` | m/s² | — | Aceleración del centro de masas (dv/dt, sin la gravedad), ejes del mundo |
| `ay` | m/s² | + arriba | Aceleración del centro de masas, ejes del mundo |
| `az` | m/s² | — | Aceleración del centro de masas, ejes del mundo |
| `a_forward` | m/s² | + hacia el morro | Aceleración longitudinal (ejes del coche) |
| `a_right` | m/s² | + hacia la derecha | Aceleración lateral (ejes del coche) |
| `steer_target` | — | [−1, 1]; + derecha | Dirección objetivo (SimInput) |
| `throttle_target` | — | [0, 1] | Acelerador objetivo (SimInput) |
| `brake_target` | — | [0, 1] | Freno objetivo (SimInput) |
| `buttons` | — | máscara | Botones mantenidos (SimInput; bits abajo); el freno de mano objetivo es el bit 0 |
| `steer` | — | [−1, 1]; + derecha | Dirección aplicada (ControlState) |
| `throttle` | — | [0, 1] | Acelerador aplicado |
| `brake` | — | [0, 1] | Freno aplicado |
| `handbrake` | — | [0, 1] | Freno de mano aplicado |
| `pressed` | — | máscara | Botones que pasan de 0 a 1 en el paso (bits abajo) |
| `surface` | — | texto | Superficie bajo el centro de masas (área de la zona); none sin zona |
| `laps` | — | ≥ 0 | Vueltas completas a la pista ciega |
| `lap_time` | s | −1 sin vuelta | Tiempo de la vuelta en curso |
| `impact` | m/s | ≥ 0 | Cambio de velocidad del paso que no explican la gravedad ni las fuerzas propias: suspensión y contactos de la carrocería (SimWorld::last_impact) |
| `gear` | — | −1 R, 0 N, 1 adelante | Selector provisional (tarea 2.6 lo sustituye) |
| `contact_fl` | — | 0 o 1 | Rueda en el suelo |
| `surface_fl` | — | texto | Superficie del material del contacto (la que usa el neumático); none en el aire |
| `load_fl` | N | ≥ 0 | Carga normal (suspensión, barra y tope del paso anterior) |
| `compression_fl` | m | ≥ 0 | Compresión de la suspensión desde la extensión máxima |
| `kappa_fl` | — | + la rueda gira más rápido que el suelo | Deslizamiento longitudinal (ω·r − vx) / max(|vx|, min_slip_speed) |
| `alpha_fl` | rad | + la rueda va hacia su derecha | Ángulo de deriva atan2(vy, max(|vx|, min_slip_speed)) |
| `fx_fl` | N | + hacia delante de la rueda | Fuerza longitudinal del neumático (media de los subpasos) |
| `fy_fl` | N | + hacia la derecha de la rueda | Fuerza lateral del neumático (media de los subpasos) |
| `omega_fl` | rad/s | + rodando hacia delante | Giro de la rueda al final del paso |
| `brake_torque_fl` | N·m | ≥ 0 | Par de freno pedido (freno y freno de mano) |
| `h_fl` | m | + arriba | Media del microrrelieve h sobre la huella |
| `contact_fr` | — | 0 o 1 | Rueda en el suelo |
| `surface_fr` | — | texto | Superficie del material del contacto (la que usa el neumático); none en el aire |
| `load_fr` | N | ≥ 0 | Carga normal (suspensión, barra y tope del paso anterior) |
| `compression_fr` | m | ≥ 0 | Compresión de la suspensión desde la extensión máxima |
| `kappa_fr` | — | + la rueda gira más rápido que el suelo | Deslizamiento longitudinal (ω·r − vx) / max(|vx|, min_slip_speed) |
| `alpha_fr` | rad | + la rueda va hacia su derecha | Ángulo de deriva atan2(vy, max(|vx|, min_slip_speed)) |
| `fx_fr` | N | + hacia delante de la rueda | Fuerza longitudinal del neumático (media de los subpasos) |
| `fy_fr` | N | + hacia la derecha de la rueda | Fuerza lateral del neumático (media de los subpasos) |
| `omega_fr` | rad/s | + rodando hacia delante | Giro de la rueda al final del paso |
| `brake_torque_fr` | N·m | ≥ 0 | Par de freno pedido (freno y freno de mano) |
| `h_fr` | m | + arriba | Media del microrrelieve h sobre la huella |
| `contact_rl` | — | 0 o 1 | Rueda en el suelo |
| `surface_rl` | — | texto | Superficie del material del contacto (la que usa el neumático); none en el aire |
| `load_rl` | N | ≥ 0 | Carga normal (suspensión, barra y tope del paso anterior) |
| `compression_rl` | m | ≥ 0 | Compresión de la suspensión desde la extensión máxima |
| `kappa_rl` | — | + la rueda gira más rápido que el suelo | Deslizamiento longitudinal (ω·r − vx) / max(|vx|, min_slip_speed) |
| `alpha_rl` | rad | + la rueda va hacia su derecha | Ángulo de deriva atan2(vy, max(|vx|, min_slip_speed)) |
| `fx_rl` | N | + hacia delante de la rueda | Fuerza longitudinal del neumático (media de los subpasos) |
| `fy_rl` | N | + hacia la derecha de la rueda | Fuerza lateral del neumático (media de los subpasos) |
| `omega_rl` | rad/s | + rodando hacia delante | Giro de la rueda al final del paso |
| `brake_torque_rl` | N·m | ≥ 0 | Par de freno pedido (freno y freno de mano) |
| `h_rl` | m | + arriba | Media del microrrelieve h sobre la huella |
| `contact_rr` | — | 0 o 1 | Rueda en el suelo |
| `surface_rr` | — | texto | Superficie del material del contacto (la que usa el neumático); none en el aire |
| `load_rr` | N | ≥ 0 | Carga normal (suspensión, barra y tope del paso anterior) |
| `compression_rr` | m | ≥ 0 | Compresión de la suspensión desde la extensión máxima |
| `kappa_rr` | — | + la rueda gira más rápido que el suelo | Deslizamiento longitudinal (ω·r − vx) / max(|vx|, min_slip_speed) |
| `alpha_rr` | rad | + la rueda va hacia su derecha | Ángulo de deriva atan2(vy, max(|vx|, min_slip_speed)) |
| `fx_rr` | N | + hacia delante de la rueda | Fuerza longitudinal del neumático (media de los subpasos) |
| `fy_rr` | N | + hacia la derecha de la rueda | Fuerza lateral del neumático (media de los subpasos) |
| `omega_rr` | rad/s | + rodando hacia delante | Giro de la rueda al final del paso |
| `brake_torque_rr` | N·m | ≥ 0 | Par de freno pedido (freno y freno de mano) |
| `h_rr` | m | + arriba | Media del microrrelieve h sobre la huella |
