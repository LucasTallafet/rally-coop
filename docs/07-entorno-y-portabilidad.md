# Rally Coop — Entorno de trabajo y portabilidad

Versión del 2 de octubre de 2026, al cierre de la fase 1.

Cómo montar en otro ordenador el mismo entorno con el que se desarrolla Rally Coop: qué hay que instalar, qué viaja con el repositorio y qué no, y cómo comprobar que el PC nuevo da los mismos resultados.

---

## 1. Qué hay y dónde está

| Pieza | Dónde vive | ¿Viaja sola? | Cómo se recupera en otro PC |
| --- | --- | --- | --- |
| Código, tests, CMake, documentos (`docs/`) | GitHub, `LucasTallafet/rally-coop` (privado) | Sí | `git clone` |
| `CLAUDE.md` y `.claude/rules/simulacion.md` | Repositorio | Sí | Vienen con el clon |
| Targets de juego `rally_game` y `rally_tests_game` (`src/game`, `tests/game`); duraciones de las acciones en `data/game/actions.txt`, que se ajustan sin recompilar | Repositorio | Sí | Vienen con el clon |
| Targets de red `rally_net` y `rally_tests_net` (`src/net`, `tests/net`; 25 casos, ~6 s por los de ENet). Regla: un test de ENet usa `127.0.0.1` y puerto 0 | Repositorio | Sí | Vienen con el clon |
| Dependencias (Jolt, SDL, raylib, doctest) | Se descargan en el primer configure, fijadas por URL y SHA256 en `cmake/Dependencies.cmake` | Sí | `cmake --preset ...` |
| `build/` (compilados, capturas, registros, informes de `--bench-forest`) | Solo local, ignorado por git | No (se regenera) | Volver a compilar. Si quieres conservar un informe o un registro, cópialo a `tests/data/` o a `docs/` |
| Proyecto de Claude "Rally Coop" (diseño y decisiones) | claude.ai, en tu cuenta | Sí | Entrar con la misma cuenta. Nada que instalar |
| Memoria de Claude Code | Local, en tu carpeta de usuario de cada PC | **No** | Lo que deba sobrevivir tiene que estar en `CLAUDE.md` (ver §5) |
| Copias de trabajo de Claude Code (`.claude/worktrees/`) | Local, ignoradas por git | No | No hacen falta: todo lo bueno se fusiona en `master` |
| Ajustes de Windows y del controlador gráfico | El PC | **No** | A mano, con la lista del §6 |
| Toolchain (Visual Studio, CMake, Python, clang-format, Claude Code) | El PC | **No** | §3 |

**Regla de oro:** si algo solo está en tu PC (una nota, un informe, una regla que Claude Code "recuerda"), no existe para el PC nuevo. Lo que importe, al repositorio.

---

## 2. Requisitos del PC de desarrollo

| Componente | Necesario | Notas |
| --- | --- | --- |
| Sistema | Windows 10 u 11 de 64 bits | El juego solo es para Windows |
| Procesador | Cualquier x64 con SSE4.2 | El código no usa AVX (decisión cerrada). Cualquier CPU de los últimos 12 años vale |
| Gráfica | Dedicada con OpenGL 3.3 | Para medir rendimiento con valor: GTX 1060 / RX 580 o similar (los requisitos mínimos del juego) |
| Disco | ~15 GB libres | Visual Studio ~8–10 GB; repositorio + `build/` ~2 GB |
| Cuenta | Claude Pro, Max, Team o Enterprise | Para Claude Code y el proyecto de claude.ai |

---

## 3. Instalación

Instala con **PowerShell como administrador** solo los programas. Todo lo demás (clonar, compilar, Claude Code, git) desde un **PowerShell normal**. Si clonas o compilas como administrador, los archivos quedan a nombre de "Administradores" y git se niega a trabajar después ("dubious ownership").

### 3.1 Programas (PowerShell como administrador)

```powershell
winget install Git.Git
winget install Microsoft.VisualStudio.2022.Community --override "--add Microsoft.VisualStudio.Workload.NativeDesktop --includeRecommended --passive"
winget install Kitware.CMake
winget install Python.Python.3.12
```

