<#
.SYNOPSIS
Saca las capturas de tests/platform/golden/comandos.txt con rally.exe --golden y las compara
byte a byte con las guardadas (o las guarda, con -Regenerar).

.DESCRIPTION
Necesita ventana y la gráfica del PC: no va en ctest ni en comprobar.ps1. Usa el rally.exe de
build/windows-release (o de build/windows-debug con -Debug), que tiene que estar compilado.
Sale con 0 si todas son iguales (o se han guardado) y con 1 si alguna cambia o falla.
#>
# Sin [CmdletBinding()]: añadiría el parámetro común -Debug.
param(
    [switch]$Debug,
    # Guarda las capturas como referencia en vez de compararlas.
    [switch]$Regenerar
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$raiz = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$config = if ($Debug) { 'Debug' } else { 'Release' }
$preset = if ($Debug) { 'windows-debug' } else { 'windows-release' }
$exe = Join-Path $raiz "build\$preset\src\$config\rally.exe"
$carpeta = Join-Path $raiz 'tests\platform\golden'
if (-not (Test-Path -LiteralPath $exe)) {
    Write-Host "golden: falta $exe (compila antes)"
    exit 1
}
$salidas = Join-Path ([IO.Path]::GetTempPath()) ("rally-golden-{0}" -f $PID)
New-Item -ItemType Directory -Force -Path $salidas | Out-Null

$fallos = 0
$lineas = Get-Content -LiteralPath (Join-Path $carpeta 'comandos.txt') -Encoding UTF8 |
    Where-Object { $_.Trim() -and -not $_.StartsWith('#') }
foreach ($linea in $lineas) {
    $partes = $linea -split '\|', 2
    $nombre = $partes[0].Trim()
    $salida = Join-Path $salidas $nombre
    $argumentos = ($partes[1].Trim() -split '\s+') | ForEach-Object { if ($_ -eq '{salida}') { $salida } else { $_ } }
    $err = Join-Path $salidas "$nombre.err.txt"
    $p = Start-Process -FilePath $exe -ArgumentList $argumentos -WorkingDirectory $raiz -Wait -PassThru `
        -RedirectStandardError $err
    if ($p.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $salida)) {
        Write-Host ("{0}: FALLO (código {1}) {2}" -f $nombre, $p.ExitCode, ((Get-Content -Raw -LiteralPath $err -ErrorAction SilentlyContinue) -as [string]).Trim())
        $fallos++
        continue
    }
    $guardada = Join-Path $carpeta $nombre
    $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $salida).Hash.Substring(0, 16)
    if ($Regenerar) {
        Copy-Item -LiteralPath $salida -Destination $guardada -Force
        Write-Host ("{0}: guardada ({1})" -f $nombre, $hash)
    } elseif (-not (Test-Path -LiteralPath $guardada)) {
        Write-Host ("{0}: FALLO, no hay referencia (golden.ps1 -Regenerar)" -f $nombre)
        $fallos++
    } elseif ((Get-FileHash -Algorithm SHA256 -LiteralPath $guardada).Hash.Substring(0, 16) -ne $hash) {
        Write-Host ("{0}: DISTINTA; la nueva está en {1}" -f $nombre, $salida)
        $fallos++
    } else {
        Write-Host ("{0}: igual ({1})" -f $nombre, $hash)
    }
}
if ($fallos -gt 0) {
    Write-Host "golden: $fallos capturas distintas o con error"
    exit 1
}
Write-Host ('golden: {0} capturas {1}' -f @($lineas).Count, $(if ($Regenerar) { 'guardadas' } else { 'iguales byte a byte' }))
Remove-Item -Recurse -Force -LiteralPath $salidas
exit 0
