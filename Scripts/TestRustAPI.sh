#!/usr/bin/env bash

# Runs Tempo Rust API integration tests against a PACKAGED build.
#
# The Rust analog of Scripts/TestPythonAPI.sh. The packaged build ships the generated `tempo-sim`
# Rust crate source under Packaged/API/Rust/. This script:
#   1. vendors that crate into the test crate (Tests/Rust/vendor/) so the path dependency resolves,
#   2. for sim-requiring groups, launches the packaged sim headless and waits for its gRPC port,
#   3. runs `cargo test --test <group>` and tears the sim down.
#
# Requires cargo on PATH. Build nothing here — run Scripts/Package.sh first (or download an artifact).
#
# Usage:
#   Scripts/TestRustAPI.sh                  # all tests
#   Scripts/TestRustAPI.sh contract         # compile/surface check only (no sim launched)
#   Scripts/TestRustAPI.sh integration      # behavior tests against a running sim
#
# Env:
#   TEMPO_PACKAGED_DIR     Path to the Packaged folder (defaults to <project root>/Packaged).
#   TEMPO_RUST_TESTS_DIR   The Rust test crate dir (defaults to <tempo>/Tests/Rust).
#   TEMPO_TEST_REPORT_DIR  Where to write the cargo log (defaults to <tempo>/Saved/RustTestReport).
#   TEMPO_SERVER_PORT      gRPC port the sim should listen on (default 10001).

set -e
# Never fail silently: report the line and exit code on any set -e abort. (The EXIT trap installed
# later for sim cleanup is a separate trap type and coexists with this one.)
trap 'echo "TestRustAPI.sh: failed at line $LINENO (exit $?)" >&2' ERR

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$( cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd )
GROUP="${1:-}"
PORT="${TEMPO_SERVER_PORT:-10001}"

if ! command -v cargo >/dev/null 2>&1; then
  echo "FAILED: cargo not on PATH." 1>&2
  exit 1
fi

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

CRATE_SRC=$(find "$PACKAGED_DIR/API/Rust" -maxdepth 1 -type d -name "tempo-sim" 2>/dev/null | head -1)
if [ -z "$CRATE_SRC" ]; then
  echo "FAILED: generated Rust crate not found under $PACKAGED_DIR/API/Rust (was the package built with TEMPO_GEN_RUST_API=1?)." 1>&2
  exit 1
fi

TESTS_DIR="${TEMPO_RUST_TESTS_DIR:-$TEMPO_ROOT/Tests/Rust}"
REPORT_DIR="${TEMPO_TEST_REPORT_DIR:-$TEMPO_ROOT/Saved/RustTestReport}"
mkdir -p "$REPORT_DIR"

# Vendor the packaged crate so the test crate's `path` dependency resolves.
rm -rf "$TESTS_DIR/vendor/tempo-sim"
mkdir -p "$TESTS_DIR/vendor"
cp -r "$CRATE_SRC" "$TESTS_DIR/vendor/tempo-sim"

# Launch the sim for groups that need it (everything except the compile-only "contract" group).
SIM_PID=""
cleanup() {
  if [ -n "$SIM_PID" ] && kill -0 "$SIM_PID" 2>/dev/null; then
    kill "$SIM_PID" 2>/dev/null || true
    wait "$SIM_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT

if [ "$GROUP" != "contract" ]; then
  # GitHub artifacts drop the executable bit; restore it (whichever platform is present).
  chmod -R +x "$PACKAGED_DIR/Linux" "$PACKAGED_DIR/Mac" "$PACKAGED_DIR/Windows" 2>/dev/null || true
  BINARY=$("$SCRIPT_DIR"/FindPackagedBinary.sh "$PACKAGED_DIR") || exit 1
  echo "Launching sim: $BINARY (port $PORT)"
  "$BINARY" -nullrhi -unattended -nopause -nosound -nosplash \
    -ServerPort="$PORT" -stdout -fullstdoutlogoutput > "$REPORT_DIR/sim.log" 2>&1 &
  SIM_PID=$!

  # Wait for the gRPC TCP port to accept connections (the tests then retry the gRPC handshake).
  echo "Waiting for sim gRPC port $PORT ..."
  for _ in $(seq 1 "${TEMPO_SIM_STARTUP_TIMEOUT_S:-300}"); do
    if ! kill -0 "$SIM_PID" 2>/dev/null; then
      echo "FAILED: sim exited early. Log tail:" 1>&2; tail -n 40 "$REPORT_DIR/sim.log" 1>&2; exit 1
    fi
    if (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then exec 3>&- 3<&-; break; fi
    sleep 1
  done
fi

export TEMPO_SERVER_PORT="$PORT"

CARGO_ARGS=(test --manifest-path "$TESTS_DIR/Cargo.toml")
if [ -n "$GROUP" ] && [ "$GROUP" != "all" ]; then
  CARGO_ARGS+=(--test "$GROUP")
fi

echo "Running Rust API tests (group='${GROUP:-all}') against the packaged crate ..."
set +e
cargo "${CARGO_ARGS[@]}" -- --nocapture 2>&1 | tee "$REPORT_DIR/cargo-${GROUP:-all}.log"
STATUS=${PIPESTATUS[0]}
set -e
exit "$STATUS"
