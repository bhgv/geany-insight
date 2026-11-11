# SPDX-License-Identifier: GPL-2.0-or-later
# EN: Create and restore a portable Windows build bundle with checked local assets.
# RU: Создаёт и восстанавливает переносимый набор сборки с проверкой локальных файлов.

# EN: Reject Bundle paths overlapping mutable tool, source, cache or install directories.
# RU: Отклоняет пути Bundle, пересекающиеся с папками инструментов, исходников, кэша или установки.
function Test-InsightBundleLocation([string]$Bundle) {
    $bundlePath = [IO.Path]::GetFullPath($Bundle).TrimEnd('\').ToLowerInvariant()
    foreach ($path in @($MsysRoot, $SourceDir, $InstallDir, $CacheDir)) {
        $target = [IO.Path]::GetFullPath($path).TrimEnd('\').ToLowerInvariant()
        if ($bundlePath -eq $target -or $bundlePath.StartsWith($target + '\') -or
            $target.StartsWith($bundlePath + '\')) {
            throw "Bundle and working directories must be separate: $Bundle overlaps $path"
        }
    }
    if ($DownloadBundle -and (Test-Path -LiteralPath $Bundle) -and
        @(Get-ChildItem -LiteralPath $Bundle -Force).Count) {
        throw "Choose an empty bundle destination: $Bundle"
    }
}

# EN: Read Bundle metadata and verify every required payload before extraction.
# RU: Читает описание Bundle и проверяет все обязательные файлы до распаковки.
function Read-InsightBundle([string]$Bundle) {
    $metadata = Join-Path $Bundle 'bundle.json'
    if (-not (Test-Path -LiteralPath $metadata -PathType Leaf)) {
        throw "Bundle is incomplete: $metadata is missing."
    }
    $info = Get-Content -LiteralPath $metadata -Raw | ConvertFrom-Json
    if ($info.format -ne 1 -or $info.platform -ne 'windows-x64') {
        throw 'Unsupported offline bundle format or platform.'
    }
    foreach ($name in @('msys2.tar', 'sources.tar.gz', 'iverilog-13.0.tar.gz')) {
        $entry = @($info.files | Where-Object { $_.name -eq $name })
        if ($entry.Count -ne 1 -or $entry[0].sha256 -notmatch '^[a-f0-9]{64}$') {
            throw "Bundle has no valid checksum for $name."
        }
        $path = Join-Path $Bundle $name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Bundle file is missing: $path"
        }
        Write-Host "[Verify] $name"
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $entry[0].sha256) {
            throw "Bundle checksum mismatch: $path. Copy the file again."
        }
    }
    return $info
}

# EN: Restore Archive to an empty Destination, or reuse its matching BundleId stamp.
# RU: Распаковывает Archive в пустой Destination либо использует набор с меткой BundleId.
function Expand-InsightBundleArchive([string]$Archive, [string]$Destination, [string]$BundleId) {
    $stamp = Join-Path $Destination '.insight-offline-bundle'
    if (Test-Path -LiteralPath $stamp) {
        if ((Get-Content -LiteralPath $stamp -Raw).Trim() -eq $BundleId) {
            Write-Host "[Offline] Reusing $Destination"
            return
        }
        throw "Another bundle already occupies $Destination. Choose an empty destination."
    }
    if ((Test-Path -LiteralPath $Destination) -and
        @(Get-ChildItem -LiteralPath $Destination -Force).Count) {
        throw "Offline restore requires an empty directory: $Destination"
    }
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (-not (Test-Path -LiteralPath $tar)) {
        throw 'Windows tar.exe is required to unpack the bundle (Windows 10 1803 or newer).'
    }
    $staging = $Destination.TrimEnd('\') + '.restore-' + [guid]::NewGuid().ToString('N')
    New-Item -ItemType Directory -Path $staging -Force | Out-Null
    try {
        Write-Host "[Unpack] $Archive -> $Destination"
        & $tar -xf $Archive -C $staging
        if ($LASTEXITCODE -ne 0) { throw "Could not extract $Archive ($LASTEXITCODE)." }
        [IO.File]::WriteAllText((Join-Path $staging '.insight-offline-bundle'), $BundleId)
        if (Test-Path -LiteralPath $Destination) {
            Remove-Item -LiteralPath $Destination -Force
        }
        Move-Item -LiteralPath $staging -Destination $Destination
    } finally {
        if (Test-Path -LiteralPath $staging) {
            Remove-Item -LiteralPath $staging -Recurse -Force
        }
    }
}

# EN: Restore Bundle into the selected tools, source and cache paths after checking options.
# RU: Восстанавливает Bundle в выбранные папки инструментов, исходников и кэша после проверки настроек.
function Restore-InsightBundle([string]$Bundle) {
    $info = Read-InsightBundle $Bundle
    foreach ($package in $packages) {
        if ($info.packages -notcontains $package) {
            throw "Bundle lacks $package. Prepare it with the same build options."
        }
    }
    if ($info.clangProvider -ne $ClangProvider) {
        throw "Bundle Clang provider is $($info.clangProvider); use -ClangProvider $($info.clangProvider)."
    }
    $bundleId = (Get-FileHash -LiteralPath (Join-Path $Bundle 'bundle.json') -Algorithm SHA256).Hash
    Expand-InsightBundleArchive (Join-Path $Bundle 'msys2.tar') $MsysRoot $bundleId
    Expand-InsightBundleArchive (Join-Path $Bundle 'sources.tar.gz') $SourceDir $bundleId
    New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $Bundle 'iverilog-13.0.tar.gz') -Destination $CacheDir -Force
    if (-not (Test-Path -LiteralPath $bash)) {
        throw 'Bundle did not restore usr\bin\bash.exe.'
    }
    foreach ($directory in @('tmp', 'home')) {
        New-Item -ItemType Directory -Path (Join-Path $MsysRoot $directory) -Force | Out-Null
    }
    $env:INSIGHT_OFFLINE = '1'
    $env:GIT_ALLOW_PROTOCOL = 'file'
    $env:INSIGHT_BOOTSTRAP_SOURCE = [IO.Path]::GetFullPath($SourceDir)
    Write-Host '[Offline] Dependencies restored. Repository updates and source downloads are skipped.'
}

# EN: Save the provisioned toolchain and all pinned sources to Bundle without compiling.
# RU: Сохраняет готовые инструменты и все закреплённые исходники в Bundle без компиляции.
function Save-InsightBundle([string]$Bundle) {
    if ((Test-Path -LiteralPath $Bundle) -and
        @(Get-ChildItem -LiteralPath $Bundle -Force).Count) {
        throw "Choose an empty bundle destination: $Bundle"
    }
    $destination = $Bundle
    $Bundle = $destination + '.partial-' + [guid]::NewGuid().ToString('N')
    New-Item -ItemType Directory -Path $Bundle -Force | Out-Null
    try {
        $env:INSIGHT_BUNDLE_PATH = [IO.Path]::GetFullPath($Bundle)
        $env:INSIGHT_BOOTSTRAP_CACHE = [IO.Path]::GetFullPath($CacheDir)
        $env:INSIGHT_BOOTSTRAP_PROVIDER = $ClangProvider.ToLowerInvariant()
        $null = Invoke-Msys -Stage 'Portable source downloads and toolchain snapshot' -Script @'
set -euo pipefail
source_dir=$(cygpath -u "$INSIGHT_BOOTSTRAP_SOURCE")
bundle=$(cygpath -u "$INSIGHT_BUNDLE_PATH")
cd "$source_dir"
python3 insight/tools/windows_bundle.py prepare --bundle "$bundle" --cache "$INSIGHT_BOOTSTRAP_CACHE" --provider "$INSIGHT_BOOTSTRAP_PROVIDER"
# EN: Dereference links so Windows tar can restore the archive without elevation.
# RU: Сохраняем содержимое ссылок для распаковки Windows tar без повышения прав.
snapshot_options=(
    --dereference --hard-dereference
    --exclude=var/cache --exclude=var/log --exclude=var/run
    --exclude=var/lib/pacman/db.lck --exclude='*/S.gpg-agent*'
)
tar "${snapshot_options[@]}" -cf "$bundle/msys2.tar" -C / usr ucrt64 etc var
# EN: Add launchers that are present in this pinned MSYS2 distribution.
# RU: Добавляем программы запуска, присутствующие в этом выпуске MSYS2.
for launcher in msys2_shell.cmd msys2.exe msys2.ini ucrt64.exe ucrt64.ini; do
    if [ -f "/$launcher" ]; then
        tar -rf "$bundle/msys2.tar" -C / "$launcher"
    fi
done
pacman -Q > "$bundle/packages.txt"
'@
        $files = @()
        foreach ($name in @('msys2.tar', 'sources.tar.gz', 'iverilog-13.0.tar.gz')) {
            $files += [ordered]@{
                name = $name
                sha256 = (Get-FileHash -LiteralPath (Join-Path $Bundle $name) -Algorithm SHA256).Hash.ToLowerInvariant()
            }
        }
        $launcherTools = Join-Path $Bundle 'insight\tools'
        New-Item -ItemType Directory -Path $launcherTools -Force | Out-Null
        foreach ($name in @('setup-windows.ps1', 'windows-toolchain.json', 'select-windows-folders.ps1', 'windows-offline.ps1')) {
            Copy-Item -LiteralPath (Join-Path $PSScriptRoot $name) -Destination $launcherTools
        }
        Copy-Item -LiteralPath (Join-Path $PSScriptRoot '..\..\build.cmd') -Destination $Bundle
        $metadata = [ordered]@{
            format = 1
            platform = 'windows-x64'
            clangProvider = $ClangProvider
            packages = $packages
            files = $files
        }
        $json = $metadata | ConvertTo-Json -Depth 5
        [IO.File]::WriteAllText((Join-Path $Bundle 'bundle.json'), $json, (New-Object Text.UTF8Encoding($false)))
        if (Test-Path -LiteralPath $destination) {
            Remove-Item -LiteralPath $destination -Force
        }
        Move-Item -LiteralPath $Bundle -Destination $destination
        Write-Host "[Bundle ready] $destination"
        Write-Host 'Copy the entire directory. On the destination PC run: build.cmd -UseBundle .'
    } finally {
        if (Test-Path -LiteralPath $Bundle) {
            Remove-Item -LiteralPath $Bundle -Recurse -Force
        }
    }
}
