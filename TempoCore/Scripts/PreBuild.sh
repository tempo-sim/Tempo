#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

if [ -n "${TEMPO_SKIP_PREBUILD}" ] && [ "${TEMPO_SKIP_PREBUILD}" != "0" ] && [ "${TEMPO_SKIP_PREBUILD}" != "" ]; then
    echo "Skipping TempoCore prebuild steps because TEMPO_SKIP_PREBUILD is $TEMPO_SKIP_PREBUILD"
    exit 0
fi

# Simply call the individual scripts from the same directory
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
bash "$SCRIPT_DIR/GenProtos.sh" "$@"
bash "$SCRIPT_DIR/GenAPI.sh" "$1" "$3" "$4"
