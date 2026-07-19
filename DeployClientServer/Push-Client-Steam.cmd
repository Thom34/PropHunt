@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Push-Client-Steam.ps1" %*
exit /b %errorlevel%
