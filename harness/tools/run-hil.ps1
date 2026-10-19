# run-hil.ps1 - one-command hardware-in-the-loop gate for f280039c-harness
#
# Flow:
#   1. resolve CCS / ccxml / .out through the same ToolDetect as ccs.ps1
#   2. open the XDS110 virtual COM port (target SCI, 115200-8N1) and capture
#      everything the firmware prints for the whole session
#   3. run dss.bat hil_test.js  -> connect, program flash, run, verify
#      self-test code / LED polarity / chase cadence / PC, leave board running
#   4. check the serial log for banner + self-test OK + heartbeat lines
#   5. write an evidence record and refresh state.json (hil section)
#
# The capture window must cover the whole DSS run: dss.bat JVM startup +
# connect + flash program takes 20-60 s before the firmware even boots, and
# the banner we want is printed (a) at the first run after loadProgram and
# (b) again at H6's standalone re-boot. -SerialSeconds 20 ended before either
# (empty SCI log, 2026-10-17); the default is now 90 s.
#
# Usage:
#   .\harness\tools\run-hil.ps1 [-Project <dir>] [-Config CPU1_FLASH] [-SerialSeconds 90]
#
# Exit codes: 0 = HIL PASS, 1 = HIL FAIL, 2 = usage/environment error,
#             3 = UNAVAILABLE (no debug probe - precondition, not a failure).

[CmdletBinding()]
param(
    [string]$Project = 'projects\launchxl-f280039c-led-chaser',
    [ValidateSet('CPU1_FLASH', 'CPU1_RAM')]
    [string]$Config = 'CPU1_FLASH',
    [int]$SerialSeconds = 90,
    [string]$ComPort = ''
)

$ErrorActionPreference = 'Continue'
$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)   # harness\tools -> repo root

# Same -Project tolerance as ccs.ps1 Get-ProjDir: repo-relative, absolute, or the
# bare project name (the spelling used by static-check.ps1 and spec.json).
# PS 5.1 Join-Path splices an absolute child into the parent instead of replacing.
if ([System.IO.Path]::IsPathRooted($Project)) { $ProjDir = $Project }
else {
    $ProjDir = Join-Path $RepoRoot $Project
    if (-not (Test-Path (Join-Path $ProjDir 'Makefile'))) {
        $alt = Join-Path $RepoRoot (Join-Path 'projects' $Project)
        if (Test-Path (Join-Path $alt 'Makefile')) { $ProjDir = $alt }
    }
}

. "$PSScriptRoot\lib\ToolDetect.ps1"   # Find-CcsRoot / Get-C28xCompiler / Find-C2000Ware

function Fail([string]$msg) { Write-Host "[FAIL] $msg" -ForegroundColor Red; exit 2 }
function Info([string]$msg) { Write-Host "[hil ] $msg" -ForegroundColor Cyan }

# ---------------------------------------------------------------- environment
$ccsRoot = Find-CcsRoot
if (-not $ccsRoot) { Fail "CCS root not found (run doctor.ps1)" }
$dssBat = Join-Path $ccsRoot 'ccs_base\scripting\bin\dss.bat'
if (-not (Test-Path $dssBat)) { Fail "dss.bat missing: $dssBat" }

$ccxml = Join-Path $ProjDir 'targetConfigs\TMS320F280039C_LaunchPad.ccxml'
if (-not (Test-Path $ccxml)) { Fail "ccxml not found: $ccxml" }
$out = Get-ChildItem (Join-Path $ProjDir "$Config\*.out") | Sort-Object LastWriteTime | Select-Object -Last 1
if (-not $out) { Fail "no .out under $ProjDir\$Config (run 'ccs.ps1 build' first)" }

# ---------------------------------------------------------------- serial port
if ([string]::IsNullOrWhiteSpace($ComPort)) {
    $pnp = Get-CimInstance Win32_PnPEntity |
        Where-Object { $_.Name -match 'XDS110 Class Application/User UART \(COM\d+\)' } |
        Select-Object -First 1
    if ($pnp) {
        $ComPort = [regex]::Match($pnp.Name, 'COM\d+').Value
        Info "detected XDS110 target UART on $ComPort"
    }
    else {
        $ComPort = 'COM5'
        Info "XDS110 UART not auto-detected, trying fallback $ComPort"
    }
}

