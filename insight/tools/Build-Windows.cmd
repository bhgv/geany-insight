@echo off
setlocal
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%~dp0setup-windows.ps1" -BuildEditor %*
set "insight_exit=%errorlevel%"
if not "%insight_exit%"=="0" pause
exit /b %insight_exit%
