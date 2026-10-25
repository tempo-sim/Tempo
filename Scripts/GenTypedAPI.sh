#!/usr/bin/env bash

# Generates the typed class surface for this build: runs the schema commandlet in the editor, then
# turns the one schema into classes for the project's Python package and (when Rust generation is
# opted in) its Rust crate.
#
# Called by Package.sh before the clients are built. Can be run on its own to refresh the classes
# after changing a Blueprint or a UPROPERTY:
#
#   Scripts/GenTypedAPI.sh
#
# The schema lands in Saved/TempoSchema/tempo_schema.json. The classes go into the project's own
# client package and crate, beside the wrappers its protos generate -- `tempo_sample.reflected` in
# Content/Python/API and `tempo_sample::reflected` in Content/Rust/API -- which Package.sh then
# builds into Packaged/API/{Python,Rust}. Both are generated and git-ignored, as the wrappers beside
# them are. So are the published types, which go into tempo-sim itself.
#
# Rust follows the same gate as the RPC client generator, TEMPO_GEN_RUST_API: a project that does
# not use the Rust client should not pay for generating 16 MB of it.

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
PROJECT_NAME=$(find "$PROJECT_ROOT" -maxdepth 1 -name "*.uproject" -exec basename {} .uproject \;)
UNREAL_ENGINE_PATH=$("$SCRIPT_DIR"/FindUnreal.sh)

SCHEMA_DIR="$PROJECT_ROOT/Saved/TempoSchema"
SCHEMA="$SCHEMA_DIR/tempo_schema.json"

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

