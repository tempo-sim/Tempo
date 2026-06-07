#!/usr/bin/env bash

# Runs Tempo's Unreal automation tests headlessly and fails (non-zero exit) if any test fails,
# if no matching tests ran, or if the editor crashed. Builds nothing — run Scripts/Build.sh first.
#
# Usage:
#   Scripts/Test.sh                 # run all "Tempo." tests
#   Scripts/Test.sh Tempo.Sensors   # run a subset by name prefix

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
cd "$PROJECT_ROOT"
PROJECT_NAME=$(find . -maxdepth 1 -name "*.uproject" -exec basename {} .uproject \;)

UNREAL_ENGINE_PATH=$("$SCRIPT_DIR"/FindUnreal.sh)

# Default to all Tempo tests; allow a name-prefix filter as the first argument.
TEST_FILTER="${1:-Tempo.}"

REPORT_DIR="$PROJECT_ROOT/Saved/TempoTestReport"
rm -rf "$REPORT_DIR"
mkdir -p "$REPORT_DIR"

PLATFORM=""
if [[ "$OSTYPE" = "msys" ]]; then
  PLATFORM="Win64"
elif [[ "$OSTYPE" = "darwin"* ]]; then
  PLATFORM="Mac"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  PLATFORM="Linux"
else
  echo "Unsupported platform"
  exit 1
fi

EDITOR_CMD=""
if [ "$PLATFORM" = "Win64" ]; then
  EDITOR_CMD="$UNREAL_ENGINE_PATH/Engine/Binaries/Win64/UnrealEditor-Cmd.exe"
elif [ "$PLATFORM" = "Mac" ]; then
  EDITOR_CMD="$UNREAL_ENGINE_PATH/Engine/Binaries/Mac/UnrealEditor-Cmd"
elif [ "$PLATFORM" = "Linux" ]; then
  EDITOR_CMD="$UNREAL_ENGINE_PATH/Engine/Binaries/Linux/UnrealEditor-Cmd"
fi

echo "Running automation tests matching '$TEST_FILTER'..."

# -nullrhi: no GPU needed for these pure-logic tests; keeps CI headless.
# -TestExit: the editor exits once the async automation queue drains. Do NOT add "; Quit" to
# -ExecCmds: RunTests only queues the tests, so Quit would fire before any test ran.
# We don't rely on the editor's exit code for pass/fail (it is unreliable across UE versions);
# instead we parse the JSON report below.
set +e
"$EDITOR_CMD" "$PROJECT_ROOT/$PROJECT_NAME.uproject" \
  -ExecCmds="Automation RunTests $TEST_FILTER" \
  -TestExit="Automation Test Queue Empty" \
  -ReportExportPath="$REPORT_DIR" \
  -nullrhi -unattended -nopause -nosplash -nosound -stdout -fullstdoutlogoutput
EDITOR_EXIT=$?
set -e

INDEX="$REPORT_DIR/index.json"
if [ ! -f "$INDEX" ]; then
  echo "FAILED: no test report was produced at $INDEX (editor exit code $EDITOR_EXIT)." 1>&2
  echo "If the log says \"No automation tests matched '$TEST_FILTER'\", the editor has no" 1>&2
  echo "matching tests compiled in. This script builds nothing — run Scripts/Build.sh first" 1>&2
  echo "(test .cpp files only take effect after a rebuild)." 1>&2
  exit 1
fi

# jq is a documented Tempo prerequisite (see README).
TOTAL=$(jq '.tests | length' "$INDEX")
FAILED=$(jq '[.tests[] | select(.state == "Fail")] | length' "$INDEX")

echo "Automation tests run: $TOTAL, failed: $FAILED"

if [ "$TOTAL" -eq 0 ]; then
  echo "FAILED: no tests matched '$TEST_FILTER'. Did the test files compile and register?" 1>&2
  exit 1
fi

if [ "$FAILED" -ne 0 ]; then
  echo "FAILED tests:" 1>&2
  jq -r '.tests[] | select(.state == "Fail") | "  - " + .fullTestPath' "$INDEX" 1>&2
  exit 1
fi

echo "All $TOTAL automation tests passed."
