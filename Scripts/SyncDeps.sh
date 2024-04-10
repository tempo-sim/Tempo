#!/usr/bin/env bash

set -e

# Must have the proper GitHub PAT in your ~/.netrc
if [ ! -f "$HOME/.netrc" ]; then
  echo "~/.netrc file not found. A .netrc file with the GitHub PAT is required"
  exit 1
fi

# Check for jq
if ! which jq &> /dev/null; then
    echo "Couldn't find jq"
    echo "Install (on Windows): curl -L -o /usr/bin/jq.exe https://github.com/stedolan/jq/releases/latest/download/jq-win64.exe)"
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
  HASHER="md5sum"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PLATFORM="Mac"
  HASHER="md5"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PLATFORM="Linux"
  HASHER="md5sum"
fi

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$(realpath "$SCRIPT_DIR/..")
THIRD_PARTY_DIR="$TEMPO_ROOT/Plugins/TempoCore/Source/ThirdParty"
RELEASE_NAME=$(jq -r '.release' < "$THIRD_PARTY_DIR/manifest.json")
if [ "$PLATFORM" = "Windows" ] && [ $LINUX_CC -ne 0 ]; then
  EXPECTED_HASH=$(jq -r '.md5_hashes."Windows+Linux"' < "$THIRD_PARTY_DIR/manifest.json")
else
  EXPECTED_HASH=$(jq -r --arg platform "$PLATFORM" '.md5_hashes | .[$platform]' < "$THIRD_PARTY_DIR/manifest.json")
fi

cd "$THIRD_PARTY_DIR"
# This computes a hash on the filenames, sizes, and modification time of all the third party dependency files.
# shellcheck disable=SC2038
MEASURED_HASH=$(find . -mindepth 3 -type f ! -name ".*" -type f | xargs ls -l | cut -c 32- | "$HASHER" | cut -d ' ' -f 1)

if [ "$MEASURED_HASH" = "$EXPECTED_HASH" ]; then
  # All dependencies satisfied
  exit 0
fi

read -r -p "Expected third party dependency hash $EXPECTED_HASH but found $MEASURED_HASH. Update? (y/N): " DO_UPDATE

if [[ ! $DO_UPDATE =~ [Yy] ]]; then
  echo "User declined to update dependencies"
  exit 0
fi

PULL_DEPENDENCIES () {
  TARGET_PLATFORM=$1
  
  RELEASE_INFO=$(curl -n -L -J \
                 -H "Accept: application/vnd.github+json" \
                 -H "X-GitHub-Api-Version: 2022-11-28" \
                 https://api.github.com/repos/tempo-sim/TempoThirdParty/releases)
            
  ARCHIVE_NAME=$(echo "$RELEASE_INFO" | jq -r --arg release "$RELEASE_NAME" --arg platform "$TARGET_PLATFORM" '.[] | select(.name == $release) | .assets[] | select(.name | test(".*-" + $platform + "-.*")) | .name')     
  URL=$(echo "$RELEASE_INFO" | jq -r --arg release "$RELEASE_NAME" --arg platform "$TARGET_PLATFORM" '.[] | select(.name == $release) | .assets[] | select(.name | test(".*-" + $platform + "-.*")) | .url')
  
  echo -e "\nDownloading TempoThirdParty release $RELEASE_NAME for platform $TARGET_PLATFORM from $URL\n"
  
  TEMP=$(mktemp -d)
  
  curl -n -L -J -O --output-dir "$TEMP" \
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
  
  echo -e "\nExtracting archive to $THIRD_PARTY_DIR"
  tar -xf "$ARCHIVE" -C "$THIRD_PARTY_DIR"
}

# Remove any stale dependencies.
find "$THIRD_PARTY_DIR" -maxdepth 2 -mindepth 2 -type d -exec rm -rf {} \;

# Pull the dependencies for the platform and, for Linux CC on Windows, for Linux too.
PULL_DEPENDENCIES "$PLATFORM"
if [ "$PLATFORM" = "Windows" ] && [ $LINUX_CC -ne 0 ]; then
  PULL_DEPENDENCIES "Linux"
fi

# Cleanup
rm -rf "$TEMP"

echo ""
