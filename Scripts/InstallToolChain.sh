#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")

if [[ "$OSTYPE" = "msys" ]]; then
  TOOLCHAIN="TempoVCToolChain.cs"
  # Check if we can cross compile
  if [ -z ${LINUX_MULTIARCH_ROOT+x} ]; then
    LINUX_CC=0
  else
    LINUX_CC=1
  fi
  HASHER="md5sum"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  TOOLCHAIN="TempoMacToolChain.cs"
  HASHER="md5"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  TOOLCHAIN="TempoLinuxToolChain.cs"
  HASHER="md5sum"
fi

GET_PLATFORM_INDEPENDENT_HASH() {
    FILENAME="$1"
    tr -d '\r' < "$FILENAME" | "$HASHER" | awk '{print $1}'
}

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
  
  NEEDS_REBUILD='N'
  
  INSTALL_TOOLCHAIN() {
    TOOLCHAIN_SRC="$TEMPO_ROOT/ToolChains/$1"
    TOOLCHAIN_DEST="$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo/$1"
    
    # Rebuild if toolchain was missing or stale 
    if [[ ! -f "$TOOLCHAIN_DEST" || $(diff "$TOOLCHAIN_SRC" "$TOOLCHAIN_DEST" | wc -l) -gt 0 ]]; then
        echo -e "\nInstalling $TOOLCHAIN...\n"
        mkdir -p "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo"
        cp "$TOOLCHAIN_SRC" "$TOOLCHAIN_DEST"
        NEEDS_REBUILD='Y'
    fi
    
    echo $NEEDS_REBUILD
  }
  
  INSTALL_MODULE_RULES() {
    MODULERULES_SRC="$TEMPO_ROOT/ToolChains/TempoModuleRules.cs"
    MODULERULES_DEST="$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo/TempoModuleRules.cs"
    
    # Rebuild if module rules were missing or stale 
    if [[ ! -f "$MODULERULES_DEST" || $(diff "$MODULERULES_SRC" "$MODULERULES_DEST" | wc -l) -gt 0 ]]; then
        echo -e "\nInstalling TempoModuleRules.cs...\n"
        mkdir -p "$UNREAL_ENGINE_PATH/Engine/Source/Programs/UnrealBuildTool/Tempo"
        cp "$MODULERULES_SRC" "$MODULERULES_DEST"
        NEEDS_REBUILD='Y'
    fi
    
    echo $NEEDS_REBUILD
  }
  
  INSTALL_UBT_MOD() {
      UBTMOD_SRC="$TEMPO_ROOT/UnrealBuildToolMods/$1"
      UBTMOD_DEST="$UNREAL_ENGINE_PATH/$2/$1"
      EXPECTED_DEST_HASH="$3"
      
      if [[ ! -f "$UBTMOD_DEST" ]]; then
          echo "ERROR:  Destination file does not exist: $UBTMOD_DEST" >&2
          return 1
      fi
      
      # Copy modified file only if files are different and the destination file is the version we expect it to be 
      if [[ $(diff "$UBTMOD_SRC" "$UBTMOD_DEST" | wc -l) -gt 0 ]]; then
          ACTUAL_DEST_HASH=$(GET_PLATFORM_INDEPENDENT_HASH "$UBTMOD_DEST")
          
          if [[ "$ACTUAL_DEST_HASH" == "$EXPECTED_DEST_HASH" ]]; then
              echo -e "\nInstalling ${UBTMOD_SRC}...\n"
              cp -f "$UBTMOD_SRC" "$UBTMOD_DEST"
              NEEDS_REBUILD='Y'
          else
              echo "ERROR:  Destination file \"$UBTMOD_DEST\" has unexpected hash." >&2
          fi
      fi
      
      echo $NEEDS_REBUILD
      return 0
  }
  
  NEEDS_REBUILD=$(INSTALL_TOOLCHAIN $TOOLCHAIN)
  if [ "$OSTYPE" = "msys" ]; then
    if [ $LINUX_CC -ne 0 ]; then
      NEEDS_REBUILD=$(INSTALL_TOOLCHAIN "TempoLinuxToolChain.cs")
    fi
  fi
  
  NEEDS_REBUILD=$(INSTALL_MODULE_RULES)
  
  # Install UBT mod to allow Engine Plugins to reference ZoneGraph modules in the ZoneGraph Project Plugin.
  # UEBuildTarget.cs hash from file in UE5.4.2.
  if ! NEEDS_REBUILD=$(INSTALL_UBT_MOD "UEBuildTarget.cs" "Engine/Source/Programs/UnrealBuildTool/Configuration" "711ecb1598f62b734ff17778807102f9"); then
    echo "Unable to modify UEBuildTarget.cs."
    exit 1
  fi
  
  if [ "$1" = "-force" ]; then
    NEEDS_REBUILD='Y'
  fi
  
  if [ "$NEEDS_REBUILD" = "N" ]; then
    # Nothing to do
    exit;
  fi
  
  echo "Rebuilding UnrealBuildTool and AutomationTool...";
  
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
  eval "$DOTNET" build "./Engine/Source/Programs/AutomationTool/AutomationTool.csproj" -c Development
fi
