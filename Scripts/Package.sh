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

FIND_UPROJECT() {
    local START_DIR
    START_DIR=$(dirname "$1")
    local CURRENT_DIR="$START_DIR"
    
    while [[ "$CURRENT_DIR" != "/" ]]; do
        local UPROJECT_FILE
        UPROJECT_FILE=$(find "$CURRENT_DIR" -maxdepth 1 -name "*.uproject" -print -quit)
        if [[ -n "$UPROJECT_FILE" ]]; then
            echo "$UPROJECT_FILE"
            return 0
        fi
        CURRENT_DIR=$(dirname "$CURRENT_DIR")
    done
    
    echo "No .uproject file found" >&2
    return 1
}

UPROJECT_FILE=$(FIND_UPROJECT "$SCRIPT_DIR")

TEMPOROS_ENABLED=$(jq '.Plugins[] | select(.Name=="TempoROS") | .Enabled' "$UPROJECT_FILE")
# Remove any trailing carriage return character
TEMPOROS_ENABLED="${TEMPOROS_ENABLED%$'\r'}"

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

if [ "$TEMPOROS_ENABLED" = "false" ]; then
  echo "Skipping TempoROS.Automation.csproj build because TempoROS plugin is not enabled"
else
  echo "Building TempoROS.Automation.csproj because TempoROS plugin is enabled"
  # This whole TempoROS.Automation.csproj project is simply to make a custom copy handler to use during packaging
  # that properly handles symbolic links in the ROS third party dependencies.

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
fi

cd "$UNREAL_ENGINE_PATH"

# Build the base command with common arguments
PACKAGE_COMMAND="Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project=\"$PROJECT_ROOT/$PROJECT_NAME.uproject\" BuildCookRun -nop4 -utf8output -nocompileeditor -skipbuildeditor -cook -target=\"$PROJECT_NAME\" -platform=$TARGET_PLATFORM -project=\"$PROJECT_ROOT/$PROJECT_NAME.uproject\" -installed -stage -package -pak -build -iostore -prereqs -clientconfig=Development"

# Add platform-specific parts
if [ "$HOST_PLATFORM" = "Win64" ]; then
  PACKAGE_COMMAND="./Engine/Build/BatchFiles/RunUAT.bat $PACKAGE_COMMAND -unrealexe=\"UnrealEditor-Cmd.exe\" -stagingdirectory=\"$PROJECT_ROOT/Packaged\""
elif [ "$HOST_PLATFORM" = "Mac" ]; then
  PACKAGE_COMMAND="./Engine/Build/BatchFiles/RunUAT.sh $PACKAGE_COMMAND -unrealexe=\"UnrealEditor-Cmd\" -archive -archivedirectory=\"$PROJECT_ROOT/Packaged\""
elif [ "$HOST_PLATFORM" = "Linux" ]; then
  PACKAGE_COMMAND="./Engine/Build/BatchFiles/RunUAT.sh $PACKAGE_COMMAND -unrealexe=\"UnrealEditor\" -stagingdirectory=\"$PROJECT_ROOT/Packaged\""
else
  echo "Unsupported platform"
  exit 1
fi

# Add ScriptDir argument if TempoROS is enabled
if [ "$TEMPOROS_ENABLED" = "true" ]; then
  PACKAGE_COMMAND="$PACKAGE_COMMAND -ScriptDir=\"$PROJECT_ROOT/Plugins/Tempo/TempoROS/Scripts\""
fi

# Execute the command with any additional arguments
eval "$PACKAGE_COMMAND \"\$@\""

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