- **Visual Studio 2022 Community**, no Build Tools: Build Tools no trae depurador, y desde la fase 2 hace falta. No uses Visual Studio 2026: el generador fijado en los presets es `Visual Studio 17 2022`.
- El **Visual C++ Redistributable** no sirve: solo ejecuta programas, no compila.
- **Python 3** lo necesitan clang-format y ctest: el test `zone_json.python` comprueba el trazado de la zona de pruebas con la biblioteca estándar (sin paquetes). Sin Python, el configure avisa y ese test falla.

### 3.2 Herramientas de usuario (PowerShell normal, ventana nueva)

```powershell
python -m pip install --user clang-format==23.1.2
python -m pip install --user -r tools/telemetry/requirements.txt
irm https://claude.ai/install.ps1 | iex
```

La versión de clang-format está fijada: otra versión formatea distinto y el código ya formateado empezaría a dar diferencias. `requirements.txt` (desde la raíz del repositorio clonado) instala matplotlib para `plot.py` y `compare.py`. Los tests de Python (`telemetry_py.*`, Python 3.12 o posterior) usan el `python` del PATH. Sin matplotlib, el configure avisa y no añade `telemetry_py.plot` ni `telemetry_py.compare`.

### 3.3 Comprobación

Abre **otra ventana nueva** de PowerShell (para que cargue el PATH) y comprueba:

```powershell
git --version
cmake --version          # 3.25 o posterior (en el PC original: 4.4)
python --version         # 3.12 o posterior
clang-format --version   # 23.1.2
claude --version
```

Si `clang-format` no se encuentra, añade al PATH la carpeta `Scripts` de Python de tu usuario (pip indica la ruta al instalar).

### 3.4 Versiones conocidas que funcionan

| Pieza | Versión en el PC original |
| --- | --- |
| Visual Studio 2022 Community | MSVC 14.44 (VS 17.14) |
| Windows SDK | 10.0.26100 |
| CMake | 4.4 |
| clang-format | 23.1.2 |
| Jolt / SDL / raylib / doctest | v5.6.0 / 3.4.16 / 6.0 / v2.5.3 (fijadas en el repositorio) |

---

## 4. Clonar, compilar y comprobar

En **PowerShell normal**:

```powershell
mkdir D:\Proyectos\RallyCoop
cd D:\Proyectos\RallyCoop
git clone https://github.com/LucasTallafet/rally-coop.git Rally
cd Rally
git config user.name "Lucas Tallafet"
git config user.email "<tu correo de GitHub>"

cmake --preset windows-release
cmake --build --preset windows-release
ctest --preset windows-release
```

- La primera vez, git abre el navegador para iniciar sesión en GitHub (Git Credential Manager).
- El primer configure descarga las dependencias (~90 s). El de Debug es igual con `windows-debug`.
- El ejecutable queda en `build\windows-release\src\Release\rally.exe`. Es un solo `.exe` sin DLL.

**Comprobación de una copia de trabajo (desde la tarea P1a):** `tools\ci\comprobar.ps1 [-Debug] [-SinFormato]` hace todo en un solo comando y vale en cualquier copia (`.claude\worktrees\...` incluidas):

- clang-format 23.1.2 sin cambios en el C++ de `src/`, `tests/` y `tools/` (el del PATH o el del paquete de pip);
- `cmake --preset`, `cmake --build --preset` y `ctest --preset` del preset;
- una línea final con los tests, la huella, las reservas de Jolt, el hash de la telemetría y `kSimVersion`. Sale con un código distinto de 0 en el primer paso que falla.

Como mucho **dos compilaciones a la vez en el PC**, con dos mutex con nombre de Windows (`Local\RallyBuild0` y `Local\RallyBuild1`): la tercera espera («esperando plaza…»).
- La plaza de un proceso que murió pasa al siguiente (mutex abandonado).
- Cada orden corre en un Job Object que la mata si muere el PowerShell, con su propio `mspdbsrv` y sin nodos de MSBuild que sobrevivan, para que el final de una compilación no tumbe la de otra copia.
- Con la otra plaza ocupada, compila con 2 procesos.

