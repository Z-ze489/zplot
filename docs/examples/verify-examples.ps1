# =====================================================================
#  verify-examples.ps1
#
#  Compiles and RUNS every code sample in docs/examples/.
#  Each .cpp is a standalone program; a sample only counts as verified
#  when it compiles clean AND exits with code 0.
#
#  Why this script sets its own environment instead of calling vcvarsall:
#  the tool environment may block cmd.exe, so we point at the toolchain
#  directly. The paths below default to auto-detection; override any of
#  the four if your install lives somewhere unusual.
#
#  Usage:  powershell -ExecutionPolicy Bypass -File verify-examples.ps1
# =====================================================================

$ErrorActionPreference = 'Stop'

# ---- toolchain locations (auto-detected, override if needed) ---------
$MSVC = $env:ZPLOT_MSVC
$KITS = $env:ZPLOT_KITS

if (-not $MSVC) {
    $vswhere = Join-Path ([Environment]::GetEnvironmentVariable('ProgramFiles(x86)')) 'Microsoft Visual Studio\Installer\vswhere.exe'
    $vsRoot = $null
    if (Test-Path $vswhere) {
        $vsRoot = & $vswhere -latest -products * -property installationPath 2>$null
    }
    # Fall back to scanning common drives for a VS install.
    if (-not $vsRoot) {
        foreach ($drive in @('C:','D:')) {
            $hit = Get-ChildItem "$drive\" -Directory -ErrorAction SilentlyContinue |
                   Where-Object { $_.Name -match 'Visual ?Studio|^20\d\dVs' } |
                   Select-Object -First 1
            if ($hit) { $vsRoot = $hit.FullName; break }
        }
    }
    if ($vsRoot) {
        $latest = Get-ChildItem "$vsRoot\VC\Tools\MSVC" -Directory -ErrorAction SilentlyContinue |
                  Sort-Object Name -Descending | Select-Object -First 1
        if ($latest) { $MSVC = $latest.FullName }
    }
}

if (-not $KITS) {
    $pf86 = [Environment]::GetEnvironmentVariable('ProgramFiles(x86)')
    $pf   = [Environment]::GetEnvironmentVariable('ProgramFiles')
    $candidates = @()
    if ($pf86) { $candidates += "$pf86\Windows Kits\10" }
    if ($pf)   { $candidates += "$pf\Windows Kits\10" }
    foreach ($drive in @('C:','D:')) { $candidates += "$drive\Windows Kits\10" }
    # Match on Include\, not on the root: an empty "$pf86\Windows Kits\10"
    # folder is common and would otherwise win the race against the real one.
    foreach ($p in $candidates) {
        if ($p -and (Test-Path "$p\Include")) { $KITS = $p; break }
    }
}

if (-not $MSVC -or -not (Test-Path "$MSVC\bin\Hostx64\x64\cl.exe")) {
    Write-Host "[FATAL] MSVC not found. Set ZPLOT_MSVC to ...\VC\Tools\MSVC\<ver>" -ForegroundColor Red
    exit 1
}
if (-not $KITS -or -not (Test-Path "$KITS\Include")) {
    Write-Host "[FATAL] Windows SDK not found. Set ZPLOT_KITS to ...\Windows Kits\10" -ForegroundColor Red
    exit 1
}

$SDKVER = (Get-ChildItem "$KITS\Include" -Directory |
           Sort-Object Name -Descending | Select-Object -First 1).Name
# ----------------------------------------------------------------------

$root    = Split-Path -Parent $MyInvocation.MyCommand.Path
$inc     = [System.IO.Path]::GetFullPath((Join-Path $root '..\..\include'))
$src     = [System.IO.Path]::GetFullPath((Join-Path $root '..\..\src'))
$build   = Join-Path $root 'build'

if (Test-Path $build) { Remove-Item $build -Recurse -Force }
New-Item -ItemType Directory -Force -Path $build | Out-Null

