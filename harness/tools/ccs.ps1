# ccs.ps1 - Code Composer Studio unified entry for the f280039c-harness workspace
#
# All C2000 build / flash operations in this repo MUST go through this wrapper so
# that tool discovery (CCS root, C28x compiler, C2000Ware) is injected
# consistently. Raw cl2000 / gmake calls bypass these guarantees and are
# forbidden (see AGENTS.md section 1).
#
# Usage:
#   .\ccs.ps1 env                                  show resolved environment (no build)
#   .\ccs.ps1 build    [-Project <dir>] [-Config CPU1_FLASH|CPU1_RAM]
#   .\ccs.ps1 rebuild  [-Project <dir>] [-Config ...]
#   .\ccs.ps1 clean    [-Project <dir>] [-Config ...]
#   .\ccs.ps1 size     [-Project <dir>] [-Config ...] [-MaxFlashPct 90] [-MaxRamPct 90]
#                      section sizes of last build, in C28x WORDS (1 word = 2 bytes);
#                      exits non-zero when a region exceeds its budget percentage
#   .\ccs.ps1 probe                                        list XDS110 probes (read-only)
#   .\ccs.ps1 program  [-Project <dir>] [-Config ...] -Yes build + load to target flash
#   .\ccs.ps1 qprogram [-Project <dir>] [-Config ...] -Yes load existing artifact only
#   .\ccs.ps1 erase    [-Project <dir>] -Confirm           ERASE TARGET FLASH (irreversible)
#   .\ccs.ps1 monitor                                       how to watch SCI log
#
# Safety: erase requires -Confirm; program warns unless -Yes is given.
# Exit codes: 0 = ok, non-zero = failure.

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('env', 'build', 'rebuild', 'clean', 'size', 'probe', 'program', 'qprogram', 'erase', 'monitor', 'help')]
    [string]$Verb,

    [string]$Project = 'projects\launchxl-f280039c-led-chaser',
    [ValidateSet('CPU1_FLASH', 'CPU1_RAM')]
    [string]$Config = 'CPU1_FLASH',
    [double]$MaxFlashPct = 90,
    [double]$MaxRamPct = 90,
    [switch]$Confirm,
    [switch]$Yes
)

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # harness\tools -> repo root

. "$PSScriptRoot\lib\ToolDetect.ps1"   # Find-CcsRoot / Get-C28xCompiler / Find-C2000Ware

function Fail([string]$msg) { Write-Host "[FAIL] $msg" -ForegroundColor Red; exit 1 }

function Get-Env {
    $ccsRoot = Find-CcsRoot
    if (-not $ccsRoot) { Fail "CCS root not found (run doctor.ps1, or set CCS_ROOT)" }
    $cl = Get-C28xCompiler $ccsRoot
    if (-not $cl) { Fail "C28x compiler not found under $ccsRoot" }
    # <cgt>\bin\cl2000.exe -> <cgt>  (parent #1 = bin, parent #2 = cgt root)
    $cgtRoot = Split-Path -Parent (Split-Path -Parent $cl)
    $c2000ware = Find-C2000Ware $ccsRoot
    if (-not $c2000ware) { Fail "C2000Ware not found (run doctor.ps1, or set C2000WARE_ROOT)" }
    $gmake = Join-Path $ccsRoot 'utils\bin\gmake.exe'
    if (-not (Test-Path $gmake)) { Fail "gmake missing: $gmake" }

    return [pscustomobject]@{
        CcsRoot    = $ccsRoot
        CgtRoot    = $cgtRoot
        Cl2000     = $cl
        C2000Ware  = $c2000ware
        Gmake      = $gmake
        Loadti     = Join-Path $ccsRoot 'ccs_base\scripting\examples\loadti\loadti.bat'
        Xdsdfu     = Join-Path $ccsRoot 'ccs_base\common\uscif\xds110\xdsdfu.exe'
    }
}

function Get-ProjDir([string]$Proj) {
    # -Project may be repo-relative (default) or already absolute; Windows
    # PowerShell 5.1 Join-Path splices an absolute second path INTO the first
    # (E:\repo\E:\work\...) instead of replacing it, which then reports a
    # misleading "No Makefile".
    $p = if ([System.IO.Path]::IsPathRooted($Proj)) { $Proj } else { Join-Path $RepoRoot $Proj }
    # Also accept the bare project name (the form static-check.ps1 and
    # specs/*/spec.json use) so one gate chain does not need two spellings.
    if (-not (Test-Path (Join-Path $p 'Makefile'))) {
        $alt = Join-Path $RepoRoot (Join-Path 'projects' $Proj)
        if (Test-Path (Join-Path $alt 'Makefile')) { $p = $alt }
    }
    if (-not (Test-Path (Join-Path $p 'Makefile'))) { Fail "No Makefile in $p - check -Project path." }
    return (Resolve-Path $p).Path
}

