<#
.SYNOPSIS
Cierra los push normales del repositorio (docs/09-agentes.md §6). Lo ejecuta Lucas una vez.

.DESCRIPTION
- Fija remote.origin.pushurl en no-push: un git push normal falla siempre.
- Instala tools/ci/pre-push en la carpeta de hooks común a todas las copias (la de
  git rev-parse --git-common-dir): rechaza los push a master sin RALLY_PROMOVER=1, los forzados y
  los borrados.
El único push que queda es el de tools/ci/promover.ps1. Con -Repositorio, sobre otro repositorio
(lo usan los tests).
#>
param(
    # Repositorio en el que se instala; por defecto, el de este script.
    [string]$Repositorio = (Join-Path $PSScriptRoot '..\..')
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$Repositorio = (Resolve-Path -LiteralPath $Repositorio).Path
$comun = (& git -C $Repositorio rev-parse --path-format=absolute --git-common-dir)
if ($LASTEXITCODE -ne 0) { Write-Host "instalar-hooks: $Repositorio no es un repositorio git"; exit 1 }
if (-not (& git -C $Repositorio config --get remote.origin.url)) {
    Write-Host 'instalar-hooks: el repositorio no tiene remote.origin.url'
    exit 1
}

& git -C $Repositorio config remote.origin.pushurl no-push
if ($LASTEXITCODE -ne 0) { exit 1 }

# Con finales de línea LF: git puede haber dejado el archivo con CRLF y sh no lo leería.
$hooks = Join-Path $comun 'hooks'
New-Item -ItemType Directory -Force -Path $hooks | Out-Null
$texto = [IO.File]::ReadAllText((Join-Path $PSScriptRoot 'pre-push')) -replace "`r`n", "`n"
[IO.File]::WriteAllText((Join-Path $hooks 'pre-push'), $texto, (New-Object Text.UTF8Encoding $false))

Write-Host "instalar-hooks: remote.origin.pushurl = no-push; pre-push instalado en $hooks"
Write-Host 'instalar-hooks: desde ahora solo se empuja con tools/ci/promover.ps1'
exit 0