# ---- build the library objects first ---------------------------------
$env:Path = "$MSVC\bin\Hostx64\x64;$KITS\bin\$SDKVER\x64;$env:Path"
$env:INCLUDE = "$MSVC\include;$KITS\Include\$SDKVER\ucrt;$KITS\Include\$SDKVER\um;$KITS\Include\$SDKVER\shared;$KITS\Include\$SDKVER\winrt"
$env:LIB = "$MSVC\lib\x64;$KITS\Lib\$SDKVER\ucrt\x64;$KITS\Lib\$SDKVER\um\x64"

# English diagnostics keep the build logs greppable regardless of the
# console code page.
$env:VSLANG = '1033'

$libObj = Join-Path $build 'lib'
New-Item -ItemType Directory -Force -Path $libObj | Out-Null

Write-Host "`n=== building zplot (library objects) ===" -ForegroundColor Cyan
$libSources = Get-ChildItem (Join-Path $src '*.cpp')
$libArgs = @('/nologo','/std:c++20','/utf-8','/EHsc','/c',"/I$inc","/Fo$libObj\") + ($libSources | ForEach-Object { $_.FullName })
$libLog = & cl.exe @libArgs 2>&1
$libLog | Out-File (Join-Path $build 'lib.log') -Encoding utf8
if ($LASTEXITCODE -ne 0) {
    Write-Host $libLog
    Write-Host "[FATAL] library failed to build" -ForegroundColor Red
    exit 1
}
$objs = (Get-ChildItem (Join-Path $libObj '*.obj') | ForEach-Object { $_.FullName })
Write-Host ("library objects: {0}" -f $objs.Count) -ForegroundColor DarkGray

# ---- compile + run each sample ---------------------------------------
Write-Host "`n=== verifying samples ===" -ForegroundColor Cyan
$pass = 0; $fail = 0; $failed = @()

foreach ($cpp in (Get-ChildItem (Join-Path $root '*.cpp') | Sort-Object Name)) {
    $name   = $cpp.BaseName
    $exe    = Join-Path $build "$name.exe"
    $log    = Join-Path $build "$name.build.log"

    $args = @('/nologo','/std:c++20','/utf-8','/EHsc','/W3',
              "/I$inc","/I$src",
              "/Fo$build\\", "/Fe$exe",
              $cpp.FullName) + $objs + @('/link','/SUBSYSTEM:CONSOLE')
    $out = & cl.exe @args 2>&1
    $out | Out-File $log -Encoding utf8

    if ($LASTEXITCODE -ne 0) {
        Write-Host ("[FAIL compile] {0}" -f $name) -ForegroundColor Red
        $fail++; $failed += $name
        $errs = $out | Where-Object { $_ -match 'error [A-Z]+\d+' } | Select-Object -First 6
        foreach ($e in $errs) { Write-Host ("        $e") -ForegroundColor DarkRed }
        continue
    }

    # run it -- a sample that compiles but crashes is not verified
    $runOut = & $exe 2>&1
    $runCode = $LASTEXITCODE
    $runOut | Out-File (Join-Path $build "$name.out.txt") -Encoding utf8

    if ($runCode -ne 0) {
        Write-Host ("[FAIL run    ] {0}  (exit {1})" -f $name, $runCode) -ForegroundColor Red
        ($runOut | Select-Object -Last 8) | ForEach-Object { Write-Host ("        $_") -ForegroundColor DarkRed }
        $fail++; $failed += $name
    } else {
        Write-Host ("[ OK ] {0}" -f $name) -ForegroundColor Green
        $pass++
    }
}

Write-Host ("`n---------------- {0} ok, {1} failed ----------------" -f $pass, $fail) -ForegroundColor Cyan
if ($fail -gt 0) {
    Write-Host ("failed: {0}" -f ($failed -join ', ')) -ForegroundColor Red
    exit 1
}
exit 0
