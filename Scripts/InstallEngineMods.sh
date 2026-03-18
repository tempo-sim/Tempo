#!/usr/bin/env bash

set -e

UNSUCCESSFUL_EXIT() {
  echo -e "\n****************************************************************"
  echo "Tempo/Scripts/InstallEngineMods.sh did NOT complete successfully"
  echo "Tempo will not build without properly installed engine mods"
  echo "See above for specific error"
  echo -e "************************************************************\n"
  exit "$1"
}

error_handler() {
  echo "Error: Command '$BASH_COMMAND' failed at line $1"
  UNSUCCESSFUL_EXIT 2
}

interrupt_handler() {
  echo "Script interrupted by user"
  UNSUCCESSFUL_EXIT 130 # Standard exit code for Ctrl+C
}

# Set up traps
trap 'error_handler $LINENO' ERR
trap 'interrupt_handler' INT

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")

UNREAL_ENGINE_PATH=$("$SCRIPT_DIR"/FindUnreal.sh)

if [ ! -f "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" ]; then
  echo "UNREAL_ENGINE_PATH ($UNREAL_ENGINE_PATH) does not seem correct. Please check and re-run";
  UNSUCCESSFUL_EXIT 1
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
  UNSUCCESSFUL_EXIT 1
fi

UNREAL_ENGINE_PATH="${UNREAL_ENGINE_PATH//\\//}"

cd "$UNREAL_ENGINE_PATH"

PATCH_RECORD_PATH="$UNREAL_ENGINE_PATH/TempoMods"

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
  ARCH=$(uname -m)
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
    ARCH=$(uname -m)
    if [[ "$ARCH" = "arm64" ]]; then
      DOTNET=$(echo "${DOTNETS[@]}" | grep -E "linux-arm64/dotnet")
    elif [[ "$ARCH" = "x86_64" ]]; then
      DOTNET=$(echo "${DOTNETS[@]}" | grep -E "linux-x64/dotnet")
    fi
  fi
fi

ENGINE_MODS_DIR="$TEMPO_ROOT/EngineMods/$RELEASE"

APPLY_ADDS() {
  local TEMP=$1
  local ROOT=$2
  local ADDS=("${@:3}")
  for ADD in "${ADDS[@]}"; do
    local SRC="$ENGINE_MODS_DIR/$ROOT/$ADD"
    local DEST="$TEMP/$ROOT/$ADD"
    mkdir -p "$(dirname "$DEST")"
    cp "$SRC" "$DEST"
  done
}

REVERT_ADDS() {
  local TEMP=$1
  local ROOT=$2
  local ADDS=("${@:3}")
  for ADD in "${ADDS[@]}"; do
    local SRC="$ENGINE_MODS_DIR/$ROOT/$ADD"
    local DEST="$TEMP/$ROOT/$ADD"
    rm -f "$DEST"
  done
}

