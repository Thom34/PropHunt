@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Push-Coordinated-Vps.ps1" %*
exit /b %errorlevel%
