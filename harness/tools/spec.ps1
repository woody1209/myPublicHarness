# spec.ps1 - spec 模式工具（kiro 风格三件套的创建 / 校验 / 追溯）
#
# 用法：
#   .\harness\tools\spec.ps1 new   -Id 0002-uart-bridge -Title "UART 桥接" [-Project <proj>]
#   .\harness\tools\spec.ps1 list
#   .\harness\tools\spec.ps1 check [-Id 0001-led-chaser]
#   .\harness\tools\spec.ps1 trace -Id 0001-led-chaser
#
# 退出码：0=通过；1=校验失败；2=用法/环境错误。
#
# 契约：harness/schemas/spec.schema.json、harness/schemas/trace.schema.json。
# 原则：本脚本是"确定性规则轨"（rule），不生成内容；内容由 Agent/人填写。

[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateSet('new', 'list', 'check', 'trace')]
    [string]$Verb,

    [string]$Id,
    [string]$Title,
    [string]$Project = 'launchxl-f280039c-led-chaser'
)

$ErrorActionPreference = 'Continue'

# 门禁输出常被管道或代理捕获，此时控制台默认 GBK 代码页会把中文打乱；显式 UTF-8。
try { [Console]::OutputEncoding = [System.Text.Encoding]::UTF8 } catch { }

. "$PSScriptRoot\lib\JsonSchema.ps1"   # Get-JsonKind / Test-JsonFileAgainstSchema

# ---------------------------------------------------------------- 基础工具 ---

function Get-RepoRoot {
    # 脚本位于 <root>/harness/tools/spec.ps1
    return (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
}

function Get-SpecRoot {
    return (Join-Path (Get-RepoRoot) 'specs')
}

function Get-SpecDirs {
    $root = Get-SpecRoot
    if (-not (Test-Path $root)) { return @() }
    Get-ChildItem -Path $root -Directory |
        Where-Object { $_.Name -notmatch '^[_.]' } |
        Sort-Object Name
}

function Read-JsonFile {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $null }
    try {
        return (Get-Content -Raw -Encoding UTF8 $Path | ConvertFrom-Json)
    } catch {
        return $null
    }
}

function Test-Prop {
    param($Obj, [string]$Name)
    if ($null -eq $Obj) { return $false }
    return ($Obj.PSObject.Properties.Name -contains $Name)
}

function Get-SchemaRoot {
    return (Join-Path (Get-RepoRoot) 'harness\schemas')
}

# trace.json 里的路径一律写成仓库相对路径；为容错，同时接受相对 spec 目录的写法。
function Resolve-RepoPath {
    param([string]$Rel, [string]$SpecDir)
    if ([string]::IsNullOrWhiteSpace($Rel)) { return $null }
    $clean = $Rel.Trim() -replace '^\[+' -replace '\]+$' -replace '^`+' -replace '`+$'
    # Windows 非法路径字符（模板占位符 <module> 之类）：绝不可能是真实路径，
    # 直接按不存在处理，避免 Test-Path/IsPathRooted 抛"非法字符"异常污染输出。
    if ($clean -match '[<>|?*"]') { return $null }
    $cands = @()
    if ([System.IO.Path]::IsPathRooted($clean)) {
        if (Test-Path $clean) { return $clean }
        return $null
    }
    $cands += (Join-Path (Get-RepoRoot) $clean)
    if ($SpecDir) { $cands += (Join-Path $SpecDir $clean) }
    foreach ($c in $cands) { if (Test-Path $c) { return $c } }
    return $null
}

# 从 verification / acceptance 文本里抽出看起来像文件的路径令牌（确定性、不猜测）。
function Get-PathTokens {
    param([string]$Text)
    if ([string]::IsNullOrWhiteSpace($Text)) { return @() }
    $rx = [regex]'(?i)(?:[A-Za-z0-9_.\-]+[\\/])+[A-Za-z0-9_.\-]+\.(?:ps1|c|h|json|js|md|map|out|txt|yml|cmd|log|csv)'
    $out = @()
    foreach ($m in $rx.Matches($Text)) { $out += ($m.Value -replace '^\.\\+', '') }
    return ($out | Select-Object -Unique)
}