$stamp    = Get-Date -Format 'yyyy-MM-dd_HHmmss'
$recDir   = Join-Path $RepoRoot 'harness\tests\on-target\records'
if (-not (Test-Path $recDir)) { New-Item -ItemType Directory -Path $recDir | Out-Null }
$dssLog   = Join-Path $recDir "hil-dss-$stamp.log"
$sciLog   = Join-Path $recDir "hil-sci-$stamp.log"
$report   = Join-Path $recDir "hil-$($stamp.Substring(0,10))-led-chaser.md"

# --------------------------------- AIDLC: honest UNAVAILABLE (never a false FAIL)
# A missing/absent debug probe is a PRECONDITION failure, not a firmware
# failure. Recording it as 'fail' makes the evidence trail claim "the firmware
# is broken" when the truth is "there is no board attached" - and a false FAIL
# is worse than an honest UNAVAILABLE, because the Curate phase (ADR, records,
# state.json) is built on trusting those records. state.schema.json already
# defines hil.status = 'unavailable' for exactly this case; use it.
function Invoke-Unavailable {
    param([string]$Reason)

    $md = @"
# HIL record - launchxl-f280039c-led-chaser

- date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
- operator: automated (run-hil.ps1)
- board: LAUNCHXL-F280039C / TMS320F280039C (cJTAG 2-pin)
- image: $($out.FullName)
- overall: **UNAVAILABLE** - hardware precondition not met, NOT a firmware failure

## reason

$Reason

## notes

- HIL was NOT executed: this record holds no evidence for or against this image.
- Do NOT read it as a regression. Re-run once the probe is attached.
- state.json: hil.status = 'unavailable'
"@
    Set-Content -Path $report -Value $md -Encoding UTF8

    $stPath = Join-Path $ProjDir 'state.json'
    if (Test-Path $stPath) {
        # explicit -Encoding: PS 5.1 decodes BOM-less UTF-8 as ANSI(GBK), and this
        # path re-serializes a truth source that carries Chinese notes.
        $st = Get-Content -Raw -Encoding UTF8 $stPath | ConvertFrom-Json
        $st.meta.updated_at = (Get-Date -Format 'yyyy-MM-dd')
        $st.hil = [pscustomobject]@{
            status      = 'unavailable'
            probe       = $null
            image       = $null
            record      = $null
            checks      = $null
            fail_reason = $Reason
            at          = (Get-Date -Format 'yyyy-MM-dd')
        }
        $st | ConvertTo-Json -Depth 6 | Set-Content -Path $stPath -Encoding UTF8
    }

    Write-Host ""
    Write-Host "[hil ] HARDWARE-IN-THE-LOOP: UNAVAILABLE - $Reason" -ForegroundColor Yellow
    Write-Host "[hil ] (no evidence either way; re-run after attaching the probe)" -ForegroundColor Yellow
    exit 3
}

# ---------------------------------------------- probe availability precheck
$Xdsdfu = Join-Path $ccsRoot 'ccs_base\common\uscif\xds110\xdsdfu.exe'
if (Test-Path $Xdsdfu) {
    $enumOut = (& $Xdsdfu -e 2>$null | Out-String)
    if ($enumOut -match 'Found\s+(\d+)\s+device') {
        if ([int]$Matches[1] -lt 1) {
            Invoke-Unavailable "XDS110 debug probe not connected (xdsdfu: Found 0 devices)"
        }
        else { Info "debug probe present ($($Matches[1]) device(s))" }
    }
}

# ------------------------------------------------- 1) start SCI capture (bg)
$serialOk = $false
$serialJob = $null
if ($ComPort) {
    $serialJob = Start-Job -ScriptBlock {
        param($portName, $seconds, $outFile)
        try {
            $port = New-Object System.IO.Ports.SerialPort $portName, 115200, 'None', 8, 'One'
            $port.ReadTimeout = 200
            $port.Open()
            $sb = New-Object System.Text.StringBuilder
            $deadline = (Get-Date).AddSeconds($seconds)
            while ((Get-Date) -lt $deadline) {
                try { [void]$sb.Append($port.ReadExisting()) } catch { }
                Start-Sleep -Milliseconds 50
            }
            try { $port.Close() } catch { }
            [System.IO.File]::WriteAllText($outFile, $sb.ToString())
            'SERIAL-CAPTURE-OK'
        }
        catch {
            'SERIAL-CAPTURE-FAILED: ' + $_.Exception.Message
        }
    } -ArgumentList $ComPort, $SerialSeconds, $sciLog
    Info "SCI capture started on $ComPort for $SerialSeconds s"
}

