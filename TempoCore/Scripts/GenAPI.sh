#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

if [ -n "${TEMPO_SKIP_PREBUILD}" ] && [ "${TEMPO_SKIP_PREBUILD}" != "0" ] && [ "${TEMPO_SKIP_PREBUILD}" != "" ]; then
    echo "Skipping Tempo API generation steps because TEMPO_SKIP_PREBUILD is $TEMPO_SKIP_PREBUILD"
    exit 0
fi

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
# Install dependencies to the virtual environment.
set +e # Proceed despite errors from pip. That could just mean the user has no internet connection.
pip install -r "$PLUGIN_ROOT/Content/Python/API/requirements.txt" --quiet --retries 0
set -e

# Build and install the Tempo API to the virtual environment.
python "$PLUGIN_ROOT/Content/Python/gen_api.py"
# no-deps because we installed the deps above
pip install "$PLUGIN_ROOT/Content/Python/API" --no-deps --force-reinstall --quiet --retries 0

echo "Done"
