# Rally Coop — Desarrollo por lotes con agentes

Desde el 2026-10-14 (tareas P1a y P1b). Diseño acordado entre Lucas y el consultor del proyecto de claude.ai, con dos revisiones más el mismo día. Manda `05-decisiones.md` («Forma de desarrollo»).

**Principio:** toda garantía crítica (quién escribe dónde, quién mueve `master`, cuántas compilaciones a la vez, qué se fusiona) la impone un script o un hook que falla cerrado, no una instrucción a un agente. Las instrucciones a los agentes solo hacen que choquen antes con esas barreras.

## 1. Por qué

Hasta la 2.7, cada tarea iba sola: inicio, plan, respuesta, informe, parada de Lucas y cierre. El código salía en 1–3 h por tarea, pero cada paso esperaba a que alguien llevara un archivo de un sitio a otro, y la parada de la 2.7 estuvo 8 días abierta.

Este diseño ataca tres cosas, por este orden:
1. Sacar a Lucas y al consultor del bucle en todo lo que una máquina puede comprobar.
2. Una sola parada de Lucas por lote.
3. Trabajo en paralelo solo donde de verdad es independiente.

Lo que limita el proyecto sigue siendo el tiempo de Lucas jugando: ningún agente lo sustituye.

## 2. Ramas y carpetas

| Qué | Dónde | Quién lo mueve |
| --- | --- | --- |
| `master` | `origin` | **Solo Lucas**, con `tools/ci/promover.ps1` después de jugar el lote (§6) |
| `integracion` | La copia principal, `D:\Proyectos\RallyCoop\Rally`, que está siempre en esta rama | El integrador (fusiones) |
| Rama de cada tarea | Su copia de trabajo en `.claude/worktrees/<nombre>/`; la crea Claude Code al lanzar un carril | El carril de esa tarea, con la rama `lote-<N>/<tarea>`. **Solo en local**: las copias comparten la carpeta `.git`, así que la copia principal ve las ramas sin push |

- **Rama de partida:** con `worktree.baseRef: "head"`, cada copia de trabajo de un carril sale del `HEAD` de la copia principal, es decir, de `integracion`.
- **Ejecutable de prueba:** el del lote es el de la copia principal.
- **Mensajes:**
  - los del consultor y los del lote van a `mensajes/` de la copia principal (local, ignorada por git);
  - cada carril escribe en `mensajes/` de su copia de trabajo.
- **Un lote sin promover como máximo.**
  - El integrador no fusiona nada en `integracion` mientras esta vaya por delante de `origin/master` con un lote que Lucas no ha jugado y promovido.
  - Solo se admiten por delante los commits `cierre lote-…` del lote anterior y los `consultor: …`, que se promueven con el lote siguiente.
  - Los carriles sí pueden adelantar trabajo en sus ramas, pero la integración espera.
  - Así, lo que Lucas nota en la prueba de un lote nunca se mezcla con el lote siguiente.
- **Lo que mide el criterio de una fase abierta no se toca desde otra.** Por ejemplo, la carretera de prueba de la 2.6b se queda congelada en su formato hasta el cierre de la fase 2 (2.11): el formato de tramo de la fase 3 vive a su lado, sin sustituirla.

## 3. Roles

Las definiciones están en `.claude/agents/`. **El director** es la sesión principal de Claude Code en la copia principal: Lucas habla con él. Los demás son subagentes.

**Los modelos van fijos en cada definición; ninguno hereda** el de la sesión. Así la calidad de un agente no depende de quién lo lanza.