# 解析 tasks.md：返回 @{ Id; Done; Reqs } 列表
function Parse-TasksMd {
    param([string]$Path)
    $tasks = New-Object System.Collections.ArrayList
    if (-not (Test-Path $Path)) { return $tasks }
    $lines = Get-Content -Encoding UTF8 $Path
    foreach ($line in $lines) {
        $m = [regex]::Match($line, '^\s*-\s*\[([ xX])\]\s*\*\*(T-\d{3})\*\*')
        if (-not $m.Success) { continue }
        $done = ($m.Groups[1].Value -ne ' ')
        $tid = $m.Groups[2].Value
        $reqs = @()
        foreach ($rm in [regex]::Matches($line, 'REQ-\d{3}')) { $reqs += $rm.Value }
        [void]$tasks.Add([pscustomobject]@{ Id = $tid; Done = $done; Reqs = $reqs })
    }
    return $tasks
}

$script:Errors = New-Object System.Collections.ArrayList
$script:Warns = New-Object System.Collections.ArrayList

function Add-Err { param([string]$m) [void]$script:Errors.Add($m) }
function Add-Warn { param([string]$m) [void]$script:Warns.Add($m) }

# ------------------------------------------------------------------- new ----

function Invoke-New {
    if (-not $Id) { Write-Host 'new 需要 -Id <NNNN-slug>' -ForegroundColor Red; exit 2 }
    if ($Id -notmatch '^[0-9]{4}-[a-z0-9-]+$') {
        Write-Host "Id 非法（应为 NNNN-kebab-slug）：$Id" -ForegroundColor Red; exit 2
    }
    $specRoot = Get-SpecRoot
    $tpl = Join-Path $specRoot '_template'
    if (-not (Test-Path $tpl)) { Write-Host "模板目录缺失：$tpl" -ForegroundColor Red; exit 2 }
    $dest = Join-Path $specRoot $Id
    if (Test-Path $dest) { Write-Host "已存在：$dest" -ForegroundColor Red; exit 2 }

    $slug = $Id -replace '^[0-9]{4}-', ''
    $name = if ($Title) { $Title } else { $slug }
    $today = (Get-Date).ToString('yyyy-MM-dd')

    New-Item -ItemType Directory -Path $dest -Force | Out-Null
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    Get-ChildItem -Path $tpl -File | ForEach-Object {
        $text = Get-Content -Raw -Encoding UTF8 $_.FullName
        $text = $text -replace '0000-template', $Id
        $text = $text -replace '<kebab-slug>', $slug
        $text = $text -replace '<特性名称>', $name
        $text = $text -replace '<YYYY-MM-DD>', $today
        $text = $text -replace '<proj>', $Project
        [System.IO.File]::WriteAllText((Join-Path $dest $_.Name), $text, $utf8NoBom)
    }
    Write-Host "已创建 spec：specs/$Id" -ForegroundColor Green
    Write-Host "下一步：填写 requirements.md 与 spec.json，经用户确认后将 meta.status 改为 requirements-approved。"
}

# ------------------------------------------------------------------ list ----

function Invoke-List {
    $dirs = Get-SpecDirs
    if ($dirs.Count -eq 0) { Write-Host '（无 spec）'; return }
    Write-Host ''
    Write-Host ('{0,-24} {1,-22} {2,-6} {3}' -f 'SPEC_ID', 'STATUS', 'REQ', 'TITLE')
    Write-Host ('-' * 78)
    foreach ($d in $dirs) {
        $spec = Read-JsonFile (Join-Path $d.FullName 'spec.json')
        if ($null -eq $spec) {
            Write-Host ('{0,-24} {1,-22} {2,-6} {3}' -f $d.Name, '<invalid>', '-', '(spec.json 无法解析)')
            continue
        }
        $st = if (Test-Prop $spec.meta 'status') { $spec.meta.status } else { '?' }
        $req = if (Test-Prop $spec 'requirements') { @($spec.requirements).Count } else { 0 }
        $ti = if (Test-Prop $spec.meta 'title') { $spec.meta.title } else { '' }
        Write-Host ('{0,-24} {1,-22} {2,-6} {3}' -f $d.Name, $st, $req, $ti)
    }
    Write-Host ''
}

# ----------------------------------------------------------------- check ----

