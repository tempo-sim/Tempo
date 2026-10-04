@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

for /f "usebackq delims=" %%I in (`"%SCRIPT_DIR%FindProjectRoot.bat"`) do set "PROJECT_ROOT=%%I"
if not defined PROJECT_ROOT exit /b 1

set "PROJECT_NAME="
set "UPROJECT_FILE="
for %%F in ("!PROJECT_ROOT!\*.uproject") do (
    if exist "%%F" (
        set "PROJECT_NAME=%%~nF"
        set "UPROJECT_FILE=%%F"
    )
)
if not defined PROJECT_NAME (
    echo No .uproject file found 1>&2
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%SCRIPT_DIR%FindUnreal.bat"`) do set "UNREAL_ENGINE_PATH=%%I"
if not defined UNREAL_ENGINE_PATH exit /b 1

set "TARGET_PLATFORM=Win64"
if /i "%~1"=="Linux" (
    if not defined LINUX_MULTIARCH_ROOT (
        echo LINUX_MULTIARCH_ROOT not set, cannot cross-compile for Linux 1>&2
        exit /b 1
    )
    set "TARGET_PLATFORM=Linux"
)

where jq.exe >nul 2>&1
if errorlevel 1 (
    echo Couldn't find jq 1>&2
    exit /b 1
)

REM See Build.bat: a makefile cached from before a plugin moved runs that plugin's old pre-build steps.
call "%SCRIPT_DIR%DiscardStaleBuildSteps.bat"
if errorlevel 1 exit /b %ERRORLEVEL%

cd /d "!UNREAL_ENGINE_PATH!"

set "EXTRA_ARGS="

call "Engine\Build\BatchFiles\RunUAT.bat" Turnkey -command=VerifySdk -platform=!TARGET_PLATFORM! -UpdateIfNeeded -project="!PROJECT_ROOT!\!PROJECT_NAME!.uproject" BuildCookRun -nop4 -utf8output -nocompileeditor -skipbuildeditor -cook -target="!PROJECT_NAME!" -platform=!TARGET_PLATFORM! -project="!PROJECT_ROOT!\!PROJECT_NAME!.uproject" -installed -stage -package -pak -build -iostore -prereqs -clientconfig=Development -unrealexe="UnrealEditor-Cmd.exe" -stagingdirectory="!PROJECT_ROOT!\Packaged" !EXTRA_ARGS! %*
if errorlevel 1 exit /b %ERRORLEVEL%

if /i "!TARGET_PLATFORM!"=="Win64" (
    set "COOKED_DIR=!PROJECT_ROOT!\Saved\Cooked\Windows\!PROJECT_NAME!"
) else (
    set "COOKED_DIR=!PROJECT_ROOT!\Saved\Cooked\!TARGET_PLATFORM!\!PROJECT_NAME!"
)

xcopy /E /I /Y "!COOKED_DIR!\Metadata" "!PROJECT_ROOT!\Packaged\Metadata" >nul
if errorlevel 1 exit /b %ERRORLEVEL%
copy /Y "!COOKED_DIR!\AssetRegistry.bin" "!PROJECT_ROOT!\Packaged\" >nul
exit /b %ERRORLEVEL%
