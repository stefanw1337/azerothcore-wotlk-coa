@echo off
setlocal
rem Heroes of the Storm mounts for WoW 3.3.5a, built from your own Heroes of the Storm install.
rem Edit the settings below, then run this file. See README.md for what each step does.

rem --- settings -------------------------------------------------------------------------------------------------
rem Your Heroes of the Storm folder (the one with .build.info in it).
set "HOTS=C:\Program Files (x86)\Heroes of the Storm"
rem CascLib.dll (64-bit) from https://github.com/ladislav-zezula/CascLib (see README.md).
set "CASCLIB=%~dp0CascLib.dll"
rem Work folder: extracted HotS files and the converted mounts end up here (about 2 GB).
set "WORK=%~dp0work"
rem tables = the mount rows go into the client patch: works with azerothcore-wotlk-coa, its forks and plain
rem AzerothCore 3.3.5a. stream = only for a CoA server that streams new rows to the client itself.
set "MODE=tables"
rem Your WoW 3.3.5a client's Data folder (CoA client or a plain 3.3.5a client).
set "CLIENT=C:\WoW 3.3.5a\Data"
rem The worldserver.conf your server uses (the installer reads WorldDatabaseInfo from it).
set "WORLDCONF=C:\AzerothCore\configs\worldserver.conf"
rem The server's dbc folder (the one with Spell.dbc in it).
set "SERVERDBC=C:\AzerothCore\data\dbc"
rem mysql.exe; leave empty when mysql is on PATH.
set "MYSQL="
rem --------------------------------------------------------------------------------------------------------------

set "PY="
where py >nul 2>&1 && set "PY=py -3"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY (echo Python 3 not found. Install it from python.org and run this again. & pause & exit /b 1)
set "MYSQLARG="
if defined MYSQL set MYSQLARG=--mysql "%MYSQL%"

echo === [1/4] Python packages (numpy, Pillow) ===
%PY% -m pip install --quiet --user numpy Pillow
if errorlevel 1 goto failed
echo.
if exist "%WORK%\catalog.json" if exist "%WORK%\extract-2" (
    echo === [2/4] HotS files: already extracted in %WORK% ^(delete that folder to extract again^) ===
    goto build
)
echo === [2/4] HotS files: mount models, data, names and textures ===
%PY% -B "%~dp0tools\hots_mounts.py" extract --hots "%HOTS%" --casclib "%CASCLIB%" --work "%WORK%"
if errorlevel 1 goto failed
:build
echo.
echo === [3/4] Converting every mount and packing the patch ===
%PY% -B "%~dp0tools\hots_mounts.py" build --work "%WORK%"
if errorlevel 1 goto failed
echo.
echo === [4/4] Installing: client patch, addons and server rows (mode %MODE%) ===
for %%E in (Wow.exe Ascension.exe Ascension64.exe CoA64.exe) do (
    tasklist /fi "imagename eq %%E" | find /i "%%E" >nul && (echo Close the game first. & pause & exit /b 1)
)
%PY% -B "%~dp0tools\hots_mounts.py" install --work "%WORK%" --mode %MODE% --client "%CLIENT%" --world-conf "%WORLDCONF%" --server-dbc "%SERVERDBC%" %MYSQLARG%
if errorlevel 1 goto failed
echo.
echo All done. Restart the worldserver, then start the game. /cj opens the mount journal.
pause
exit /b 0
:failed
echo.
echo That step failed - see the messages above.
pause
exit /b 1
