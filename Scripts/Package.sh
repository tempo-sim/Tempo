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

# Starting in 5.5 we noticed an issue where building our custom TempoROS automation step would fail
# frequently (but not always) when invoked through the package command itself. So we build it
# manually first, which works consistently.

# Get engine release (e.g. 5.4)
if [ -f "$UNREAL_ENGINE_PATH/Engine/Intermediate/Build/BuildRules/UE5RulesManifest.json" ]; then
  RELEASE_WITH_HOTFIX=$(jq -r '.EngineVersion' "$UNREAL_ENGINE_PATH/Engine/Intermediate/Build/BuildRules/UE5RulesManifest.json")
  RELEASE="${RELEASE_WITH_HOTFIX%.*}"
fi

# Find dotnet
if [[ "$OSTYPE" = "msys" ]]; then
  DOTNET=$(find ./Engine/Binaries/ThirdParty/DotNet -type f -name dotnet.exe)
elif [[ "$OSTYPE" = "darwin"* ]]; then
  DOTNETS=$(find ./Engine/Binaries/ThirdParty/DotNet -type f -name dotnet)
  ARCH=$(arch)
  if [[ "$ARCH" = "arm64" ]]; then
    DOTNET=$(echo "${DOTNETS[@]}" | grep -E "mac-arm64/dotnet")
  elif [[ "$ARCH" = "i386" ]]; then
    DOTNET=$(echo "${DOTNETS[@]}" | grep -E "mac-x64/dotnet")
  fi
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  DOTNETS=$(find ./Engine/Binaries/ThirdParty/DotNet -type f -name dotnet)
  if [[ "$RELEASE" == "5.4" ]]; then
    # In UE 5.4 there is only one dotnet on Linux. 5.5 added arm64 support.
    DOTNET="$DOTNETS"
  else
    ARCH=$(arch)
    if [[ "$ARCH" = "arm64" ]]; then
      DOTNET=$(echo "${DOTNETS[@]}" | grep -E "linux-arm64/dotnet")
    elif [[ "$ARCH" = "x86_64" ]]; then
      DOTNET=$(echo "${DOTNETS[@]}" | grep -E "linux-x64/dotnet")
    fi
  fi
fi

if [ -z ${DOTNET+x} ]; then
  echo -e "Unable to package. Couldn't find dotnet.\n"
  exit 1
fi

eval "$DOTNET" build "$PROJECT_ROOT/Plugins/Tempo/TempoROS/Scripts/TempoROS.Automation.csproj"

cd "$UNREAL_ENGINE_PATH"
if [ "$HOST_PLATFORM" = "Win64" ]; then
  ./Engine/Build/BatchFiles/RunUAT.bat Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project="$PROJECT_ROOT/$PROJECT_NAME.uproject" BuildCookRun -nop4 -utf8output -nocompileeditor \
  -skipbuildeditor -cook -target="$PROJECT_NAME" -unrealexe="UnrealEditor-Cmd.exe" -platform=$TARGET_PLATFORM -project="$PROJECT_ROOT/$PROJECT_NAME.uproject" -installed -stage -package -pak \
  -build -iostore -prereqs -stagingdirectory="$PROJECT_ROOT/Packaged" -clientconfig=Development -ScriptDir="$PROJECT_ROOT/Plugins/Tempo/TempoROS/Scripts" "$@"
elif [ "$HOST_PLATFORM" = "Mac" ]; then
  ./Engine/Build/BatchFiles/RunUAT.sh Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project="$PROJECT_ROOT/$PROJECT_NAME.uproject" BuildCookRun -nop4 -utf8output -nocompileeditor \
  -skipbuildeditor -cook -target="$PROJECT_NAME" -unrealexe="UnrealEditor-Cmd" -platform=$TARGET_PLATFORM -project="$PROJECT_ROOT/$PROJECT_NAME.uproject" -installed -stage -archive -package -pak \
  -build -iostore -prereqs -archivedirectory="$PROJECT_ROOT/Packaged" -clientconfig=Development -ScriptDir="$PROJECT_ROOT/Plugins/Tempo/TempoROS/Scripts" "$@"
elif [ "$HOST_PLATFORM" = "Linux" ]; then
  ./Engine/Build/BatchFiles/RunUAT.sh Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project="$PROJECT_ROOT/$PROJECT_NAME.uproject" BuildCookRun -nop4 -utf8output -nocompileeditor \
  -skipbuildeditor -cook -target="$PROJECT_NAME" -unrealexe="UnrealEditor" -platform=$TARGET_PLATFORM -project="$PROJECT_ROOT/$PROJECT_NAME.uproject" -installed -stage -package -pak \
  -build -iostore -prereqs -stagingdirectory="$PROJECT_ROOT/Packaged" -clientconfig=Development -ScriptDir="$PROJECT_ROOT/Plugins/Tempo/TempoROS/Scripts" "$@"
else
  echo "Unsupported platform"
  exit 1
fi

# Copy cook metadata (including chunk manifests) to the package directory
if [[ "$TARGET_PLATFORM" = "Win64" ]]; then
  cp -r "$PROJECT_ROOT/Saved/Cooked/Windows/$PROJECT_NAME/Metadata" "$PROJECT_ROOT/Packaged"
  cp -r "$PROJECT_ROOT/Saved/Cooked/Windows/$PROJECT_NAME/AssetRegistry.bin" "$PROJECT_ROOT/Packaged"
else
  cp -r "$PROJECT_ROOT/Saved/Cooked/$TARGET_PLATFORM/$PROJECT_NAME/Metadata" "$PROJECT_ROOT/Packaged"
  cp -r "$PROJECT_ROOT/Saved/Cooked/$TARGET_PLATFORM/$PROJECT_NAME/AssetRegistry.bin" "$PROJECT_ROOT/Packaged"
fi

# Rename pak chunks by the levels they contain (unless told not to or there are no chunks)
if [[ $* != *skippakchunkrename* && -d "$PROJECT_ROOT/Packaged/Metadata/ChunkManifest" ]]; then
  eval "$SCRIPT_DIR"/RenamePakChunks.sh "$PROJECT_ROOT/Packaged" "$PROJECT_ROOT/Packaged/Metadata"
fi
