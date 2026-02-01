@echo off
REM Copyright Tempo Simulation, LLC. All Rights Reserved

REM Simply call the individual scripts from the same directory
bash %~dp0GenAPI.sh %1 %3 %4
if %errorlevel% neq 0 exit /b %errorlevel%

exit /b 0