# The project package's import name, from gen_naming.py -- the function that names it for the
# proto wrappers too, so TempoSample's is tempo_sample. Derived rather than spelled here, because a
# second place that spells it is a second place to get it wrong.
IMPORT_NAME=$("$PYTHON_BIN" -c "
import sys
sys.path.insert(0, '$PYTHON_DIR')
from gen_naming import package_import_name, project_package_name
print(package_import_name(project_package_name('$PROJECT_ROOT')))
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
  # A build that is there and will not answer is an error, not a reason to fall back.
  #
  # Falling back ships stubs for the editor's whole view -- 11,297 classes where the build has
  # 6,068, so 5,229 that type-check and then fail at runtime, which is precisely what filtering
  # exists to prevent. It happened: the packaged sim exited before answering once during a package
  # run, the warning scrolled past in a two-thousand-line log, and the wheel went out describing a
  # build nobody has. Package.sh treats a failure here as non-fatal and simply ships no typed
  # classes, which is the right outcome -- absent beats wrong.
  if ! "$PYTHON_BIN" "$PYTHON_DIR/query_packaged_classes.py" "$PACKAGED_BINARY" "$CLASS_PATHS"; then
    echo "Could not query $PACKAGED_BINARY, which is the authority on what ships." >&2
    echo "  Refusing to generate from the editor's full view: it has thousands of classes a" >&2
    echo "  packaged build does not, and a stub for one of those fails at runtime." >&2
    exit 1
  fi
  if ! "$PYTHON_BIN" "$PYTHON_DIR/filter_schema.py" "$SCHEMA" "$CLASS_PATHS" "$FILTERED"; then
    echo "Could not narrow the schema to what $PACKAGED_BINARY reports." >&2
    exit 1
  fi
  GENERATE_FROM="$FILTERED"
else
  # No build at all is the standalone case -- someone refreshing the classes after editing a
  # Blueprint, before packaging. The editor's full view is then what there is, and it is also the
  # right input for diffing one release against another.
  echo "No packaged build to filter against; generating from the editor's full view."
  echo "  (Run this from Package.sh, or package first, to generate only classes that ship.)"
fi

RUST=""
if [ -n "${TEMPO_GEN_RUST_API:-}" ] && [ "${TEMPO_GEN_RUST_API}" != "0" ]; then
  RUST=1
fi

# The published types, generated into tempo-sim itself.
#
# Everything below describes one project's build and ships beside it. This describes only the
# modules that are in every build -- the engine's always-loaded ones and Tempo's own -- so it can
# travel with the published wheel and crate:
#
#   from tempo_sim.reflected.Engine import Pawn
#   use tempo_sim::reflected::classes::engine::Pawn;     // feature = "reflected"
#
# From the one engine version this build uses, which is the latest Tempo supports: a client
# against an older engine is type-checked against the latest's classes (see
# docs/clients/typed-classes.md). Generated into the source tree and git-ignored, exactly as the
# protos and service wrappers beside it are.
#
# First, because the project's types re-export what these define identically. Skipped without a
# packaged build to filter against, because a subset of the editor's unfiltered view would describe
# classes and members no build has -- which is the whole thing this is for.
PUBLISHED=""
if [ "$GENERATE_FROM" != "$SCHEMA" ]; then
  SUBSET="$SCHEMA_DIR/tempo_schema_published.json"
  echo "Narrowing to the modules every build ships"
  if "$PYTHON_BIN" "$PYTHON_DIR/subset_schema.py" "$GENERATE_FROM" "$SUBSET"; then
    PUBLISHED="$SUBSET"
    PUBLISHED_PYTHON="$PYTHON_DIR/API/tempo_sim/reflected"
    rm -rf "$PUBLISHED_PYTHON"
    echo "Generating tempo_sim.reflected"
    "$PYTHON_BIN" "$PYTHON_DIR/gen_py_classes.py" \
      "$SUBSET" "$PUBLISHED_PYTHON" "--package=tempo_sim.reflected"
    if [ -n "$RUST" ]; then
      PUBLISHED_RUST="$SCRIPT_DIR/../TempoCore/Content/Rust/API/src/reflected"
      rm -rf "$PUBLISHED_RUST"
      echo "Generating tempo_sim::reflected"
      "$PYTHON_BIN" "$PYTHON_DIR/gen_rust_classes.py" \
        "$SUBSET" "$PUBLISHED_RUST" "--root=crate::reflected"
    fi
  else
    echo "Warning: could not narrow the schema; there will be no published types" >&2
  fi
else
  echo "Skipping the published types (they need a packaged build to filter against)"
fi

# What the project's types are compared against, to re-export rather than redefine.
PUBLISHED_ARG=()
if [ -n "$PUBLISHED" ]; then
  PUBLISHED_ARG=("--published=$PUBLISHED")
fi

# Into the project's own package, creating it if the project has no protos of its own to have made
# it already. A directory of its own, so a class module named after an Unreal module cannot land on
# the proto module of the same name.
"$PYTHON_BIN" "$PYTHON_DIR/gen_project_package.py" python "$PROJECT_ROOT"
CLASSES_DIR="$PROJECT_ROOT/Content/Python/API/$IMPORT_NAME/reflected"
rm -rf "$CLASSES_DIR"

echo "Generating $IMPORT_NAME.reflected"
"$PYTHON_BIN" "$PYTHON_DIR/gen_py_classes.py" \
  "$GENERATE_FROM" "$CLASSES_DIR" "--package=$IMPORT_NAME.reflected" \
  ${PUBLISHED_ARG[@]+"${PUBLISHED_ARG[@]}"}
echo "Typed Python classes generated at $CLASSES_DIR"

# The Rust types, from the same schema, into the project's own crate as its `reflected` module. The
# crate's manifest and lib.rs are written after them, because lib.rs declares what src/ holds.
if [ -n "$RUST" ]; then
  RUST_CLASSES_DIR="$PROJECT_ROOT/Content/Rust/API/src/reflected"
  rm -rf "$RUST_CLASSES_DIR"
  echo "Generating $IMPORT_NAME::reflected"
  "$PYTHON_BIN" "$PYTHON_DIR/gen_rust_classes.py" \
    "$GENERATE_FROM" "$RUST_CLASSES_DIR" "--root=crate::reflected" \
    ${PUBLISHED_ARG[@]+"${PUBLISHED_ARG[@]}"}
  "$PYTHON_BIN" "$PYTHON_DIR/gen_project_package.py" rust "$PROJECT_ROOT"
  echo "Typed Rust classes generated at $RUST_CLASSES_DIR"
else
  echo "Skipping the typed Rust classes (set TEMPO_GEN_RUST_API=1 to generate them)"
fi

# The C++ classes, from the same schema, compiled into libtempo_reflected and installed beside the
# C++ client, which a build with TEMPO_GEN_CPP_API=1 has already made. One tree, the project's whole
# build: the C++ client is built per project, so there is no published subset to share
# (TYPED_WORLD_API_PLAN.md section 23.2).
if [ -n "${TEMPO_GEN_CPP_API:-}" ] && [ "${TEMPO_GEN_CPP_API}" != "0" ]; then
  CPP_API_DIR="$SCRIPT_DIR/../TempoCore/Content/Cpp/API"
  if [ ! -d "$CPP_API_DIR/include/proto" ]; then
    echo "The C++ client has not been built (TEMPO_GEN_CPP_API=1 Scripts/Build.sh makes it)" >&2
    exit 1
  fi
  CPP_CLASSES_DIR="$SCHEMA_DIR/cpp"
  rm -rf "$CPP_CLASSES_DIR"
  echo "Generating tempo::reflected"
  "$PYTHON_BIN" "$PYTHON_DIR/gen_cpp_classes.py" "$GENERATE_FROM" "$CPP_CLASSES_DIR"
  "$PYTHON_BIN" "$PYTHON_DIR/build_cpp_classes.py" "$CPP_CLASSES_DIR" "$CPP_API_DIR" \
    "$SCRIPT_DIR/../TempoCore/Source/ThirdParty/gRPC"
else
  echo "Skipping the typed C++ classes (set TEMPO_GEN_CPP_API=1 to generate them)"
fi
