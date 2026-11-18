@echo off
setlocal EnableExtensions
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1"
if %errorlevel% equ 0 (
    echo.
    echo Done. Right-click a file or folder and choose "Classic Menu".
    echo If it does not appear, restart Explorer.
    echo.
    pause
    exit /b 0
)

echo.
echo Direct install failed (error %errorlevel%). Trying with administrator rights...
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0register.ps1"
echo.
pause
