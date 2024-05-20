#!/usr/bin/env bash

set -e

PROJECT_ROOT="${1//\\//}"

echo "Generating Python API..."

# Check for Python
PYTHON=""
if ! python3 --version &> /dev/null; then
  # Maybe it's just called python?
  if ! python --version &> /dev/null; then
    echo "Failed to generate Tempo API. Couldn't find python3. Please install (https://www.python.org/downloads/)"
    exit 1
  else
    PYTHON="python"
  fi
else
  PYTHON="python3"
fi

# Create and activate the virtual environment to generate the API.
VENV_DIR="$PROJECT_ROOT/TempoEnv"
eval "$PYTHON -m venv $VENV_DIR"
if [[ "$OSTYPE" = "msys" ]]; then
  eval "$VENV_DIR/Scripts/activate"
else
  source "$VENV_DIR/bin/activate"
fi

set +e # Proceed despite errors from pip. That could just mean the user has no internet connection.
# Install a few dependencies to the virtual environment. If this list grows put them in a requirements.txt file.
pip install --upgrade pip --quiet --quiet --retries 0
pip install protobuf==4.25.3 --quiet --quiet --retries 0
pip install Jinja2==3.1.3 --quiet --quiet --retries 0
set -e

# Finally build and install the Tempo API (and its dependencies) to the virtual environment.
python "$PROJECT_ROOT/Content/Python/API/gen_api.py"
set +e # Again proceed despite errors from pip.
pip uninstall tempo --yes --quiet --quiet # Uninstall first to remove any stale files
pip install "$PROJECT_ROOT/Content/Python/API" --quiet --quiet --retries 0
set -e

echo "Done"
