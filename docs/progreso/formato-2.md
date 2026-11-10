# Fragmento de `formato-2` (lote 6, carril sim)

Estado: **parada prevista tras la parte A** («Revisión del consultor tras la parte A»). Informe: `mensajes/formato-2-informe.md` de la copia de trabajo de la tarea. La parte B (la banda como suelo del juego, la rejilla en el borde de la banda, los criterios 1–10, los hashes y `stage_lazadas.rrec`) espera la respuesta del consultor; este fragmento se completa entonces.

## Fila del registro (provisional, parte A)

| Fecha | Fase | Horas | Qué se hizo | Siguiente paso |
| --- | --- | --- | --- | --- |
| 2026-11-10 | 3 | | Tarea `formato-2` (lote 6), **parte A, parada prevista**. Medido el formato 1 por las salidas guionizadas antes de cambiarlo (criterio 11, `rally_stage_exits`). Prototipo de la banda de entorno como variante de prueba (`StageGroundKind::Band`; el suelo del juego sigue siendo el formato 1): cuneta en coseno de 1,5 × 0,25 m sobre el plano del arcén (18 tramos), eje medio con la otra parte del tramo (`StageTrack::other_feet`), talud o muro recto de cabeza a cabeza, explanada de 5 m sin pasada enfrentada, rampa continua de 10 a 15 m y abanico al centro en los arcos cerrados; secciones añadidas donde el final de la banda se aparta de su cuerda. Premisas: P1 77,92° en `lazadas` (s 657,75 frente a 830,88); P2 sin triángulos invertidos ni sin área en los seis tramos de test; P3 ninguna cara de más de 80° ni de la banda sobre el núcleo; P4 paso ×1,00 en la media y ×1,33 en el p99 frente a la carretera de la 2.6b en `lazadas`; P5 y P6, en el informe. Tests: 661 → 667 en Release (6 nuevos de `stage_band`); 644/644 en Debug. Huella `0x66c9e3a86632750e`, reservas 1990, telemetría `0xfe3c612ddcaa6805`, `kSimVersion` 5, `kExpectedStageHash`, `kExpectedStageGroundHash` y `kExpectedStageStateHash`: sin cambios en la parte A. Sin validar a ojo. | Respuesta del consultor a la parte A (`mensajes/lote-6-respuesta-formato-2.md`) |

## Hallazgos (parte A)

| Fecha | Hallazgo | Cifra | Qué se hace |
| --- | --- | --- | --- |
| 2026-11-10 | El muro entre los brazos de `lazadas` cabe en 80° por poco | 77,92° en s 657,75 (brazo de entrada) frente a s 830,88 (central) | Premisa P1 cumplida; el margen es de 2,1° |
| 2026-11-10 | Con el terreno del formato 1, una pasada aislada no tiene desmonte ni terraplén: el terreno natural está a la altura de su borde | Talud de altura 0 en 1537 de 2374 lados de `lazadas` | Pregunta al consultor (informe) |
| 2026-11-10 | La carga del suelo con la banda es lenta | `lazadas` 0,97 s (formato 1: 0,13 s) | Aviso; se optimiza en la parte B |

## Ediciones propuestas de `docs/02`, `07` y `08`

Se escriben en la parte B, con el diseño que salga de la revisión del consultor.

## Lo que pido de lo compartido

Nada en la parte A.