| Agente | Qué hace | Qué puede tocar | Modelo |
| --- | --- | --- | --- |
| Director (sesión principal) | `/lote`: lanza los agentes, guarda sus resultados, habla con Lucas. No programa | — | Sonnet por defecto (lo elige Lucas con `/model`) |
| `revisor-diseno` | Revisa cada tarea antes del plan y cada plan antes de programar, contra `05`, la regla de alcance y `docs/agentes/criterios-revision.md` | Solo lectura; devuelve texto | Opus |
| `carril-sim` | `src/sim`, `src/gen`, `data/cars`. **El único que puede cambiar la huella o `kSimVersion`** | Su copia, sus rutas | Opus, esfuerzo alto |
| `carril-plataforma` | Render, interior, audio, entrada, pantalla, demo | Su copia, sus rutas | Sonnet |
| `carril-juego` | `src/game`, `src/net`, `src/io`: acciones, sesión, red y formatos | Su copia, sus rutas | Sonnet |
| `carril-herramientas` | `tools/` | Su copia, sus rutas | Sonnet |
| `verificador` | Release y Debug, ctest, rendimiento; comprueba que cada test nuevo mide lo que dice (lo rompe y lo restaura). Nunca es el autor | Solo dentro de `.claude/worktrees/`, sin commits; termina con la copia limpia | Sonnet |
| `revisor-determinismo` | Reglas de reproducibilidad (§5, paso 7) | Solo lectura | Opus, esfuerzo alto |
| `integrador` | Fusiona en `integracion`, regenera lo generado, aplica las propuestas de documentos y los mensajes del consultor, junta los fragmentos de progreso, escribe la prueba del lote y lo cierra tras la promoción | Lo suyo de §4, los documentos del consultor cuando trae su mensaje (nunca `.claude/**`, `tools/ci/**` y `tests/ci/**`), y `mensajes/**` de la copia principal | Sonnet |

**Uso del plan Pro.** El uso es uno solo para claude.ai (el consultor incluido) y para Claude Code, y Opus gasta bastante más que Sonnet.
- `mensajes/lote-<N>/estado.md` apunta lo que da `/status` antes y después de cada paso del lote.
- Si un lote agota el uso, el director para y deja escrito por dónde va.
- **Criterio tras el lote 1:** si no cabe en dos ventanas de uso, `revisor-diseno` pasa a Sonnet. `carril-sim` y `revisor-determinismo` siguen en Opus.

**Equipos de agentes: no se usan.** `CLAUDE_CODE_EXPERIMENTAL_AGENT_TEAMS` = 0. Con los equipos activos, un subagente lanzado con nombre arranca como compañero y trabaja en la carpeta de la sesión principal: se perdería el aislamiento sin enterarse nadie.

## 4. Propiedad de las rutas

Cada ruta tiene un único dueño. La tabla está en `.claude/hooks/propiedad.json`.

| Dueño | Rutas |
| --- | --- |
| sim | `src/sim/**`, `src/gen/**`, `data/cars/**`, `tests/sim/**`, `tests/data/cars/**`, `.claude/rules/simulacion.md` |
| plataforma | `src/platform/**`, `src/main.cpp`, `data/**` (salvo `data/cars/`), `tests/platform/**` |
| juego | `src/game/**`, `src/net/**`, `src/io/**`, `tests/io/**`, `tests/game/**` |
| herramientas | `tools/**` (salvo `tools/ci/**`), `tests/tools/**` |
| sim y juego | `tests/data/recordings/**`. Son binarios: nunca se fusionan. Si dos ramas del lote las tocan, el integrador las regenera con `rally_replay` después de fusionar el código |
| cada carril | `docs/progreso/<su tarea>.md` y `mensajes/**` de su copia |
| integrador | `CMakeLists.txt` de la raíz, `src/CMakeLists.txt`, `tests/CMakeLists.txt`, `tests/common/**` (`car_worlds.h` va en `tests/sim/common/`, del carril sim), `cmake/**`, `CMakePresets.json`, `docs/progreso.md`, la tabla generada de `docs/07` §8, `docs/02`, `docs/07` y `docs/08` |
| solo el consultor; los aplica el integrador desde un mensaje del consultor | `docs/01`, `docs/03`, `docs/04`, `docs/05`, `docs/06-guia-claude-code.md`, `docs/09-agentes.md`, `docs/agentes/**`, `docs/prompts/**`, `docs/progreso/README.md`, `CLAUDE.md` y `proyecto-claude/**` |
| solo el consultor; se aplican en una sesión manual con `RALLY_SESION=libre` | `.claude/**`, `tools/ci/**` y `tests/ci/**` (salvo `.claude/rules/simulacion.md`, del carril sim). Son las barreras y sus tests: ningún agente las cambia, tampoco el integrador, porque un agente no edita a su propio guardián |
| integrador (rutas que no son de ningún carril) | `.clang-format`, `.gitignore`, `README.md` y `docs/manejo/**` |
| plataforma (además de lo de arriba) | `tests/data/frames_rx580_60hz.txt` (lo lee `tests/platform/test_fixed_step_clock.cpp`) |

