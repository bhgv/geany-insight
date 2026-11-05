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
if ($plan.msysMirror -ne 'auto') { throw 'Incorrect default MSYS2 mirror.' }
$mirrorPlan = (& $scriptPath -Plan -MsysMirror 'https://example.org/msys2' | Out-String) | ConvertFrom-Json
if ($mirrorPlan.msysMirror -ne 'https://example.org/msys2') { throw 'Mirror override lost.' }
$system = (& $scriptPath -Plan -ClangProvider System -Autotools -Packaging | Out-String) | ConvertFrom-Json
foreach ($suffix in @('clang','llvm','autotools','nsis')) {
    if ($system.packages -notcontains "mingw-w64-ucrt-x86_64-$suffix") { throw "Missing optional $suffix" }
}
$disabled = (& $scriptPath -Plan -ClangProvider Disabled | Out-String) | ConvertFrom-Json
if ($disabled.packages -contains 'mingw-w64-ucrt-x86_64-clang') { throw 'Disabled selected system Clang.' }
if (-not ((& $scriptPath -Help | Out-String) -match '-FetchSources')) { throw 'Help missing.' }
$checkout = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$launcher = Join-Path $checkout 'build.cmd'
$rootMenu = ('4' | & $launcher | Out-String)
if ($LASTEXITCODE -ne 0 -or $rootMenu -notmatch 'Preview the default build plan') { throw 'Root launcher menu failed.' }
$rootHelp = (& $launcher -Help | Out-String)
if ($LASTEXITCODE -ne 0 -or $rootHelp -notmatch 'build.cmd -BuildEditor') { throw 'Root launcher help failed.' }
$rootPlan = (& $launcher -BuildEditor -NonInteractive -RootDir 'C:\Insight Test' -Plan | Out-String) | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $rootPlan.sourceDir -ne $checkout -or
    $rootPlan.installDir -ne 'C:\Insight Test\editor' -or -not $rootPlan.buildEditor) { throw 'Root launcher plan failed.' }
