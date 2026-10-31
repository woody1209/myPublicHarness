# run-sil-tests.ps1 - build and run the SIL (Software-In-the-Loop) layer
#
# SIL sits between the host unit tests and the board:
#
#   harness/tests/host   unit tests   - one call, one return value
#   harness/tests/sil    SIL          - the real module inside a simulated main
#                                       loop; asserts properties of TIME SERIES
#   harness/tools/dss    HIL          - the same properties on real silicon
#
# A gate verdict must be countable (ADR 0009), so this script prints ONE
# aggregate "cases: N, failures: M -> PASS|FAIL" line FIRST, plus the machine
# readable "SIL RESULT: PASS|FAIL" marker that verify.ps1's sil leg matches.
#
# Driver: MSVC cl.exe (this repository must run its regression suite offline;
# cmake is optional - see run-host-tests.ps1 for the same two-driver pattern).

[CmdletBinding()]
param(
    [string]$BuildDir = 'build-sil'
)

$ErrorActionPreference = 'Continue'
$Here = $PSScriptRoot
$SilDir = Join-Path $Here '..\tests\sil'

. "$Here\lib\ToolDetect.ps1"   # Find-MsvcHost / Find-HostCC

$RepoRoot = (Resolve-Path (Join-Path $Here '..\..')).Path
$ManifestPath = Join-Path $SilDir 'modules.json'
if (-not (Test-Path $ManifestPath)) {
    Write-Output "SIL manifest missing: $ManifestPath"
    exit 2
}
$MF = Get-Content -Raw -Encoding UTF8 $ManifestPath | ConvertFrom-Json
if (-not $MF.projects -or @($MF.projects).Count -eq 0) {
    Write-Output "sil/modules.json declares no projects[]"
    exit 2
}

$script:TotalCases = 0
$script:TotalFailures = 0

function Update-TotalsFromVerdict {
    param([string]$Text, [int]$RunExit)
    if ($Text -match 'cases:\s*(\d+),\s*failures:\s*(\d+)') {
        $script:TotalCases += [int]$Matches[1]
        $script:TotalFailures += [int]$Matches[2]
    }
    elseif ($RunExit -ne 0) {
        # A runner that died without printing a verdict is an UNPROVEN leg,
        # never a pass (ADR 0009).
        $script:TotalFailures += 1
    }
}

function Invoke-SilProject {
    param($msvc, $proj, [int]$Index)

    $env:INCLUDE = $msvc.Include
    $env:LIB = $msvc.Lib
    $env:PATH = "$($msvc.Bin);$env:PATH"

    $fwSrc = Join-Path $RepoRoot (([string]$proj.source_dir) -replace '/', '\')
    if (-not (Test-Path $fwSrc)) {
        Write-Host "[sil] firmware source dir missing: $fwSrc"
        return $false
    }

    $list = @()
    foreach ($f in @($MF.framework)) { $list += (Join-Path $SilDir ($f -replace '/', '\')) }
    foreach ($m in @($proj.logic)) { $list += (Join-Path $fwSrc "$m.c") }
    foreach ($t in @($proj.suites)) { $list += (Join-Path $SilDir (([string]$t.file) -replace '/', '\')) }
    $list += (Join-Path $SilDir (([string]$proj.runner) -replace '/', '\'))
    $missing = @($list | Where-Object { -not (Test-Path $_) })
    if ($missing.Count -gt 0) {
        Write-Host "[sil] $($proj.name) declares sources that do not exist:"
        foreach ($m in $missing) { Write-Host "  - $m" }
        return $false
    }

    $outDir = Join-Path $SilDir 'build-sil-msvc'
    New-Item -ItemType Directory -Force -Path $outDir | Out-Null
    $exe = Join-Path $outDir ("$($proj.cmake_exe).exe")

    # -I to BOTH the host framework dir (fw_test.h) and the firmware source dir.
    $hostFwDir = Join-Path $Here '..\tests\host\framework'
    $clArgs = @('/nologo', '/W3', '/Od', '/utf-8',
                "-I$hostFwDir", "-I$fwSrc",
                "/Fe:$exe", "/Fo:$outDir\\") + $list

    Write-Host "[sil] building $($proj.name) with cl.exe ($($msvc.Version)) - $($list.Count) sources"
    $clOut = & cl.exe @clArgs 2>&1
    $clExit = $LASTEXITCODE
    $clOut | ForEach-Object { Write-Host $_ }
    if ($clExit -ne 0) { return $false }
    if (-not (Test-Path $exe)) { return $false }

    $runOut = & $exe 2>&1
    $runExit = $LASTEXITCODE
    $runText = ($runOut | Out-String)

    # Trust the runner's own verdict line, not just its exit code (ADR 0008).
    if ($runText -match 'failures:\s*(\d+)') {
        if ([int]$Matches[1] -gt 0) { $runExit = 1 }
    }
    elseif ($runText -match '\-\>\s*FAIL') {
        $runExit = 1
    }

    Update-TotalsFromVerdict -Text $runText -RunExit $runExit

    $log = Join-Path $outDir ("last-sil-log-$Index.txt")
    $runOut | Set-Content -Path $log -Encoding UTF8

    $verdict = if ($runExit -eq 0) { 'PASS' } else { 'FAIL' }
    $cases = '-'; $fails = '-'
    if ($runText -match 'cases:\s*(\d+),\s*failures:\s*(\d+)') {
        $cases = $Matches[1]; $fails = $Matches[2]
    }
    Write-Host "[sil] $($proj.name) runner exit $runExit ($cases cases, $fails failures) -> $verdict"

    return ($runExit -eq 0)
}

$msvc = Find-MsvcHost
if (-not $msvc) {
    Write-Output "SIL RESULT: FAIL (no host C compiler found)"
    exit 1
}

$ok = $true
$index = 0
foreach ($proj in @($MF.projects)) {
    if (-not (Invoke-SilProject -msvc $msvc -proj $proj -Index $index)) { $ok = $false }
    $index++
}

# Aggregate verdict FIRST (verify.ps1 parses the first countable line).
if ($script:TotalFailures -gt 0 -or -not $ok) {
    Write-Output ("cases: {0}, failures: {1} -> FAIL (all SIL projects aggregated)" -f $script:TotalCases, $script:TotalFailures)
    Write-Output "SIL RESULT: FAIL"
    exit 1
}
Write-Output ("cases: {0}, failures: {1} -> PASS (all SIL projects aggregated)" -f $script:TotalCases, $script:TotalFailures)
Write-Output "SIL RESULT: PASS"
exit 0
