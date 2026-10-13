@echo off
setlocal EnableExtensions DisableDelayedExpansion
cd /d "%~dp0"
set "PATH=%~dp0bin;%PATH%"
if not exist "%~dp0profile" mkdir "%~dp0profile"
set "GDK_PIXBUF_MODULEDIR=%~dp0lib\gdk-pixbuf-2.0\2.10.0\loaders"
set "GDK_PIXBUF_MODULE_FILE=%~dp0profile\pixbuf-loaders.cache"
if exist "%~dp0bin\gdk-pixbuf-query-loaders.exe" "%~dp0bin\gdk-pixbuf-query-loaders.exe" > "%GDK_PIXBUF_MODULE_FILE%"
"%~dp0bin\geany.exe" --new-instance --config "%~dp0profile" %*
set "INSIGHT_RESULT=%ERRORLEVEL%"
if not "%INSIGHT_RESULT%"=="0" (
 echo Geany Insight exited with code %INSIGHT_RESULT%.
 pause
)
exit /b %INSIGHT_RESULT%
