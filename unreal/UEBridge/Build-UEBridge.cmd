@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-UEBridge.ps1" %*
set "BRIDGE_BUILD_EXIT=%ERRORLEVEL%"
pause
exit /b %BRIDGE_BUILD_EXIT%
