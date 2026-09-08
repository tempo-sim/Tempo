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
  # Drop any trailing separator so the path can be appended to below.
  UNREAL_ENGINE_PATH="${UNREAL_ENGINE_PATH%[\\/]}"
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
  # Windows build script is a little different.
  # Invoke the .bat by absolute short path, not relatively: MSYS resolves a
  # relative program path through the real filesystem, which restores the long
  # "Program Files" form and reintroduces the space the 8.3 conversion removed.
  "$UNREAL_ENGINE_PATH\\Engine\\Build\\BatchFiles\\Build.bat" "${PROJECT_NAME}Editor" Development $PLATFORM -Project="$PROJECT_ROOT/$PROJECT_NAME.uproject" -WaitMutex -FromMsBuild -clean "$@"
else
  ./Engine/Build/BatchFiles/$PLATFORM/Build.sh "${PROJECT_NAME}Editor" Development $PLATFORM -Project="$PROJECT_ROOT/$PROJECT_NAME.uproject" -buildscw -clean "$@"
fi
