"""Hook de rutas de Rally Coop (docs/09-agentes.md §4 y §6).

Como hook (PreToolUse de Write|Edit|MultiEdit|NotebookEdit, sin argumentos): lee el JSON de la
llamada por la entrada estándar y sale con 0 si el que escribe puede escribir en esa ruta, o con
2 y el motivo por stderr si no. Falla cerrado: cualquier error o excepción sale con 2.

Quién escribe:
- un subagente (campo agent_type): su rol según "agentes" de propiedad.json; otro tipo, nada;
- la sesión principal (sin agent_type): el rol de la variable RALLY_SESION (un carril,
  "integrador" o "libre"); sin ella, solo mensajes/ de la copia principal.

Raíz de la copia en la que se escribe: git rev-parse --show-toplevel del campo cwd. Copia
principal: CLAUDE_PROJECT_DIR (la carpeta del proyecto de la sesión). Carpetas de copias de
trabajo: .claude/worktrees/ de CLAUDE_PROJECT_DIR y de la carpeta padre de su --git-common-dir,
que es donde las crea Claude Code; si la sesión está en la copia principal, son la misma.

Para tools/ci/propiedad.ps1 y los tests, con la misma regla:
- rutas.py --rama <carril> [--tarea <tarea>]: lee rutas del repositorio (una por línea) y
  escribe las que no son del carril; sale con 1 si hay alguna;
- rutas.py --duenos: lee rutas y escribe las que no tienen dueño o tienen dos sin ser
  compartidas; sale con 1 si hay alguna.

Solo biblioteca estándar.
"""

import fnmatch
import json
import os
import subprocess
import sys

PROPIEDAD = os.path.join(os.path.dirname(os.path.abspath(__file__)), "propiedad.json")
HERRAMIENTAS_QUE_ESCRIBEN = ("Write", "Edit", "MultiEdit", "NotebookEdit")


class Bloqueo(Exception):
    """Escritura no permitida, con el motivo para el agente."""


def cargar_propiedad():
    with open(PROPIEDAD, encoding="utf-8") as f:
        return json.load(f)


def coincide(patron, ruta):
    """'carpeta/**' es todo lo que hay debajo; '*' no cruza '/'. Sin distinguir mayúsculas."""
    patron = patron.lower()
    ruta = ruta.lower()
    if patron.endswith("/**"):
        return ruta.startswith(patron[:-2])
    partes_p = patron.split("/")
    partes_r = ruta.split("/")
    if len(partes_p) != len(partes_r):
        return False
    return all(fnmatch.fnmatchcase(r, p) for p, r in zip(partes_p, partes_r))


def alguna(patrones, ruta):
    return any(coincide(p, ruta) for p in patrones)


def rutas_de_dueno(prop, dueno):
    """Patrones de un dueño y sus excepciones."""
    return prop["duenos"][dueno], prop["excepto"].get(dueno, [])


def es_de(prop, dueno, ruta):
    incluye, excluye = rutas_de_dueno(prop, dueno)
    return alguna(incluye, ruta) and not alguna(excluye, ruta)


def es_fragmento(prop, ruta, tarea=None):
    """docs/progreso/<tarea>.md de un carril (nunca el README de la carpeta)."""
    if not coincide(prop["fragmentos"], ruta) or es_de(prop, "consultor", ruta):
        return False
    if tarea is None:
        return True
    return ruta.lower() == prop["fragmentos"].replace("*", tarea).lower()


def puede_carril(prop, carril, ruta, tarea=None):
    return es_de(prop, carril, ruta) or es_fragmento(prop, ruta, tarea)


def puede_integrador(prop, ruta):
    if es_de(prop, "integrador", ruta):
        return True
    return es_de(prop, "consultor", ruta) and not alguna(prop["barreras"], ruta)


def normalizar(ruta):
    """Ruta absoluta, sin '..', con los enlaces resueltos y en minúsculas con '\\' en Windows."""
    return os.path.normcase(os.path.realpath(os.path.abspath(ruta)))


def relativa(ruta, raiz):
    """Ruta relativa a raiz con '/', o None si está fuera (o es la propia raíz)."""
    if ruta == raiz or not ruta.startswith(raiz.rstrip(os.sep) + os.sep):
        return None
    return ruta[len(raiz.rstrip(os.sep)) + 1 :].replace(os.sep, "/")


