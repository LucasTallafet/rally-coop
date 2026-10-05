# Prompts de la fase 2 · Coche

Un prompt por tarea para Claude Code. En la consola solo se pega una línea:

```
Lee docs/prompts/fase-2/2.X.md y aplícalo. Plan corto primero.
```

La 2.10 tiene tres modos y su propia línea (ver `2.10.md`). Cada prompt da por leído `comun.md`, que tiene las reglas de todas las tareas: plan, reproducibilidad, huella, parada, cierre y resumen.

## Archivos

| Archivo | Qué es |
| --- | --- |
| `comun.md` | Reglas comunes a todas las tareas |
| `adenda-diseno.md` | Revisión del diseño de la fase 2: decisiones D1–D7 que confirma Lucas y cambios exactos para `docs/` |
| `2.0.md` | Aplicar el diseño y la adenda a los documentos (sin código) |
| `2.0b.md` | Correcciones tras la 2.0: arena en la fase 2, manejo realista, modo difícil y cifras (sin código) |
| `2.1.md` … `2.11.md` | Una tarea cada uno |
| `../../manejo/protocolo.md` | Protocolo de pruebas de manejo para Lucas y su compañero (tarea 2.10) |
| `../../manejo/plantilla-hoja.csv` | Cabecera de la hoja de cada sesión |

## Paso 0 (antes de la 2.1, sin Claude Code)

1. Marca en la sección 0 de `adenda-diseno.md` las decisiones D1–D7 (vienen marcadas las recomendadas).
2. Tarea 2.0.
3. Prueba los mandos de Xbox, PS5 y Switch Pro con la demo actual y Steam cerrado (pendiente de la fase 1). El Switch Pro es el importante: sus gatillos son digitales y desde la 2.1 el freno va en el gatillo izquierdo. Apunta el resultado en `progreso.md`.

## Orden y dependencias

| Tarea | Depende de | Módulos | Parada | Horas |
| --- | --- | --- | --- | --- |
| 2.0 Documentos | — | `docs` | Solo si D1–D7 no están marcadas | — |
| 2.0b Correcciones de documentos | 2.0 | `docs` | No | — |
| 2.1 Entrada, grabación y `rally_replay` | 2.0b, paso 0 | sim, platform, io, tools | Sí | 3–4 |
| 2.2 Zona de pruebas | 2.1 | sim, platform, tools | Sí | 4–6 |
| 2.3 Telemetría | 2.1, 2.2 | sim (lectura), io, platform, tools, Python | Sí (corta) | 4–6 |
| 2.4 Vehículo base | 2.1, 2.2, 2.3 | sim, platform, data, tools | Sí | 7–11 |
| 2.4b Pantalla y asiento | 2.4 | platform, docs | Sí (corta) | 2–3 |
| 2.5 Neumático | 2.4 | sim, data, tools | Sí | 8–13 |
| 2.5b Dirección: ayuda, teclado y ratón | 2.5 | sim, platform | Sí | 3–4 |
| 2.6 Motor y transmisión | 2.5b | sim, data, platform | Sí | 6–10 |
| 2.6b Carretera de prueba | 2.6 | sim (zona), platform, tools | Sí (corta) | 3–5 |
| 2.7 Conductor en el coche | 2.6b | platform, data | Sí (larga) | 5–8 |
| 2.8 Ayudas y ajustes | 2.6b | sim, platform, data | Sí | 3–4 |
| 2.9 Máquina de acciones | 2.1 | game, io, platform | Sí (corta) | 3–4 |
| 2.10 Ajuste y pruebas de manejo | 2.1–2.8 | data, docs, tools | En cada iteración | 15–28 |
| 2.11 Cierre | Todas | docs | Sí | 2–3 |
| **Total** | | | | **67–109** (la fase: 67–114, con margen) |

```
2.0 ─► 2.1 ─► 2.2 ─► 2.3 ─► 2.4 ─► 2.4b ─► 2.5 ─► 2.5b ─► 2.6 ─► 2.6b ─┬─► 2.7 ─┬─► 2.10 ─► 2.11
        │                                                             └─► 2.8 ─┘             ▲
        └─► 2.9 (en cualquier momento) ──────────────────────────────────────────────────────┘
```

## Qué puede ir en paralelo

- **2.9** no depende del coche: puede hacerse en cualquier momento después de la 2.1, en otra sesión y otra copia de trabajo. Es lo único que va bien en paralelo con el camino principal.
- **2.7 y 2.8**, después de la 2.6. La 2.7 no cambia la simulación y la 2.8 sí; tocan los dos `main.cpp` y la demo, así que habrá algún conflicto al fusionar. Se fusiona primero la que se valide antes, y la otra se pone al día con `master` antes de su parada.
- **Nunca en paralelo:** dos tareas que cambien `kSimVersion` (2.1, 2.4, 2.5, 2.5b, 2.6, 2.8), porque las dos tocan la huella. La 2.6b cambia el hash de la zona y regenera las grabaciones de test: no va en paralelo con otra tarea que las regenere.
- Paralelo solo si tienes tiempo de validar las dos: lo que limita la fase no es el código, sino tus pruebas.

