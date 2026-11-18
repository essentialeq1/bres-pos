@echo off
setlocal EnableExtensions
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0unregister.ps1"
if %errorlevel% equ 0 (
    echo.
    echo Uninstalled.
    echo.
    pause
    exit /b 0
)

echo.
echo Direct uninstall failed. Trying with administrator rights...
net session >nul 2>&1
if %errorlevel% neq 0 (
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b 0
)

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0unregister.ps1"
echo.
pause
