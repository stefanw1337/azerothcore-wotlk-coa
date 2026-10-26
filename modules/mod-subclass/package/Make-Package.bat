@echo off
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0Make-Package.ps1" %*
pause
