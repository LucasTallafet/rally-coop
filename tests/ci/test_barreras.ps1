<#
.SYNOPSIS
Tests de las barreras de push (instalar-hooks.ps1, pre-push, promover.ps1) y de propiedad.ps1,
con un remoto de prueba (git init --bare) en una carpeta temporal. Los lanza ctest.

.DESCRIPTION
test_barreras.ps1 push       pushurl, pre-push: master, forzados y borrados
test_barreras.ps1 promover   promover.ps1 -WhatIf, los tres push y el push atómico
test_barreras.ps1 propiedad  propiedad.ps1 con una rama limpia y otra con una ruta ajena
Sale con 0 si todo pasa y con 1 si algo falla.
#>
param([Parameter(Mandatory = $true)][ValidateSet('push', 'promover', 'propiedad')][string]$Modo)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$ci = (Resolve-Path (Join-Path $PSScriptRoot '..\..\tools\ci')).Path
$script:fallos = 0
$script:casos = 0

function Esperar([string]$nombre, [bool]$ok, [string]$detalle = '') {
    $script:casos++
    if ($ok) { Write-Host "ok   $nombre" } else { Write-Host "FALLO $nombre $detalle"; $script:fallos++ }
}

# Lanza git sin que un código distinto de 0 pare el script; devuelve código y salida juntos.
function G {
    $antes = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try { $salida = (& git @args 2>&1 | Out-String) } finally { $ErrorActionPreference = $antes }
    [pscustomobject]@{ Codigo = $LASTEXITCODE; Salida = $salida }
}

function Lanzar([string]$script, [string[]]$argumentos) {
    $antes = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $salida = (& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $ci $script) @argumentos 2>&1 | Out-String)
    } finally { $ErrorActionPreference = $antes }
    [pscustomobject]@{ Codigo = $LASTEXITCODE; Salida = $salida }
}

function Ref([string]$repo, [string]$ref) {
    $r = G -C $repo rev-parse --verify --quiet $ref
    if ($r.Codigo -ne 0) { return '' }
    return $r.Salida.Trim()
}

function Commit([string]$repo, [string]$archivo, [string]$texto) {
    $ruta = Join-Path $repo $archivo
    New-Item -ItemType Directory -Force -Path (Split-Path $ruta) | Out-Null
    [IO.File]::WriteAllText($ruta, $texto)
    G -C $repo add -- $archivo | Out-Null
    $r = G -C $repo -c user.name=t -c user.email=t@t commit -q -m "cambia $archivo"
    if ($r.Codigo -ne 0) { throw "commit: $($r.Salida)" }
}

# Remoto vacío y un repositorio con master empujado antes de cerrar los push.
function Montar([string]$base) {
    $remoto = Join-Path $base 'remoto.git'
    $repo = Join-Path $base 'repo'
    G init -q --bare -b master $remoto | Out-Null
    G init -q -b master $repo | Out-Null
    Commit $repo 'README.md' 'base'
    G -C $repo remote add origin $remoto | Out-Null
    $r = G -C $repo push -q origin master
    if ($r.Codigo -ne 0) { throw "push inicial: $($r.Salida)" }
    G -C $repo fetch -q origin | Out-Null
    G -C $repo branch integracion master | Out-Null
    $r = Lanzar 'instalar-hooks.ps1' @('-Repositorio', $repo)
    if ($r.Codigo -ne 0) { throw "instalar-hooks: $($r.Salida)" }
    return $remoto, $repo
}

function ConPromover([scriptblock]$bloque) {
    $env:RALLY_PROMOVER = '1'
    try { & $bloque } finally { Remove-Item Env:RALLY_PROMOVER -ErrorAction SilentlyContinue }
}

