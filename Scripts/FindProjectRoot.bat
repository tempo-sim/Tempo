@echo off
setlocal enabledelayedexpansion

set "CURRENT_DIR=%~dp0"

:loop
for %%F in ("!CURRENT_DIR!*.uproject") do (
    if exist "%%F" (
        set "OUTPUT=!CURRENT_DIR!"
        if "!OUTPUT:~-1!"=="\" set "OUTPUT=!OUTPUT:~0,-1!"
        echo !OUTPUT!
        exit /b 0
    )
)

REM At drive root ("X:\") — nothing further up to walk
if "!CURRENT_DIR:~3!"=="" (
    echo No .uproject file found 1>&2
    exit /b 1
)

set "TRIMMED=!CURRENT_DIR:~0,-1!"
for %%I in ("!TRIMMED!") do set "CURRENT_DIR=%%~dpI"
goto :loop