**Fuentes de las dependencias, una sola vez para todas las copias:** `tools\ci\deps.ps1` las descarga en `RALLY_DEPS_SRC` (por defecto `D:\Proyectos\RallyCoop\deps-src`) comprobando su SHA256, que lee de `cmake/Dependencies.cmake`.
- Con la variable de entorno puesta (`setx RALLY_DEPS_SRC D:\Proyectos\RallyCoop\deps-src`, en una ventana nueva), el configure usa esas fuentes si su sello coincide con el hash; si no, descarga como siempre y lo avisa.
- Cada copia compila igualmente sus dependencias en su propio `build/`.

**Capturas de referencia:** `tools\ci\golden.ps1 [-Regenerar]` saca con `rally.exe --golden` las capturas de `tests/platform/golden/comandos.txt` y las compara byte a byte. Solo valen en el mismo PC y sirven para reestructurar sin cambiar el dibujo (detalle en la cabecera de `comandos.txt`); no van en `comprobar.ps1`.

**Comprobación de que el PC nuevo es equivalente:**

1. **Todos los tests en verde**, en Release y en Debug (92 al cierre de la fase 1).
2. **El test de huella (`sim_world: state fingerprint...`) tiene que pasar sin tocar nada.** Comprueba que la simulación da exactamente el mismo resultado que en el PC original.
   - **Si falla en el PC nuevo con el mismo commit, no actualices la huella.** Sería un hallazgo importante: la simulación no da lo mismo en dos PCs distintos. Eso afecta a la fase 9 (clasificatorias), y hay que apuntarlo en `progreso.md` antes de seguir.
3. `rally.exe --fullscreen`: arranca en pantalla completa sin bordes, y el panel F1 muestra como tarjeta gráfica (GL_RENDERER) la **dedicada**, no la integrada.

---

## 5. Claude: proyecto y Claude Code

### 5.1 Proyecto de claude.ai ("Rally Coop")

Es el sitio de las conversaciones de diseño y decisiones. Está en tu cuenta: en otro PC solo hay que entrar en claude.ai. Si alguna vez hubiera que montarlo de cero, los pasos están en `proyecto-claude/LEEME.md` y las instrucciones en `proyecto-claude/instrucciones-del-proyecto.md`.

Documentos que tiene que tener subidos, y mantener al día cuando cambien en el repositorio: `01` a `05`, `progreso.md`, este documento (`07`) y la bitácora (`08`).

Para que el consultor del proyecto pueda leer el repositorio directamente, la conversación tiene que estar vinculada a ese PC desde la app de escritorio de Claude, con acceso a la carpeta del repositorio.

### 5.2 Claude Code

- Se abre en la carpeta del repositorio (`cd D:\Proyectos\RallyCoop\Rally` y `claude`). Lee `CLAUDE.md` en cada sesión, y las reglas de `.claude/rules/simulacion.md` se cargan solas al tocar `src/sim`, `src/gen`, `src/net` o `tests`.
- **Su memoria es local de cada PC.** En el PC original guardó al menos esta regla, que no está en el repositorio: *"antes de borrar archivos en la carpeta del exe (capturas, registros), mirar qué hay y borrar solo lo que haya creado él"*. Hay que pasarla a `CLAUDE.md` (está en la lista de pendientes de la bitácora).

---

## 6. Ajustes del PC que no viajan con git

Revísalos en cada PC nuevo. Todos salieron de problemas reales de la fase 1 (detalle en la bitácora).

