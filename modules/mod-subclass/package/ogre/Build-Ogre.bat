@echo off
rem Builds the Two-Players male Two-Headed Ogre from your Heroes of the Storm install into your game's Data folder.
cd /d "%~dp0"
"..\..\..\Runtime\python\python.exe" -B build_ogre.py %*
pause
