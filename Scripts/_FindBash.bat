@echo off
REM Prints the path to Git for Windows' bash.exe, or fails with an error to stderr.
REM Used by wrapper scripts that delegate to the corresponding .sh file.

if exist "%ProgramFiles%\Git\bin\bash.exe" (
    echo %ProgramFiles%\Git\bin\bash.exe
    exit /b 0
)
if defined ProgramW6432 if exist "%ProgramW6432%\Git\bin\bash.exe" (
    echo %ProgramW6432%\Git\bin\bash.exe
    exit /b 0
)
set "PFX86=%ProgramFiles(x86)%"
if exist "%PFX86%\Git\bin\bash.exe" (
    echo %PFX86%\Git\bin\bash.exe
    exit /b 0
)
if exist "%LocalAppData%\Programs\Git\bin\bash.exe" (
    echo %LocalAppData%\Programs\Git\bin\bash.exe
    exit /b 0
)

echo Could not find Git for Windows bash.exe. Install Git for Windows from https://git-scm.com/download/win 1>&2
exit /b 1
