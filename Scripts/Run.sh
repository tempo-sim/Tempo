#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
cd "$PROJECT_ROOT"
PROJECT_NAME=$(find . -maxdepth 1 -name "*.uproject" -exec basename {} .uproject \;)

# Check for UNREAL_ENGINE_PATH
if [ -z ${UNREAL_ENGINE_PATH+x} ]; then
  echo "Please set UNREAL_ENGINE_PATH environment variable and re-run";
  exit 1
fi

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
  ./Engine/Binaries/Win64/UnrealEditor-Cmd.exe "$PROJECT_ROOT/$PROJECT_NAME.uproject" "$@"
elif [ "$PLATFORM" = "Mac" ]; then
  ./Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor "$PROJECT_ROOT/$PROJECT_NAME.uproject" "$@"
elif [ "$PLATFORM" = "Linux" ]; then
  # -norelativemousemode is a workaround for an issue where the mouse is extremely sensitive with Ubuntu remote desktop
  # https://forums.unrealengine.com/t/work-from-home-how-to-use-unreal-through-remote-desktop/141157
  ./Engine/Binaries/Linux/UnrealEditor "$PROJECT_ROOT/$PROJECT_NAME.uproject" -norelativemousemode "$@"
else
  echo "Unsupported platform"
  exit 1
fi
