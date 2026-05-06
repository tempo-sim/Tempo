@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

if defined UNREAL_ENGINE_PATH (
    if not exist "!UNREAL_ENGINE_PATH!\Engine\Binaries" goto :invalid_engine
    if not exist "!UNREAL_ENGINE_PATH!\Engine\Build" goto :invalid_engine
    if not exist "!UNREAL_ENGINE_PATH!\Engine\Config" goto :invalid_engine
    echo !UNREAL_ENGINE_PATH!
    exit /b 0
)

where jq.exe >nul 2>&1
if errorlevel 1 (
    echo Couldn't find jq 1>&2
    echo Install: download jq-windows-amd64.exe from https://github.com/jqlang/jq/releases and place it on PATH as jq.exe 1>&2
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%SCRIPT_DIR%FindProjectRoot.bat"`) do set "PROJECT_ROOT=%%I"
if not defined PROJECT_ROOT exit /b 1

set "UPROJECT_FILE="
set /a UPROJECT_COUNT=0
for %%F in ("!PROJECT_ROOT!\*.uproject") do (
    if exist "%%F" (
        set /a UPROJECT_COUNT+=1
        set "UPROJECT_FILE=%%F"
    )
)
if !UPROJECT_COUNT! equ 0 (
    echo Error: No .uproject file found in !PROJECT_ROOT! 1>&2
    exit /b 1
)
if !UPROJECT_COUNT! gtr 1 (
    echo Error: Multiple .uproject files found in !PROJECT_ROOT! 1>&2
    exit /b 1
)

set "ENGINE_ASSOC="
for /f "usebackq delims=" %%I in (`jq -r ".EngineAssociation" "!UPROJECT_FILE!"`) do set "ENGINE_ASSOC=%%I"
if not defined ENGINE_ASSOC goto :no_assoc
if "!ENGINE_ASSOC!"=="null" goto :no_assoc

REM If it looks like a bare release version (e.g. 5.4), prepend UE_
echo !ENGINE_ASSOC!| findstr /r /c:"^[0-9][0-9]*\.[0-9][0-9]*$" >nul
if not errorlevel 1 set "ENGINE_ASSOC=UE_!ENGINE_ASSOC!"

set "REGISTRY_FILE="
if exist "%APPDATA%\Epic\UnrealEngineLauncher\LauncherInstalled.dat" (
    set "REGISTRY_FILE=%APPDATA%\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
)
if not defined REGISTRY_FILE if exist "%ProgramData%\Epic\UnrealEngineLauncher\LauncherInstalled.dat" (
    set "REGISTRY_FILE=%ProgramData%\Epic\UnrealEngineLauncher\LauncherInstalled.dat"
)
if not defined REGISTRY_FILE (
    echo Error: Could not find Epic Games LauncherInstalled.dat 1>&2
    echo Make sure the engine is installed via Epic Games Launcher, or set UNREAL_ENGINE_PATH 1>&2
    exit /b 1
)

set "ENGINE_PATH="
for /f "usebackq delims=" %%I in (`jq -r --arg ENGINE_ID "!ENGINE_ASSOC!" ".InstallationList[]? | select(.AppName == $ENGINE_ID) | .InstallLocation // empty" "!REGISTRY_FILE!"`) do (
    if not defined ENGINE_PATH set "ENGINE_PATH=%%I"
)

if not defined ENGINE_PATH (
    echo Error: Could not find engine installation for: !ENGINE_ASSOC! 1>&2
    echo Make sure the engine is installed via Epic Games Launcher 1>&2
    exit /b 1
)

if not exist "!ENGINE_PATH!\Engine\Binaries" goto :invalid_engine
if not exist "!ENGINE_PATH!\Engine\Build" goto :invalid_engine
if not exist "!ENGINE_PATH!\Engine\Config" goto :invalid_engine

echo !ENGINE_PATH!
exit /b 0

:no_assoc
echo Error: No EngineAssociation found in !UPROJECT_FILE! 1>&2
exit /b 1

:invalid_engine
echo Error: Engine path appears incomplete: !ENGINE_PATH! 1>&2
exit /b 1
