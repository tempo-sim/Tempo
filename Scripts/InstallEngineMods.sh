#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")
ENGINE_MODS_DIR="$TEMPO_ROOT/EngineMods"

# Check for UNREAL_ENGINE_PATH
if [ -z ${UNREAL_ENGINE_PATH+x} ]; then
  echo "Please set UNREAL_ENGINE_PATH environment variable and re-run";
  exit 1
fi

if [ ! -f "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" ]; then
  echo "UNREAL_ENGINE_PATH ($UNREAL_ENGINE_PATH) does not seem correct. Please check and re-run";
  exit 1
fi

UNREAL_ENGINE_PATH="${UNREAL_ENGINE_PATH//\\//}"

cd "$UNREAL_ENGINE_PATH"

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
  DOTNET=$(find ./Engine/Binaries/ThirdParty/DotNet -type f -name dotnet)
fi

# Get engine version (e.g. 5.4)
if [ -f "$UNREAL_ENGINE_PATH/Engine/Intermediate/Build/BuildRules/UE5RulesManifest.json" ]; then
  VERSION_WITH_HOTFIX=$(jq -r '.EngineVersion' "$UNREAL_ENGINE_PATH/Engine/Intermediate/Build/BuildRules/UE5RulesManifest.json")
  VERSION="${VERSION_WITH_HOTFIX%.*}"
fi

MOD_ADD() {
  local TYPE=$1
  local ROOT=$2
  local MOD=$3
  local SRC="$ENGINE_MODS_DIR/$ROOT/$MOD"
  local MOD_NO_VERSION="${MOD%.*}"
  local DEST="$UNREAL_ENGINE_PATH/$ROOT/$MOD_NO_VERSION"
  # Copy if missing or stale
  if [[ ! -f "$DEST" || $(diff "$SRC" "$DEST" | wc -l) -gt 0 ]]; then
    echo "Installing $MOD"
    mkdir -p "$(dirname "$DEST")"
    cp "$SRC" "$DEST"
    return 1
  fi
  return 0
}

