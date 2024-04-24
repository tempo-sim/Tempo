#!/usr/bin/env bash

set -e

TEMPO_ROOT="$1"
TOOL_DIR="$2"

PROTOC="$TOOL_DIR/protoc"
GRPC_CPP_PLUGIN="$TOOL_DIR/grpc_cpp_plugin"
GRPC_PYTHON_PLUGIN="$TOOL_DIR/grpc_python_plugin"

# The Linux binaries were compiled from the Windows cross-compiler
# and they don't have their permissions set correctly.
if [[ "$OSTYPE" = "linux-gnu"* ]]; then
  chmod +x "$PROTOC"
  chmod +x "$GRPC_CPP_PLUGIN"
  chmod +x "$GRPC_PYTHON_PLUGIN"
fi

# Function to compare and replace/copy files
REPLACE_IF_STALE () {
  local FRESH="$1"
  local POSSIBLY_STALE="$2"
  
  if [ ! -f "$POSSIBLY_STALE" ]; then
    cp -p "$FRESH" "$POSSIBLY_STALE"
  else
    if ! diff --brief "$POSSIBLY_STALE" "$POSSIBLY_STALE" >/dev/null 2>&1; then
      cp -f "$FRESH" "$POSSIBLY_STALE"
    fi
  fi
}

GEN_MODULE_PROTOS() {
  local MODULE_PATH="$1"
  local INCLUDE_ARG="$2"
    
  PROTO_PATH="$MODULE_PATH/Protos"
  
  if [ ! -d "$PROTO_PATH" ]; then
    return
  fi
    
  CPP_DES_DIR="$MODULE_PATH/Private"
  PYTHON_DEST_DIR="$TEMPO_ROOT/Content/Python"
  mkdir -p "$PYTHON_DEST_DIR"
  
  TEMP=$(mktemp -d)
  CPP_TEMP_DIR="$TEMP/CPP"
  PYTHON_TEMP_DIR="$TEMP/Python"
  mkdir -p "$CPP_TEMP_DIR"
  mkdir -p "$PYTHON_TEMP_DIR"
  for PROTO in $(find $PROTO_PATH -name "*.proto" -type f); do
    eval "$PROTOC" --cpp_out="$CPP_TEMP_DIR" --grpc_out="$CPP_TEMP_DIR" --plugin=protoc-gen-grpc="$GRPC_CPP_PLUGIN" "$PROTO" "$INCLUDE_ARG"
    eval "$PROTOC" --python_out="$PYTHON_TEMP_DIR" --grpc_out="$PYTHON_TEMP_DIR" --plugin=protoc-gen-grpc="$GRPC_PYTHON_PLUGIN" "$PROTO" "$INCLUDE_ARG"
  done

  # Replace any stale cpp files.
  for GENERATED_FILE in $(find $CPP_TEMP_DIR -type f); do
    if [ -d "$GENERATED_FILE" ]; then continue; fi # Skip directories
    
    # Construct relative path in CPP_DES_DIR
    RELATIVE_PATH="${GENERATED_FILE#$CPP_TEMP_DIR}"
    POSSIBLY_STALE_FILE="$CPP_DES_DIR/$RELATIVE_PATH"
    
    REPLACE_IF_STALE "$GENERATED_FILE" "$POSSIBLY_STALE_FILE"
  done
  
  # Replace any stale Python files.
  for GENERATED_FILE in $(find $PYTHON_TEMP_DIR -type f); do
    if [ -d "$GENERATED_FILE" ]; then continue; fi # Skip directories
    
    # Construct relative path in PYTHON_DEST_DIR
    RELATIVE_PATH="${GENERATED_FILE#$PYTHON_TEMP_DIR}"
    POSSIBLY_STALE_FILE="$PYTHON_DEST_DIR/$RELATIVE_PATH"
    
    REPLACE_IF_STALE "$GENERATED_FILE" "$POSSIBLY_STALE_FILE"
  done
}

echo "Generating protobuf code..."

# Gather all the Protos paths to make the include arg (protos can include each other).
INCLUDE_ARG=""
for BUILD_CS_FILE in $(find $TEMPO_ROOT -name '*.Build.cs' -type f); do
  PROTO_PATH="$(dirname "$BUILD_CS_FILE")/Protos"
  if [ -d "$PROTO_PATH" ]; then
    INCLUDE_ARG="$INCLUDE_ARG -I $PROTO_PATH"
  fi
done

for BUILD_CS_FILE in $(find $TEMPO_ROOT -name '*.Build.cs' -type f); do
  GEN_MODULE_PROTOS "$(dirname "$BUILD_CS_FILE")" "$INCLUDE_ARG"
done
