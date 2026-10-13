$ErrorActionPreference = 'Stop'
$env:PATH = (Join-Path $PSScriptRoot 'bin') + ';' + $env:PATH
$profile = Join-Path $PSScriptRoot 'profile'
New-Item -ItemType Directory -Force -Path $profile | Out-Null
& (Join-Path $PSScriptRoot 'bin\geany.exe') '--new-instance' '--config' $profile @args