## Puntos de parada humanos

| Tarea | Qué compruebas tú |
| --- | --- |
| 2.1 | Acelerador y freno separados, botones y teclas; 1 minuto jugado y `rally_replay` da "hash OK" |
| 2.2 | La zona se entiende y las medidas convencen; modo a ciegas en el dibujo; vuelta cronometrada |
| 2.3 | Gráficas en pantalla; CSV, `plot.py` y `drift_check.py` funcionan |
| 2.4 | Suspensión, baches sin golpes falsos, parado en la rampa, recolocar y F5 (el manejo aún no se valora) |
| 2.4b | La pantalla se lee de un vistazo; H explica todas las teclas; el panel de depuración tiene lo que hacía falta; el ojo ajustado se conserva al reabrir |
| 2.5 | Primer derrape en grava y superficies distintas (orientativo) |
| 2.5b | Con teclado y con ratón, la zona a 100 km/h sin volantazos hasta la horquilla; ayuda activada y desactivada en el círculo y la explanada (contravolante completo, sin sensación de que el coche corrige solo) |
| 2.6 | Retardo del turbo, cambios, freno de mano en la horquilla, primera vuelta con cada configuración |
| 2.6b | Recorrer la carretera en grava y en asfalto: se entiende como una carretera estrecha de rally, la curva tras la cresta no se ve, salirse cuesta; 3 pasadas con road_check.py |
| 2.7 | Vista desde el asiento, cabeza (mareo), sonido, vibración, modo a ciegas completo |
| 2.8 | Cada ajuste se nota; ABS frenando y girando |
| 2.9 | Anillo de progreso con las teclas de depuración |
| 2.10 | Sesiones del protocolo; apruebas cada iteración de cambios |
| 2.11 | Tabla de cierre, decisiones y estimación nueva; das la fase por terminada |

## Si una tarea no pasa su criterio

1. **Claude Code para.** No relaja umbrales, no cambia valores esperados, no desactiva tests y no fusiona. Te da el valor medido, el esperado, lo que ha probado y la pregunta exacta del prompt.
2. **Si la respuesta es tuya** (gusto, prioridad, rango): contesta en la misma sesión.
3. **Si cambia el diseño** (una decisión provisional, un umbral de `04`, el modelo físico): lleva la pregunta y las cifras al proyecto de claude.ai. De ahí salen los cambios exactos para `docs/` y la fila de Cambios de `05`; luego vuelves a la sesión con la respuesta.
4. **Si la tarea crece** más de un 50 % sobre sus horas: se parte. Lo que funciona se cierra y fusiona, y lo que falta pasa a una tarea `2.Xb` con su propio prompt (cópialo del original y recorta el alcance).
5. **Si la prueba de derrape no pasa en la 2.10** (3 iteraciones o más de 28 h): la pregunta de reapertura de "Física del coche" está en `2.10.md`. Se decide en el proyecto de claude.ai, no en Claude Code.
6. Nada sin fusionar se pierde: la copia de trabajo y la rama se quedan hasta que decidas.

## Riesgos principales por tarea

La pregunta exacta de cada uno está en su prompt.

| Tarea | Riesgo principal |
| --- | --- |
| 2.1 | La partida y la repetición se separan (bloqueante); freno todo o nada con el Switch Pro |
| 2.2 | Memoria o tiempo de construcción del suelo; coste de dibujar el microrrelieve |
| 2.3 | matplotlib no aprobada; una definición de la prueba de derrape que no se puede implementar sin interpretar |
| 2.4 | La rueda oscila a baja velocidad; el coche repta en la rampa; golpes falsos en bordes de triángulos |
| 2.5 | Grava y tierra iguales; trompos con la tracción provisional |
| 2.6 | El tren motriz oscila; el freno de mano no gira el coche; turbo frustrante |
| 2.6b | La carretera no cabe en la zona; bordes de material en escalera o cuneta con golpes falsos por la rejilla de 0,75 m |
| 2.7 | La cabeza marea; cortes de audio |
| 2.8 | La ayuda de dirección contravolantea sola; ABS en grava |
| 2.9 | Ampliar el formato de grabación rompe las grabaciones de test |
| 2.10 | La prueba de derrape no pasa → reapertura de "Física del coche"; superficies que no se distinguen; configuración elegida distinta del 205 |
| 2.11 | El paso pasa de 0,5 ms; criterio cumplido con reservas |

## Seguimiento

- Cada tarea deja su fila en `docs/progreso.md` con la columna de horas vacía: la rellenas tú.
- Al cerrar cada tarea, compara las horas acumuladas con las 61–109 de la fase. Si al terminar la 2.6 llevas más de 50 h, la 2.10 no cabe en el rango: llévalo al proyecto de diseño antes de empezar las pruebas.
- Después de cada tarea que cambie `docs/`, vuelve a subir esos documentos al proyecto de claude.ai.
