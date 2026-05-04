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
  # 8.3 short form removes the space in "Program Files" so PWD is spaces-free
  # when we invoke .bat files — otherwise cmd.exe's strip-outer-quotes rule
  # mangles the path when another argument (e.g. -Project=...) is also quoted.
  UNREAL_ENGINE_PATH=$(cygpath -w -s "$UNREAL_ENGINE_PATH")
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
  ./Engine/Build/BatchFiles/Build.bat "${PROJECT_NAME}Editor" Development "$PLATFORM" -Project="$PROJECT_ROOT/$PROJECT_NAME.uproject" -WaitMutex -FromMsBuild "$@"
else
  ./Engine/Build/BatchFiles/"$PLATFORM"/Build.sh "${PROJECT_NAME}Editor" Development "$PLATFORM" -Project="$PROJECT_ROOT/$PROJECT_NAME.uproject" -buildscw "$@"
fi
