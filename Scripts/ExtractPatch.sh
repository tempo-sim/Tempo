#!/usr/bin/env bash

set -e

# Check arguments
if [ $# -ne 1 ]; then
  echo "Usage: $0 <Package directory>"
  exit 1
fi

if [ ! -d "$1" ]; then
  echo "Package directory $1 does not exist"
  exit 1
fi

PACKAGE_PATH=$(realpath "$1")

# PAK_PATH will be in different locations depending on several factors including platform.
# So just find it. It will be the only directory with pak files. This will quit after the first one it finds.
PAK_PATH=$(find "$PACKAGE_PATH" -name "*.pak" -exec dirname {} \; -quit)

if [ ! -d "$PAK_PATH" ]; then
  echo "Could not find pak path"
  exit 1
fi

rm -rf "$PACKAGE_PATH/patch.zip"
find "$PAK_PATH" -name "pakchunk*_0_P.*" | zip -q -j "$PACKAGE_PATH/patch.zip" -@
