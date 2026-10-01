# Rally Coop

Juego de rally cooperativo en 3D low-poly para 2 jugadores en 2 PCs: uno conduce en primera persona y el otro canta las curvas desde una tablet. Motor propio en C++ con raylib, Jolt Physics y ENet, para Windows.

## Contenido de este kit

| Archivo | Para qué |
| --- | --- |
| `docs/01-diseno-de-juego.md` | Cómo se juega: roles, tablet, notas, puntuación, mecánicas, lo que llega después de la 1.0 |
| `docs/02-arquitectura.md` | Cómo se construye: módulos, física, seguimiento del tramo, generador, red, render, audio |
| `docs/03-requisitos.md` | Requisitos mínimos, presupuestos de rendimiento y requisitos de la 1.0 |
| `docs/04-plan-de-fases.md` | Fases 0 a 6 de la 1.0 con horas y criterios, y fases posteriores |
| `docs/05-decisiones.md` | Registro de decisiones cerradas y provisionales, con historial de cambios |
| `docs/progreso.md` | Horas reales por fase, registro de sesiones y hallazgos |
| `docs/06-guia-claude-code.md` | Instalar, montar el repositorio y primeras tareas con Claude Code |
| `CLAUDE.md` | Instrucciones que Claude Code lee en cada sesión |
| `.claude/rules/simulacion.md` | Reglas de reproducibilidad que se cargan al tocar la simulación |
| `proyecto-claude/` | Instrucciones y pasos para montar el proyecto de Claude |

## Por dónde empezar

1. Sigue `docs/06-guia-claude-code.md` para instalar y crear el repositorio con este kit.
2. Monta el proyecto de Claude con `proyecto-claude/LEEME.md`.
3. Empieza la fase 1: base del motor.
