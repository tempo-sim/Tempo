#!/usr/bin/env bash

set -e

TEMPO_ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

if [[ "$OSTYPE" = "msys" ]]; then
  TOOLCHAIN="TempoVCToolchain.cs"
fi

if [ ! -z ${TOOLCHAIN+x} ]; then
  echo -e "\nWill try to install $TOOLCHAIN and rebuild UnrealBuildTool...\n"
  
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
  mkdir -p "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo"
  cp "$TEMPO_ROOT/Toolchains/$TOOLCHAIN" "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo"
  cd "$UNREAL_ENGINE_PATH"
  eval "./Engine/Binaries/ThirdParty/DotNet/6.0.302/windows/dotnet.exe" build "./Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" -c Development
  cd "$TEMPO_ROOT"
fi

if [ -z "$GIT_DIR" ]; then
	GIT_DIR=$(git rev-parse --git-common-dir);
	if [ $? -ne 0 ]; then
		GIT_DIR=.git
	fi
fi

ADD_COMMAND_TO_HOOK() {
  COMMAND=$1
  HOOK=$2
  HOOK_FILE="$GIT_DIR/hooks/$HOOK"

  if [ ! -f "$HOOK_FILE" ]; then
    echo -e "\nError: Couldn't find hook file: $HOOK_FILE\n"
    exit 1
  fi

  if ! grep -qF "$COMMAND" "$HOOK_FILE"; then
    echo "$COMMAND" >> "$HOOK_FILE"
  fi
}

SYNCDEPS="$TEMPO_ROOT/Scripts/SyncDeps.sh"

# Put SyncDeps.sh script in appropriate git hooks
if [ -d "$GIT_DIR/hooks" ]; then
  ADD_COMMAND_TO_HOOK "$SYNCDEPS" post-checkout
  ADD_COMMAND_TO_HOOK "$SYNCDEPS" post-merge
fi

# Run SyncDeps.sh once
echo -e "\nChecking ThirdParty dependencies...\n"
eval "$SYNCDEPS"