$base = Join-Path ([IO.Path]::GetTempPath()) ("rally-barreras-{0}-{1}" -f $Modo, $PID)
Remove-Item Env:RALLY_PROMOVER -ErrorAction SilentlyContinue
try {
    New-Item -ItemType Directory -Force -Path $base | Out-Null
    $remoto, $repo = Montar $base
    $masterRemoto = Ref $remoto 'refs/heads/master'

    if ($Modo -eq 'push') {
        Esperar 'pushurl es no-push' ((G -C $repo config --get remote.origin.pushurl).Salida.Trim() -eq 'no-push')
        Commit $repo 'a.txt' 'a'

        $r = G -C $repo push origin master
        Esperar 'git push origin master falla por la URL' ($r.Codigo -ne 0 -and (Ref $remoto 'refs/heads/master') -eq $masterRemoto) $r.Salida
        $r = G -C $repo push origin master:otra
        Esperar 'git push origin a otra rama también falla' ($r.Codigo -ne 0 -and -not (Ref $remoto 'refs/heads/otra')) $r.Salida

        $r = G -C $repo push $remoto master
        Esperar 'URL explícita a master sin RALLY_PROMOVER: rechazado' ($r.Codigo -ne 0 -and $r.Salida -match 'promover.ps1' -and (Ref $remoto 'refs/heads/master') -eq $masterRemoto) $r.Salida
        $r = G -C $repo push $remoto HEAD:refs/heads/master
        Esperar 'URL explícita con HEAD:refs/heads/master: rechazado' ($r.Codigo -ne 0 -and (Ref $remoto 'refs/heads/master') -eq $masterRemoto) $r.Salida

        $r = ConPromover { G -C $repo push $remoto master }
        Esperar 'URL explícita a master con RALLY_PROMOVER=1: entra' ($r.Codigo -eq 0 -and (Ref $remoto 'refs/heads/master') -eq (Ref $repo 'refs/heads/master')) $r.Salida

        # Forzado: master vuelve atrás y avanza por otro lado.
        G -C $repo reset -q --hard HEAD~1 | Out-Null
        Commit $repo 'b.txt' 'b'
        $masterRemoto = Ref $remoto 'refs/heads/master'
        $r = ConPromover { G -C $repo push --force $remoto master }
        Esperar 'push forzado a master con RALLY_PROMOVER=1: rechazado' ($r.Codigo -ne 0 -and $r.Salida -match 'avance r' -and (Ref $remoto 'refs/heads/master') -eq $masterRemoto) $r.Salida

        G -C $repo switch -q -c tarea | Out-Null
        $r = G -C $repo push $remoto tarea
        Esperar 'rama nueva por URL explícita: entra' ($r.Codigo -eq 0 -and (Ref $remoto 'refs/heads/tarea')) $r.Salida
        Commit $repo 'c.txt' 'c'
        $r = G -C $repo push $remoto tarea
        Esperar 'avance rápido de otra rama: entra' ($r.Codigo -eq 0 -and (Ref $remoto 'refs/heads/tarea') -eq (Ref $repo 'refs/heads/tarea')) $r.Salida
        # tarea vuelve atrás y avanza por otro lado: el remoto ya no es antecesor.
        G -C $repo reset -q --hard HEAD~1 | Out-Null
        Commit $repo 'd.txt' 'd'
        $tareaRemota = Ref $remoto 'refs/heads/tarea'
        $r = G -C $repo push --force $remoto tarea
        Esperar 'push forzado a otra rama: rechazado' ($r.Codigo -ne 0 -and (Ref $remoto 'refs/heads/tarea') -eq $tareaRemota) $r.Salida
        $r = G -C $repo push $remoto '+tarea:tarea'
        Esperar 'push forzado con + a otra rama: rechazado' ($r.Codigo -ne 0 -and (Ref $remoto 'refs/heads/tarea') -eq $tareaRemota) $r.Salida
        $r = G -C $repo push $remoto ':tarea'
        Esperar 'borrar una rama del remoto: rechazado' ($r.Codigo -ne 0 -and (Ref $remoto 'refs/heads/tarea') -eq $tareaRemota) $r.Salida
        $r = G -C $repo push --delete $remoto tarea
        Esperar 'push --delete: rechazado' ($r.Codigo -ne 0 -and (Ref $remoto 'refs/heads/tarea') -eq $tareaRemota) $r.Salida
    }

    if ($Modo -eq 'promover') {
        G -C $repo switch -q integracion | Out-Null
        Commit $repo 'src/sim/a.cpp' 'lote 1'
        G -C $repo branch -q 'lote-1/2.8' integracion | Out-Null
        G -C $repo branch -q 'lote-1/2.9' integracion | Out-Null

        $r = Lanzar 'promover.ps1' @('-WhatIf', '-Repositorio', $repo)
        $ok = $r.Codigo -eq 0 -and $r.Salida -match 'integracion:refs/heads/integracion' -and
            $r.Salida -match 'integracion:refs/heads/master' -and $r.Salida -match 'lote-1/2.8' -and
            $r.Salida -match 'lote-1/2.9' -and $r.Salida -match '--atomic' -and
            (Ref $remoto 'refs/heads/master') -eq $masterRemoto -and -not (Ref $remoto 'refs/heads/integracion')
        Esperar 'promover -WhatIf dice los push y no empuja' $ok $r.Salida

        $r = Lanzar 'promover.ps1' @('-Repositorio', $repo)
        $integ = Ref $repo 'refs/heads/integracion'
        $ok = $r.Codigo -eq 0 -and (Ref $remoto 'refs/heads/master') -eq $integ -and
            (Ref $remoto 'refs/heads/integracion') -eq $integ -and (Ref $remoto 'refs/heads/lote-1/2.8') -eq $integ -and
            (Ref $remoto 'refs/heads/lote-1/2.9') -eq $integ
        Esperar 'promover empuja integracion, master y las ramas del lote' $ok $r.Salida

        $r = Lanzar 'promover.ps1' @('-WhatIf', '-Repositorio', $repo)
        Esperar 'promover -WhatIf después: ninguna rama del lote nueva' ($r.Codigo -eq 0 -and $r.Salida -match 'nuevas en el remoto: ninguna') $r.Salida

        # integracion por detrás de origin/master: no es un avance rápido.
        $otro = Join-Path $base 'otro'
        G clone -q $remoto $otro | Out-Null
        Commit $otro 'm.txt' 'm'
        $r = ConPromover { G -C $otro push -q origin master }
        if ($r.Codigo -ne 0) { throw "push de otro: $($r.Salida)" }
        $masterRemoto = Ref $remoto 'refs/heads/master'
        Commit $repo 'src/sim/b.cpp' 'lote 2'
        $r = Lanzar 'promover.ps1' @('-Repositorio', $repo)
        Esperar 'promover con master del remoto por delante: no empuja' ($r.Codigo -ne 0 -and $r.Salida -match 'avance r' -and (Ref $remoto 'refs/heads/master') -eq $masterRemoto) $r.Salida

        # Atómico: master es un avance rápido, pero integracion del remoto ha divergido. Con el
        # pre-push lo rechaza el hook; sin él, lo rechaza el remoto y --atomic deja master quieto.
        G -C $repo merge -q --no-edit origin/master | Out-Null
        G -C $otro fetch -q origin | Out-Null
        G -C $otro switch -q -c integracion origin/integracion | Out-Null
        Commit $otro 'x.txt' 'diverge'
        $r = G -C $otro push -q origin integracion
        if ($r.Codigo -ne 0) { throw "push de integracion divergente: $($r.Salida)" }
        $masterRemoto = Ref $remoto 'refs/heads/master'
        $integRemota = Ref $remoto 'refs/heads/integracion'
        $r = Lanzar 'promover.ps1' @('-Repositorio', $repo)
        Esperar 'promover con integracion del remoto divergida: rechazado, master quieto' ($r.Codigo -ne 0 -and (Ref $remoto 'refs/heads/master') -eq $masterRemoto -and (Ref $remoto 'refs/heads/integracion') -eq $integRemota) $r.Salida
        $comun = (G -C $repo rev-parse --path-format=absolute --git-common-dir).Salida.Trim()
        Remove-Item -LiteralPath (Join-Path $comun 'hooks\pre-push')
        $r = Lanzar 'promover.ps1' @('-Repositorio', $repo)
        Esperar 'sin pre-push, el remoto rechaza integracion y --atomic deja master quieto' ($r.Codigo -ne 0 -and (Ref $remoto 'refs/heads/master') -eq $masterRemoto -and (Ref $remoto 'refs/heads/integracion') -eq $integRemota) $r.Salida
    }

    if ($Modo -eq 'propiedad') {
        # El lote, como en la copia principal: en mensajes/, fuera de git.
        New-Item -ItemType Directory -Force -Path (Join-Path $repo 'mensajes') | Out-Null
        [IO.File]::WriteAllText((Join-Path $repo 'mensajes\lote-1.md'),
            "| 2.8 Ayudas | carril-sim | 1 |`n| 2.9 Acciones | carril-juego | 2 |`n| 2.80 Otra | carril-plataforma | 3 |`n")
        [IO.File]::WriteAllText((Join-Path $repo '.git\info\exclude'), "mensajes/`n")
        G -C $repo switch -q integracion | Out-Null
        G -C $repo switch -q -c 'lote-1/2.8' | Out-Null
        Commit $repo 'src/sim/assists.cpp' 'ayudas'
        Commit $repo 'docs/progreso/2.8.md' 'fragmento'
        $r = Lanzar 'propiedad.ps1' @('lote-1/2.8', '-Repositorio', $repo)
        Esperar 'rama limpia (carril del lote): 0' ($r.Codigo -eq 0 -and $r.Salida -match 'carril sim') $r.Salida
        $r = Lanzar 'propiedad.ps1' @('lote-1/2.8', '-Carril', 'juego', '-Repositorio', $repo)
        Esperar 'la misma rama como carril juego: 1' ($r.Codigo -eq 1 -and $r.Salida -match 'src/sim/assists.cpp') $r.Salida

        G -C $repo switch -q integracion | Out-Null
        G -C $repo switch -q -c 'lote-1/2.9' | Out-Null
        Commit $repo 'src/game/actions.cpp' 'acciones'
        # Una ruta ajena escrita con Bash, como la escribiría un agente saltándose el hook.
        Push-Location $repo
        try { & bash -c 'mkdir -p src/platform && echo x > src/platform/ajeno.cpp' 2>&1 | Out-Null } finally { Pop-Location }
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path (Join-Path $repo 'src\platform\ajeno.cpp'))) { throw 'bash no ha escrito el archivo' }
        G -C $repo add -A | Out-Null
        G -C $repo -c user.name=t -c user.email=t@t commit -q -m 'ajeno' | Out-Null
        $r = Lanzar 'propiedad.ps1' @('lote-1/2.9', '-Repositorio', $repo)
        Esperar 'rama con una ruta ajena escrita con Bash: 1 y la lista' ($r.Codigo -eq 1 -and $r.Salida -match 'src/platform/ajeno.cpp' -and $r.Salida -notmatch 'actions.cpp') $r.Salida

        G -C $repo switch -q -c 'otra-rama' integracion | Out-Null
        Commit $repo 'tools/ci/x.ps1' 'barrera'
        $r = Lanzar 'propiedad.ps1' @('otra-rama', '-Repositorio', $repo)
        Esperar 'rama sin lote ni -Carril: 2' ($r.Codigo -eq 2) $r.Salida
        $r = Lanzar 'propiedad.ps1' @('otra-rama', '-Carril', 'herramientas', '-Repositorio', $repo)
        Esperar 'tools/ci en el carril herramientas: 1' ($r.Codigo -eq 1 -and $r.Salida -match 'tools/ci/x.ps1') $r.Salida
        $r = Lanzar 'propiedad.ps1' @('lote-1/2.8', '-Base', 'no-existe', '-Repositorio', $repo)
        Esperar 'base que no existe: 2' ($r.Codigo -eq 2) $r.Salida
        # Sin -Repositorio, el del script (este repositorio): una rama que no existe da 2 con su
        # mensaje, no un error de PowerShell.
        $r = Lanzar 'propiedad.ps1' @('no-existe-esta-rama', '-Carril', 'sim', '-Base', 'HEAD')
        Esperar 'sin -Repositorio usa el del script' ($r.Codigo -eq 2 -and $r.Salida -match 'no existe la rama no-existe-esta-rama') $r.Salida
    }
} finally {
    Remove-Item Env:RALLY_PROMOVER -ErrorAction SilentlyContinue
    Set-Location $PSScriptRoot
    Remove-Item -Recurse -Force -LiteralPath $base -ErrorAction SilentlyContinue
}

Write-Host "$script:casos casos, $script:fallos fallos"
if ($script:fallos -gt 0 -or $script:casos -eq 0) { exit 1 }
exit 0
