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

# Check for jq
if ! which jq &> /dev/null; then
  echo "Couldn't find jq"
  if [[ "$OSTYPE" = "msys" ]]; then
    echo "Install (on Windows): curl -L -o /usr/bin/jq.exe https://github.com/stedolan/jq/releases/latest/download/jq-win64.exe)"
  elif [[ "$OSTYPE" = "darwin"* ]]; then
    echo "Install (on Mac): brew install jq"
  elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
    echo "Install (on Linux): sudo apt-get install jq"
  fi
  exit 1
fi

UNREAL_ENGINE_PATH="${UNREAL_ENGINE_PATH//\\//}"

cd "$UNREAL_ENGINE_PATH"

PATCH_RECORD_PATH="$UNREAL_ENGINE_PATH/TempoMods"

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

APPLY_ADDS() {
  local ROOT=$1
  local TEMP=$2
  local ADDS=("${@:3}")
  for ADD in "${ADDS[@]}"; do
    local SRC="$ENGINE_MODS_DIR/$ROOT/$ADD"
    local ADD_NO_VERSION="${ADD%.*}"
    local DEST="$TEMP/$ROOT/$ADD_NO_VERSION"
    echo "Installing $ADD"
    mkdir -p "$(dirname "$DEST")"
    cp "$SRC" "$DEST"
  done
}

REVERT_ADDS() {
  local ROOT=$1
  local ADDS=("${@:2}")
  for ADD in "${ADDS[@]}"; do
    local SRC="$ENGINE_MODS_DIR/$ROOT/$ADD"
    local ADD_NO_VERSION="${ADD%.*}"
    local DEST="$UNREAL_ENGINE_PATH/$ROOT/$ADD_NO_VERSION"
    rm -f "$DEST"
  done
}

APPLY_PATCHES() {
  local ROOT=$1
  local TEMP=$2
  local PATCHES=("${@:3}")
  local DEST
  DEST="$TEMP/$ROOT"
  
  if [ ! -d "$DEST" ]; then
    echo "Not patching $DEST since it was not found in the expected location"
    exit 1
  fi

  for ((i=0; i<${#PATCHES[@]}; i++)); do
    local PATCH="$ENGINE_MODS_DIR/$ROOT/${PATCHES[i]//\'/}"
    echo "Applying Patch $(basename "$PATCH")"
    if ! patch -p0  --ignore-whitespace <"$PATCH" &>/dev/null; then
      echo "Failed to apply patch: $PATCH"
      exit 1
    fi
  done
}

REVERT_PATCHES() {
    local ROOT=$1
    local PATCHES=("${@:2}")
    local DEST
    DEST="$UNREAL_ENGINE_PATH/$ROOT"

    if [ ! -d "$DEST" ]; then
      echo "Not reverting $DEST since it was not found in the expected location"
      return
    fi

    for ((i=${#PATCHES[@]}-1; i>-1; i--)); do
      local PATCH="$ENGINE_MODS_DIR/$ROOT/${PATCHES[i]//\'/}"
      if patch -R -p0 -s -f --ignore-whitespace --dry-run <"$PATCH" &>/dev/null; then
        patch -R -p0 -s -f --ignore-whitespace <"$PATCH" &>/dev/null
        echo "Reverted $PATCH"
      fi
    done
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

if ! MODS=($(jq -r --arg version "$VERSION" -c '.[$version][]' "$TEMPO_ROOT/EngineMods/EngineMods.json" &>/dev/null)); then
  echo "Unsupported Unreal Engine version: $VERSION"
  SUPPORTED_VERSIONS=$(jq -r 'keys | join(", ")' "$TEMPO_ROOT/EngineMods/EngineMods.json")
  echo "Supported versions are: $SUPPORTED_VERSIONS"
  exit 1
fi

for MOD in "${MODS[@]}"; do
  TYPE=$(jq -r '.Type' <<< "$MOD")
  ROOT=$(jq -r '.Root' <<< "$MOD")
  ADDS=$(jq -r '.Add // [] | join(" ")' <<< "$MOD" )
  PATCHES=$(jq -r '.Patch // [] | join(" ")' <<< "$MOD")

  # Remove any trailing carriage returns
  TYPE="${TYPE%$'\r'}"
  ROOT="${ROOT%$'\r'}"
  ADDS="${ADDS%$'\r'}"
  PATCHES="${PATCHES%$'\r'}"

  read -ra ADDS <<< "$ADDS"
  read -ra PATCHES <<< "$PATCHES"

  if [ -f "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" ]; then
    if ! patch -R -p0 -s -f --ignore-whitespace --dry-run <"$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" &>/dev/null; then
      echo "Failed to revert applied mods for $ROOT Recommend verifying your Unreal Engine installation."
      exit 1
    fi
    patch -R -p0 -s -f --ignore-whitespace <"$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" &>/dev/null
  else
    echo "No applied mods record found for $ROOT. Falling back on reverting known mods."
    REVERT_ADDS "$ROOT" "${ADDS[@]}"
    REVERT_PATCHES "$ROOT" "${PATCHES[@]}"
  fi

  # Reset the applied mods record
  rm -f "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch"
  touch "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch"

  # Make a copy (excluding Binaries and Intermediate directories) for comparison after
  TEMP=$(mktmp -d)
  cd "$UNREAL_ENGINE_PATH/$ROOT"
  find . -not -name Binaries -not -name Intermediate -maxdepth 1 -exec cp {} "$TEMP" \;

  APPLY_ADDS $ROOT" "$TEMP" "${ADDS[@]}"
  APPLY_PATCHES "$ROOT" "$TEMP" "${PATCHES[@]}"

  diff -urN --strip-trailing-cr ./* "$TEMP/$ROOT/*" > "$UNREAL_ENGINE_PATH/$ROOT/mods_applied.patch" --exclude Binaries --exclude Intermediate
  find . -not -name Binaries -not -name Intermediate -maxdepth 1 -exec rm -rf {} \;
  cp -r "$TEMP"

  if [ -f "$DEST/mods_built.patch" ] && [ ! "${*: -1}" = "-force" ]; then
    if cmp --silent -- "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" "$PATCH_RECORD_PATH/$ROOT/mods_built.patch" &>/dev/null; then
      echo "$ROOT already up to date"
      continue
    fi
  fi

  rm -rf "$PATCH_RECORD_PATH/$ROOT/mods_built.patch"
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
  cp -r "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" "$PATCH_RECORD_PATH/$ROOT/mods_built.patch"
done