| Ajuste | Qué hacer | Por qué |
| --- | --- | --- |
| Cable del monitor | En la gráfica **dedicada**, no en la placa base | Si no, la imagen sale por la integrada |
| Pantallas en Windows | Una sola pantalla activa. Nunca "Duplicar estas pantallas" entre la integrada y la dedicada | Duplicar entre dos gráficas causó tearing fuerte |
| Gráfica que usa el juego | El exe ya pide la dedicada (AMD y NVIDIA). Compruébalo en el panel F1 | Sin eso, OpenGL elegía la Intel, y su controlador ocupa un núcleo entero de la CPU con sincronización vertical |
| Sincronización vertical en el panel del controlador (AMD Adrenalin / panel de NVIDIA) | "Desactivado, a menos que la aplicación especifique" o "Controlado por la aplicación" | Si el controlador la fuerza, se salta la del juego |
| Modo de ventana | Probar y medir siempre en **pantalla completa sin bordes** (F11 o `--fullscreen`) | En ventana, con AMD en Windows 10, hay tearing y ruido de presentación fuera de nuestro control |
| Steam | **Cerrado** al probar mandos | Steam Input reasigna los mandos |
| Mando de PS3 | Instalar el driver **DsHidMini** | Sin él, Windows no lo reconoce. Xbox, PS4, PS5 y Switch Pro no necesitan nada |

---

## 7. Flujo de trabajo

Desde la P1b, el desarrollo va por lotes con agentes: el detalle está en `docs/09-agentes.md`. En resumen:

1. **La copia principal está siempre en la rama `integracion`.** No se programa en ella: es la del director (la sesión de Claude Code con la que hablas) y la del ejecutable de prueba de cada lote.
2. **Lote.** El consultor escribe `mensajes/lote-N.md`. En la sesión del director: `/lote mensajes/lote-N.md`. Los carriles trabajan en `.claude/worktrees/` y el integrador fusiona en `integracion`.
3. **Prueba.** Juegas con `mensajes/lote-N/prueba.md` y llevas los resultados al consultor (o al director, si son respuestas cortas).
4. **Promover**, solo después de jugar: `powershell -ExecutionPolicy Bypass -File D:\Proyectos\RallyCoop\Rally\tools\ci\promover.ps1` (con `-WhatIf`, solo dice lo que haría). Empuja `integracion`, `master` y las ramas del lote. Es el único push que se hace: los agentes no empujan, `pushurl` está en `no-push` y el `pre-push` rechaza cualquier otro push a `master` y cualquier push forzado. Si el script dice que no es un avance rápido, no fuerces nada: díselo al director.

   **Sesiones manuales** (cuando el director o el consultor lo pidan): lánzalas con el rol, por ejemplo `$env:RALLY_SESION = "juego"; claude` en la copia de ese carril. Sin la variable, la sesión principal solo puede escribir en `mensajes/`.
5. **Un lote sin promover como máximo:** hasta que no promuevas un lote, el integrador no fusiona el siguiente.
6. **Sincronizar el proyecto de claude.ai** con los documentos que el integrador diga que han cambiado.
7. **Uso del plan Pro:** el director te pide el porcentaje de `/status` en cada paso grande del lote.
8. **Limpiar las copias de trabajo** cuando estén fusionadas. Lo hace el integrador al cerrar el lote:

```powershell
git worktree list
git worktree remove .claude/worktrees/<carpeta>
git branch -D <rama>
```

Si alguna está bloqueada ("locked" o "en uso por otro proceso"), cierra primero la sesión de Claude Code que la creó y los procesos que la usan, y fuerza:

```powershell
Stop-Process -Name rally, MSBuild, mspdbsrv, VBCSCompiler -ErrorAction SilentlyContinue
git worktree unlock .claude/worktrees/<carpeta>
git worktree remove -f .claude/worktrees/<carpeta>
git worktree prune
```

Si al borrar una copia queda su carpeta vacía «en uso por otro proceso», cierra la sesión de Claude Code que trabajaba en ella y bórrala con `rmdir`.

**Mensajes:** los del consultor y los del lote van a `mensajes/` de la copia principal (local, ignorada por git). Cada carril escribe en `mensajes/` de su copia de trabajo. Los textos largos nunca se pegan en la consola.

---

## 8. Referencia rápida de la demo actual

