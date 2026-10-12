$ErrorActionPreference = 'Stop'
$profile = Join-Path $PSScriptRoot 'profile'
New-Item -ItemType Directory -Force -Path $profile | Out-Null
& (Join-Path $PSScriptRoot 'bin\geany.exe') '--new-instance' '--no-plugins' '--config' $profile @args
