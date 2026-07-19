@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Push-Server-Vps.ps1" %*
exit /b %errorlevel%
