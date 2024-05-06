@echo off

REM Simply call the bash version from the same directory
bash %~dp0GenAPI.sh %*

exit /b
