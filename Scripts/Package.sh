#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
cd "$PROJECT_ROOT"
PROJECT_NAME=$(find . -maxdepth 1 -name "*.uproject" -exec basename {} .uproject \;)

# Check for project packaging flags.
LOW_MEMORY_MODE=false
BUILD_CONFIGURATION=Development
for arg in "$@"; do
  case "$arg" in
    --low-memory)
      LOW_MEMORY_MODE=true
      ;;
    --release)
      BUILD_CONFIGURATION=Shipping
      ;;
  esac
done

export UNREAL_ENGINE_PATH=$("$SCRIPT_DIR"/FindUnreal.sh)

FIND_UPROJECT() {
    local START_DIR
    START_DIR=$(dirname "$1")
    local CURRENT_DIR="$START_DIR"
    
    while [[ "$CURRENT_DIR" != "/" ]]; do
        local UPROJECT_FILE
        UPROJECT_FILE=$(find "$CURRENT_DIR" -maxdepth 1 -name "*.uproject" -print -quit)
        if [[ -n "$UPROJECT_FILE" ]]; then
            echo "$UPROJECT_FILE"
            return 0
        fi
        CURRENT_DIR=$(dirname "$CURRENT_DIR")
    done
    
    echo "No .uproject file found" >&2
    return 1
}

UPROJECT_FILE=$(FIND_UPROJECT "$SCRIPT_DIR")

TEMPOROS_ENABLED=$(jq '.Plugins[] | select(.Name=="TempoROS") | .Enabled' "$UPROJECT_FILE")
# Remove any trailing carriage return character
TEMPOROS_ENABLED="${TEMPOROS_ENABLED%$'\r'}"

HOST_PLATFORM=""
TARGET_PLATFORM=""
if [[ "$OSTYPE" = "msys" ]]; then
  HOST_PLATFORM="Win64"
  # 8.3 short form removes the space in "Program Files" so PWD is spaces-free
  # when we invoke .bat files — otherwise cmd.exe's strip-outer-quotes rule
  # mangles the path when another argument (e.g. -project=...) is also quoted.
  export UNREAL_ENGINE_PATH=$(cygpath -w -s "$UNREAL_ENGINE_PATH")
  if [ "$1" = "Linux" ]; then
    if [ -z ${LINUX_MULTIARCH_ROOT+x} ]; then
      echo "LINUX_MULTIARCH_ROOT not set, cannot cross-compile for Linux"
      exit 1
    else
      TARGET_PLATFORM="Linux"
    fi
  else
    TARGET_PLATFORM="Win64"
  fi
elif [[ "$OSTYPE" = "darwin"* ]]; then
  HOST_PLATFORM="Mac"
  TARGET_PLATFORM="Mac"
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  HOST_PLATFORM="Linux"
  TARGET_PLATFORM="Linux"
else
  echo "Unsupported platform"
  exit 1
fi

cd "$UNREAL_ENGINE_PATH"

if [ "$TEMPOROS_ENABLED" = "false" ]; then
  echo "Skipping TempoROS automation build because TempoROS plugin is not enabled"
else
  echo "Building TempoROS automation (for custom copy handler)"
  "$PROJECT_ROOT/Plugins/Tempo/TempoROS/Scripts/BuildAutomation.sh"
fi

cd "$UNREAL_ENGINE_PATH"

# Build the base command with common arguments
PACKAGE_COMMAND="Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project=\"$PROJECT_ROOT/$PROJECT_NAME.uproject\" BuildCookRun -nop4 -utf8output -nocompileeditor -skipbuildeditor -cook -target=\"$PROJECT_NAME\" -platform=$TARGET_PLATFORM -project=\"$PROJECT_ROOT/$PROJECT_NAME.uproject\" -installed -stage -package -pak -build -prereqs -clientconfig=$BUILD_CONFIGURATION"

echo "Packaging $PROJECT_NAME in $BUILD_CONFIGURATION configuration -> $PROJECT_ROOT/Packaged"

# Add platform-specific parts
if [ "$HOST_PLATFORM" = "Win64" ]; then
  PACKAGE_COMMAND="./Engine/Build/BatchFiles/RunUAT.bat $PACKAGE_COMMAND -unrealexe=\"UnrealEditor-Cmd.exe\" -stagingdirectory=\"$PROJECT_ROOT/Packaged\""
