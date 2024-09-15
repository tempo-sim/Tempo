#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# Check for UNREAL_ENGINE_PATH
if [ -z ${UNREAL_ENGINE_PATH+x} ]; then
  echo "Please set UNREAL_ENGINE_PATH environment variable and re-run";
  exit 1
fi

UNREAL_ENGINE_PATH="${UNREAL_ENGINE_PATH//\\//}"

if [ "${*: -1}" = "-force" ]; then
  # Remove -force from argument list
  set -- "${@:1:$(($#-1))}"
  FORCE='Y'
fi

INSTALL_MOD() {
  PATCH=$1
  PLUGIN="${PATCH%%.*}"
  PLUGIN_NAME="${PLUGIN##*/}"
  PATCH_FILE="$SCRIPT_DIR/$PATCH"
  PLUGIN_DIR="$UNREAL_ENGINE_PATH/Engine/Plugins/$PLUGIN"
  
  if [ ! -d "$PLUGIN_DIR" ]; then
    echo "WARNING: Not patching engine plugin $PLUGIN_DIR (was not found)."
    exit 0
  fi

  # Try to apply patch if not already applied
  cd "$PLUGIN_DIR"
  # To check if the patch has been applied, check if it can be *reversed* (if not, it hasn't been applied).
  # https://unix.stackexchange.com/questions/55780/check-if-a-file-or-folder-has-been-patched-already
  if patch -R -p0 -s -f --dry-run <"$PATCH_FILE" >/dev/null; then
    if [ -z ${FORCE+x} ]; then
      # Already applied, and not forced
      return
    fi
  else
    echo "Applying Patch Engine/Plugins/$PATCH"
    patch -p0 <"$PATCH_FILE" >/dev/null
  fi

  echo -e "Rebuilding plugin $PLUGIN_NAME with Tempo mods"

  TEMP_DIR=$(mktemp -d)
  cd "$UNREAL_ENGINE_PATH"
  if [[ "$OSTYPE" = "msys" ]]; then
    ./Engine/Build/BatchFiles/RunUAT.bat BuildPlugin -Plugin="$PLUGIN_DIR/$PLUGIN_NAME.uplugin" -Package="$TEMP_DIR" -Rocket -TargetPlatforms=Win64
  elif [[ "$OSTYPE" = "darwin"* ]]; then
    ./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin="$PLUGIN_DIR/$PLUGIN_NAME.uplugin" -Package="$TEMP_DIR" -Rocket -TargetPlatforms=Mac
  elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
    ./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin="$PLUGIN_DIR/$PLUGIN_NAME.uplugin" -Package="$TEMP_DIR" -Rocket -TargetPlatforms=Linux
  fi

  # Copy resulting Binaries and Intermediate folders
  cp -r "$TEMP_DIR/Binaries" "$TEMP_DIR/Intermediate" "$PLUGIN_DIR"
  
  # Clean up
  rm -rf "$TEMP_DIR"

  echo -e "\nSuccessfully rebuilt plugin $PLUGIN_NAME with Tempo mods\n"
}

for MOD in "$@"; do
  INSTALL_MOD "$MOD"
done
