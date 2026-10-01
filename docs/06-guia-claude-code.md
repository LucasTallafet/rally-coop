# Rally Coop — Empezar a trabajar con Claude Code

Guía para montar el repositorio y trabajar el proyecto con Claude Code en Windows.

## 1. Instalar

1. **Git para Windows:** <https://git-scm.com/downloads/win>. Claude Code lo usa para tener una terminal Bash.
2. **Visual Studio 2022 Community o Build Tools,** con la carga de trabajo "Desarrollo para el escritorio con C++". Incluye el compilador MSVC y CMake.
3. **Python 3.12 o posterior,** marcando "Add python.exe to PATH". Hace falta para la fase 0 y el analizador.
4. **Claude Code.** En PowerShell, sin permisos de administrador:

   ```powershell
   irm https://claude.ai/install.ps1 | iex
   ```

   Abre una terminal nueva y comprueba con `claude --version`. Necesita una cuenta Pro, Max, Team, Enterprise o de Console. Instrucciones oficiales: <https://code.claude.com/docs/en/setup>.

## 2. Crear el repositorio

```powershell
mkdir C:\dev\rally-coop
cd C:\dev\rally-coop
git init
# copia aquí el contenido del kit: CLAUDE.md, .claude\, docs\
git add .
git commit -m "Initial design docs and Claude Code setup"
```

Recomendado: un repositorio privado en GitHub como copia de seguridad.

## 3. Primera sesión

```powershell
cd C:\dev\rally-coop
claude
```

- Escribe `/context` y comprueba que `CLAUDE.md` aparece en la lista de archivos de memoria.
- No hace falta `/init`: el `CLAUDE.md` ya está escrito.
- Las reglas de `.claude/rules/simulacion.md` se cargan solas cuando Claude abre archivos de `src/sim/`, `src/gen/`, `src/net/` o `tests/`.

## 4. Forma de trabajar en cada sesión

1. **Una tarea concreta por sesión,** sacada del plan de la fase actual.
2. **Pide primero un plan.** Ejemplo: "Lee la fase 1 en el plan y propón cómo montar el bucle de paso fijo. No escribas código todavía." Corrige el plan antes de que empiece.
3. **Deja que escriba código y tests,** y que los ejecute.
4. **Revisa el resultado jugando** cuando la tarea tenga algo que se vea o se sienta.
5. **Cierra:** pide que añada la fila al registro de `docs/progreso.md`, pon tus horas y haz commit.

Para mantener el contexto entre sesiones:
- Si Claude repite un error, pídele que añada la regla a `CLAUDE.md`.
- Si una prueba cambia una decisión, pídele que actualice `docs/05-decisiones.md` con fecha y motivo.
- Las conversaciones de diseño largas van mejor en el proyecto de Claude (ver la carpeta `proyecto-claude` del kit). Cuando cambie el diseño allí, actualiza los documentos de `docs/` en el repositorio.

## 5. Primeras tareas

### Fase 0 · Prueba A: mapa en vivo para DiRT Rally 2.0

> Vamos con la prueba A de la fase 0 (ver `docs/04-plan-de-fases.md`). Crea en `prototypes/dirt-map/` un script en Python que:
> 1. reciba la telemetría UDP de DiRT Rally 2.0 con `extradata="3"` en el puerto 20777 (investiga el formato del paquete);
> 2. en modo grabación, guarde las posiciones del coche como trazado;
> 3. en modo copiloto, dibuje el trazado grabado orientado por la carretera, con marcas cada 50 m, y el coche moviéndose encima;
> 4. permita cambiar con una tecla entre orientar el mapa por la tangente y por un punto 30–50 m por delante.
>
> Usa pygame. Propón primero el plan.

### Fase 0 · Prueba B: prototipo 2D a dos jugadores

> Prueba B de la fase 0. En `prototypes/coop-2d/`, un prototipo en Python con pygame y dos procesos conectados por UDP: conductor con vista cenital girada y un cono de 40 m visible, y copiloto con mapa, cursor libre, dial principal, dial de distancias y borrar, tal como están en `docs/01-diseno-de-juego.md`. La carretera se lee de un archivo de texto de primitivas (recta, clotoide, arco). Panel del conductor curva · distancia · curva. Empieza por un plan dividido en pasos pequeños que pueda probar entre medias.

### Fase 1 · Arranque del motor

> Empezamos la fase 1. Propón la estructura de CMake con presets para Windows, cómo traer raylib (backend SDL), Jolt y ENet, y los flags de Jolt y del compilador que piden las reglas de simulación. Incluye el primer test de CTest. Plan primero.

## 6. Qué vigilar

- **Tareas grandes:** si una tarea toca más de un módulo, divídela.
- **Dependencias:** Claude debe preguntar antes de añadir una librería.
- **Reproducibilidad:** cualquier cambio en `src/sim/` debe pasar el test de mismo tiempo con mismas entradas.
- **Rendimiento:** mira el contador en pantalla al final de cada tarea visual, y mide en el PC de referencia (GTX 1060 o similar) al final de cada fase.
