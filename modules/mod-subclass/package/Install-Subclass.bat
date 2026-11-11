@echo off
rem Installs the subclass package on this repack (CoA Custom must be installed first).
cd /d "%~dp0"
"..\Runtime\python\python.exe" -B install.py %*
pause
