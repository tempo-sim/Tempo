#!/usr/bin/env bash

# Generates the reflected API for this build: runs the schema commandlet in the editor, narrows the
# schema to what the packaged build has, then turns it into classes for the project's Python package
# and (when Rust or C++ generation is opted in) its Rust crate and the C++ client.
#
# Called by Package.sh before the clients are built. Can be run on its own to refresh the classes
# after changing a Blueprint or a UPROPERTY:
#
#   Scripts/GenReflectedAPI.sh
#
# Exits 3 when only the C++ classes failed, having generated the Python and Rust ones, so that
# Package.sh can still ship those. Any other failure exits 1.
#
# The schema lands in Saved/TempoSchema/tempo_schema.json. The classes go into the project's own
# client package and crate, beside the wrappers its protos generate -- `tempo_sample.reflected` in
# Content/Python/API and `tempo_sample::reflected` in Content/Rust/API -- which Package.sh then
# builds into Packaged/API/{Python,Rust}. Both are generated and git-ignored, as the wrappers beside
# them are.
#
# The reflected API lives only in the project's package, never in tempo-sim: it describes one build,
# from the exact engine version (hotfix included) and plugin set that build has.
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
  echo "No python interpreter available to generate the reflected API" >&2
  exit 1
fi

PYTHON_DIR="$SCRIPT_DIR/../TempoCore/Content/Python"
CPP_API_DIR="$SCRIPT_DIR/../TempoCore/Content/Cpp/API"

# Whatever an earlier run generated goes first, before anything here can fail. The types describe
# one build, and every step below can stop short -- a packaged build that will not answer, a schema
# that cannot be narrowed -- so the previous run's must be gone by then, or they ship in this
# build's wheel and crate as if they were its own. Absent beats stale, and Package.sh promises
# exactly that. gen_project_package.py clear knows the project's Python and Rust trees; the C++
# ones are installed beside the C++ client.
"$PYTHON_BIN" "$PYTHON_DIR/gen_project_package.py" clear "$PROJECT_ROOT"
rm -rf "$CPP_API_DIR/include/tempo/reflected"
rm -f "$CPP_API_DIR"/lib/*/libtempo_reflected.a "$CPP_API_DIR"/lib/*/tempo_reflected.lib

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
LOG="$SCHEMA_DIR/gen_reflected_api.log"

echo "Walking reflection to $SCHEMA (this loads every Blueprint, so it takes a minute)"
# The editor's own exit status is not usable here: on macOS it is non-zero after a commandlet that
# succeeded, because of trace-server shutdown. The commandlet's result is what matters, and it says
# so in the log.
CRASH_DIR="$PROJECT_ROOT/Saved/Crashes"
CRASH_MARKER="$SCHEMA_DIR/.commandlet_started"
: > "$CRASH_MARKER"
set +e
"$EDITOR_CMD" "$PROJECT_ROOT/$PROJECT_NAME.uproject" \
  -run=TempoWorldSchema \
  -Out="$SCHEMA" \
  -nullrhi -unattended -nopause -nosplash -nosound -stdout -fullstdoutlogoutput > "$LOG" 2>&1
EDITOR_STATUS=$?
set -e

# Everything about a failed walk goes to stderr, because in CI this log is the only place the cause
# is ever seen. A crash ends the log with the crash reporter's own output, so its tail says nothing;
# the signal and callstack come earlier, and the engine also writes a report under Saved/Crashes.
report_commandlet_failure() {
  echo "Schema commandlet failed (editor exit status $EDITOR_STATUS)." >&2

  echo "---- Crash lines from $LOG ----" >&2
  grep -n -E "Caught signal|Unhandled Exception|Assertion failed|Fatal error|\[Callstack\]|Error: |appError" "$LOG" >&2 \
    || echo "(none found)" >&2

  find "$CRASH_DIR" -newer "$CRASH_MARKER" -type f \
       \( -name "Diagnostics.txt" -o -name "CrashContext.runtime-xml" \) 2>/dev/null |
    while IFS= read -r report; do
      echo "---- $report ----" >&2
      cat "$report" >&2
    done || true

  echo "---- Full log: $LOG ----" >&2
  cat "$LOG" >&2
  echo "---- End of $LOG ----" >&2
}

if ! grep -q "finished execution (result 0)" "$LOG"; then
  report_commandlet_failure
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
  # build nobody has. Package.sh treats a failure here as non-fatal and simply ships no
  # reflected API, which is the right outcome -- absent beats wrong.
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