elif [ "$HOST_PLATFORM" = "Mac" ]; then
  PACKAGE_COMMAND="./Engine/Build/BatchFiles/RunUAT.sh $PACKAGE_COMMAND -unrealexe=\"UnrealEditor-Cmd\" -archive -archivedirectory=\"$PROJECT_ROOT/Packaged\""
elif [ "$HOST_PLATFORM" = "Linux" ]; then
  PACKAGE_COMMAND="./Engine/Build/BatchFiles/RunUAT.sh $PACKAGE_COMMAND -unrealexe=\"UnrealEditor\" -stagingdirectory=\"$PROJECT_ROOT/Packaged\""
else
  echo "Unsupported platform"
  exit 1
fi

# Add ScriptDir argument if TempoROS is enabled
if [ "$TEMPOROS_ENABLED" = "true" ]; then
  PACKAGE_COMMAND="$PACKAGE_COMMAND -ScriptDir=\"$PROJECT_ROOT/Plugins/Tempo/TempoROS/Scripts\""
fi

# Add low memory options if requested
if [ "$LOW_MEMORY_MODE" = "true" ]; then
  # The cooker and C++ build have independent concurrency controls. A single
  # cook process alone does not prevent UBT/UBA from scheduling enough compiler
  # actions to exhaust available memory on lower-memory machines. Three local
  # actions leave headroom for UAT, the linker, and the cook commandlet while
  # retaining useful parallelism.
  PACKAGE_COMMAND="$PACKAGE_COMMAND -CookPartialGC -NoXGE -UbtArgs=\"-MaxParallelActions=3 -NoUBA -NoXGE\" -AdditionalCookerOptions=\"-cookprocesscount=1\""
  echo "Low memory mode enabled: at most 3 compile actions, single cook process, partial GC, no UBA/XGE"
fi

# Filter out our custom flags before passing to UAT
PASSTHROUGH_ARGS=()
for arg in "$@"; do
  case "$arg" in
    --low-memory|--release)
      ;;
    *)
      PASSTHROUGH_ARGS+=("$arg")
      ;;
  esac
done

# Stream the packaging progress. UAT (UE 5.8) prints nothing at all while a child
# process runs and dumps that child's whole output when it exits, so the terminal
# looks hung for the entire C++ build and again for the entire cook -- long enough
# that people kill a working package. Measured on Mac: 167 lines all arriving at
# once after 58s of silence, while UnrealBuildTool run directly streams normally.
#
# Each child does write its own log file line by line as it goes, so pin the two
# folders UAT writes them to (both are UAT's own env overrides) and tail those.
# UnrealBuildTool's log lands in uebp_LogFolder, but a commandlet's live log goes
# to uebp_EngineSavedFolder -- RunCommandlet passes it as -abslog and only copies
# it into the log folder once the commandlet exits, so watching the log folder
# alone would miss the whole cook. Keep the two as siblings: UAT wipes
# uebp_LogFolder on startup and would take a nested folder with it.
#
# UAT still re-prints everything when each child exits, so lines appear twice.
BUILD_LOG_DIR="$PROJECT_ROOT/Saved/PackageLogs"
UAT_SAVED_DIR="$PROJECT_ROOT/Saved/PackageUAT"
export uebp_LogFolder="$BUILD_LOG_DIR"
export uebp_EngineSavedFolder="$UAT_SAVED_DIR"
mkdir -p "$BUILD_LOG_DIR" "$UAT_SAVED_DIR"
echo "Build logs: $BUILD_LOG_DIR (streamed below as each tool writes them)"

# Offsets live in files, not an associative array: macOS ships bash 3.2, which has
# none. `tail -F` is no help either -- the cook log's name carries a timestamp and
# UAT creates it partway through, so the files to follow aren't known up front.
FOLLOW_STATE_DIR=$(mktemp -d)

