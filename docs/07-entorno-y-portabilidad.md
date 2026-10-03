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

### 3.2 Herramientas de usuario (PowerShell normal, ventana nueva)

```powershell
python -m pip install --user clang-format==23.1.2
irm https://claude.ai/install.ps1 | iex
```

La versión de clang-format está fijada: otra versión formatea distinto y el código ya formateado empezaría a dar diferencias.

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

**Prompts largos:** la consola de Windows a veces corta lo que pegas. Si Claude Code responde a medias o pregunta por algo que estaba en el prompt, vuelve a pegarlo en dos partes.

---

## 8. Referencia rápida de la demo actual

Válido desde la tarea 2.2 de la fase 2. La demo de la caja es temporal y cambiará con el coche (tarea 2.4). Arranca en la zona de pruebas; K cambia al recinto de la fase 1. Los botones del mando van por posición, sin teclas de depuración.

| Tecla / mando | Acción |
| --- | --- |
| W o ↑ · gatillo derecho | Acelerador |
| S o ↓ · gatillo izquierdo | Freno (con la caja, la lleva hacia atrás hasta la 2.4) |
| A/D o ←/→ · stick izquierdo | Girar |
| Espacio · botón de abajo de la cara derecha (A Xbox, ✕ PlayStation, B Nintendo) | Freno de mano (sin efecto en la caja hasta la 2.6) |
| Q · hombro izquierdo (LB, L1, L) | Bajar marcha (sin efecto hasta la 2.6) |
| E · hombro derecho (RB, R1, R) | Subir marcha (sin efecto hasta la 2.6) |
| R · botón de arriba de la cara derecha (Y Xbox, △ PlayStation, X Nintendo) | Recolocar: en la zona, al punto de recolocación más cercano, parado y con su orientación; en el recinto, a la salida mirando al frente |
| X | Saltar (temporal, solo la caja; desaparece en la 2.4) |
| Retroceso | Reiniciar el mundo (cierra la grabación y empieza otra) |
| Tab | Cámara: primera persona → fija (recinto) o detrás y por encima (zona) → toda la zona |
| K | Zona de pruebas / recinto de la fase 1 (reconstruye el mundo y empieza otra grabación) |
| G | Zona: siguiente superficie de la pista ciega (reconstruye); a ciegas, siguiente prueba de la serie |
| M | Zona: modo a ciegas sí / no (todas las superficies del mismo color, sin microrrelieve ni nombres; en pantalla solo «Prueba k/15») |
| H | Ocultar / mostrar el texto (quedan las vueltas) |
| O | Recinto: mundo a 20 km del origen (y de vuelta) |
| J | Dibujar en float del mundo (depuración del render relativo) |
| F | Frecuencia del monitor / 30 fps |
| V | Sincronización vertical sí / no |
| I | Interpolación sí / no |
| N | Ajuste del dt al refresco sí / no |
| P | Pausa |
| C | Captura del fotograma (`captura_NNN.png` junto al exe) |
| L | Registro por fotograma (`fotogramas_NNN.csv` junto al exe) |
| F1 | Contador de rendimiento (CPU, GPU, draw calls, memoria, gráfica de fotogramas) |
| B | Recinto: escena de medida del bosque: apagado → uno a uno → `DrawMeshInstanced` → persistente |
| F11 o Alt+Intro | Pantalla completa sin bordes |
| Esc | Salir |

| Opción de arranque | Qué hace |
| --- | --- |
| `--fullscreen` | Arranca en pantalla completa sin bordes |
| `--frame-log` | Graba el registro por fotograma desde el inicio |
| `--bench-forest` | Mide los cuatro modos del bosque en el recinto a 1920×1080 sin sincronización vertical, escribe `rendimiento_NNN.txt` junto al exe y se cierra |
| `--bench-zone` | Mide la CPU del dibujo del suelo de la zona en pantalla completa sin bordes, con la cámara por la pista ciega a 100 km/h (3 s de calentamiento y 20 s de medida); escribe `rendimiento_zona_NNN.txt` junto al exe y se cierra |
| `--blind-seed <n>` | Semilla de la serie de 15 pruebas a ciegas (M); por defecto, 1 |

Las grabaciones de cada pasada quedan en `grabaciones/AAAAMMDD-HHMMSS.rrec` junto al exe y el overlay muestra el archivo y los pasos. Se repiten con la herramienta de consola `build/<preset>/tools/replay/<Config>/rally_replay.exe`: `info <archivo>`, `run <archivo>` («hash OK» o el primer problema), `run <archivo> --bench N`, `rewrite <archivo>` (solo grabaciones sintéticas de tests) y `zone --json <archivo> [--blind <superficie>]` (trazado de la zona de pruebas para los scripts de Python). `info` dice la superficie real de la pista ciega y `run`, la mejor vuelta cronometrada.

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
