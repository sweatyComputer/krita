@echo off
rem ===================================================================
rem  Build the DH Krita fork for Android (WSL) and Windows at the same
rem  time, from the one source tree on D:.
rem  Put this file in D:\Users\Darren\Documents\ and double-click it.
rem
rem  Assumes the one-time Windows setup is done (C:\krita-dev\env.bat
rem  exists and C:\krita-dev\b_krita has been configured once).
rem
rem  i7-12700F = 20 threads, 32 GB RAM: 8 jobs each side. Raise one
rem  side to 12 if you're only building that one.
rem ===================================================================

set SRC_WSL=/mnt/d/Users/Darren/Documents/krita
set APK_WSL=/mnt/d/Users/Darren/Documents/krita-apk
set ANDROID_JOBS=8
set WIN_JOBS=8

rem --- Android: own window, runs in WSL ---------------------------------
start "Krita Android build" wsl.exe -d Ubuntu-24.04 --cd %SRC_WSL% -- bash -lc "KRITA_SRC=%SRC_WSL% OUT_DIR=%APK_WSL% JOBS=%ANDROID_JOBS% bash build-android-wsl.sh; echo; echo === Android build finished, exit code $? ===; exec bash"

rem --- Windows: this window ---------------------------------------------
cd /d C:\krita-dev
call env.bat
cd /d C:\krita-dev\b_krita
ninja -j%WIN_JOBS% install
if errorlevel 1 (
    echo.
    echo === Windows build FAILED ===
) else (
    echo.
    echo === Windows build OK: run C:\krita-dev\run-krita.cmd ===
)
pause