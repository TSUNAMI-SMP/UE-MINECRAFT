@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Get-Minecraft-Code.ps1" %*
set "CODE_RESULT=%ERRORLEVEL%"
echo.
if not "%CODE_RESULT%"=="0" echo Code acquisition failed. Read the error above and the .work log.
pause
exit /b %CODE_RESULT%
