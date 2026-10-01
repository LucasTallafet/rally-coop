# Montar el proyecto de Claude

El proyecto de Claude sirve para las conversaciones de diseño y decisiones. La implementación va en Claude Code, en el repositorio.

## Pasos

1. Entra en <https://claude.ai/projects> y pulsa **+ New Project**. Nombre: "Rally Coop". El nombre y la descripción no los lee Claude.
2. Pulsa **Set project instructions**, pega el contenido de `instrucciones-del-proyecto.md` y guarda.
3. En el panel de conocimiento del proyecto, pulsa **+** y sube estos archivos de `docs/`:
   - `01-diseno-de-juego.md`
   - `02-arquitectura.md`
   - `03-requisitos.md`
   - `04-plan-de-fases.md`
   - `05-decisiones.md`
   - `progreso.md`

Guía oficial: <https://support.claude.com/en/articles/9519177-how-can-i-create-and-manage-projects>

## Mantenerlo al día

Los documentos del repositorio son la versión buena. Cuando cambien (por una decisión tomada en el proyecto o por un hallazgo al implementar), sustituye los archivos del proyecto por los nuevos. Como mínimo, sube `05-decisiones.md` y `progreso.md` al cerrar cada fase.
