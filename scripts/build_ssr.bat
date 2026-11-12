@echo off
setlocal EnableExtensions
rem Builds Butterfly (Nintendo Switch Sports Resort) into E:\Butterfly with Hookshot Builder's tools.
rem Run Hookshot Builder once first, so its tools are installed. Then put this file anywhere and run it.

rem ---- your settings -------------------------------------------------------------------------------------
set "NUP=E:\Switch 2 roms\Switch Sports Resort.nup"
set "BASE=E:\Butterfly"
set "BRANCH=ssr"
rem ----------------------------------------------------------------------------------------------------------

set "TOOLS=%BASE%\tools"
set "GAME=%BASE%\game"
set "LOG=%BASE%\build.log"
set "WORK=%BASE%\work"

if not exist "%NUP%" (
    echo Can't find your game file: %NUP%
    echo Open this .bat in Notepad and set NUP to the full path of your Sports Resort .nup.
    pause & exit /b 1
)
if not exist "%BASE%" mkdir "%BASE%"

rem 1. Hookshot Builder's tools (its own Python, compiler and libraries), copied once
if not exist "%TOOLS%\python\python.exe" (
    set "SRC="
    for /d %%V in ("%LOCALAPPDATA%\HookshotBuilder\versions\*") do (
        if exist "%%V\HookshotBuilder\tools\python\python.exe" set "SRC=%%V\HookshotBuilder\tools"
    )
    call :copy_tools
    if errorlevel 1 (pause & exit /b 1)
)

rem 2. Butterfly's code over them, fresh from GitHub each time
echo Downloading Butterfly (%BRANCH%)...
set "ZIP=%BASE%\butterfly.zip"
set "SRCDIR=%BASE%\src"
if exist "%SRCDIR%" rmdir /s /q "%SRCDIR%"
mkdir "%SRCDIR%"
powershell -NoProfile -Command "Invoke-WebRequest -UseBasicParsing 'https://github.com/reinderhe/Butterfly/archive/refs/heads/%BRANCH%.zip' -OutFile '%ZIP%'"
if errorlevel 1 (echo The download failed. & pause & exit /b 1)
tar -xf "%ZIP%" -C "%SRCDIR%"
if errorlevel 1 (echo Unzipping failed. & pause & exit /b 1)
for /d %%D in ("%SRCDIR%\*") do robocopy "%%D" "%TOOLS%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (echo Copying Butterfly's code failed. & pause & exit /b 1)

rem 3. The build: the game is unpacked into %GAME%, translated, compiled
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
"%TOOLS%\python\python.exe" -I nx2\make_game.py --nup "%NUP%" --out "%GAME%" --work "%WORK%" --log "%LOG%"
if errorlevel 1 (
    echo.
    echo The build stopped. Send Claude the end of %LOG%.
    pause & exit /b 1
)
echo.
echo Done. The game is in %GAME%. Start bin\hookshot.exe there, or hookshot.exe.
pause
exit /b 0

:copy_tools
if not defined SRC (
    echo Hookshot Builder's tools were not found in %LOCALAPPDATA%\HookshotBuilder\versions.
    echo Start HookshotBuilder.exe once ^(it downloads them^), close it, then run this again.
    exit /b 1
)
echo Copying Hookshot Builder's tools from %SRC% (once, about 1 GB)...
robocopy "%SRC%" "%TOOLS%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (echo Copying the tools failed. & exit /b 1)
exit /b 0
