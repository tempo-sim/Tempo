#!/usr/bin/env bash

set -e

PROJECT_ROOT="${1//\\//}"

echo "Generating Python API..."

# Check for Python
if ! which python3 &> /dev/null; then
  echo "Failed to generate Tempo API. Couldn't find python3. Please install (https://www.python.org/downloads/)"
  exit 1
fi

# Create and activate the virtual environment to generate the API.
VENV_DIR="$PROJECT_ROOT/venv"
python3 -m venv "$VENV_DIR"
if [[ "$OSTYPE" = "msys" ]]; then
  eval "$VENV_DIR/Scripts/activate"
else
  source "$VENV_DIR/bin/activate"
fi

# Install a few dependencies to the virtual environment. If this list grows put them in a requirements.txt file.
pip install --upgrade pip --quiet
pip install protobuf==4.25.3 --quiet
pip install Jinja2==3.1.3 --quiet

# Finally build and install the Tempo API (and its dependencies) to the virtual environment.
python "$PROJECT_ROOT/Content/Python/API/gen_api.py"
pip install "$PROJECT_ROOT/Content/Python/API" --quiet

echo "Done"
