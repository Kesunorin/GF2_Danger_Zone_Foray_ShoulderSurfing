@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Start-GameAndMod.ps1" -WaitOnFailure
exit /b %errorlevel%
