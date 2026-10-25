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
#   Scripts/TestPythonAPI.sh                # all sim-facing groups (not 'sensors', not 'codegen')
#   Scripts/TestPythonAPI.sh core           # one group: contract | core | world | movement | sensors
#   Scripts/TestPythonAPI.sh codegen        # the generators and typed runtime; no sim, in-tree package
#
# Env:
#   TEMPO_PACKAGED_DIR        Path to the Packaged folder (defaults to <project root>/Packaged). The
#                             codegen group reads only the tempo-sim wheel from it, and only when the
#                             tree has not been built.
#   TEMPO_PYTHON_TESTS_DIR    Directory of tests to run (defaults to <tempo>/Tests/Python). Point
#                             this at your own tests to reuse this runner from a downstream project.
#   TEMPO_TEST_REPORT_DIR     Where to write the JUnit report (defaults to <tempo>/Saved/PythonTestReport).
#   TEMPO_SERVER_PORT         gRPC port the sim should listen on (default 10001).
#   TEMPO_SERVER_SOCKET       A Unix domain socket for the sim to listen on instead of a port
#                             (Linux/macOS). Lets several sims share a machine without each
#                             needing its own port. Takes precedence over TEMPO_SERVER_PORT.
#   TEMPO_TEST_VENV           venv to create/use (default <tempo>/.tempo_test_venv).
#   TEMPO_TEST_INTREE_CLIENT  Install the in-tree tempo-sim instead of the packaged wheel. For
#                             iterating on the client against an already-packaged sim; unset, the
#                             tests check exactly what was packaged.

set -e
# Never fail silently: report the line and exit code on any set -e abort.
trap 'echo "TestPythonAPI.sh: failed at line $LINENO (exit $?)" >&2' ERR

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$( cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd )
GROUP="${1:-}"

# The repo-side groups test this repository's own tooling -- the class generators, the runtime that
# serves the generated classes, and the schema tools -- against a fixture schema. They need no
# packaged sim and install the in-tree source rather than a shipped wheel, so they take their own
# path and return before any of the packaging machinery below.
if [ "$GROUP" = "codegen" ]; then
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

  VENV="${TEMPO_CODEGEN_TEST_VENV:-$TEMPO_ROOT/.tempo_codegen_venv}"
  "$PYTHON_BIN" -m venv "$VENV"
  # shellcheck disable=SC1091
  if [ -f "$VENV/bin/activate" ]; then
    source "$VENV/bin/activate"
  else
    source "$VENV/Scripts/activate"
  fi
  pip install --upgrade pip >/dev/null

  # The repo-side tests still need the generated client: tempo_sim.world imports the proto modules,
  # and the typed runtime's tests build real WorldControl requests. Those modules -- the *_pb2 files
  # and the per-module wrappers -- are what a build writes into the tree, and they are not tracked.
  # A built tree has them. A clean checkout (CI's test jobs) does not, and takes them from the
  # packaged tempo-sim wheel: only the generated files, never the hand-written ones the tree is the
  # authority on.
  API_DIR="$TEMPO_ROOT/TempoCore/Content/Python/API"
  if [ -f "$API_DIR/tempo_sim/tempo_world.py" ] && [ -f "$API_DIR/tempo_sim/TempoWorld/WorldControl_pb2.py" ]; then
    echo "Using the generated client modules in the tree."
  else
    PACKAGED_DIR="${TEMPO_PACKAGED_DIR:-}"
    if [ -z "$PACKAGED_DIR" ]; then
      if PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh 2>/dev/null); then
        PACKAGED_DIR="$PROJECT_ROOT/Packaged"
      fi
    fi
    WHEEL=""
    if [ -n "$PACKAGED_DIR" ] && [ -d "$PACKAGED_DIR/API/Python" ]; then
      WHEEL=$(find "$PACKAGED_DIR/API/Python" -name "tempo_sim-*.whl" 2>/dev/null | head -1)
    fi
    if [ -z "$WHEEL" ]; then
      echo "FAILED: the tree has no generated client modules (tempo_sim/tempo_world.py," 1>&2
      echo "  tempo_sim/TempoWorld/WorldControl_pb2.py), and there is no packaged tempo_sim wheel" 1>&2
      echo "  to take them from${PACKAGED_DIR:+ under $PACKAGED_DIR/API/Python}." 1>&2
      echo "  Either build the project (Scripts/Build.sh generates them into the tree), or package" 1>&2
      echo "  it (Scripts/Package.sh) / download a packaged artifact and set TEMPO_PACKAGED_DIR." 1>&2
      exit 1
    fi
    echo "Taking the generated client modules from $WHEEL"
    python - "$WHEEL" "$API_DIR" <<'PY'
