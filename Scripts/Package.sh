#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
cd "$PROJECT_ROOT"
PROJECT_NAME=$(find . -maxdepth 1 -name "*.uproject" -exec basename {} .uproject \;)

# Check for low memory mode flag
LOW_MEMORY_MODE=false
for arg in "$@"; do
  if [[ "$arg" == "--low-memory" ]]; then
    LOW_MEMORY_MODE=true
  fi
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
PACKAGE_COMMAND="Turnkey -command=VerifySdk -platform=$TARGET_PLATFORM -UpdateIfNeeded -project=\"$PROJECT_ROOT/$PROJECT_NAME.uproject\" BuildCookRun -nop4 -utf8output -nocompileeditor -skipbuildeditor -cook -target=\"$PROJECT_NAME\" -platform=$TARGET_PLATFORM -project=\"$PROJECT_ROOT/$PROJECT_NAME.uproject\" -installed -stage -package -pak -build -iostore -prereqs -clientconfig=Development"

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
  PACKAGE_COMMAND="$PACKAGE_COMMAND -CookPartialGC -NoXGE -AdditionalCookerOptions=\"-cookprocesscount=1\""
  echo "Low memory mode enabled: single cook process, partial GC, no XGE"
fi

# Filter out our custom flags before passing to UAT
PASSTHROUGH_ARGS=()
for arg in "$@"; do
  if [[ "$arg" != "--low-memory" ]]; then
    PASSTHROUGH_ARGS+=("$arg")
  fi
done

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
  CRATE_NAME=$(grep -m1 '^name' "$CRATE_MANIFEST" | sed -E 's/^name\s*=\s*"([^"]+)".*/\1/')
  local DEST="$PROJECT_ROOT/Packaged/API/Rust/$CRATE_NAME"
  echo "Packaging Rust crate $CRATE_NAME -> $DEST"
  rm -rf "$DEST"
  mkdir -p "$DEST"
  # `--no-verify` skips the compile-the-extracted-crate step, so this works
  # even when a path dep (e.g. tempo-sim) isn't on crates.io yet.
  (cd "$CRATE_DIR" && cargo package --list --no-verify --allow-dirty 2>/dev/null) | \
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
for candidate in python3 python; do
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
  DIST_NAME=$(grep -m1 '^name' "$PYPROJECT" | sed -E 's/^name\s*=\s*"([^"]+)".*/\1/')
  local DEST="$PROJECT_ROOT/Packaged/API/Python/$DIST_NAME"
  echo "Packaging Python package $DIST_NAME -> $DEST"
  rm -rf "$DEST"
  mkdir -p "$DEST"
  # `python -m build` produces an sdist (.tar.gz) and a wheel (.whl) in --outdir.
  (cd "$PKG_DIR" && "$PYTHON_BIN" -m build --outdir "$DEST") || \
    echo "Warning: failed to build Python package $DIST_NAME"
}

# Only package when a Python build toolchain is available (guard like cargo above).
if [[ -n "$PYTHON_BIN" ]] && "$PYTHON_BIN" -c "import build" >/dev/null 2>&1; then
  PACKAGE_PYTHON_PACKAGE "$PROJECT_ROOT/Plugins/Tempo/TempoCore/Content/Python/API"
  PACKAGE_PYTHON_PACKAGE "$PROJECT_ROOT/Content/Python/API"
else
  echo "Skipping Python package build: python with the 'build' module not available"
fi
