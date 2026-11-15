@echo off
rem Builds the Two-Players male Two-Headed Ogre from your Heroes of the Storm install into your game's Data folder.
cd /d "%~dp0"
if not exist "..\..\..\Runtime\python\python.exe" (
    echo The repack's Python was not found at:
    echo   %~dp0..\..\..\Runtime\python\python.exe
    echo.
    echo Run this from the installed package: CoA-Subclass\files\ogre\Build-Ogre.bat inside the repack folder.
    pause
    exit /b 1
)
"..\..\..\Runtime\python\python.exe" -B build_ogre.py %*
pause
