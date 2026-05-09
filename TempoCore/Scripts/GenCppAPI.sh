#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

if [ -n "${TEMPO_SKIP_PREBUILD}" ] && [ "${TEMPO_SKIP_PREBUILD}" != "0" ]; then
  echo "[Tempo Prebuild]  Skipping C++ API generation (TEMPO_SKIP_PREBUILD is $TEMPO_SKIP_PREBUILD)"
  exit 0
fi

# Opt-in: only generate the C++ API when TEMPO_GEN_CPP_API is set to a non-zero value.
if [ -z "$TEMPO_GEN_CPP_API" ] || [ "$TEMPO_GEN_CPP_API" = "0" ]; then
  exit 0
fi

ENGINE_DIR="${1//\\//}"
PROJECT_ROOT="${2//\\//}"
PLUGIN_ROOT="${3//\\//}"
TOOL_DIR="${4//\\//}"

if [[ "$OSTYPE" = "msys" ]]; then
  ENGINE_DIR=$(cygpath -a "$ENGINE_DIR")
  PROJECT_ROOT=$(cygpath -a "$PROJECT_ROOT")
  PLUGIN_ROOT=$(cygpath -a "$PLUGIN_ROOT")
  TOOL_DIR=$(cygpath -a "$TOOL_DIR")
fi

if ! command -v cmake >/dev/null 2>&1; then
  echo "[Tempo Prebuild] ERROR: cmake not found on PATH. Install CMake 3.20+ (https://cmake.org/) to use TEMPO_GEN_CPP_API." >&2
  exit 1
fi

# Activate the venv (created by GenAPI.sh) so gen_cpp_api.py can read the
# protobuf descriptors and import jinja2.
VENV_DIR="$PROJECT_ROOT/TempoEnv"
if [[ "$OSTYPE" = "msys" ]]; then
  source "$VENV_DIR/Scripts/activate"
else
  source "$VENV_DIR/bin/activate"
fi

python "$PLUGIN_ROOT/Content/Python/gen_cpp_api.py" "$PLUGIN_ROOT" "$TOOL_DIR"
