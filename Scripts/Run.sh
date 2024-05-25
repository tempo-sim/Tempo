#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")

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
  ./Engine/Binaries/Win64/UnrealEditor-Cmd.exe "$TEMPO_ROOT/Tempo.uproject"
elif [ "$PLATFORM" = "Mac" ]; then
  ./Engine/Binaries/Mac/UnrealEditor.app/Contents/MacOS/UnrealEditor "$TEMPO_ROOT/Tempo.uproject"
elif [ "$PLATFORM" = "Linux" ]; then
  ./Engine/Binaries/Linux/UnrealEditor "$TEMPO_ROOT/Tempo.uproject"
else
  echo "Unsupported platform"
  exit 1
fi