Válido desde la tarea 2.4b de la fase 2: el coche (`data/cars/mid_awd_turbo.car`) en la zona de pruebas; K cambia al recinto de la fase 1. Los botones del mando van por posición, sin teclas de depuración. En pantalla, siempre, el indicador mínimo (velocidad, marcha, rpm, presión del turbo, modo de la caja y la configuración del coche, salvo a ciegas o con `--hide-config`; en la zona, la vuelta en curso y la mejor; a ciegas, «Prueba k/15») y «H: ayuda»; H enseña la ayuda de teclas, F3 el panel de depuración, F1 el rendimiento y T las gráficas, cada uno en su sitio fijo; mientras la ayuda está abierta, F3, F1 y T no se dibujan (desde la 2.5b la ayuda ocupa su sitio). La ayuda de dirección (F6, activada por defecto), la caja automática o secuencial (F2, automática por defecto; desde la 2.6) y la dirección con teclado o ratón (F7; con ratón, X o el botón central recentran y el indicador mínimo enseña el volante) se guardan en `ajustes.txt`, igual que el asiento (Re Pág / Av Pág, Inicio / Fin, Insert / Supr), que se guarda junto al exe; si ese archivo tiene una línea mal escrita, se avisa en pantalla con su número y se usa el valor por defecto. Desde la 2.7, en la vista del ojo se ven el interior provisional, la cabeza que se mueve con el coche (F9, activada por defecto) y suena el coche (F4: volumen 100, 70, 40 y 0 %); los dos se guardan también en `ajustes.txt`. Desde la 2.8, los números 4 / 5 bajan y suben la sensibilidad del stick (50–150 %, de 10 en 10; se guarda en `ajustes.txt` como `sensibilidad_direccion`). Desde la 2.8, las teclas 1, 2 y 3 cambian el ABS (sí / no), el control de tracción (no / bajo / alto) y la dureza de la suspensión (blanda / media / dura): reconstruyen el mundo y empiezan otra grabación, y se guardan en `ajustes.txt` con las claves `abs` (1 o 0; desactivado por defecto desde el lote 3), `control_traccion` (`no`, `bajo` o `alto`) y `suspension` (`blanda`, `media` o `dura`). `data/game/actions.txt` se busca desde la carpeta del exe hacia arriba, como el coche; su texto va en la cabecera de cada grabación. La tecla 6 (depuración) cambia el sonido del deslizamiento entre el nuevo y el de la 2.7 (A/B del lote 3; empieza siempre en el nuevo; no se guarda ni va en la grabación). Las teclas 7, 8 y 9 hacen de copiloto (guardar la tablet, sacarla, cancelar la acción) hasta que haya tablet (fase 3); el anillo de progreso sale centrado, algo por debajo del centro, también con la ayuda abierta, encima de ella. La ayuda (H) va por páginas: H pasa a la siguiente y, tras la última, la oculta. F1 enseña además la CPU del hilo de audio, su llamada más larga y los cortes.

Las tablas de teclas salen de las tablas de cada componente (`src/platform/drive_keys.cpp`, `window_keys.cpp` y las de las partes de la demo, `demo_*_keys.cpp`), que `src/platform/key_tables.cpp` junta por bloques y por su `order`: es la misma tabla que usa el juego y la ayuda en pantalla. El test `keys: docs/teclas.md matches the generated table` comprueba que coinciden, y `keys: no key is repeated within a context` que ninguna tecla se repite entre tablas. Para cambiarlas, se cambia la tabla del componente y se copia en `docs/teclas.md` lo que imprime ese test al fallar.

La tabla de teclas está en `docs/teclas.md`.

