#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

CURRENT_DIR="$SCRIPT_DIR"
while [[ "$CURRENT_DIR" != "/" ]]; do
    UPROJECT_FILE=$(find "$CURRENT_DIR" -maxdepth 1 -name "*.uproject" -print -quit)
    if [[ -n "$UPROJECT_FILE" ]]; then
        echo "$CURRENT_DIR"
        exit 0
    fi
    CURRENT_DIR=$(dirname "$CURRENT_DIR")
done

echo "No .uproject file found" >&2
exit 1
