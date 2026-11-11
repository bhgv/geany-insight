@echo off
setlocal
rem EN: Forward -DownloadBundle PATH / -UseBundle PATH to the bootstrap.
rem RU: Передаёт флаги подготовки и использования переносимого набора в сборщик.
set "insight_setup=%~dp0insight\tools\setup-windows.ps1"
if "%~1"=="" goto menu
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%insight_setup%" -UseCheckout %*
exit /b %errorlevel%

:menu
echo(
echo Geany Insight - Windows x64 build
echo   1. Build and install ^(choose folders^)
echo   2. Preview the default build plan ^(no changes^)
echo   3. Show command-line help
echo   4. Exit
choice.exe /C 1234 /N /M "Select [1-4]: "
if errorlevel 4 exit /b 0
if errorlevel 3 goto help
if errorlevel 2 goto plan
if errorlevel 1 goto build
exit /b 0

:build
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%insight_setup%" -UseCheckout -BuildEditor
set "insight_exit=%errorlevel%"
if not "%insight_exit%"=="0" (
    echo Build failed with exit code %insight_exit%. See the messages above.
    pause
)
exit /b %insight_exit%

:plan
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%insight_setup%" -UseCheckout -BuildEditor -Plan
exit /b %errorlevel%

:help
powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -File "%insight_setup%" -Help
exit /b %errorlevel%
