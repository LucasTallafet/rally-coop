"""Tests del hook de rutas (.claude/hooks/rutas.py) y de propiedad.json.

Uso: test_rutas.py hook          todos los casos del hook contra repositorios git temporales
     test_rutas.py rama          el modo --rama que usa tools/ci/propiedad.ps1
     test_rutas.py duenos <repo> toda ruta de git ls-files tiene un dueño (o es compartida)

Lanza el hook como lo lanza Claude Code: un proceso nuevo con el JSON por la entrada estándar.
Todo lo que tiene que bloquear sale con 2. Solo biblioteca estándar.
"""

import json
import os
import shutil
import subprocess
import sys
import tempfile

AQUI = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(AQUI, "..", ".."))
RUTAS = os.path.join(REPO, ".claude", "hooks", "rutas.py")

fallos = []
casos = 0


def lanzar(args, entrada=None, env_extra=None, quitar=()):
    env = dict(os.environ)
    for clave in ("RALLY_SESION", "CLAUDE_PROJECT_DIR", *quitar):
        env.pop(clave, None)
    env.update(env_extra or {})
    datos = entrada if isinstance(entrada, bytes) else json.dumps(entrada).encode("utf-8")
    return subprocess.run(
        [sys.executable, RUTAS, *args], input=datos, capture_output=True, env=env, timeout=60
    )


def esperar(nombre, codigo, resultado, texto=None):
    global casos
    casos += 1
    stderr = resultado.stderr.decode("utf-8", "replace")
    ok = resultado.returncode == codigo and (texto is None or texto in stderr)
    if not ok:
        fallos.append(f"{nombre}: código {resultado.returncode} (esperado {codigo}); stderr: {stderr.strip()}")
    else:
        print(f"ok   {nombre} -> {codigo}")


def git(*args, cwd):
    subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True)


def montar_repos(base):
    """Copia principal con dos copias de trabajo en .claude/worktrees/ y una copia ajena."""
    principal = os.path.join(base, "Principal")
    os.makedirs(principal)
    git("init", "-q", "-b", "integracion", cwd=principal)
    git("-c", "user.name=t", "-c", "user.email=t@t", "commit", "-q", "--allow-empty", "-m", "base", cwd=principal)
    copias = {}
    for nombre in ("w1", "w2"):
        ruta = os.path.join(principal, ".claude", "worktrees", nombre)
        git("worktree", "add", "-q", "-b", nombre, ruta, cwd=principal)
        copias[nombre] = ruta
    otra = os.path.join(base, "Otra")
    os.makedirs(otra)
    git("init", "-q", cwd=otra)
    return principal, copias, otra


def entrada(cwd, ruta, agente=None, herramienta="Write"):
    datos = {
        "hook_event_name": "PreToolUse",
        "tool_name": herramienta,
        "tool_input": {"notebook_path" if herramienta == "NotebookEdit" else "file_path": ruta},
        "cwd": cwd,
    }
    if agente:
        datos["agent_type"] = agente
        datos["agent_id"] = "a1"
    return datos


def variantes_de_unidad(ruta):
    """La misma ruta con la unidad en minúscula y en mayúscula (D: y d:)."""
    if len(ruta) > 1 and ruta[1] == ":":
        return [ruta[0].lower() + ruta[1:], ruta[0].upper() + ruta[1:]]
    return [ruta]


