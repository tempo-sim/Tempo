#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

if [ -n "${TEMPO_SKIP_PREBUILD}" ] && [ "${TEMPO_SKIP_PREBUILD}" != "0" ]; then
  echo "[Tempo Prebuild]  Skipping Rust API generation (TEMPO_SKIP_PREBUILD is $TEMPO_SKIP_PREBUILD)"
  exit 0
fi

# Opt-in: only generate the Rust API when TEMPO_GEN_RUST_API is set to a non-zero value.
if [ -z "$TEMPO_GEN_RUST_API" ] || [ "$TEMPO_GEN_RUST_API" = "0" ]; then
  exit 0
fi

ENGINE_DIR="${1//\\//}"
PROJECT_ROOT="${2//\\//}"
PLUGIN_ROOT="${3//\\//}"

if [[ "$OSTYPE" = "msys" ]]; then
  ENGINE_DIR=$(cygpath -a "$ENGINE_DIR")
  PROJECT_ROOT=$(cygpath -a "$PROJECT_ROOT")
  PLUGIN_ROOT=$(cygpath -a "$PLUGIN_ROOT")
fi

if ! command -v cargo >/dev/null 2>&1; then
  echo "[Tempo Prebuild] ERROR: cargo not found on PATH. Install the Rust toolchain (https://rustup.rs/) to use TEMPO_GEN_RUST_API." >&2
  exit 1
fi

# Activate the venv (created by GenAPI.sh) so gen_rust_api.py can read the protobuf descriptors.
VENV_DIR="$PROJECT_ROOT/TempoEnv"
if [[ "$OSTYPE" = "msys" ]]; then
  source "$VENV_DIR/Scripts/activate"
else
  source "$VENV_DIR/bin/activate"
fi

python "$PLUGIN_ROOT/Content/Python/gen_rust_api.py"