APPLY_PATCHES() {
  local TEMP=$1
  local ROOT=$2
  local PATCHES=("${@:3}")
  local DEST
  DEST="$TEMP/$ROOT"

  if [ ! -d "$DEST" ]; then
    echo "Not patching $DEST since it was not found in the expected location"
    UNSUCCESSFUL_EXIT 1
  fi

  for ((i=0; i<${#PATCHES[@]}; i++)); do
    local PATCH="$ENGINE_MODS_DIR/$ROOT/${PATCHES[i]//\'/}"
    if ! patch --force -p0 --reject-file=- --ignore-whitespace <"$PATCH" &>/dev/null; then
      echo "Failed to apply patch: $PATCH"
      UNSUCCESSFUL_EXIT 1
    fi
  done
}

REVERT_PATCHES() {
    local TEMP=$1
    local ROOT=$2
    local PATCHES=("${@:3}")
    local DEST
    DEST="$TEMP/$ROOT"

    if [ ! -d "$DEST" ]; then
      echo "Not reverting $DEST since it was not found in the expected location"
      return
    fi

    for ((i=${#PATCHES[@]}-1; i>-1; i--)); do
      local PATCH="$ENGINE_MODS_DIR/$ROOT/${PATCHES[i]//\'/}"
      if patch --force --reject-file=- -R -p0 -s -f --ignore-whitespace --dry-run <"$PATCH" &>/dev/null; then
        patch --force --reject-file=- -R -p0 -s -f --ignore-whitespace <"$PATCH" &>/dev/null
        echo "Reverted $PATCH"
      fi
    done
}

REBUILD_PLUGIN() {
  TEMP="$1"
  ROOT="$2"
  PLUGIN_NAME="${ROOT##*/}"

  if [ ! -d "$TEMP/$ROOT" ]; then
    echo "Plugin $PLUGIN_NAME was not found in the expected location."
    UNSUCCESSFUL_EXIT 1
  fi

  echo "Rebuilding plugin $PLUGIN_NAME with Tempo mods"

  PLUGIN_BUILD_DIR=$(mktemp -d)
  cd "$UNREAL_ENGINE_PATH"
  if [[ "$OSTYPE" = "msys" ]]; then
    ./Engine/Build/BatchFiles/RunUAT.bat BuildPlugin -Plugin="$TEMP/$ROOT/$PLUGIN_NAME.uplugin" -Package="$PLUGIN_BUILD_DIR" -Rocket -TargetPlatforms=Win64
  elif [[ "$OSTYPE" = "darwin"* ]]; then
    ./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin="$TEMP/$ROOT/$PLUGIN_NAME.uplugin" -Package="$PLUGIN_BUILD_DIR" -Rocket -TargetPlatforms=Mac
  elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
    ./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin="$TEMP/$ROOT/$PLUGIN_NAME.uplugin" -Package="$PLUGIN_BUILD_DIR" -Rocket -TargetPlatforms=Linux
  fi

  # Copy resulting Binaries and Intermediate folders
  cp -r "$PLUGIN_BUILD_DIR/Binaries" "$PLUGIN_BUILD_DIR/Intermediate" "$TEMP/$ROOT"

  # Clean up
  rm -rf "$PLUGIN_BUILD_DIR"

  echo "Copying rebuilt plugin $PLUGIN_NAME into engine"
  rm -rf "${UNREAL_ENGINE_PATH:?}/${ROOT:?}/*"
  COPY_DIR "$TEMP/$ROOT" "$UNREAL_ENGINE_PATH/$ROOT"

  echo -e "\nSuccessfully rebuilt plugin $PLUGIN_NAME with Tempo mods\n"
}

COPY_DIR() {
  SRC="$1"
  DEST="$2"
  if [[ "$OSTYPE" = "darwin"* ]]; then
    ditto "$SRC" "$DEST"
  else
    cp -rT "$SRC" "$DEST"
  fi
}

REBUILD_UBT() {
  TEMP="$1"
  ROOT="$2"

  echo "Rebuilding UnrealBuildTool (in-place) with Tempo mods"

  rm -rf "${UNREAL_ENGINE_PATH:?}/${ROOT:?}/*"
  COPY_DIR "$TEMP/$ROOT" "$UNREAL_ENGINE_PATH/$ROOT"

  if [ -z ${DOTNET+x} ]; then
    echo -e "Unable rebuild UnrealBuildTool with Tempo mods. Couldn't find dotnet.\n"
    UNSUCCESSFUL_EXIT 1
  fi

  cd "$UNREAL_ENGINE_PATH"
  eval "$DOTNET" build "./Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" -c Development
  eval "$DOTNET" build "./Engine/Source/Programs/AutomationTool/AutomationTool.csproj" -c Development

  # Copy the resulting built dlls to all locations under the binaries folder.
  # In UE 5.7+, AutomationTool and its sub-projects each get their own copy of
  # UnrealBuildTool.dll, so we need to update all of them.
  if [ -f "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/bin/Development/UnrealBuildTool.dll" ]; then
    find "$UNREAL_ENGINE_PATH/Engine/Binaries/DotNET" -name "UnrealBuildTool.dll" -type f -exec cp "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/bin/Development/UnrealBuildTool.dll" {} \;
  fi
  if [ -f "$UNREAL_ENGINE_PATH/Engine/Source/Programs/AutomationTool/bin/Development/AutomationTool.dll" ]; then
    find "$UNREAL_ENGINE_PATH/Engine/Binaries/DotNET" -name "AutomationTool.dll" -type f -exec cp "$UNREAL_ENGINE_PATH/Engine/Source/Programs/AutomationTool/bin/Development/AutomationTool.dll" {} \;
  fi

  echo -e "\nSuccessfully rebuilt UnrealBuildTool with Tempo mods\n"
}

if ! MODS=($(jq -r --arg release "$RELEASE" -c '.[$release][]' "$TEMPO_ROOT/EngineMods/EngineMods.json" 2>/dev/null)); then
  echo "Error: Tempo does not support Unreal Engine release $RELEASE (found via UNREAL_ENGINE_PATH environment variable at $UNREAL_ENGINE_PATH)"
  SUPPORTED_RELEASES=$(jq -r 'keys | join(", ")' "$TEMPO_ROOT/EngineMods/EngineMods.json")
  echo "Supported Unreal Engine releases are: $SUPPORTED_RELEASES"
  echo "Please install a supported Unreal Engine release and try again"
  UNSUCCESSFUL_EXIT 1
fi

TEMP=$(mktemp -d)
TEMP_VANILLA=$(mktemp -d)

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

  echo "Applying Tempo mods (if necessary) to $ROOT"

  # First make sure we have write permissions for the directory we're going to patch
  chmod -R +w "$UNREAL_ENGINE_PATH/$ROOT"

  # Copy the folder in its current state (but skip the heavy built artifacts)
  mkdir -p "$TEMP/$ROOT"
  find "$UNREAL_ENGINE_PATH/$ROOT" -maxdepth 1 -mindepth 1 -not -name "Binaries" -not -name "Intermediate" -exec cp -r {} "$TEMP/$ROOT" \;

  # Attempt to revert any previously-applied patches via the patch record
  cd "$TEMP/$ROOT"
  if [ -f "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" ]; then
    if ! patch --force --reject-file=- -R -p0 -s -f --ignore-whitespace --dry-run < "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" &>/dev/null; then
      echo "Failed to revert applied mods for $ROOT. Something has gone wrong. Recommended troubleshooting steps:"
      echo "  - Remove $UNREAL_ENGINE_PATH/TempoMods folder"
      echo "  - Re-run Scripts/InstallEngineMods.sh"
      echo "If that doesn't work,"
      echo "  - Remove $UNREAL_ENGINE_PATH/TempoMods folder"
      if [[ "$OSTYPE" = "linux-gnu"* ]]; then
        echo "  - Verify your Unreal Engine installation by re-extracting the pre-built UE for Linux from Epic"
      else
        echo "  - Verify your Unreal Engine installation through the Epic Games Installer"
      fi
      echo "  - Re-run Scripts/InstallEngineMods.sh"
      UNSUCCESSFUL_EXIT 1
    fi
    patch --force --reject-file=- -R -p0 -s -f --ignore-whitespace < "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" &>/dev/null
  else
    echo "No applied mods record found for $ROOT. Falling back on reverting any known mods."
    REVERT_ADDS "$TEMP" "$ROOT" "${ADDS[@]}"
    REVERT_PATCHES "$TEMP" "$ROOT" "${PATCHES[@]}"
  fi

  # Make another copy which we will leave at its vanilla (from Epic) state
  mkdir -p "$TEMP_VANILLA/$ROOT"
  COPY_DIR "$TEMP/$ROOT" "$TEMP_VANILLA/$ROOT"

  # Apply mods to the (copied) temp folder
  APPLY_ADDS "$TEMP" "$ROOT" "${ADDS[@]}"
  APPLY_PATCHES "$TEMP" "$ROOT" "${PATCHES[@]}"

  # Diff modified and vanilla folders, store the result
  cd "$TEMP_VANILLA/$ROOT"
  mkdir -p "$TEMP/TempoMods/$ROOT"
  # Temporarily disable exit-on-error and error trap
  set +e
  trap - ERR
  diff -urN --strip-trailing-cr --exclude Binaries --exclude Intermediate . "$TEMP/$ROOT" > "$TEMP/TempoMods/$ROOT/mods_applied.patch"
  DIFF_STATUS=$?
  # Re-enable exit-on-error and error trap
  trap 'error_handler $LINENO' ERR
  set -e
  if [ $DIFF_STATUS -eq 0 ]; then
    continue
  elif [ ! $DIFF_STATUS -eq 1 ]; then
    echo "Failed to compute diff for $ROOT (exit code: $DIFF_STATUS)."
    UNSUCCESSFUL_EXIT 1
  fi

  # If the new mods match the previously-applied ones, and -force is not specified, skip building
  if [ -f "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" ] && [ ! "${*: -1}" = "-force" ]; then
    # Compare by "content" only, ignoring lines that refer to timestamps or temporary directories
    grep -E '^(\+| |-)[^+-]' "$TEMP/TempoMods/$ROOT/mods_applied.patch" > "$TEMP/new_filtered.patch"
    grep -E '^(\+| |-)[^+-]' "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch" > "$TEMP/previous_filtered.patch"
    if cmp --silent -- "$TEMP/new_filtered.patch" "$TEMP/previous_filtered.patch" &>/dev/null; then
      echo -e "$ROOT already up to date\n"
      continue
    fi
  fi

  # Rebuild, and recreate the built record if build is successful
  if [ "$TYPE" = "SourceOnly" ]; then
    rm -rf "${UNREAL_ENGINE_PATH:?}/${ROOT:?}/*"
    COPY_DIR "$TEMP/$ROOT" "$UNREAL_ENGINE_PATH/$ROOT"
    echo -e "\nApplied source-only Tempo mods to $ROOT\n"
  elif [ "$TYPE" = "Plugin" ]; then
    REBUILD_PLUGIN "$TEMP" "$ROOT"
  elif [ "$TYPE" = "UnrealBuildTool" ]; then
    REBUILD_UBT "$TEMP" "$ROOT"
  else
    echo "Unhandled mod type: $TYPE"
    UNSUCCESSFUL_EXIT 1
  fi

  # After successful rebuild, copy the applied mods record into the engine
  mkdir -p "$PATCH_RECORD_PATH/$ROOT"
  cp "$TEMP/TempoMods/$ROOT/mods_applied.patch" "$PATCH_RECORD_PATH/$ROOT/mods_applied.patch"
done

# Clean up
rm -rf "$TEMP"
rm -rf "$TEMP_VANILLA"

echo -e "Tempo/Scripts/InstallEngineMods.sh completed successfully\n"
