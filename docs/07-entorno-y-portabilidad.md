# Rally Coop — Entorno de trabajo y portabilidad

Versión del 2 de octubre de 2026, al cierre de la fase 1.

Cómo montar en otro ordenador el mismo entorno con el que se desarrolla Rally Coop: qué hay que instalar, qué viaja con el repositorio y qué no, y cómo comprobar que el PC nuevo da los mismos resultados.

---

## 1. Qué hay y dónde está

| Pieza | Dónde vive | ¿Viaja sola? | Cómo se recupera en otro PC |
| --- | --- | --- | --- |
| Código, tests, CMake, documentos (`docs/`) | GitHub, `LucasTallafet/rally-coop` (privado) | Sí | `git clone` |
| `CLAUDE.md` y `.claude/rules/simulacion.md` | Repositorio | Sí | Vienen con el clon |
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

1. **Una tarea por sesión** de Claude Code: sesión nueva (o `/clear`) para cada tarea.
2. **Plan primero**: pídele el plan sin código, revísalo en el proyecto de claude.ai y apruébalo.
3. Claude Code suele trabajar en una copia aparte (`.claude/worktrees/<tarea>`, ignorada por git). Pruebas el exe de esa copia y, si está bien, la fusiona en `master` y hace push.
4. **Cierre de cada tarea:** tests en verde, fila en `docs/progreso.md` con tus horas, commit y `git push`.
5. **Sincronizar el proyecto de claude.ai** con los documentos que hayan cambiado.
6. **Limpiar las copias de trabajo** cuando estén fusionadas:

```powershell
git worktree list
git worktree remove .claude/worktrees/<carpeta>
git branch -D <rama>
git push origin --delete <rama>
```

Si alguna está bloqueada ("locked" o "en uso por otro proceso"), cierra primero la sesión de Claude Code que la creó y los procesos que la usan, y fuerza:

```powershell
Stop-Process -Name rally, MSBuild, mspdbsrv, VBCSCompiler -ErrorAction SilentlyContinue
git worktree unlock .claude/worktrees/<carpeta>
git worktree remove -f .claude/worktrees/<carpeta>
git worktree prune
```

Si al borrar una copia queda su carpeta vacía «en uso por otro proceso», cierra la sesión de Claude Code que trabajaba en ella y bórrala con `rmdir`.

**Mensajes con el consultor:** los textos largos no se pegan en la consola. El consultor del proyecto de claude.ai los deja como archivos en `mensajes/` de la copia principal (local, ignorada por git), y a Claude Code se le dice «Lee y aplica mensajes/<archivo>». Claude Code escribe sus informes en `mensajes/` de su copia de trabajo (`comun.md` §12).

---

## 8. Referencia rápida de la demo actual

Válido desde la tarea 2.4b de la fase 2: el coche (`data/cars/mid_awd_turbo.car`) en la zona de pruebas; K cambia al recinto de la fase 1. Los botones del mando van por posición, sin teclas de depuración. En pantalla, siempre, el indicador mínimo (velocidad, marcha, rpm, presión del turbo, modo de la caja y la configuración del coche, salvo a ciegas o con `--hide-config`; en la zona, la vuelta en curso y la mejor; a ciegas, «Prueba k/15») y «H: ayuda»; H enseña la ayuda de teclas, F3 el panel de depuración, F1 el rendimiento y T las gráficas, cada uno en su sitio fijo; mientras la ayuda está abierta, F3, F1 y T no se dibujan (desde la 2.5b la ayuda ocupa su sitio). La ayuda de dirección (F6, activada por defecto), la caja automática o secuencial (F2, automática por defecto; desde la 2.6) y la dirección con teclado o ratón (F7; con ratón, X o el botón central recentran y el indicador mínimo enseña el volante) se guardan en `ajustes.txt`, igual que el asiento (Re Pág / Av Pág, Inicio / Fin, Insert / Supr), que se guarda junto al exe; si ese archivo tiene una línea mal escrita, se avisa en pantalla con su número y se usa el valor por defecto. Desde la 2.7, en la vista del ojo se ven el interior provisional, la cabeza que se mueve con el coche (F9, activada por defecto) y suena el coche (F4: volumen 100, 70, 40 y 0 %); los dos se guardan también en `ajustes.txt`. La ayuda (H) va por páginas: H pasa a la siguiente y, tras la última, la oculta. F1 enseña además la CPU del hilo de audio, su llamada más larga y los cortes.

Las tablas de teclas salen de las tablas de cada componente (`src/platform/drive_keys.cpp`, `window_keys.cpp` y las de las partes de la demo, `demo_*_keys.cpp`), que `src/platform/key_tables.cpp` junta por bloques y por su `order`: es la misma tabla que usa el juego y la ayuda en pantalla. El test `keys: docs/07 section 8 matches the generated table` comprueba que coinciden, y `keys: no key is repeated within a context` que ninguna tecla se repite entre tablas. Para cambiarlas, se cambia la tabla del componente y se copia aquí lo que imprime ese test al fallar.

<!-- teclas: generado desde las tablas de src/platform/key_tables.cpp (keys_markdown); no editar a mano -->

**Conducir**