function Check-OneSpec {
    param([System.IO.DirectoryInfo]$Dir)

    $name = $Dir.Name
    Write-Host ''
    Write-Host "== $name ==" -ForegroundColor Cyan

    # --- 1. 文件齐备 ---
    $required = @('requirements.md', 'design.md', 'tasks.md', 'spec.json', 'trace.json')
    $missing = @()
    foreach ($f in $required) {
        if (-not (Test-Path (Join-Path $Dir.FullName $f))) { $missing += $f }
    }
    if ($missing.Count -gt 0) {
        Add-Err "[$name] 缺少文件：$($missing -join ', ')"
        return
    }

    # --- 2. spec.json 解析 ---
    $spec = Read-JsonFile (Join-Path $Dir.FullName 'spec.json')
    if ($null -eq $spec) { Add-Err "[$name] spec.json 无法解析"; return }

    # --- 2b. 结构契约：schema 是可执行门禁，不是文档 ---
    $schemaRoot = Get-SchemaRoot
    foreach ($pair in @(
        @{ file = 'spec.json'; schema = 'spec.schema.json' },
        @{ file = 'trace.json'; schema = 'trace.schema.json' }
    )) {
        $verr = @(Test-JsonFileAgainstSchema `
            -JsonPath (Join-Path $Dir.FullName $pair.file) `
            -SchemaPath (Join-Path $schemaRoot $pair.schema))
        foreach ($v in $verr) { Add-Err "[$name] schema 违规：$v" }
    }

    foreach ($k in @('spec_id', 'title', 'status', 'schema_version', 'created_at', 'updated_at', 'project')) {
        if (-not (Test-Prop $spec.meta $k)) { Add-Err "[$name] spec.json meta.$k 缺失" }
    }
    if ((Test-Prop $spec.meta 'spec_id') -and ($spec.meta.spec_id -ne $name)) {
        Add-Err "[$name] spec_id($($spec.meta.spec_id)) 与目录名不一致"
    }
    $status = if (Test-Prop $spec.meta 'status') { $spec.meta.status } else { 'draft' }
    $isDraft = ($status -eq 'draft')

    if (-not (Test-Prop $spec 'requirements')) { Add-Err "[$name] spec.json 缺 requirements"; return }
    $reqs = @($spec.requirements)
    if ($reqs.Count -eq 0) { Add-Err "[$name] requirements 为空"; return }

    # --- 3. REQ 编号：格式 / 唯一 / 顺序 ---
    $seen = @{}
    $expect = 1
    foreach ($r in $reqs) {
        $rid = $r.id
        if ($rid -notmatch '^REQ-\d{3}$') { Add-Err "[$name] 非法 REQ 编号：$rid"; continue }
        if ($seen.ContainsKey($rid)) { Add-Err "[$name] REQ 重复：$rid" }
        $seen[$rid] = $true
        $want = 'REQ-{0:D3}' -f $expect
        if ($rid -ne $want) { Add-Err "[$name] REQ 编号不连续：期望 $want，实际 $rid" }
        $expect++
    }

    # --- 4. 每条 REQ 的必填与内容 ---
    foreach ($r in $reqs) {
        $rid = $r.id
        foreach ($k in @('title', 'user_story', 'priority', 'category', 'ears', 'acceptance', 'tasks')) {
            if (-not (Test-Prop $r $k)) { Add-Err "[$name/$rid] 缺字段 $k" }
        }
        if (Test-Prop $r 'priority') {
            if ($r.priority -notin @('must', 'should', 'could')) { Add-Err "[$name/$rid] priority 非法：$($r.priority)" }
        }
        if (Test-Prop $r 'category') {
            if ($r.category -notin @('functional', 'interface', 'performance', 'constraint')) {
                Add-Err "[$name/$rid] category 非法：$($r.category)"
            }
        }
        $ears = @()
        if (Test-Prop $r 'ears') { $ears = @($r.ears) }
        $acc = @()
        if (Test-Prop $r 'acceptance') { $acc = @($r.acceptance) }
        $tk = @()
        if (Test-Prop $r 'tasks') { $tk = @($r.tasks) }
        if ($ears.Count -lt 1) {
            if ($isDraft) { Add-Warn "[$name/$rid] draft：尚无 EARS 验收句" } else { Add-Err "[$name/$rid] 缺 EARS 验收句" }
        }
        if ($acc.Count -lt 1) {
            if ($isDraft) { Add-Warn "[$name/$rid] draft：尚无可执行验收" } else { Add-Err "[$name/$rid] 缺可执行验收" }
        }
        if ($tk.Count -lt 1) {
            if ($isDraft) { Add-Warn "[$name/$rid] draft：未关联任务" } else { Add-Err "[$name/$rid] 未关联任何任务" }
        }
        if (-not $isDraft) {
            foreach ($e in $ears) {
                if ($e -notmatch 'THE SYSTEM SHALL') {
                    Add-Err "[$name/$rid] EARS 句缺少 'THE SYSTEM SHALL'：$e"
                }
            }
        }
    }

    # --- 5. tasks.md 与 REQ 双向覆盖 ---
    $tasks = Parse-TasksMd (Join-Path $Dir.FullName 'tasks.md')
    if ($tasks.Count -eq 0) {
        Add-Err "[$name] tasks.md 未解析到任何 '- [ ] **T-XXX**' 任务行"
    }
    $taskIds = @{}
    foreach ($t in $tasks) {
        if ($taskIds.ContainsKey($t.Id)) { Add-Err "[$name] 任务重复：$($t.Id)" }
        $taskIds[$t.Id] = $true
    }
    foreach ($r in $reqs) {
        if (-not (Test-Prop $r 'tasks')) { continue }
        foreach ($t in @($r.tasks)) {
            if (-not $taskIds.ContainsKey($t)) { Add-Err "[$name/$($r.id)] 引用了不存在的任务 $t" }
        }
    }
    foreach ($r in $reqs) {
        $rid = $r.id
        $hit = @($tasks | Where-Object { $_.Reqs -contains $rid })
        if ($hit.Count -eq 0) {
            if ($isDraft) { Add-Warn "[$name/$rid] draft：tasks.md 尚未引用" }
            else { Add-Err "[$name/$rid] tasks.md 中没有任何任务引用该需求" }
        }
    }

    # --- 6. trace.json 覆盖 ---
    $trace = Read-JsonFile (Join-Path $Dir.FullName 'trace.json')
    if ($null -eq $trace) { Add-Err "[$name] trace.json 无法解析"; return }
    if (-not (Test-Prop $trace 'traces')) { Add-Err "[$name] trace.json 缺 traces"; return }
    $specDir = $Dir.FullName
    $tr = @($trace.traces)
    $traced = @{}
    foreach ($e in $tr) {
        if (-not (Test-Prop $e 'requirement')) { Add-Err "[$name] trace 条目缺 requirement"; continue }
        $rid = $e.requirement
        if ($traced.ContainsKey($rid)) { Add-Err "[$name] trace 重复：$rid" }
        $traced[$rid] = $e
        if (-not $seen.ContainsKey($rid)) { Add-Err "[$name] trace 含未知 REQ：$rid（spec.json 中不存在）" }
        $files = @(); if (Test-Prop $e 'files') { $files = @($e.files) }
        $tests = @(); if (Test-Prop $e 'tests') { $tests = @($e.tests) }
        if ($files.Count -lt 1) { Add-Err "[$name/$rid] trace 缺 files" }

        # 门禁判据：追溯指向的路径必须真的存在（追溯不能指向空气）。
        $missingPaths = @()
        foreach ($p in ($files + $tests)) {
            if (-not (Resolve-RepoPath $p $specDir)) { $missingPaths += $p }
        }
        foreach ($p in $missingPaths) {
            Add-Err "[$name/$rid] trace 指向不存在的路径：$p"
        }

        $ver = ''
        if (Test-Prop $e 'verification') { $ver = [string]$e.verification }
        if ([string]::IsNullOrWhiteSpace($ver)) {
            Add-Err "[$name/$rid] trace 缺 verification"
        }
        $verPaths = @(Get-PathTokens $ver)
        foreach ($p in $verPaths) {
            if (-not (Resolve-RepoPath $p $specDir)) {
                Add-Err "[$name/$rid] verification 引用的证据文件不存在：$p"
            }
        }

        if (-not (Test-Prop $e 'status')) { Add-Err "[$name/$rid] trace 缺 status"; continue }
        if ($e.status -notin @('pending', 'verified', 'waived')) {
            Add-Err "[$name/$rid] trace.status 非法：$($e.status)"
            continue
        }
        $wr = ''
        if (Test-Prop $e 'waiver_reason') { $wr = [string]$e.waiver_reason }
        if ($e.status -eq 'waived') {
            if ([string]::IsNullOrWhiteSpace($wr)) {
                Add-Err "[$name/$rid] status=waived 必须给出 waiver_reason（谁豁免、为何豁免、何时补）"
            }
        }
        if ($e.status -eq 'verified') {
            $evd = @(@($tests) + @($verPaths) | Where-Object { Resolve-RepoPath $_ $specDir } | Select-Object -Unique)
            if ($evd.Count -lt 1) {
                Add-Err "[$name/$rid] status=verified 但 tests/verification 里没有任一真实存在的证据路径"
            }
            if ($tests.Count -eq 0 -and $verPaths.Count -eq 0) {
                Add-Err "[$name/$rid] tests 为空且 verification 未指向任何可核对产物（无法复核）"
            }
            # 自相矛盾检测：声称 verified，却在验证描述里承认有未覆盖/延后项。
            # 这类描述必须落到 waived + waiver_reason，否则"verified"的含义会被稀释。
            if ($ver -match '延后|尚未|未覆盖|待后续|无法裁决|未执行') {
                if ([string]::IsNullOrWhiteSpace($wr)) {
                    Add-Err "[$name/$rid] status=verified 却写明有未覆盖项，必须同时给出 waiver_reason：$ver"
                }
            }
        }
        if ($e.status -eq 'verified' -and ($isDraft -or $status -ne 'done')) {
            # 未收口的 spec 里，verified 条目必须留下可重跑的机读命令（不能只写"人工看过"）。
            if ($ver -notmatch '\.ps1') {
                Add-Warn "[$name/$rid] verified 的 verification 未包含可重跑命令（含 .ps1）：$ver"
            }
        }
    }
    foreach ($rid in $seen.Keys) {
        if (-not $traced.ContainsKey($rid)) {
            if ($isDraft) { Add-Warn "[$name/$rid] draft：trace.json 尚未覆盖" }
            else { Add-Err "[$name/$rid] trace.json 未覆盖该需求" }
        }
    }
    # --- 6b. acceptance 里的命令必须是本仓库真实存在的门禁 ---
    foreach ($r in $reqs) {
        foreach ($a in @(Get-PathTokens ((@($r.acceptance) -join ' | ')))) {
            if (-not (Resolve-RepoPath $a $specDir)) {
                Add-Err "[$name/$($r.id)] acceptance 引用了不存在的门禁/产物：$a"
            }
        }
    }

    # --- 7. 状态一致性（done）---
    if ($status -eq 'done') {
        $undone = @($tasks | Where-Object { -not $_.Done })
        if ($undone.Count -gt 0) {
            Add-Err "[$name] status=done 但仍有未勾选任务：$((@($undone | ForEach-Object { $_.Id })) -join ', ')"
        }
        $unver = @($tr | Where-Object { $_.status -ne 'verified' -and $_.status -ne 'waived' })
        if ($unver.Count -gt 0) {
            Add-Err "[$name] status=done 但 trace 中仍有未 verified 的条目：$((@($unver | ForEach-Object { $_.requirement })) -join ', ')"
        }
    }
}

