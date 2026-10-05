@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

for /f "usebackq delims=" %%I in (`"%SCRIPT_DIR%FindProjectRoot.bat"`) do set "PROJECT_ROOT=%%I"
if not defined PROJECT_ROOT exit /b 1

set "PROJECT_NAME="
for %%F in ("!PROJECT_ROOT!\*.uproject") do (
    if exist "%%F" set "PROJECT_NAME=%%~nF"
)
if not defined PROJECT_NAME (
    echo No .uproject file found 1>&2
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%SCRIPT_DIR%FindUnreal.bat"`) do set "UNREAL_ENGINE_PATH=%%I"
if not defined UNREAL_ENGINE_PATH exit /b 1

REM UBT runs a target's cached pre-build steps before it validates the makefile that owns them, so a
REM plugin that has moved since the last build takes the build down with it until the makefile goes.
call "%SCRIPT_DIR%DiscardStaleBuildSteps.bat"
if errorlevel 1 exit /b %ERRORLEVEL%

cd /d "!UNREAL_ENGINE_PATH!"
call "Engine\Build\BatchFiles\Build.bat" "!PROJECT_NAME!Editor" Development Win64 -Project="!PROJECT_ROOT!\!PROJECT_NAME!.uproject" -WaitMutex -FromMsBuild %*
exit /b %ERRORLEVEL%
