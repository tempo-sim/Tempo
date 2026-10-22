#!/usr/bin/env bash

# Generates the typed Python class surface for this build: runs the schema commandlet in the editor,
# turns the schema into an importable package, and writes the pyproject that packages it as a wheel.
#
# Called by Package.sh before the wheels are built. Can be run on its own to refresh the classes
# after changing a Blueprint or a UPROPERTY:
#
#   Scripts/GenTypedAPI.sh
#
# The schema lands in Saved/TempoSchema/tempo_schema.json and the package in
# Saved/TempoTypedAPI/<dist>/, which Package.sh then builds into Packaged/API/Python.

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
PROJECT_NAME=$(find "$PROJECT_ROOT" -maxdepth 1 -name "*.uproject" -exec basename {} .uproject \;)
UNREAL_ENGINE_PATH=$("$SCRIPT_DIR"/FindUnreal.sh)

SCHEMA_DIR="$PROJECT_ROOT/Saved/TempoSchema"
SCHEMA="$SCHEMA_DIR/tempo_schema.json"
STAGE_ROOT="$PROJECT_ROOT/Saved/TempoTypedAPI"

PYTHON_BIN=""
for candidate in \
  "$PROJECT_ROOT/TempoEnv/bin/python3" \
  "$PROJECT_ROOT/TempoEnv/bin/python" \
  "$PROJECT_ROOT/TempoEnv/Scripts/python.exe" \
  python3 python; do
  if command -v "$candidate" >/dev/null 2>&1; then
    PYTHON_BIN="$candidate"
    break
  fi
done
if [ -z "$PYTHON_BIN" ]; then
  echo "No python interpreter available to generate the typed classes" >&2
  exit 1
fi

PYTHON_DIR="$SCRIPT_DIR/../TempoCore/Content/Python"

# Distribution and import names, from gen_naming.py -- the same function that names the project's
# own generated client, so TempoSample becomes tempo-sample-classes / tempo_sample_classes. Derived
# rather than spelled here, because a second place that spells them is a second place to get them
# wrong.
DIST_NAME=$("$PYTHON_BIN" -c "
import sys
sys.path.insert(0, '$PYTHON_DIR')
from gen_naming import project_package_name
print(project_package_name('$PROJECT_ROOT') + '-classes')
")
IMPORT_NAME=$("$PYTHON_BIN" -c "
import sys
sys.path.insert(0, '$PYTHON_DIR')
from gen_naming import package_import_name
print(package_import_name('$DIST_NAME'))
")

EDITOR_CMD=""
case "$OSTYPE" in
  msys*|cygwin*|win*) EDITOR_CMD="$UNREAL_ENGINE_PATH/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" ;;
  darwin*)            EDITOR_CMD="$UNREAL_ENGINE_PATH/Engine/Binaries/Mac/UnrealEditor-Cmd" ;;
  linux-gnu*)         EDITOR_CMD="$UNREAL_ENGINE_PATH/Engine/Binaries/Linux/UnrealEditor-Cmd" ;;
  *) echo "Unsupported platform: $OSTYPE" >&2; exit 1 ;;
esac

mkdir -p "$SCHEMA_DIR"
LOG="$SCHEMA_DIR/gen_typed_api.log"

echo "Walking reflection to $SCHEMA (this loads every Blueprint, so it takes a minute)"
# The editor's own exit status is not usable here: on macOS it is non-zero after a commandlet that
# succeeded, because of trace-server shutdown. The commandlet's result is what matters, and it says
# so in the log.
set +e
"$EDITOR_CMD" "$PROJECT_ROOT/$PROJECT_NAME.uproject" \
  -run=TempoWorldSchema \
  -Out="$SCHEMA" \
  -nullrhi -unattended -nopause -nosplash -nosound -stdout -fullstdoutlogoutput > "$LOG" 2>&1
set -e

