@echo off
REM ---------------------------------------------------------------------------
REM  UartX - one-shot release build, installer and portable zip
REM
REM  Run it from anywhere:   scripts\build_release.bat
REM
REM  Produces:
REM    build\windows\UartX.exe                                (the build)
REM    build\windows\stage\                                   (self-contained tree)
REM    build\windows\installer\UartX-<ver>-setup.exe
REM    build\windows\installer\UartX-<ver>-portable-win64.zip
REM
REM  Adjust QT_DIR / MINGW_DIR below if your Qt lives somewhere else.
REM ---------------------------------------------------------------------------
setlocal

set "QT_DIR=C:\Qt\6.11.2\mingw_64"
set "MINGW_DIR=C:\Qt\Tools\mingw1310_64"
set "CMAKE_DIR=C:\Qt\Tools\CMake_64\bin"
set "NINJA_DIR=C:\Qt\Tools\Ninja"

if not exist "%QT_DIR%\bin\qmake.exe" (
    echo ERROR: Qt not found at %QT_DIR%
    echo        Edit QT_DIR at the top of this script.
    exit /b 1
)

set "PATH=%MINGW_DIR%\bin;%CMAKE_DIR%;%NINJA_DIR%;%QT_DIR%\bin;%PATH%"

REM The script lives in scripts\; every path below is relative to the
REM repository root, so step up one level first.
cd /d "%~dp0.."

echo.
echo === Configuring (Release) ===
cmake -S . -B build\windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 exit /b 1

echo.
echo === Building ===
cmake --build build\windows
if errorlevel 1 exit /b 1

echo.
echo === Staging + compiling installer ===
cmake --build build\windows --target installer
if errorlevel 1 (
    echo.
    echo The installer step failed. If Inno Setup is missing, install it with:
    echo     winget install -e --id JRSoftware.InnoSetup
    echo and then re-run this script.
    exit /b 1
)

echo.
echo === Building portable zip ===
cmake --build build\windows --target portable
if errorlevel 1 exit /b 1

echo.
echo === Done ===
dir /b build\windows\installer
echo.
echo Artifacts are in: %CD%\build\windows\installer
endlocal
