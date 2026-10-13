$ErrorActionPreference = 'Stop'
$env:PATH = (Join-Path $PSScriptRoot 'bin') + ';' + $env:PATH
$profile = Join-Path $PSScriptRoot 'profile'
New-Item -ItemType Directory -Force -Path $profile | Out-Null
$env:GDK_PIXBUF_MODULEDIR = Join-Path $PSScriptRoot 'lib\gdk-pixbuf-2.0\2.10.0\loaders'
$env:GDK_PIXBUF_MODULE_FILE = Join-Path $profile 'pixbuf-loaders.cache'
$loader = Join-Path $PSScriptRoot 'bin\gdk-pixbuf-query-loaders.exe'
if (Test-Path $loader) {
    $info = New-Object System.Diagnostics.ProcessStartInfo
    $info.FileName = $loader
    $info.UseShellExecute = $false
    $info.RedirectStandardOutput = $true
    $info.StandardOutputEncoding = [System.Text.UTF8Encoding]::new($false)
    $process = [System.Diagnostics.Process]::Start($info)
    $cache = $process.StandardOutput.ReadToEnd()
    $process.WaitForExit()
    [System.IO.File]::WriteAllText($env:GDK_PIXBUF_MODULE_FILE, $cache, [System.Text.UTF8Encoding]::new($false))
    $process.Dispose()
}
& (Join-Path $PSScriptRoot 'bin\geany.exe') '--new-instance' '--config' $profile @args
