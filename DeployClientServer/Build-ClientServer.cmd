@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Build-ClientServer.ps1" %*
exit /b %errorlevel%
