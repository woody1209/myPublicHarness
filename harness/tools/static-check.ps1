# static-check.ps1 - deterministic static analysis gate (rules, config contract,
#                    doc routes, MSVC /analyze).
#
# Why: AGENTS.md sections 3/4 state hard rules (JTAG pins untouched, single pin
# truth, no sprintf into fixed buffers, no dynamic memory, pure-logic layer free
# of driverlib, builds only through ccs.ps1, C3M3-harness read-only). A rule that
# only exists in prose is not enforced by anything, so it drifts. This script is
# the executable form of those rules; every finding names file:line and the fix.
#
# Legs:
#   rule       : PowerShell source scanner, no external binary, always runs.
#                Also asserts the host-test module manifest (harness/tests/host/
#                modules.json) agrees with CMakeLists.txt, test_runner.c and the
#                firmware sources, so a new pure-logic module cannot go untested.
#   config     : projects/<proj>/project.config.json against its schema, plus the
#                name/path/build_config cross-checks, so the declared contract runs.
#   doc routes : every path written into AGENTS.md, harness/docs/**, specs/**/*.md and
#                the project READMEs must resolve (a map with dead routes is not a map).
#   analyze    : MSVC cl.exe /analyze /W4 over the host-compilable sources.
#                cppcheck / clang-tidy are not installed on this host; when they
#                appear, add them here rather than only to the CI template.
#
# Usage:
#   .\harness\tools\static-check.ps1 [-Project launchxl-f280039c-led-chaser] [-SkipAnalyze]
# Exit: 0 = clean, 1 = violations, 2 = usage / environment error.

[CmdletBinding()]
param(
    [string]$Project = 'launchxl-f280039c-led-chaser',
    [switch]$SkipAnalyze
)

$ErrorActionPreference = 'Continue'
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

$RepoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
. "$PSScriptRoot\lib\ToolDetect.ps1"     # Find-MsvcHost / Test-RealExe
. "$PSScriptRoot\lib\JsonSchema.ps1"     # Test-JsonFileAgainstSchema (project.config contract)

$HostTestDir = Join-Path $RepoRoot 'harness\tests\host'
$ManifestPath = Join-Path $HostTestDir 'modules.json'
$FwSrcDir = Join-Path $RepoRoot "projects\$Project\source"

$script:Violations = New-Object System.Collections.ArrayList

function Add-Viol {
    param([string]$Rule, [string]$Loc, [string]$Msg, [string]$Fix)
    [void]$script:Violations.Add([pscustomobject]@{ Rule = $Rule; Loc = $Loc; Msg = $Msg; Fix = $Fix })
}

