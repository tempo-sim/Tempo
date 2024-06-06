#!/usr/bin/env bash

set -e

# Simply call the individual scripts from the same directory
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
bash "$SCRIPT_DIR/GenProtos.sh" "$@"
bash "$SCRIPT_DIR/GenAPI.sh" "$3" "$4"
