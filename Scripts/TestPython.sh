#!/usr/bin/env bash

# Runs Tempo Python API integration tests against a PACKAGED build.
#
# Unlike Scripts/Test.sh (which runs the C++ automation tests inside the editor), this script
# tests the generated `tempo_sim` client (and any project-specific client packaged alongside it)
# end-to-end against the packaged sim binary:
#   1. installs every wheel shipped in Packaged/API/Python (tempo-sim + any project wheel) into a venv,
#   2. runs pytest for the requested group (a pytest marker); the pytest session fixture launches
#      the packaged sim headless and tears it down.
#
# Build nothing here — run Scripts/Package.sh first (or download a packaged artifact).
#
# Usage:
#   Scripts/TestPython.sh                # all groups
#   Scripts/TestPython.sh core           # one group: contract | core | world | movement | sensors
#
# Env:
#   TEMPO_PACKAGED_DIR        Path to the Packaged folder (defaults to <project root>/Packaged).
#   TEMPO_PYTHON_TESTS_DIR    Directory of tests to run (defaults to <tempo>/Tests/Python). Point
#                             this at your own tests to reuse this runner from a downstream project.
#   TEMPO_TEST_REPORT_DIR     Where to write the JUnit report (defaults to <tempo>/Saved/PythonTestReport).
#   TEMPO_SERVER_PORT         gRPC port the sim should listen on (default 10001).
#   TEMPO_TEST_VENV           venv to create/use (default <tempo>/.tempo_test_venv).

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$( cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd )
GROUP="${1:-}"

# Locate the Packaged folder.
PACKAGED_DIR="${TEMPO_PACKAGED_DIR:-}"
if [ -z "$PACKAGED_DIR" ]; then
  if PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh 2>/dev/null); then
    PACKAGED_DIR="$PROJECT_ROOT/Packaged"
  fi
fi
if [ -z "$PACKAGED_DIR" ] || [ ! -d "$PACKAGED_DIR" ]; then
  echo "FAILED: Packaged folder not found. Set TEMPO_PACKAGED_DIR, or run Scripts/Package.sh first." 1>&2
  exit 1
fi

# GitHub artifacts drop the executable bit; restore it on the staged Linux binaries.
chmod -R +x "$PACKAGED_DIR/Linux" 2>/dev/null || true

BINARY=$(find "$PACKAGED_DIR/Linux" -maxdepth 1 -name "*.sh" 2>/dev/null | head -1)
if [ -z "$BINARY" ]; then
  echo "FAILED: no Linux packaged launcher (*.sh) found under $PACKAGED_DIR/Linux." 1>&2
  exit 1
fi

PYTHON_BIN=""
for candidate in python3 python; do
  if command -v "$candidate" >/dev/null 2>&1; then
    PYTHON_BIN="$candidate"
    break
  fi
done
if [ -z "$PYTHON_BIN" ]; then
  echo "FAILED: no python interpreter on PATH." 1>&2
  exit 1
fi

VENV="${TEMPO_TEST_VENV:-$TEMPO_ROOT/.tempo_test_venv}"
"$PYTHON_BIN" -m venv "$VENV"
# shellcheck disable=SC1091
source "$VENV/bin/activate"
pip install --upgrade pip >/dev/null

# Install every packaged wheel (tempo-sim plus any project-specific client). pip resolves the
# inter-wheel dependencies when they're given together.
WHEELS=$(find "$PACKAGED_DIR/API/Python" -name "*.whl" 2>/dev/null)
if [ -n "$WHEELS" ]; then
  echo "Installing packaged wheel(s):"; echo "$WHEELS" | sed 's/^/  /'
  # shellcheck disable=SC2086
  pip install $WHEELS
else
  echo "No wheels under $PACKAGED_DIR/API/Python; falling back to the in-tree source package."
  pip install "$TEMPO_ROOT/TempoCore/Content/Python/API"
fi
pip install pytest

TESTS_DIR="${TEMPO_PYTHON_TESTS_DIR:-$TEMPO_ROOT/Tests/Python}"
REPORT_DIR="${TEMPO_TEST_REPORT_DIR:-$TEMPO_ROOT/Saved/PythonTestReport}"
mkdir -p "$REPORT_DIR"

export TEMPO_PACKAGED_BINARY="$BINARY"
export TEMPO_SERVER_PORT="${TEMPO_SERVER_PORT:-10001}"

PYTEST_ARGS=("$TESTS_DIR" "-v" "--junitxml=$REPORT_DIR/python-${GROUP:-all}.xml")
if [ -n "$GROUP" ] && [ "$GROUP" != "all" ]; then
  PYTEST_ARGS+=("-m" "$GROUP")
fi

echo "Running Python API tests (group='${GROUP:-all}') from $TESTS_DIR against $BINARY ..."
pytest "${PYTEST_ARGS[@]}"
