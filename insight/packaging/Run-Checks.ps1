$ErrorActionPreference = 'Stop'
foreach ($name in 'runtime', 'index', 'document', 'snapshot', 'commands', 'editor_api', 'events', 'context', 'occurrences') {
    & (Join-Path $PSScriptRoot "checks\${name}_test.exe")
    if ($LASTEXITCODE -ne 0) { throw "Check failed: $name ($LASTEXITCODE)" }
}
Write-Host 'All 9 core checks passed.'
