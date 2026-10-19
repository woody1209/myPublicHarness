# doctor.ps1 - environment self-check for the f280039c-harness workspace
#
# Verifies the local development environment and writes a machine-readable
# snapshot to harness/env/manifest.json. Run this FIRST in any session and
# refresh whenever tools change. AI agents: if this script fails, fix the
# environment before doing anything else - never guess tool paths.

[CmdletBinding()]
param(
    # Left empty on purpose: PowerShell evaluates parameter defaults before
    # binding $PSScriptRoot, so "powershell -File doctor.ps1" used to die with
    # Join-Path's ParameterArgumentValidationErrorEmptyStringNotAllowed
    # (fixed 2026-10-17). The default is resolved in the body instead.
    [string]$OutFile = ''
)

$ErrorActionPreference = 'Continue'
$ToolsDir = if ($PSScriptRoot) { $PSScriptRoot } else { Split-Path -Parent $MyInvocation.MyCommand.Path }
if (-not $OutFile) { $OutFile = [System.IO.Path]::GetFullPath((Join-Path $ToolsDir '..\env\manifest.json')) }
$RepoRoot = Split-Path -Parent (Split-Path -Parent $ToolsDir)   # harness\tools -> repo root

. (Join-Path $ToolsDir 'lib\ToolDetect.ps1')   # Find-CMake / Find-HostCC / Find-CcsRoot / Get-C28xCompiler / Find-C2000Ware
. (Join-Path $ToolsDir 'lib\JsonSchema.ps1')   # Test-JsonFileAgainstSchema - the manifest is a contract, not a dump

$report = [ordered]@{
    generated = (Get-Date).ToString('s')
    os        = "$($env:OS) $([System.Environment]::OSVersion.Version)"
    repo_root = (Resolve-Path $RepoRoot).Path
    ccs       = $null
    toolchain = @{}
    sdk       = @{}
    tools     = @{}
    probes    = @()
    workspace = [ordered]@{
        app_dirs = @()
    }
    problems  = @()
}

function Add-Problem([string]$msg) {
    $script:report.problems += $msg
    Write-Host "[FAIL] $msg" -ForegroundColor Red
}
function OK([string]$msg) { Write-Host "[ OK ] $msg" -ForegroundColor Green }

# ---- Code Composer Studio ---------------------------------------------------
$ccsRoot = Find-CcsRoot
if (-not $ccsRoot) { Add-Problem "Code Composer Studio root not found (set CCS_ROOT to help detection)" }
else {
    $ver = Get-CcsVersion $ccsRoot
    $report.ccs = [ordered]@{ root = $ccsRoot; version = $ver }
    OK "Code Composer Studio $ver at $ccsRoot"
}

# ---- C28x compiler ----------------------------------------------------------
if ($ccsRoot) {
    $cl = Get-C28xCompiler $ccsRoot
    if ($cl) {
        $rev = (& $cl --compiler_revision 2>$null | Select-Object -First 1)
        $report.toolchain['c2000'] = [ordered]@{ path = $cl; version = $rev }
        OK "cl2000 $rev"
    } else { Add-Problem "C28x compiler (ti-cgt-c2000) not found under $ccsRoot\tools\compiler" }

    # ---- other tools shipped with CCS --------------------------------------
    $gmake = Join-Path $ccsRoot 'utils\bin\gmake.exe'
    if (Test-Path $gmake) { $report.tools['gmake'] = $gmake; OK "gmake -> $gmake" }
    else { Add-Problem "gmake missing: $gmake" }

    $xdsdfu = Join-Path $ccsRoot 'ccs_base\common\uscif\xds110\xdsdfu.exe'
    if (Test-Path $xdsdfu) { $report.tools['xdsdfu'] = $xdsdfu; OK "xdsdfu -> $xdsdfu" }
    else { Write-Host "[WARN] xdsdfu missing - probe detection degraded" -ForegroundColor Yellow }

    $loadti = Join-Path $ccsRoot 'ccs_base\scripting\examples\loadti\loadti.bat'
    if (Test-Path $loadti) { $report.tools['loadti'] = $loadti; OK "loadti -> $loadti" }
    else { Write-Host "[WARN] loadti missing - 'ccs.ps1 program' unavailable" -ForegroundColor Yellow }

    $syscfgDirs = Get-ChildItem (Join-Path $ccsRoot 'utils') -Directory -Filter 'sysconfig_*' -ErrorAction SilentlyContinue |
        Sort-Object Name | Select-Object -Last 1
    if ($syscfgDirs) {
        $cli = Join-Path $syscfgDirs.FullName 'sysconfig_cli.bat'
        if (Test-Path $cli) {
            $report.tools['sysconfig'] = $cli
            $report.tools['sysconfig_version'] = $syscfgDirs.Name -replace '^sysconfig_', ''
            OK "sysconfig CLI -> $cli"
        }
    }
}

# ---- C2000Ware SDK ----------------------------------------------------------
$c2000ware = Find-C2000Ware $ccsRoot
if ($c2000ware) {
    $ver = ''
    if ($c2000ware -match 'C2000Ware_(\d+)_(\d+)_(\d+)_(\d+)') {
        $ver = "$($matches[1]).$($matches[2]).$($matches[3]).$($matches[4])"
    }
    $driverlib = Join-Path $c2000ware 'driverlib\f28003x\driverlib'
    $report.sdk['c2000ware'] = [ordered]@{ root = $c2000ware; version = $ver }
    OK "C2000Ware $ver"
    if (Test-Path $driverlib) { OK "driverlib f28003x present" }
    else { Add-Problem "driverlib/f28003x missing under $c2000ware" }
} else { Add-Problem "C2000Ware not found (set C2000WARE_ROOT to help detection)" }