| Opción de arranque | Qué hace |
| --- | --- |
| `--car <archivo>` | Coche que se carga (por defecto, `data/cars/mid_awd_turbo.car`, buscado desde la carpeta del exe hacia arriba); F5 relee ese archivo |
| `--fullscreen` | Arranca en pantalla completa sin bordes |
| `--frame-log` | Graba el registro por fotograma desde el inicio |
| `--bench-forest` | Mide los cuatro modos del bosque en el recinto a 1920×1080 sin sincronización vertical, escribe `rendimiento_NNN.txt` junto al exe y se cierra |
| `--bench-zone` | Mide la CPU del dibujo del suelo de la zona en pantalla completa sin bordes, con la cámara por la pista ciega a 100 km/h (3 s de calentamiento y 20 s de medida); escribe `rendimiento_zona_NNN.txt` junto al exe y se cierra |
| `--bench-road` | Lo mismo con la cámara por la carretera de prueba (con sus árboles), de la entrada a la frenada; escribe `rendimiento_carretera_NNN.txt` |
| `--hide-config` | La pantalla no dice qué configuración del coche se lleva (F8 sigue cambiándola): bloque C del protocolo de la 2.10 |
| `--blind-seed <n>` | Semilla de la serie de 15 pruebas a ciegas (M); por defecto, 1 |

Las grabaciones de cada pasada quedan en `grabaciones/AAAAMMDD-HHMMSS.rrec` junto al exe y el overlay muestra el archivo y los pasos. Se repiten con la herramienta de consola `build/<preset>/tools/replay/<Config>/rally_replay.exe`: 
- `info <archivo>`: cabecera y pie; dice, entre otras cosas, la superficie real de la pista ciega y las ayudas de la descripción del mundo (dirección, caja, ABS, control de tracción y suspensión). Con formato 7, además, los nombres de las acciones, el número de eventos con los 10 primeros (paso, jugador, tipo, acción) y el hash de la línea de tiempo.
- `run <archivo>`: «hash OK» o el primer problema, y la mejor vuelta cronometrada.
- `run <archivo> --telemetry <salida.csv>`: CSV por paso; columnas en `tools/telemetry/columns.md`.
- `run` repite también las acciones del formato 7 y compara el hash de su línea de tiempo; el CSV de telemetría lleva 133 columnas (con `abs_torque_cut_<rueda>` y `traction_torque_cut`). Para generar la grabación de formato 7 de los tests: `RALLY_WRITE_ACTIONS_REC=<ruta> rally_tests_io -tc="actions_recording: a session*"`.
- `run <archivo> --bench N`: ms por paso, sin y con la captura de la telemetría, alternando las dos en cada repetición, con los 5 pasos más lentos (repetición y paso). Si los pasos más lentos cambian de una repetición a otra, son ruido del sistema y no de la simulación.
- `run tests/data/recordings/bench_lap.rrec --bench 10`: la medida de referencia del coste por paso (120 s del coche congelado en la zona; presupuesto ≤ 0,5 ms de media en Release).
- `rewrite <archivo>`: solo para grabaciones sintéticas de tests.
- `synth <salida.rrec> --car <archivo.car>`: genera `bench_lap.rrec` (120 s de entradas sintéticas deterministas en la zona).
- `zone --json <archivo> [--blind <superficie>]`: trazado de la zona de pruebas para los scripts de Python.
- `columns --md [<archivo>]`: la tabla de columnas.

Scripts en `tools/telemetry` (ver su `README.md`):
- `plot.py <csv> <columnas...>`: curvas frente al tiempo.
- `compare.py <a.csv> <b.csv> <columnas...>`: dos grabaciones superpuestas.
- `drift_check.py <csv> <zona.json>`: la prueba de derrape.

---

## 9. Lista de comprobación del PC nuevo

- [ ] Programas instalados y versiones comprobadas (§3)
- [ ] Repositorio clonado **sin** permisos de administrador
- [ ] Configure, build y ctest en Release y Debug, todo en verde
- [ ] El test de huella pasa sin tocar nada
- [ ] El exe usa la gráfica dedicada (panel F1)
- [ ] Una sola pantalla activa; sincronización vertical en manos de la aplicación
- [ ] Pantalla completa sin bordes sin tearing ni tirones (Tab, conducir, F1 con la gráfica de fotogramas)
- [ ] Claude Code abre el repositorio y lee `CLAUDE.md`
- [ ] Proyecto de claude.ai accesible con los documentos al día
