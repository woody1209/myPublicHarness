# review.ps1 - machine-readable CODE REVIEW gate
#
# Distinct from static-check.ps1 on purpose:
#   static-check = CONFORMANCE (does the tree obey the harness contract:
#                  layering, test manifest, config schema, dead links, /analyze)
#   review       = CRAFT/CONTRACT (is this code reviewable, traceable and
#                  honest about what it has not finished)
#
# Both are gates. Neither re-implements the other.
#
# Severity model (ADR 0009: a gate that cannot fail is not a gate):
#   ERROR  - fails the leg. A finding here means a human reviewer would
#            legitimately block the change.
#   WARN   - reported, counted, NOT blocking. Used for debt that is real but
#            explicitly scheduled (it is listed in the audit doc's backlog).
#
# Verdict lines (parsed by verify.ps1):
#   REVIEW OK (N findings)      - N == 0 ERROR findings
#   REVIEW FAILED (N findings)  - N >= 1 ERROR findings
#
# Usage:
#   .\harness\tools\review.ps1 [-Project <name>]   # default: ALL projects
#   Exit: 0 = OK, 1 = FAILED, 2 = usage/internal error

[CmdletBinding()]
param(
    [string]$Project = ''
)

$ErrorActionPreference = 'Continue'
$Here = $PSScriptRoot
$RepoRoot = Split-Path -Parent (Split-Path -Parent $Here)

try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

