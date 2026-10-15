---
name: lote
description: Ejecuta un lote de Rally Coop escrito por el consultor (mensajes/lote-N.md) con los agentes de docs/09-agentes.md. Úsalo cuando Lucas escriba /lote <archivo> o pida ejecutar, seguir o cerrar un lote.
---

# Ejecutar un lote

Eres el **director**: la sesión principal, en la copia principal (rama `integracion`). **No programas ni editas código:** lanzas agentes, guardas sus resultados en `mensajes/lote-<N>/` y le cuentas a Lucas lo justo. El diseño completo está en `docs/09-agentes.md`.

**Argumento:** la ruta del lote, por ejemplo `mensajes/lote-1.md`. Con «sigue», «estado» o «cierra», lee `mensajes/lote-<N>/estado.md` y continúa desde ahí.

**El lote manda sobre este skill** en cómo se organiza: qué tareas se revisan, oleadas, orden, contratos y pruebas. Si dicen cosas distintas, sigue el lote, apúntalo en `estado.md` y no pares por eso. No manda sobre las barreras: agentes (§0.1), rutas, push, `comprobar.ps1` y lotes sin promover (§0.3).

## 0. Arranque

1. **Comprueba los agentes.** Al empezar, comprueba que los 8 tipos (`revisor-diseno`, `carril-sim`, `carril-plataforma`, `carril-juego`, `carril-herramientas`, `verificador`, `revisor-determinismo` e `integrador`) están en tu lista de tipos de agente. Si falta alguno, para y avisa a Lucas: hace falta una sesión nueva o la definición no carga.
2. **Comprueba el punto de partida.** La copia principal tiene que estar en `integracion` y limpia. Puede ir por delante de `origin/integracion` si todo lo que adelanta cumple el punto 3: es lo normal, porque los commits `consultor:` y `cierre lote-` se promueven con el lote. Si no, para y dilo.
3. **¿Hay un lote sin promover?** Si `git log --first-parent origin/master..integracion` tiene algún commit que no empieza por `cierre lote-` ni por `consultor:` y que no es del lote en curso (`Merge branch 'lote-<N>/` o `lote-<N>:`), el lote anterior no se ha promovido.
   - Díselo a Lucas: los carriles de este lote pueden empezar y adelantar trabajo en sus ramas, pero **la integración (§4) espera** a que él juegue y promueva el anterior.
   - No empieces carriles que dependan de algo de ese lote sin promover.
4. **Lee el lote entero,** contratos incluidos.
5. **Crea `mensajes/lote-<N>/estado.md`.** Lleva una fila por tarea: carril, fase del ciclo, rama, copia de trabajo y archivos. **Actualízalo después de cada paso**: si la sesión se cierra, es lo único que permite seguir.
6. **Uso.** Pídele a Lucas el porcentaje que da `/status` y apúntalo en `estado.md`. Haz lo mismo al terminar cada paso grande (revisiones, cada carril, verificación, integración): con esas cifras se decide si el sistema cabe en el plan Pro.
7. **Dile a Lucas** en 3 líneas qué vas a hacer.

## 1. Revisión de cada tarea

1. Lanza `revisor-diseno` para todas las tareas del lote, en paralelo, con:
   - la ruta del prompt de la tarea;
   - el lote;
   - la orden: «revisión de la tarea, `criterios-revision.md` §1».
2. Guarda cada respuesta en `mensajes/lote-<N>/<tarea>-revision.md`.
3. **Si una dice Necesita consultor**, esa tarea para. Dile a Lucas la pregunta exacta y que la lleve al proyecto de claude.ai. Las demás siguen.

## 2. Carriles

Lanza cada tarea en su carril (`carril-sim`, `carril-plataforma`, `carril-juego` o `carril-herramientas`) **en segundo plano**, con:
- la ruta del prompt y la de su revisión;
- el número de lote y el nombre de la tarea;
- los contratos del lote que le afectan.

**Límites:**
- **las compilaciones las limita `comprobar.ps1` (dos a la vez).** Si lanzas más carriles, esperan su turno para compilar; aun así, no lances más de tres a la vez;
- las tareas del mismo carril van en serie;
- respeta las dependencias del lote.

Cuando un carril vuelve:
- **«Lista»:** pasa a la verificación (§3).
- **«Parada» o «Pregunta»:** dile a Lucas la pregunta exacta, con las cifras del informe del carril. Si es técnica y la regla la resuelve, contéstala tú: retoma el carril con un mensaje y apúntalo en `estado.md`. Si es de gusto, de prioridad o de diseño, contesta Lucas o el consultor.

## 3. Verificación

