@echo off
setlocal enabledelayedexpansion

REM Generates the reflected API from the editor's reflection: runs the schema commandlet in the
REM editor, then turns the one schema into classes for the project's Python package and (when Rust
REM generation is opted in) its Rust crate.
REM
REM Run on its own, to refresh the classes after changing a Blueprint or a UPROPERTY:
REM
REM   Scripts\GenReflectedAPI.bat
REM
REM Nothing calls it: Package.bat builds no client packages, so nothing this generates ships. That
REM is also why this mirror stops short of the .sh -- it generates from the editor's unfiltered
REM walk and so does not make the C++ library (see the note below).
REM
REM The schema lands in Saved\TempoSchema\tempo_schema.json. The classes go into the project's own
REM client package and crate, beside the wrappers its protos generate: <project>.reflected in
REM Content\Python\API and <project>::reflected in Content\Rust\API.
REM
REM Rust follows the same gate as the RPC client generator, TEMPO_GEN_RUST_API.

set "SCRIPT_DIR=%~dp0"

for /f "usebackq delims=" %%I in (`"%SCRIPT_DIR%FindProjectRoot.bat"`) do set "PROJECT_ROOT=%%I"
if not defined PROJECT_ROOT exit /b 1

set "PROJECT_NAME="
for %%F in ("!PROJECT_ROOT!\*.uproject") do (
    if exist "%%F" set "PROJECT_NAME=%%~nF"
)
if not defined PROJECT_NAME (
    echo No .uproject file found 1>&2
    exit /b 1
)

for /f "usebackq delims=" %%I in (`"%SCRIPT_DIR%FindUnreal.bat"`) do set "UNREAL_ENGINE_PATH=%%I"
if not defined UNREAL_ENGINE_PATH exit /b 1

set "PYTHON_DIR=!SCRIPT_DIR!..\TempoCore\Content\Python"

REM Prefer Tempo's managed venv, as Package.bat does; gen_naming.py is stdlib-only so any
REM interpreter can derive the names.
set "PYTHON_BIN="
if exist "!PROJECT_ROOT!\TempoEnv\Scripts\python.exe" set "PYTHON_BIN=!PROJECT_ROOT!\TempoEnv\Scripts\python.exe"
if not defined PYTHON_BIN (
    where python >nul 2>&1 && set "PYTHON_BIN=python"
)
if not defined PYTHON_BIN (
    echo No python interpreter available to generate the reflected API 1>&2
    exit /b 1
)

REM The project package's import name from gen_naming.py -- the function that names it for the
REM proto wrappers too -- rather than spelled a second time here.
for /f "usebackq delims=" %%I in (`"!PYTHON_BIN!" -c "import sys; sys.path.insert(0, r'!PYTHON_DIR!'); from gen_naming import package_import_name, project_package_name; print(package_import_name(project_package_name(r'!PROJECT_ROOT!')))"`) do set "IMPORT_NAME=%%I"

set "SCHEMA_DIR=!PROJECT_ROOT!\Saved\TempoSchema"
set "SCHEMA=!SCHEMA_DIR!\tempo_schema.json"
set "LOG=!SCHEMA_DIR!\gen_reflected_api.log"
if not exist "!SCHEMA_DIR!" mkdir "!SCHEMA_DIR!"

echo Walking reflection to !SCHEMA! (this loads every Blueprint, so it takes a minute)
REM The editor's own exit status is not relied on here: the commandlet's result is what matters, and
REM it says so in the log.
"!UNREAL_ENGINE_PATH!\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "!PROJECT_ROOT!\!PROJECT_NAME!.uproject" -run=TempoWorldSchema -Out="!SCHEMA!" -nullrhi -unattended -nopause -nosplash -nosound -stdout -fullstdoutlogoutput > "!LOG!" 2>&1

findstr /c:"finished execution (result 0)" "!LOG!" >nul
if errorlevel 1 (
    echo Schema commandlet failed. See !LOG! 1>&2
    exit /b 1
)
if not exist "!SCHEMA!" (
    echo Schema commandlet reported success but wrote nothing to !SCHEMA! 1>&2
    exit /b 1
)
findstr /c:"Wrote schema" "!LOG!"

REM Into the project's own package, creating it if the project has no protos to have made it
REM already, in a directory of its own so a class module cannot land on a proto module's name.
"!PYTHON_BIN!" "!PYTHON_DIR!\gen_project_package.py" python "!PROJECT_ROOT!"
if errorlevel 1 exit /b 1
set "CLASSES_DIR=!PROJECT_ROOT!\Content\Python\API\!IMPORT_NAME!\reflected"
if exist "!CLASSES_DIR!" rmdir /s /q "!CLASSES_DIR!"

echo Generating !IMPORT_NAME!.reflected
"!PYTHON_BIN!" "!PYTHON_DIR!\gen_py_classes.py" "!SCHEMA!" "!CLASSES_DIR!" "--package=!IMPORT_NAME!.reflected"
if errorlevel 1 exit /b 1
echo Reflected Python API generated at !CLASSES_DIR!

REM This mirror generates from the editor's unfiltered walk: the .sh asks the packaged build what it
REM has and refuses to generate without an answer, while this one has never asked. Package.bat builds
REM no Python or Rust clients either, so nothing here ships today -- closing both is one job.

REM The Rust classes, from the same schema, into the project's own crate as its reflected module. The
REM crate's manifest and lib.rs are written after them, because lib.rs declares what src\ holds.
if not defined TEMPO_GEN_RUST_API goto :no_rust
if "!TEMPO_GEN_RUST_API!"=="0" goto :no_rust

set "RUST_CLASSES_DIR=!PROJECT_ROOT!\Content\Rust\API\src\reflected"
if exist "!RUST_CLASSES_DIR!" rmdir /s /q "!RUST_CLASSES_DIR!"
echo Generating !IMPORT_NAME!::reflected
"!PYTHON_BIN!" "!PYTHON_DIR!\gen_rust_classes.py" "!SCHEMA!" "!RUST_CLASSES_DIR!" "--root=crate::reflected"
if errorlevel 1 exit /b 1
"!PYTHON_BIN!" "!PYTHON_DIR!\gen_project_package.py" rust "!PROJECT_ROOT!"
if errorlevel 1 exit /b 1
echo Reflected Rust API generated at !RUST_CLASSES_DIR!
goto :cpp

:no_rust
echo Skipping the reflected Rust API (set TEMPO_GEN_RUST_API=1 to generate it)

:cpp
REM No C++ classes here either, under the same caveat. The .sh compiles them into libtempo_reflected
REM and installs it beside the C++ client in TempoCore\Content\Cpp\API, which is what a consumer
REM links. Built from the unfiltered walk, that library would declare the editor's classes and
REM members, so a program against it compiles and then fails at runtime on a packaged sim -- and
REM unlike the Python and Rust trees above, it would sit exactly where the next C++ build looks.
REM A reflected C++ library that cannot be trusted is worse than none.
echo Skipping the reflected C++ API ^(this mirror generates from the editor's unfiltered walk^)
exit /b 0
