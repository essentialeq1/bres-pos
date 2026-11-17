@echo off
setlocal
rem Runs the headless diagnostics and shows the report (and the app log).
"%~dp0LegacyMenuBridge.exe" --selftest
echo.
if exist "%LOCALAPPDATA%\LegacyMenuBridge\selftest.log" (
  type "%LOCALAPPDATA%\LegacyMenuBridge\selftest.log"
) else (
  echo selftest.log was not produced - did LegacyMenuBridge.exe run?
)
echo.
echo Full app log: %LOCALAPPDATA%\LegacyMenuBridge\log.txt
echo.
pause
