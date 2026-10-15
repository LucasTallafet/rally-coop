<#
.SYNOPSIS
Promueve un lote jugado: empuja integracion, integracion a master y las ramas lote-* nuevas en un
solo push atómico (docs/09-agentes.md §6). Solo lo ejecuta Lucas.

.DESCRIPTION
1. git fetch origin y comprueba que integracion es un avance rápido sobre origin/master.
2. Con la URL de origin explícita (remote.origin.url; pushurl está en no-push) y RALLY_PROMOVER=1
   solo para esa orden, git push --atomic de:
   - integracion;
   - integracion:master;
   - las ramas lote-* locales que no están en el remoto.
   Atómico: si una referencia no entra (por ejemplo, integracion no es un avance rápido sobre la
   del remoto), no entra ninguna y master no se mueve.
Con -WhatIf solo dice lo que haría. Sale con 0 si todo ha ido bien y con 1 si no.
#>
param(
    # Dice lo que haría, sin empujar.
    [switch]$WhatIf,
    # URL a la que se empuja; por defecto, remote.origin.url (los tests usan un remoto local).
    [string]$Remoto = '',
    # Repositorio desde el que se promueve; por defecto, el de este script.
    [string]$Repositorio = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

# $PSScriptRoot está vacío en los valores por defecto de param en PowerShell 5.1.
if (-not $Repositorio) { $Repositorio = Join-Path $PSScriptRoot '..\..' }

function GitOk {
    & git -C $script:repo @args
    if ($LASTEXITCODE -ne 0) { throw "git $($args -join ' ') ha fallado ($LASTEXITCODE)" }
}

function Sale([string]$texto) {
    Write-Host "promover: $texto"
    exit 1
}

$script:repo = (Resolve-Path -LiteralPath $Repositorio).Path
if (-not $Remoto) { $Remoto = (& git -C $repo config --get remote.origin.url) }
if (-not $Remoto) { Sale 'no hay remote.origin.url' }

& git -C $repo rev-parse --verify --quiet refs/heads/integracion | Out-Null
if ($LASTEXITCODE -ne 0) { Sale 'no existe la rama integracion' }

GitOk fetch --quiet origin
& git -C $repo rev-parse --verify --quiet refs/remotes/origin/master | Out-Null
if ($LASTEXITCODE -ne 0) { Sale 'no existe origin/master (git fetch)' }
& git -C $repo merge-base --is-ancestor refs/remotes/origin/master refs/heads/integracion
if ($LASTEXITCODE -ne 0) {
    Sale 'integracion no es un avance rápido sobre origin/master. No fuerces nada: díselo al director.'
}

$refs = @('refs/heads/integracion:refs/heads/integracion', 'refs/heads/integracion:refs/heads/master')
$lotes = @(GitOk for-each-ref --format='%(refname:short)' 'refs/heads/lote-*' 'refs/heads/lote-*/**')
$nuevas = @()
foreach ($rama in ($lotes | Sort-Object -Unique)) {
    & git -C $repo rev-parse --verify --quiet "refs/remotes/origin/$rama" | Out-Null
    if ($LASTEXITCODE -ne 0) {
        $nuevas += $rama
        $refs += "refs/heads/${rama}:refs/heads/$rama"
    }
}

$cambios = @(GitOk log --oneline 'refs/remotes/origin/master..refs/heads/integracion')
Write-Host ("promover: master avanza {0} commits hasta integracion ({1})" -f $cambios.Count,
    (GitOk rev-parse --short refs/heads/integracion))
Write-Host ("promover: ramas del lote nuevas en el remoto: {0}" -f $(if ($nuevas) { $nuevas -join ', ' } else { 'ninguna' }))
Write-Host "promover: git push --atomic $Remoto $($refs -join ' ')"
if ($WhatIf) {
    Write-Host 'promover: -WhatIf, no se ha empujado nada'
    exit 0
}

$antes = $env:RALLY_PROMOVER
$env:RALLY_PROMOVER = '1'
try {
    & git -C $repo push --atomic $Remoto @refs
    $codigo = $LASTEXITCODE
} finally {
    $env:RALLY_PROMOVER = $antes
}
if ($codigo -ne 0) { Sale "el push ha fallado ($codigo): no se ha movido nada del remoto" }
GitOk fetch --quiet origin
Write-Host 'promover: hecho. Dile al director que el lote está promovido.'
exit 0
