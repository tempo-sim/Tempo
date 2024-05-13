@echo off

REM Simply call the bash versions from the same directory
REM For some reason calling multiple batch files as prebuild steps doesn't work? So we combine them here.
bash %~dp0GenProtos.sh %*
bash %~dp0GenAPI.sh %3

exit /b 0