def git(cwd, *args):
    salida = subprocess.run(
        ["git", "-C", cwd, *args],
        capture_output=True,
        text=True,
        timeout=20,
        check=True,
    )
    return salida.stdout.strip()


def raiz_de(cwd):
    return normalizar(git(cwd, "rev-parse", "--show-toplevel"))


def principal_comun(cwd):
    """Carpeta padre de la carpeta .git común a todas las copias."""
    comun = git(cwd, "rev-parse", "--path-format=absolute", "--git-common-dir")
    return normalizar(os.path.dirname(normalizar(comun)))


def rol_de(entrada, prop):
    tipo = entrada.get("agent_type")
    if tipo:
        if tipo not in prop["agentes"]:
            raise Bloqueo(
                f"el tipo de agente «{tipo}» no puede escribir archivos. Solo escriben los "
                f"agentes de .claude/agents/ en sus rutas (docs/09-agentes.md §4)"
            )
        return prop["agentes"][tipo], f"agente {tipo}"
    sesion = os.environ.get("RALLY_SESION", "")
    if not sesion:
        return "director", "sesión principal sin RALLY_SESION (director)"
    validos = prop["carriles"] + ["integrador", "libre"]
    if sesion not in validos:
        raise Bloqueo(
            f"RALLY_SESION={sesion} no es un rol válido ({', '.join(validos)})"
        )
    return sesion, f"sesión principal con RALLY_SESION={sesion}"


def comprobar(entrada, prop):
    """Lanza Bloqueo si no se puede escribir; vuelve sin más si se puede."""
    if not isinstance(entrada, dict):
        raise Bloqueo("la entrada del hook no es un objeto JSON")
    herramienta = entrada.get("tool_name")
    if herramienta not in HERRAMIENTAS_QUE_ESCRIBEN:
        raise Bloqueo(f"herramienta inesperada: {herramienta!r}")
    datos = entrada.get("tool_input") or {}
    destino = datos.get("file_path") or datos.get("notebook_path")
    cwd = entrada.get("cwd")
    if not destino or not cwd:
        raise Bloqueo("falta la ruta del archivo o el campo cwd en la entrada del hook")

    rol, quien = rol_de(entrada, prop)
    if rol == "libre":
        return

    proyecto = os.environ.get("CLAUDE_PROJECT_DIR")
    if not proyecto:
        raise Bloqueo("falta CLAUDE_PROJECT_DIR: no se sabe cuál es la copia principal")
    principal = normalizar(proyecto)
    if not os.path.isabs(destino):
        destino = os.path.join(cwd, destino)
    ruta = normalizar(destino)
    raiz = raiz_de(cwd)

    def bloquear(permitidas):
        raise Bloqueo(
            f"{quien} no puede escribir en {destino}. Puede escribir en: {permitidas}. "
            f"Si necesitas algo fuera de tus rutas, pídelo en tu fragmento o en tu informe "
            f"(docs/09-agentes.md §4)"
        )

    if rol == "director":
        rel = relativa(ruta, principal)
        if rel is None or not coincide("mensajes/**", rel):
            bloquear(f"solo mensajes/ de la copia principal ({proyecto})")
        return

    if rol == "solo-lectura":
        bloquear("nada (los revisores solo leen y devuelven texto)")

    if rol in prop["carriles"] or rol == "verificador":
        principales = {principal, principal_comun(proyecto)}
        en_principal = raiz in principales | {principal_comun(cwd)}
        if en_principal:
            raise Bloqueo(
                f"{quien} está trabajando en la copia principal ({raiz}), no en su copia de "
                f"trabajo de .claude/worktrees/: no escribe nada. Si su copia no se creó, "
                f"para y díselo a Lucas (docs/09-agentes.md §4)"
            )

    if rol == "verificador":
        # Claude Code crea las copias en .claude/worktrees/ de la copia principal de git, que
        # no es CLAUDE_PROJECT_DIR si la sesión se abrió en otra copia: valen las dos carpetas.
        copias = sorted({normalizar(os.path.join(p, ".claude", "worktrees")) for p in principales})
        zonas = [raiz] if any(relativa(raiz, c) is not None for c in copias) else copias
        # Nunca en una copia principal, aunque esté dentro de .claude/worktrees/ de la otra.
        en_una_principal = any(
            ruta == p
            or (relativa(ruta, p) is not None
                and relativa(ruta, os.path.join(p, ".claude", "worktrees")) is None)
            for p in principales
        )
        if en_una_principal or not any(relativa(ruta, z) is not None for z in zonas):
            bloquear(f"solo su copia de trabajo ({' o '.join(zonas)})")
        return

    rel = relativa(ruta, raiz)
    if rol == "integrador":
        if rel is not None and (coincide("mensajes/**", rel) or puede_integrador(prop, rel)):
            return
        bloquear(
            "sus rutas de .claude/hooks/propiedad.json, los documentos del consultor (no "
            ".claude/, tools/ci/ ni tests/ci/) y mensajes/ de su copia"
        )

    # Un carril (agente o sesión manual con RALLY_SESION).
    if rel is not None and (coincide("mensajes/**", rel) or puede_carril(prop, rol, rel)):
        return
    incluye, excluye = rutas_de_dueno(prop, rol)
    texto = ", ".join(incluye)
    if excluye:
        texto += " (salvo " + ", ".join(excluye) + ")"
    bloquear(f"{texto}, docs/progreso/<tarea>.md y mensajes/ de su copia ({raiz})")


