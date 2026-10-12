$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$profile = Join-Path $root 'profile'
New-Item -ItemType Directory -Force -Path $profile | Out-Null
$config = Join-Path $profile 'geany.conf'
if (-not (Test-Path -LiteralPath $config)) {
    $plugin = (Join-Path $root 'lib\geany\insight_lua.dll').Replace('\', '/')
    $plugin = $plugin.Replace(';', '\;')
    $text = "[plugins]`nload_plugins=true`nactive_plugins=$plugin;`n"
    [System.IO.File]::WriteAllText($config, $text, (New-Object System.Text.UTF8Encoding($false)))
}
# Existing profiles are kept intact. After moving the bundle, enable Insight
# again in Tools > Plugin Manager if its previous absolute path is stale.
& (Join-Path $root 'bin\geany.exe') '--new-instance' '--config' $profile @args
