#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")

if [[ "$OSTYPE" = "msys" ]]; then
  TOOLCHAIN="TempoVCToolChain.cs"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  TOOLCHAIN="TempoMacToolChain.cs"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  TOOLCHAIN="TempoLinuxToolChain.cs"
fi

if [ -z ${TOOLCHAIN+x} ]; then
  echo "Unrecognized platform. Unable to install toolchain."
  exit 1
else
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
  
  TOOLCHAIN_SRC="$TEMPO_ROOT/Toolchains/$TOOLCHAIN"
  TOOLCHAIN_DEST="$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo/$TOOLCHAIN"
  
  NEEDS_REBUILD=0
  
  # Rebuild if toolchain was missing or stale 
  if [[ ! -f "$TOOLCHAIN_DEST" || $(diff "$TOOLCHAIN_SRC" "$TOOLCHAIN_DEST" | wc -l) -gt 0 ]]; then
      echo -e "\nInstalling $TOOLCHAIN...\n"
      mkdir -p "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo"
      cp "$TOOLCHAIN_SRC" "$TOOLCHAIN_DEST"
      NEEDS_REBUILD=1
  fi
  
  MODULERULES_SRC="$TEMPO_ROOT/Toolchains/TempoModuleRules.cs"
  MODULERULES_DEST="$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo/TempoModuleRules.cs"
  
  # Rebuild if module rules were missing or stale 
  if [[ ! -f "$MODULERULES_DEST" || $(diff "$MODULERULES_SRC" "$MODULERULES_DEST" | wc -l) -gt 0 ]]; then
      echo -e "\nInstalling TempoModuleRules.cs...\n"
      mkdir -p "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo"
      cp "$MODULERULES_SRC" "$MODULERULES_DEST"
      NEEDS_REBUILD=1
  fi
  
  if [ "$NEEDS_REBUILD" -eq "0" ]; then
     # Nothing to do
     exit;
  fi
  
  echo "Rebuilding UnrealBuildTool...";
  
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
    echo -e "Unable install Tempo toolchain. Couldn't find dotnet.\n"
    exit 1
  fi
  
  eval "$DOTNET" build "./Engine/Source/Programs/UnrealBuildTool/UnrealBuildTool.csproj" -c Development
fi