# ------------------------------------------------- 2) run the DSS HIL script
Info "running DSS HIL test (program + verify) ..."
$dssArgs = @("$PSScriptRoot\dss\hil_test.js", $ccxml, $out.FullName)
$dssOutput = & $dssBat @dssArgs 2>&1 | ForEach-Object { "$_" }
$dssOutput | Tee-Object -FilePath $dssLog | ForEach-Object { Write-Host "  $_" }
$dssText = ($dssOutput | Out-String)

# A probe that vanished mid-run is the same precondition failure.
if ($dssText -match 'Error\s+-260') {
    Invoke-Unavailable "XDS110 disappeared during the run (Error -260 @ 0x0)"
}

# ------------------------------------------------- 3) wait for SCI capture
if ($serialJob) {
    Info "waiting for SCI capture to finish ..."
    Wait-Job $serialJob | Out-Null
    $serialState = Receive-Job $serialJob
    Remove-Job $serialJob -Force | Out-Null
    Write-Host "  serial job: $serialState"
    $serialOk = ($serialState -match 'SERIAL-CAPTURE-OK') -and (Test-Path $sciLog)
}

# ------------------------------------------------- 4) evaluate results
$hilPass = ($dssText -match 'HIL RESULT:\s*PASS')
$failReason = ''
if (-not $hilPass) {
    $m = [regex]::Match($dssText, 'HIL RESULT:\s*FAIL\s*(.*)')
    $failReason = if ($m.Success) { $m.Groups[1].Value.Trim() } else { 'no HIL RESULT line (dss crashed?)' }
}

# File::WriteAllText produced BOM-less UTF-8; without -Encoding, PS 5.1 would
# decode any non-ASCII target byte as GBK and the expected-line match would lie.
$sciText = if ($serialOk) { Get-Content -Raw -Encoding UTF8 $sciLog } else { '' }
$serialChecks = [ordered]@{
    'banner'        = ($sciText -match 'LAUNCHXL-F280039C LED chaser')
    'sysclk=120MHz' = ($sciText -match 'sysclk\s*:\s*120000000')
    'step=200ms'    = ($sciText -match 'step\s*:\s*200\s*ms')
    'selftest-pattern-OK' = ($sciText -match 'self-test:\s*pattern OK')
    'selftest-fmt-OK'     = ($sciText -match 'self-test:\s*fmt OK')
    'heartbeat-alive'     = ($sciText -match 'alive,\s*steps=')
}
$serialPass = $true
foreach ($k in $serialChecks.Keys) { if (-not $serialChecks[$k]) { $serialPass = $false } }

$overall = if ($hilPass -and ($serialOk -eq $false -or $serialPass)) { 'PASS' } else { 'FAIL' }

# ------------------------------------------------- 5) evidence record
$pcLine  = ($dssText -split "`n" | Where-Object { $_ -match '^PC\s*=' } | Select-Object -First 1)
$stLine  = ($dssText -split "`n" | Where-Object { $_ -match 'g_self_test_code' } | Select-Object -First 1)
$cadLine = ($dssText -split "`n" | Where-Object { $_ -match 'transition period' } | Select-Object -First 1)
$banner  = ($sciText -split "`r?`n" | Where-Object { $_ -match 'LAUNCHXL-F280039C LED chaser|self-test:|alive,|sysclk|step\s+:' } | Select-Object -First 12)

