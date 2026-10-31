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
$projName = Split-Path -Leaf $ProjDir
# Project-specific DSS script: each project's H5 proves its own hardware
# chain (led-chaser: LED polarity+cadence; motor-pwm: ePWM/TZ/CMPSS register
# readback + forced over-limit experiment). The output contract is shared:
# "H1..Hn PASS" lines plus a final "HIL RESULT: PASS/FAIL <reason>".
if ($projName -eq 'launchxl-f280039c-motor-pwm') {
    $dssScriptName = 'hil_test_mp.js'
}
elseif ($projName -eq 'launchxl-f280039c-full-platform') {
    # full-platform: liveness is judged from the watchdog service counter and
    # the three subsystem status words (halp-free sampling - see the header of
    # hil_test_fp.js). It drives every peripheral on the board, so there is no
    # single "the one output" to watch the way the LED is for led-chaser.
    $dssScriptName = 'hil_test_fp.js'
}
else {
    $dssScriptName = 'hil_test.js'
}
# full-platform's init walks every peripheral before the scheduler starts, and
# its health line is on a 5 s cadence: widen the SCI capture unless the caller
# pinned it (an empty log would fail every serial check for probe reasons).
if ($projName -eq 'launchxl-f280039c-full-platform' -and
    -not $PSBoundParameters.ContainsKey('SerialSeconds')) {
    $SerialSeconds = 120
}
# motor-pwm's DSS script runs a forced over-limit experiment with a 5 s
# firmware dwell before H6's standalone re-boot, which pushes the re-printed
# banner ~15 s later than led-chaser's. Unless the caller pinned -SerialSeconds
# explicitly, widen the capture window so the H6 banner is not missed (an
# empty SCI log would fail every serial check - a probe artifact, not firmware).
if ($projName -eq 'launchxl-f280039c-motor-pwm' -and
    -not $PSBoundParameters.ContainsKey('SerialSeconds')) {
    $SerialSeconds = 120
}
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
$report   = Join-Path $recDir ("hil-{0}-{1}.md" -f $stamp.Substring(0,10), (Split-Path -Leaf $ProjDir))

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
# HIL record - $projName

