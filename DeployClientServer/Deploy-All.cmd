@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Deploy-All.ps1" %*
exit /b %errorlevel%