| Tecla / mando | Acción |
| --- | --- |
| W o Arriba · gatillo derecho | Acelerador |
| S o Abajo · gatillo izquierdo | Freno |
| A/D o Izquierda/Derecha · stick izquierdo | Girar |
| Espacio · botón de abajo de la cara derecha (A Xbox, Cruz PlayStation, B Nintendo) | Freno de mano: ruedas traseras; mientras se tira, sin par motor |
| Q · hombro izquierdo (LB, L1, L) | Bajar marcha: caja R, N, 1 a 5: de 1 a N (en automático, solo parado) y de N a R parado (menos de 2 km/h); en secuencial, una marcha (no si pasaría del limitador) |
| E · hombro derecho (RB, R1, R) | Subir marcha: de R a N y de N a 1 (no si pasaría del limitador); en secuencial, una marcha; en automático, desde 1 cambia solo |
| R · botón de arriba de la cara derecha (Y Xbox, Triángulo PlayStation, X Nintendo) | Recolocar: en la zona, al punto de recolocación más cercano, parado y con su orientación; en el recinto, a la salida mirando al frente |
| X | Recentrar volante (ratón): también el botón central del ratón; solo con la dirección con ratón |
| F6 | Ayuda de dirección: la activa o la quita; limita las ruedas delanteras al ángulo de máximo agarre desde 30 km/h; reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt |
| F2 | Caja: automática / secuencial: reconstruye el mundo y empieza otra grabación; se guarda en ajustes.txt |
| F7 | Dirección: teclado / ratón: con ratón, el desplazamiento horizontal es el volante (cursor capturado; se suelta en pausa o al perder el foco y el volante vuelve al centro); A/D no giran; no reconstruye el mundo; se guarda en ajustes.txt |

**Cámara y asiento**

| Tecla / mando | Acción |
| --- | --- |
| Tab | Cámara: ojo del conductor (el del archivo del coche más el asiento), persecución y fija (recinto) o toda la zona, por turnos |
| F9 | Movimiento de la cabeza: sí / no: el ojo se mueve con las aceleraciones del coche (solo la cámara del ojo); se guarda en ajustes.txt |
| Re Pág/Av Pág | Asiento: arriba / abajo: 1 cm por pulsación, se repite al mantener; solo la vista del ojo, no la simulación ni la grabación; límite ± 15 cm en cada eje; se guarda en ajustes.txt junto al exe |
| Inicio/Fin | Asiento: adelante / atrás |
| Insert/Supr | Asiento: a los lados: derecha / izquierda |

**Zona**

| Tecla / mando | Acción |
| --- | --- |
| G | Superficie de la pista ciega: la siguiente; reconstruye el mundo |
| G | A ciegas: siguiente prueba: de la serie de 15 (--blind-seed) |
| M | Modo a ciegas sí / no: todas las superficies del mismo color, sin microrrelieve ni nombres; en pantalla, «Prueba k/15»; cada prueba sale en la pista ciega |
| U | Superficie de la carretera de prueba: grava / asfalto; reconstruye el mundo con el coche en la salida de la carretera |
| Y | Salida: recta / carretera: dónde sale el coche (también al reconstruir el mundo con G, F2, F6 o Retroceso); reconstruye el mundo |

**Grabación**

| Tecla / mando | Acción |
| --- | --- |
| C | Captura del fotograma: captura_NNN.png junto al exe |
| L | Registro por fotograma: fotogramas_NNN.csv junto al exe |

**Depuración**

| Tecla / mando | Acción |
| --- | --- |
| F3 | Panel de depuración: mando, entradas, impacto, coche, grabación, zona, cámara, asiento y ritmo de fotogramas; en el modo a ciegas, sin superficie ni configuración |
| F1 | Contador de rendimiento: CPU, GPU, draw calls, memoria y gráfica de fotogramas |
| T | Gráficas de la telemetría: últimos 10 s, por páginas: 1 · manejo (beta, guiñada, velocidad y entradas aplicadas), 2 · tren motriz, 3 · suspensión (carga y compresión por rueda) y ocultas; las páginas sin canales se saltan; en el modo a ciegas no hace nada |
| O | Recinto a 20 km del origen: y de vuelta |
| B | Bosque de medida (recinto): escena de medida del bosque, por turnos: apagado, uno a uno, DrawMeshInstanced y persistente |
| J | Dibujar en float del mundo: depuración del render relativo |
| F | Monitor / 30 fps: a la frecuencia del monitor o a 30 fps |
| V | Sincronización vertical |
| I | Interpolación |
| N | Ajuste del dt al refresco |

**General**

| Tecla / mando | Acción |
| --- | --- |
| H | Ayuda de teclas: por páginas: página siguiente y, tras la última, oculta |
| P | Pausa |
| F4 | Volumen: 100, 70, 40 y 0 %, por turnos; se guarda en ajustes.txt |
| F5 | Releer el archivo del coche: reconstruye el mundo con el coche en el punto de recolocación más cercano (en el recinto, en su salida); si el archivo tiene un error, lo muestra con su línea y sigue el coche anterior |
| F8 | Configuración siguiente: mid_awd_turbo, front_awd_turbo_5cyl, front_rwd_na y front_awd_turbo_groupa, por turnos, de la carpeta del coche; reconstruye el mundo; a ciegas o con --hide-config, la pantalla no dice cuál |
| Retroceso | Reiniciar el mundo: cierra la grabación y empieza otra |
| K | Zona de pruebas / recinto: el recinto es el de la fase 1; reconstruye el mundo y empieza otra grabación |
| F11 o Alt+Intro | Pantalla completa sin bordes |
| Esc | Salir |

<!-- fin de teclas -->

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
- `info <archivo>`: dice, entre otras cosas, la superficie real de la pista ciega.
- `run <archivo>`: «hash OK» o el primer problema, y la mejor vuelta cronometrada.
- `run <archivo> --telemetry <salida.csv>`: CSV por paso; columnas en `tools/telemetry/columns.md`.
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
