@echo off
setlocal enabledelayedexpansion

set "SCRIPT_DIR=%~dp0"

for /f "usebackq delims=" %%I in (`"%SCRIPT_DIR%FindProjectRoot.bat"`) do set "PROJECT_ROOT=%%I"
if not defined PROJECT_ROOT exit /b 1

set "DESCRIPTOR="
for /f "usebackq delims=" %%I in (`dir /s /b "!PROJECT_ROOT!\Plugins\TempoROS.uplugin" 2^>nul`) do (
    if not defined DESCRIPTOR set "DESCRIPTOR=%%I"
)

if not defined DESCRIPTOR (
    echo No TempoROS plugin found under !PROJECT_ROOT!\Plugins 1>&2
    exit /b 1
)

for %%I in ("!DESCRIPTOR!") do set "OUTPUT=%%~dpI"
if "!OUTPUT:~-1!"=="\" set "OUTPUT=!OUTPUT:~0,-1!"
echo !OUTPUT!