function Invoke-Check {
    $dirs = Get-SpecDirs
    if ($Id) {
        $dirs = @($dirs | Where-Object { $_.Name -eq $Id })
        if ($dirs.Count -eq 0) { Write-Host "未找到 spec：$Id" -ForegroundColor Red; exit 2 }
    }
    if ($dirs.Count -eq 0) { Write-Host '（无 spec 可校验）'; return }

    foreach ($d in $dirs) { Check-OneSpec -Dir $d }

    Write-Host ''
    if ($script:Warns.Count -gt 0) {
        Write-Host "警告 $($script:Warns.Count) 条：" -ForegroundColor Yellow
        foreach ($w in $script:Warns) { Write-Host "  ! $w" -ForegroundColor Yellow }
    }
    if ($script:Errors.Count -gt 0) {
        Write-Host "错误 $($script:Errors.Count) 条：" -ForegroundColor Red
        foreach ($e in $script:Errors) { Write-Host "  x $e" -ForegroundColor Red }
        Write-Host ''
        Write-Host 'CHECK FAILED' -ForegroundColor Red
        exit 1
    }
    Write-Host 'CHECK OK' -ForegroundColor Green
}

# ----------------------------------------------------------------- trace ----

function Invoke-Trace {
    if (-not $Id) { Write-Host 'trace 需要 -Id <spec-id>' -ForegroundColor Red; exit 2 }
    $dir = Join-Path (Get-SpecRoot) $Id
    if (-not (Test-Path $dir)) { Write-Host "未找到 spec：$Id" -ForegroundColor Red; exit 2 }
    $trace = Read-JsonFile (Join-Path $dir 'trace.json')
    if ($null -eq $trace) { Write-Host 'trace.json 无法解析' -ForegroundColor Red; exit 1 }

    Write-Host ''
    Write-Host "追溯矩阵：$Id" -ForegroundColor Cyan
    Write-Host ('{0,-9} {1,-10} {2}' -f 'REQ', 'STATUS', 'FILES / VERIFICATION')
    Write-Host ('-' * 78)
    foreach ($e in @($trace.traces)) {
        $rid = $e.requirement
        $st = $e.status
        $files = (@($e.files) -join ', ')
        $ver = $e.verification
        Write-Host ('{0,-9} {1,-10} {2}' -f $rid, $st, $files)
        if ($ver) { Write-Host ('{0,-9} {1,-10} {2}' -f '', '', "verify: $ver") }
    }
    Write-Host ''
}

# ------------------------------------------------------------------ main ----

switch ($Verb) {
    'new'   { Invoke-New }
    'list'  { Invoke-List }
    'check' { Invoke-Check }
    'trace' { Invoke-Trace }
}

exit 0
