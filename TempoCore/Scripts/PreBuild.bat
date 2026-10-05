@echo off
REM Copyright Tempo Simulation, LLC. All Rights Reserved

setlocal

REM Find Git Bash to avoid invoking WSL's bash.exe
set "GIT_BASH="
for /f "usebackq delims=" %%I in (`"%~dp0_FindBash.bat"`) do set "GIT_BASH=%%I"
if not defined GIT_BASH (
    echo [Tempo Prebuild] ERROR: Could not find Git Bash. See above.
    exit /b 1
)

REM Simply call the individual scripts from the same directory
"%GIT_BASH%" "%~dp0GenAPI.sh" %1 %3 %4
if %errorlevel% neq 0 exit /b %errorlevel%

"%GIT_BASH%" "%~dp0GenRustAPI.sh" %1 %3 %4
if %errorlevel% neq 0 exit /b %errorlevel%

"%GIT_BASH%" "%~dp0GenCppAPI.sh" %1 %3 %4 %8
if %errorlevel% neq 0 exit /b %errorlevel%

exit /b 0
