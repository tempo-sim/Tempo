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
  echo "Build script only supported on Mac and Linux currently"
  exit 1
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PLATFORM="Mac"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PLATFORM="Linux"
else
  echo "Unsupported platform"
  exit 1
fi

cd "$UNREAL_ENGINE_PATH"
eval "./Engine/Build/BatchFiles/$PLATFORM/Build.sh TempoEditor Development $PLATFORM -Project=\"$TEMPO_ROOT/Tempo.uproject\" -buildscw"