# Seed each existing log at its current end so the stream carries only this run.
# Without this the previous package's logs are replayed in full at startup, before
# UAT gets around to clearing them. When UAT does replace a log the size goes
# backwards, which the loop below treats as "resync from the top".
for STALE_LOG in "$BUILD_LOG_DIR"/*.txt "$UAT_SAVED_DIR"/*.txt; do
  [ -f "$STALE_LOG" ] || continue
  wc -c < "$STALE_LOG" | tr -d ' ' > "$FOLLOW_STATE_DIR/$(basename "$STALE_LOG" .txt)"
done

FOLLOW_BUILD_LOGS() {
  local log tag offset size
  while true; do
    for log in "$BUILD_LOG_DIR"/*.txt "$UAT_SAVED_DIR"/*.txt; do
      [ -f "$log" ] || continue
      tag=$(basename "$log" .txt)
      # UAT's own log just duplicates its stdout, and stalls in lockstep with it.
      [ "$tag" = "Log" ] && continue
      offset=0
      [ -f "$FOLLOW_STATE_DIR/$tag" ] && offset=$(cat "$FOLLOW_STATE_DIR/$tag")
      size=$(wc -c < "$log" | tr -d ' ')
      if [ "$size" -gt "$offset" ]; then
        tail -c "+$((offset + 1))" "$log" | sed "s/^/[${tag#UBA-}] /"
        echo "$size" > "$FOLLOW_STATE_DIR/$tag"
      elif [ "$size" -lt "$offset" ]; then
        echo 0 > "$FOLLOW_STATE_DIR/$tag"  # log replaced; resync from the top
      fi
    done
    sleep 1
  done
}

FOLLOW_BUILD_LOGS &
FOLLOW_PID=$!
# Detached so bash doesn't print "Terminated: 15" when the trap kills it, and
# cleaned up here because a signalled subshell never runs an EXIT trap of its own.
disown "$FOLLOW_PID" 2>/dev/null || true
trap 'kill "$FOLLOW_PID" 2>/dev/null || true; rm -rf "$FOLLOW_STATE_DIR"' EXIT

# Execute the command with any additional arguments
eval "$PACKAGE_COMMAND" "${PASSTHROUGH_ARGS[@]}"

# Copy cook metadata (including chunk manifests) to the package directory
if [[ "$TARGET_PLATFORM" = "Win64" ]]; then
  cp -r "$PROJECT_ROOT/Saved/Cooked/Windows/$PROJECT_NAME/Metadata" "$PROJECT_ROOT/Packaged"
  cp -r "$PROJECT_ROOT/Saved/Cooked/Windows/$PROJECT_NAME/AssetRegistry.bin" "$PROJECT_ROOT/Packaged"
else
  cp -r "$PROJECT_ROOT/Saved/Cooked/$TARGET_PLATFORM/$PROJECT_NAME/Metadata" "$PROJECT_ROOT/Packaged"
  cp -r "$PROJECT_ROOT/Saved/Cooked/$TARGET_PLATFORM/$PROJECT_NAME/AssetRegistry.bin" "$PROJECT_ROOT/Packaged"
fi

# Copy generated Rust crate(s) to Packaged/API/Rust/ so downstream consumers can
# build a Rust client against this packaged build. Only ships the files that
# `cargo package` would include — same as the publish artifact, minus target/,
# Cargo.lock, tempo_proto_includes/, etc. Source of truth is each crate's
# `include = [...]` field; `cargo package --list` honors it.
PACKAGE_RUST_CRATE() {
  local CRATE_DIR="$1"
  local CRATE_MANIFEST="$CRATE_DIR/Cargo.toml"
  if [[ ! -f "$CRATE_MANIFEST" ]]; then
    return 0
  fi
  local CRATE_NAME
  # POSIX [[:space:]] (not \s — BSD/macOS sed doesn't support \s and would leave the value as the
  # whole `name = "..."` line, creating a directory with spaces in its name).
  CRATE_NAME=$(grep -m1 '^name' "$CRATE_MANIFEST" | sed -E 's/^name[[:space:]]*=[[:space:]]*"([^"]+)".*/\1/')
  local DEST="$PROJECT_ROOT/Packaged/API/Rust/$CRATE_NAME"
  echo "Packaging Rust crate $CRATE_NAME -> $DEST"
  # `--no-verify` skips the compile-the-extracted-crate step, so this works
  # even when a path dep (e.g. tempo-sim) isn't on crates.io yet. Capture the
  # file list (stdout) and check the exit status so a `cargo package` failure
  # surfaces instead of silently shipping an empty crate dir; cargo's own
  # warnings/errors still go to the terminal via stderr.
  local FILE_LIST
  if ! FILE_LIST=$(cd "$CRATE_DIR" && cargo package --list --no-verify --allow-dirty); then
    echo "Error: 'cargo package --list' failed for $CRATE_NAME; leaving any prior package untouched." >&2
    return 1
  fi
  rm -rf "$DEST"
  mkdir -p "$DEST"
  printf '%s\n' "$FILE_LIST" | \
    grep -vE '^(Cargo\.lock|Cargo\.toml\.orig|\.cargo_vcs_info\.json)$' | \
    while IFS= read -r rel; do
      if [[ -f "$CRATE_DIR/$rel" ]]; then
        mkdir -p "$DEST/$(dirname "$rel")"
        cp "$CRATE_DIR/$rel" "$DEST/$rel"
      fi
    done
}

