@echo off
setlocal
rem Starts patch-ZZZZY.MPQ over from the client's own files: the Vault for the original races, our client table rows
rem (subclass-tables.MPQ) carried onto the client's newest tables, then the mirrored brother. A backup is kept.
tasklist /fi "imagename eq Ascension64.exe" | find /i "Ascension64.exe" >nul && (echo Close the game first. & pause & exit /b 1)
tasklist /fi "imagename eq CoA64.exe" | find /i "CoA64.exe" >nul && (echo Close the game first. & pause & exit /b 1)
pushd "%~dp0"
set "PY="
where py >nul 2>&1 && set "PY=py -3"
if not defined PY where python >nul 2>&1 && set "PY=python"
if not defined PY set "PY="L:\CoA Server-Ogre-edits\Runtime\python\python.exe""
echo === [1/3] Vault patches, started over ===
call "Build-Vault-Patches.bat" --fresh < nul
if errorlevel 1 goto failed
echo === [2/3] Our client table rows (Spell, SkillLineAbility, SkillRaceClassInfo) ===
%PY% -B "%~dp0carrytables.py" "%~dp0..\Data" "%~dp0subclass-tables.MPQ" "%~dp0."
if errorlevel 1 goto failed
echo === [3/3] Mirrored brother ===
call "Build-Mirror-Twin.bat" < nul
if errorlevel 1 goto failed
popd
echo.
echo All rebuilt. Now run L:\WoW-Skinshifter\tools\Build-Skinshifter-Client.bat again (it builds on these tables).
pause
exit /b 0
:failed
popd
echo.
echo That step failed - see the messages above.
pause
exit /b 1