import os
import sys
import zipfile

wheel, api_dir = sys.argv[1], sys.argv[2]
package = "tempo_sim"
package_dir = os.path.join(api_dir, package)

# The hand-written files are the ones the package's own .gitignore whitelists, so the rule is
# spelled in one place; nothing here may replace one of those.
handwritten = set()
with open(os.path.join(package_dir, ".gitignore")) as f:
    for line in f:
        line = line.strip()
        if line.startswith("!"):
            handwritten.add(line[1:])

with zipfile.ZipFile(wheel) as archive:
    names = archive.namelist()
    # A subdirectory is a proto module only if it holds _pb2 files, as the RPC generators count
    # them; that is what keeps reflected/ out.
    proto_modules = {name.split("/")[1] for name in names
                     if name.count("/") == 2 and name.split("/")[0] == package
                     and name.rsplit("/", 1)[1].endswith(("_pb2.py", "_pb2.pyi", "_pb2_grpc.py"))}

    def generated(name):
        parts = name.split("/")
        if parts[0] != package or not parts[-1]:
            return False
        if len(parts) == 2:
            leaf = parts[1]
            return leaf.startswith("tempo_") and leaf.endswith(".py") and leaf not in handwritten
        if len(parts) == 3 and parts[1] in proto_modules:
            leaf = parts[2]
            return leaf.endswith(("_pb2.py", "_pb2.pyi", "_pb2_grpc.py")) or leaf == "__init__.py"
        return False

    selected = [name for name in names if generated(name)]
    for name in selected:
        destination = os.path.realpath(os.path.join(api_dir, name))
        if not destination.startswith(os.path.realpath(package_dir) + os.sep):
            sys.exit("refusing to extract {} outside {}".format(name, package_dir))
        archive.extract(name, api_dir)

if not selected:
    sys.exit("{} holds no generated client modules".format(wheel))
print("  {} generated files from {} proto module(s)".format(len(selected), len(proto_modules)))
PY
  fi

  # The in-tree package, because these tests check the generator and runtime as they are in this
  # working tree, not as some earlier build shipped them.
  pip install --quiet "$API_DIR"
  pip install --quiet --force-reinstall --no-deps "$API_DIR"
  pip install --quiet pytest
  # pyright brings its own prebuilt Node, so this needs no system Node and no change to a CI image.
  # The tests skip rather than fail if it is somehow unavailable.
  pip install --quiet pyright || echo "Warning: could not install pyright; type-checking cases will skip" >&2

  TESTS_DIR="${TEMPO_PYTHON_TESTS_DIR:-$TEMPO_ROOT/Tests/Python}"
  REPORT_DIR="${TEMPO_TEST_REPORT_DIR:-$TEMPO_ROOT/Saved/PythonTestReport}"
  mkdir -p "$REPORT_DIR"

  echo "Running codegen tests from $TESTS_DIR (no sim) ..."
  pytest "$TESTS_DIR" -v -m codegen --junitxml="$REPORT_DIR/python-codegen.xml"
  exit $?
fi

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

# GitHub artifacts drop the executable bit; restore it on the staged binaries (whichever platform
# is present — harmless for the others).
chmod -R +x "$PACKAGED_DIR/Linux" "$PACKAGED_DIR/Mac" "$PACKAGED_DIR/Windows" 2>/dev/null || true

BINARY=$("$SCRIPT_DIR"/FindPackagedBinary.sh "$PACKAGED_DIR") || exit 1

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
# venv layout differs by platform: bin/ on Linux & Mac, Scripts/ on Windows.
# shellcheck disable=SC1091
if [ -f "$VENV/bin/activate" ]; then
  source "$VENV/bin/activate"