function Rel-Path {
    param([string]$Full)
    return ($Full -replace [regex]::Escape($RepoRoot + '\'), '' -replace '\\', '/')
}

# C comments are stripped so a rule can never fire on documentation that warns
# about the very pattern it forbids (fmt.h says "must never sprintf()").
function Remove-CComments {
    param([string[]]$Lines)
    $out = New-Object System.Collections.ArrayList
    $inBlock = $false
    foreach ($ln in $Lines) {
        $s = [string]$ln
        if ($inBlock) {
            $end = $s.IndexOf('*/')
            if ($end -lt 0) { [void]$out.Add(''); continue }
            $inBlock = $false
            $s = $s.Substring($end + 2)
        }
        while ($true) {
            $start = $s.IndexOf('/*')
            if ($start -lt 0) { break }
            $end = $s.IndexOf('*/', $start + 2)
            if ($end -lt 0) { $s = $s.Substring(0, $start); $inBlock = $true; break }
            $s = $s.Substring(0, $start) + $s.Substring($end + 2)
        }
        $lineComment = $s.IndexOf('//')
        if ($lineComment -ge 0) { $s = $s.Substring(0, $lineComment) }
        [void]$out.Add($s)
    }
    return $out.ToArray()
}

function Scan-Files {
    param([System.IO.FileInfo[]]$Files, [hashtable[]]$Rules)
    foreach ($f in $Files) {
        $lines = @(Remove-CComments -Lines (Get-Content -Encoding UTF8 $f.FullName))
        $rel = Rel-Path $f.FullName
        foreach ($rule in $Rules) {
            if ($rule['Only'] -and ($f.Name -notin $rule['Only'])) { continue }
            if ($rule['Skip'] -and ($f.Name -in $rule['Skip'])) { continue }
            if ($rule['SkipGlob'] -and ($rel -match $rule['SkipGlob'])) { continue }
            for ($i = 0; $i -lt $lines.Count; $i++) {
                if ($lines[$i] -match $rule['Pattern']) {
                    Add-Viol -Rule $rule['Id'] -Loc ("{0}:{1}" -f $rel, ($i + 1)) -Msg $rule['Msg'] -Fix $rule['Fix']
                }
            }
        }
    }
}

# --------------------------------------------------------------- rule tables ---

$fwFiles = @()
if (Test-Path $FwSrcDir) {
    $fwFiles = @(Get-ChildItem $FwSrcDir -Include *.c, *.h -Recurse -File)
}
if ($fwFiles.Count -eq 0) {
    Write-Host "[static-check] no firmware sources under $FwSrcDir" -ForegroundColor Red
    exit 2
}

$ps1Files = @(Get-ChildItem $RepoRoot -Include *.ps1 -Recurse -File -ErrorAction SilentlyContinue |
              Where-Object { (Rel-Path $_.FullName) -notmatch '(^|/)C3M3-harness/' })

$fwRules = @(
    @{ Id = 'PIN-JTAG-3538'; Pattern = '\bGPIO_[A-Za-z]+\s*\(\s*3[5-8]\b'
       Msg = 'GPIO35-38 (TMS/TDO/TCK/TDI) is being driven as GPIO'
       Fix = 'never bind pins 35-38 - it bricks the JTAG debug port permanently (AGENTS.md section 3)' }
    @{ Id = 'PIN-JTAG-3538-MACRO'; Pattern = '^\s*#\s*define\s+\w*PIN\w*\s+3[5-8]\b'
       Only = @('board_pins.h')
       Msg = 'pin table binds a JTAG-reserved pin'
       Fix = 'pick another pin; GPIO35-38 are reserved for the debug port' }
    @{ Id = 'PIN-JTAG-3538'; Pattern = '\bGPIO_3[5-8]_'
       Msg = 'driverlib pin-config macro for GPIO35-38 (cJTAG TMS/TDI/TDO/TCK) is referenced'
       Fix = 'never bind pins 35-38 - it bricks the JTAG debug port permanently (AGENTS.md section 3)' }
    @{ Id = 'PIN-BARE-LITERAL'; Pattern = '\bGPIO_[A-Za-z]+\s*\(\s*\d+\s*,'
       Skip = @('board_pins.h')
       Msg = 'bare pin number outside the pin table'
       Fix = 'reference BOARD_*_PIN from board_pins.h so pin binding stays in one place (REQ-002)' }
    @{ Id = 'PIN-BARE-PINCFG'; Pattern = '\bGPIO_\d+_[A-Za-z0-9_]+'
       Skip = @('board_pins.h')
       Msg = 'driverlib pin-config macro used outside the pin table'
       Fix = 'define it once in board_pins.h as BOARD_*_PINCFG and reference that symbol (AGENTS.md section 4)' }
    @{ Id = 'FMT-SPRINTF'; Pattern = '\bsprintf\s*\('
       Msg = 'sprintf() writes into a fixed-size buffer with no length bound'
       Fix = 'use the fmt_* length-bounded helpers (REQ-005, AGENTS.md section 4)' }
    @{ Id = 'MEM-DYNAMIC'; Pattern = '\b(?:malloc|calloc|realloc|free)\s*\('
       Msg = 'dynamic allocation in firmware'
       Fix = 'use static allocation; the heap is not budgeted on this target (AGENTS.md section 4)' }
    @{ Id = 'IO-STDIO'; Pattern = '#\s*include\s*[<"]stdio\.h'
       Msg = 'stdio pulled into firmware'
       Fix = 'log through sci_log.c + fmt.c (no filesystem runtime on C28x)' }
    @{ Id = 'PIN-DUPLICATE-TRUTH'; Pattern = '^\s*#\s*define\s+BOARD_LED\d'
       Skip = @('board_pins.h')
       Msg = 'second definition of an LED pin binding'
       Fix = 'board_pins.h is the only pin truth (REQ-002); include it instead' }
)

$ps1Rules = @(
    @{ Id = 'TOOL-BARE-BUILD'; Pattern = '\b(?:cl2000|gmake)(?:\.exe)?\b'
       # ccs.ps1/ToolDetect.ps1/doctor.ps1 own tool discovery; static-check.ps1
       # carries the pattern text itself, so it must not match its own rule.
       SkipGlob = '(ccs\.ps1|ToolDetect\.ps1|doctor\.ps1|static-check\.ps1)$'
       Msg = 'names the raw C2000 toolchain instead of the wrapper'
       Fix = 'call .\harness\tools\ccs.ps1 build|rebuild|size so tool discovery stays injected (AGENTS.md section 1)' }
    @{ Id = 'REF-READONLY-C3M3'; Pattern = '(?:Set-Content|Add-Content|Out-File|Remove-Item|Move-Item|Rename-Item|New-Item|WriteAllText)[^\r\n]*C3M3-harness'
       # The rule table below quotes the forbidden shape; exempt this file only.
       SkipGlob = 'static-check\.ps1$'
       Msg = 'writes into the read-only reference project'
       Fix = 'C3M3-harness/** is read-only reference; copy what you need into this repo' }
    @{ Id = 'PS-READ-ENCODING'; Pattern = 'Get-Content(?=[^\r\n]*-Raw)(?![^\r\n]*-Encoding)'
       SkipGlob = 'static-check\.ps1$'
       Msg = 'reads a file with -Raw but no -Encoding'
       Fix = 'add -Encoding UTF8 (this host is PowerShell 5.1: BOM-less UTF-8 is decoded as ANSI/GBK, so a truth file with Chinese notes gets re-serialized as mojibake)' }
)

# ------------------------------------------------------------------- manifest ---

if (-not (Test-Path $ManifestPath)) {
    Write-Host "[static-check] manifest missing: $ManifestPath" -ForegroundColor Red
    exit 2
}
$mf = Get-Content -Raw -Encoding UTF8 $ManifestPath | ConvertFrom-Json

# v2 manifest: projects[] each declare their own logic set, suites and runner.
# Every check below runs PER PROJECT so identical module names in different
# projects are judged against their own declarations (spec 0002 T-005).
$hostProjects = @($mf.projects)
if ($hostProjects.Count -eq 0) {
    Add-Viol -Rule 'TEST-MANIFEST-EMPTY' -Loc 'harness/tests/host/modules.json' `
        -Msg 'modules.json declares no projects[]' `
        -Fix 'declare at least one project with source_dir/logic/suites/runner'
}

foreach ($hp in $hostProjects) {
    $pName = [string]$hp.name
    $pSrc = Join-Path $RepoRoot (([string]$hp.source_dir) -replace '/', '\')
    $pLogic = @($hp.logic) | ForEach-Object { [string]$_ }
    $pSuites = @($hp.suites)
    $pRunnerRel = [string]$hp.runner
    $pLib = [string]$hp.cmake_lib
    $pExe = [string]$hp.cmake_exe

    if (-not (Test-Path $pSrc)) {
        Add-Viol -Rule 'TEST-SOURCE-DIR-MISSING' -Loc 'harness/tests/host/modules.json' `
            -Msg "projects[] entry '$pName' source_dir does not resolve: $([string]$hp.source_dir)" `
            -Fix 'fix source_dir or remove the stale project entry'
        continue
    }

    # M1: every host-compilable firmware module must be declared in the manifest.
    $pEligible = @()
    foreach ($f in (Get-ChildItem $pSrc -Filter *.c -File | Where-Object { $_.BaseName -ne 'main' })) {
        $txt = ((Remove-CComments -Lines (Get-Content -Encoding UTF8 $f.FullName)) -join "`n")
        if ($txt -notmatch '#\s*include\s*[<"](driverlib|device\.h)') {
            $pEligible += $f.BaseName
        }
    }
    foreach ($m in $pEligible) {
        if ($pLogic -notcontains $m) {
            Add-Viol -Rule 'TEST-MODULE-UNDECLARED' -Loc (Rel-Path (Join-Path $pSrc "$m.c")) `
                -Msg "[$pName] host-compilable module '$m' is not in harness/tests/host/modules.json" `
                -Fix "add '$m' to projects[$pName].logic[] plus a suite that covers it, otherwise it ships untested (TDD)"
        }
    }
    foreach ($m in $pLogic) {
        if ($pEligible -notcontains $m) {
            Add-Viol -Rule 'TEST-MODULE-STALE' -Loc 'harness/tests/host/modules.json' `
                -Msg "[$pName] manifest declares '$m' as host-tested but no such host-compilable source exists" `
                -Fix "drop it from projects[$pName].logic[] or restore projects/$pName/source/$m.c"
        }
    }

    # Logic-layer purity: declared logic modules must not reach hardware headers.
    foreach ($m in $pLogic) {
        $p = Join-Path $pSrc "$m.c"
        if (-not (Test-Path $p)) { continue }
        $code = @(Remove-CComments -Lines (Get-Content -Encoding UTF8 $p))
        for ($i = 0; $i -lt $code.Count; $i++) {
            if ($code[$i] -match '#\s*include\s*[<"](driverlib|device\.h)') {
                Add-Viol -Rule 'LAY-DRIVERLIB-IN-LOGIC' -Loc ("{0}:{1}" -f (Rel-Path $p), ($i + 1)) `
                    -Msg "[$pName] host-tested module '$m' includes a hardware header" `
                    -Fix 'move register access to the board layer or drop the module from logic[]'
            }
        }
    }

    # M2: every declared module needs a suite that covers it, and that suite must
    # really be wired into the per-project runner.
    $covered = @()
    $runnerPath = Join-Path $HostTestDir ($pRunnerRel -replace '/', '\')
    $rtxt = ''
    if (Test-Path $runnerPath) { $rtxt = Get-Content -Raw -Encoding UTF8 $runnerPath }
    foreach ($s in $pSuites) {
        $sp = Join-Path $HostTestDir (([string]$s.file) -replace '/', '\')
        if (-not (Test-Path $sp)) {
            Add-Viol -Rule 'TEST-SUITE-MISSING' -Loc ('harness/tests/host/' + ([string]$s.file)) `
                -Msg "[$pName] declared test suite file does not exist" `
                -Fix 'create the suite or correct modules.json'
            continue
        }
        foreach ($c in @($s.covers)) { $covered += [string]$c }
        $entry = [string]$s.entry
        if ($rtxt -notmatch ('(?m)^\s*' + [regex]::Escape($entry) + '\s*\(\s*\)\s*;')) {
            Add-Viol -Rule 'TEST-SUITE-NOT-RUN' -Loc ('harness/tests/host/' + $pRunnerRel) `
                -Msg "[$pName] suite entry '$entry' is never called by the runner" `
                -Fix "call $entry(); from main() in $pRunnerRel, else these cases silently do not run"
        }
    }
    foreach ($m in $pLogic) {
        if ($covered -notcontains $m) {
            Add-Viol -Rule 'TEST-MODULE-UNCOVERED' -Loc 'harness/tests/host/modules.json' `
                -Msg "[$pName] logic module '$m' has no suite with covers: $m" `
                -Fix 'add a suite entry (and test/<name>.c) that covers it'
        }
    }

    # M3: CMakeLists must compile exactly the manifest module set (no 4th copy).
    $cmakePath = Join-Path $HostTestDir 'CMakeLists.txt'
    if (Test-Path $cmakePath) {
        $ctxt = Get-Content -Raw -Encoding UTF8 $cmakePath
        $fwBlock = [regex]::Match($ctxt, ('(?s)add_library\(\s*' + [regex]::Escape($pLib) + '\s+STATIC(.*?)\)'))
        $cmakeLogic = @()
        if ($fwBlock.Success) {
            foreach ($mm in [regex]::Matches($fwBlock.Groups[1].Value, '([A-Za-z0-9_\-]+)\.c')) { $cmakeLogic += $mm.Groups[1].Value }
        }
        foreach ($m in $pLogic) {
            if ($cmakeLogic -notcontains $m) {
                Add-Viol -Rule 'TEST-CMAKE-DRIFT' -Loc 'harness/tests/host/CMakeLists.txt' `
                    -Msg "[$pName] $pLib does not compile '$m.c' although modules.json declares it" `
                    -Fix ('add "${FW_SOURCE_DIR}/' + $m + '.c" to add_library(' + $pLib + ' STATIC ...)')
            }
        }
        foreach ($c in $cmakeLogic) {
            if ($pLogic -notcontains $c) {
                Add-Viol -Rule 'TEST-CMAKE-STALE' -Loc 'harness/tests/host/CMakeLists.txt' `
                    -Msg "[$pName] $pLib compiles '$c.c' which modules.json does not declare" `
                    -Fix 'declare it in projects[$pName].logic[] with a covering suite'
            }
        }
        $exeBlock = [regex]::Match($ctxt, ('(?s)add_executable\(\s*' + [regex]::Escape($pExe) + '\s*\(?(.*?)\)'))
        foreach ($s in $pSuites) {
            $needle = ([string]$s.file)
            if ($exeBlock.Success -and ($exeBlock.Groups[1].Value -notmatch [regex]::Escape($needle))) {
                Add-Viol -Rule 'TEST-CMAKE-DRIFT' -Loc 'harness/tests/host/CMakeLists.txt' `
                    -Msg "[$pName] $pExe does not build $needle" `
                    -Fix "list $needle in add_executable($pExe ...) so ctest runs it"
            }
        }
        if ($rtxt -and ($ctxt -notmatch ('runner/' + [regex]::Escape((Split-Path $pRunnerRel -Leaf))))) {
            Add-Viol -Rule 'TEST-CMAKE-DRIFT' -Loc 'harness/tests/host/CMakeLists.txt' `
                -Msg "[$pName] CMakeLists does not build the runner $pRunnerRel" `
                -Fix "list $pRunnerRel in add_executable($pExe ...)"
        }
    } else {
        Add-Viol -Rule 'TEST-CMAKE-MISSING' -Loc 'harness/tests/host/CMakeLists.txt' `
            -Msg 'CMake host-test project missing' -Fix 'restore the cmake driver'
    }
}

Scan-Files -Files $fwFiles -Rules $fwRules
Scan-Files -Files $ps1Files -Rules $ps1Rules

# ------------------------------------------------------------ project config ---

# projects/<proj>/project.config.json is documented as the project contract
# (docs/00 owner table, README), so it must be executed somewhere. This leg is that
# somewhere: contract-valid shape, name agrees with its directory, and every path it
# declares really resolves. A contract nothing reads is how stale configs survive.
# (2026-10-24: iterates every project in the repo, not just -Project, so a second
# project cannot grow an unvalidated config.)
$cfgSchema = Join-Path $RepoRoot 'harness\schemas\project.config.schema.json'
$cfgProjects = @(Get-ChildItem (Join-Path $RepoRoot 'projects') -Filter 'project.config.json' -Recurse -Depth 1 -ErrorAction SilentlyContinue)
if ($cfgProjects.Count -eq 0) {
    Add-Viol -Rule 'CFG-MISSING' -Loc 'projects/' `
        -Msg 'no project.config.json exists although docs declare it the project contract' `
        -Fix 'create projects/<proj>/project.config.json or delete the claim from the docs'
}
foreach ($cfgFile in $cfgProjects) {
    $ProjDir = $cfgFile.Directory.FullName
    $Project = Split-Path -Leaf $ProjDir
    $cfgPath = $cfgFile.FullName
    if (-not (Test-Path $cfgSchema)) {
        Add-Viol -Rule 'CFG-SCHEMA-MISSING' -Loc 'harness/schemas/project.config.schema.json' `
            -Msg 'project.config contract file is missing, so the config cannot be validated' `
            -Fix 'restore the schema or drop the contract (a contract nothing executes must not be kept)'
    }
    else {
        $cviol = @(Test-JsonFileAgainstSchema -JsonPath $cfgPath -SchemaPath $cfgSchema)
        foreach ($x in @($cviol | Where-Object { $_ })) {
            Add-Viol -Rule 'CFG-SCHEMA' -Loc (Rel-Path $cfgPath) -Msg ([string]$x) `
                -Fix 'fix the value or update harness/schemas/project.config.schema.json + the ADR that justifies it'
        }
        $cfg = Get-Content -Raw -Encoding UTF8 $cfgPath | ConvertFrom-Json
        if ([string]$cfg.project.name -ne $Project) {
            Add-Viol -Rule 'CFG-NAME' -Loc (Rel-Path $cfgPath) `
                -Msg "project.name '$($cfg.project.name)' does not match the directory name '$Project'" `
                -Fix 'the name is how ccs.ps1 resolves -Project; keep them equal'
        }
        if ($cfg.paths) {
            foreach ($p in @($cfg.paths.PSObject.Properties)) {
                $dir = Join-Path $ProjDir (([string]$p.Value) -replace '/', '\')
                if (-not (Test-Path -LiteralPath $dir)) {
                    Add-Viol -Rule 'CFG-PATH' -Loc (Rel-Path $cfgPath) `
                        -Msg "[$Project] paths.$($p.Name) = '$($p.Value)' does not resolve to a directory" `
                        -Fix 'point it at the real directory (relative to projects/<proj>/) or remove the entry'
                }
            }
        }
        $bc = [string]$cfg.project.build_config
        if ($bc -and -not (Test-Path (Join-Path $ProjDir $bc))) {
            Add-Viol -Rule 'CFG-BUILD-CONFIG' -Loc (Rel-Path $cfgPath) `
                -Msg "[$Project] project.build_config '$bc' has no such output directory under the project" `
                -Fix 'build it (ccs.ps1 build -Config) or correct the default in project.config.json'
        }
        $specDir = Join-Path $RepoRoot (([string]$cfg.specs.dir) -replace '/', '\')
        if ($cfg.specs -and -not (Test-Path -LiteralPath $specDir)) {
            Add-Viol -Rule 'CFG-PATH' -Loc (Rel-Path $cfgPath) `
                -Msg "[$Project] specs.dir '$($cfg.specs.dir)' does not exist from the repository root" `
                -Fix 'point it at the spec root (specs/)'
        }
    }
}

# ----------------------------------------------------------------- analyze leg ---

$analyzeSkipped = $true
if (-not $SkipAnalyze) {
    $msvc = Find-MsvcHost
    if (-not $msvc) {
        Add-Viol -Rule 'ANALYZE-UNAVAILABLE' -Loc 'harness/tools/static-check.ps1' `
            -Msg 'MSVC cl.exe not found, so the /analyze leg did not run' `
            -Fix 'install VS Build Tools, or re-run with -SkipAnalyze and record the waiver'
    } else {
        $env:INCLUDE = $msvc.Include
        $env:LIB = $msvc.Lib
        $env:PATH = "$($msvc.Bin);$env:PATH"

        $tmp = Join-Path $HostTestDir 'build-analyze'
        New-Item -ItemType Directory -Force -Path $tmp | Out-Null
        $sources = @()
        $incDirs = @((Join-Path $HostTestDir 'framework'))
        foreach ($f in @($mf.framework)) { $sources += (Join-Path $HostTestDir ($f -replace '/', '\')) }
        foreach ($hp in $hostProjects) {
            $pSrc2 = Join-Path $RepoRoot (([string]$hp.source_dir) -replace '/', '\')
            if (Test-Path $pSrc2) { $incDirs += $pSrc2 }
            foreach ($m in @($hp.logic)) { $sources += (Join-Path $pSrc2 "$m.c") }
            foreach ($s in @($hp.suites)) { $sources += (Join-Path $HostTestDir (([string]$s.file) -replace '/', '\')) }
            $sources += (Join-Path $HostTestDir (([string]$hp.runner) -replace '/', '\'))
        }
        $sources = @($sources | Where-Object { Test-Path $_ } | Select-Object -Unique)

        $clArgs = @('/nologo', '/W4', '/analyze', '/c', '/utf-8', "/Fo:$tmp\")
        foreach ($d in ($incDirs | Select-Object -Unique)) { $clArgs += "-I$d" }
        $clArgs += $sources

        Write-Host "[analyze] cl.exe /analyze /W4 over $($sources.Count) host-compilable sources ($($msvc.Version))"
        $out = & $msvc.Cl @clArgs 2>&1
        $analyzeExit = $LASTEXITCODE
        $diags = @($out | Where-Object { ([string]$_) -match '(warning|error) C\d+' } |
                   Where-Object { ([string]$_).Contains($RepoRoot) })
        foreach ($d in $diags) {
            $loc = ''
            if ($d -match '^(.*?):\s*line\s+(\d+)') { $loc = (Rel-Path $Matches[1]) + ':' + $Matches[2] }
            Add-Viol -Rule 'ANALYZE' -Loc $loc -Msg ([string]$d).Trim() `
                -Fix 'cl /analyze diagnostic: fix the defect, do not suppress the warning'
        }
        if ($analyzeExit -ne 0 -and $diags.Count -eq 0) {
            Add-Viol -Rule 'ANALYZE' -Loc 'harness/tests/host' `
                -Msg "cl.exe /analyze exited $analyzeExit without a parseable diagnostic" `
                -Fix 'run the command by hand and read the compiler output'
        }
        $analyzeSkipped = $false
    }
}

# ------------------------------------------------------------------ doc routes ---
# AGENTS.md and harness/docs/ are the map an agent follows cold. Four routes in
# AGENTS.md were dead (docs/30, docs/40, bare "source/...", .workbuddy/memory/)
# and a lying map is worse than no map, so every backticked repo path inside the
# instruction documents must resolve - the same existence rule spec.ps1 applies
# to trace evidence paths.
$docRouteFiles = @(
    (Get-Item (Join-Path $RepoRoot 'AGENTS.md')).FullName
    # The root README is the entry map a new reader (or agent) follows first, so its
    # routes matter as much as AGENTS.md's.
    (Get-Item (Join-Path $RepoRoot 'README.md')).FullName
    (Get-ChildItem (Join-Path $RepoRoot 'harness\docs') -Filter '*.md' -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName })
    (Get-ChildItem (Join-Path $RepoRoot 'specs') -Filter '*.md' -Recurse -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName })
    (Get-ChildItem (Join-Path $RepoRoot 'harness\tests') -Filter '*.md' -Recurse -ErrorAction SilentlyContinue |
        # records/*.md are per-run evidence artifacts: they quote build-output paths that
        # only exist right after a run, so they are evidence, not part of the walkable map.
        Where-Object { $_.FullName -notmatch '\\records\\' } |
        ForEach-Object { $_.FullName })
    (Get-ChildItem (Join-Path $RepoRoot 'harness\env') -Filter '*.md' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName })
    (Get-ChildItem (Join-Path $RepoRoot 'projects') -Filter 'README.md' -Recurse -Depth 2 -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName })
    (Get-ChildItem (Join-Path $RepoRoot 'targets') -Filter '*.md' -ErrorAction SilentlyContinue |
        ForEach-Object { $_.FullName })
)

function Expand-DocRouteToken {
    param([string]$Tok)
    # Two shorthands are common in prose and must not be reported as dead routes:
    # braces ("fw_test.{h,c}") and a bare extension tail ("led_hw.c/.h").
    $base = @($Tok)
    if ($Tok -match '\{([^}]*)\}') {
        $pre = $Tok.Substring(0, $Tok.IndexOf('{'))
        $post = $Tok.Substring($Tok.IndexOf('}') + 1)
        $base = @(($Matches[1] -split ',') | ForEach-Object { '{0}{1}{2}' -f $pre, $_, $post })
    }
    $out = New-Object System.Collections.ArrayList
    foreach ($b in $base) {
        if ($b -match '^(.*\.(?:c|h|ps1|json|md|js))/\.([a-z]+)$') {
            [void]$out.Add($Matches[1])
            [void]$out.Add(($Matches[1] -replace '\.[^.]+$', ('.{0}' -f $Matches[2])))
        }
        else { [void]$out.Add($b) }
    }
    # "spec|trace.schema.json" means two files, not one path with a pipe.
    $final = New-Object System.Collections.ArrayList
    foreach ($c in $out) {
        if ($c.Contains('|')) { foreach ($p in ($c -split '\|')) { [void]$final.Add($p) } }
        else { [void]$final.Add($c) }
    }
    return $final.ToArray()
}

function Test-OneRoute {
    param([string]$Tok, [string]$DocFile)
    # A reader has three vantage points: the repository root, the directory the
    # document itself sits in (and its parents), and a project root - specs and
    # task lists name `source/...` from inside a project. Dead means it resolves
    # from NONE of them.
    $roots = New-Object System.Collections.ArrayList
    [void]$roots.Add($RepoRoot)
    $dir = Split-Path -Parent $DocFile
    while ($dir -and $dir.StartsWith($RepoRoot)) {
        [void]$roots.Add($dir)
        $parent = Split-Path -Parent $dir
        if (-not $parent -or $parent -eq $dir) { break }
        $dir = $parent
    }
    foreach ($pj in (Get-ChildItem (Join-Path $RepoRoot 'projects') -Directory -ErrorAction SilentlyContinue)) {
        [void]$roots.Add($pj.FullName)
    }
    $rel = $Tok -replace '/', '\'
    foreach ($r in $roots) {
        try { if (Test-Path -LiteralPath (Join-Path $r $rel)) { return $true } } catch { }
    }
    return $false
}

function Test-DocRouteToken {
    param([string]$Tok, [string]$DocFile)
    # Only judge tokens that actually CLAIM to be an openable repo path. Prose
    # enumerations ("code_start/wd_disable", "requirements/design/tasks",
    # ".text/.data"), placeholders (<proj>, *.md), globs, commands, URLs, line
    # references ("spec.ps1:50/67") and absolute paths are not routes.
    if ($Tok -notmatch '/') { return 'skip' }
    if ($Tok -match '[<>*?\s=":]') { return 'skip' }
    if ($Tok -match '^(?:https?:|\\\\|//|-|/)') { return 'skip' }
    $segs = @(($Tok.TrimEnd('/', '\') -split '[/\\]') | Where-Object { $_ })
    if ($segs.Count -lt 2) { return 'skip' }
    $fileish = ($Tok.EndsWith('/') -or $Tok.EndsWith('\'))
    if (-not $fileish) {
        foreach ($s in $segs) {
            if ($s -match '^[^.][^/\\]*\.(c|h|ps1|json|md|js|map|out|cmd|yml|csv|txt|lib|asm)$') { $fileish = $true; break }
        }
    }
    if (-not $fileish -and $segs[-1] -notmatch '^\d+$') { return 'skip' }
    if ([System.IO.Path]::IsPathRooted($Tok)) { return 'skip' }
    foreach ($c in (Expand-DocRouteToken $Tok)) {
        if (Test-OneRoute $c $DocFile) { return 'ok' }
    }
    return 'dead'
}

$script:DocRoutesChecked = 0
foreach ($f in @($docRouteFiles | Where-Object { $_ })) {
    $rel = Rel-Path $f
    $txt = Get-Content -Raw -Encoding UTF8 $f
    if (-not $txt) { continue }
    foreach ($m in [regex]::Matches($txt, '`([^`\r\n]{2,140})`')) {
        $tok = $m.Groups[1].Value.Trim()
        $verdict = Test-DocRouteToken $tok $f
        if ($verdict -eq 'skip') { continue }
        $script:DocRoutesChecked++
        if ($verdict -eq 'ok') { continue }
        $line = 1 + ([regex]::Matches($txt.Substring(0, $m.Index), "`n")).Count
        Add-Viol -Rule 'DOC-DEAD-ROUTE' -Loc ("{0}:{1}" -f $rel, $line) `
            -Msg "route ``$tok`` does not exist in this repository" `
            -Fix "point it at the real owner file (see AGENTS.md section 5's table), or delete the claim"
    }
}

# ----------------------------------------------------------------------- report ---

$manifestLogicTotal = 0
foreach ($hp in $hostProjects) { $manifestLogicTotal += @($hp.logic).Count }

Write-Host ''
Write-Host "static check: projects/$Project/source + harness tools" -ForegroundColor Cyan
Write-Host ("  scanned : {0} firmware sources, {1} PowerShell scripts, {2} manifest modules in {3} projects, {4} doc routes" -f `
    $fwFiles.Count, $ps1Files.Count, $manifestLogicTotal, $hostProjects.Count, $script:DocRoutesChecked)
if ($analyzeSkipped) { Write-Host '  analyze : SKIPPED (-SkipAnalyze or no MSVC)' -ForegroundColor Yellow }

if ($script:Violations.Count -eq 0) {
    Write-Host 'STATIC CHECK OK (0 violations)' -ForegroundColor Green
    exit 0
}

Write-Host ''
foreach ($v in $script:Violations) {
    Write-Host ("  x [{0}] {1}" -f $v.Rule, $v.Msg) -ForegroundColor Red
    Write-Host ("      at : {0}" -f $v.Loc) -ForegroundColor Red
    Write-Host ("      fix: {0}" -f $v.Fix) -ForegroundColor DarkGray
}
Write-Host ''
Write-Host ("STATIC CHECK FAILED ({0} violations)" -f $script:Violations.Count) -ForegroundColor Red
exit 1