- **Documentos técnicos.** Un carril que tiene que cambiar `docs/02`, `07` u `08` escribe la edición exacta en su fragmento (sección y texto) y el integrador la aplica al fusionar.
- **Documentos de diseño.** Un carril que necesita cambiar `01`, `03`, `04` o `05` para: es un «Necesita consultor».
- **Teclas:** cada componente de la plataforma tiene su tabla de teclas en su propio archivo, y la tabla completa (ayuda y `07` §8) se monta juntándolas. Una tecla repetida falla en un test.
- **Tests:** van en `tests/<carril>/`, cada carpeta con su `CMakeLists.txt`, que es del carril. Un test que mezcla carriles va con el de lo que mide.

**Dos barreras:**
1. **Un solo hook de rutas** en `.claude/settings.json` (`PreToolUse` en `Edit|Write|NotebookEdit`, síncrono, `onFailure: "block"`). Es la primera: avisa al agente a tiempo.
   - **No va en el frontmatter de cada agente:** esos hooks no funcionan si la carpeta no tiene la confianza aceptada, ni en un agente lanzado sin su definición.
   - **El rol lo saca del campo `agent_type`** de la entrada, que Claude Code pone en cada llamada dentro de un subagente.

   | Quién escribe | Qué deja el hook |
   | --- | --- |
   | Un subagente de los 8 roles | Sus rutas de `propiedad.json` |
   | Un subagente de otro tipo (`general-purpose`, `Explore`, una bifurcación…) | Nada: bloquea |
   | La sesión principal (sin `agent_type`) con `RALLY_SESION=<rol>` en su entorno | Las rutas de ese rol. `libre` lo deja todo, solo para sesiones de infraestructura que pide el consultor |
   | La sesión principal sin `RALLY_SESION` (el director) | Solo `mensajes/` de la copia principal |

   - **Copia principal:** la carpeta del proyecto de la sesión (`CLAUDE_PROJECT_DIR`). **Raíz de la copia en la que se escribe:** `git rev-parse --show-toplevel` del `cwd` de la entrada.
   - **Un carril o el verificador con la raíz en la copia principal** (comparada con `CLAUDE_PROJECT_DIR` y con la carpeta padre de `git rev-parse --git-common-dir`) no escribe nada: si su copia de trabajo no se hubiera creado, escribiría en `integracion` sin que nadie lo notara.
   - **El verificador:** si su raíz es una copia de `.claude/worktrees/`, solo escribe en esa copia; si no, en cualquiera de `.claude/worktrees/`. Nunca en la principal.

   `RALLY_SESION` solo la pone Lucas al lanzar Claude Code (por ejemplo, `$env:RALLY_SESION = "juego"; claude`, para una sesión manual en la copia de un carril). El hook hereda el entorno de Claude Code; un agente no puede cambiarlo desde su consola.
2. **Comprobación de propiedad en la integración** (`tools/ci/propiedad.ps1`). Es la que decide: antes de cada fusión, cruza `git diff --name-only integracion...<rama>` con `propiedad.json` y rechaza la fusión si la rama toca rutas que no son de su carril, haya escrito con la herramienta que haya escrito (Bash, `clang-format -i` o un generador incluidos).

## 5. El ciclo de un lote

1. **El consultor** escribe `mensajes/lote-N.md` con:
   - el objetivo;
   - las tareas, cada una con su prompt, su carril, sus dependencias y su orden de fusión;
   - los **contratos**: interfaces y números de versión de formato fijados de antemano. Por ejemplo, si dos tareas tocan `.rrec`, cada una lleva su número;
   - lo que Lucas probará, con **un A/B en el mismo ejecutable** para cada cambio que se juzga conduciendo. Si no se puede, el lote dice que esa tarea se juega antes que el resto.
2. **Lucas**, en la sesión del director: `/lote mensajes/lote-N.md`. El director crea `mensajes/lote-N/estado.md` y lo pone al día tras cada paso.
3. **Revisión de cada tarea.** `revisor-diseno` las revisa todas, y cada revisión se guarda en `mensajes/lote-N/<tarea>-revision.md`.
   - **Necesita consultor** solo por: un choque con una decisión cerrada, una tarea que crece más del 50 %, o un cambio del diseño de `01`–`05`.
   - Con ese veredicto, la tarea para y Lucas lo lleva al proyecto de claude.ai. Las demás siguen.
