@echo off
setlocal EnableExtensions DisableDelayedExpansion
set "INSIGHT_ROOT=%~dp0"
set "INSIGHT_LOG=%INSIGHT_ROOT%startup-diagnostic.log"
cd /d "%INSIGHT_ROOT%"
set "PATH=%INSIGHT_ROOT%bin;%PATH%"
> "%INSIGHT_LOG%" echo Geany Insight Windows startup diagnostic
if errorlevel 1 (
 echo Cannot write startup-diagnostic.log. Extract the ZIP to a writable folder.
 pause
 exit /b 1
)
ver >> "%INSIGHT_LOG%" 2>&1
if not exist "%INSIGHT_ROOT%bin\geany.exe" goto missing
if not exist "%INSIGHT_ROOT%bin\libgeany-0.dll" goto missing
if not exist "%INSIGHT_ROOT%data\geany.glade" goto missing
>> "%INSIGHT_LOG%" echo [1] Loading editor and runtime with --version
"%INSIGHT_ROOT%bin\geany.exe" --version >> "%INSIGHT_LOG%" 2>&1
set "INSIGHT_RESULT=%ERRORLEVEL%"
>> "%INSIGHT_LOG%" echo Version exit code: %INSIGHT_RESULT%
if not "%INSIGHT_RESULT%"=="0" goto done
if not exist "%INSIGHT_ROOT%profile" mkdir "%INSIGHT_ROOT%profile" >> "%INSIGHT_LOG%" 2>&1
if not exist "%INSIGHT_ROOT%profile" goto missing
set "GDK_PIXBUF_MODULEDIR=%INSIGHT_ROOT%lib\gdk-pixbuf-2.0\2.10.0\loaders"
set "GDK_PIXBUF_MODULE_FILE=%INSIGHT_ROOT%profile\pixbuf-loaders.cache"
if exist "%INSIGHT_ROOT%bin\gdk-pixbuf-query-loaders.exe" "%INSIGHT_ROOT%bin\gdk-pixbuf-query-loaders.exe" > "%GDK_PIXBUF_MODULE_FILE%" 2>> "%INSIGHT_LOG%"
>> "%INSIGHT_LOG%" echo [2] Starting GUI directly, without PowerShell
"%INSIGHT_ROOT%bin\geany.exe" --new-instance --verbose --config "%INSIGHT_ROOT%profile" >> "%INSIGHT_LOG%" 2>&1
set "INSIGHT_RESULT=%ERRORLEVEL%"
>> "%INSIGHT_LOG%" echo GUI exit code: %INSIGHT_RESULT%
goto done
:missing
>> "%INSIGHT_LOG%" echo Required EXE, DLL, data or writable profile is missing. Extract the complete ZIP first.
set "INSIGHT_RESULT=1"
:done
type "%INSIGHT_LOG%"
echo.
echo Please send startup-diagnostic.log from this folder.
pause
exit /b %INSIGHT_RESULT%
