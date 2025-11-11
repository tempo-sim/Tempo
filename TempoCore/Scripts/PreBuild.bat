@echo off
REM Copyright Tempo Simulation, LLC. All Rights Reserved

if defined TEMPO_SKIP_PREBUILD (
    if not "%TEMPO_SKIP_PREBUILD%"=="0" (
        if not "%TEMPO_SKIP_PREBUILD%"=="" (
            echo Skipping TempoCore prebuild steps because TEMPO_SKIP_PREBUILD is %TEMPO_SKIP_PREBUILD%
            exit /b 0
        )
    )
)

REM Simply call the bash script from the same directory
bash %~dp0PreBuild.sh %*
if %errorlevel% neq 0 exit /b %errorlevel%

exit /b 0
