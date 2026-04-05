@echo off
REM Copyright Tempo Simulation, LLC. All Rights Reserved

REM Find Git Bash to avoid invoking WSL's bash.exe
set "GIT_BASH="
where git >nul 2>nul && for /f "delims=" %%i in ('where git') do set "GIT_DIR=%%~dpi.."
if defined GIT_DIR if exist "%GIT_DIR%\bin\bash.exe" set "GIT_BASH=%GIT_DIR%\bin\bash.exe"
if not defined GIT_BASH if exist "C:\Program Files\Git\bin\bash.exe" set "GIT_BASH=C:\Program Files\Git\bin\bash.exe"
if not defined GIT_BASH (
    echo [Tempo Prebuild] ERROR: Could not find Git Bash. Please ensure Git for Windows is installed and on PATH.
    exit /b 1
)

REM Simply call the individual scripts from the same directory
"%GIT_BASH%" %~dp0GenAPI.sh %1 %3 %4
if %errorlevel% neq 0 exit /b %errorlevel%

exit /b 0
