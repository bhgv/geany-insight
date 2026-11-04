# SPDX-License-Identifier: GPL-2.0-or-later
#requires -Version 5.1
# Run with Windows PowerShell 5.1 or PowerShell 7 on Windows x64.
[CmdletBinding()]
param(
    [string]$MsysRoot = 'C:\msys64',
    [string]$CacheDir = (Join-Path $env:TEMP 'geany-insight-toolchain'),
    [ValidateSet('Internal', 'System', 'Disabled')][string]$ClangProvider = 'Internal',
    [switch]$Autotools,
    [switch]$Packaging,
    [switch]$FetchSources,
    [string]$SourceDir = 'C:\src\geany-insight',
    [switch]$Plan,
    [switch]$Help
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$manifestPath = Join-Path $PSScriptRoot 'windows-toolchain.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
if ($Help) {
    @'
Geany Insight Windows x64 toolchain bootstrap
  .\setup-windows.ps1 [-MsysRoot C:\msys64] [-ClangProvider Internal|System|Disabled]
    [-FetchSources] [-SourceDir C:\src\geany-insight] [-Autotools] [-Packaging]
    [-CacheDir PATH] [-Plan] [-Help]
Default: install/update MSYS2 UCRT64 and Meson build tools for internal Clang.
-FetchSources also clones the editor and initializes external/llvm-project.
-Plan prints the download/package plan without changing anything.
Close other MSYS2 processes before running. Existing MSYS2 receives a full update.
No Visual Studio, WSL, global PATH or machine execution-policy changes are needed.
'@ | Write-Output
    return
}
$packages = @($manifest.packages.common)
if ($Autotools) { $packages += @($manifest.packages.autotools) }
if ($Packaging) { $packages += @($manifest.packages.packaging) }
if ($ClangProvider -eq 'System') { $packages += @($manifest.packages.systemClang) }
if ($Plan) {
    [ordered]@{ msysRoot=$MsysRoot; installer=$manifest.installer; packages=$packages;
        clangProvider=$ClangProvider; fetchSources=[bool]$FetchSources; sourceDir=$SourceDir } |
        ConvertTo-Json -Depth 6
    return
}
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT -or
    -not [Environment]::Is64BitOperatingSystem) { throw 'Windows x64 is required.' }
# Keep installation paths compatible with the MSYS2 installer and shell launchers.
if ($MsysRoot -notmatch '^[A-Za-z]:\\[A-Za-z0-9_\\-]+$') {
    throw 'MsysRoot must be an absolute drive path without spaces, e.g. C:\msys64.'
}
$MsysRoot = [IO.Path]::GetFullPath($MsysRoot).TrimEnd('\')
$bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
$envNames = @('MSYSTEM','CHERE_INVOKING','INSIGHT_BOOTSTRAP_PACKAGES','INSIGHT_BOOTSTRAP_SOURCE','INSIGHT_BOOTSTRAP_FETCH','INSIGHT_BOOTSTRAP_INTERNAL')
$previous = @{}
foreach ($name in $envNames) { $previous[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
function Invoke-Msys([string]$Script, [switch]$AllowCoreRestart) {
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
    if ($code -ne 0 -and -not $AllowCoreRestart) { throw "MSYS2 command failed ($code). Rerun after fixing the reported error." }
    return $code
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
            Invoke-WebRequest -UseBasicParsing -Uri $manifest.installer.url -OutFile $partial
            Move-Item -LiteralPath $partial -Destination $installer -Force
        }
        $hash = (Get-FileHash -LiteralPath $installer -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($hash -ne $manifest.installer.sha256) { throw "Installer SHA256 mismatch. Remove $installer and retry." }
        Write-Host 'Installing verified MSYS2 installer...'
        $process = Start-Process -FilePath $installer -ArgumentList @('in','--confirm-command','--accept-messages','--root',($MsysRoot -replace '\\','/')) -Wait -PassThru
        if ($process.ExitCode -ne 0) { throw "MSYS2 installer failed ($($process.ExitCode))." }
        if (-not (Test-Path -LiteralPath $bash)) { throw 'Installer did not create usr\bin\bash.exe.' }
    }
    $env:MSYSTEM = 'UCRT64'
    $env:CHERE_INVOKING = '1'
    $env:INSIGHT_BOOTSTRAP_PACKAGES = $packages -join ' '
    # Core updates may terminate their shell; always resume in a new login shell.
    # Only this first exit is tolerated; the second full upgrade must succeed.
    Write-Host 'Updating MSYS2 (close all other MSYS2 terminals first)...'
    $null = Invoke-Msys 'pacman -Syu --noconfirm' -AllowCoreRestart
    $null = Invoke-Msys 'set -e; pacman -Syu --noconfirm'
    Write-Host 'Downloading and installing build tools...'
    $null = Invoke-Msys @'
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
        $env:INSIGHT_BOOTSTRAP_SOURCE = [IO.Path]::GetFullPath($SourceDir)
        $env:INSIGHT_BOOTSTRAP_FETCH = $manifest.repository
        $env:INSIGHT_BOOTSTRAP_INTERNAL = if ($ClangProvider -eq 'Internal') { '1' } else { '0' }
        $null = Invoke-Msys @'
set -euo pipefail
source_dir=$(cygpath -u "$INSIGHT_BOOTSTRAP_SOURCE")
if [ -d "$source_dir/.git" ]; then
    test "$(git -C "$source_dir" remote get-url origin)" = "$INSIGHT_BOOTSTRAP_FETCH" || { echo 'Different origin: refusing to modify this checkout.' >&2; exit 1; }
else
    git clone --depth 1 "$INSIGHT_BOOTSTRAP_FETCH" "$source_dir"
fi
if [ "$INSIGHT_BOOTSTRAP_INTERNAL" = 1 ] && [ ! -f "$source_dir/external/llvm-project/llvm/CMakeLists.txt" ]; then
    git -C "$source_dir" submodule update --init --depth 1 -- external/llvm-project
fi
# Never pull/reset an existing checkout or overwrite initialized Clang sources.
git -C "$source_dir" rev-parse HEAD
'@
    }
    Write-Host "Ready. Open $MsysRoot\ucrt64.exe and follow windows-build.md (EN/RU)."
    Write-Host 'The editor and internal Clang have not been compiled. Installed package versions were printed above.'
} finally {
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $previous[$name], 'Process') }
}