4. **Carriles**, cada tarea en el suyo y en segundo plano.
   - Las tareas del mismo carril van en serie, y se respetan las dependencias.
   - **El límite de compilaciones lo impone `comprobar.ps1`** (§6) para todos: carriles, verificador e integrador.
   - **Dos tareas que cambian la huella nunca van en el mismo lote.**
5. **Plan.** El carril:
   - escribe el plan;
   - lanza él mismo a `revisor-diseno` (Opus, sea cual sea el carril) sobre su plan, y aplica sus cambios;
   - con «Necesita consultor», para.
6. **Programar.** El carril programa y prueba con `comprobar.ps1` en Release y en Debug.
   - Escribe su fragmento `docs/progreso/<tarea>.md`: la fila del registro, los hallazgos, las ediciones exactas de `02`, `07` u `08` y lo que pide de lo compartido.
   - Hace commit en su rama. **Ningún agente empuja:** las ramas se ven desde la copia principal porque comparten `.git`, y una copia con commits sin empujar es la que la limpieza periódica de Claude Code respeta.
   - Escribe su informe, con lo que Lucas tiene que probar y su A/B.
   - Si un criterio no pasa, `comun.md` §9: para con el valor medido, el esperado y la pregunta.
7. **Verificación** de cada rama, por agentes que no la escribieron:
   - **`verificador`.** Termina con `git status --porcelain` vacío en la copia del carril, y el director lo comprueba antes de retomar el carril.
   - **`revisor-determinismo`**, si la rama toca `src/sim`, `src/gen`, `src/net`, `src/game` o `src/io`, o si cambia la huella, el hash de la telemetría o `tests/data/recordings`. Comprueba:
     - `rmath` y PCG;
     - nada añadido tras `seal()`;
     - cero reservas en el paso;
     - para cada campo nuevo del estado (también el de `src/game` que entre en el SaveState), un test que falla si se quita;
     - la huella cambiada solo con justificación.
   - **Si sale «No pasa»,** el carril se retoma con la lista y se vuelve a verificar. Tres vueltas sin pasar: se para y se le dice a Lucas.
8. **Integración.** Solo si no hay lote sin promover (§2). Para cada rama, en el orden del lote:
   1. `propiedad.ps1`;
   2. `git merge --no-ff`;
   3. `comprobar.ps1`.

   Los conflictos:
   - en un archivo generado, se regenera;
   - en las grabaciones de test, se regeneran con `rally_replay`;
   - en cualquier otro sitio, `merge --abort` y lo resuelve el carril dueño.

   - **Base cambiada.** Si `integracion` cambió algún archivo de la rama desde que salió (arreglos de la prueba anterior, otra rama del lote), se repite el `verificador` sobre el resultado de la fusión, y el `revisor-determinismo` si toca su terreno. Si no, basta con `comprobar.ps1`.

   Después, el integrador aplica las ediciones de documentos de los fragmentos. No empuja: lo hace Lucas al promover.
9. **Prueba.** El integrador escribe `mensajes/lote-N/prueba.md`: el ejecutable, **una sola lista de pruebas** con los A/B, y las preguntas numeradas.
10. **Lucas juega.**
    - Las respuestas cortas van al director, que lanza arreglos pequeños en el mismo carril sobre `integracion`.
    - Lo que cambia el diseño o necesita diagnóstico va al consultor.
11. **Promoción y cierre.**
    - Lucas promueve con `tools/ci/promover.ps1` (§6): empuja `integracion`, `master` y las ramas del lote en una sola orden.
    - El integrador cierra el lote: fragmentos a `progreso.md`, limpieza de las copias y lista de documentos cambiados.
    - El consultor sube al proyecto los documentos cambiados, leídos de `master`.

## 6. Comprobaciones automáticas

- **`tools/ci/comprobar.ps1 [-Debug] [-SinFormato]`:**
  - hace: clang-format, configurar, compilar y ctest;
  - acaba con una línea de resumen: tests, huella, reservas, hash de la telemetría y `kSimVersion`;
  - si falla, sale con un código distinto de 0.
