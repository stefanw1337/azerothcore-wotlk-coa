@echo off
setlocal
rem Builds the Witch Hunter Vault for the original races from this client's own models.
rem Run it from a folder next to Data (for example <client>\merge-tools), with the game closed.
set "HERE=%~dp0"
set "DATA=%~1"
if "%DATA%"=="" set "DATA=%HERE%..\Data"
set "PY="
if not defined PY where py >nul 2>&1 && set "PY=py -3"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY if exist "L:\CoA Server-Ogre-edits\Runtime\python\python.exe" set "PY="L:\CoA Server-Ogre-edits\Runtime\python\python.exe""
if not defined PY (
    echo Python 3 was not found. Install it from python.org and run this again.
    pause
    exit /b 1
)
%PY% -B "%HERE%build_vault_patches.py" "%DATA%" --backup "%HERE%."
if errorlevel 1 (
    echo ERROR: see the lines above. patch-ZZZZY.MPQ was not replaced.
    pause
    exit /b 1
)
pause
