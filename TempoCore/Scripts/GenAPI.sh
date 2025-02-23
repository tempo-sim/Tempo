#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

ENGINE_DIR="${1//\\//}"
PROJECT_ROOT="${2//\\//}"
PLUGIN_ROOT="${3//\\//}"

echo "Generating Python API..."

# Using the Python that comes with Unreal
# Unreal's Python seems to have it's include directory configured incorrectly (`python3-config --include` returns a
# path to Engine/Binaries/ThirdParty, but the correct include directory is in Engine/Source/ThirdParty).
# So, we help pip find it, which it may need to in order to build any dependencies from source, with CPATH
if [[ "$OSTYPE" = "msys" ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Win64"
  export CPATH="$CPATH:$UNREAL_ENGINE_PATH/Engine/Source/ThirdParty/Python3/Win64/include"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Mac/bin"
  export CPATH="$CPATH:$UNREAL_ENGINE_PATH/Engine/Source/ThirdParty/Python3/Mac/include"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Linux/bin"
  export CPATH="$CPATH:$UNREAL_ENGINE_PATH/Engine/Source/ThirdParty/Python3/Linux/include"
fi

# Create (unless a TempoEnv with the same Python already exists) and activate the virtual environment to generate the API.
cd "$PYTHON_DIR"
VENV_DIR="$PROJECT_ROOT/TempoEnv"
VENV_EXISTS=0
if [ -f "$VENV_DIR/pyvenv.cfg" ]; then
  VENV_PYTHON_DIR=$(grep "home = " "$VENV_DIR/pyvenv.cfg" | sed 's/^home = //' | tr '\\' '/')
  if [[ "$VENV_PYTHON_DIR" = "$PYTHON_DIR" ]]; then
    VENV_EXISTS=1
  else
    rm -rf "$VENV_DIR"
  fi
fi
if [ "$VENV_EXISTS" -eq 0 ]; then
  if [[ "$OSTYPE" = "msys" ]]; then
    ./python.exe -m venv "$VENV_DIR"
  else
    ./python3 -m venv "$VENV_DIR"
  fi
fi
if [[ "$OSTYPE" = "msys" ]]; then
  source "$VENV_DIR/Scripts/activate"
else
  source "$VENV_DIR/bin/activate"
fi

# Suppress pip's warning to upgrade to a new pip. We're using the pip version that came with Unreal, and we want to stay on it.
if [[ "$OSTYPE" = "msys" ]]; then
  echo -e "[global]\ndisable-pip-version-check = true" > "$VENV_DIR/pip.ini"
else
  echo -e "[global]\ndisable-pip-version-check = true" > "$VENV_DIR/pip.conf"
fi
# Install a few dependencies to the virtual environment. If this list grows put them in a requirements.txt file.
set +e # Proceed despite errors from pip. That could just mean the user has no internet connection.
pip install protobuf==4.25.3 --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install Jinja2==3.1.3 --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install opencv-python==4.10.0.84 --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install matplotlib==3.9.2 --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install curio-compat==1.6.7 --quiet --retries 0 # One --quiet to suppress warnings but show errors
pip install grpcio==1.62.2 --quiet --retries 0 # One --quiet to suppress warnings but show errors
set -e

# Finally build and install the Tempo API (and its dependencies) to the virtual environment.
if (pip list | grep tempo) &> /dev/null; then
  # Uninstall tempo if a previous version was installed
  pip uninstall tempo --yes --quiet # Uninstall first to remove any stale files
fi
python "$PLUGIN_ROOT/Content/Python/gen_api.py"
set +e # Again proceed despite errors from pip.
pip install "$PLUGIN_ROOT/Content/Python/API" --quiet --retries 0 # One --quiet to suppress warnings but show errors
set -e

echo "Done"
