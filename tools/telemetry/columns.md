# Columnas de la telemetría

Generado por `rally_replay columns --md`; no se edita a mano (un test comprueba que coincide). CSV del formato 2, escrito por `rally_replay run <grabación> --telemetry <salida.csv>`: una fila por paso de 1/240 s.

## Cabecera

Líneas `# clave=valor` antes de la fila de nombres: `format` (versión del CSV), `sim_version` (`kSimVersion`), `recording` (nombre de la grabación, sin ruta) y `blind_surface` (superficie de la pista ciega: `asphalt`, `gravel`, `dirt`, `snow`, `sand`, o `none` sin zona de pruebas) y `road_surface` (calzada de la carretera de prueba: `gravel` o `asphalt`, o `none` sin zona).

## Convenciones

- Unidades SI: m, s, m/s, m/s², rad, rad/s. Los scripts muestran km/h y grados.
- Números con punto decimal y precisión fija por columna; un valor que redondea a 0 se escribe sin signo.
- Mundo: +y arriba. Coche: adelante = −z local, derecha = +x local, arriba = +y local.
- Posición y velocidad son las del centro de masas del coche (no coincide con el origen del cuerpo, que está en el suelo entre los ejes).
- Ruedas (sufijos `_fl`, `_fr`, `_rl`, `_rr`: delante izquierda, delante derecha, detrás izquierda, detrás derecha): ejes del contacto, longitudinal hacia delante de la rueda y lateral hacia su derecha. La carga es la de la suspensión del paso anterior; las fuerzas, la media de los 4 subpasos. κ y α son los relajados, de la deformación de la huella (u, w). En el aire, `contact` = 0, `surface` = none y lo demás a 0 salvo `omega`, `slip_x` y `brake_torque`.
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
| `steer_aided` | — | [−1, 1]; + derecha | Dirección objetivo tras el recorte de la ayuda: la que sigue el limitador de las manos |
| `steer_digital` | — | 0 o 1 | Modo del eje de dirección: 1 digital (teclado), 0 analógico (stick o ratón); el dispositivo no es de la simulación |
| `assist_active` | — | 0 o 1 | La ayuda de dirección calcula su ventana en el paso (activada, ≥ 15 km/h, hacia delante y con alguna delantera cargada) |
| `assist_rear_sliding` | — | 0 o 1 | La trasera pasa de su pico: el lado hacia el que se va es contravolante y no recorta |
| `assist_right_max` | — | [0, 1] | Tope de la dirección hacia la derecha que deja la ayuda (1: sin recorte) |
| `assist_left_max` | — | [0, 1] | Tope de la dirección hacia la izquierda (magnitud; 1: sin recorte) |
| `steer_speed` | m/s | ≥ 0 | Velocidad horizontal del centro del eje delantero: la de la ayuda y la de las velocidades de giro |
| `throttle` | — | [0, 1] | Acelerador aplicado |
| `brake` | — | [0, 1] | Freno aplicado |
| `handbrake` | — | [0, 1] | Freno de mano aplicado |
| `pressed` | — | máscara | Botones que pasan de 0 a 1 en el paso (bits abajo) |
| `surface` | — | texto | Superficie bajo el centro de masas (área de la zona); none sin zona |
| `laps` | — | ≥ 0 | Vueltas completas a la pista ciega |
| `lap_time` | s | −1 sin vuelta | Tiempo de la vuelta en curso |
| `road_time` | s | −1 sin pasada | Tiempo de la pasada en curso por la carretera de prueba (de la salida a la meta) |
| `road_resets` | — | ≥ 0 | Recolocaciones en la pasada en curso por la carretera |
| `road_runs` | — | ≥ 0 | Pasadas completas por la carretera |
| `impact` | m/s | ≥ 0 | Cambio de velocidad del paso que no explican la gravedad ni las fuerzas propias: suspensión y contactos de la carrocería (SimWorld::last_impact) |
| `gear` | — | −1 R, 0 N, 1..5 | Marcha engranada |
| `target_gear` | — | −1 R, 0 N, 1..5 | Marcha del cambio en curso (la engranada si no hay cambio) |
| `gearbox` | — | 0 automático, 1 secuencial | Modo de la caja |
| `shifting` | — | 0/1 | Cambio en curso: corte de par y embrague abierto |
| `clutch_mode` | — | 0 arranque, 1 cerrado | Modo del embrague automático |
| `clutch_open` | — | 0/1 | Embrague abierto por el cambio, el freno de mano (D3) o sin marcha |
| `rpm` | rpm | ≥ 0 | Giro del motor |
| `boost` | bar | [0, máx.] | Presión del turbo sobre la atmosférica |
| `engine_throttle` | — | [0, 1] | Acelerador efectivo del motor: el del piloto, el del regulador de ralentí o el del cambio |
| `engine_torque` | N·m | — | Par neto del motor (combustión menos rozamiento), media de los subpasos |
| `clutch_slip` | rad/s | — | Deslizamiento del embrague ω_motor − ω_caja (0 sin marcha) |
| `clutch_torque` | N·m | — | Par que pasa el embrague al tren (> 0 empujando el coche) |
| `axle_torque_front` | N·m | — | Par de entrada al eje delantero (caja y viscoacoplador) |
| `axle_torque_rear` | N·m | — | Par de entrada al eje trasero (caja y viscoacoplador) |
| `center_slip` | rad/s | — | Δω del central: giro medio delantero − trasero |
| `viscous_torque` | N·m | — | Par del viscoacoplador, del eje delantero al trasero (+) |
| `lsd_torque_front` | N·m | ≥ 0 | Par de bloqueo del autoblocante delantero |
| `lsd_torque_rear` | N·m | ≥ 0 | Par de bloqueo del autoblocante trasero |
| `drift_hold` | — | 0/1 | El automático está retrasando la subida en derrape |
| `lcp_iterations` | — | 1..8 | Iteraciones del LCP del tren motriz: máximo de los subpasos del paso |
| `lcp_failures` | — | 0..4 | Subpasos del paso en que el LCP no convergió (impulsos recortados a su caja) |
| `traction_torque_cut` | N·m | ≥ 0 | Par motor que quita el control de tracción (media de los subpasos); 0 sin control |
| `contact_fl` | — | 0 o 1 | Rueda en el suelo |
| `surface_fl` | — | texto | Superficie del material del contacto (la que usa el neumático); none en el aire |
| `load_fl` | N | ≥ 0 | Carga normal (suspensión, barra y tope del paso anterior) |
| `compression_fl` | m | ≥ 0 | Compresión de la suspensión desde la extensión máxima |
| `kappa_fl` | — | + tracción | Deslizamiento relajado κ_r = u/σ_x (deformación longitudinal de la huella) |
| `alpha_fl` | rad | − la rueda va hacia su derecha | Ángulo de deriva relajado α_r = atan(w/σ_y) (deformación lateral de la huella) |
| `slip_x_fl` | m/s | + la rueda gira más rápido que el suelo | Velocidad de deslizamiento longitudinal ω·r − v_x |
| `slip_y_fl` | m/s | + la rueda va hacia su izquierda | Velocidad de deslizamiento lateral −v_y |
| `fx_fl` | N | + hacia delante de la rueda | Fuerza longitudinal del neumático (media de los subpasos) |
| `fy_fl` | N | + hacia la derecha de la rueda | Fuerza lateral del neumático (media de los subpasos) |
| `fx_pure_fl` | N | + hacia delante de la rueda | Fuerza longitudinal que daría κ_r solo, sin la combinada (media de los subpasos) |
| `fy_pure_fl` | N | + hacia la derecha de la rueda | Fuerza lateral que daría α_r solo, sin la combinada (media de los subpasos) |
| `mu_fl` | — | ≥ 0 | μ efectivo: pico de la superficie con la sensibilidad a la carga, μ(F_z); 0 en el aire |
| `omega_fl` | rad/s | + rodando hacia delante | Giro de la rueda al final del paso |
| `brake_torque_fl` | N·m | ≥ 0 | Par de freno pedido (freno y freno de mano) |
| `h_fl` | m | + arriba | Media del microrrelieve h sobre la huella |
| `abs_torque_cut_fl` | N·m | ≥ 0 | Par del freno de pie que quita el ABS (media de los subpasos); 0 sin ABS |
| `contact_fr` | — | 0 o 1 | Rueda en el suelo |
| `surface_fr` | — | texto | Superficie del material del contacto (la que usa el neumático); none en el aire |
| `load_fr` | N | ≥ 0 | Carga normal (suspensión, barra y tope del paso anterior) |
| `compression_fr` | m | ≥ 0 | Compresión de la suspensión desde la extensión máxima |
| `kappa_fr` | — | + tracción | Deslizamiento relajado κ_r = u/σ_x (deformación longitudinal de la huella) |
| `alpha_fr` | rad | − la rueda va hacia su derecha | Ángulo de deriva relajado α_r = atan(w/σ_y) (deformación lateral de la huella) |
| `slip_x_fr` | m/s | + la rueda gira más rápido que el suelo | Velocidad de deslizamiento longitudinal ω·r − v_x |
| `slip_y_fr` | m/s | + la rueda va hacia su izquierda | Velocidad de deslizamiento lateral −v_y |
| `fx_fr` | N | + hacia delante de la rueda | Fuerza longitudinal del neumático (media de los subpasos) |
| `fy_fr` | N | + hacia la derecha de la rueda | Fuerza lateral del neumático (media de los subpasos) |
| `fx_pure_fr` | N | + hacia delante de la rueda | Fuerza longitudinal que daría κ_r solo, sin la combinada (media de los subpasos) |
| `fy_pure_fr` | N | + hacia la derecha de la rueda | Fuerza lateral que daría α_r solo, sin la combinada (media de los subpasos) |
| `mu_fr` | — | ≥ 0 | μ efectivo: pico de la superficie con la sensibilidad a la carga, μ(F_z); 0 en el aire |
| `omega_fr` | rad/s | + rodando hacia delante | Giro de la rueda al final del paso |
| `brake_torque_fr` | N·m | ≥ 0 | Par de freno pedido (freno y freno de mano) |
| `h_fr` | m | + arriba | Media del microrrelieve h sobre la huella |
| `abs_torque_cut_fr` | N·m | ≥ 0 | Par del freno de pie que quita el ABS (media de los subpasos); 0 sin ABS |
| `contact_rl` | — | 0 o 1 | Rueda en el suelo |
| `surface_rl` | — | texto | Superficie del material del contacto (la que usa el neumático); none en el aire |
| `load_rl` | N | ≥ 0 | Carga normal (suspensión, barra y tope del paso anterior) |
| `compression_rl` | m | ≥ 0 | Compresión de la suspensión desde la extensión máxima |
| `kappa_rl` | — | + tracción | Deslizamiento relajado κ_r = u/σ_x (deformación longitudinal de la huella) |
| `alpha_rl` | rad | − la rueda va hacia su derecha | Ángulo de deriva relajado α_r = atan(w/σ_y) (deformación lateral de la huella) |
| `slip_x_rl` | m/s | + la rueda gira más rápido que el suelo | Velocidad de deslizamiento longitudinal ω·r − v_x |
| `slip_y_rl` | m/s | + la rueda va hacia su izquierda | Velocidad de deslizamiento lateral −v_y |
| `fx_rl` | N | + hacia delante de la rueda | Fuerza longitudinal del neumático (media de los subpasos) |
| `fy_rl` | N | + hacia la derecha de la rueda | Fuerza lateral del neumático (media de los subpasos) |
| `fx_pure_rl` | N | + hacia delante de la rueda | Fuerza longitudinal que daría κ_r solo, sin la combinada (media de los subpasos) |
| `fy_pure_rl` | N | + hacia la derecha de la rueda | Fuerza lateral que daría α_r solo, sin la combinada (media de los subpasos) |
| `mu_rl` | — | ≥ 0 | μ efectivo: pico de la superficie con la sensibilidad a la carga, μ(F_z); 0 en el aire |
| `omega_rl` | rad/s | + rodando hacia delante | Giro de la rueda al final del paso |
| `brake_torque_rl` | N·m | ≥ 0 | Par de freno pedido (freno y freno de mano) |
| `h_rl` | m | + arriba | Media del microrrelieve h sobre la huella |
| `abs_torque_cut_rl` | N·m | ≥ 0 | Par del freno de pie que quita el ABS (media de los subpasos); 0 sin ABS |
| `contact_rr` | — | 0 o 1 | Rueda en el suelo |
| `surface_rr` | — | texto | Superficie del material del contacto (la que usa el neumático); none en el aire |
| `load_rr` | N | ≥ 0 | Carga normal (suspensión, barra y tope del paso anterior) |
| `compression_rr` | m | ≥ 0 | Compresión de la suspensión desde la extensión máxima |
| `kappa_rr` | — | + tracción | Deslizamiento relajado κ_r = u/σ_x (deformación longitudinal de la huella) |
| `alpha_rr` | rad | − la rueda va hacia su derecha | Ángulo de deriva relajado α_r = atan(w/σ_y) (deformación lateral de la huella) |
| `slip_x_rr` | m/s | + la rueda gira más rápido que el suelo | Velocidad de deslizamiento longitudinal ω·r − v_x |
| `slip_y_rr` | m/s | + la rueda va hacia su izquierda | Velocidad de deslizamiento lateral −v_y |
| `fx_rr` | N | + hacia delante de la rueda | Fuerza longitudinal del neumático (media de los subpasos) |
| `fy_rr` | N | + hacia la derecha de la rueda | Fuerza lateral del neumático (media de los subpasos) |
| `fx_pure_rr` | N | + hacia delante de la rueda | Fuerza longitudinal que daría κ_r solo, sin la combinada (media de los subpasos) |
| `fy_pure_rr` | N | + hacia la derecha de la rueda | Fuerza lateral que daría α_r solo, sin la combinada (media de los subpasos) |
| `mu_rr` | — | ≥ 0 | μ efectivo: pico de la superficie con la sensibilidad a la carga, μ(F_z); 0 en el aire |
| `omega_rr` | rad/s | + rodando hacia delante | Giro de la rueda al final del paso |
| `brake_torque_rr` | N·m | ≥ 0 | Par de freno pedido (freno y freno de mano) |
| `h_rr` | m | + arriba | Media del microrrelieve h sobre la huella |
| `abs_torque_cut_rr` | N·m | ≥ 0 | Par del freno de pie que quita el ABS (media de los subpasos); 0 sin ABS |
