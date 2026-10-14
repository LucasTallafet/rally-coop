<#
.SYNOPSIS
Descarga una sola vez las fuentes de las dependencias (Jolt, SDL, raylib y doctest) para todas
las copias de trabajo.

.DESCRIPTION
Lee la URL y el SHA256 de cada FetchContent_Declare de cmake/Dependencies.cmake (una sola fuente
de verdad), descarga el archivo, comprueba su hash y lo descomprime en
$env:RALLY_DEPS_SRC\<nombre>-<8 primeros del hash>\, con el sello .rally-sha256 dentro.
cmake/Dependencies.cmake usa esa carpeta como FETCHCONTENT_SOURCE_DIR_<NOMBRE> solo si el sello
coincide con su hash; si no, descarga como siempre.

Solo se comparten las fuentes: cada copia compila sus dependencias en su propio build/.

Sale con 0 si las cuatro quedan listas y con 1 si alguna falla.
#>
[CmdletBinding()]
param(
    # Carpeta de las fuentes; por defecto, $env:RALLY_DEPS_SRC o D:\Proyectos\RallyCoop\deps-src.
    [string]$Destino
)

$ErrorActionPreference = 'Stop'
# Sin la barra de progreso, Invoke-WebRequest de PowerShell 5.1 descarga decenas de veces más rápido.
$ProgressPreference = 'SilentlyContinue'
Set-StrictMode -Version 3.0

if (-not $Destino) {
    $Destino = if ($env:RALLY_DEPS_SRC) { $env:RALLY_DEPS_SRC } else { 'D:\Proyectos\RallyCoop\deps-src' }
}
$raiz = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$cmakeDeps = Join-Path $raiz 'cmake\Dependencies.cmake'

# FetchContent_Declare(<nombre> ... URL <url> URL_HASH SHA256=<hash> ...)
$texto = Get-Content -Raw -LiteralPath $cmakeDeps
$patron = 'FetchContent_Declare\(\s*(\w+)\s+URL\s+(\S+)\s+URL_HASH\s+SHA256=([0-9a-fA-F]{64})'
$deps = [regex]::Matches($texto, $patron) | ForEach-Object {
    [pscustomobject]@{ Nombre = $_.Groups[1].Value; Url = $_.Groups[2].Value; Sha = $_.Groups[3].Value.ToLowerInvariant() }
}
if (@($deps).Count -ne 4) {
    Write-Host "error: esperaba 4 dependencias en $cmakeDeps y hay $(@($deps).Count)"
    exit 1
}

New-Item -ItemType Directory -Force -Path $Destino | Out-Null
$descargas = Join-Path $Destino '_descargas'
New-Item -ItemType Directory -Force -Path $descargas | Out-Null
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

$fallos = 0
foreach ($d in $deps) {
    $carpeta = Join-Path $Destino ('{0}-{1}' -f $d.Nombre, $d.Sha.Substring(0, 8))
    $sello = Join-Path $carpeta '.rally-sha256'
    if ((Test-Path -LiteralPath $sello) -and ((Get-Content -Raw -LiteralPath $sello).Trim() -eq $d.Sha)) {
        Write-Host ("{0}: ya está en {1}" -f $d.Nombre, $carpeta)
        continue
    }
    try {
        $archivo = Join-Path $descargas ('{0}-{1}.tar.gz' -f $d.Nombre, $d.Sha.Substring(0, 8))
        if (-not (Test-Path -LiteralPath $archivo) -or
            (Get-FileHash -Algorithm SHA256 -LiteralPath $archivo).Hash.ToLowerInvariant() -ne $d.Sha) {
            Write-Host ("{0}: descargando {1}" -f $d.Nombre, $d.Url)
            Invoke-WebRequest -Uri $d.Url -OutFile $archivo -UseBasicParsing
        }
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $archivo).Hash.ToLowerInvariant()
        if ($hash -ne $d.Sha) {
            Remove-Item -LiteralPath $archivo
            throw "SHA256 distinto: $hash (esperado $($d.Sha))"
        }
        # Se descomprime al lado y se mueve el directorio de arriba del archivo: la carpeta final
        # o está completa con su sello, o no está.
        $tmp = "$carpeta.tmp"
        if (Test-Path -LiteralPath $tmp) { Remove-Item -Recurse -Force -LiteralPath $tmp }
        if (Test-Path -LiteralPath $carpeta) { Remove-Item -Recurse -Force -LiteralPath $carpeta }
        New-Item -ItemType Directory -Path $tmp | Out-Null
        & tar.exe -xzf $archivo -C $tmp
        if ($LASTEXITCODE -ne 0) { throw "tar falló con el código $LASTEXITCODE" }
        $dentro = @(Get-ChildItem -LiteralPath $tmp)
        if ($dentro.Count -ne 1 -or -not $dentro[0].PSIsContainer) { throw 'el archivo no tiene una sola carpeta arriba' }
        Move-Item -LiteralPath $dentro[0].FullName -Destination $carpeta
        Remove-Item -Recurse -Force -LiteralPath $tmp
        Set-Content -LiteralPath $sello -Value $d.Sha -Encoding ascii
        Write-Host ("{0}: listo en {1}" -f $d.Nombre, $carpeta)
    } catch {
        Write-Host ("{0}: ERROR: {1}" -f $d.Nombre, $_.Exception.Message)
        $fallos++
    }
}

if ($fallos -gt 0) {
    Write-Host "deps: $fallos dependencias con error"
    exit 1
}
Write-Host "deps: las 4 fuentes listas en $Destino (RALLY_DEPS_SRC=$Destino)"
exit 0
