@echo off
setlocal
rem Gives the Thin Twins brother his mirror-image model: builds it from the client's own Thin Twins model and merges
rem it into patch-ZZZZY.MPQ. Run it from a folder next to Data (for example <client>\merge-tools), with the game closed.
set "HERE=%~dp0"
set "CLIENT=%HERE%.."
set "PY="
where py >nul 2>&1 && set "PY=py -3"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY if exist "L:\CoA Server-Ogre-edits\Runtime\python\python.exe" set "PY="L:\CoA Server-Ogre-edits\Runtime\python\python.exe""
if not defined PY (
    echo Python 3 was not found. Install it from python.org and run this again.
    pause
    exit /b 1
)
%PY% -B "%HERE%mkmirror.py" --client "%CLIENT%\Data" "%CLIENT%\EsteriaAppearanceGeometry.bin" "%HERE%."
if errorlevel 1 (
    echo ERROR: see the lines above. patch-ZZZZY.MPQ was not replaced.
    pause
    exit /b 1
)
pause
