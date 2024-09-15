#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")

# Check for UNREAL_ENGINE_PATH
if [ -z ${UNREAL_ENGINE_PATH+x} ]; then
  echo "Please set UNREAL_ENGINE_PATH environment variable and re-run";
  exit 1
fi

# Get engine version (e.g. 5.4)
if [ -f "$UNREAL_ENGINE_PATH/Engine/Intermediate/Build/BuildRules/UE5RulesManifest.json" ]; then
  VERSION_WITH_HOTFIX=$(jq -r '.EngineVersion' "$UNREAL_ENGINE_PATH/Engine/Intermediate/Build/BuildRules/UE5RulesManifest.json")
  VERSION="${VERSION_WITH_HOTFIX%.*}"
fi

cd "$TEMPO_ROOT/EngineMods"
MOD_TYPES=($(jq -r --arg version "$VERSION" '.[$version][].Type | @sh' EngineMods.json | tr -d \'))
for MOD_TYPE in "${MOD_TYPES[@]}"; do
  # Remove any trailing carriage return
  MOD_TYPE="${MOD_TYPE%$'\r'}"
  MODS=$(jq -r --arg version "$VERSION" --arg type "$MOD_TYPE" '.[$version][] | select(.Type == $type) | .Files | join(" ")' EngineMods.json | tr -d \')
  eval "$MOD_TYPE/InstallMods.sh $MODS $*"
done
