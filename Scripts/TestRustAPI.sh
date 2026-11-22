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
#   TEMPO_SERVER_SOCKET    A Unix domain socket for the sim to listen on instead of a port
#                          (Linux/macOS). Takes precedence over TEMPO_SERVER_PORT.

set -e
# Never fail silently: report the line and exit code on any set -e abort. (The EXIT trap installed
# later for sim cleanup is a separate trap type and coexists with this one.)
trap 'echo "TestRustAPI.sh: failed at line $LINENO (exit $?)" >&2' ERR

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$( cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd )
GROUP="${1:-}"
PORT="${TEMPO_SERVER_PORT:-10001}"
SOCKET="${TEMPO_SERVER_SOCKET:-}"

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

# The reflected API's tests are their own crate, because they need a second path dependency whose
# package name belongs to the host project. Everything else lives in Tests/Rust itself.
CRATE_DIR="$TESTS_DIR"
if [ "$GROUP" = "typed" ]; then
  CRATE_DIR="$TESTS_DIR/typed"
fi

# Vendored crates are copied into a scratch directory and synced from there into vendor/, which
# rewrites only the files that changed. cargo decides what to rebuild by modification time, and a
# fresh copy of the project's crate -- thousands of generated classes -- costs minutes to compile
# again even when not a byte of it changed. Without a Python to sync with, a plain copy.
SYNC_PYTHON=""
for candidate in python3 python; do
  if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c "" >/dev/null 2>&1; then
    SYNC_PYTHON="$candidate"
    break
  fi
done
VENDOR_STAGING="$CRATE_DIR/vendor/.staging"
# vendor_from_staging <name>: moves $VENDOR_STAGING into vendor/<name>.
vendor_from_staging() {
  local dest="$CRATE_DIR/vendor/$1"
  if [ -n "$SYNC_PYTHON" ]; then
    "$SYNC_PYTHON" "$TEMPO_ROOT/TempoCore/Content/Python/sync_tree.py" "$VENDOR_STAGING" "$dest"
    rm -rf "$VENDOR_STAGING"
  else
    rm -rf "$dest"
    mv "$VENDOR_STAGING" "$dest"
  fi
}

# Vendor the packaged crate so the test crate's `path` dependency resolves.
mkdir -p "$CRATE_DIR/vendor"
rm -rf "$VENDOR_STAGING"
cp -r "$CRATE_SRC" "$VENDOR_STAGING"
vendor_from_staging tempo-sim

