# run-host-tests.ps1 - configure/build/run host-side unit tests
#
# Host tests compile PURE LOGIC firmware modules with a host compiler and run
# them on the PC. Hardware must never enter this layer.
#
# v2 (2026-10-24, spec 0002 T-005): modules.json declares projects[]; each
# project gets its OWN logic library and runner executable, so identical
# module names in different projects never collide. The script aggregates the
# per-project verdicts into ONE "cases: N, failures: M" line printed FIRST in
# its output (verify.ps1 parses the first such line it sees - ADR 0009: a
# gate verdict must be countable).
#
# Two drivers, in order of preference:
#   1) cmake + ctest   (preferred; also what CI uses)
#   2) MSVC cl.exe     (fallback for machines without cmake - this repository
#                       must be able to run its regression suite offline)
#
# The test framework (harness/tests/host/framework/fw_test.{h,c}) is
# self-contained: no FetchContent, no network, no dynamic allocation.

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
# static-check.ps1 asserts CMakeLists.txt / per-project runners agree with it,
# so a new pure-logic module cannot be added without a host test.
$RepoRoot = (Resolve-Path (Join-Path $Here '..\..')).Path
$ManifestPath = Join-Path $HostDir 'modules.json'
if (-not (Test-Path $ManifestPath)) {
    Write-Output "manifest missing: $ManifestPath"
    exit 2
}
$MF = Get-Content -Raw -Encoding UTF8 $ManifestPath | ConvertFrom-Json
if (-not $MF.projects -or @($MF.projects).Count -eq 0) {
    Write-Output "modules.json v2 declares no projects[]"
    exit 2
}

$script:TotalCases = 0
$script:TotalFailures = 0
$script:PerProjectLogs = @()

# Parse a runner verdict line "cases: N, failures: M -> X" into the totals.
function Update-TotalsFromVerdict {
    param([string]$Text, [int]$RunExit)
    if ($Text -match 'cases:\s*(\d+),\s*failures:\s*(\d+)') {
        $script:TotalCases += [int]$Matches[1]
        $script:TotalFailures += [int]$Matches[2]
    }
    elseif ($RunExit -ne 0) {
        # no countable verdict + non-zero exit counts as one unproven failure
        $script:TotalFailures += 1
    }
}

