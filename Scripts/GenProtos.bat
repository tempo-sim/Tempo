@echo off

REM Simply call the bash version from the same directory
bash %~dp0GenProtos.sh %*

exit /b