function Invoke-Gmake([string]$AppDir, [string]$Target, $Env) {
    pushd $AppDir
    try {
        Write-Host "[ccs.ps1] gmake $Target CONFIG=$Config ($AppDir)" -ForegroundColor DarkGray
        # SHELL=cmd is not optional: the generated Makefiles carry Windows
        # recipes (if not exist / mkdir), but CCS gmake takes the first sh.exe
        # it finds in PATH. Started from Git Bash / MSYS that shell ran every
        # line and the build died with "syntax error: unexpected end of file"
        # on the dirs target (2026-10-17).
        & $Env.Gmake $Target `
            "CONFIG=$Config" `
            "CCS_ROOT=$($Env.CcsRoot)" `
            "CGT_ROOT=$($Env.CgtRoot)" `
            "C2000WARE_ROOT=$($Env.C2000Ware)" `
            "SHELL=cmd"
        if ($LASTEXITCODE -ne 0) { Fail "gmake '$Target' failed (exit $LASTEXITCODE)" }
    }
    finally { popd }
}

function Get-TargetBudget {
    # Region budgets come from the hardware card, the single source of truth for
    # this target (targets/F280039C.json). Never hard-code them here.
    $card = Join-Path $RepoRoot 'targets\F280039C.json'
    if (-not (Test-Path $card)) { Fail "hardware card missing: $card" }
    $t = (Get-Content -Raw -Encoding UTF8 $card | ConvertFrom-Json).target
    if (-not $t.flash.words -or -not $t.sram.words) { Fail "hardware card lacks flash/sram word budget: $card" }
    return [pscustomobject]@{ FlashWords = [int64]$t.flash.words; RamWords = [int64]$t.sram.words }
}

function Show-Size([string]$AppDir, $Env) {
    $map = Join-Path (Join-Path $AppDir $Config) "*.map"
    $mapFile = Get-ChildItem $map -ErrorAction SilentlyContinue | Sort-Object LastWriteTime | Select-Object -Last 1
    if (-not $mapFile) { Fail "no .map under $AppDir\$Config (run 'ccs.ps1 build' first)" }

    # SECTION ALLOCATION MAP columns are: section page origin length. C28x uses
    # 16-bit WORD addressing, so origin and length are both WORDS (ADR 0002);
    # bytes = words * 2. Region is decided by the origin the linker chose.
    $pattern = '^([.A-Za-z_][A-Za-z0-9_.]*)\s+\d+\s+([0-9a-fA-F]{6,8})\s+([0-9a-fA-F]{6,8})'
    $seen = @{}
    $rows = New-Object System.Collections.ArrayList
    $raw = (Get-Content -Raw -Encoding UTF8 $mapFile.FullName) -split '\r?\n'
    for ($i = 0; $i -lt $raw.Count; $i++) {
        # A long section name wraps: name on its own line, then '* page origin length'.
        $line = $raw[$i]
        if ($line -match '^\s*([.A-Za-z_][A-Za-z0-9_.]*)\s*$') {
            $pending = $Matches[1]
            if ($i -lt ($raw.Count - 1) -and $raw[$i + 1] -match '^\s*\*\s+(\d+\s+[0-9a-fA-F]{6,8}\s+[0-9a-fA-F]{6,8})') {
                $line = ('{0} {1}' -f $pending, $Matches[1])
            }
        }
        $m = [regex]::Match($line, $pattern)
        if (-not $m.Success) { continue }
        $name = $m.Groups[1].Value
        if ($seen.ContainsKey($name)) { continue }
        $seen[$name] = $true
        $origin = [Convert]::ToInt64($m.Groups[2].Value, 16)
        $words = [Convert]::ToInt64($m.Groups[3].Value, 16)
        $region = if ($origin -ge 0x080000 -and $origin -lt 0x0B0000) { 'FLASH' }
                  elseif ($origin -ge 0x3F0000) { 'ROM/RESET' }
                  else { 'RAM' }
        [void]$rows.Add([pscustomobject]@{ Name = $name; Origin = $origin; Words = $words; Region = $region })
    }
    if ($rows.Count -eq 0) { Fail "could not parse any section from $($mapFile.Name)" }

    $budget = Get-TargetBudget
    $flashWords = [int64](($rows | Where-Object { $_.Region -eq 'FLASH' } | Measure-Object -Sum Words).Sum)
    $ramWords = [int64](($rows | Where-Object { $_.Region -eq 'RAM' } | Measure-Object -Sum Words).Sum)
    if (-not $flashWords) { $flashWords = 0L }
    if (-not $ramWords) { $ramWords = 0L }
    $flashPct = [math]::Round(100.0 * $flashWords / $budget.FlashWords, 2)
    $ramPct = [math]::Round(100.0 * $ramWords / $budget.RamWords, 2)

    Write-Host ""
    Write-Host "size report: $($mapFile.Name)  ($Config)" -ForegroundColor Cyan
    Write-Host "units: C28x 16-bit WORDS (1 word = 2 bytes) - ADR 0002" -ForegroundColor DarkGray
    Write-Host ('{0,-14} {1,-9} {2,-10} {3,8} {4,10} {5,10}' -f 'SECTION', 'REGION', 'ORIGIN', 'WORDS', 'BYTES', 'HEX')
    Write-Host ('-' * 66)
    foreach ($r in ($rows | Where-Object { $_.Words -gt 0 } | Sort-Object Region, Name)) {
        Write-Host ('{0,-14} {1,-9} {2,-10} {3,8} {4,10} {5,10}' -f `
            $r.Name, $r.Region, ('0x{0:X}' -f $r.Origin), $r.Words, ($r.Words * 2), ('0x{0:X}' -f $r.Words))
    }
    Write-Host ('-' * 66)
    Write-Host ('{0,-14} {1,-9} {2,-10} {3,8} {4,10}   of {5} words ({6} % of budget)' -f `
        'FLASH', '', '', $flashWords, ($flashWords * 2), $budget.FlashWords, $flashPct)
    Write-Host ('{0,-14} {1,-9} {2,-10} {3,8} {4,10}   of {5} words ({6} % of budget)' -f `
        'RAM', '', '', $ramWords, ($ramWords * 2), $budget.RamWords, $ramPct)
    Write-Host ""

    $bad = @()
    if ($flashPct -gt $MaxFlashPct) { $bad += "FLASH $flashPct % > $MaxFlashPct %" }
    if ($ramPct -gt $MaxRamPct) { $bad += "RAM $ramPct % > $MaxRamPct %" }

    # Machine-readable copy of this exact verdict, so downstream gates (verify.ps1)
    # consume the numbers this leg produced instead of re-parsing the map with a
    # second, drifting implementation of the same rules.
    $reportPath = Join-Path (Join-Path $AppDir $Config) 'size-report.json'
    $rep = [ordered]@{
        schema_version = '1.0'
        unit           = 'words'
        word_bytes     = 2
        map            = $mapFile.Name
        config         = $Config
        measured_at    = (Get-Date).ToString('yyyy-MM-dd HH:mm:ss')
        measured_by    = 'harness/tools/ccs.ps1 size'
        budget         = [ordered]@{ flash_words = $budget.FlashWords; ram_words = $budget.RamWords }
        flash_words    = $flashWords
        flash_pct      = $flashPct
        ram_words      = $ramWords
        ram_pct        = $ramPct
        max_flash_pct  = $MaxFlashPct
        max_ram_pct    = $MaxRamPct
        verdict        = $(if ($bad.Count -gt 0) { 'FAIL' } else { 'PASS' })
        sections       = @($rows | ForEach-Object { [ordered]@{ name = $_.Name; region = $_.Region; words = $_.Words } })
    }
    try {
        $rep | ConvertTo-Json -Depth 4 | Set-Content -Path $reportPath -Encoding UTF8
        Write-Host "machine-readable report: $reportPath" -ForegroundColor DarkGray
    } catch {
        Write-Host "[warn] could not write size-report.json: $_" -ForegroundColor DarkGray
    }

    if ($bad.Count -gt 0) { Fail ("size gate FAIL: " + ($bad -join '; ')) }
    Write-Host "size gate PASS (flash $flashPct % <= $MaxFlashPct %, ram $ramPct % <= $MaxRamPct %)" -ForegroundColor Green
    Write-Host ""
}

