@echo off
setlocal
cd /d "%~dp0"
if not exist "build\langmake.exe" (
    echo Langmake has not been built yet.
    echo Run: powershell -ExecutionPolicy Bypass -File scripts\build.ps1
    pause
    exit /b 1
)
"build\langmake.exe" preview --project "examples\2d"
set "langmake_exit=%errorlevel%"
if not "%langmake_exit%"=="0" pause
exit /b %langmake_exit%
