#!/usr/bin/env bash

# Runs the Tempo C++ client's tests against a PACKAGED build.
#
# The C++ analog of Scripts/TestRustAPI.sh. The packaged build ships a self-contained C++ client under
# Packaged/API/Cpp/ (Scripts/Package.sh with TEMPO_GEN_CPP_API=1). This script:
#   1. configures and builds Tests/Cpp against it, through its CMake package,
#   2. for sim-requiring groups, launches the packaged sim headless and waits for its gRPC endpoint,
#   3. runs the group's executable and tears the sim down.
#
# Requires cmake and a C++20 compiler on PATH. Builds nothing of the sim -- package first.
#
# Usage:
#   Scripts/TestCppAPI.sh contract         # links, conversions, the typed hierarchy (no sim)
#   Scripts/TestCppAPI.sh integration      # the RPC client against a running sim
#   Scripts/TestCppAPI.sh typed            # the reflected API against a running sim
#
# Env:
#   TEMPO_PACKAGED_DIR     Path to the Packaged folder (defaults to <project root>/Packaged).
#   TEMPO_TEST_REPORT_DIR  Where to write the logs (defaults to <tempo>/Saved/CppTestReport).
#   TEMPO_SERVER_PORT      gRPC port the sim should listen on (default 10001).
#   TEMPO_SERVER_SOCKET    A Unix domain socket for the sim to listen on instead of a port
#                          (Linux/macOS). Takes precedence over TEMPO_SERVER_PORT.

set -e
trap 'echo "TestCppAPI.sh: failed at line $LINENO (exit $?)" >&2' ERR

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$( cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd )
GROUP="${1:-}"
PORT="${TEMPO_SERVER_PORT:-10001}"
SOCKET="${TEMPO_SERVER_SOCKET:-}"

case "$GROUP" in
  contract|integration|typed) ;;
  *) echo "Usage: TestCppAPI.sh contract|integration|typed" 1>&2; exit 2 ;;
esac

if ! command -v cmake >/dev/null 2>&1; then
  echo "FAILED: cmake not on PATH." 1>&2
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
CPP_DIR="$PACKAGED_DIR/API/Cpp"
if [ ! -f "$CPP_DIR/TempoConfig.cmake" ]; then
  echo "FAILED: no C++ client under $CPP_DIR (was the package built with TEMPO_GEN_CPP_API=1?)." 1>&2
  exit 1
fi

REPORT_DIR="${TEMPO_TEST_REPORT_DIR:-$TEMPO_ROOT/Saved/CppTestReport}"
BUILD_DIR="$TEMPO_ROOT/Saved/CppTestBuild"
mkdir -p "$REPORT_DIR"

# The same job count every C++ build here uses (gen_naming.build_jobs): one per core, as memory
# allows. A bare --parallel is `make -j`, which starts every compile at once.
JOBS="${TEMPO_CPP_BUILD_JOBS:-}"
if [ -z "$JOBS" ]; then
  JOBS=$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4)
fi

echo "Building Tests/Cpp against $CPP_DIR ..."
cmake -S "$TEMPO_ROOT/Tests/Cpp" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DTempo_DIR="$CPP_DIR" \
  > "$REPORT_DIR/cmake-configure.log" 2>&1 || { tail -40 "$REPORT_DIR/cmake-configure.log" 1>&2; exit 1; }
cmake --build "$BUILD_DIR" --config Release --parallel "$JOBS" --target "$GROUP" \
  > "$REPORT_DIR/cmake-build.log" 2>&1 || { tail -60 "$REPORT_DIR/cmake-build.log" 1>&2; exit 1; }
EXECUTABLE=$(find "$BUILD_DIR" -maxdepth 2 \( -name "$GROUP" -o -name "$GROUP.exe" \) -type f | head -1)

SIM_PID=""
# The sim is launched under job control (set -m), which makes it the leader of a process group of
# its own, so this signals the group: what a package calls its binary is a launcher script on
# Linux, and signalling the launcher alone stops the script and orphans the game, which keeps the
# port. Terminated first, killed if the group is still there after a grace period.
cleanup() {
  [ -n "$SIM_PID" ] || return 0
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

  if [ -n "$SOCKET" ]; then
    ENDPOINT_ARG=(-ServerSocket="$SOCKET")
    echo "Launching sim: $BINARY (socket $SOCKET)"
  else
    ENDPOINT_ARG=(-ServerPort="$PORT")
    echo "Launching sim: $BINARY (port $PORT)"
  fi
  # Under job control, so the sim leads a process group cleanup can signal as a whole.
  set -m
  "$BINARY" -nullrhi -unattended -nopause -nosound -nosplash \
    "${ENDPOINT_ARG[@]}" -stdout -fullstdoutlogoutput > "$REPORT_DIR/sim.log" 2>&1 &
  SIM_PID=$!
  set +m

  echo "Waiting for sim gRPC endpoint ${SOCKET:-port $PORT} ..."
  for _ in $(seq 1 "${TEMPO_SIM_STARTUP_TIMEOUT_S:-300}"); do
    if ! kill -0 "$SIM_PID" 2>/dev/null; then
      echo "FAILED: sim exited early. Log tail:" 1>&2; tail -n 40 "$REPORT_DIR/sim.log" 1>&2; exit 1
    fi
    if [ -n "$SOCKET" ]; then
      if [ -S "$SOCKET" ]; then break; fi
    elif (exec 3<>"/dev/tcp/127.0.0.1/$PORT") 2>/dev/null; then
      exec 3>&- 3<&-; break
    fi
    sleep 1
  done
fi

export TEMPO_SERVER_PORT="$PORT"
export TEMPO_SERVER_SOCKET="$SOCKET"

echo "Running C++ client tests (group='$GROUP') against the packaged client ..."
set +e
"$EXECUTABLE" 2>&1 | tee "$REPORT_DIR/cpp-$GROUP.log"
STATUS=${PIPESTATUS[0]}
set -e
exit "$STATUS"
