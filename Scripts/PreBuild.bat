@echo off

REM Simply call the individual scripts from the same directory
bash %~dp0GenProtos.sh %*
bash %~dp0GenAPI.sh %3

exit /b 0
