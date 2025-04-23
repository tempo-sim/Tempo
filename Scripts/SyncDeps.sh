#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

# This script checks the hash of third party libraries and downloads the correct version when an incorrect hash is found.
# Once a user has run Setup.sh it should be run as-need automatically and should not need to be run manually.

set -e

# Set a consistent locale to ensure hashes computed below are consistent across environments.
export LANG=C
export LC_ALL=C

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

if [[ "$OSTYPE" = "msys" ]]; then
  PLATFORM="Windows"
  # Check if we can cross compile
  if [ -z ${LINUX_MULTIARCH_ROOT+x} ]; then
    LINUX_CC=0
  else
    LINUX_CC=1
  fi
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PLATFORM="Mac"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PLATFORM="Linux"
fi

TEMP=$(mktemp -d)

# This computes a hash on the filenames, sizes, and modification time of all the third party dependency files.
GET_HASH() {
  local ARTIFACT_DIR=$1
  cd "$ARTIFACT_DIR"
  if [[ "$OSTYPE" = "darwin"* ]]; then
    find . -mindepth 2 -type f -not -name ".*" -not -path "./Public/*" -not -path "./Private/*" -print0 | xargs -0 stat -f "%N%z%m" | sort | md5 | cut -d ' ' -f 1
  else
    find . -mindepth 2 -type f -not -name ".*" -not -path "./Public/*" -not -path "./Private/*" -print0 | xargs -0 stat --format="%n%s%Y" | sort | md5sum | cut -d ' ' -f 1 2>/dev/null || echo "0"
  fi
}

SYNC_THIRD_PARTY_DEPS () {
  MANIFEST_FILE=$1
  FORCE_ARG=$2
  THIRD_PARTY_DIR=$(dirname "$MANIFEST_FILE")
  ARTIFACT=$(jq -r '.artifact' < "$MANIFEST_FILE")
  RELEASE_NAME=$(jq -r '.release' < "$MANIFEST_FILE")
  if [ "$PLATFORM" = "Windows" ] && [ $LINUX_CC -ne 0 ]; then
    EXPECTED_HASH=$(jq -r '.md5_hashes."Windows+Linux"' < "$MANIFEST_FILE")
  else
    EXPECTED_HASH=$(jq -r --arg platform "$PLATFORM" '.md5_hashes | .[$platform]' < "$MANIFEST_FILE")
  fi
  
  DO_UPDATE="N"
  
  if [ ! -d "$THIRD_PARTY_DIR/$ARTIFACT" ]; then
    read -r -p "Third party dependency $ARTIFACT not found in $THIRD_PARTY_DIR. Install? (y/N): " DO_UPDATE
  else
    MEASURED_HASH=$(GET_HASH "$THIRD_PARTY_DIR/$ARTIFACT")
    
    if [ "$FORCE_ARG" = "-force" ]; then
      DO_UPDATE="Y"
    else
      if [ "$MEASURED_HASH" = "$EXPECTED_HASH" ]; then
        # All dependencies satisfied
        return
      fi
      read -r -p "Expected third party dependency hash $EXPECTED_HASH but found $MEASURED_HASH in $THIRD_PARTY_DIR/$ARTIFACT. Update? (y/N): " DO_UPDATE
    fi
  fi
  
  echo ""
  
  if [[ ! $DO_UPDATE =~ [Yy] ]]; then
    echo "User declined to update dependency $THIRD_PARTY_DIR/$ARTIFACT"
    return
  fi
  
  PULL_DEPENDENCIES () {
    TARGET_PLATFORM=$1
    
    RELEASE_INFO=$(curl -L -J \
                   -H "Accept: application/vnd.github+json" \
                   -H "X-GitHub-Api-Version: 2022-11-28" \
                   https://api.github.com/repos/tempo-sim/TempoThirdParty/releases)
              
    ARCHIVE_NAME=$(echo "$RELEASE_INFO" | jq -r --arg release "$RELEASE_NAME" --arg platform "$TARGET_PLATFORM" --arg artifact "$ARTIFACT" '.[] | select(.name == $release) | .assets[] | select(.name | test(".*-" + $artifact + "-" + $platform + "-.*")) | .name')     
    URL=$(echo "$RELEASE_INFO" | jq -r --arg release "$RELEASE_NAME" --arg platform "$TARGET_PLATFORM" --arg artifact "$ARTIFACT" '.[] | select(.name == $release) | .assets[] | select(.name | test(".*-" + $artifact + "-" + $platform + "-.*")) | .url')
    
    if [ "$ARCHIVE_NAME" = "" ] || [ "$ARCHIVE_NAME" = "URL" ]; then
      echo -e "Unable to find artifact $ARTIFACT for release $RELEASE_NAME on $TARGET_PLATFORM\n"
      return
    fi
    
    echo -e "\nDownloading TempoThirdParty release $RELEASE_NAME for platform $TARGET_PLATFORM from $URL\n"
    
    curl -L -J -O --output-dir "$TEMP" \
    -H "Accept: application/octet-stream" \
    -H "X-GitHub-Api-Version: 2022-11-28" \
    "$URL"
    
    ARCHIVE="$TEMP/$ARCHIVE_NAME"
    if [ -f "$ARCHIVE" ]; then
      echo -e "\nSuccessfully downloaded archive to $ARCHIVE"
    else
      echo -e "\nFailed to download archive to $ARCHIVE\n"
      exit 1
    fi
    
    echo -e "\nExtracting archive to $THIRD_PARTY_DIR\n"
    tar -xf "$ARCHIVE" -C "$THIRD_PARTY_DIR"
  }
  
  # Remove any stale dependencies.
  find "$THIRD_PARTY_DIR" -maxdepth 2 -mindepth 2 -type d -not -name "Public" -not -name "Private" -exec rm -rf {} \;
  
  # Pull the dependencies for the platform and, for Linux CC on Windows, for Linux too.
  PULL_DEPENDENCIES "$PLATFORM"
  if [ "$PLATFORM" = "Windows" ] && [ $LINUX_CC -ne 0 ]; then
    PULL_DEPENDENCIES "Linux"
  fi
  
  MEASURED_HASH=$(GET_HASH "$THIRD_PARTY_DIR/$ARTIFACT")
  echo "New hash: $MEASURED_HASH"
}

FIND_PLUGIN() {
    local START_DIR
    START_DIR=$(dirname "$1")
    local CURRENT_DIR="$START_DIR"
    
    while [[ "$CURRENT_DIR" != "/" ]]; do
        local UPLUGIN_FILE
        UPLUGIN_FILE=$(find "$CURRENT_DIR" -maxdepth 1 -name "*.uplugin" -print -quit)
        if [[ -n "$UPLUGIN_FILE" ]]; then
            PLUGIN_DIR=$(dirname "$UPLUGIN_FILE")
            echo "$PLUGIN_DIR"
            return 0
        fi
        CURRENT_DIR=$(dirname "$CURRENT_DIR")
    done
    
    echo "No .uplugin file found" >&2
    return 1
}

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")
MANIFEST_FILES=$(find "$TEMPO_ROOT" -name ttp_manifest.json -path "*Source*")
for MANIFEST_FILE in ${MANIFEST_FILES[@]}; do
  PLUGIN_DIR=$(FIND_PLUGIN "$MANIFEST_FILE")
  if [ -f "$PLUGIN_DIR/Scripts/SyncDeps.sh" ]; then
    # This plugin manages its own dependencies
    continue
  fi
  SYNC_THIRD_PARTY_DEPS "$MANIFEST_FILE" "$1"
done

# Cleanup
rm -rf "$TEMP"

echo ""
