# SPDX-License-Identifier: GPL-2.0-or-later
#requires -Version 5.1
# Run with Windows PowerShell 5.1 or PowerShell 7 on Windows x64.
[CmdletBinding()]
param(
    [string]$RootDir = '',
    [string]$MsysRoot = '',
    [string]$CacheDir = '',
    [ValidateSet('Internal', 'System', 'Disabled')][string]$ClangProvider = 'Internal',
    [switch]$Autotools,
    [switch]$Packaging,
    [switch]$FetchSources,
    [string]$SourceDir = '',
    [string]$InstallDir = '',
    [switch]$NonInteractive,
    [switch]$BuildEditor,
    [switch]$UseCheckout,
    [switch]$Plan,
    [switch]$Help
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
# Windows PowerShell 5.1 progress rendering can severely slow binary downloads.
$ProgressPreference = 'SilentlyContinue'
$manifestPath = Join-Path $PSScriptRoot 'windows-toolchain.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($Help) {
    @'
Geany Insight Windows x64 toolchain bootstrap
  .\setup-windows.ps1 [-MsysRoot C:\msys64] [-ClangProvider Internal|System|Disabled]
    [-FetchSources] [-SourceDir C:\src\geany-insight] [-Autotools] [-Packaging]
    [-RootDir D:\Development\Insight] [-InstallDir PATH] [-CacheDir PATH]
    [-BuildEditor] [-UseCheckout] [-NonInteractive] [-Plan] [-Help]
From a repository checkout: build.cmd -BuildEditor
-UseCheckout defaults SourceDir to the checkout containing this script.
Default: show the location chooser, then install/update MSYS2 UCRT64 and Meson build tools for internal Clang.
-FetchSources also clones the editor and initializes external/llvm-project.
-Plan prints the download/package plan without changing anything.
Close other MSYS2 processes before running. Existing MSYS2 receives a full update.
No Visual Studio, WSL, global PATH or machine execution-policy changes are needed.
'@ | Write-Output
    return
}
if (-not $RootDir) {
    $RootDir = Join-Path $env:LOCALAPPDATA 'GeanyInsight'
    if ($RootDir -notmatch '^[A-Za-z]:\\[A-Za-z0-9_\\.\-]+$') { $RootDir = Join-Path $env:SystemDrive 'Development\GeanyInsight' }
}
if (-not $MsysRoot) { $MsysRoot = Join-Path $RootDir 'tools\msys64' }
if (-not $SourceDir) {
    $SourceDir = if ($UseCheckout) { [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..')) } else { Join-Path $RootDir 'src' }
}
if (-not $InstallDir) { $InstallDir = Join-Path $RootDir 'editor' }
$defaultCache = -not $CacheDir
if ($defaultCache) { $CacheDir = Join-Path $RootDir 'downloads' }
if ($BuildEditor) { $FetchSources = $true }
$packages = @($manifest.packages.common)
if ($Autotools) { $packages += @($manifest.packages.autotools) }
if ($Packaging) { $packages += @($manifest.packages.packaging) }
if ($ClangProvider -eq 'System') { $packages += @($manifest.packages.systemClang) }
if ($Plan) {
    [ordered]@{ msysRoot=$MsysRoot; installer=$manifest.installer; packages=$packages;
        clangProvider=$ClangProvider; fetchSources=[bool]$FetchSources; sourceDir=$SourceDir; rootDir=$RootDir; installDir=$InstallDir; cacheDir=$CacheDir; buildEditor=[bool]$BuildEditor; nonInteractive=[bool]$NonInteractive } |
        ConvertTo-Json -Depth 6
    return
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
    -not [Environment]::Is64BitOperatingSystem) { throw 'Windows x64 is required.' }
$locationHelper = Join-Path $PSScriptRoot 'select-windows-folders.ps1'
. $locationHelper
$locations = @{RootDir=$RootDir; MsysRoot=$MsysRoot; SourceDir=$SourceDir; InstallDir=$InstallDir}
if (-not $NonInteractive) { $locations = Select-InsightLocations $locations }
$locations = Test-InsightLocations $locations
$RootDir=$locations.RootDir; $MsysRoot=$locations.MsysRoot
$SourceDir=$locations.SourceDir; $InstallDir=$locations.InstallDir
if ($defaultCache) { $CacheDir=Join-Path $RootDir 'downloads' }
# Keep installation paths compatible with the MSYS2 installer and shell launchers.
if ($MsysRoot -notmatch '^[A-Za-z]:\\[A-Za-z0-9_\\.\-]+$') {
    throw 'MsysRoot must be an absolute drive path without spaces, e.g. C:\msys64.'
}
$MsysRoot = [IO.Path]::GetFullPath($MsysRoot).TrimEnd('\')
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
$envNames = @('MSYSTEM','CHERE_INVOKING','INSIGHT_BOOTSTRAP_PACKAGES','INSIGHT_BOOTSTRAP_SOURCE','INSIGHT_BOOTSTRAP_FETCH','INSIGHT_BOOTSTRAP_INTERNAL','INSIGHT_BOOTSTRAP_INSTALL','INSIGHT_BOOTSTRAP_PROVIDER')
$previous = @{}
foreach ($name in $envNames) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Wait-InsightProcess([Diagnostics.Process]$Child, [string]$Stage) {
    $clock = [Diagnostics.Stopwatch]::StartNew()
    while (-not $Child.WaitForExit(20000)) {
        Write-Host ('[Running] {0} - elapsed {1:hh\:mm\:ss}' -f $Stage, $clock.Elapsed)
    }
    return $Child.ExitCode
}

function Invoke-Msys([string]$Script, [string]$Stage, [switch]$AllowCoreRestart) {
    # A UTF-8/LF file avoids native argument quoting and CRLF stdin differences
    # between Windows PowerShell 5.1 and PowerShell 7.
    $scriptFile = [IO.Path]::GetTempFileName()
    try {
        [IO.File]::WriteAllText($scriptFile, ($Script.Replace("`r", '') + "`n"), (New-Object Text.UTF8Encoding($false)))
        & $bash --login ($scriptFile -replace '\\','/') | Out-Host
        $code = $LASTEXITCODE
    } finally {
        Remove-Item -LiteralPath $scriptFile -Force
    }
    if ($code -ne 0 -and -not $AllowCoreRestart) { throw "${Stage}: MSYS2 exited with code $code. See the output above." }
    if ($code -ne 0) { Write-Host "[Resume] $Stage exited with code $code; continuing in a fresh MSYS2 shell." }
    return $code
}
function Receive-InsightInstaller([string]$Url, [string]$Destination) {
    $response = $null
    $inputStream = $null
    $outputStream = $null
    try {
        $request = [Net.WebRequest]::Create($Url)
        $request.Timeout = 60000
        $request.ReadWriteTimeout = 60000
        $response = $request.GetResponse()
        $length = $response.ContentLength
        $inputStream = $response.GetResponseStream()
        $outputStream = [IO.File]::Create($Destination)
        $buffer = New-Object byte[] 65536
        $received = 0L
        $clock = [Diagnostics.Stopwatch]::StartNew()
        $nextReport = 0L
        while (($count = $inputStream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            $outputStream.Write($buffer, 0, $count)
            $received += $count
            if ($clock.ElapsedMilliseconds -ge $nextReport) {
                if ($length -gt 0) {
                    $percent = [Math]::Min(99, [Math]::Floor(100.0 * $received / $length))
                    Write-Host ('  {0}% ({1:N1}/{2:N1} MiB)' -f $percent, ($received / 1MB), ($length / 1MB))
                } else {
                    Write-Host ('  {0:N1} MiB received' -f ($received / 1MB))
                }
                $nextReport = $clock.ElapsedMilliseconds + 1000
            }
        }
        if ($length -ge 0 -and $received -ne $length) { throw "Incomplete installer download: $received of $length bytes." }
        $completion = if ($length -gt 0) { '100% downloaded' } else { 'Download complete' }
        Write-Host ('  {0}: {1:N1} MiB. Verifying SHA256...' -f $completion, ($received / 1MB))
    } finally {
        if ($outputStream) { $outputStream.Dispose() }
        if ($inputStream) { $inputStream.Dispose() }
        if ($response) { $response.Dispose() }
    }
}
try {
    if (-not (Test-Path -LiteralPath $bash)) {
        if ((Test-Path -LiteralPath $MsysRoot) -and @(Get-ChildItem -LiteralPath $MsysRoot -Force).Count) {
            throw 'MSYS2 directory is nonempty but has no bash.exe; choose a new MsysRoot.'
        }
        New-Item -ItemType Directory -Path $CacheDir -Force | Out-Null
        $installer = Join-Path $CacheDir $manifest.installer.file
        [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
        if (-not (Test-Path -LiteralPath $installer)) {
            $partial = "$installer.partial"
            Write-Host "[Download] $($manifest.installer.file) from $($manifest.installer.url)"
            try {
                Receive-InsightInstaller $manifest.installer.url $partial
                Move-Item -LiteralPath $partial -Destination $installer -Force
            } finally {
                if (Test-Path -LiteralPath $partial) { Remove-Item -LiteralPath $partial -Force }
            }
        } else {
            Write-Host "[Cache] Verifying $installer"
        }
        $hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $manifest.installer.sha256) { throw "Installer SHA256 mismatch. Remove $installer and retry." }
        Write-Host '[Install] MSYS2 (the installer reports its own progress)...'
        $process = Start-Process -FilePath $installer -ArgumentList @('in','--confirm-command','--accept-messages','--root',($MsysRoot -replace '\\','/')) -PassThru
        $code = Wait-InsightProcess $process 'MSYS2 installer'
        if ($code -ne 0) { throw "MSYS2 installer failed ($code)." }
        if (-not (Test-Path -LiteralPath $bash)) { throw 'Installer did not create usr\bin\bash.exe.' }
    }
    $env:MSYSTEM = 'UCRT64'
    $env:CHERE_INVOKING = '1'
    $env:INSIGHT_BOOTSTRAP_PACKAGES = $packages -join ' '
    # Core updates may terminate their shell; always resume in a new login shell.
    # Only this first exit is tolerated; the second full upgrade must succeed.
    Write-Host '[Update] MSYS2 (close all other MSYS2 terminals first)...'
    $null = Invoke-Msys 'pacman -Syu --noconfirm' -Stage 'MSYS2 core update' -AllowCoreRestart
    $null = Invoke-Msys 'set -e; pacman -Syu --noconfirm' -Stage 'MSYS2 full update'
    Write-Host '[Packages] Downloading and installing UCRT64 build tools...'
    $null = Invoke-Msys -Stage 'UCRT64 packages' -Script @'
set -euo pipefail
read -r -a packages <<< "$INSIGHT_BOOTSTRAP_PACKAGES"
pacman -S --needed --noconfirm "${packages[@]}"
for tool in gcc g++ pkg-config meson ninja cmake python3 git; do
    command -v "$tool"
done
pkg-config --atleast-version=3.22 gtk+-3.0
pkg-config --atleast-version=2.56 glib-2.0
gcc --version | head -n 1
meson --version
cmake --version
pacman -Q "${packages[@]}"
'@
    if ($FetchSources) {
        Write-Host "[Sources] Geany Insight and compiler sources: $SourceDir"
        $env:INSIGHT_BOOTSTRAP_SOURCE = [IO.Path]::GetFullPath($SourceDir)
        $env:INSIGHT_BOOTSTRAP_FETCH = $manifest.repository
        $env:INSIGHT_BOOTSTRAP_INTERNAL = if ($ClangProvider -eq 'Internal') { '1' } else { '0' }
        $null = Invoke-Msys -Stage 'Git source checkout' -Script @'
set -euo pipefail
source_dir=$(cygpath -u "$INSIGHT_BOOTSTRAP_SOURCE")
if [ -d "$source_dir/.git" ]; then
    test "$(git -C "$source_dir" remote get-url origin)" = "$INSIGHT_BOOTSTRAP_FETCH" || { echo 'Different origin: refusing to modify this checkout.' >&2; exit 1; }
else
    git clone --progress --depth 1 "$INSIGHT_BOOTSTRAP_FETCH" "$source_dir"
fi
if [ "$INSIGHT_BOOTSTRAP_INTERNAL" = 1 ] && [ ! -f "$source_dir/external/llvm-project/llvm/CMakeLists.txt" ]; then
    printf '%s\n' '[Sources] Downloading internal LLVM/Clang'
    git -C "$source_dir" submodule update --progress --init --depth 1 -- external/llvm-project
fi
# Never pull/reset an existing checkout or overwrite initialized Clang sources.
git -C "$source_dir" rev-parse HEAD
'@
    }
    if ($BuildEditor) {
        Write-Host "[Build] Clang provider: $ClangProvider; building slang and Geany Insight for $InstallDir"
        $env:INSIGHT_BOOTSTRAP_INSTALL=$InstallDir
        $env:INSIGHT_BOOTSTRAP_PROVIDER=$ClangProvider.ToLowerInvariant()
        $null = Invoke-Msys -Stage 'Clang, slang and editor build' -Script @'
set -euo pipefail
cd "$(cygpath -u "$INSIGHT_BOOTSTRAP_SOURCE")"
export CC=gcc CXX=g++
export NINJA_STATUS='[ %f/%t %p ] '
python3 insight/tools/build_editor.py --backend meson --clang-provider "$INSIGHT_BOOTSTRAP_PROVIDER" --prefix "$(cygpath -m "$INSIGHT_BOOTSTRAP_INSTALL")" --install -- --libdir=lib -Dhtml-docs=disabled -Dpdf-docs=disabled -Dapi-docs=disabled -Dgtkdoc=false -Dvte=false -Dmac-integration=disabled
'@
    }
    Write-Host "Editor sources: $SourceDir"
    Write-Host "Editor installation: $InstallDir"
    Write-Host "Ready. Open $MsysRoot\ucrt64.exe and follow windows-build.md (EN/RU)."
    if (-not $BuildEditor) { Write-Host 'Tools ready; use -BuildEditor to compile and install the editor, Clang and slang.' }
} finally {
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