# Into the project's own package, creating it if the project has no protos of its own to have made
# it already. A directory of its own, so a class module named after an Unreal module cannot land on
# the proto module of the same name.
"$PYTHON_BIN" "$PYTHON_DIR/gen_project_package.py" python "$PROJECT_ROOT"
CLASSES_DIR="$PROJECT_ROOT/Content/Python/API/$IMPORT_NAME/reflected"

echo "Generating $IMPORT_NAME.reflected"
"$PYTHON_BIN" "$PYTHON_DIR/gen_py_classes.py" \
  "$GENERATE_FROM" "$CLASSES_DIR" "--package=$IMPORT_NAME.reflected"
echo "Reflected Python API generated at $CLASSES_DIR"

# Into TempoEnv as well, which the build installs the project package into, so a script run there
# -- against the editor or this build -- has this build's classes rather than an earlier one's.
TEMPO_ENV="$PROJECT_ROOT/TempoEnv"
TEMPO_ENV_PYTHON=""
for candidate in "$TEMPO_ENV/bin/python3" "$TEMPO_ENV/Scripts/python.exe"; do
  if [ -x "$candidate" ]; then
    TEMPO_ENV_PYTHON="$candidate"
    break
  fi
done
if [ -n "$TEMPO_ENV_PYTHON" ]; then
  "$TEMPO_ENV_PYTHON" -m pip install "$PROJECT_ROOT/Content/Python/API" --no-deps --force-reinstall --quiet
  "$TEMPO_ENV_PYTHON" "$PYTHON_DIR/update_venv_cache.py" "$(cd "$SCRIPT_DIR/../TempoCore" && pwd)" "$TEMPO_ENV" "$PROJECT_ROOT"
  echo "Installed $IMPORT_NAME into $TEMPO_ENV"
fi

# The Rust types, from the same schema, into the project's own crate as its `reflected` module. The
# crate's manifest and lib.rs are written after them, because lib.rs declares what src/ holds.
if [ -n "$RUST" ]; then
  RUST_CLASSES_DIR="$PROJECT_ROOT/Content/Rust/API/src/reflected"
  echo "Generating $IMPORT_NAME::reflected"
  "$PYTHON_BIN" "$PYTHON_DIR/gen_rust_classes.py" \
    "$GENERATE_FROM" "$RUST_CLASSES_DIR" "--root=crate::reflected"
  "$PYTHON_BIN" "$PYTHON_DIR/gen_project_package.py" rust "$PROJECT_ROOT"
  echo "Reflected Rust API generated at $RUST_CLASSES_DIR"
else
  echo "Skipping the reflected Rust API (set TEMPO_GEN_RUST_API=1 to generate it)"
fi

# The C++ classes, from the same schema, compiled into libtempo_reflected and installed beside the
# C++ client, which a build with TEMPO_GEN_CPP_API=1 has already made.
if [ -n "${TEMPO_GEN_CPP_API:-}" ] && [ "${TEMPO_GEN_CPP_API}" != "0" ]; then
  if [ ! -d "$CPP_API_DIR/include/proto" ]; then
    echo "The C++ client has not been built (TEMPO_GEN_CPP_API=1 Scripts/Build.sh makes it)" >&2
    exit 1
  fi
  CPP_CLASSES_DIR="$SCHEMA_DIR/cpp"
  rm -rf "$CPP_CLASSES_DIR"
  echo "Generating tempo::reflected"
  # A compiler can fail here for reasons of its own, out of memory on a small machine for one, which
  # say nothing about the Python and Rust classes already generated from the same schema.
  if ! "$PYTHON_BIN" "$PYTHON_DIR/gen_cpp_classes.py" "$GENERATE_FROM" "$CPP_CLASSES_DIR" ||
     ! "$PYTHON_BIN" "$PYTHON_DIR/build_cpp_classes.py" "$CPP_CLASSES_DIR" "$CPP_API_DIR" \
         "$SCRIPT_DIR/../TempoCore/Source/ThirdParty/gRPC"; then
    rm -rf "$CPP_API_DIR/include/tempo/reflected"
    rm -f "$CPP_API_DIR"/lib/*/libtempo_reflected.a "$CPP_API_DIR"/lib/*/tempo_reflected.lib
    echo "Failed to generate the reflected C++ API; the Python and Rust ones were generated" >&2
    exit 3
  fi
else
  echo "Skipping the reflected C++ API (set TEMPO_GEN_CPP_API=1 to generate it)"
fi
