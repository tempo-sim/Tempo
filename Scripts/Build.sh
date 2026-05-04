#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
cd "$PROJECT_ROOT"
PROJECT_NAME=$(find . -maxdepth 1 -name "*.uproject" -exec basename {} .uproject \;)

UNREAL_ENGINE_PATH=$("$SCRIPT_DIR"/FindUnreal.sh)

PLATFORM=""
if [[ "$OSTYPE" = "msys" ]]; then
  PLATFORM="Win64"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PLATFORM="Mac"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PLATFORM="Linux"
else
  echo "Unsupported platform"
  exit 1
fi

cd "$UNREAL_ENGINE_PATH"
if [ "$PLATFORM" = "Win64" ]; then
  # Windows build script is a little different
  # cygpath converts the Unix-style path to a native Windows path so MSYS doesn't
  # try to convert it itself — MSYS path conversion splits on spaces in the value
  # of -Project=..., turning "Unreal Projects/..." into two arguments.
  UPROJECT_WIN=$(cygpath -w "$PROJECT_ROOT/$PROJECT_NAME.uproject")
  ./Engine/Build/BatchFiles/Build.bat "${PROJECT_NAME}Editor" Development "$PLATFORM" -Project="$UPROJECT_WIN" -WaitMutex -FromMsBuild "$@"
else
  ./Engine/Build/BatchFiles/"$PLATFORM"/Build.sh "${PROJECT_NAME}Editor" Development "$PLATFORM" -Project="$PROJECT_ROOT/$PROJECT_NAME.uproject" -buildscw "$@"
fi
