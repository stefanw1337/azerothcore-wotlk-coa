@echo off
rem Installs the subclass test package on this CoA Manager repack.
cd /d "%~dp0"
"..\Runtime\python\python.exe" -B install.py %*
pause
