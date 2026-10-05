@echo off
REM Prints the path to Git for Windows' bash.exe, or fails with an error to stderr.
REM Used by wrapper scripts that delegate to the corresponding .sh file.
REM The search lives with TempoCore, whose UBT pre-build step needs it too.

call "%~dp0..\TempoCore\Scripts\_FindBash.bat"
exit /b %ERRORLEVEL%
