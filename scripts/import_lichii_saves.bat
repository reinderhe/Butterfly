@echo off
setlocal EnableExtensions
rem Copies Nintendo Switch Sports Resort's save from Lichii into Butterfly (E:\Butterfly\game-longshot).
rem Butterfly's save as it is now is kept first in user\save_backup_<date>_<time>, so nothing is lost.
rem Close Lichii and the game before running this.

set "LICHII=%APPDATA%\lichii\nand\user\save\0000000000000000"
set "GAME=E:\Butterfly\game-longshot"
set "TITLE=04008E001F098000"

rem (Lichii's profile folder: the one that has this game's save; the all-zero one is the console-wide save)
set "PROFILE="
set "UID="
for /d %%U in ("%LICHII%\*") do (
    if /i not "%%~nxU"=="00000000000000000000000000000000" if exist "%%U\%TITLE%\" (
        set "PROFILE=%%U\%TITLE%"
        set "UID=%%~nxU"
    )
)
set "DEVICE=%LICHII%\00000000000000000000000000000000\%TITLE%"

if not defined PROFILE (
    echo No Sports Resort save of a Lichii profile found in %LICHII%.
    pause & exit /b 1
)
echo Lichii's save:   %PROFILE%
if exist "%DEVICE%\" (echo Console-wide:    %DEVICE%) else (echo Console-wide:    none in Lichii)
echo Lichii's user:   %UID%
echo Into Butterfly:  %GAME%\user\save\%UID%
echo.

for /f %%T in ('powershell -NoProfile -Command "Get-Date -Format yyyyMMdd_HHmmss"') do set "STAMP=%%T"
if exist "%GAME%\user\save\" (
    robocopy "%GAME%\user\save" "%GAME%\user\save_backup_%STAMP%" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 (echo Backing up Butterfly's save failed: nothing changed. & pause & exit /b 1)
    echo Butterfly's old save kept in %GAME%\user\save_backup_%STAMP%
)

rem (the save into a folder named after Lichii's user, and Butterfly told to be that same user (HOS_ACCOUNT_UID in
rem longshot_env.txt): the save remembers which user its characters belong to, so with another user they show as guests)
robocopy "%PROFILE%" "%GAME%\user\save\%UID%" /E /NFL /NDL /NJH /NJS /NP >nul
if errorlevel 8 (echo Copying the save failed. & pause & exit /b 1)
set "ENVF=%GAME%\longshot_env.txt"
if exist "%ENVF%" (
    findstr /v /i /b /c:"HOS_ACCOUNT_UID=" "%ENVF%" > "%ENVF%.new"
    move /y "%ENVF%.new" "%ENVF%" >nul
)
>>"%ENVF%" echo HOS_ACCOUNT_UID=%UID%
echo Butterfly now plays as Lichii's user (HOS_ACCOUNT_UID=%UID% in longshot_env.txt)
if exist "%DEVICE%\" (
    robocopy "%DEVICE%" "%GAME%\user\save\device" /E /NFL /NDL /NJH /NJS /NP >nul
    if errorlevel 8 (echo Copying the console-wide save failed. & pause & exit /b 1)
)
echo.
echo Done. Start the game; your Lichii progress should be there.
pause