- **Límite de compilaciones dentro de `comprobar.ps1`.** Dos plazas, hechas con dos mutex con nombre de Windows (`Local\RallyBuild0` y `Local\RallyBuild1`).
  - **No es un semáforo:** un semáforo no tiene dueño, y la plaza de un proceso muerto se pierde. Un mutex abandonado pasa al siguiente que espera.
  - **`AbandonedMutexException` cuenta como plaza conseguida.**
  - **El mutex se toma y se suelta en el mismo hilo:** la compilación no va en un `Start-Job` ni en otro runspace.
  - **La compilación va en un Job Object de Windows con `KILL_ON_JOB_CLOSE`:** si muere el PowerShell, mueren también `cmake`, MSBuild y los `cl.exe` hijos. Si no, quedarían tres compilaciones reales con dos plazas libres.
  - Con otra compilación en marcha, se compila con 2 procesos.
- **`tools/ci/propiedad.ps1 <rama>`:** la segunda barrera de §4. Sale con un código distinto de 0 y la lista de rutas ajenas.
- **Hook de rutas: `.claude/hooks/rutas.py`,** registrado una sola vez en `.claude/settings.json`:
  - el rol sale de `agent_type` (o de `RALLY_SESION` en la sesión principal), según la tabla de §4;
  - la raíz de la copia sale del campo `cwd` de la entrada, nunca de `${CLAUDE_PROJECT_DIR}` (que apunta a la copia principal);
  - **falla cerrado:** sale con 2 ante cualquier excepción, y el hook es síncrono con `onFailure: "block"` (Claude Code ≥ 2.1.295). Si Python no arranca o se pasa de tiempo, también bloquea;
  - el script vive en la copia principal, así que un agente no puede editar su propio guardián.
- **Nadie empuja salvo Lucas.**
  - `tools/ci/instalar-hooks.ps1` fija `remote.origin.pushurl` en `no-push`, así que un `git push` normal falla siempre.
  - Instala además `.git/hooks/pre-push`, común a todas las copias: rechaza los push a `refs/heads/master` salvo con `RALLY_PROMOVER=1` y rechaza siempre los forzados.
- **Promoción: `tools/ci/promover.ps1 [-WhatIf]`,** que solo ejecuta Lucas. Comprueba que `integracion` es un avance rápido sobre `origin/master` y, con la URL de `origin` explícita y `RALLY_PROMOVER=1`, empuja en una sola orden:
  - `integracion`;
  - `integracion:master`;
  - las ramas `lote-*` del lote.
- **Permisos** (`.claude/settings.json`):
  - **permitido sin preguntar:** compilar, probar, git local (sin `push`, `config` ni `remote`) y los scripts de `tools/` salvo `promover.ps1`;
  - **prohibido:** `git push`, `git config`, `git remote` (también con `git -C <ruta>`), `--no-verify`, `RALLY_PROMOVER`, `RALLY_SESION` y `promover.ps1`, en Bash y en PowerShell. Son reglas sobre el texto de la orden: la barrera que manda es `pushurl` más el `pre-push`.
- **GitHub (opcional; lo hace Lucas una vez):** si el repositorio es público o tiene GitHub Pro, una regla de la rama `master` que bloquee el push forzado y el borrado. Se aplica en el servidor, aunque las credenciales sean las de Lucas.
- **Límite honesto.**
  - Un agente que use a propósito la URL explícita y se salte varias barreras podría empujar con las credenciales de Lucas. Quedaría en su transcripción. Si alguna vez `master` se mueve sin Lucas, se recupera con el reflog.
  - Las ramas sin promover solo existen en el PC de Lucas: lo que se pierde si falla el disco es como mucho un lote.
- **Dependencias.**
  - Las fuentes se descargan una sola vez, en la carpeta de `RALLY_DEPS_SRC`, con `FETCHCONTENT_SOURCE_DIR_*` desde los presets.
  - **Nunca** se comparte una carpeta de compilación ni `FETCHCONTENT_BASE_DIR` entre copias.

## 7. Lo que no cambia

- `CLAUDE.md`, `comun.md` y las reglas de la simulación valen para todos los agentes.
- **Lucas decide** lo que se juzga jugando (manejo, aspecto, sonido, diversión) y promueve a `master`.
- **El consultor decide el diseño y las decisiones cerradas.** Escribe los lotes, sus contratos, los prompts y los criterios de revisión, y diagnostica las pruebas de Lucas.
- **Ningún agente decide un cambio de diseño.**