# --------------------------------------------------------------- file selection
if ($Project) {
    $ProjName = Split-Path -Leaf ($Project.TrimEnd('\', '/'))
    $projDirs = @(Join-Path $RepoRoot "projects\$ProjName")
    if (-not (Test-Path $projDirs[0])) {
        Write-Output "REVIEW FAILED (0 findings)"
        Write-Output "project not found: $ProjName"
        exit 2
    }
}
else {
    $projDirs = @(Get-ChildItem -Path (Join-Path $RepoRoot 'projects') -Directory | ForEach-Object { $_.FullName })
}

$SrcFiles = @()
foreach ($d in $projDirs) {
    $sd = Join-Path $d 'source'
    if (Test-Path $sd) {
        $SrcFiles += @(Get-ChildItem -Path $sd -File -Include *.c, *.h -Recurse)
    }
}
if ($SrcFiles.Count -eq 0) {
    Write-Output "REVIEW FAILED (0 findings)"
    Write-Output "no firmware sources found - refusing to report a green review over nothing"
    exit 2
}

$findings = New-Object System.Collections.ArrayList

function Add-Finding {
    param([string]$Rule, [string]$Severity, [string]$File, [int]$Line, [string]$Msg, [string]$Why)
    [void]$findings.Add([pscustomobject]@{
        Rule = $Rule; Severity = $Severity
        Loc = ("{0}:{1}" -f ($File -replace [regex]::Escape($RepoRoot + '\'), '' -replace '\\', '/'), $Line)
        Msg = $Msg; Why = $Why
    })
}

function Rel-Path { param([string]$Full) return ($Full -replace [regex]::Escape($RepoRoot + '\'), '' -replace '\\', '/') }

# ------------------------------------------------------------- spec trace index
# WARN-level rule REVIEW-SPEC-UNTRACED needs to know which sources any spec
# already claims. Built once from every specs/*/trace.json.
$tracedFiles = New-Object System.Collections.Generic.HashSet[string]
Get-ChildItem -Path (Join-Path $RepoRoot 'specs') -Directory | ForEach-Object {
    $tj = Join-Path $_.FullName 'trace.json'
    if (Test-Path $tj) {
        try {
            $t = Get-Content -Raw -Encoding UTF8 $tj | ConvertFrom-Json
            foreach ($tr in @($t.traces)) {
                foreach ($f in @($tr.files)) { [void]$tracedFiles.Add([string]$f) }
            }
        }
        catch { }
    }
}

# ------------------------------------------------------------------- the rules
foreach ($f in $SrcFiles) {
    $raw = @(Get-Content -Encoding UTF8 $f.FullName)
    $rel = Rel-Path $f.FullName
    $base = $f.Name

    # --- R1 (ERROR) TODO/FIXME/XXX/HACK must carry a traceable reference -----
    # A bare "TODO" in a firmware tree is invisible debt: nobody can tell which
    # spec, ADR or issue owns it, so it survives every review forever.
    for ($i = 0; $i -lt $raw.Count; $i++) {
        $ln = [string]$raw[$i]
        if ($ln -match '(TODO|FIXME|XXX|HACK)\b') {
            if ($ln -notmatch '(spec\s*\d{4}|ADR\s*\d{4}|REQ-\d{3}|issue\s*#?\d+|#\d+)') {
                Add-Finding -Rule 'REVIEW-TODO-UNTRACKED' -Severity 'ERROR' -File $f.FullName -Line ($i + 1) `
                    -Msg "untracked marker: $($Matches[1])" `
                    -Why 'debt must name the spec/ADR/issue that owns it, or it never gets paid'
            }
        }
    }

    # --- R2 (ERROR) no hard-coded peripheral addresses ------------------------
    # C28x is 16-bit WORD addressed (ADR 0002). A literal like HWREGH(0x6B)
    # reads as a byte address to most reviewers and is the exact class of bug
    # that produced the motor-pwm "CMPA never changes" false FAIL.
    for ($i = 0; $i -lt $raw.Count; $i++) {
        $ln = [string]$raw[$i]
        # {2,} not {3,}: C28x word offsets like 0x6B (CMPA) are two hex digits,
        # and those are exactly the dangerous ones.
        if ($ln -match '(HWREG|HWREGH|HWREGB|HWREGBITW?)[A-Z]*\s*\(\s*0[xX][0-9A-Fa-f]{2,}') {
            Add-Finding -Rule 'REVIEW-MAGIC-REG-ADDR' -Severity 'ERROR' -File $f.FullName -Line ($i + 1) `
                -Msg "literal peripheral address passed to $($Matches[1])" `
                -Why 'use the hw_*.h offset macro: word addressing makes literals a silent bug class'
        }
    }

    # --- R3 (ERROR) every header needs an include guard -----------------------
    if ($f.Extension -eq '.h') {
        $hasGuard = $false
        foreach ($ln in $raw) {
            if ($ln -match '^\s*#ifndef\s+\w+') { $hasGuard = $true; break }
            if ($ln -match '^\s*#pragma\s+once') { $hasGuard = $true; break }
        }
        if (-not $hasGuard) {
            Add-Finding -Rule 'REVIEW-GUARD-MISSING' -Severity 'ERROR' -File $f.FullName -Line 1 `
                -Msg 'header has no #ifndef guard or #pragma once' `
                -Why 'a double include in one translation unit is a redefinition error, or worse, a silent duplicate'
        }
    }

    # --- R4 (ERROR) whole-file size ceiling -----------------------------------
    # 1000 lines is where a reviewer stops being able to hold the file in their
    # head. Not a style opinion: this is the point where review turns into
    # skimming, and skimming is how regressions ship.
    if ($raw.Count -gt 1000) {
        Add-Finding -Rule 'REVIEW-LONG-FILE' -Severity 'ERROR' -File $f.FullName -Line 1 `
            -Msg "$($raw.Count) lines exceeds the 1000-line review ceiling" `
            -Why 'split by responsibility; a file nobody can review is a file nobody reviews'
    }

    # --- R5 (WARN) source not referenced by any spec trace --------------------
    # Real debt (the derived full-platform brings 50+ untraced sources), but
    # blocking on it would strand the derivation. Scheduled, not ignored.
    $claimed = $false
    foreach ($t in $tracedFiles) { if ($t -like "*$base") { $claimed = $true; break } }
    if (-not $claimed) {
        Add-Finding -Rule 'REVIEW-SPEC-UNTRACED' -Severity 'WARN' -File $f.FullName -Line 1 `
            -Msg "$base is not claimed by any specs/*/trace.json" `
            -Why 'traceability debt, tracked in the completeness audit backlog'
    }
}

# ------------------------------------------------------------------- reporting
$errors = @($findings | Where-Object { $_.Severity -eq 'ERROR' })
$warns  = @($findings | Where-Object { $_.Severity -eq 'WARN' })

Write-Output "review: $($SrcFiles.Count) firmware sources scanned, rules: R1-TODO, R2-MAGIC-ADDR, R3-GUARD, R4-SIZE, R5-TRACE"

if ($errors.Count -gt 0) {
    Write-Output '--- ERROR findings (blocking) ---'
    foreach ($x in $errors) { Write-Output ("  {0}  {1}  {2}  [{3}]" -f $x.Rule, $x.Loc, $x.Msg, $x.Why) }
}
if ($warns.Count -gt 0) {
    Write-Output "--- WARN findings (non-blocking, $($warns.Count)) ---"
    foreach ($x in ($warns | Select-Object -First 12)) { Write-Output ("  {0}  {1}  {2}" -f $x.Rule, $x.Loc, $x.Msg) }
    if ($warns.Count -gt 12) { Write-Output "  ... $($warns.Count - 12) more (see build-verify/review.log)" }
}

if ($errors.Count -gt 0) {
    Write-Output "REVIEW FAILED ($($errors.Count) findings)"
    exit 1
}
Write-Output "REVIEW OK (0 findings)"
exit 0