if ! grep -q "finished execution (result 0)" "$LOG"; then
  echo "Schema commandlet failed. Last lines of $LOG:" >&2
  tail -30 "$LOG" >&2
  exit 1
fi
if [ ! -s "$SCHEMA" ]; then
  echo "Schema commandlet reported success but wrote nothing to $SCHEMA" >&2
  exit 1
fi
grep -A1 "Wrote schema" "$LOG" | tail -2 || true

# Narrow the schema to what the packaged build actually has.
#
# The walk above ran in the editor, because only an editor reports property metadata. But an editor
# also has thousands of classes a packaged game does not -- whole editor-only modules -- and stubs
# for those are worse than no stubs: a scenario addressing one type-checks and then fails at
# runtime. The build's own reflection is the authority on what ships, so ask it.
#
# Skipped when there is no packaged binary yet, which is the case when this script is run on its own
# rather than from Package.sh. The schema is then the editor's full view, which is right for
# diffing one release against another and wrong for generating a client.
GENERATE_FROM="$SCHEMA"
PACKAGED_BINARY=$("$SCRIPT_DIR"/FindPackagedBinary.sh "$PROJECT_ROOT/Packaged" 2>/dev/null || true)

if [ -n "$PACKAGED_BINARY" ] && [ -x "$PACKAGED_BINARY" ]; then
  echo "Asking the packaged build which classes it has"
  CLASS_PATHS="$SCHEMA_DIR/packaged_class_paths.json"
  FILTERED="$SCHEMA_DIR/tempo_schema_packaged.json"
  if "$PYTHON_BIN" "$PYTHON_DIR/query_packaged_classes.py" "$PACKAGED_BINARY" "$CLASS_PATHS"; then
    if "$PYTHON_BIN" "$PYTHON_DIR/filter_schema.py" "$SCHEMA" "$CLASS_PATHS" "$FILTERED"; then
      GENERATE_FROM="$FILTERED"
    else
      echo "Warning: could not filter the schema; generating from the editor's full view" >&2
    fi
  else
    echo "Warning: could not query the packaged build; generating from the editor's full view" >&2
  fi
else
  echo "No packaged build to filter against; generating from the editor's full view."
  echo "  (Run this from Package.sh, or package first, to generate only classes that ship.)"
fi

STAGE="$STAGE_ROOT/$DIST_NAME"
rm -rf "$STAGE"
mkdir -p "$STAGE"

echo "Generating $IMPORT_NAME"
"$PYTHON_BIN" "$PYTHON_DIR/gen_py_classes.py" \
  "$GENERATE_FROM" "$STAGE/$IMPORT_NAME"

# The version follows tempo-sim's, so a client and the classes it addresses move together.
TEMPO_SIM_VERSION=$(grep -m1 '^version' \
  "$PYTHON_DIR/API/pyproject.toml" \
  | sed -E 's/^version[[:space:]]*=[[:space:]]*"([^"]+)".*/\1/')

cat > "$STAGE/pyproject.toml" <<EOF
# Generated by Scripts/GenTypedAPI.sh. Do not edit.
[build-system]
requires = ["setuptools>=77.0"]
build-backend = "setuptools.build_meta"

[project]
name = "$DIST_NAME"
version = "$TEMPO_SIM_VERSION"
description = "Typed Python classes for this build of $PROJECT_NAME, generated from its Unreal reflection."
requires-python = ">=3.9"
license = "Apache-2.0"
# The base class, the value types and the runtime that serves attribute access all live there.
dependencies = ["tempo-sim==$TEMPO_SIM_VERSION"]

[tool.setuptools.packages.find]
include = ["$IMPORT_NAME", "$IMPORT_NAME.*"]

# The .pyi files are the whole point and are not .py, so they have to be listed explicitly -- as
# does py.typed, without which a type checker ignores an installed package's stubs altogether.
[tool.setuptools.package-data]
"*" = ["py.typed", "*.pyi", "*.meta.json", "index.json", "schema_header.json"]
EOF

echo "Typed classes staged at $STAGE"
