<#
.SYNOPSIS
Comprueba que una rama solo toca rutas de su carril (docs/09-agentes.md §4, segunda barrera). La
lanza el integrador antes de cada fusión.

.DESCRIPTION
Cruza git diff --name-only <Base>...<rama> (sin detectar renombrados: cuentan la ruta vieja y la
nueva) con .claude/hooks/propiedad.json, con la misma regla que el hook de rutas
(rutas.py --rama). Da igual con qué se escribieran los archivos (Bash, clang-format -i, un
generador): cuenta lo que hay en la rama.

El carril sale de -Carril o, con una rama lote-<N>/<tarea>, de la línea de mensajes/lote-<N>.md
de la copia principal que nombra la tarea y un solo carril-<nombre>. Con la tarea, el único
fragmento de progreso permitido es docs/progreso/<tarea>.md.

Sale con 0 si la rama está limpia, con 1 y la lista de rutas ajenas si no, y con 2 si no puede
comprobarlo.
#>
param(
    [Parameter(Mandatory = $true, Position = 0)][string]$Rama,
    # sim, plataforma, juego o herramientas.
    [string]$Carril = '',
    # Nombre de la tarea (para docs/progreso/<tarea>.md); por defecto, el de la rama lote-<N>/<tarea>.
    [string]$Tarea = '',
    # Rama contra la que se compara.
    [string]$Base = 'integracion',
    # Repositorio en el que se comprueba; por defecto, el de este script (lo usan los tests).
    [string]$Repositorio = ''
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

# $PSScriptRoot está vacío en los valores por defecto de param en PowerShell 5.1.
if (-not $Repositorio) { $Repositorio = Join-Path $PSScriptRoot '..\..' }

function Sale([string]$texto) {
    Write-Host "propiedad: $texto"
    exit 2
}

$raiz = (Resolve-Path -LiteralPath $Repositorio).Path
# La regla es siempre la de la copia de este script, nunca la de la rama que se comprueba.
$rutas = (Resolve-Path (Join-Path $PSScriptRoot '..\..\.claude\hooks\rutas.py')).Path
# Rutas con acentos: UTF-8 de ida y vuelta con git y con Python.
$OutputEncoding = New-Object Text.UTF8Encoding $false
try { [Console]::OutputEncoding = $OutputEncoding } catch { }
$carriles = @('sim', 'plataforma', 'juego', 'herramientas')

$lote = ''
if ($Rama -match '^lote-(\d+)/(.+)$') {
    $lote = $Matches[1]
    if (-not $Tarea) { $Tarea = $Matches[2] }
}

if (-not $Carril) {
    if (-not $lote) { Sale "la rama $Rama no es lote-<N>/<tarea>: di el carril con -Carril" }
    $archivo = Join-Path $raiz "mensajes\lote-$lote.md"
    if (-not (Test-Path -LiteralPath $archivo)) { Sale "no está ${archivo}: di el carril con -Carril" }
    $patronTarea = '(?<![\w.])' + [regex]::Escape($Tarea) + '(?![\w.])'
    $encontrados = @(Get-Content -LiteralPath $archivo -Encoding UTF8 |
        Where-Object { $_ -match $patronTarea } |
        ForEach-Object { [regex]::Matches($_, 'carril-(sim|plataforma|juego|herramientas)\b') } |
        ForEach-Object { $_.Groups[1].Value } | Sort-Object -Unique)
    if ($encontrados.Count -ne 1) {
        Sale ("en $archivo, las líneas de la tarea $Tarea nombran {0} carriles ({1}): di el carril con -Carril" -f
            $encontrados.Count, ($encontrados -join ', '))
    }
    $Carril = $encontrados[0]
}
if ($carriles -notcontains $Carril) { Sale "carril desconocido: $Carril ($($carriles -join ', '))" }

foreach ($ref in @($Base, $Rama)) {
    & git -C $raiz rev-parse --verify --quiet "$ref^{commit}" | Out-Null
    if ($LASTEXITCODE -ne 0) { Sale "no existe la rama $ref" }
}
$cambiadas = @(& git -C $raiz -c core.quotepath=off diff --name-only --no-renames "$Base...$Rama")
if ($LASTEXITCODE -ne 0) { Sale "git diff $Base...$Rama ha fallado" }

$argumentos = @($rutas, '--rama', $Carril)
if ($Tarea) { $argumentos += @('--tarea', $Tarea) }
$ajenas = @($cambiadas | & python @argumentos)
$codigo = $LASTEXITCODE
if ($codigo -eq 0) {
    Write-Host ("propiedad: {0} limpia: {1} archivos, todos del carril {2}" -f $Rama, $cambiadas.Count, $Carril)
    exit 0
}
if ($codigo -ne 1) { Sale "rutas.py ha fallado ($codigo)" }
Write-Host ("propiedad: {0} toca {1} rutas que no son del carril {2}:" -f $Rama, $ajenas.Count, $Carril)
$ajenas | ForEach-Object { Write-Host "  $_" }
exit 1
