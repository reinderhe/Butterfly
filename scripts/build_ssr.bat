@echo off
setlocal EnableExtensions
rem Builds Butterfly (Nintendo Switch Sports Resort) on Longshot's code into E:\Butterfly\game-longshot.
rem Its own folders (tools-longshot, game-longshot, work-longshot), so the Hookshot-based build in E:\Butterfly\game stays
rem as it is. The compilers come from E:\Butterfly\tools (made by the Hookshot-based build_ssr.bat) or Hookshot Builder;
rem the newer ones Longshot builds with are downloaded once (scripts\fetch_tools.py, checked against tools.lock).

rem ---- your settings -------------------------------------------------------------------------------------
set "NUP=E:\Switch 2 roms\NintendoSwitchSportsResort_v1.1.0.nup"
set "BASE=E:\Butterfly"
set "BRANCH=ssr-longshot"
rem ----------------------------------------------------------------------------------------------------------

set "TOOLS=%BASE%\tools-longshot"
set "GAME=%BASE%\game-longshot"
set "LOG=%BASE%\build-longshot.log"
set "WORK=%BASE%\work-longshot"

if not exist "%NUP%" (
    echo Can't find your game file: %NUP%
    echo Open this .bat in Notepad and set NUP to the full path of your Sports Resort .nup.
    pause & exit /b 1
)
if not exist "%BASE%" mkdir "%BASE%"

rem 1. The compilers and libraries (only their folders: no Hookshot code comes along), copied once
if not exist "%TOOLS%\python\python.exe" (
    set "SRC="
    if exist "%BASE%\tools\python\python.exe" set "SRC=%BASE%\tools"
    if not defined SRC for /d %%V in ("%LOCALAPPDATA%\HookshotBuilder\versions\*") do (
        if exist "%%V\HookshotBuilder\tools\python\python.exe" set "SRC=%%V\HookshotBuilder\tools"
    )
    call :copy_tools
    if errorlevel 1 (pause & exit /b 1)
)

rem 2. Butterfly-on-Longshot's code, fresh from GitHub each time
echo Downloading Butterfly (%BRANCH%)...
set "ZIP=%BASE%\butterfly-longshot.zip"
set "SRCDIR=%BASE%\src-longshot"
if exist "%SRCDIR%" rmdir /s /q "%SRCDIR%"
mkdir "%SRCDIR%"
powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing 'https://github.com/reinderhe/Butterfly/archive/refs/heads/%BRANCH%.zip' -OutFile '%ZIP%'"
if errorlevel 1 (echo The download failed. & pause & exit /b 1)
tar -xf "%ZIP%" -C "%SRCDIR%"
if errorlevel 1 (echo Unzipping failed. & pause & exit /b 1)
set "REPO="
for /d %%D in ("%SRCDIR%\*") do set "REPO=%%D"
rem (each folder of the code mirrored, so a file the code no longer has does not stay behind and get built; but
rem third_party only added to, as it also holds the tools' ready-made libraries the code doesn't have (SDL2.dll, SDL3.dll),
rem with its a64-kit, all code, mirrored)
for /d %%D in ("%REPO%\*") do (
    if /i "%%~nxD"=="third_party" (
        robocopy "%%D" "%TOOLS%\third_party" /E /NFL /NDL /NJH /NJS /NP >nul
        robocopy "%%D\a64-kit" "%TOOLS%\third_party\a64-kit" /MIR /NFL /NDL /NJH /NJS /NP >nul
    ) else (
        robocopy "%%D" "%TOOLS%\%%~nxD" /MIR /NFL /NDL /NJH /NJS /NP >nul
    )
    if errorlevel 8 (echo Copying the code failed. & pause & exit /b 1)
)
rem (the ready-made libraries a first build_ssr_longshot.bat lost, put back from the Hookshot-based tools)
if exist "%BASE%\tools\third_party\sdl2\SDL3.dll" if not exist "%TOOLS%\third_party\sdl2\SDL3.dll" (
    robocopy "%BASE%\tools\third_party" "%TOOLS%\third_party" /E /XC /XN /XO /NFL /NDL /NJH /NJS /NP >nul
)
robocopy "%REPO%" "%TOOLS%" /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (echo Copying the code failed. & pause & exit /b 1)

rem 3. The newer tools Longshot builds with (llvm-mingw, the CPM packages, wgpu-native): downloaded once, checked
"%TOOLS%\python\python.exe" -I "%TOOLS%\scripts\fetch_tools.py" "%TOOLS%"
if errorlevel 1 (echo Fetching Longshot's tools failed. Send Claude what it printed above. & pause & exit /b 1)

rem 4. The build: the game is unpacked into %GAME%, translated, compiled
set "RX_CPM=%TOOLS%\cpm"
set "WGPU_NATIVE=%TOOLS%\wgpu-native"
if exist "%TOOLS%\emsdk" set "EMSDK=%TOOLS%\emsdk"
set "PYTHONIOENCODING=utf-8"
set "HOOKSHOT_GAME=ssr"
rem (temporary files on E: too)
set "TEMP=%BASE%\temp"
set "TMP=%BASE%\temp"
if not exist "%BASE%\temp" mkdir "%BASE%\temp"
cd /d "%TOOLS%"
echo.
echo Building. This takes 20 to 60 minutes; the PC stays usable. The log: %LOG%
rem (below normal priority, so the PC stays responsive; LONGSHOT_JOBS=<n> limits the compilers)
start "" /belownormal /wait /b "%TOOLS%\python\python.exe" -I nx2\make_game.py --nup "%NUP%" --out "%GAME%" --work "%WORK%" --log "%LOG%"
if errorlevel 1 (
    echo.
    echo The build stopped. Send Claude the end of %LOG%.
    pause & exit /b 1
)

rem 5. diagnostics.bat beside the game, as in E:\Butterfly\game (F9 frame dump, F11 thread dump)
(
    echo @echo off
    echo cd /d "%%~dp0"
    echo set "RX_VK_DUMP_KEY=1"
    echo set "RX_VK_DUMP_DIR=%%~dp0dumps"
    echo set "EDEN_NX2_GAME_PUSH_CONSTANTS=1"
    echo rem set "BUTTERFLY_MOTION_AXES=7"
    echo if exist bin\longshot.exe ^(start "" bin\longshot.exe^) else start "" longshot.exe
) > "%GAME%\diagnostics.bat"

echo.
echo Done. The game is in %GAME%. Start it with diagnostics.bat there, or longshot.exe.
echo The first start compiles its shaders again (Longshot keeps its own caches).
pause
exit /b 0

:copy_tools
if not defined SRC (
    echo No tools found: neither %BASE%\tools nor Hookshot Builder's in %LOCALAPPDATA%\HookshotBuilder\versions.
    echo Run the Hookshot-based build_ssr.bat once, or start HookshotBuilder.exe once, then run this again.
    exit /b 1
)
echo Copying the compilers and libraries from %SRC% (once)...
for %%T in (python emsdk cpm llvm-mingw wgpu-native third_party) do (
    if exist "%SRC%\%%T" (
        robocopy "%SRC%\%%T" "%TOOLS%\%%T" /E /NFL /NDL /NJH /NJS /NP >nul
        if errorlevel 8 (echo Copying %%T failed. & exit /b 1)
    )
)
exit /b 0
