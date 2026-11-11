@echo off
setlocal
rem Builds the 64-bit client from ilusixn's CustomRacesCoa (branch races-port) with the Skinshifter patches
rem (coa64-patches\*.patch next to this file) and copies it into your client folder as Ascension64.exe.
rem Needs Git and Visual Studio 2022 with "Desktop development with C++". The first run downloads about 1 GB.

rem Your client folder (the one with Ascension32.exe / Ascension64.exe and Data\).
set "CLIENT=C:\Path\To\CoA Client"
rem Where the client source is kept (any empty folder; it is reset to races-port on every run).
set "SRC=%~dp0CoA64-src"

set "PATCHES=%~dp0coa64-patches"
set "BUILD=%SRC%\out\build\x64-Release"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"

if not exist "%CLIENT%" (
    echo Set CLIENT at the top of this file to your client folder first.
    pause
    exit /b 1
)

if exist "%SRC%\.git" (
    echo Updating the source...
    git -C "%SRC%" fetch --depth 1 origin races-port || goto :fail
    git -C "%SRC%" reset --hard FETCH_HEAD || goto :fail
) else (
    echo Downloading the source, about 1 GB...
    git clone --depth 1 --branch races-port https://github.com/ilusixn/CustomRacesCoa "%SRC%" || goto :fail
)
for /f "usebackq delims=" %%i in (`git -C "%SRC%" rev-parse --short^=8 HEAD`) do set "HASH=%%i"
for %%P in ("%PATCHES%\*.patch") do (
    git -C "%SRC%" apply --reverse --check "%%~fP" >nul 2>&1 && (
        echo Already in races-port: %%~nxP
    ) || (
        echo Applying %%~nxP
        git -C "%SRC%" apply --whitespace=nowarn "%%~fP" || goto :fail
    )
)
echo Source: races-port %HASH%

set "XVID=%SRC%\ThirdParty\xvidcore\lib\x64\libxvidcore.lib"
if not exist "%XVID%" (
    mkdir "%SRC%\ThirdParty\xvidcore\lib\x64" 2>nul
    copy /y "%SRC%\races-extras\sources\esteria\wowdecomp\ThirdParty\xvidcore\lib\x64\libxvidcore.lib" "%XVID%" >nul || goto :fail
)

if not exist "%VSWHERE%" goto :novs
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -version [17.0^,18.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto :novs
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || goto :novs

if not exist "%BUILD%\build.ninja" (
    cmake -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo -S "%SRC%" -B "%BUILD%" || goto :fail
)
cmake --build "%BUILD%" --target Wow XvidDecoder || goto :fail
findstr /b /c:"build dxvk:" "%BUILD%\build.ninja" >nul && cmake --build "%BUILD%" --target dxvk

if not exist "%SRC%\bin\Win\CoA64.exe" goto :missing
if exist "%CLIENT%\Ascension64.exe" if not exist "%CLIENT%\Ascension64.exe.before-skinshifter" (
    copy /y "%CLIENT%\Ascension64.exe" "%CLIENT%\Ascension64.exe.before-skinshifter" >nul
    echo Your previous Ascension64.exe is kept as Ascension64.exe.before-skinshifter
)
copy /y "%SRC%\bin\Win\CoA64.exe" "%CLIENT%\Ascension64.exe" >nul || goto :fail
copy /y "%SRC%\bin\Win\XvidDecoder.dll" "%CLIENT%\XvidDecoder.dll" >nul || goto :fail
if exist "%SRC%\bin\Win\x64\d3d9.dll" (
    if not exist "%CLIENT%\x64" mkdir "%CLIENT%\x64"
    copy /y "%SRC%\bin\Win\x64\d3d9.dll" "%CLIENT%\x64\d3d9.dll" >nul
)
echo.
echo Done: %CLIENT%\Ascension64.exe (races-port %HASH% with the Skinshifter patches)
pause
exit /b 0

:novs
echo ERROR: Visual Studio 2022 with the C++ x64 tools was not found.
pause
exit /b 1
:missing
echo ERROR: the build finished without bin\Win\CoA64.exe.
pause
exit /b 1
:fail
echo ERROR: see the lines above.
pause
exit /b 1