if ($rootPlan.simulator.version -ne '13.0' -or
    $rootPlan.simulator.sha256 -notmatch '^[a-f0-9]{64} -BuildEditor -Plan -SourceDir 'C:\Alternate Checkout' | Out-String) | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $override.sourceDir -ne 'C:\Alternate Checkout') { throw 'Source override lost.' }
Write-Host 'PowerShell parsing and bootstrap plans passed; no filesystem or environment changes.'
# Production folder dialog: changing drives, independent paths, validation and cancel.
. (Join-Path $PSScriptRoot '..\tools\select-windows-folders.ps1')
$base = if (Test-Path D:\) { 'D:\Development\GeanyInsight' } else { 'C:\Development\GeanyInsight' }
$dialog = New-InsightFolderDialog @{RootDir=$base; MsysRoot="$base\tools\msys64"; SourceDir="$base\src"; InstallDir="$base\editor"}
try {
    $dialog.Show(); [Windows.Forms.Application]::DoEvents()
    $find = {param($name) $dialog.Controls.Find($name,$true)[0]}
    $root = & $find 'RootDir'; $root.Text = $base
    (& $find 'ApplyBase').PerformClick()
    if ((& $find 'MsysRoot').Text -ne "$base\tools\msys64") { throw 'Base folder did not propagate.' }
    (& $find 'SourceDir').Text = "$base\my sources"
    (& $find 'InstallDir').Text = "$base\my editor"
    (& $find 'MsysRoot').Text = 'C:\Bad tools'
    (& $find 'AcceptLocations').PerformClick()
    if ($dialog.DialogResult -eq [Windows.Forms.DialogResult]::OK) { throw 'Accepted unsupported MSYS path.' }
    (& $find 'MsysRoot').Text = "$base\tools\msys64"
    $image = New-Object Drawing.Bitmap($dialog.Width,$dialog.Height)
    try {
        $dialog.DrawToBitmap($image, (New-Object Drawing.Rectangle(0,0,$image.Width,$image.Height)))
        $image.Save((Join-Path (Get-Location) ("windows-locations-" + $PSVersionTable.PSVersion.Major + '.jpg')), [Drawing.Imaging.ImageFormat]::Jpeg)
    } finally { $image.Dispose() }
    (& $find 'AcceptLocations').PerformClick()
    if ($dialog.Tag.SourceDir -ne "$base\my sources" -or $dialog.Tag.InstallDir -ne "$base\my editor") { throw 'Independent selections lost.' }
} finally { $dialog.Dispose() }
$plan = (& $scriptPath -Plan -RootDir $base -BuildEditor -NonInteractive | Out-String) | ConvertFrom-Json
if ($plan.installDir -ne "$base\editor" -or $plan.msysRoot -ne "$base\tools\msys64" -or -not $plan.fetchSources) { throw 'Selected base/build plan lost.' }
Write-Host 'Production folder dialog and selected-drive build plan passed.'
 -or
    $rootPlan.simulator.url -notmatch '^https://github.com/steveicarus/iverilog/archive/') {
    throw 'Pinned Icarus Verilog source is missing from the build plan.'
}
foreach ($package in @('make','bison','flex','mingw-w64-ucrt-x86_64-readline')) {
    if ($rootPlan.packages -notcontains $package) { throw "Missing Icarus build dependency: $package" }
}
$override = (& $launcher -BuildEditor -Plan -SourceDir 'C:\Alternate Checkout' | Out-String) | ConvertFrom-Json
if ($LASTEXITCODE -ne 0 -or $override.sourceDir -ne 'C:\Alternate Checkout') { throw 'Source override lost.' }
Write-Host 'PowerShell parsing and bootstrap plans passed; no filesystem or environment changes.'
# Production folder dialog: changing drives, independent paths, validation and cancel.
. (Join-Path $PSScriptRoot '..\tools\select-windows-folders.ps1')
$base = if (Test-Path D:\) { 'D:\Development\GeanyInsight' } else { 'C:\Development\GeanyInsight' }
$dialog = New-InsightFolderDialog @{RootDir=$base; MsysRoot="$base\tools\msys64"; SourceDir="$base\src"; InstallDir="$base\editor"}
try {
    $dialog.Show(); [Windows.Forms.Application]::DoEvents()
    $find = {param($name) $dialog.Controls.Find($name,$true)[0]}
    $root = & $find 'RootDir'; $root.Text = $base
    (& $find 'ApplyBase').PerformClick()
    if ((& $find 'MsysRoot').Text -ne "$base\tools\msys64") { throw 'Base folder did not propagate.' }
    (& $find 'SourceDir').Text = "$base\my sources"
    (& $find 'InstallDir').Text = "$base\my editor"
    (& $find 'MsysRoot').Text = 'C:\Bad tools'
    (& $find 'AcceptLocations').PerformClick()
    if ($dialog.DialogResult -eq [Windows.Forms.DialogResult]::OK) { throw 'Accepted unsupported MSYS path.' }
    (& $find 'MsysRoot').Text = "$base\tools\msys64"
    $image = New-Object Drawing.Bitmap($dialog.Width,$dialog.Height)
    try {
        $dialog.DrawToBitmap($image, (New-Object Drawing.Rectangle(0,0,$image.Width,$image.Height)))
        $image.Save((Join-Path (Get-Location) ("windows-locations-" + $PSVersionTable.PSVersion.Major + '.jpg')), [Drawing.Imaging.ImageFormat]::Jpeg)
    } finally { $image.Dispose() }
    (& $find 'AcceptLocations').PerformClick()
    if ($dialog.Tag.SourceDir -ne "$base\my sources" -or $dialog.Tag.InstallDir -ne "$base\my editor") { throw 'Independent selections lost.' }
} finally { $dialog.Dispose() }
$plan = (& $scriptPath -Plan -RootDir $base -BuildEditor -NonInteractive | Out-String) | ConvertFrom-Json
if ($plan.installDir -ne "$base\editor" -or $plan.msysRoot -ne "$base\tools\msys64" -or -not $plan.fetchSources) { throw 'Selected base/build plan lost.' }
Write-Host 'Production folder dialog and selected-drive build plan passed.'
