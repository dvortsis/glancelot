@echo off
setlocal
cd /d "%~dp0"
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\test-app.ps1" -NoBuild %*
if errorlevel 1 (
    echo.
    echo Testing could not start. See the error above.
    pause
    exit /b 1
)
echo.
echo Press any key to close this launcher window. The app stays open.
pause >nul
