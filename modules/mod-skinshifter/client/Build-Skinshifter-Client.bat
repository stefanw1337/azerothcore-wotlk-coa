@echo off
rem Builds patch-ZZZZZZ-SKIN.MPQ (the Skinshifter client patch) into your client's Data folder, from your own
rem client's files and the files the Skinshifter worldserver writes. Close the game before running this.
setlocal

rem Your client's Data folder (the one with the patch-*.MPQ files).
set "CLIENT_DATA=C:\Path\To\CoA Client\Data"

rem Server owners only: the worldserver's DataDir (where it writes skinshifter_client_spells.txt) and your
rem skinshifter.conf. A server-files folder next to this file (the player package) is used instead when present.
set "SERVER_DATA=C:\Path\To\Server\Data"
set "SERVER_CONF=C:\Path\To\Server\configs\modules\skinshifter.conf"

if exist "%~dp0server-files\skinshifter_client_spells.txt" (
    set "SERVER_DATA=%~dp0server-files"
    set "SERVER_CONF=%~dp0server-files\skinshifter.conf"
)
set "TOOLS=%~dp0tools"

set "PY="
where py >nul 2>&1 && set "PY=py -3"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY (
    echo Python 3 was not found. Install it from python.org, then run this again.
    pause & exit /b 1
)
if not exist "%CLIENT_DATA%" (
    echo Open this file in Notepad and set CLIENT_DATA at the top to your client's Data folder.
    pause & exit /b 1
)
if not exist "%SERVER_DATA%\skinshifter_client_spells.txt" (
    echo %SERVER_DATA%\skinshifter_client_spells.txt is missing.
    echo Set SERVER_DATA at the top of this file, and start the worldserver with mod-skinshifter once.
    pause & exit /b 1
)
for %%E in (Ascension64.exe Ascension32.exe Ascension.exe Wow.exe) do (
    tasklist /fi "imagename eq %%E" | find /i "%%E" >nul && (echo Close the game first. & pause & exit /b 1)
)
set "CONF_ARG="
if exist "%SERVER_CONF%" set CONF_ARG=--server-conf "%SERVER_CONF%"

%PY% "%TOOLS%\mkskinclient.py" "%CLIENT_DATA%" "%CLIENT_DATA%\patch-ZZZZZZ-SKIN.MPQ" --server-data "%SERVER_DATA%" %CONF_ARG%
if errorlevel 1 (echo. & echo Building the client patch failed - see the messages above. & pause & exit /b 1)
echo.
echo To remove Skinshifter from the client again: delete %CLIENT_DATA%\patch-ZZZZZZ-SKIN.MPQ
pause
