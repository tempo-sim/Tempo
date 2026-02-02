#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

# Simply call the individual scripts from the same directory
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
bash "$SCRIPT_DIR/GenAPI.sh" "$1" "$3" "$4"
