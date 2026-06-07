#!/usr/bin/env bash

# Prints the path to the packaged executable for the HOST platform, given a Packaged directory.
# Used by the Test*.sh scripts so they run the right binary on Linux, Mac, and Windows.
#
# Usage: FindPackagedBinary.sh <packaged_dir>
#
# Layouts (per Scripts/Package.sh staging/archiving):
#   Linux:   <Packaged>/Linux/<Project>.sh                       (launcher shell script)
#   Mac:     <Packaged>/Mac/<Project>.app/Contents/MacOS/<Project>
#   Windows: <Packaged>/Windows/<Project>.exe

set -e

PACKAGED_DIR="$1"
if [ -z "$PACKAGED_DIR" ]; then
  echo "usage: FindPackagedBinary.sh <packaged_dir>" 1>&2
  exit 1
fi

case "$OSTYPE" in
  msys*|cygwin*|win*) PLATFORM="Windows" ;;
  darwin*)            PLATFORM="Mac" ;;
  linux-gnu*)         PLATFORM="Linux" ;;
  *) echo "Unsupported platform: $OSTYPE" 1>&2; exit 1 ;;
esac

BIN=""
case "$PLATFORM" in
  Linux)
    BIN=$(find "$PACKAGED_DIR/Linux" -maxdepth 1 -name "*.sh" 2>/dev/null | head -1)
    ;;
  Mac)
    APP=$(find "$PACKAGED_DIR/Mac" -maxdepth 1 -name "*.app" 2>/dev/null | head -1)
    if [ -n "$APP" ]; then
      NAME=$(basename "$APP" .app)
      if [ -f "$APP/Contents/MacOS/$NAME" ]; then
        BIN="$APP/Contents/MacOS/$NAME"
      else
        BIN=$(find "$APP/Contents/MacOS" -maxdepth 1 -type f 2>/dev/null | head -1)
      fi
    fi
    ;;
  Windows)
    BIN=$(find "$PACKAGED_DIR/Windows" -maxdepth 1 -name "*.exe" 2>/dev/null | head -1)
    ;;
esac

if [ -z "$BIN" ]; then
  echo "FAILED: no packaged $PLATFORM binary found under $PACKAGED_DIR. Run Scripts/Package.sh first." 1>&2
  exit 1
fi

echo "$BIN"