# Only package the Rust crate(s) when Rust generation is opted in via
# TEMPO_GEN_RUST_API (same gate as gen_rust_api.py / GenRustAPI.sh); otherwise
# the crates are absent or stale and shouldn't be shipped.
if [[ -n "$TEMPO_GEN_RUST_API" && "$TEMPO_GEN_RUST_API" != "0" ]]; then
  if command -v cargo >/dev/null 2>&1; then
    PACKAGE_RUST_CRATE "$PROJECT_ROOT/Plugins/Tempo/TempoCore/Content/Rust/API"
    PACKAGE_RUST_CRATE "$PROJECT_ROOT/Content/Rust/API"
  else
    echo "Skipping Rust crate packaging: cargo not on PATH"
  fi
fi

# Build the generated Python package(s) (sdist + wheel) into
# Packaged/API/Python/<dist-name>/ so downstream consumers can `pip install` a
# Python client against this packaged build. Mirrors PACKAGE_RUST_CRATE.
PYTHON_BIN=""
# Prefer Tempo's managed venv (TempoEnv, created by the build's GenAPI step). It has the build
# toolchain (`build`, via requirements.txt) and the package's own deps. Fall back to a system python.
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

PACKAGE_PYTHON_PACKAGE() {
  local PKG_DIR="$1"
  local PYPROJECT="$PKG_DIR/pyproject.toml"
  if [[ ! -f "$PYPROJECT" ]]; then
    return 0
  fi
  local DIST_NAME
  # POSIX [[:space:]] (not \s — BSD/macOS sed doesn't support \s and would leave the value as the
  # whole `name = "..."` line, creating a directory with spaces in its name).
  DIST_NAME=$(grep -m1 '^name' "$PYPROJECT" | sed -E 's/^name[[:space:]]*=[[:space:]]*"([^"]+)".*/\1/')
  local DEST="$PROJECT_ROOT/Packaged/API/Python/$DIST_NAME"
  echo "Packaging Python package $DIST_NAME -> $DEST"
  rm -rf "$DEST"
  mkdir -p "$DEST"
  if "$PYTHON_BIN" -c "import build" >/dev/null 2>&1; then
    # Preferred: `python -m build` produces an sdist (.tar.gz) and a wheel (.whl) in --outdir.
    (cd "$PKG_DIR" && "$PYTHON_BIN" -m build --outdir "$DEST") || \
      echo "Warning: failed to build Python package $DIST_NAME"
  else
    # Fallback: build just the wheel via pip's PEP 517 frontend, which needs no `build` module.
    # --no-deps so only this package's wheel lands in DEST (not its dependencies').
    echo "  ('build' module unavailable; producing a wheel via pip — no sdist)"
    "$PYTHON_BIN" -m pip wheel --no-deps --wheel-dir "$DEST" "$PKG_DIR" || \
      echo "Warning: failed to build Python wheel for $DIST_NAME"
  fi
}

# Package whenever any Python interpreter is available. The wheel is built either way (via `build`
# if present, else pip); only a complete absence of Python skips it.
if [[ -n "$PYTHON_BIN" ]]; then
  PACKAGE_PYTHON_PACKAGE "$PROJECT_ROOT/Plugins/Tempo/TempoCore/Content/Python/API"
  PACKAGE_PYTHON_PACKAGE "$PROJECT_ROOT/Content/Python/API"
else
  echo "Skipping Python package build: no python interpreter available"
fi
