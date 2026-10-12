@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
if not exist "%~dp0profile" mkdir "%~dp0profile"
"%~dp0bin\geany.exe" --new-instance --config "%~dp0profile" %*
set "INSIGHT_RESULT=%ERRORLEVEL%"
if not "%INSIGHT_RESULT%"=="0" (
 echo Geany Insight exited with code %INSIGHT_RESULT%.
 pause
)
exit /b %INSIGHT_RESULT%