def hook():
    prop = cargar_propiedad()
    try:
        entrada = json.loads(sys.stdin.buffer.read().decode("utf-8"))
    except ValueError as error:
        raise Bloqueo(f"la entrada del hook no es JSON válido: {error}") from None
    comprobar(entrada, prop)
    return 0


def leer_rutas():
    # utf-8-sig: con chcp 65001, PowerShell antepone un BOM a lo que pasa por la tubería.
    texto = sys.stdin.buffer.read().decode("utf-8-sig")
    return [linea.strip().replace("\\", "/") for linea in texto.splitlines() if linea.strip()]


def modo_rama(argumentos):
    prop = cargar_propiedad()
    if len(argumentos) not in (1, 3) or (len(argumentos) == 3 and argumentos[1] != "--tarea"):
        print("uso: rutas.py --rama <carril> [--tarea <tarea>]", file=sys.stderr)
        return 2
    carril = argumentos[0]
    tarea = argumentos[2] if len(argumentos) == 3 else None
    if carril not in prop["carriles"]:
        print(f"carril desconocido: {carril} ({', '.join(prop['carriles'])})", file=sys.stderr)
        return 2
    ajenas = [r for r in leer_rutas() if not puede_carril(prop, carril, r, tarea)]
    for r in ajenas:
        print(r)
    return 1 if ajenas else 0


def modo_duenos():
    prop = cargar_propiedad()
    malas = 0
    for ruta in leer_rutas():
        duenos = [d for d in prop["duenos"] if es_de(prop, d, ruta)]
        if not duenos and es_fragmento(prop, ruta):
            continue  # docs/progreso/<tarea>.md: del carril de su tarea (docs/09-agentes.md §4)
        if not duenos:
            print(f"sin dueño: {ruta}")
            malas += 1
        elif len(duenos) > 1 and not alguna(prop["compartidas"], ruta):
            print(f"{len(duenos)} dueños ({', '.join(duenos)}): {ruta}")
            malas += 1
    return 1 if malas else 0


def main(argumentos):
    if argumentos[:1] == ["--rama"]:
        return modo_rama(argumentos[1:])
    if argumentos == ["--duenos"]:
        return modo_duenos()
    if argumentos:
        print("uso: rutas.py [--rama <carril> [--tarea <tarea>] | --duenos]", file=sys.stderr)
        return 2
    return hook()


if __name__ == "__main__":
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
    sys.stderr.reconfigure(encoding="utf-8", errors="replace")
    try:
        codigo = main(sys.argv[1:])
    except Bloqueo as bloqueo:
        print(f"Hook de rutas: {bloqueo}", file=sys.stderr)
        codigo = 2
    except BaseException as error:  # Falla cerrado ante cualquier error, también los de git.
        print(f"Hook de rutas: error ({type(error).__name__}: {error}); se bloquea", file=sys.stderr)
        codigo = 2
    sys.exit(codigo)