function Get-Ccxml([string]$AppDir) {
    # LaunchPad REQUIRES the cJTAG 2-pin variant; fall back to any ccxml if absent.
    $preferred = Join-Path $AppDir 'targetConfigs\TMS320F280039C_LaunchPad.ccxml'
    if (Test-Path $preferred) { return $preferred }
    $any = Get-ChildItem (Join-Path $AppDir 'targetConfigs') -Filter '*.ccxml' -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($any) { return $any.FullName }
    return $null
}

switch ($Verb) {
    'help' {
        Write-Host (Get-Content -Raw -Encoding UTF8 $MyInvocation.MyCommand.Path | Select-String -Pattern '^# Usage:' -Context 0, 14)
        exit 0
    }
    'env' {
        $e = Get-Env
        Write-Host "CCS root     : $($e.CcsRoot)"
        Write-Host "CGT root     : $($e.CgtRoot)"
        Write-Host "cl2000       : $($e.Cl2000)"
        Write-Host "C2000Ware    : $($e.C2000Ware)"
        Write-Host "gmake        : $($e.Gmake)"
        Write-Host "loadti       : $($e.Loadti)"
        Write-Host "xdsdfu       : $($e.Xdsdfu)"
        Write-Host "Repo root    : $RepoRoot"
        exit 0
    }
    'build'   { $e = Get-Env; $app = Get-ProjDir $Project; Invoke-Gmake $app 'all' $e }
    'rebuild' { $e = Get-Env; $app = Get-ProjDir $Project; Invoke-Gmake $app 'clean' $e; Invoke-Gmake $app 'all' $e }
    'clean'   { $e = Get-Env; $app = Get-ProjDir $Project; Invoke-Gmake $app 'clean' $e }
    'size'    { $e = Get-Env; $app = Get-ProjDir $Project; Show-Size $app $e }
    'probe' {
        $e = Get-Env
        if (-not (Test-Path $e.Xdsdfu)) { Fail "xdsdfu missing: $($e.Xdsdfu)" }
        Write-Host "[probe] read-only XDS110 enumeration (no target operation):" -ForegroundColor Cyan
        & $e.Xdsdfu -e
        exit $LASTEXITCODE
    }
    'program' {
        if (-not $Yes) {
            Write-Host "REFUSED: 'program' builds and LOADS the target flash. Re-run with -Yes." -ForegroundColor Red
            exit 1
        }
        $e = Get-Env
        $app = Get-ProjDir $Project
        Invoke-Gmake $app 'all' $e
        $ccxml = Get-Ccxml $app
        if (-not $ccxml) { Fail "no ccxml under $app\targetConfigs" }
        if (-not (Test-Path $e.Loadti)) { Fail "loadti missing: $($e.Loadti)" }
        $out = Get-ChildItem (Join-Path $app "$Config/*.out") | Sort-Object LastWriteTime | Select-Object -Last 1
        if (-not $out) { Fail "no .out under $app\$Config" }
        Write-Host "[ccs.ps1] loading $($out.FullName) via $ccxml" -ForegroundColor Cyan
        & "$($e.Loadti)" -c "$ccxml" -l "$($out.FullName)"
        if ($LASTEXITCODE -ne 0) { Fail "loadti failed (exit $LASTEXITCODE)" }
        Write-Host "program OK" -ForegroundColor Green
    }
    'qprogram' {
        if (-not $Yes) {
            Write-Host "REFUSED: 'qprogram' LOADS the target flash. Re-run with -Yes." -ForegroundColor Red
            exit 1
        }
        $e = Get-Env
        $app = Get-ProjDir $Project
        $ccxml = Get-Ccxml $app
        $out = Get-ChildItem (Join-Path $app "$Config/*.out") | Sort-Object LastWriteTime | Select-Object -Last 1
        if (-not $out) { Fail "no .out under $app\$Config (build first)" }
        Write-Host "[ccs.ps1] loading $($out.FullName) via $ccxml" -ForegroundColor Cyan
        & "$($e.Loadti)" -c "$ccxml" -l "$($out.FullName)"
        if ($LASTEXITCODE -ne 0) { Fail "loadti failed (exit $LASTEXITCODE)" }
        Write-Host "program OK" -ForegroundColor Green
    }
    'erase' {
        if (-not $Confirm) {
            Write-Host "REFUSED: erase requires -Confirm. This erases MCU internal flash irreversibly." -ForegroundColor Red
            exit 1
        }
        $e = Get-Env
        Write-Host "[ccs.ps1] erase is done through CCS/UniFlash flash tool: uniflash or 'ccs-server-cli'." -ForegroundColor Yellow
        Write-Host "  Recommended (GUI): UniFlash -> TMS320F280039C / XDS110 -> Erase Flash."
        Write-Host "  This wrapper intentionally does not automate destructive erase."
        exit 0
    }
    'monitor' {
        Write-Host "SCI log (115200-8N1) on the XDS110 virtual COM port:"
        Write-Host "  1) Device Manager -> Ports (COM & LPT) -> 'XDS110 Class Auxiliary Data Port' (for TX side use 'USB Serial Port')"
        Write-Host "  2) Open with any terminal: 115200, 8N1, no flow control"
        Write-Host "  3) Expected: boot banner + 'self-test: pattern OK' + periodic 'alive' lines"
        exit 0
    }
}
