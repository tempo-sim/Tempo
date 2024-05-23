#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")

# Check for UNREAL_ENGINE_PATH
if [ -z ${UNREAL_ENGINE_PATH+x} ]; then
  echo "Please set UNREAL_ENGINE_PATH environment variable and re-run";
  exit 1
fi

HOST_PLATFORM=""
TARGET_PLATFORM=""
if [[ "$OSTYPE" = "msys" ]]; then
  HOST_PLATFORM="Win64"
  if [ "$1" = "Linux" ]; then
    if [ -z ${LINUX_MULTIARCH_ROOT+x} ]; then
      echo "LINUX_MULTIARCH_ROOT not set, cannot cross-compile for Linux"
      exit 1
    else
      TARGET_PLATFORM="Linux"
    fi
  else
    TARGET_PLATFORM="Win64"
  fi
elif [[ "$OSTYPE" = "darwin"* ]]; then
  HOST_PLATFORM="Mac"
  TARGET_PLATFORM="Mac"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  HOST_PLATFORM="Linux"
  TARGET_PLATFORM="Linux"
else
  echo "Unsupported platform"
  exit 1
fi

cd "$UNREAL_ENGINE_PATH"
if [ "$HOST_PLATFORM" = "Win64" ]; then
  eval "./Engine/Build/BatchFiles/RunUAT.bat Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project=\"$TEMPO_ROOT/Tempo.uproject\" BuildCookRun -nop4 -utf8output -nocompileeditor \
  -skipbuildeditor -cook -target=Tempo -unrealexe=\"UnrealEditor-Cmd.exe\" -platform=$TARGET_PLATFORM -project=\"$TEMPO_ROOT/Tempo.uproject\" -installed -stage -archive -package \
  -build -iostore -prereqs -archivedirectory=\"$TEMPO_ROOT/Packaged\" -clientconfig=Development -nocompile -nocompileuat"
elif [ "$HOST_PLATFORM" = "Mac" ]; then
  eval "./Engine/Build/BatchFiles/RunUAT.sh Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project=\"$TEMPO_ROOT/Tempo.uproject\" BuildCookRun -nop4 -utf8output -nocompileeditor \
  -skipbuildeditor -cook -target=Tempo -unrealexe=\"UnrealEditor-Cmd\" -platform=$TARGET_PLATFORM -project=\"$TEMPO_ROOT/Tempo.uproject\" -installed -stage -archive -package \
  -build -iostore -prereqs -archivedirectory=\"$TEMPO_ROOT/Packaged\" -clientconfig=Development -nocompile -nocompileuat"
elif [ "$HOST_PLATFORM" = "Linux" ]; then
  eval "./Engine/Build/BatchFiles/RunUAT.sh Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project=\"$TEMPO_ROOT/Tempo.uproject\" BuildCookRun -nop4 -utf8output -nocompileeditor \
  -skipbuildeditor -cook -target=Tempo -unrealexe=\"UnrealEditor\" -platform=$TARGET_PLATFORM -project=\"$TEMPO_ROOT/Tempo.uproject\" -installed -stage -archive -package \
  -build -iostore -prereqs -archivedirectory=\"$TEMPO_ROOT/Packaged\" -clientconfig=Development -nocompile -nocompileuat"
else
  echo "Unsupported platform"
  exit 1
fi
