# ToolDetect.ps1 - shared tool-detection helpers for f280039c-harness scripts.
#
# Dot-source from doctor.ps1 / run-host-tests.ps1:
#     . "$PSScriptRoot\lib\ToolDetect.ps1"
#
# Why this exists (carried over from C3M3-harness, same three pitfalls):
#   * winget app-execution aliases under %LOCALAPPDATA%\Microsoft\WinGet\Links
#     can be 0-byte stubs that exit 0 silently in some shells - they must NOT
#     be treated as working executables.
#   * winget portable packages install under
#     %LOCALAPPDATA%\Microsoft\WinGet\Packages\<pkg>\<version>\bin - the real
#     executables live there even when the aliases are broken.
#   * A long-lived shell may predate a winget PATH update; probing well-known
#     locations keeps the scripts usable without restarting the terminal.

function Test-RealExe {
    param([string]$Path)
    (Test-Path $Path) -and ((Get-Item $Path -ErrorAction SilentlyContinue).Length -gt 0)
}

# Find-CMake -> path to a working cmake.exe, or $null.
# If found outside PATH, the containing directory is prepended to $env:Path
# (current process + children) so plain `cmake` works afterwards.
function Find-CMake {
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath -and (Test-RealExe $onPath.Source)) { return $onPath.Source }

    $dirs = @()
    $pkgs = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $pkgs) {
        Get-ChildItem -Path $pkgs -Directory -Filter 'Kitware.CMake*' -ErrorAction SilentlyContinue |
            ForEach-Object {
                Get-ChildItem -Path $_.FullName -Directory -Filter 'cmake-*' -ErrorAction SilentlyContinue |
                    ForEach-Object { $dirs += Join-Path $_.FullName 'bin' }
            }
    }
    $dirs += @('C:\Program Files\CMake\bin', 'C:\Program Files (x86)\CMake\bin')

    foreach ($dir in $dirs) {
        if ((Test-RealExe (Join-Path $dir 'cmake.exe')) -and (Test-RealExe (Join-Path $dir 'ctest.exe'))) {
            $env:Path = "$dir;$env:Path"
            return Join-Path $dir 'cmake.exe'
        }
    }
    return $null
}

# Find-HostCC -> path to a working host C compiler (gcc.exe), the string
# 'msvc' when only MSVC is available, or $null. MinGW dirs are put on PATH.
function Find-HostCC {
    foreach ($name in @('cc', 'gcc', 'clang')) {
        $c = Get-Command $name -ErrorAction SilentlyContinue
        if ($c -and (Test-RealExe $c.Source)) { return $c.Source }
    }

    $dirs = @()
    $pkgs = Join-Path $env:LOCALAPPDATA 'Microsoft\WinGet\Packages'
    if (Test-Path $pkgs) {
        Get-ChildItem -Path $pkgs -Directory -Filter '*WinLibs*' -ErrorAction SilentlyContinue |
            ForEach-Object { $dirs += Join-Path $_.FullName 'mingw64\bin' }
    }
    $dirs += @('C:\mingw64\bin', 'C:\msys64\mingw64\bin')
    foreach ($dir in $dirs) {
        if ($dir -and (Test-RealExe (Join-Path $dir 'gcc.exe'))) {
            $env:Path = "$dir;$env:Path"
            return Join-Path $dir 'gcc.exe'
        }
    }

    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path $vsWhere) {
        $vsPath = & $vsWhere -products * -latest -property installationPath 2>$null
        if ($vsPath -and (Test-Path (Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'))) {
            return 'msvc'
        }
    }
    return $null
}

# Find-MsvcHost -> MSVC x64 host compiler plus the INCLUDE/LIB it needs.
#
# vcvars64.bat is deliberately NOT used: it shells out to reg.exe, which
# corporate/sandbox policies often blacklist. Assembling the environment from
# the install layout (MSVC tools + Windows Kits) keeps it hermetic and offline.
# Returns $null when MSVC is absent so callers can fall back or report UNAVAILABLE.
function Find-MsvcHost {
    $vsWhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vsWhere)) { return $null }
    $vsPath = (& $vsWhere -products * -latest -property installationPath 2>$null | Select-Object -First 1)
    if (-not $vsPath) { return $null }

    $msvcRoot = Join-Path $vsPath 'VC\Tools\MSVC'
    if (-not (Test-Path $msvcRoot)) { return $null }
    $msvc = Get-ChildItem $msvcRoot -Directory | Sort-Object Name | Select-Object -Last 1
    if (-not $msvc) { return $null }

    $bin = Join-Path $msvc.FullName 'bin\Hostx64\x64'
    $cl = Join-Path $bin 'cl.exe'
    if (-not (Test-RealExe $cl)) { return $null }

    $kitsRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10'
    if (-not (Test-Path (Join-Path $kitsRoot 'Include'))) { return $null }
    $kitVer = (Get-ChildItem (Join-Path $kitsRoot 'Include') -Directory |
               Sort-Object Name | Select-Object -Last 1).Name
    $kitInc = Join-Path $kitsRoot "Include\$kitVer"
    $kitLib = Join-Path $kitsRoot "Lib\$kitVer"

    return [pscustomobject]@{
        Cl      = $cl
        Bin     = $bin
        Version = $msvc.Name
        Include = "$(Join-Path $msvc.FullName 'include');$kitInc\ucrt;$kitInc\um;$kitInc\shared"
        Lib     = "$(Join-Path $msvc.FullName 'lib\x64');$kitLib\ucrt\x64;$kitLib\um\x64"
    }
}

