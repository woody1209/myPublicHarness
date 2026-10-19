# JsonSchema.ps1 - minimal, deterministic JSON-Schema subset validator.
#
# Why this exists: harness/schemas/*.json are the written contract for
# spec.json / trace.json / state.json, but a contract nothing executes cannot
# fail, so it silently rots (state.json carried keys its own schema forbade).
# PowerShell has no schema validator and the host has no python jsonschema
# module, so the subset below is implemented here and *called by the gates*.
#
# Supported keywords: type (incl. union), required, properties,
# additionalProperties (bool or subschema), items, minItems, minLength,
# pattern, enum, minimum. Anything else (title/description/$id) is documentation.
#
# Callers get an array of violation strings; empty = valid.
#
# PS gotcha: inside `if (...)` a bare-word command call swallows the following
# -and/-notmatch/-lt tokens as *parameters*, so `(Test-SchemaKey ...)` must stay
# parenthesised or the condition is permanently true.

function Get-JsonKind {
    param($Value)
    if ($null -eq $Value) { return 'null' }
    if ($Value -is [bool]) { return 'boolean' }
    if ($Value -is [byte] -or $Value -is [int16] -or $Value -is [int] -or $Value -is [long] -or $Value -is [uint32] -or $Value -is [int64]) { return 'integer' }
    if ($Value -is [single] -or $Value -is [double] -or $Value -is [decimal]) { return 'number' }
    if ($Value -is [string]) { return 'string' }
    if ($Value -is [System.Management.Automation.PSCustomObject]) { return 'object' }
    if ($Value -is [System.Collections.IDictionary]) { return 'object' }
    if ($Value -is [System.Collections.IEnumerable]) { return 'array' }
    return 'unknown'
}

function Test-SchemaKey {
    param($Schema, [string]$Key)
    if ($null -eq $Schema) { return $false }
    return ($Schema.PSObject.Properties.Name -contains $Key)
}

function Test-JsonSchema {
    param(
        $Instance,
        $Schema,
        [string]$Node = '$'
    )

    $errs = New-Object System.Collections.ArrayList
    if ($null -eq $Schema) { return $errs
    }
    $kind = Get-JsonKind $Instance

    # ---- type ------------------------------------------------------------
    if ((Test-SchemaKey $Schema 'type')) {
        $want = @($Schema.type) | ForEach-Object { [string]$_ }
        $ok = ($want -contains $kind) -or (($want -contains 'number') -and ($kind -eq 'integer'))
        if (-not $ok) {
            [void]$errs.Add("$Node : type is '$kind', schema requires $($want -join '|')")
            return $errs
        }
    }

    # ---- enum ------------------------------------------------------------
    if ((Test-SchemaKey $Schema 'enum')) {
        $want = @($Schema.enum) | ForEach-Object { [string]$_ }
        if ($want -notcontains [string]$Instance) {
            [void]$errs.Add("$Node : value '$Instance' is not one of [$($want -join ', ')]")
        }
    }

    # ---- string ----------------------------------------------------------
    if ($kind -eq 'string') {
        if ((Test-SchemaKey $Schema 'pattern') -and ($Instance -notmatch $Schema.pattern)) {
            [void]$errs.Add("$Node : '$Instance' does not match pattern $($Schema.pattern)")
        }
        if ((Test-SchemaKey $Schema 'minLength') -and ($Instance.Length -lt [int]$Schema.minLength)) {
            [void]$errs.Add("$Node : length $($Instance.Length) < minLength $($Schema.minLength)")
        }
    }

    # ---- number ----------------------------------------------------------
    if ($kind -eq 'integer' -or $kind -eq 'number') {
        if ((Test-SchemaKey $Schema 'minimum') -and ($Instance -lt $Schema.minimum)) {
            [void]$errs.Add("$Node : value $Instance < minimum $($Schema.minimum)")
        }
    }

    # ---- array -----------------------------------------------------------
    if ($kind -eq 'array') {
        $items = @($Instance)
        if ((Test-SchemaKey $Schema 'minItems') -and ($items.Count -lt [int]$Schema.minItems)) {
            [void]$errs.Add("$Node : $($items.Count) items < minItems $($Schema.minItems)")
        }
        if ((Test-SchemaKey $Schema 'items')) {
            for ($i = 0; $i -lt $items.Count; $i++) {
                $sub = @(Test-JsonSchema -Instance $items[$i] -Schema $Schema.items -Node "$Node[$i]")
                foreach ($e in $sub) { [void]$errs.Add($e) }
            }
        }
    }

    # ---- object ----------------------------------------------------------
    if ($kind -eq 'object') {
        $names = @($Instance.PSObject.Properties.Name)

        if ((Test-SchemaKey $Schema 'required')) {
            foreach ($r in @($Schema.required)) {
                if ($names -notcontains $r) { [void]$errs.Add("$Node : required member '$r' is missing") }
            }
        }

        $propNames = @()
        if ((Test-SchemaKey $Schema 'properties')) {
            $propNames = @($Schema.properties.PSObject.Properties.Name)
        }

        foreach ($n in $names) {
            if ($propNames -contains $n) {
                $sub = @(Test-JsonSchema -Instance $Instance.$n -Schema $Schema.properties.$n -Node "$Node.$n")
                foreach ($e in $sub) { [void]$errs.Add($e) }
                continue
            }
            if ((Test-SchemaKey $Schema 'additionalProperties')) {
                $ap = $Schema.additionalProperties
                if ($ap -is [bool]) {
                    if (-not $ap) { [void]$errs.Add("$Node : member '$n' is not allowed (additionalProperties=false)") }
                } else {
                    $sub = @(Test-JsonSchema -Instance $Instance.$n -Schema $ap -Node "$Node.$n")
                    foreach ($e in $sub) { [void]$errs.Add($e) }
                }
            }
        }
    }

    return $errs
}

function Read-JsonSchema {
    param([string]$Path)
    if (-not (Test-Path $Path)) { return $null }
    try { return (Get-Content -Raw -Encoding UTF8 $Path | ConvertFrom-Json) } catch { return $null }
}

# Validates one JSON file against one schema file. Returns violation strings
# prefixed with the repo-relative document path so the message is actionable.
function Test-JsonFileAgainstSchema {
    param([string]$JsonPath, [string]$SchemaPath)

    $label = $JsonPath
    if (Test-Path $JsonPath) { $label = (Resolve-Path -Relative $JsonPath) -replace '^\.' -replace '\\', '/' }
    if (-not (Test-Path $JsonPath)) { return @("$label : file missing") }
    $schema = Read-JsonSchema $SchemaPath
    if ($null -eq $schema) { return @("$label : schema unreadable ($SchemaPath)") }
    try {
        $doc = Get-Content -Raw -Encoding UTF8 $JsonPath | ConvertFrom-Json
    } catch {
        return @("$label : JSON parse failed: $($_.Exception.Message)")
    }
    return @(Test-JsonSchema -Instance $doc -Schema $schema -Node $label)
}