# Build + run ONE project's suite with MSVC cl.exe. Returns $true on pass.
# Toolchain environment comes from Find-MsvcHost (deliberately not vcvars64.bat
# - see the note in lib/ToolDetect.ps1).
function Invoke-MsvcFallbackForProject {
    param($msvc, $proj, [int]$Index)

    $env:INCLUDE = $msvc.Include
    $env:LIB = $msvc.Lib
    $env:PATH = "$($msvc.Bin);$env:PATH"

    $fwSrc = Join-Path $RepoRoot (([string]$proj.source_dir) -replace '/', '\')
    if (-not (Test-Path $fwSrc)) {
        Write-Host "[msvc fallback] firmware source dir missing: $fwSrc"
        return $false
    }

    $list = @()
    foreach ($f in @($MF.framework)) { $list += (Join-Path $HostDir ($f -replace '/', '\')) }
    foreach ($m in @($proj.logic)) { $list += (Join-Path $fwSrc "$m.c") }
    foreach ($t in @($proj.suites)) { $list += (Join-Path $HostDir (([string]$t.file) -replace '/', '\')) }
    $list += (Join-Path $HostDir (([string]$proj.runner) -replace '/', '\'))
    $missing = @($list | Where-Object { -not (Test-Path $_) })
    if ($missing.Count -gt 0) {
        Write-Host "[msvc fallback] $($proj.name) declares sources that do not exist:"
        foreach ($m in $missing) { Write-Host "  - $m" }
        return $false
    }

    $outDir = Join-Path $HostDir 'build-host-msvc'
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $exe = Join-Path $outDir ("$($proj.cmake_exe).exe")

    $clArgs = @('/nologo', '/W3', '/Od', '/utf-8',
                "-I$HostDir\framework", "-I$fwSrc",
                "/Fe:$exe", "/Fo:$outDir\\") + $list

    # Write-Host, not Write-Output - see ADR 0008 (output stream = return value).
    Write-Host "[msvc fallback] building $($proj.name) with cl.exe ($($msvc.Version)) - $($list.Count) sources"
    $clOut = & cl.exe @clArgs 2>&1
    $clExit = $LASTEXITCODE
    $clOut | ForEach-Object { Write-Host $_ }
    if ($clExit -ne 0) { return $false }
    if (-not (Test-Path $exe)) { return $false }

    # Run the suite and trust BOTH the exit code and the runner's own verdict
    # line (ADR 0008: exit-code propagation alone once produced a false PASS).
    $runOut = & $exe 2>&1
    $runExit = $LASTEXITCODE
    $runText = ($runOut | Out-String)

    if ($runText -match 'failures:\s*(\d+)') {
        if ([int]$Matches[1] -gt 0) { $runExit = 1 }
    }
    elseif ($runText -match '\-\>\s*FAIL') {
        $runExit = 1
    }

    Update-TotalsFromVerdict -Text $runText -RunExit $runExit

    # Per-project evidence log next to the artifact. The full runner output
    # (including its own "cases:" verdict line) goes here only - the console
    # must not echo a second countable verdict line, or verify.ps1's
    # first-match parse would read a single project instead of the aggregate.
    $log = Join-Path $outDir ("last-test-log-$Index.txt")
    $runOut | Set-Content -Path $log -Encoding UTF8
    $script:PerProjectLogs += $log

    $projVerdict = if ($runExit -eq 0) { 'PASS' } else { 'FAIL' }
    $projCases = '-'
    $projFails = '-'
    if ($runText -match 'cases:\s*(\d+),\s*failures:\s*(\d+)') {
        $projCases = $Matches[1]
        $projFails = $Matches[2]
    }
    Write-Host "[msvc fallback] $($proj.name) runner exit $runExit ($projCases cases, $projFails failures) -> $projVerdict"

    return ($runExit -eq 0)
}

# ---- MSVC fallback path --------------------------------------------------------
function Invoke-MsvcFallback {
    $msvc = Find-MsvcHost
    if (-not $msvc) { return $false }

    $ok = $true
    $index = 0
    foreach ($proj in @($MF.projects)) {
        if (-not (Invoke-MsvcFallbackForProject -msvc $msvc -proj $proj -Index $index)) {
            $ok = $false
        }
        $index++
    }
    return $ok
}

# Aggregate verdict line FIRST in the output: verify.ps1 matches the first
# "cases: N, failures: M" occurrence, and skipping legs is not a pass (ADR 0009).
function Show-AggregateVerdict {
    if ($script:TotalFailures -gt 0) {
        Write-Output ("cases: {0}, failures: {1} -> FAIL (all projects aggregated)" -f $script:TotalCases, $script:TotalFailures)
    }
    else {
        Write-Output ("cases: {0}, failures: {1} -> PASS (all projects aggregated)" -f $script:TotalCases, $script:TotalFailures)
    }
}

# ---- cmake path ---------------------------------------------------------------
$cmake = Find-CMake
if (-not $cmake) {
    Write-Output "cmake not found - falling back to MSVC cl.exe."
    Write-Output "  (for the preferred path: winget install Kitware.CMake)"
    if (Invoke-MsvcFallback) {
        Show-AggregateVerdict
        Write-Output "all host unit tests passed"
        exit 0
    }
    Show-AggregateVerdict
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

    # Aggregate countable verdicts by running each project's runner directly.
    $index = 0
    foreach ($proj in @($MF.projects)) {
        $fwSrc = Join-Path $RepoRoot (([string]$proj.source_dir) -replace '/', '\')
        $exe = Join-Path $HostDir (Join-Path $BuildDir ("$($proj.cmake_exe).exe"))
        if (-not (Test-Path $exe)) { $exe = Join-Path $HostDir (Join-Path $BuildDir $proj.cmake_exe) }
        if (-not (Test-Path $exe)) { throw "runner executable not found: $exe" }
        $runOut = & $exe 2>&1
        $runExit = $LASTEXITCODE
        $runText = ($runOut | Out-String)
        if ($runExit -ne 0) { throw "$($proj.name) runner exited $runExit" }
        Update-TotalsFromVerdict -Text $runText -RunExit $runExit
        $log = Join-Path $HostDir (Join-Path $BuildDir ("last-test-log-$index.txt"))
        $runOut | Set-Content -Path $log -Encoding UTF8
        $index++
    }

    Show-AggregateVerdict
    Write-Output "all host unit tests passed"
}
finally { Pop-Location }
