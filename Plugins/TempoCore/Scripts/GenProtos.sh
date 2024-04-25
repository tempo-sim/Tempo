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
    mkdir -p "$(dirname "$POSSIBLY_STALE")"
    cp -p "$FRESH" "$POSSIBLY_STALE"
  else
    if ! diff --brief "$POSSIBLY_STALE" "$POSSIBLY_STALE" >/dev/null 2>&1; then
      cp -f "$FRESH" "$POSSIBLY_STALE"
    fi
  fi
}

GEN_MODULE_PROTOS() {
  local SOURCE_DIR="$1"
  local MODULE_PATH="$2"
  local CPP_DEST_DIR="$MODULE_PATH/Private"
  local H_DEST_DIR="$MODULE_PATH/Public"
  local MODULE_NAME="$3"
  local INCLUDE_DIR="$4"
  local PYTHON_DEST_DIR="$TEMPO_ROOT/Content/Python"
  local EXPORT_MACRO
  EXPORT_MACRO=$(echo "$MODULE_NAME" | tr '[:lower:]' '[:upper:]')_API
  
  mkdir -p "$PYTHON_DEST_DIR"
  
  local GEN_TEMP_DIR
  GEN_TEMP_DIR=$(mktemp -d)
  local CPP_TEMP_DIR="$GEN_TEMP_DIR/CPP"
  local PYTHON_TEMP_DIR="$GEN_TEMP_DIR/Python"
  mkdir "$CPP_TEMP_DIR"
  mkdir "$PYTHON_TEMP_DIR"
  for PROTO in $(find "$SOURCE_DIR" -name "*.proto" -type f); do
    if grep -q "service " "$PROTO"; then
      eval "$PROTOC" --cpp_out=dllexport_decl="$EXPORT_MACRO":"$CPP_TEMP_DIR" --grpc_out="$CPP_TEMP_DIR" --plugin=protoc-gen-grpc="$GRPC_CPP_PLUGIN" "$PROTO" -I "$INCLUDE_DIR"
    else
      eval "$PROTOC" --cpp_out=dllexport_decl="$EXPORT_MACRO":"$CPP_TEMP_DIR" "$PROTO" -I "$INCLUDE_DIR"
    fi
    eval "$PROTOC" --python_out="$PYTHON_TEMP_DIR" --grpc_out="$PYTHON_TEMP_DIR" --plugin=protoc-gen-grpc="$GRPC_PYTHON_PLUGIN" "$PROTO" -I "$INCLUDE_DIR"
  done

  # Replace any stale cpp files.
  for GENERATED_FILE in $(find "$CPP_TEMP_DIR" -type f); do    
    # Construct relative path in CPP/H_DEST_DIR
    local RELATIVE_PATH="${GENERATED_FILE#$CPP_TEMP_DIR}"
    if [[ $GENERATED_FILE == *pb.h && ! $GENERATED_FILE == *grpc.pb.h ]]; then
      local POSSIBLY_STALE_FILE="$H_DEST_DIR/$RELATIVE_PATH"
    else
      local POSSIBLY_STALE_FILE="$CPP_DEST_DIR/$RELATIVE_PATH"
    fi
    REPLACE_IF_STALE "$GENERATED_FILE" "$POSSIBLY_STALE_FILE"
  done
  
  # Replace any stale Python files.
  for GENERATED_FILE in $(find "$PYTHON_TEMP_DIR" -type f); do    
    # Construct relative path in PYTHON_DEST_DIR
    local RELATIVE_PATH="${GENERATED_FILE#$PYTHON_TEMP_DIR}"
    local POSSIBLY_STALE_FILE="$PYTHON_DEST_DIR/$RELATIVE_PATH"
    REPLACE_IF_STALE "$GENERATED_FILE" "$POSSIBLY_STALE_FILE"
  done
  
  rm -rf "$TEMP"
}

echo "Generating protobuf code..."

# First pass over all the modules:
# - Copy all protos to a temp source directory
# - Check that no module names are repeated
# - Check that no protos have package specifiers already
# - Add a package specifier for the module-relative location to every proto
SRC_TEMP_DIR=$(mktemp -d)
for BUILD_CS_FILE in $(find "$TEMPO_ROOT" -name '*.Build.cs' -type f); do
  BUILD_CS_FILENAME="$(basename "$BUILD_CS_FILE")"
  MODULE_NAME="${BUILD_CS_FILENAME%%.*}"
  MODULE_PATH="$(dirname "$BUILD_CS_FILE")"
  PROTO_PATH="$MODULE_PATH/Protos"
  if [ -d "$PROTO_PATH" ]; then
    MODULE_SRC_TEMP_DIR="$SRC_TEMP_DIR/$MODULE_NAME"
    if [ -d "$MODULE_SRC_TEMP_DIR" ]; then
      echo "Multiple modules with proto files named $MODULE_NAME found. Please rename one."
      exit 1
    fi
    mkdir "$MODULE_SRC_TEMP_DIR"
    cp -r "$PROTO_PATH"/* "$MODULE_SRC_TEMP_DIR"
    for PROTO_FILE in $(find "$MODULE_SRC_TEMP_DIR" -name '*.proto' -type f); do
      if grep -q "package[^;]*;" "$PROTO_FILE"; then
        PROTO_NAME=$(basename "$PROTO_FILE")
        echo "Proto $PROTO_NAME in module $MODULE_NAME defines a package. This is not allowed. We will add a package based on the folder structure automatically."
        exit 1
      fi
      PROTO_PATH=$(dirname "$PROTO_FILE")
      RELATIVE_PATH="${PROTO_PATH#$SRC_TEMP_DIR}"
      RELATIVE_PATH="${RELATIVE_PATH:1}"
      PACKAGE="${RELATIVE_PATH////.}"
      echo "package $PACKAGE;" >> $PROTO_FILE
    done
  fi
done


# Second pass over all the modules:
# - Generate all the protos for the module (from the copy in the temporary source directory we created)
for BUILD_CS_FILE in $(find "$TEMPO_ROOT" -name '*.Build.cs' -type f); do
  BUILD_CS_FILENAME="$(basename "$BUILD_CS_FILE")"
  MODULE_NAME="${BUILD_CS_FILENAME%%.*}"
  MODULE_PATH="$(dirname "$BUILD_CS_FILE")"
  PROTO_PATH="$MODULE_PATH/Protos"
  if [ -d "$PROTO_PATH" ]; then
    GEN_MODULE_PROTOS "$SRC_TEMP_DIR/$MODULE_NAME" "$MODULE_PATH" "$MODULE_NAME" "$SRC_TEMP_DIR"
  fi
done

rm -rf "$SRC_TEMP_DIR"
