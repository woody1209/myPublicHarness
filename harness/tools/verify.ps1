# verify.ps1 - the one command that IS the Definition of Done (AGENTS.md §2)
#
# Every leg is a separate gate process; this runner only chains them and reports
# each leg's OWN exit code plus the quantitative criterion it had to meet. It
# never re-implements a check, so there is exactly one place where a rule lives.
#
# Legs (in order):
#   doctor        harness/tools/doctor.ps1          env manifest matches this machine
#   review        harness/tools/review.ps1          code-review rules (0 findings)
#   static-check  harness/tools/static-check.ps1    0 rule findings + cl /analyze clean
#   sil-tests     harness/tools/run-sil-tests.ps1   SIL time-series properties hold
#   host-tests    harness/tools/run-host-tests.ps1  failures == 0 AND cases > 0
#   spec-check    harness/tools/spec.ps1 check      schema + trace + evidence paths resolve
#   state-schema  harness/tools/lib/JsonSchema.ps1  projects/<proj>/state.json validates
#   build         harness/tools/ccs.ps1 build       wrapper build exits 0
#   size          harness/tools/ccs.ps1 size        flash/ram percent <= thresholds (WORDS)
#   size-delta    projects/<proj>/state.json        |.text - baseline| <= -MaxTextDeltaWords
#   hil           harness/tools/run-hil.ps1         "HIL RESULT: PASS" (opt-in: -Hil)
#
# A leg that could not be counted is NEVER reported as PASS. "green but unable
# to fail" is the failure mode this runner exists to prevent (ADR 0009).
#
# Usage:
#   .\harness\tools\verify.ps1                # software legs, stop at first FAIL
#                                             # (hil shows NOT RUN -> exit 1, by design)
#   .\harness\tools\verify.ps1 -Hil           # the full DoD, board attached
#   .\harness\tools\verify.ps1 -All           # keep going, print the whole matrix
#   .\harness\tools\verify.ps1 -Skip build    # only for a doc-only change; the verdict
#                                             # then states what was NOT proven
#
# Exit codes: 0 = every leg PASSed, 1 = FAIL or a leg left unproven, 2 = usage error.

[CmdletBinding()]
param(
    [string]$Project = 'launchxl-f280039c-led-chaser',
    [ValidateSet('CPU1_FLASH', 'CPU1_RAM')]
    [string]$Config = 'CPU1_FLASH',
    [double]$MaxFlashPct = 90,
    [double]$MaxRamPct = 90,
    [int]$MaxTextDeltaWords = 1024,   # AGENTS.md §2: .text growth over 2KB must be registered
    [switch]$Hil,
    [switch]$All,
    [string]$Skip = '',
    [int]$HilSerialSeconds = 90
)

$ErrorActionPreference = 'Continue'
$Here = $PSScriptRoot
$RepoRoot = Split-Path -Parent (Split-Path -Parent $Here)   # harness\tools -> repo root

# Chinese finding text from spec.ps1 / static-check.ps1 is mojibake unless this
# host and its children use UTF-8 (the console default here is gb2312).
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

. "$Here\lib\JsonSchema.ps1"   # Test-JsonFileAgainstSchema

function Info([string]$m) { Write-Host "[verify] $m" -ForegroundColor Cyan }
function Bad-Usage([string]$m) { Write-Host "[verify] USAGE: $m" -ForegroundColor Red; exit 2 }

