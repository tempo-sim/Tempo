#!/usr/bin/env bash
set -euo pipefail

# Directory of this script: .../Plugins/Tempo/Scripts
SCRIPT_DIR="$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )"

# Plugin root is TempoCore: .../Plugins/Tempo/TempoCore
PLUGIN_ROOT="$( cd "$SCRIPT_DIR/../TempoCore" && pwd )"

# Project root (VayuSim): three levels up from TempoCore
# .../VayuSim/Plugins/Tempo/TempoCore -> .../VayuSim
PROJECT_ROOT="$( cd "$PLUGIN_ROOT/../.." && pwd )"

PYTHON_BIN="${PYTHON_BIN:-python3}"

# Call the new Python-based generator introduced with PR 380
exec "$PYTHON_BIN" "$PLUGIN_ROOT/Content/Python/gen_protos.py" "$PROJECT_ROOT" "$PLUGIN_ROOT"