def tests_hook():
    base = tempfile.mkdtemp(prefix="rally_rutas_")
    try:
        principal, copias, otra = montar_repos(base)
        w1, w2 = copias["w1"], copias["w2"]
        env = {"CLAUDE_PROJECT_DIR": principal}

        # Cada carril, en su copia de trabajo: rutas propias y ajenas, relativas y absolutas.
        carriles = {
            "carril-sim": ("src/sim/car.cpp", "src/platform/x.cpp"),
            "carril-plataforma": ("data/roads/a.road", "data/cars/evo.car"),
            "carril-juego": ("src/net/host.cpp", "src/sim/car.cpp"),
            "carril-herramientas": ("tools/replay/main.cpp", "tools/ci/comprobar.ps1"),
        }
        for agente, (propia, ajena) in carriles.items():
            esperar(f"{agente} relativa propia", 0, lanzar([], entrada(w1, propia, agente), env))
            esperar(f"{agente} relativa ajena", 2, lanzar([], entrada(w1, ajena, agente), env), "no puede escribir")
            for unidad in variantes_de_unidad(os.path.join(w1, propia)):
                esperar(f"{agente} absoluta propia {unidad[:2]}", 0, lanzar([], entrada(w1, unidad, agente), env))
            for unidad in variantes_de_unidad(os.path.join(w1, ajena)):
                esperar(f"{agente} absoluta ajena {unidad[:2]}", 2, lanzar([], entrada(w1, unidad, agente), env))
            con_barras = os.path.join(w1, propia).replace("/", "\\")
            esperar(f"{agente} con barras de Windows", 0, lanzar([], entrada(w1, con_barras, agente), env))
            esperar(f"{agente} mayúsculas en la ruta", 0, lanzar([], entrada(w1, propia.upper(), agente), env))
            esperar(f"{agente} fragmento de progreso", 0, lanzar([], entrada(w1, "docs/progreso/2.8.md", agente), env))
            esperar(f"{agente} README de progreso", 2, lanzar([], entrada(w1, "docs/progreso/README.md", agente), env))
            esperar(f"{agente} mensajes de su copia", 0, lanzar([], entrada(w1, "mensajes/2.8-plan.md", agente), env))
            esperar(f"{agente} .. que vuelve a lo suyo", 0, lanzar([], entrada(w1, "docs/../" + propia, agente), env))
            esperar(f"{agente} .. que sale de la copia", 2,
                    lanzar([], entrada(w1, "../w2/" + propia, agente), env))
            esperar(f"{agente} otra copia de trabajo", 2, lanzar([], entrada(w1, os.path.join(w2, propia), agente), env))
            esperar(f"{agente} copia principal", 2,
                    lanzar([], entrada(w1, os.path.join(principal, propia), agente), env))
            esperar(f"{agente} barreras", 2, lanzar([], entrada(w1, ".claude/hooks/rutas.py", agente), env))
            esperar(f"{agente} con la raíz en la principal", 2,
                    lanzar([], entrada(principal, propia, agente), env), "copia principal")
            esperar(f"{agente} raíz en la principal según git, aunque CLAUDE_PROJECT_DIR sea otra", 2,
                    lanzar([], entrada(principal, propia, agente), {"CLAUDE_PROJECT_DIR": w2}), "copia principal")
            esperar(f"{agente} sin CLAUDE_PROJECT_DIR", 2, lanzar([], entrada(w1, propia, agente)))

        esperar("carril-sim regla de simulación", 0,
                lanzar([], entrada(w1, ".claude/rules/simulacion.md", "carril-sim"), env))
        esperar("carril-sim grabaciones", 0, lanzar([], entrada(w1, "tests/data/recordings/a.rrec", "carril-sim"), env))
        esperar("carril-juego grabaciones", 0, lanzar([], entrada(w1, "tests/data/recordings/a.rrec", "carril-juego"), env))
        esperar("carril-plataforma grabaciones", 2,
                lanzar([], entrada(w1, "tests/data/recordings/a.rrec", "carril-plataforma"), env))
        esperar("carril-sim Edit", 0, lanzar([], entrada(w1, "src/sim/car.cpp", "carril-sim", "Edit"), env))
        esperar("carril-sim MultiEdit ajeno", 2, lanzar([], entrada(w1, "src/io/a.cpp", "carril-sim", "MultiEdit"), env))
        esperar("carril-plataforma NotebookEdit propio", 0,
                lanzar([], entrada(w1, "data/a.ipynb", "carril-plataforma", "NotebookEdit"), env))
        esperar("carril-plataforma NotebookEdit ajeno", 2,
                lanzar([], entrada(w1, "src/sim/a.ipynb", "carril-plataforma", "NotebookEdit"), env))
        # Generados (del dueño de su fuente) y datos del juego.
        for agente, ruta, codigo in [
            ("carril-plataforma", "docs/teclas.md", 0),
            ("carril-plataforma", "data/game/actions.txt", 2),
            ("carril-juego", "data/game/actions.txt", 0),
            ("carril-juego", "tools/telemetry/columns.md", 0),
            ("carril-juego", "tools/telemetry/plot.py", 2),
            ("carril-herramientas", "tools/telemetry/columns.md", 2),
            ("carril-herramientas", "tools/telemetry/plot.py", 0),
        ]:
            esperar(f"{agente} {ruta}", codigo, lanzar([], entrada(w1, ruta, agente), env))
        esperar("carril-sim fuera de todo repositorio", 2,
                lanzar([], entrada(w1, os.path.join(otra, "src/sim/a.cpp"), "carril-sim"), env))
        # Una unión de Windows (o un enlace) dentro de la copia que apunta a otra copia.
        enlace = os.path.join(w1, "src", "sim", "puente")
        os.makedirs(os.path.dirname(enlace), exist_ok=True)
        hecho = subprocess.run(["cmd", "/c", "mklink", "/J", enlace, w2], capture_output=True) if os.name == "nt" else None
        if hecho is not None and hecho.returncode == 0:
            esperar("carril-sim a través de una unión a otra copia", 2,
                    lanzar([], entrada(w1, os.path.join(enlace, "src/sim/a.cpp"), "carril-sim"), env))
            os.rmdir(enlace)

        # Integrador: lo suyo y los documentos del consultor, nunca las barreras.
        for ruta, codigo in [
            ("cmake/Dependencies.cmake", 0), ("docs/progreso.md", 0), ("docs/05-decisiones.md", 0),
            ("CLAUDE.md", 0), ("docs/prompts/fase-2/comun.md", 0), ("mensajes/lote-1/prueba.md", 0),
            ("docs/manejo/protocolo.md", 0), ("README.md", 0),
            (".claude/hooks/rutas.py", 2), (".claude/settings.json", 2), (".claude/agents/integrador.md", 2),
            ("tools/ci/promover.ps1", 2), ("tests/ci/test_rutas.py", 2), ("src/sim/car.cpp", 2),
            ("tools/replay/main.cpp", 2), ("docs/teclas.md", 2),
        ]:
            esperar(f"integrador {ruta}", codigo, lanzar([], entrada(principal, ruta, "integrador"), env))
        esperar("integrador ruta absoluta con unidad en minúscula", 0,
                lanzar([], entrada(principal, variantes_de_unidad(os.path.join(principal, "docs", "progreso.md"))[0],
                                   "integrador"), env))

        # Verificador: en su copia; nunca en la principal ni en otra copia.
        esperar("verificador en su copia", 0, lanzar([], entrada(w1, "src/sim/car.cpp", "verificador"), env))
        esperar("verificador en otra copia", 2,
                lanzar([], entrada(w1, os.path.join(w2, "src/sim/car.cpp"), "verificador"), env))
        esperar("verificador en la principal", 2,
                lanzar([], entrada(w1, os.path.join(principal, "src/sim/car.cpp"), "verificador"), env))
        esperar("verificador con la raíz en la principal", 2,
                lanzar([], entrada(principal, "src/sim/car.cpp", "verificador"), env), "copia principal")
        # Con la raíz fuera de .claude/worktrees/ (una copia de trabajo en otra carpeta), en
        # cualquier copia de .claude/worktrees/, pero nunca en la principal ni fuera de ellas.
        fuera = os.path.join(base, "Fuera")
        git("worktree", "add", "-q", "-b", "fuera", fuera, cwd=principal)
        esperar("verificador fuera de .claude/worktrees, en una copia", 0,
                lanzar([], entrada(fuera, os.path.join(w2, "src/sim/car.cpp"), "verificador"), env))
        esperar("verificador fuera de .claude/worktrees, en la principal", 2,
                lanzar([], entrada(fuera, os.path.join(principal, "src/sim/car.cpp"), "verificador"), env))
        esperar("verificador fuera de .claude/worktrees, en su propia carpeta", 2,
                lanzar([], entrada(fuera, "src/sim/car.cpp", "verificador"), env))
        esperar("verificador en otro repositorio", 2,
                lanzar([], entrada(otra, os.path.join(w2, "src/sim/car.cpp"), "verificador"), env), "copia principal")
        # Sesión abierta en una copia (w2): Claude Code crea las copias de los agentes en
        # .claude/worktrees/ de la principal de git, que no está en w2/.claude/worktrees/.
        sesion = {"CLAUDE_PROJECT_DIR": w2}
        esperar("sesión en otra copia: verificador en su copia", 0,
                lanzar([], entrada(w1, "src/sim/car.cpp", "verificador"), sesion))
        esperar("sesión en otra copia: verificador en su copia, ruta absoluta", 0,
                lanzar([], entrada(w1, os.path.join(w1, "src/sim/car.cpp"), "verificador"), sesion))
        esperar("sesión en otra copia: verificador en la copia de la sesión", 2,
                lanzar([], entrada(w1, os.path.join(w2, "src/sim/car.cpp"), "verificador"), sesion))
        esperar("sesión en otra copia: verificador en la principal de git", 2,
                lanzar([], entrada(w1, os.path.join(principal, "src/sim/car.cpp"), "verificador"), sesion))
        esperar("sesión en otra copia: verificador fuera de todo", 2,
                lanzar([], entrada(w1, os.path.join(base, "x.txt"), "verificador"), sesion))
        esperar("sesión en otra copia: verificador fuera de .claude/worktrees, en una copia", 0,
                lanzar([], entrada(fuera, os.path.join(w1, "src/sim/car.cpp"), "verificador"), sesion))
        esperar("sesión en otra copia: verificador fuera de .claude/worktrees, en la de la sesión", 2,
                lanzar([], entrada(fuera, os.path.join(w2, "src/sim/car.cpp"), "verificador"), sesion))
        esperar("sesión en otra copia: verificador con la raíz en la copia de la sesión", 2,
                lanzar([], entrada(w2, "src/sim/car.cpp", "verificador"), sesion), "copia principal")

        # Revisores: solo leen.
        for agente in ("revisor-diseno", "revisor-determinismo"):
            esperar(f"{agente} escribe", 2, lanzar([], entrada(w1, "mensajes/a.md", agente), env))

        # Otros tipos de agente.
        for agente in ("general-purpose", "Explore", "fork", "carril-simx", "Plan"):
            esperar(f"agente desconocido {agente}", 2,
                    lanzar([], entrada(w1, "mensajes/a.md", agente), env), "no puede escribir archivos")

        # Sesión principal sin agent_type.
        esperar("director en mensajes de la principal", 0, lanzar([], entrada(principal, "mensajes/lote-1/estado.md"), env))
        esperar("director en mensajes, ruta absoluta", 0,
                lanzar([], entrada(principal, os.path.join(principal, "mensajes", "a.md")), env))
        esperar("director en mensajes, desde una copia", 0,
                lanzar([], entrada(w1, os.path.join(principal, "mensajes", "a.md")), env))
        esperar("director en el código", 2, lanzar([], entrada(principal, "src/sim/car.cpp"), env), "director")
        esperar("director en mensajes de una copia", 2, lanzar([], entrada(w1, "mensajes/a.md"), env))
        esperar("director con mensajes/.. ", 2, lanzar([], entrada(principal, "mensajes/../CLAUDE.md"), env))
        con_rol = lambda rol: {**env, "RALLY_SESION": rol}
        esperar("RALLY_SESION=juego en su ruta", 0, lanzar([], entrada(w1, "src/game/a.cpp"), con_rol("juego")))
        esperar("RALLY_SESION=juego fuera", 2, lanzar([], entrada(w1, "src/sim/a.cpp"), con_rol("juego")))
        esperar("RALLY_SESION=juego en la principal", 2,
                lanzar([], entrada(principal, "src/game/a.cpp"), con_rol("juego")), "copia principal")
        esperar("RALLY_SESION=integrador", 0, lanzar([], entrada(principal, "docs/02-arquitectura.md"), con_rol("integrador")))
        esperar("RALLY_SESION=integrador barreras", 2,
                lanzar([], entrada(principal, "tools/ci/pre-push"), con_rol("integrador")))
        esperar("RALLY_SESION=libre", 0, lanzar([], entrada(principal, ".claude/hooks/rutas.py"), con_rol("libre")))
        esperar("RALLY_SESION=libre fuera del repositorio", 0,
                lanzar([], entrada(principal, os.path.join(base, "x.txt")), con_rol("libre")))
        esperar("RALLY_SESION=verificador (no es un rol de sesión)", 2,
                lanzar([], entrada(w1, "src/sim/a.cpp"), con_rol("verificador")), "no es un rol válido")
        esperar("RALLY_SESION desconocida", 2, lanzar([], entrada(w1, "mensajes/a.md"), con_rol("todo")))
        esperar("subagente con RALLY_SESION=libre sigue con su rol", 2,
                lanzar([], entrada(w1, "src/sim/a.cpp", "carril-juego"), con_rol("libre")))

        # Entradas rotas.
        esperar("entrada que no es JSON", 2, lanzar([], b"esto no es json"), "no es JSON")
        esperar("entrada vacía", 2, lanzar([], b""))
        esperar("JSON que no es un objeto", 2, lanzar([], b"[1, 2]", env))
        esperar("sin ruta", 2, lanzar([], {"tool_name": "Write", "tool_input": {}, "cwd": w1}, env))
        esperar("sin cwd", 2, lanzar([], {"tool_name": "Write", "tool_input": {"file_path": "a"}}, env))
        esperar("herramienta inesperada", 2, lanzar([], entrada(w1, "mensajes/a.md", None, "Bash"), env))
        esperar("cwd que no es un repositorio", 2,
                lanzar([], entrada(base, "src/sim/a.cpp", "carril-sim"), env), "error")
        esperar("cwd que no existe", 2,
                lanzar([], entrada(os.path.join(base, "no_existe"), "src/sim/a.cpp", "carril-sim"), env))
        esperar("argumento desconocido", 2, lanzar(["--otro"], b""))
    finally:
        subprocess.run(["git", "worktree", "prune"], cwd=base, capture_output=True)
        shutil.rmtree(base, ignore_errors=True)


