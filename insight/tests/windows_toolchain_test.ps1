# SPDX-License-Identifier: GPL-2.0-or-later
# No Pester or network required. Run in Windows PowerShell 5.1 and PowerShell 7.
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$scriptPath = Join-Path $PSScriptRoot '..\tools\setup-windows.ps1'
$tokens = $null; $errors = $null
$null = [Management.Automation.Language.Parser]::ParseFile($scriptPath, [ref]$tokens, [ref]$errors)
if ($errors.Count) { throw ($errors | Out-String) }
$unused = Join-Path $env:TEMP ('insight-plan-' + [guid]::NewGuid().ToString('N'))
$before = $env:MSYSTEM
$plan = (& $scriptPath -Plan -MsysRoot 'C:\not-installed' -CacheDir $unused -SourceDir "$unused\src" -FetchSources | Out-String) | ConvertFrom-Json
if (Test-Path -LiteralPath $unused) { throw 'Plan wrote to disk.' }
if ($env:MSYSTEM -ne $before) { throw 'Plan changed the process environment.' }
if ($plan.clangProvider -ne 'Internal' -or -not $plan.fetchSources) { throw 'Incorrect default plan.' }
if ($plan.installer.sha256 -notmatch '^[a-f0-9]{64}$' -or $plan.installer.url -notmatch '^https://github.com/msys2/msys2-installer/releases/download/') { throw 'Unverified installer source.' }
foreach ($suffix in @('gcc','gtk3','pkgconf','meson','ninja','cmake','python','python-docutils')) {
    if ($plan.packages -notcontains "mingw-w64-ucrt-x86_64-$suffix") { throw "Missing $suffix" }
}
if ($plan.packages -contains 'mingw-w64-ucrt-x86_64-clang') { throw 'Internal selected system Clang.' }
$system = (& $scriptPath -Plan -ClangProvider System -Autotools -Packaging | Out-String) | ConvertFrom-Json
foreach ($suffix in @('clang','llvm','autotools','nsis')) {
    if ($system.packages -notcontains "mingw-w64-ucrt-x86_64-$suffix") { throw "Missing optional $suffix" }
}
$disabled = (& $scriptPath -Plan -ClangProvider Disabled | Out-String) | ConvertFrom-Json
if ($disabled.packages -contains 'mingw-w64-ucrt-x86_64-clang') { throw 'Disabled selected system Clang.' }
if (-not ((& $scriptPath -Help | Out-String) -match '-FetchSources')) { throw 'Help missing.' }
Write-Host 'PowerShell parsing and bootstrap plans passed; no filesystem or environment changes.'
