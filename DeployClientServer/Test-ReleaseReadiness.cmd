@echo off
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-ReleaseReadiness.ps1" %*
exit /b %errorlevel%