$serialMd = 'serial capture skipped'
if ($serialOk) {
$serialMd = @"
| expected line | found |
|---|---|
| boot banner | $(if ($serialChecks['banner']) {'yes'} else {'NO'}) |
| sysclk 120000000 Hz | $(if ($serialChecks['sysclk=120MHz']) {'yes'} else {'NO'}) |
| step 200 ms | $(if ($serialChecks['step=200ms']) {'yes'} else {'NO'}) |
| self-test: pattern OK | $(if ($serialChecks['selftest-pattern-OK']) {'yes'} else {'NO'}) |
| self-test: fmt OK | $(if ($serialChecks['selftest-fmt-OK']) {'yes'} else {'NO'}) |
| alive, steps= heartbeat | $(if ($serialChecks['heartbeat-alive']) {'yes'} else {'NO'}) |

```
$($banner -join "`n")
```
"@
}

$md = @"
# HIL record - launchxl-f280039c-led-chaser

- date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
- operator: automated (run-hil.ps1 + dss hil_test.js)
- board: LAUNCHXL-F280039C / TMS320F280039C (cJTAG 2-pin)
- image: $($out.FullName)
- overall: **$overall**

## JTAG / flash / logic checks (DSS)

| check | result |
|---|---|
| H1 JTAG connect (cJTAG) | $(if ($dssText -match 'H1 PASS') {'PASS'} else {'FAIL'}) |
| H2 flash programmed (code_start non-blank) | $(if ($dssText -match 'H2 PASS') {'PASS'} else {'FAIL'}) |
| H3 power-on self test (g_self_test_code==0) | $(if ($dssText -match 'H3 PASS') {'PASS'} else {'FAIL'}) |
| H4 PC inside application main loop | $(if ($dssText -match 'H4 PASS') {'PASS'} else {'FAIL'}) |
| H5 LED polarity + 200ms cadence | $(if ($dssText -match 'H5 PASS') {'PASS'} else {'FAIL'}) |
| H6 left running standalone | $(if ($dssText -match 'H6 PASS') {'PASS'} else {'FAIL'}) |

- $stLine
- $pcLine
- $cadLine
- fail reason: $(if ($failReason) { $failReason } else { 'n/a' })

## SCI console log ($ComPort)

capture: $(if ($serialOk) { "OK ($sciLog)" } else { 'unavailable - port busy or absent' })

$serialMd

## artifacts

- dss log: $dssLog
- sci log: $(if ($serialOk) { $sciLog } else { 'n/a' })
"@

Set-Content -Path $report -Value $md -Encoding UTF8
Info "evidence record: $report"

# ------------------------------------------------- 6) refresh state.json
$statePath = Join-Path $ProjDir 'state.json'
if (Test-Path $statePath) {
    # -Encoding UTF8 is load-bearing: state.json is a truth source holding
    # Chinese notes, and PS 5.1 decodes BOM-less UTF-8 as ANSI(GBK) by default.
    # Without this, a state.json saved by an editor that drops the BOM would be
    # re-serialized here with mojibake notes.
    $state = Get-Content -Raw -Encoding UTF8 $statePath | ConvertFrom-Json
    $state.meta.updated_at = (Get-Date -Format 'yyyy-MM-dd')
    $hilSection = [pscustomobject]@{
        status  = if ($overall -eq 'PASS') { 'pass' } else { 'fail' }
        probe   = 'XDS110 (cJTAG)'
        image   = $out.FullName.Replace("$RepoRoot\", '').Replace('\', '/')
        record  = $report.Replace("$RepoRoot\", '').Replace('\', '/')
        checks  = [pscustomobject]@{
            jtag     = ($dssText -match 'H1 PASS')
            flash    = ($dssText -match 'H2 PASS')
            selftest = ($dssText -match 'H3 PASS')
            loop     = ($dssText -match 'H4 PASS')
            polarity = ($dssText -match 'H5 PASS')
            sci      = if ($serialOk) { $serialPass } else { $null }
        }
        fail_reason = if ($failReason) { $failReason } else { $null }
        at = (Get-Date -Format 'yyyy-MM-dd')
    }
    if ($state.PSObject.Properties.Name -contains 'hil') {
        $state.hil = $hilSection
    } else {
        Add-Member -InputObject $state -NotePropertyName 'hil' -NotePropertyValue $hilSection
    }
    $state | ConvertTo-Json -Depth 6 | Set-Content -Path $statePath -Encoding UTF8
    Info "state.json updated (hil=$($hilSection.status))"
}

# ------------------------------------------------- 7) verdict
Write-Host ""
if ($overall -eq 'PASS') {
    Write-Host "[hil ] ================================================" -ForegroundColor Green
    Write-Host "[hil ]  HARDWARE-IN-THE-LOOP: PASS" -ForegroundColor Green
    Write-Host "[hil ]  board is running the chaser standalone" -ForegroundColor Green
    Write-Host "[hil ] ================================================" -ForegroundColor Green
    exit 0
}
else {
    Write-Host "[hil ] HARDWARE-IN-THE-LOOP: FAIL - $failReason" -ForegroundColor Red
    if ($serialOk -and -not $serialPass) {
        Write-Host "[hil ] serial log missing expected lines, see $sciLog" -ForegroundColor Red
    }
    exit 1
}
