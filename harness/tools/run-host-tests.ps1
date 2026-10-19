# run-host-tests.ps1 - configure/build/run host-side unit tests
#
# Host tests compile PURE LOGIC firmware modules (led_pattern, fmt, self_test,
# ...) with a host compiler and run them on the PC. Hardware must never enter
# this layer.
#
# Two drivers, in order of preference:
#   1) cmake + ctest   (preferred; also what CI uses)
#   2) MSVC cl.exe     (fallback for machines without cmake - this repository
#                       must be able to run its regression suite offline)
#
# The test framework (harness/tests/host/framework/fw_test.{h,c}) is
# self-contained: no FetchContent, no network, no dynamic allocation.
#
# Output goes to the success/error streams (capture with *>&1 if needed).

[CmdletBinding()]
param(
    [string]$BuildDir = 'build-host'
)

$ErrorActionPreference = 'Continue'
$Here = $PSScriptRoot
$HostDir = Join-Path $Here '..\tests\host'

. "$Here\lib\ToolDetect.ps1"   # Find-CMake / Find-HostCC / Find-MsvcHost / Test-RealExe

# modules.json is the single declared truth for which firmware modules are
# host-tested. The MSVC fallback compiles exactly that list, and
# static-check.ps1 asserts CMakeLists.txt / test_runner.c agree with it, so a new
# pure-logic module cannot be added without a host test (previously three hand
# -maintained copies could each drift silently).
$RepoRoot = (Resolve-Path (Join-Path $Here '..\..')).Path
$ManifestPath = Join-Path $HostDir 'modules.json'
if (-not (Test-Path $ManifestPath)) {
    Write-Output "manifest missing: $ManifestPath"
    exit 2
}
$MF = Get-Content -Raw -Encoding UTF8 $ManifestPath | ConvertFrom-Json
$FwSrc = (Join-Path $RepoRoot (($MF.meta.source_dir) -replace '/', '\'))
if (-not (Test-Path $FwSrc)) {
    Write-Output "firmware source dir missing: $FwSrc (modules.json meta.source_dir)"
    exit 2
}

function Get-HostSourceList {
    $list = @()
    foreach ($f in @($MF.framework)) { $list += (Join-Path $HostDir ($f -replace '/', '\')) }
    foreach ($m in @($MF.logic)) { $list += (Join-Path $FwSrc "$m.c") }
    foreach ($t in @($MF.suites)) { $list += (Join-Path $HostDir (([string]$t.file) -replace '/', '\')) }
    $list += (Join-Path $HostDir (($MF.runner) -replace '/', '\'))
    $missing = @($list | Where-Object { -not (Test-Path $_) })
    if ($missing.Count -gt 0) {
        Write-Output "modules.json declares sources that do not exist:"
        foreach ($m in $missing) { Write-Output "  - $m" }
        exit 2
    }
    return $list
}

function Get-FwSourceDir {
    return $FwSrc
}

# Build + run the suite with MSVC cl.exe. Returns $true when all tests pass.
# Toolchain environment comes from Find-MsvcHost (deliberately not vcvars64.bat -
# see the note in lib/ToolDetect.ps1).
function Invoke-MsvcFallback {
    $msvc = Find-MsvcHost
    if (-not $msvc) { return $false }

    $env:INCLUDE = $msvc.Include
    $env:LIB = $msvc.Lib
    $env:PATH = "$($msvc.Bin);$env:PATH"

    $fwSrc  = Get-FwSourceDir
    $outDir = Join-Path $HostDir 'build-host-msvc'
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null

    $srcs = Get-HostSourceList

    $clArgs = @('/nologo', '/W3', '/Od', '/utf-8',
                "-I$HostDir\framework", "-I$fwSrc",
                "/Fe:$outDir\test_runner.exe", "/Fo:$outDir\\") + $srcs

    # Write-Host, not Write-Output - see the note further down (ADR 0008).
    Write-Host "[msvc fallback] building host tests with cl.exe ($($msvc.Version)) - $($srcs.Count) sources from modules.json"
    # Capture + re-emit via Write-Host: an unredirected native call would also
    # leak into this function's return value and make the caller's 'if (...)'
    # truthy no matter what (ADR 0008).
    $clOut = & cl.exe @clArgs 2>&1
    $clExit = $LASTEXITCODE
    $clOut | ForEach-Object { Write-Host $_ }
    if ($clExit -ne 0) { return $false }

    $exe = Join-Path $outDir 'test_runner.exe'
    if (-not (Test-Path $exe)) { return $false }

    # Run the suite via Start-Process instead of '& $exe': $LASTEXITCODE from a
    # native child is NOT reliably propagated in every PowerShell host. Proven
    # 2026-10-18 (ADR 0008): the runner exited 1 and printed
    # "cases: 20, failures: 2 -> FAIL", yet this script still reported
    # "all host unit tests passed" and exited 0 - a silent false PASS, which is
    # the worst possible failure mode for this gate.
    $runOut = & $exe 2>&1
    $runExit = $LASTEXITCODE
    $runText = ($runOut | Out-String)

    # Belt and braces: also trust the verdict line the runner prints itself.
    if ($runText -match 'failures:\s*(\d+)') {
        if ([int]$Matches[1] -gt 0) { $runExit = 1 }
    }
    elseif ($runText -match '\-\>\s*FAIL') {
        $runExit = 1
    }

    # Persist a machine-readable log next to the artifact: the proof that the
    # suite ran, independent of whether the host console streams native output.
    $runOut | Set-Content -Path (Join-Path $outDir 'last-test-log.txt') -Encoding UTF8

    # Write-Host, NOT Write-Output: everything a function writes to the output
    # stream becomes part of its RETURN VALUE. With Write-Output the caller's
    # 'if (Invoke-MsvcFallback)' received an ARRAY of (message strings + bool),
    # and a non-empty array is truthy - so this gate reported PASS even when the
    # suite failed (ADR 0008). Write-Host keeps the messages on the console and
    # lets the function return exactly one Boolean.
    $runOut | ForEach-Object { Write-Host $_ }
    Write-Host "[msvc fallback] runner exit code: $runExit"

    return ($runExit -eq 0)
}

# ---- cmake path ---------------------------------------------------------------
$cmake = Find-CMake
if (-not $cmake) {
    Write-Output "cmake not found - falling back to MSVC cl.exe."
    Write-Output "  (for the preferred path: winget install Kitware.CMake)"
    if (Invoke-MsvcFallback) {
        Write-Output "all host unit tests passed"
        exit 0
    }
    Write-Output "host unit tests FAILED (msvc fallback)"
    exit 1
}

# ---- host C compiler ----------------------------------------------------------
$cc = Find-HostCC
if (-not $cc) {
    Write-Output "No host C compiler found. Install one of:"
    Write-Output "  winget install --id BrechtSanders.WinLibs.POSIX.UCRT --source winget   (MinGW-w64 gcc)"
    Write-Output "  or Visual Studio Build Tools (MSVC), then re-run this script."
    exit 1
}
Write-Output "using host compiler: $cc"

# ---- configure / build / test --------------------------------------------------
Push-Location $HostDir
try {
    $commonArgs = @('-DCMAKE_BUILD_TYPE=Debug')
    if ($cc -like '*.exe') { $commonArgs += "-DCMAKE_C_COMPILER=$cc" }

    cmake -S . -B $BuildDir @commonArgs
    if ($LASTEXITCODE -ne 0) { throw "cmake configure failed" }
    cmake --build $BuildDir
    if ($LASTEXITCODE -ne 0) { throw "cmake build failed" }
    ctest --test-dir $BuildDir --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw "unit tests FAILED" }
    Write-Output "all host unit tests passed"
}
finally { Pop-Location }