else
  source "$VENV/Scripts/activate"
fi
pip install --upgrade pip >/dev/null

# Install every packaged wheel (tempo-sim plus any project-specific client). pip resolves the
# inter-wheel dependencies when they're given together. Read into an array via -print0 so paths
# with spaces survive; guard the missing-dir case so `set -e` doesn't abort, falling back to the
# in-tree source package.
# Iterating on the client itself -- tempo_sim's runtime, or a proto -- means the wheel under
# Packaged is older than the tree, and the tests then check the previous build's client against the
# current sim. Rebuilding the wheel is one option; this is the other, and it is explicit so that the
# default still tests exactly what was packaged.
WHEELS=()
if [ -n "$TEMPO_TEST_INTREE_CLIENT" ] && [ "$TEMPO_TEST_INTREE_CLIENT" != "0" ]; then
  echo "TEMPO_TEST_INTREE_CLIENT is set: installing the in-tree client instead of the packaged wheel."
  echo "  (The sim is still the packaged one. Any project or generated-class wheels are still used.)"
  while IFS= read -r -d '' whl; do
    case "$(basename "$whl")" in
      tempo_sim-*) ;;  # replaced by the in-tree source below
      *) WHEELS+=("$whl") ;;
    esac
  done < <(find "$PACKAGED_DIR/API/Python" -name "*.whl" -print0 2>/dev/null)
  WHEELS+=("$TEMPO_ROOT/TempoCore/Content/Python/API")
elif [ -d "$PACKAGED_DIR/API/Python" ]; then
  while IFS= read -r -d '' whl; do
    WHEELS+=("$whl")
  done < <(find "$PACKAGED_DIR/API/Python" -name "*.whl" -print0 2>/dev/null)
fi
if [ "${#WHEELS[@]}" -gt 0 ]; then
  echo "Installing packaged wheel(s):"
  printf '  %s\n' "${WHEELS[@]}"
  pip install "${WHEELS[@]}"
  # pip skips a wheel whose version is already installed, so a reused venv would keep testing
  # the previous build's client whenever the version hasn't been bumped. Re-unpack the Tempo
  # wheels themselves (--no-deps, so this stays fast) to guarantee we test what we just packaged.
  pip install --force-reinstall --no-deps "${WHEELS[@]}"
else
  echo "No wheels under $PACKAGED_DIR/API/Python; falling back to the in-tree source package."
  pip install "$TEMPO_ROOT/TempoCore/Content/Python/API"
  pip install --force-reinstall --no-deps "$TEMPO_ROOT/TempoCore/Content/Python/API"
fi
pip install pytest

TESTS_DIR="${TEMPO_PYTHON_TESTS_DIR:-$TEMPO_ROOT/Tests/Python}"
REPORT_DIR="${TEMPO_TEST_REPORT_DIR:-$TEMPO_ROOT/Saved/PythonTestReport}"
mkdir -p "$REPORT_DIR"

export TEMPO_PACKAGED_BINARY="$BINARY"
export TEMPO_SERVER_PORT="${TEMPO_SERVER_PORT:-10001}"
# Empty unless the caller asked for a socket; conftest launches the sim with -ServerSocket= and
# points the client at the same path.
export TEMPO_SERVER_SOCKET="${TEMPO_SERVER_SOCKET:-}"
# Let conftest write the sim's full log into the report dir so it's uploaded as a CI artifact.
export TEMPO_TEST_REPORT_DIR="$REPORT_DIR"

# No group (or "all") runs every group EXCEPT two opt-in ones: sensors, which needs a GPU and
# TEMPO_SIM_RENDER=1, and codegen, which has its own path above and no use for the sim this one
# launches. Request either explicitly.
if [ -z "$GROUP" ] || [ "$GROUP" = "all" ]; then
  GROUP="all"
  MARKER="not sensors and not codegen"
else
  MARKER="$GROUP"
fi

echo "Running Python API tests (group='$GROUP', marker='$MARKER') from $TESTS_DIR against $BINARY ..."
pytest "$TESTS_DIR" -v -m "$MARKER" --junitxml="$REPORT_DIR/python-${GROUP}.xml"