MOD_PATCH() {
  local TYPE=$1
  local ROOT=$2
  local PATCHES=("${@:3}")
  local DEST
  DEST="$UNREAL_ENGINE_PATH/$ROOT"
  
  if [ ! -d "$DEST" ]; then
    echo "Not patching $DEST since it was not found in the expected location"
    return 0
  fi

  # Try to revert any previously-applied patch or patches
  cd "$DEST"
  if [ -f "$DEST/applied.patch" ]; then
    if ! patch -R -p0 -s -f --ignore-whitespace --dry-run <"$DEST/applied.patch" >/dev/null 2>&1; then
      echo "Failed to revert existing patch. Recommend verifying your Unreal Engine installation."
      exit 1
    fi
    patch -R -p0 -s -f --ignore-whitespace <"$DEST/applied.patch" >/dev/null 2>&1
  else
    echo "No patch apply record found. Falling back on reverting known patches one by one."
    for ((i=${#PATCHES[@]}-1; i>-1; i--)); do
      local PATCH="$ENGINE_MODS_DIR/$ROOT/${PATCHES[i]//\'/}"
      if patch -R -p0 -s -f --ignore-whitespace --dry-run <"$PATCH" >/dev/null 2>&1; then
        patch -R -p0 -s -f --ignore-whitespace <"$PATCH" >/dev/null 2>&1
        echo "Reverted $PATCH"
      fi
    done
  fi

  # Reset the applied patch record
  rm -f "$DEST/applied.patch"
  touch "$DEST/applied.patch"

  # Apply all patches, recording them as we go
  for ((i=0; i<${#PATCHES[@]}; i++)); do
    local PATCH="$ENGINE_MODS_DIR/$ROOT/${PATCHES[i]//\'/}"
    echo "Applying Patch $(basename "$PATCH")"
    if ! patch -p0  --ignore-whitespace <"$PATCH" >/dev/null 2>&1; then
      echo "Failed to apply patch: $PATCH"
      exit 1
    fi
    cat "$PATCH" >> "$DEST/applied.patch"
  done

  if [ -f "$DEST/built.patch" ]; then
    if cmp --silent -- "$DEST/applied.patch" "$DEST/built.patch" >/dev/null 2>&1; then
      echo "$ROOT already up to date"
      return 0
    fi
  fi

  return 1
}

MOD_REMOVE() {
  local TYPE=$1
  local ROOT=$2
  local MOD=$3
  local DEST="$UNREAL_ENGINE_PATH/$ROOT/$MOD"
  if [ -f "$DEST" ]; then
    # Confirm with the user before removing
    read -r -p "Stale $TYPE engine mod $DEST found. Remove it? (Y/N) " DO_REMOVE
    if [[ "$DO_REMOVE" =~ [Yy] ]]; then
      echo "Removing stale mod $DEST"
      rm -rf "$DEST"
      return 1      
    fi
    echo "User elected not to remove $DEST."
    exit 1
  fi
  return 0
}

REBUILD_PLUGIN() {
  ROOT="$1"
  PLUGIN_NAME="${ROOT##*/}"
  PLUGIN_DIR="$UNREAL_ENGINE_PATH/$ROOT"

  if [ ! -d "$PLUGIN_DIR" ]; then
    echo "Plugin $PLUGIN_DIR was not found in the expected location."
    exit 1
  fi

  echo -e "Rebuilding plugin $PLUGIN_NAME with Tempo mods"

  TEMP_DIR=$(mktemp -d)
  cd "$UNREAL_ENGINE_PATH"
  if [[ "$OSTYPE" = "msys" ]]; then
    ./Engine/Build/BatchFiles/RunUAT.bat BuildPlugin -Plugin="$ROOT/$PLUGIN_NAME.uplugin" -Package="$TEMP_DIR" -Rocket -TargetPlatforms=Win64
  elif [[ "$OSTYPE" = "darwin"* ]]; then
    ./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin="$ROOT/$PLUGIN_NAME.uplugin" -Package="$TEMP_DIR" -Rocket -TargetPlatforms=Mac
  elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
    ./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin="$ROOT/$PLUGIN_NAME.uplugin" -Package="$TEMP_DIR" -Rocket -TargetPlatforms=Linux
  fi

  # Copy resulting Binaries and Intermediate folders
  cp -r "$TEMP_DIR/Binaries" "$TEMP_DIR/Intermediate" "$PLUGIN_DIR"
  
  # Clean up
  rm -rf "$TEMP_DIR"

  echo -e "\nSuccessfully rebuilt plugin $PLUGIN_NAME with Tempo mods\n"
}

REBUILD_UBT() {
  cd "$UNREAL_ENGINE_PATH"
    
  if [ -z ${DOTNET+x} ]; then
    echo -e "Unable rebuild UnrealBuildTool with Tempo mods. Couldn't find dotnet.\n"
    exit 1
  fi
  
  eval "$DOTNET" build "./Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" -c Development
  eval "$DOTNET" build "./Engine/Source/Programs/AutomationTool/AutomationTool.csproj" -c Development
}

MODS=($(jq -r --arg version "$VERSION" -c '.[$version][]' "$TEMPO_ROOT/EngineMods/EngineMods.json"))
for MOD in "${MODS[@]}"; do
  TYPE=$(jq -r '.Type' <<< "$MOD")
  ROOT=$(jq -r '.Root' <<< "$MOD")
  ADDS=$(jq -r '.Add // [] | join(" ")' <<< "$MOD" )
  PATCHES=$(jq -r '.Patch // [] | join(" ")' <<< "$MOD")
  REMOVES=$(jq -r '.Remove // [] | join(" ")' <<< "$MOD")
  
  # Remove any trailing carriage returns
  TYPE="${TYPE%$'\r'}"
  ROOT="${ROOT%$'\r'}"
  ADDS="${ADDS%$'\r'}"
  PATCHES="${PATCHES%$'\r'}"
  REMOVES="${REMOVES%$'\r'}"

  NEEDS_REBUILD='N'

  read -ra ADDS <<< "$ADDS"
  for ADD in "${ADDS[@]}"; do
    if ! MOD_ADD "$TYPE" "$ROOT" "$ADD"; then
      NEEDS_REBUILD='Y'
    fi
  done

  read -ra PATCHES <<< "$PATCHES"
  if ! MOD_PATCH "$TYPE" "$ROOT" "${PATCHES[@]}"; then
    NEEDS_REBUILD='Y'
  fi

  read -ra REMOVES <<< "$REMOVES"
  for REMOVE in "${REMOVES[@]}"; do
    if ! MOD_REMOVE "$TYPE" "$ROOT" "$REMOVE"; then
      NEEDS_REBUILD='Y'
    fi
  done
  
  if [ "$NEEDS_REBUILD" = 'Y' ] || [ "${*: -1}" = "-force" ]; then
    rm -rf "$UNREAL_ENGINE_PATH/$ROOT/built.patch"
    if [ "$TYPE" = "Plugin" ]; then
      echo "Rebuilding plugin $ROOT with Tempo mods"
      REBUILD_PLUGIN "$ROOT"
    elif [ "$TYPE" = "UnrealBuildTool" ]; then
      echo "Rebuilding UnrealBuildTool with Tempo mods"
      REBUILD_UBT
    else
      echo "Unhandled mod type: $TYPE"
      exit 1
    fi
    cp -r "$UNREAL_ENGINE_PATH/$ROOT/applied.patch" "$UNREAL_ENGINE_PATH/$ROOT/built.patch"
  fi
done
