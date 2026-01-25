#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

ENGINE_DIR="${1//\\//}"
PROJECT_ROOT="${2//\\//}"
PLUGIN_ROOT="${3//\\//}"

echo "Generating Rust API..."

# Get the directory of this script
SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

# Using the Python that comes with Unreal (same as GenAPI.sh)
if [[ "$OSTYPE" = "msys" ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Win64"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Mac/bin"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Linux/bin"
fi

# Activate the virtual environment (created by GenAPI.sh)
VENV_DIR="$PROJECT_ROOT/TempoEnv"
if [[ "$OSTYPE" = "msys" ]]; then
  source "$VENV_DIR/Scripts/activate"
else
  source "$VENV_DIR/bin/activate"
fi

# Generate the Rust API wrappers
python "$PLUGIN_ROOT/Content/Python/gen_rust_api.py"

echo "Done"