if [ "$GROUP" = "typed" ]; then
  # The project's own crate, which carries this build's generated classes as its `reflected` module,
  # vendored under a fixed name. The crate is called after the host project (tempo-sample), and a
  # dependency has to be named at compile time, so the one `name =` line is rewritten rather than
  # having every project fork the test crate.
  CLASSES_SRC=""
  for candidate in "$PACKAGED_DIR"/API/Rust/*/; do
    if [ "$(basename "$candidate")" != "tempo-sim" ] \
        && [ -f "$candidate/src/reflected/mod.rs" ]; then
      CLASSES_SRC="${candidate%/}"
      break
    fi
  done
  if [ -z "$CLASSES_SRC" ]; then
    echo "FAILED: no crate under $PACKAGED_DIR/API/Rust carries generated classes (was the package built with TEMPO_GEN_RUST_API=1?)." 1>&2
    exit 1
  fi
  echo "Vendoring $(basename "$CLASSES_SRC") as project"
  rm -rf "$VENDOR_STAGING"
  cp -r "$CLASSES_SRC" "$VENDOR_STAGING"
  # Not `sed -i`: its suffix argument is required on BSD and rejected on GNU, and this script runs
  # on both. [[:space:]] rather than \s for the same reason. In the scratch copy, so the vendored
  # manifest only changes when the packaged one does.
  CLASSES_MANIFEST="$VENDOR_STAGING/Cargo.toml"
  sed -E 's/^name[[:space:]]*=[[:space:]]*"[^"]+"/name = "project"/' \
    "$CLASSES_MANIFEST" > "$CLASSES_MANIFEST.renamed"
  mv "$CLASSES_MANIFEST.renamed" "$CLASSES_MANIFEST"
  vendor_from_staging project
fi

# Launch the sim for groups that need it (everything except the compile-only "contract" group).
SIM_PID=""
# The sim is launched under job control (set -m), which makes it the leader of a process group of
# its own, so this signals the group: what a package calls its binary is a launcher script on
# Linux, and signalling the launcher alone stops the script and orphans the game, which keeps the
# port. Asked to quit over gRPC first, as the Python harness does, because on Windows a signal is
# TerminateProcess, and a sim terminated mid-load once stayed stuck in its termination, holding the
# build tree until a reboot (examples/quit.rs). Then terminated, and killed if the group is still
# there after a grace period.
sim_alive() { kill -0 -- -"$SIM_PID" 2>/dev/null || kill -0 "$SIM_PID" 2>/dev/null; }
cleanup() {
  [ -n "$SIM_PID" ] || return 0
  if sim_alive; then
    # In the background, so a wedged sim that never answers can't hold the teardown. The tests
    # built everything the example needs, so building it is a link.
    TEMPO_SERVER_PORT="$PORT" TEMPO_SERVER_SOCKET="$SOCKET" \
      cargo run --quiet --manifest-path "$CRATE_DIR/Cargo.toml" --example quit > /dev/null 2>&1 &
    QUIT_PID=$!
    QUIT_SENT=""
    for _ in $(seq 1 60); do
      sim_alive || break
      if [ -z "$QUIT_SENT" ] && ! kill -0 "$QUIT_PID" 2>/dev/null; then
        wait "$QUIT_PID" || break  # the request failed, so no exit is coming
        QUIT_SENT=1
      fi
      sleep 1
    done
    kill "$QUIT_PID" 2>/dev/null || true
  fi
  kill -TERM -- -"$SIM_PID" 2>/dev/null || kill -TERM "$SIM_PID" 2>/dev/null || true
  for _ in $(seq 1 30); do
    kill -0 -- -"$SIM_PID" 2>/dev/null || break
    sleep 1
  done
  kill -KILL -- -"$SIM_PID" 2>/dev/null || true
  wait "$SIM_PID" 2>/dev/null || true
}
trap cleanup EXIT

if [ "$GROUP" != "contract" ]; then
  # GitHub artifacts drop the executable bit; restore it (whichever platform is present).
  chmod -R +x "$PACKAGED_DIR/Linux" "$PACKAGED_DIR/Mac" "$PACKAGED_DIR/Windows" 2>/dev/null || true
  BINARY=$("$SCRIPT_DIR"/FindPackagedBinary.sh "$PACKAGED_DIR") || exit 1

  # -nullrhi for the core/world groups; TEMPO_SIM_RENDER=1 renders off-screen (sensors).
  RHI_ARG="-nullrhi"
  [ "${TEMPO_SIM_RENDER:-0}" = "1" ] && RHI_ARG="-RenderOffScreen"

  # One endpoint argument, matching the server's exclusive transports.
  if [ -n "$SOCKET" ]; then
    ENDPOINT_ARG=(-ServerSocket="$SOCKET")
    echo "Launching sim: $BINARY $RHI_ARG (socket $SOCKET)"
  else
    ENDPOINT_ARG=(-ServerPort="$PORT")
    echo "Launching sim: $BINARY $RHI_ARG (port $PORT)"
  fi
  # -llm keeps the engine's low-level memory tracker on, on macOS. Without it a packaged UE 5.8 Mac
  # sim switches LLM off early in startup, freeing every thread's tag stack, and a tag scope already
  # open then pops an empty one: the PopTag check below. The relaunch stays as the backstop.
  LLM_ARG=()
  [ "$(uname -s)" = Darwin ] && LLM_ARG=(-llm)
  # A sim killed by a signal within 30s of launch, before its endpoint appears, is launched once more:
  # UE 5.8's packaged Mac build intermittently fails an LLM check (FLLMThreadState::PopTag, "stack was
  # empty on pop") about a second and a half after exec, before its logging starts, and a relaunch
  # starts cleanly. Bash reports a signal as 128+N. The crashed attempt's log is kept beside sim.log.
  for ATTEMPT in 1 2; do
    LAUNCHED=$SECONDS
    # Under job control, so the sim leads a process group cleanup can signal as a whole.
    set -m
    "$BINARY" "$RHI_ARG" -unattended -nopause -nosound -nosplash \
      "${ENDPOINT_ARG[@]}" "${LLM_ARG[@]}" -stdout -fullstdoutlogoutput > "$REPORT_DIR/sim.log" 2>&1 &
    SIM_PID=$!
    set +m

    # Wait for the endpoint to show up (the tests then retry the gRPC handshake). A socket has no
    # /dev/tcp equivalent, so wait for the server to create the file.
    echo "Waiting for sim gRPC endpoint ${SOCKET:-port $PORT} ..."
    SIM_RC=""
    for _ in $(seq 1 "${TEMPO_SIM_STARTUP_TIMEOUT_S:-300}"); do
      if ! kill -0 "$SIM_PID" 2>/dev/null; then
        SIM_RC=0; wait "$SIM_PID" || SIM_RC=$?
        break
      fi
      if [ -n "$SOCKET" ]; then
        if [ -S "$SOCKET" ]; then break; fi
      elif (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
        exec 3>&- 3<&-; break
      fi
      sleep 1
    done
    [ -n "$SIM_RC" ] || break
    LIVED=$((SECONDS - LAUNCHED))
    if [ "$ATTEMPT" = 1 ] && [ "$SIM_RC" -gt 128 ] && [ "$SIM_RC" -lt 192 ] && [ "$LIVED" -lt 30 ]; then
      mv "$REPORT_DIR/sim.log" "$REPORT_DIR/sim-crashed-at-startup.log"
      echo "::warning title=Sim relaunched::The sim died at startup (exit code $SIM_RC, after ${LIVED}s), so it was launched again. Its log: $REPORT_DIR/sim-crashed-at-startup.log"
      continue
    fi
    echo "FAILED: sim exited early (exit code $SIM_RC). Log tail:" 1>&2; tail -n 40 "$REPORT_DIR/sim.log" 1>&2; exit 1
  done
fi

export TEMPO_SERVER_PORT="$PORT"
export TEMPO_SERVER_SOCKET="$SOCKET"

CARGO_ARGS=(test --manifest-path "$CRATE_DIR/Cargo.toml")
if [ -n "$GROUP" ] && [ "$GROUP" != "all" ]; then
  CARGO_ARGS+=(--test "$GROUP")
fi

# The sim-backed tests share the client's global connection context and drive the one sim, so they
# run on a single thread. The contract group compiles and inspects, and may use them all.
HARNESS_ARGS=(--nocapture)
if [ "$GROUP" != "contract" ]; then
  HARNESS_ARGS+=(--test-threads=1)
fi

echo "Running Rust API tests (group='${GROUP:-all}') against the packaged crate ..."
set +e
cargo "${CARGO_ARGS[@]}" -- "${HARNESS_ARGS[@]}" 2>&1 | tee "$REPORT_DIR/cargo-${GROUP:-all}.log"
STATUS=${PIPESTATUS[0]}
set -e
exit "$STATUS"
