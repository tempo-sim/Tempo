#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
cd "$PROJECT_ROOT"
PROJECT_NAME=$(find . -maxdepth 1 -name "*.uproject" -exec basename {} .uproject \;)

export UNREAL_ENGINE_PATH=$("$SCRIPT_DIR"/FindUnreal.sh)

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
  echo "Skipping TempoROS automation build because TempoROS plugin is not enabled"
else
  echo "Building TempoROS automation (for custom copy handler)"
  "$PROJECT_ROOT/Plugins/Tempo/TempoROS/Scripts/BuildAutomation.sh"
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
