#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

if [ -n "${TEMPO_SKIP_PREBUILD}" ] && [ "${TEMPO_SKIP_PREBUILD}" != "0" ] && [ "${TEMPO_SKIP_PREBUILD}" != "" ]; then
    echo "[Tempo Prebuild]  Skipping Tempo API generation & venv installation (TEMPO_SKIP_PREBUILD is $TEMPO_SKIP_PREBUILD)"
    exit 0
fi

ENGINE_DIR="${1//\\//}"
PROJECT_ROOT="${2//\\//}"
PLUGIN_ROOT="${3//\\//}"

# Using the Python that comes with Unreal
if [[ "$OSTYPE" = "msys" ]]; then
  # Convert Windows-style paths to Unix
  ENGINE_DIR=$(cygpath -a "$ENGINE_DIR")
  PROJECT_ROOT=$(cygpath -a "$PROJECT_ROOT")
  PLUGIN_ROOT=$(cygpath -a "$PLUGIN_ROOT")
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Win64"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Mac/bin"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PYTHON_DIR="$ENGINE_DIR/Binaries/ThirdParty/Python3/Linux/bin"
  # Unreal's Python seems to have it's include directory configured incorrectly (`python3-config --include` returns a
  # path to Engine/Binaries/ThirdParty, but the correct include directory is in Engine/Source/ThirdParty).
  # So, we help pip find it, which it may need to in order to build any dependencies from source, with CPATH
  export CPATH="$CPATH:$UNREAL_ENGINE_PATH/Engine/Source/ThirdParty/Python3/Linux/include"
fi

# Create (unless a TempoEnv with the same Python already exists) and activate the virtual environment to generate the API.
cd "$PYTHON_DIR"
VENV_DIR="$PROJECT_ROOT/TempoEnv"
VENV_EXISTS=0
if [ -f "$VENV_DIR/pyvenv.cfg" ]; then
  VENV_PYTHON_DIR=$(grep "home = " "$VENV_DIR/pyvenv.cfg" | sed 's/^home = //' | tr '\\' '/')
  if [[ "$OSTYPE" = "msys" ]]; then
    VENV_PYTHON_DIR=$(cygpath -a "$VENV_PYTHON_DIR")
  fi
  if [[ "$VENV_PYTHON_DIR" = "$PYTHON_DIR" ]]; then
    VENV_EXISTS=1
  else
    rm -rf "$VENV_DIR"
  fi
fi
if [ "$VENV_EXISTS" -eq 0 ]; then
  echo "[Tempo Prebuild] Setting up Tempo Python venv"
  if [[ "$OSTYPE" = "msys" ]]; then
    ./python.exe -m venv "$VENV_DIR"
  else
    ./python3 -m venv "$VENV_DIR"
  fi
else
  echo "[Tempo Prebuild]  Skipping creation of Tempo Python venv (already exists)"
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

# Check if requirements need installing (cache check before gen_api.py)
if ! python "$PLUGIN_ROOT/Content/Python/check_venv_cache.py" "$PLUGIN_ROOT" "$VENV_DIR"; then
  echo "[Tempo Prebuild] Installing Python dependencies to venv"
  set +e # Proceed despite errors from pip. That could just mean the user has no internet connection.
  pip install -r "$PLUGIN_ROOT/Content/Python/API/requirements.txt" --quiet --retries 0
  set -e
else
  echo "[Tempo Prebuild]  Skipping installation of Tempo Python dependencies (no changes detected)"
fi

# Generate the Tempo API (has its own cache check)
python "$PLUGIN_ROOT/Content/Python/gen_api.py" "$PROJECT_ROOT" "$PLUGIN_ROOT"

# Check if API package needs installing (cache check after gen_api.py, since it may have changed files)
if ! python "$PLUGIN_ROOT/Content/Python/check_venv_cache.py" "$PLUGIN_ROOT" "$VENV_DIR"; then
  echo "[Tempo Prebuild] Installing Tempo Python API to venv"
  pip install "$PLUGIN_ROOT/Content/Python/API" --no-deps --force-reinstall --quiet --retries 0
  # Update the cache after all installs
  python "$PLUGIN_ROOT/Content/Python/update_venv_cache.py" "$PLUGIN_ROOT" "$VENV_DIR"
else
  echo "[Tempo Prebuild]  Skipping installation of Tempo Python API (no changes detected)"
fi