def tests_rama():
    rutas = "\n".join(["src/sim/car.cpp", "tests/sim/test_car.cpp", "docs/progreso/2.8.md",
                       "tests/data/recordings/a.rrec"]).encode()
    esperar("rama sim limpia", 0, lanzar(["--rama", "sim", "--tarea", "2.8"], rutas))
    r = lanzar(["--rama", "sim", "--tarea", "2.9"], rutas)
    esperar("rama sim con el fragmento de otra tarea", 1, r)
    if b"docs/progreso/2.8.md" not in r.stdout:
        fallos.append("rama sim: la lista no nombra docs/progreso/2.8.md")
    r = lanzar(["--rama", "juego"], b"src/game/a.cpp\nsrc\\sim\\car.cpp\nCMakeLists.txt\n")
    esperar("rama juego con rutas ajenas", 1, r)
    if r.stdout.decode().split() != ["src/sim/car.cpp", "CMakeLists.txt"]:
        fallos.append(f"rama juego: lista inesperada {r.stdout!r}")
    esperar("rama herramientas tools/ci", 1, lanzar(["--rama", "herramientas"], b"tools/ci/promover.ps1\n"))
    esperar("rama de carril desconocido", 2, lanzar(["--rama", "integrador"], b"a\n"))
    esperar("rama sin carril", 2, lanzar(["--rama"], b"a\n"))


def tests_duenos(repo):
    listado = subprocess.run(["git", "-c", "core.quotepath=off", "ls-files"], cwd=repo,
                             capture_output=True, check=True).stdout
    r = lanzar(["--duenos"], listado)
    esperar(f"toda ruta de git ls-files tiene dueño ({len(listado.splitlines())} rutas)", 0, r)
    if r.returncode != 0:
        fallos.append(r.stdout.decode("utf-8", "replace"))
    esperar("una ruta sin dueño se detecta", 1, lanzar(["--duenos"], b"src/nuevo/a.cpp\n"))
    esperar("una ruta compartida (sim y juego) tiene dos dueños y vale", 0,
            lanzar(["--duenos"], b"tests/data/recordings/a.rrec\n"))


def main():
    modo = sys.argv[1] if len(sys.argv) > 1 else ""
    if modo == "hook":
        tests_hook()
    elif modo == "rama":
        tests_rama()
    elif modo == "duenos" and len(sys.argv) == 3:
        tests_duenos(sys.argv[2])
    else:
        print(__doc__)
        return 2
    print(f"{casos} casos, {len(fallos)} fallos")
    for fallo in fallos:
        print("FALLO", fallo)
    return 1 if fallos else 0


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    sys.exit(main())
