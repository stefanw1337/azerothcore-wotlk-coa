@echo off
rem Installs the subclass package on this repack (CoA Custom must be installed first).
cd /d "%~dp0"
if not exist "..\Runtime\python\python.exe" (
    echo The repack's Python was not found at:
    echo   %~dp0..\Runtime\python\python.exe
    echo.
    echo Extract the zip into the repack folder, so that CoA-Subclass sits next to CoA-Custom, Scripts and Runtime,
    echo then run CoA-Subclass\Install-Subclass.bat from there. Running it from inside the zip does not work.
    pause
    exit /b 1
)
"..\Runtime\python\python.exe" -B install.py %*
pause