# ---- generic tools -----------------------------------------------------------
foreach ($t in @('git', 'python', 'py')) {
    $c = Get-Command $t -ErrorAction SilentlyContinue
    if (-not $c) { Write-Host "[INFO] $t not in PATH (optional)" -ForegroundColor Yellow; continue }
    # Get-Command also finds the Microsoft Store 'python.exe' alias stub, which is
    # not an interpreter (exits 9009 and prints nothing - measured 2026-10-17 on
    # this host, where the real interpreter is reachable as `py`). Record a tool
    # only after it actually runs, otherwise manifest.json advertises a phantom.
    & $c.Source --version 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0) { $report.tools[$t] = $c.Source; OK "$t -> $($c.Source)" }
    else { Write-Host "[INFO] $t at $($c.Source) does not run (exit $LASTEXITCODE) - not recorded" -ForegroundColor Yellow }
}

# ---- host toolchain (for host-side unit tests) -------------------------------
$cmake = Find-CMake
if ($cmake) { $report.tools['cmake'] = $cmake; OK "cmake -> $cmake" }
else { Write-Host "[WARN] cmake not found - host tests will use the MSVC cl.exe fallback (see tests/host/README.md). Install cmake for the preferred path: winget install Kitware.CMake" -ForegroundColor Yellow }

$hostCC = Find-HostCC
if ($hostCC) { $report.tools['host_cc'] = $hostCC; OK "host C compiler -> $hostCC" }
else { Write-Host "[WARN] no host C compiler - host unit tests unavailable." -ForegroundColor Yellow }

# ---- debug probes (XDS110 via xdsdfu) ---------------------------------------
if ($report.tools['xdsdfu']) {
    $raw = & $report.tools['xdsdfu'] -e 2>$null
    $cur = $null
    foreach ($line in $raw) {
        if ($line -match 'Device Name:\s*(.+)') { $cur = [ordered]@{ name = $matches[1].Trim(); serial = $null; version = $null } }
        elseif ($line -match 'Serial Num:\s*(.+)' -and $cur) { $cur.serial = $matches[1].Trim() }
        elseif ($line -match 'Version:\s*(.+)' -and $cur) { $cur.version = $matches[1].Trim() }
        elseif ($line -match 'Found \d+ device' -and $cur) {
            $report.probes += $cur
            OK "Debug probe: $($cur.name) SN=$($cur.serial) FW=$($cur.version)"
            $cur = $null
        }
    }
}
if ($report.probes.Count -eq 0) {
    Write-Host "[INFO] No XDS110 probe detected (connect LAUNCHXL-F280039C USB-C before flashing)" -ForegroundColor Yellow
}

# ---- workspace sanity --------------------------------------------------------
Get-ChildItem (Join-Path $RepoRoot 'projects') -Directory -ErrorAction SilentlyContinue |
    Where-Object { Test-Path (Join-Path $_.FullName 'Makefile') } |
    ForEach-Object { $report.workspace.app_dirs += $_.Name }
if ($report.workspace.app_dirs.Count -gt 0) { OK "projects: $($report.workspace.app_dirs -join ', ')" }
else { Add-Problem "no projects with Makefile under $RepoRoot\projects" }

$targetCard = Join-Path $RepoRoot 'targets\F280039C.json'
if (Test-Path $targetCard) { OK "target card present" }
else { Add-Problem "target card missing: $targetCard" }

# ---- write manifest -----------------------------------------------------------
$dir = Split-Path -Parent $OutFile
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
$report | ConvertTo-Json -Depth 6 | Set-Content -Path $OutFile -Encoding UTF8
Write-Host ""
Write-Host "manifest written: $OutFile"

# The manifest is the path truth source for every wrapper, so its own contract
# is checked here: a snapshot that does not validate must fail the gate, not just
# print a warning (a silently malformed manifest turns into a guessed path later).
$manifestSchema = Join-Path $RepoRoot 'harness\schemas\manifest.schema.json'
$mv = @(Test-JsonFileAgainstSchema -JsonPath $OutFile -SchemaPath $manifestSchema)
$m_bad = @($mv | Where-Object { $_ })
if ($m_bad.Count -gt 0) {
    foreach ($b in $m_bad) { Write-Host "[FAIL] manifest contract: $b" -ForegroundColor Red }
    Add-Problem "manifest.json violates harness/schemas/manifest.schema.json ($($m_bad.Count) violation(s))"
    # re-serialize: the snapshot on disk must record its own failure, not claim problems=[]
    $report | ConvertTo-Json -Depth 6 | Set-Content -Path $OutFile -Encoding UTF8
} else {
    OK "manifest validates against harness/schemas/manifest.schema.json"
}

if ($report.problems.Count -gt 0) {
    Write-Host "$($report.problems.Count) problem(s) found - see above." -ForegroundColor Red
    exit 1
}
Write-Host "environment OK" -ForegroundColor Green
exit 0
