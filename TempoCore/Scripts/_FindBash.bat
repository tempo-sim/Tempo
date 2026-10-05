@echo off
REM Copyright Tempo Simulation, LLC. All Rights Reserved

REM Prints the path to Git for Windows' bash.exe, or fails with an error to stderr.
REM Used by PreBuild.bat and (via Tempo/Scripts/_FindBash.bat) the wrappers that delegate to a .sh file.
REM Never falls back on a bare "bash" from PATH, which is often WSL's bash.exe.
REM Search order:
REM   1. TEMPO_GIT_BASH, the full path to bash.exe, for installs nothing below finds
REM   2. The InstallPath Git for Windows' installer records in the registry
REM   3. The Git install that owns the git.exe on PATH
REM   4. Git for Windows' default install locations

setlocal
set "FOUND="

if defined TEMPO_GIT_BASH call :TryBash "%TEMPO_GIT_BASH%"
if defined TEMPO_GIT_BASH if not defined FOUND echo Warning: TEMPO_GIT_BASH (%TEMPO_GIT_BASH%) does not exist, searching elsewhere 1>&2

for %%K in (HKLM\SOFTWARE\GitForWindows HKCU\SOFTWARE\GitForWindows HKLM\SOFTWARE\WOW6432Node\GitForWindows) do (
    for /f "tokens=2,*" %%A in ('reg query "%%K" /v InstallPath 2^>nul ^| findstr /i /c:"InstallPath"') do (
        if not defined FOUND call :TryBash "%%B\bin\bash.exe"
    )
)

REM git.exe lives in <Git>\cmd, <Git>\bin or <Git>\mingw64\bin.
for /f "delims=" %%G in ('where git 2^>nul') do (
    if not defined FOUND call :TryBash "%%~dpG..\bin\bash.exe"
    if not defined FOUND call :TryBash "%%~dpG..\..\bin\bash.exe"
)

if not defined FOUND call :TryBash "%ProgramFiles%\Git\bin\bash.exe"
if not defined FOUND if defined ProgramW6432 call :TryBash "%ProgramW6432%\Git\bin\bash.exe"
if not defined FOUND call :TryBash "%ProgramFiles(x86)%\Git\bin\bash.exe"
if not defined FOUND call :TryBash "%LocalAppData%\Programs\Git\bin\bash.exe"

if defined FOUND goto :Print
echo Could not find Git for Windows bash.exe. Install Git for Windows from https://git-scm.com/download/win, 1>&2
echo or set TEMPO_GIT_BASH to the full path of its bin\bash.exe. 1>&2
exit /b 1

:Print
echo %FOUND%
exit /b 0

:TryBash
if exist "%~1" set "FOUND=%~f1"
exit /b 0
