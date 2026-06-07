@echo off
setlocal enabledelayedexpansion

REM Runs Tempo's Unreal automation tests headlessly and fails if any test fails, if no matching
REM tests ran, or if the editor crashed. Builds nothing - run Scripts\Build.bat first.
REM
REM Usage:
REM   Scripts\Test.bat                 (run all "Tempo." tests)
REM   Scripts\Test.bat Tempo.Sensors   (run a subset by name prefix)

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

set "TEST_FILTER=%~1"
if not defined TEST_FILTER set "TEST_FILTER=Tempo."

set "REPORT_DIR=!PROJECT_ROOT!\Saved\TempoTestReport"
if exist "!REPORT_DIR!" rmdir /s /q "!REPORT_DIR!"
mkdir "!REPORT_DIR!"

echo Running automation tests matching '!TEST_FILTER!'...

"!UNREAL_ENGINE_PATH!\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "!PROJECT_ROOT!\!PROJECT_NAME!.uproject" -ExecCmds="Automation RunTests !TEST_FILTER!" -TestExit="Automation Test Queue Empty" -ReportExportPath="!REPORT_DIR!" -nullrhi -unattended -nopause -nosplash -nosound -stdout -fullstdoutlogoutput

set "INDEX=!REPORT_DIR!\index.json"
if not exist "!INDEX!" (
    echo FAILED: no test report was produced at !INDEX!. 1>&2
    exit /b 1
)

REM jq is a documented Tempo prerequisite (see README).
for /f "usebackq delims=" %%T in (`jq ".tests | length" "!INDEX!"`) do set "TOTAL=%%T"
for /f "usebackq delims=" %%F in (`jq "[.tests[] | select(.state == \"Fail\")] | length" "!INDEX!"`) do set "FAILED=%%F"

echo Automation tests run: !TOTAL!, failed: !FAILED!

if "!TOTAL!"=="0" (
    echo FAILED: no tests matched '!TEST_FILTER!'. Did the test files compile and register? 1>&2
    exit /b 1
)
if not "!FAILED!"=="0" (
    echo FAILED tests: 1>&2
    jq -r ".tests[] | select(.state == \"Fail\") | \"  - \" + .fullTestPath" "!INDEX!" 1>&2
    exit /b 1
)

echo All !TOTAL! automation tests passed.
exit /b 0
