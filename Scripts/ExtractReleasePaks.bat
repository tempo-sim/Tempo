@echo off
setlocal
for /f "usebackq delims=" %%I in (`"%~dp0_FindBash.bat"`) do set "BASH_EXE=%%I"
if not defined BASH_EXE exit /b 1
"%BASH_EXE%" "%~dp0ExtractReleasePaks.sh" %*
exit /b %ERRORLEVEL%
