# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Exercise real bundle restoration into occupied directories without downloading or building tools.
# RU: Проверяет восстановление набора при занятых каталогах без загрузки и сборки инструментов.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# EN: Fail with Message unless Condition holds; no Pester dependency is required.
# RU: Завершает проверку сообщением Message при ложном Condition; Pester не требуется.
function Assert-Offline([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

# EN: Pack Folder into Archive with the real platform tar and check its exit code.
# RU: Упаковывает Folder в Archive настоящим tar платформы и проверяет код возврата.
function New-OfflineFixtureArchive([string]$Folder, [string]$Archive) {
    & $tar -cf $Archive -C $Folder .
    if ($LASTEXITCODE -ne 0) { throw "Fixture tar failed: $Archive" }
}

. (Join-Path $PSScriptRoot '..\tools\windows-offline.ps1')
$isWindowsHost = [Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT
$temporary = [IO.Path]::GetTempPath()
$root = Join-Path $temporary ('insight-offline-restore-' + [guid]::NewGuid().ToString('N'))
$names = @('SystemRoot', 'INSIGHT_OFFLINE', 'GIT_ALLOW_PROTOCOL', 'INSIGHT_BOOTSTRAP_SOURCE')
$original = @{}
foreach ($name in $names) { $original[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
New-Item -ItemType Directory -Path $root | Out-Null
try {
    if ($isWindowsHost) {
        . (Join-Path $PSScriptRoot '..\tools\select-windows-folders.ps1')
        $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    } else {
        # EN: Only the Windows location chooser is stubbed on Linux; extraction uses real tar.
        # RU: В Linux заменяется только валидатор путей Windows; распаковку выполняет настоящий tar.
        function Test-InsightLocations($Locations) { return $Locations }
        $env:SystemRoot = Join-Path $root 'windows'
        $system = Join-Path $env:SystemRoot 'System32'
        New-Item -ItemType Directory -Path $system -Force | Out-Null
        $tar = Join-Path $system 'tar.exe'
        Copy-Item -LiteralPath (Get-Command tar -CommandType Application).Source -Destination $tar
    }
    $RootDir = Join-Path $root 'work'
    $MsysRoot = Join-Path $RootDir 'msys64'
    $SourceDir = Join-Path $RootDir 'checkout with spaces'
    $InstallDir = Join-Path $RootDir 'editor'
    $CacheDir = Join-Path $RootDir 'cache'
    $DownloadBundle = ''
    $packages = @('fixture')
    $ClangProvider = 'Disabled'
    $bundle = Join-Path $root 'bundle'
    $toolFixture = Join-Path $root 'tool-fixture'
    $sourceFixture = Join-Path $root 'source-fixture'
    $bin = Join-Path $toolFixture 'usr\bin'
    New-Item -ItemType Directory -Path $bin, $sourceFixture, $bundle, $MsysRoot, $SourceDir -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $bin 'bash.exe'), 'fixture bash marker')
    [IO.File]::WriteAllText((Join-Path $sourceFixture 'build.cmd'), 'fixture source snapshot')
    [IO.File]::WriteAllText((Join-Path $MsysRoot 'existing-tool.txt'), 'keep existing tools')
    [IO.File]::WriteAllText((Join-Path $SourceDir 'local-edit.txt'), 'keep local editor changes')
    $originalTools = $MsysRoot
    $originalSource = $SourceDir
    New-OfflineFixtureArchive $toolFixture (Join-Path $bundle 'msys2.tar')
    New-OfflineFixtureArchive $sourceFixture (Join-Path $bundle 'sources.tar.gz')
    New-OfflineFixtureArchive $sourceFixture (Join-Path $bundle 'iverilog-13.0.tar.gz')
    $files = @()
    foreach ($name in @('msys2.tar', 'sources.tar.gz', 'iverilog-13.0.tar.gz')) {
        $files += [ordered]@{
            name=$name
            sha256=(Get-FileHash -LiteralPath (Join-Path $bundle $name) -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $metadata = [ordered]@{
        format=1; platform='windows-x64'; clangProvider=$ClangProvider; packages=$packages; files=$files
    }
    [IO.File]::WriteAllText((Join-Path $bundle 'bundle.json'), ($metadata | ConvertTo-Json -Depth 6))
    $id = (Get-FileHash -LiteralPath (Join-Path $bundle 'bundle.json') -Algorithm SHA256).Hash

    # EN: Reproduce the screenshot with both tool and source directories already occupied.
    # RU: Воспроизводим снимок с уже занятыми папками инструментов и исходников.
    Restore-InsightBundle $bundle
    Assert-Offline ($MsysRoot -ne $originalTools) 'Occupied MSYS2 path was not redirected.'
    Assert-Offline ($SourceDir -ne $originalSource) 'Existing checkout was not redirected.'
    Assert-Offline ((Get-Content -LiteralPath (Join-Path $originalTools 'existing-tool.txt') -Raw) -eq 'keep existing tools') 'Existing tools changed.'
    Assert-Offline ((Get-Content -LiteralPath (Join-Path $originalSource 'local-edit.txt') -Raw) -eq 'keep local editor changes') 'Local source edits changed.'
    Assert-Offline (Test-Path -LiteralPath $bash -PathType Leaf) 'Bash path did not follow the redirected toolchain.'
    Assert-Offline (Test-Path -LiteralPath (Join-Path $SourceDir 'build.cmd')) 'Bundled sources were not restored.'
    Assert-Offline ($env:INSIGHT_BOOTSTRAP_SOURCE -eq $SourceDir) 'Build environment still points at the occupied checkout.'
    $restoredTools = $MsysRoot
    $restoredSource = $SourceDir

    # EN: Repeating from the original selections must reuse both restored directories.
    # RU: Повтор из исходных выбранных папок должен использовать уже восстановленные каталоги.
    [IO.File]::WriteAllText((Join-Path $restoredSource 'reuse-sentinel.txt'), 'reuse')
    $MsysRoot = $originalTools
    $SourceDir = $originalSource
    Restore-InsightBundle $bundle
    Assert-Offline ($MsysRoot -eq $restoredTools -and $SourceDir -eq $restoredSource) 'Retry selected different destinations.'
    Assert-Offline (Test-Path -LiteralPath (Join-Path $SourceDir 'reuse-sentinel.txt')) 'Retry unpacked over a matching snapshot.'

    # EN: An occupied reserved fallback must fail before tools are unpacked elsewhere.
    # RU: Занятая резервная папка должна вызвать отказ до распаковки инструментов в другое место.
    $MsysRoot = Join-Path $RootDir 'new-tools'
    $SourceDir = Join-Path $RootDir 'busy-source'
    $reserved = $SourceDir + '.bundle-' + $id.Substring(0, 12).ToLowerInvariant()
    New-Item -ItemType Directory -Path $SourceDir, $reserved -Force | Out-Null
    [IO.File]::WriteAllText((Join-Path $SourceDir 'user.txt'), 'original')
    [IO.File]::WriteAllText((Join-Path $reserved 'user.txt'), 'reserved')
    $rejected = $false
    try { Restore-InsightBundle $bundle } catch {
        $rejected = $_.Exception.Message -like '*reserved bundle directory is occupied*'
        if (-not $rejected) { throw }
    }
    Assert-Offline $rejected 'Occupied reserved source directory was accepted.'
    Assert-Offline (-not (Test-Path -LiteralPath (Join-Path $RootDir 'new-tools'))) 'Toolchain unpacked before source preflight failed.'
    Assert-Offline ((Get-Content -LiteralPath (Join-Path $reserved 'user.txt') -Raw) -eq 'reserved') 'Reserved user directory changed.'

    # EN: New and empty destinations retain their requested names.
    # RU: Новые и пустые каталоги сохраняют выбранные имена.
    $empty = Join-Path $RootDir 'empty'
    New-Item -ItemType Directory -Path $empty | Out-Null
    Assert-Offline ((Resolve-InsightBundleDestination $empty $id 'SourceDir') -eq $empty) 'Empty destination was redirected.'
    $new = Join-Path $RootDir 'absent'
    Assert-Offline ((Resolve-InsightBundleDestination $new $id 'SourceDir') -eq $new) 'New destination was redirected.'
    # EN: Empty marker files and ordinary files must not be mistaken for a reusable restore.
    # RU: Пустая метка и обычный файл не должны считаться готовым восстановленным набором.
    [IO.File]::WriteAllText((Join-Path $empty '.insight-offline-bundle'), '')
    Assert-Offline ((Resolve-InsightBundleDestination $empty $id 'SourceDir') -ne $empty) 'Empty stamp was reused.'
    $file = Join-Path $RootDir 'ordinary-file'
    [IO.File]::WriteAllText($file, 'keep this file')
    Assert-Offline ((Resolve-InsightBundleDestination $file $id 'MsysRoot') -ne $file) 'Ordinary file was accepted as a tool directory.'
    Assert-Offline ((Get-Content -LiteralPath $file -Raw) -eq 'keep this file') 'Ordinary file changed.'
    Write-Host 'Offline restore: occupied checkout/tools, retry reuse and preflight preservation passed.'
} finally {
    foreach ($name in $names) { [Environment]::SetEnvironmentVariable($name, $original[$name], 'Process') }
    Remove-Item -LiteralPath $root -Recurse -Force
}