- date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
- operator: automated (run-hil.ps1 + dss $dssScriptName)
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
$dssScript = Join-Path $PSScriptRoot ("dss\" + $dssScriptName)
Info "running DSS HIL test ($dssScriptName : program + verify) ..."
$dssArgs = @($dssScript, $ccxml, $out.FullName)
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
# Expected serial lines are the project's console contract (project README /
# spec). Each project must add its own map here or the serial leg will judge it
# against the wrong strings.
if ($projName -eq 'launchxl-f280039c-motor-pwm') {
    # motor-pwm console contract (specs/0003-motor-pwm-skeleton): pure-logic
    # self-test trio, ePWM register readback, CMPSS bidirectional COMPSTS
    # probe, TZ chain armed, the motor telemetry line, the heartbeat.
    # FAULT semantics: the DSS H5 stage INJECTS one over-limit trip on purpose
    # (hil_test_mp.js), so the capture must show exactly one
    # "FAULT: tz-latched" followed by "tz: cleared" - the firmware's
    # autonomous recovery. A FAULT before "tz: chain armed" would be a
    # spurious trip (fail-safe semantics violated), and a second FAULT would
    # mean the trip did not actually clear.
    $armedIdx  = $sciText.IndexOf('tz: chain armed')
    $faultIdx  = $sciText.IndexOf('FAULT: tz-latched')
    $faultCnt  = ([regex]::Matches($sciText, 'FAULT:\s*tz-latched')).Count
    $serialChecks = [ordered]@{
        'banner'              = ($sciText -match 'LAUNCHXL-F280039C motor-pwm')
        'sysclk=120MHz'       = ($sciText -match 'sysclk\s*:\s*120000000')
        'selftest-logic-OK'   = ($sciText -match 'self-test:\s*svpwm/angle/fmt OK')
        'pwm-regs-OK'         = ($sciText -match 'pwm:\s*prd=6000 db=120 pol=2 OK')
        'cmpss-bidir-OK'      = ($sciText -match 'cmpss:\s*adc=\d+\s+hi=1 lo=0 OK')
        'tz-armed'            = ($sciText -match 'tz:\s*chain armed')
        'motor-line'          = ($sciText -match 'motor:\s*ang=\d+\s+mag=\d+')
        'heartbeat-alive'     = ($sciText -match 'alive,\s*ticks=')
        'fault-injected-once' = ($faultCnt -eq 1)
        'fault-recovered'     = ($sciText -match 'tz:\s*cleared')
        'no-spurious-fault'   = ($faultIdx -lt 0 -or ($armedIdx -ge 0 -and $faultIdx -gt $armedIdx))
    }
    # state.json hil.checks key that records the H5 result for this project.
    $stateH5Key = 'pwm'
}
elseif ($projName -eq 'launchxl-f280039c-full-platform') {
    # full-platform console contract (specs/0002-full-platform): banner,
    # 120 MHz sysclk, the four self-test lines, the periodic health line and
    # the per-milestone subsystem lines. REQ-018 (windowed watchdog) bring-up
    # is OPEN, so the watchdog line is reported but NOT made a gate here: if
    # the device resets, hil_test_fp.js H5b catches it on the DSS side with a
    # concrete reason instead of this leg silently averaging it away.
    $serialChecks = [ordered]@{
        'banner'              = ($sciText -match 'LAUNCHXL-F280039C full-platform')
        'sysclk=120MHz'       = ($sciText -match 'sysclk\s*:\s*120000000')
        'selftest-pattern-OK' = ($sciText -match 'self-test:\s*pattern OK')
        'selftest-fmt-OK'     = ($sciText -match 'self-test:\s*fmt OK')
        'selftest-sched-OK'   = ($sciText -match 'self-test:\s*scheduler OK')
        'selftest-health-OK'  = ($sciText -match 'self-test:\s*health OK')
        'health-line'         = ($sciText -match 'health:\s*up=')
        'no-selftest-fail'    = ($sciText -notmatch 'self-test FAIL')
    }
    $stateH5Key = 'liveness'
}
else {
    $serialChecks = [ordered]@{
        'banner'        = ($sciText -match 'LAUNCHXL-F280039C LED chaser')
        'sysclk=120MHz' = ($sciText -match 'sysclk\s*:\s*120000000')
        'step=200ms'    = ($sciText -match 'step\s*:\s*200\s*ms')
        'selftest-pattern-OK' = ($sciText -match 'self-test:\s*pattern OK')
        'selftest-fmt-OK'     = ($sciText -match 'self-test:\s*fmt OK')
        'heartbeat-alive'     = ($sciText -match 'alive,\s*steps=')
    }
    $stateH5Key = 'polarity'
}
$serialPass = $true
foreach ($k in $serialChecks.Keys) { if (-not $serialChecks[$k]) { $serialPass = $false } }

$overall = if ($hilPass -and ($serialOk -eq $false -or $serialPass)) { 'PASS' } else { 'FAIL' }

# ------------------------------------------------- 5) evidence record
$pcLine  = ($dssText -split "`n" | Where-Object { $_ -match '^PC\s*=' } | Select-Object -First 1)
$stLine  = ($dssText -split "`n" | Where-Object { $_ -match 'g_self_test_code' } | Select-Object -First 1)
$cadLine = ($dssText -split "`n" | Where-Object { $_ -match 'transition period' } | Select-Object -First 1)
$banner  = ($sciText -split "`r?`n" | Where-Object { $_ -match 'motor-pwm|LED chaser|self-test:|alive,|sysclk|step\s+:' } | Select-Object -First 12)

# DSS check labels are project-specific (hil_test.js emits H1..Hn per project).
# H1-H4/H6 are shared; H5 is the project's own hardware proof.
if ($projName -eq 'launchxl-f280039c-motor-pwm') {
    $dssLabels = [ordered]@{
        'H1' = 'JTAG connect (cJTAG)'
        'H2' = 'flash programmed (code_start non-blank)'
        'H3' = 'power-on self test (g_self_test_code==0)'
        'H4' = 'PC inside application main loop'
        'H5' = 'ePWM/TZ/CMPSS register readback + forced over-limit trip and autonomous recovery'
        'H6' = 'left running standalone'
    }
}
elseif ($projName -eq 'launchxl-f280039c-full-platform') {
    $dssLabels = [ordered]@{
        'H1' = 'JTAG connect (cJTAG)'
        'H2' = 'flash programmed (code_start non-blank, 5 HIL symbols resolve)'
        'H3' = 'power-on self test (g_self_test_code==0)'
        'H4' = 'PC inside application main loop'
        'H5' = 'halt-free liveness: watchdog service advances, no reset, subsystem status non-zero'
        'H6' = 'left running standalone'
    }
}
else {
    $dssLabels = [ordered]@{
        'H1' = 'JTAG connect (cJTAG)'
        'H2' = 'flash programmed (code_start non-blank)'
        'H3' = 'power-on self test (g_self_test_code==0)'
        'H4' = 'PC inside application main loop'
        'H5' = 'LED polarity + 200ms cadence'
        'H6' = 'left running standalone'
    }
}
$dssRows = ($dssLabels.GetEnumerator() | ForEach-Object {
    "| $($_.Key) $($_.Value) | $(if ($dssText -match "$($_.Key) PASS") {'PASS'} else {'FAIL'}) |"
}) -join "`n"

$serialMd = 'serial capture skipped'
if ($serialOk) {
$rows = ($serialChecks.GetEnumerator() | ForEach-Object { "| $($_.Key) | $(if ($_.Value) {'yes'} else {'NO'}) |" }) -join "`n"
# A markdown fence inside a double-quoted here-string is a TRAP: the backtick
# is PowerShell's escape character there, so three backticks swallow the
# following newline (odd count -> the terminating "@ is glued onto the previous
# line and the here-string never ends; every brace after it becomes string
# content and the script dies with "missing }" hundreds of lines later).
# Interpolating the fence from a variable keeps it literal.
$fence = '```'
$serialMd = @"
| expected line | found |
|---|---|
$rows

$fence
$($banner -join "`n")
$fence
"@
}

$md = @"
# HIL record - $projName

- date: $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss')
- operator: automated (run-hil.ps1 + dss $dssScriptName)
- board: LAUNCHXL-F280039C / TMS320F280039C (cJTAG 2-pin)
- image: $($out.FullName)
- overall: **$overall**

## JTAG / flash / logic checks (DSS)

| check | result |
|---|---|
$dssRows

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
# H5's state.json key is project-specific (polarity for led-chaser, pwm for
# motor-pwm) - state.schema.json declares both as optional properties.
$hilChecks = [ordered]@{
    jtag     = ($dssText -match 'H1 PASS')
    flash    = ($dssText -match 'H2 PASS')
    selftest = ($dssText -match 'H3 PASS')
    loop     = ($dssText -match 'H4 PASS')
}
$hilChecks[$stateH5Key] = ($dssText -match 'H5 PASS')
$hilChecks['sci'] = if ($serialOk) { $serialPass } else { $null }

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
        checks  = $hilChecks
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
    Write-Host "[hil ]  board is running $projName standalone" -ForegroundColor Green
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
