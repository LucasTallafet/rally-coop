<#
.SYNOPSIS
Comprobación completa de una copia de trabajo: formato, configurar, compilar y ctest, con un
límite de dos compilaciones a la vez en el PC.

.DESCRIPTION
1. clang-format 23.1.2 con --dry-run --Werror sobre el C++ de src/, tests/ y tools/ (salvo con
   -SinFormato).
2. Con una plaza de compilación: cmake --preset, cmake --build --preset y ctest --preset.
3. Una línea final con los tests, la huella, las reservas de Jolt, el hash de la telemetría y
   kSimVersion, sacados de las líneas RALLY_SUMMARY de los tests.

Plazas: dos mutex con nombre (Local\RallyBuild0 y Local\RallyBuild1), tomados y soltados en el
hilo del script. Un mutex abandonado (su proceso murió) cuenta como plaza conseguida. No es un
semáforo: un semáforo perdería para siempre la plaza de un proceso muerto.

Cada orden corre dentro de un Job Object con KILL_ON_JOB_CLOSE, creada suspendida y metida en el
job antes de arrancar: si este PowerShell muere, Windows mata también cmake, MSBuild y los cl.exe.
Cada llamada usa su propio mspdbsrv (_MSPDBSRV_ENDPOINT_) y MSBuild sin nodos reutilizables, para
que la muerte de un job no tumbe la compilación de otra copia que se hubiera enganchado a ellos.

