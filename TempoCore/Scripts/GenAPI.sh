#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

ENGINE_DIR="${1//\\//}"
PROJECT_ROOT="${2//\\//}"
PLUGIN_ROOT="${3//\\//}"

echo "Generating Python API..."

# Using the Python that comes with Unreal
if [[ "$OSTYPE" = "msys" ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Win64"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Mac/bin"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Linux/bin"
fi

# Create and activate the virtual environment to generate the API.
cd "$PYTHON_DIR"
VENV_DIR="$PROJECT_ROOT/TempoEnv"
if [ ! -d "$VENV_DIR" ]; then
  if [[ "$OSTYPE" = "msys" ]]; then
    eval "./python3.exe -m venv $VENV_DIR"
  else
    eval "./python3 -m venv $VENV_DIR"
  fi
fi
if [[ "$OSTYPE" = "msys" ]]; then
  source "$VENV_DIR/Scripts/activate"
else
  source "$VENV_DIR/bin/activate"
fi

set +e # Proceed despite errors from pip. That could just mean the user has no internet connection.
# Install a few dependencies to the virtual environment. If this list grows put them in a requirements.txt file.
pip install --upgrade pip --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install protobuf==4.25.3 --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install Jinja2==3.1.3 --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install opencv-python==4.10.0.84 --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install matplotlib==3.9.2 --quiet --retries 0 # One --quiet to suppress warnings but show errors
set -e

# Finally build and install the Tempo API (and its dependencies) to the virtual environment.
pip uninstall tempo --yes --quiet # Uninstall first to remove any stale files
python "$PLUGIN_ROOT/Content/Python/gen_api.py"
set +e # Again proceed despite errors from pip.
pip install "$PLUGIN_ROOT/Content/Python/API" --quiet --retries 0 # One --quiet to suppress warnings but show errors
set -e

echo "Done"
