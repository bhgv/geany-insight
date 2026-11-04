@echo off
setlocal
if "%~1"=="" goto help
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0insight\tools\setup-windows.ps1" -UseCheckout %*
exit /b %errorlevel%
:help
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0insight\tools\setup-windows.ps1" -Help
exit /b %errorlevel%