Sale con 0 si todo pasa y con 1 en el primer paso que falla.
#>
# Sin [CmdletBinding()]: añadiría el parámetro común -Debug, que aquí es el preset de Debug.
param(
    [switch]$Debug,
    [switch]$SinFormato
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$raiz = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$preset = if ($Debug) { 'windows-debug' } else { 'windows-release' }
$build = Join-Path $raiz "build\$preset"
$inicio = [Diagnostics.Stopwatch]::StartNew()

function Terminar([int]$codigo, [string]$texto) {
    $t = $inicio.Elapsed
    Write-Host ''
    Write-Host ('{0} · {1} · {2} min {3} s' -f $(if ($codigo -eq 0) { 'OK' } else { 'FALLO' }), $texto,
        [int][Math]::Floor($t.TotalMinutes), $t.Seconds)
    exit $codigo
}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;

public static class RallyJob {
    [StructLayout(LayoutKind.Sequential)]
    struct IO_COUNTERS { public ulong a, b, c, d, e, f; }
    [StructLayout(LayoutKind.Sequential)]
    struct BASIC_LIMIT {
        public long PerProcessUserTimeLimit, PerJobUserTimeLimit;
        public uint LimitFlags;
        public UIntPtr MinimumWorkingSetSize, MaximumWorkingSetSize;
        public uint ActiveProcessLimit;
        public UIntPtr Affinity;
        public uint PriorityClass, SchedulingClass;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct EXTENDED_LIMIT {
        public BASIC_LIMIT Basic;
        public IO_COUNTERS Io;
        public UIntPtr ProcessMemoryLimit, JobMemoryLimit, PeakProcessMemoryUsed, PeakJobMemoryUsed;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct SECURITY_ATTRIBUTES { public int nLength; public IntPtr lpSecurityDescriptor; public bool bInheritHandle; }
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    struct STARTUPINFO {
        public int cb; public string lpReserved, lpDesktop, lpTitle;
        public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
        public short wShowWindow, cbReserved2; public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
    }
    [StructLayout(LayoutKind.Sequential)]
    struct PROCESS_INFORMATION { public IntPtr hProcess, hThread; public int dwProcessId, dwThreadId; }

    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateJobObject(IntPtr attributes, string name);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool SetInformationJobObject(IntPtr job, int infoClass, ref EXTENDED_LIMIT info, int length);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern bool CreateProcess(string app, System.Text.StringBuilder commandLine, IntPtr pa, IntPtr ta,
        bool inherit, uint flags, IntPtr env, string cwd, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
    [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
    static extern IntPtr CreateFile(string name, uint access, uint share, ref SECURITY_ATTRIBUTES sa,
        uint disposition, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern uint ResumeThread(IntPtr thread);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool TerminateProcess(IntPtr process, uint code);
    [DllImport("kernel32.dll")]
    static extern uint WaitForSingleObject(IntPtr handle, uint ms);
    [DllImport("kernel32.dll", SetLastError = true)]
    static extern bool GetExitCodeProcess(IntPtr process, out uint code);
    [DllImport("kernel32.dll", SetLastError = true)]
    public static extern bool CloseHandle(IntPtr handle);

    const uint KILL_ON_JOB_CLOSE = 0x2000;
    const int EXTENDED_LIMIT_INFORMATION = 9;
    const uint CREATE_SUSPENDED = 0x4, CREATE_NO_WINDOW = 0x08000000;
    const int STARTF_USESTDHANDLES = 0x100;

    // Job que mata todo lo que contiene cuando se cierra su último handle (este proceso).
    public static IntPtr CreateKillJob() {
        IntPtr job = CreateJobObject(IntPtr.Zero, null);
        if (job == IntPtr.Zero) throw new System.ComponentModel.Win32Exception();
        EXTENDED_LIMIT info = new EXTENDED_LIMIT();
        info.Basic.LimitFlags = KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job, EXTENDED_LIMIT_INFORMATION, ref info, Marshal.SizeOf(typeof(EXTENDED_LIMIT))))
            throw new System.ComponentModel.Win32Exception();
        return job;
    }

    static IntPtr OpenInheritable(string path, bool write) {
        SECURITY_ATTRIBUTES sa = new SECURITY_ATTRIBUTES();
        sa.nLength = Marshal.SizeOf(typeof(SECURITY_ATTRIBUTES));
        sa.bInheritHandle = true;
        // GENERIC_WRITE / GENERIC_READ; compartido para leer y escribir; CREATE_ALWAYS / OPEN_EXISTING.
        IntPtr h = CreateFile(path, write ? 0x40000000u : 0x80000000u, 3, ref sa, write ? 2u : 3u, 0x80, IntPtr.Zero);
        if (h == new IntPtr(-1)) throw new System.ComponentModel.Win32Exception();
        return h;
    }

    // Arranca la orden suspendida, la mete en el job y la reanuda: ningún hijo nace fuera del job.
    // La salida (y los errores) van a logPath. Devuelve el handle del proceso.
    public static IntPtr Start(IntPtr job, string commandLine, string cwd, string logPath) {
        IntPtr log = OpenInheritable(logPath, true);
        IntPtr nul = OpenInheritable("NUL", false);
        try {
            STARTUPINFO si = new STARTUPINFO();
            si.cb = Marshal.SizeOf(typeof(STARTUPINFO));
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = nul; si.hStdOutput = log; si.hStdError = log;
            PROCESS_INFORMATION pi;
            if (!CreateProcess(null, new System.Text.StringBuilder(commandLine), IntPtr.Zero, IntPtr.Zero, true,
                    CREATE_SUSPENDED | CREATE_NO_WINDOW, IntPtr.Zero, cwd, ref si, out pi))
                throw new System.ComponentModel.Win32Exception();
            if (!AssignProcessToJobObject(job, pi.hProcess)) {
                TerminateProcess(pi.hProcess, 1);
                CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
                throw new System.ComponentModel.Win32Exception();
            }
            ResumeThread(pi.hThread);
            CloseHandle(pi.hThread);
            return pi.hProcess;
        } finally {
            CloseHandle(log);
            CloseHandle(nul);
        }
    }

    // true si el proceso ha terminado (espera como mucho ms).
    public static bool Wait(IntPtr process, uint ms) { return WaitForSingleObject(process, ms) == 0; }

    public static int ExitCode(IntPtr process) {
        uint code;
        GetExitCodeProcess(process, out code);
        return (int)code;
    }
}
'@

# Ejecuta una orden dentro del job y enseña su salida mientras corre. Devuelve su código.
function Ejecutar([IntPtr]$job, [string]$orden, [string]$log) {
    Write-Host "> $orden"
    $proceso = [RallyJob]::Start($job, $orden, $raiz, $log)
    $lector = $null
    try {
        $flujo = [IO.File]::Open($log, 'Open', 'Read', 'ReadWrite')
        $lector = New-Object IO.StreamReader($flujo, [Text.Encoding]::GetEncoding([Console]::OutputEncoding.CodePage))
        while ($true) {
            # Esperas cortas: entre una y otra, PowerShell atiende Ctrl+C y corre los finally.
            $fin = [RallyJob]::Wait($proceso, 200)
            $nuevo = $lector.ReadToEnd()
            if ($nuevo) { [Console]::Out.Write($nuevo) }
            if ($fin) { break }
        }
        return [RallyJob]::ExitCode($proceso)
    } finally {
        if ($lector) { $lector.Dispose() }
        [void][RallyJob]::CloseHandle($proceso)
    }
}

# --- 1. Formato -----------------------------------------------------------------------------
if (-not $SinFormato) {
    # El del PATH o, si no está, el del paquete de pip (pip --user no siempre deja Scripts en el
    # PATH).
    $clangFormat = $null
    $enPath = Get-Command clang-format -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($enPath) { $clangFormat = $enPath.Source }
    if (-not $clangFormat -and (Get-Command python -ErrorAction SilentlyContinue)) {
        $clangFormat = (& python -c "import clang_format,os;print(os.path.join(os.path.dirname(clang_format.__file__),'data','bin','clang-format.exe'))" 2>$null)
    }
    if (-not $clangFormat -or -not (Test-Path -LiteralPath $clangFormat)) {
        Write-Host 'clang-format: no está en el PATH ni en el Python del PATH'
        Write-Host '  python -m pip install --user clang-format==23.1.2'
        Terminar 1 'formato: falta clang-format'
    }
    $version = (& $clangFormat --version) -join ' '
    if ($version -notmatch 'version 23\.1\.2\b') {
        Write-Host "clang-format: hace falta la 23.1.2 y está: $version"
        Write-Host '  python -m pip install --user clang-format==23.1.2'
        Terminar 1 'formato: versión de clang-format equivocada'
    }
    $archivos = @(& git -C $raiz ls-files -- 'src/*.cpp' 'src/*.h' 'tests/*.cpp' 'tests/*.h' 'tools/*.cpp' 'tools/*.h')
    $mal = @()
    foreach ($grupo in 0..([Math]::Ceiling($archivos.Count / 50) - 1)) {
        $lote = $archivos | Select-Object -Skip ($grupo * 50) -First 50 | ForEach-Object { Join-Path $raiz $_ }
        # clang-format escribe sus avisos en stderr: con Stop, PowerShell 5.1 los convertiría en
        # una excepción.
        $salida = & { $ErrorActionPreference = 'Continue'; & $clangFormat --dry-run --Werror $lote 2>&1 }
        if ($LASTEXITCODE -ne 0) {
            $mal += $salida | ForEach-Object { "$_" } | Where-Object { $_ -match '^(.+?):\d+:\d+: error' } |
                ForEach-Object { $Matches[1] }
        }
    }
    if ($mal.Count -gt 0) {
        Write-Host 'clang-format: estos archivos no tienen el formato del repositorio:'
        $mal | Sort-Object -Unique | ForEach-Object { Write-Host "  $_" }
        Terminar 1 ('formato: archivos sin formato: {0}' -f @($mal | Sort-Object -Unique).Count)
    }
    Write-Host ('clang-format: {0} archivos con formato' -f $archivos.Count)
}

# --- 2. Plaza de compilación ------------------------------------------------------------------
$plazas = @(
    (New-Object Threading.Mutex($false, 'Local\RallyBuild0')),
    (New-Object Threading.Mutex($false, 'Local\RallyBuild1'))
)
$plaza = -1
$abandonada = $false
$job = [IntPtr]::Zero
$entornoAntes = @{
    '_MSPDBSRV_ENDPOINT_' = $env:_MSPDBSRV_ENDPOINT_
    'MSBUILDDISABLENODEREUSE' = $env:MSBUILDDISABLENODEREUSE
}
try {
    $espera = [Diagnostics.Stopwatch]::StartNew()
    while ($plaza -lt 0) {
        try {
            $i = [Threading.WaitHandle]::WaitAny([Threading.WaitHandle[]]$plazas, 30000)
            if ($i -ne [Threading.WaitHandle]::WaitTimeout) { $plaza = $i }
        } catch [Threading.AbandonedMutexException] {
            # Su dueño murió sin soltarla: es nuestra.
            $plaza = $_.Exception.MutexIndex
            $abandonada = $true
        }
        if ($plaza -lt 0) {
            Write-Host ('esperando plaza de compilación ({0:F0} s)...' -f $espera.Elapsed.TotalSeconds)
        }
    }
    # La otra plaza, ocupada: se compila con 2 procesos. Libre (o abandonada): con todos.
    $otra = $plazas[1 - $plaza]
    $otraLibre = $false
    try { $otraLibre = $otra.WaitOne(0) } catch [Threading.AbandonedMutexException] { $otraLibre = $true }
    if ($otraLibre) { $otra.ReleaseMutex() }
    $paralelo = if ($otraLibre) { '--parallel' } else { '--parallel 2' }
    Write-Host ('plaza {0}{1}; la otra {2}: {3}' -f $plaza, $(if ($abandonada) { ' (abandonada por un proceso muerto)' } else { '' }),
        $(if ($otraLibre) { 'libre' } else { 'ocupada' }), $paralelo)

    # mspdbsrv propio y MSBuild sin nodos que sobrevivan: los dos vivirían en este job.
    $env:_MSPDBSRV_ENDPOINT_ = 'rally-{0}-{1}' -f $PID, $plaza
    $env:MSBUILDDISABLENODEREUSE = '1'
    $job = [RallyJob]::CreateKillJob()
    New-Item -ItemType Directory -Force -Path $build | Out-Null

    if ((Ejecutar $job "cmake --preset $preset" (Join-Path $build 'comprobar-configurar.log')) -ne 0) {
        Terminar 1 'configurar'
    }
    if ((Ejecutar $job "cmake --build --preset $preset $paralelo -- /nodeReuse:false" (Join-Path $build 'comprobar-compilar.log')) -ne 0) {
        Terminar 1 'compilar'
    }
    $ctestLog = Join-Path $build 'comprobar-ctest.log'
    # El JUnit guarda la salida de todos los tests, también de los que pasan (--output-log no):
    # de ahí salen las líneas RALLY_SUMMARY.
    $junit = Join-Path $build 'comprobar-ctest.xml'
    if (Test-Path -LiteralPath $junit) { Remove-Item -LiteralPath $junit }
    $codigoTests = Ejecutar $job "ctest --preset $preset --output-junit `"$junit`"" $ctestLog
} finally {
    # La plaza se suelta siempre, en el mismo hilo que la tomó; el job se cierra (y mata lo que
    # quede vivo).
    if ($job -ne [IntPtr]::Zero) { [void][RallyJob]::CloseHandle($job) }
    if ($plaza -ge 0) { $plazas[$plaza].ReleaseMutex() }
    foreach ($m in $plazas) { $m.Dispose() }
    foreach ($k in $entornoAntes.Keys) { Set-Item -Path "env:$k" -Value $entornoAntes[$k] -ErrorAction SilentlyContinue }
}

# --- 3. Resumen -------------------------------------------------------------------------------
$texto = Get-Content -Raw -LiteralPath $ctestLog
if (Test-Path -LiteralPath $junit) { $texto += "`n" + (Get-Content -Raw -LiteralPath $junit) }
# «100% tests passed out of 341» o «99% tests passed, 2 tests failed out of 341».
$total = 0
$fallidos = -1
if ($texto -match '\d+% tests passed(?:, (\d+) tests failed)? out of (\d+)') {
    $total = [int]$Matches[2]
    $fallidos = if ($Matches[1]) { [int]$Matches[1] } else { 0 }
}
$valores = @{}
foreach ($m in [regex]::Matches($texto, 'RALLY_SUMMARY (\w+)=(\S+)')) { $valores[$m.Groups[1].Value] = $m.Groups[2].Value }
$faltan = @('huella', 'reservas_jolt', 'telemetria', 'sim_version') | Where-Object { -not $valores.ContainsKey($_) }
$linea = 'tests {0}/{1} · huella {2} · reservas Jolt {3} · telemetría {4} · kSimVersion {5} · {6} · plaza {7}' -f
    ($total - [Math]::Max($fallidos, 0)), $total, $valores['huella'], $valores['reservas_jolt'], $valores['telemetria'],
    $valores['sim_version'], $preset, $plaza
if ($codigoTests -ne 0 -or $fallidos -ne 0) { Terminar 1 "tests: $linea" }
if (@($faltan).Count -gt 0) { Terminar 1 ("resumen sin {0}: $linea" -f ($faltan -join ', ')) }
Terminar 0 $linea