$ProjName = Split-Path -Leaf ($Project.TrimEnd('\', '/'))
if (-not $ProjName) { Bad-Usage '-Project is empty' }
$ProjDir = Join-Path $RepoRoot "projects\$ProjName"
if (-not (Test-Path (Join-Path $ProjDir 'Makefile'))) {
    Bad-Usage "no Makefile under $ProjDir (check -Project)"
}
# Child gates take the BARE project name (that is their documented contract -
# static-check.ps1 joins "projects\$Project" itself). Pass $ProjName, never the
# raw -Project spelling, or a repo-relative value doubles the path segment.
# motor-pwm's HIL script adds a ~15 s forced-trip experiment before H6's
# re-boot banner; widen its capture window unless the caller pinned it.
if ($ProjName -eq 'launchxl-f280039c-motor-pwm' -and
    -not $PSBoundParameters.ContainsKey('HilSerialSeconds')) {
    $HilSerialSeconds = 120
}

$LogDir = Join-Path $RepoRoot 'build-verify'
New-Item -ItemType Directory -Force -Path $LogDir | Out-Null

$legs = New-Object System.Collections.Generic.List[object]

# ------------------------------------------------------------------ leg runner
# Runs one gate as a child process and returns exactly one object. Nothing the
# leg prints can leak into a caller's 'if': all child output is captured and
# re-emitted with Write-Host (ADR 0008).
function Invoke-ChildLeg {
    param(
        [string]$Name,
        [string]$File,
        [string[]]$ChildArgs,
        [string]$Criterion,
        [scriptblock]$Check
    )

    $full = Join-Path $Here $File
    if (-not (Test-Path $full)) {
        return [pscustomobject]@{ Name = $Name; Status = 'FAIL'; Exit = -1
                                  Detail = "gate script missing: $File"; Criterion = $Criterion }
    }

    $child = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $full)
    if ($ChildArgs) { $child += @($ChildArgs) }
    Info "$Name  ($Criterion)"

    $out = & powershell.exe @child 2>&1
    $code = $LASTEXITCODE
    $text = ($out | Out-String)

    if ($code -eq $null) { $code = -1 }

    $text | Set-Content -Path (Join-Path $LogDir "$Name.log") -Encoding UTF8

    # Condensed echo on screen; the full log stays on disk as the artifact.
    $lines = @($out | ForEach-Object { [string]$_ } | Where-Object { $_.Trim() -ne '' })
    $show = if ($lines.Count -gt 14) { $lines[-14..-1] } else { $lines }
    foreach ($l in $show) { Write-Host "    | $l" -ForegroundColor DarkGray }
    if ($lines.Count -gt 14) { Write-Host "    | ... full log: build-verify/$Name.log" -ForegroundColor DarkGray }

    $status = if ($code -eq 0) { 'PASS' } else { 'FAIL' }
    $detail = "exit $code"
    if ($Check) {
        $why = & $Check $text $code
        if ($why) { $status = 'FAIL'; $detail = "$detail; $why" }
        elseif ($code -eq 0) { $detail = "$detail; criterion met" }
    }
    return [pscustomobject]@{ Name = $Name; Status = $status; Exit = $code; Detail = $detail; Criterion = $Criterion }
}

# Records one leg result, prints it, and (unless -All) marks the rest of the
# chain as unproven the moment a leg FAILs. Returns the status so the caller can
# drive fail-fast without reading a coloured console line.
function Add-Result {
    param([string]$Name, [string]$Status, [int]$Exit, [string]$Detail, [string]$Criterion)

    [void]$legs.Add([pscustomobject]@{ Name = $Name; Status = $Status; Exit = $Exit; Detail = $Detail; Criterion = $Criterion })
    $col = switch ($Status) { 'PASS' { 'Green' } 'FAIL' { 'Red' } default { 'Yellow' } }
    Write-Host "[verify] ${Name}: $Status ($Detail)" -ForegroundColor $col
    return $Status
}

function Add-Skipped {
    param([string]$Name, [string]$Criterion, [string]$Why)
    [void]$legs.Add([pscustomobject]@{ Name = $Name; Status = 'SKIPPED'; Exit = -1; Detail = $Why; Criterion = $Criterion })
    Info "$Name SKIPPED ($Why) - its criterion is NOT proven"
    return 'SKIPPED'
}

# ------------------------------------------------------------------ gate table
# One row per DoD item. 'Script' = runs in this process (it needs lib/JsonSchema
# or the size artifact); otherwise the leg is an out-of-process gate and its OWN
# exit code is the verdict. Args are spelled exactly as a human would type them
# because PowerShell -File hard-errors on an unknown named parameter.
$gates = @(
    [pscustomobject]@{
        Name = 'doctor'; Script = 'doctor.ps1'
        Args = @()
        Criterion = 'manifest.json regenerated and every tool path resolved on THIS machine'
        Check = { param($t, $c) if ($t -notmatch 'environment OK') { return 'no "environment OK" verdict' }; return $null }
    }
    [pscustomobject]@{
        Name = 'review'; Script = 'review.ps1'
        Args = @('-Project', $ProjName)
        Criterion = '0 ERROR-level review findings (WARN debt is reported, not blocking)'
        Check = { param($t, $c)
                  if ($t -match 'REVIEW FAILED \((\d+)') { return "$($Matches[1]) review finding(s), see build-verify/review.log" }
                  if ($t -notmatch 'REVIEW OK \((\d+) findings\)') { return 'no machine-readable review verdict line' }
                  if ([int]$Matches[1] -ne 0) { return "findings=$($Matches[1])" }
                  return $null }
    }
    [pscustomobject]@{
        Name = 'static-check'; Script = 'static-check.ps1'
        Args = @('-Project', $ProjName)
        Criterion = '0 rule violations AND cl /analyze clean over the host-testable sources'
        Check = { param($t, $c)
                  if ($t -match 'STATIC CHECK FAILED \((\d+)') { return "$($Matches[1]) finding(s), see build-verify/static-check.log" }
                  if ($t -notmatch 'STATIC CHECK OK \((\d+) violations\)') { return 'no machine-readable verdict line' }
                  if ([int]$Matches[1] -ne 0) { return "violations=$($Matches[1])" }
                  return $null }
    }
    [pscustomobject]@{
        Name = 'sil-tests'; Script = 'run-sil-tests.ps1'
        Args = @()
        Criterion = '"SIL RESULT: PASS" AND cases > 0 (time-series properties hold in the loop)'
        Check = { param($t, $c)
                  if ($t -match 'cases:\s*(\d+),\s*failures:\s*(\d+)') {
                      $cases = [int]$Matches[1]; $failures = [int]$Matches[2]
                      if ($failures -gt 0) { return "failures=$failures" }
                      if ($cases -le 0) { return 'cases=0 - no SIL scenario actually ran' }
                  }
                  else { return 'no countable SIL verdict line' }
                  if ($t -notmatch 'SIL RESULT: PASS') { return 'no "SIL RESULT: PASS" verdict' }
                  return $null }
    }
    [pscustomobject]@{
        Name = 'host-tests'; Script = 'run-host-tests.ps1'
        Args = @()
        Criterion = 'failures == 0 AND cases > 0 (a suite that ran 0 cases is not a pass)'
        Check = { param($t, $c)
                  if ($t -match 'cases:\s*(\d+),\s*failures:\s*(\d+)') {
                      $cases = [int]$Matches[1]; $failures = [int]$Matches[2]
                      if ($failures -gt 0) { return "failures=$failures" }
                      if ($cases -le 0) { return 'cases=0 - no test actually ran' }
                      return $null
                  }
                  return 'no "cases: N, failures: M" verdict line - cannot count' }
    }
    [pscustomobject]@{
        Name = 'spec-check'; Script = 'spec.ps1'
        Args = @('check', '-Project', $ProjName)
        Criterion = 'spec.json/trace.json validate, every REQ traced, every evidence path exists'
        Check = { param($t, $c) if ($t -notmatch 'CHECK OK') { return 'no CHECK OK verdict' }; return $null }
    }
    [pscustomobject]@{
        Name = 'state-schema'; Script = ''
        Args = @()
        Criterion = 'projects/<proj>/state.json validates against state.schema.json'
        Check = $null
    }
    [pscustomobject]@{
        Name = 'build'; Script = 'ccs.ps1'
        Args = @('build', '-Project', $ProjName, '-Config', $Config)
        Criterion = "the wrapper build target completes with exit 0 for $Config"
        Check = { param($t, $c) if ($t -match '\berror\b.*\*\*\*') { return 'compiler/linker error in the build log' }; return $null }
    }
    [pscustomobject]@{
        Name = 'size'; Script = 'ccs.ps1'
        Args = @('size', '-Project', $ProjName, '-Config', $Config, '-MaxFlashPct', "$MaxFlashPct", '-MaxRamPct', "$MaxRamPct")
        Criterion = "FLASH <= $MaxFlashPct % AND RAM <= $MaxRamPct % of the hardware card, in C28x WORDS"
        Check = { param($t, $c)
                  if ($t -match 'size gate FAIL: (.+)') { return "gate reason: $($Matches[1].Trim())" }
                  if ($t -notmatch 'size gate PASS \(flash ([\d.]+) %') { return 'no machine-readable size verdict line' }
                  return $null }
    }
    [pscustomobject]@{
        Name = 'size-delta'; Script = ''
        Args = @()
        Criterion = "|.text - state.json baseline| <= $MaxTextDeltaWords words (2 KB)"
        Check = $null
    }
    [pscustomobject]@{
        Name = 'hil'; Script = 'run-hil.ps1'
        Args = @('-Project', $ProjName, '-Config', $Config, '-SerialSeconds', "$HilSerialSeconds")
        Criterion = 'HIL RESULT: PASS with H1-H6 each PASS (exit 3 = probe absent = UNAVAILABLE)'
        Check = { param($t, $c)
                  if ($c -eq 3) { return 'probe not connected - precondition unmet, NOT a firmware pass' }
                  if ($c -eq 2) { return 'environment/usage error inside run-hil.ps1' }
                  if ($t -notmatch 'HIL RESULT:\s*PASS') { return 'no "HIL RESULT: PASS" line' }
                  foreach ($h in @('H1', 'H2', 'H3', 'H4', 'H5', 'H6')) {
                      if ($t -notmatch "$h PASS") { return "$h did not pass" }
                  }
                  return $null }
    }
)

$stopped = $false

# -Skip arrives as ONE string when the script is launched through -File (PowerShell
# does not split a comma list into string[] there), and a silently-ignored typo in a
# leg name would pretend a gate was run. Split, then reject unknown names.
$gateNames = @($gates | ForEach-Object { $_.Name })
$SkipList = @($Skip -split '[,; ]+' | Where-Object { $_ })
foreach ($s in $SkipList) {
    if ($gateNames -notcontains $s) {
        Bad-Usage "unknown leg '$s' in -Skip (legs: $($gateNames -join ', '))"
    }
}

foreach ($g in $gates) {

    if ($stopped -and -not $All) { Add-Skipped $g.Name $g.Criterion 'an earlier leg FAILED (use -All to run every leg)'; continue }

    if ($SkipList -contains $g.Name) { Add-Skipped $g.Name $g.Criterion '-Skip was given'; continue }

    # HIL is only meaningful with a board in the loop; never let its absence read
    # as a pass, and never run it unless the operator asked for it.
    if ($g.Name -eq 'hil' -and -not $Hil) {
        [void]$legs.Add([pscustomobject]@{ Name = 'hil'; Status = 'NOT RUN'; Exit = -1
                                           Detail = 'needs -Hil and an attached XDS110'; Criterion = $g.Criterion })
        Info 'hil NOT RUN - the on-target DoD item stays unproven; this verdict does not claim it'
        continue
    }

    if ($g.Script) {
        $r = Invoke-ChildLeg -Name $g.Name -File $g.Script -ChildArgs $g.Args -Criterion $g.Criterion -Check $g.Check
        $st = Add-Result $r.Name $r.Status $r.Exit $r.Detail $r.Criterion
        if ($st -eq 'FAIL') { $stopped = $true }
        continue
    }

    # ---- in-process legs ----------------------------------------------------
    if ($g.Name -eq 'state-schema') {
        $statePath = Join-Path $ProjDir 'state.json'
        $schemaPath = Join-Path $RepoRoot 'harness\schemas\state.schema.json'
        if (-not (Test-Path $statePath)) {
            $st = Add-Result $g.Name 'FAIL' 1 "missing projects\$ProjName\state.json" $g.Criterion
        }
        elseif (-not (Test-Path $schemaPath)) {
            $st = Add-Result $g.Name 'FAIL' 1 'harness/schemas/state.schema.json missing' $g.Criterion
        }
        else {
            $viol = @(Test-JsonFileAgainstSchema -JsonPath $statePath -SchemaPath $schemaPath)
            $bad = @($viol | Where-Object { $_ })
            foreach ($b in $bad) { Write-Host "    | $b" -ForegroundColor DarkGray }
            $st = Add-Result $g.Name $(if ($bad.Count) { 'FAIL' } else { 'PASS' }) $(if ($bad.Count) { 1 } else { 0 }) `
                $(if ($bad.Count) { "$($bad.Count) schema violation(s)" } else { '0 violations' }) $g.Criterion
        }
        if ($st -eq 'FAIL') { $stopped = $true }
        continue
    }

    if ($g.Name -eq 'size-delta') {
        $rep = Join-Path $ProjDir "$Config\size-report.json"
        $status = 'FAIL'; $detail = ''
        if (-not (Test-Path $rep)) {
            $detail = "no $Config\size-report.json - the 'size' leg must run first"
        }
        else {
            $r = Get-Content -Raw -Encoding UTF8 $rep | ConvertFrom-Json
            # .text may be SPLIT across flash sectors when it outgrows one
            # (the linker/map then emits .text.1/.text.2/...): sum every
            # section named .text or .text.<n>, else a >4K-word .text reads
            # as 0 and this leg lies (found 2026-10-24 on full-platform M1).
            $text = [int64](($r.sections |
                             Where-Object { $_.name -match '^\.text(\.\d+)?$' } |
                             Measure-Object -Sum words).Sum)
            if (-not $text) { $text = 0L }
            $base = $null
            $sp = Join-Path $ProjDir 'state.json'
            if (Test-Path $sp) {
                $stj = Get-Content -Raw -Encoding UTF8 $sp | ConvertFrom-Json
                if ($stj.build -and $stj.build.size) { $base = $stj.build.size.text_words }
            }
            if ($null -eq $base) {
                $detail = "state.json has no build.size.text_words baseline (current .text = $text words)"
            }
            else {
                $d = [math]::Abs($text - [int64]$base)
                if ($d -le $MaxTextDeltaWords) { $status = 'PASS'; $detail = ".text $text words, baseline $base, delta $d" }
                else {
                    $detail = ".text $text vs baseline $base = delta $d > $MaxTextDeltaWords; fix: register it in projects/$ProjName/state.json (build.size.text_words + build.note) and explain the increment in the commit message (AGENTS.md §2), then re-run"
                }
            }
        }
        $st = Add-Result $g.Name $status $(if ($status -eq 'PASS') { 0 } else { 1 }) $detail $g.Criterion
        if ($st -eq 'FAIL') { $stopped = $true }
        continue
    }
}

# ------------------------------------------------------------------- matrix ---
Write-Host ''
Write-Host "verify matrix - $ProjName ($Config)" -ForegroundColor Cyan
Write-Host ('{0,-14} {1,-9} {2}' -f 'LEG', 'STATUS', 'CRITERION | DETAIL')
Write-Host ('-' * 108)
$failed = @($legs | Where-Object { $_.Status -eq 'FAIL' })
$unproven = @($legs | Where-Object { $_.Status -in @('SKIPPED', 'NOT RUN') })
$passed = @($legs | Where-Object { $_.Status -eq 'PASS' })
foreach ($l in $legs) {
    $col = switch ($l.Status) { 'PASS' { 'Green' } 'FAIL' { 'Red' } default { 'Yellow' } }
    Write-Host ('{0,-14} {1,-9} {2} | {3}' -f $l.Name, $l.Status, $l.Criterion, $l.Detail) -ForegroundColor $col
}
Write-Host ('-' * 108)
Write-Host "logs: $LogDir" -ForegroundColor DarkGray

if ($failed.Count -gt 0) {
    Write-Host ("VERIFY FAIL - {0} leg(s): {1}" -f $failed.Count, (($failed | ForEach-Object { $_.Name }) -join ', ')) -ForegroundColor Red
    exit 1
}
if ($unproven.Count -gt 0) {
    Write-Host ("VERIFY INCOMPLETE - {0} PASS, unproven: {1}" -f `
        $passed.Count, (($unproven | ForEach-Object { "$($_.Name)[$($_.Status)]" }) -join ', ')) -ForegroundColor Yellow
    Write-Host '  DoD is NOT met by this run; close the legs above (typical: -Hil, or drop -Skip)' -ForegroundColor Yellow
    exit 1
}
Write-Host ("VERIFY OK - {0} legs PASS" -f $passed.Count) -ForegroundColor Green
exit 0
