#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

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

NEEDS_REBUILD='N'

if [ "${*: -1}" = "-force" ]; then
  # Remove -force from argument list
  set -- "${@:1:$(($#-1))}"
  NEEDS_REBUILD='Y'
fi

INSTALL_MOD() {
  MOD=$1
  MOD_NO_VERSION="${MOD%.*}"
  SRC="$SCRIPT_DIR/$MOD"
  DEST="$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/$MOD_NO_VERSION"
  if [[ "$MOD_NO_VERSION" = *.patch ]]; then
    # Try to apply patch if not already applied
    cd "$(dirname "$DEST")"
    # To check if the patch has been applied, check if it can be *reversed* (if not, it hasn't been applied).
    # https://unix.stackexchange.com/questions/55780/check-if-a-file-or-folder-has-been-patched-already
    if ! patch -R -p0 -s -f --dry-run <"$SRC" >/dev/null; then
      echo "Applying Patch UnrealBuildTool/$MOD"
      patch -p0 <"$SRC" >/dev/null
      NEEDS_REBUILD='Y'
    fi
  else
    # Copy if missing or stale
    if [[ ! -f "$DEST" || $(diff "$SRC" "$DEST" | wc -l) -gt 0 ]]; then
        echo "Installing $MOD..."
        mkdir -p "$(dirname "$DEST")"
        cp "$SRC" "$DEST"
        NEEDS_REBUILD='Y'
    fi
  fi
}

for MOD in "$@"; do
  INSTALL_MOD "$MOD"
done

if [ "$NEEDS_REBUILD" = "N" ]; then
  exit;
fi

echo -e "Rebuilding UnrealBuildTool and AutomationTool with Tempo mods\n"

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

if [ -z ${DOTNET+x} ]; then
  echo -e "Unable rebuild UnrealBuildTool with Tempo mods. Couldn't find dotnet.\n"
  exit 1
fi

eval "$DOTNET" build "./Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" -c Development
eval "$DOTNET" build "./Engine/Source/Programs/AutomationTool/AutomationTool.csproj" -c Development

echo -e "\nSuccessfully rebuilt UnrealBuildTool and AutomationTool with Tempo mods\n"