# Find-CcsRoot -> newest CCS install root (contains ccs_base + tools).
# Honours $env:CCS_ROOT override; otherwise scans well-known roots and drives.
# NOTE: do NOT assume C:\ti - this machine has all TI tooling on D:\ti.
function Find-CcsRoot {
    $candidates = New-Object System.Collections.ArrayList

    if ($env:CCS_ROOT) { [void]$candidates.Add($env:CCS_ROOT) }

    foreach ($root in @('C:\ti', 'D:\ti', 'C:\TexasInstruments')) {
        if (Test-Path $root) {
            Get-ChildItem -Path $root -Directory -Filter 'ccs*' -ErrorAction SilentlyContinue |
                ForEach-Object {
                    [void]$candidates.Add((Join-Path $_.FullName 'ccs'))
                    [void]$candidates.Add($_.FullName)
                }
        }
    }
    # Also a direct root such as C:\ccs (installer custom location)
    foreach ($direct in @('C:\ccs', 'C:\Program Files\Texas Instruments\ccs')) {
        if (Test-Path $direct) { [void]$candidates.Add($direct) }
    }

    foreach ($c in $candidates) {
        if ($c -and (Test-Path (Join-Path $c 'ccs_base')) -and (Test-Path (Join-Path $c 'tools'))) {
            return (Resolve-Path $c).Path
        }
    }
    return $null
}

# Get-CcsVersion -> best-effort version string from install_info folder name.
function Get-CcsVersion {
    param([string]$CcsRoot)
    $info = Join-Path $CcsRoot 'install_info'
    if (-not (Test-Path $info)) { return $null }
    $exe = Get-ChildItem $info -Filter 'ccs_setup_*.exe' -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $exe) { return $null }
    if ($exe.Name -match 'ccs_setup_([\d.]+)\.exe') { return $matches[1] }
    return $null
}

# Get-C28xCompiler -> path to newest ti-cgt-c2000 cl2000.exe under CCS tools.
function Get-C28xCompiler {
    param([string]$CcsRoot)
    $base = Join-Path $CcsRoot 'tools\compiler'
    if (-not (Test-Path $base)) { return $null }
    $dirs = Get-ChildItem $base -Directory -Filter 'ti-cgt-c2000_*' |
        Sort-Object Name | Select-Object -Last 1
    if (-not $dirs) { return $null }
    $exe = Join-Path $dirs.FullName 'bin\cl2000.exe'
    if (Test-Path $exe) { return $exe }
    return $null
}

# Find-C2000Ware -> newest C2000Ware_* directory (sibling of ccs by default).
function Find-C2000Ware {
    param([string]$CcsRoot)

    if ($env:C2000WARE_ROOT) { return $env:C2000WARE_ROOT }

    $roots = New-Object System.Collections.ArrayList
    if ($CcsRoot) { [void]$roots.Add((Split-Path -Parent $CcsRoot)) }
    foreach ($r in @('C:\ti', 'D:\ti', 'C:\')) { if (Test-Path $r) { [void]$roots.Add($r) } }

    foreach ($r in ($roots | Where-Object { $_ })) {
        $hit = Get-ChildItem -Path $r -Directory -Filter 'C2000Ware_*' -ErrorAction SilentlyContinue |
            Sort-Object Name | Select-Object -Last 1
        if ($hit) { return $hit.FullName }
    }
    return $null
}
