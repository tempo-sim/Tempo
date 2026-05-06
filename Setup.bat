@echo off
setlocal
for /f "usebackq delims=" %%I in (`"%~dp0Scripts\_FindBash.bat"`) do set "BASH_EXE=%%I"
if not defined BASH_EXE exit /b 1
"%BASH_EXE%" "%~dp0Setup.sh" %*
exit /b %ERRORLEVEL%