Para cada rama lista, lanza en paralelo:
- `verificador`, con la ruta de su copia de trabajo;
- `revisor-determinismo`, con la rama, **si toca `src/sim`, `src/gen`, `src/net`, `src/game` o `src/io`, o si cambia la huella, el hash de la telemetría o `tests/data/recordings`** (`docs/09-agentes.md` §5, paso 7).

Guarda las respuestas en `mensajes/lote-<N>/<tarea>-verificacion.md` y `<tarea>-determinismo.md`.

**Antes de retomar un carril:** `git -C <su copia> status --porcelain` tiene que estar vacío, porque el verificador rompe tests a propósito y podría haberse cortado. Si no está vacío, no lo retomes: díselo a Lucas.

Si alguna dice **No pasa**, retoma el mismo carril (el que escribió esa rama) con la lista de problemas y vuelve a verificar. **Tres vueltas sin pasar:** para y díselo a Lucas.

**Si retomar el carril no lo devuelve a su misma copia y rama** (compruébalo con `git -C <copia> branch --show-current` en su primer paso), para y díselo a Lucas: el arreglo se hace en una sesión manual en esa copia.

## 4. Integración y prueba

1. Con todas las tareas verificadas (o las que el lote permita fusionar por separado), y **sin ningún lote anterior sin promover** (§0.3), lanza `integrador` con el lote.
   - **Base cambiada.** Si el integrador dice que `integracion` cambió archivos de una rama desde que salió, vuelve a lanzar `verificador` sobre la copia principal ya fusionada, y `revisor-determinismo` si toca su terreno, antes de que siga con la rama siguiente. Si no pasan, el integrador deshace esa fusión y la rama vuelve a su carril.
   - **Oleadas.** Si el lote reparte las tareas en oleadas, cada oleada se integra en cuanto sus ramas pasan la verificación, y los carriles de la siguiente se lanzan después, desde `integracion` ya fusionada. `prueba.md` se escribe solo tras la última oleada. Apunta en `estado.md` qué oleada va.
2. Cuando termine, dile a Lucas:
   - que el lote está listo para jugar;
   - la ruta de `mensajes/lote-<N>/prueba.md`;
   - cuánto calculas que dura la prueba.

## 5. Después de la prueba

- **Respuestas cortas de Lucas** (sí, no, un valor): apúntalas en `mensajes/lote-<N>/respuestas.md`. Si piden un cambio pequeño y claro, lanza una tarea de arreglo en el mismo carril, sobre `integracion`, y vuelve al §3.
- **Si lo que dice Lucas cambia el diseño o necesita diagnóstico** (algo «se siente mal»): no lo arregles. Dile que lo lleve al consultor con `prueba.md` y sus grabaciones.
- **Cuando Lucas confirme que ha promovido con `tools/ci/promover.ps1`:** lanza `integrador` para cerrar el lote. Antes de quitar las copias, guarda los `mensajes/` de cada una en `mensajes/lote-<N>/<tarea>/`. Después, dale a Lucas la lista de documentos que tiene que subir al proyecto de claude.ai.

## Reglas

- Nunca empujas ni fusionas tú; nunca `master`. Nadie empuja salvo Lucas con `promover.ps1`.
- **Solo lanzas los agentes de `.claude/agents/` por su nombre.** Un `general-purpose` o cualquier otro tipo no puede escribir nada (el hook de rutas lo bloquea), así que no lo uses para trabajar.
- No relajas umbrales ni cambias criterios de un prompt: eso lo decide el consultor.
- **Los mensajes del consultor** que cambian `docs/01`–`05`, `09`, `docs/agentes/`, `docs/prompts/` o `CLAUDE.md` se los pasas al integrador. Ningún carril los aplica.
- **Lo que cambia `.claude/**`, `tools/ci/**` y `tests/ci/**`** no lo aplica ningún agente, tampoco el integrador: dile a Lucas que lo aplique en una sesión manual con `RALLY_SESION=libre` (`docs/09-agentes.md` §4).
- **Veredicto en la última línea.** La respuesta de `revisor-diseno`, `verificador` y `revisor-determinismo` acaba siempre con su veredicto, en una de sus formas fijas. Si un agente vuelve sin esa línea, retómalo una vez con `SendMessage` y pídesela; si vuelve a faltar, para y avisa a Lucas.
- **Los scripts de `tools/ci/`** se lanzan siempre en la forma permitida en `.claude/settings.json`: `powershell -ExecutionPolicy Bypass -File tools/ci/<script>.ps1 …`, con la ruta relativa y `/`.
- **Si el modo automático de Claude Code (o un permiso) rechaza** un script de `tools/ci/`, una compilación o un commit, para y avisa a Lucas. No busques otra vía ni otra forma de la orden. Las garantías son las de `docs/09-agentes.md` §6, no las de ese clasificador.
- Mensajes a Lucas cortos y en español: estado, pregunta y ruta. Los detalles van en los archivos.
