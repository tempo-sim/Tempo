#!/usr/bin/env bash

set -e

ENGINE_DIR="${1//\\//}"
PROJECT_FILE="$2"
PROJECT_ROOT="$3"
TARGET_NAME="$4"
TARGET_CONFIG="$5"
TARGET_PLATFORM="$6"
TOOL_DIR="$7"

PROTOC="$TOOL_DIR/protoc"
GRPC_CPP_PLUGIN="$TOOL_DIR/grpc_cpp_plugin"
GRPC_PYTHON_PLUGIN="$TOOL_DIR/grpc_python_plugin"

# Check for jq
if ! which jq &> /dev/null; then
  echo "Couldn't find jq"
  if [[ "$OSTYPE" = "msys" ]]; then
    echo "Install (on Windows): curl -L -o /usr/bin/jq.exe https://github.com/stedolan/jq/releases/latest/download/jq-win64.exe)"
  elif [[ "$OSTYPE" = "darwin"* ]]; then
    echo "Install (on Mac): brew install jq"
  elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
    echo "Install (on Linux): sudo apt-get install jq"
  fi
  exit 1
fi

# The Linux binaries were compiled from the Windows cross-compiler
# and they don't have their permissions set correctly.
if [[ "$OSTYPE" = "linux-gnu"* ]]; then
  chmod +x "$PROTOC"
  chmod +x "$GRPC_CPP_PLUGIN"
  chmod +x "$GRPC_PYTHON_PLUGIN"
fi

# The temp directory where we will rearrange the .proto files and store the generated outputs.
# Refer to the tempo README for file name and message/service name de-conflicting guarantees Tempo offers.
# To achieve this, the TEMP directory will have three sub-folders:
# - Source: Here we will create one folder for every module, with a Public and Private folder beneath, containing
#           copies of the .proto files found in the source module folders, with relative folder structure preserved.
#           These copied files will have a `package` automatically added based on their module, relative location,
#           and proto file name.
# - Includes: Here we will create one folder for every module, with folders beneath for every module it depends on,
#             including itself and modules it depends on through public dependencies of other modules. These folders
#             contain the public protos for other modules, except for the module's own protos, which include both
#             public and private.
# - Generated: This is where the generated CPP and Python files will go. We don't put them directly in the source
#              folder because we don't want to overwrite them there (and force a rebuild) unless they've changed. 
TEMP=$(mktemp -d)
SRC_TEMP_DIR="$TEMP/Source"
mkdir "$SRC_TEMP_DIR"
INCLUDES_TEMP_DIR="$TEMP/Includes"
mkdir -p "$INCLUDES_TEMP_DIR"
GEN_TEMP_DIR="$TEMP/Generated"
mkdir -p "$GEN_TEMP_DIR"

# Function to refresh stale files
REPLACE_IF_STALE () {
  local FRESH="$1"
  local POSSIBLY_STALE="$2"
  
  if [ ! -f "$POSSIBLY_STALE" ]; then
    mkdir -p "$(dirname "$POSSIBLY_STALE")"
    cp -p "$FRESH" "$POSSIBLY_STALE"
  else
    if ! diff --brief "$FRESH" "$POSSIBLY_STALE" > /dev/null 2>&1; then
      cp -f "$FRESH" "$POSSIBLY_STALE"
    fi
  fi
}

GEN_MODULE_PROTOS() {
  local SOURCE_DIR="$1"
  local MODULE_PATH="$2"
  local PRIVATE_DEST_DIR="$MODULE_PATH/Private/ProtobufGenerated"
  local PUBLIC_DEST_DIR="$MODULE_PATH/Public/ProtobufGenerated"
  local MODULE_NAME="$3"
  local INCLUDE_DIR="$4"
  local PYTHON_DEST_DIR="$PROJECT_ROOT/Content/Python"
  local EXPORT_MACRO
  EXPORT_MACRO=$(echo "$MODULE_NAME" | tr '[:lower:]' '[:upper:]')_API
  
  mkdir -p "$PYTHON_DEST_DIR"
  
  local MODULE_GEN_TEMP_DIR
  MODULE_GEN_TEMP_DIR="$GEN_TEMP_DIR/$MODULE_NAME"
  local CPP_TEMP_DIR="$MODULE_GEN_TEMP_DIR/CPP"
  local PYTHON_TEMP_DIR="$MODULE_GEN_TEMP_DIR/Python"
  mkdir -p "$CPP_TEMP_DIR"
  mkdir -p "$PYTHON_TEMP_DIR"
  for PROTO in $(find "$SOURCE_DIR" -name "*.proto" -type f); do
    if grep -q "service " "$PROTO"; then
      eval "$PROTOC" --cpp_out=dllexport_decl="$EXPORT_MACRO":"$CPP_TEMP_DIR" --grpc_out="$CPP_TEMP_DIR" --plugin=protoc-gen-grpc="$GRPC_CPP_PLUGIN" "$PROTO" "$INCLUDE_DIR"
    else
      eval "$PROTOC" --cpp_out=dllexport_decl="$EXPORT_MACRO":"$CPP_TEMP_DIR" "$PROTO" "$INCLUDE_DIR"
    fi
    eval "$PROTOC" --python_out="$PYTHON_TEMP_DIR" --grpc_out="$PYTHON_TEMP_DIR" --plugin=protoc-gen-grpc="$GRPC_PYTHON_PLUGIN" "$PROTO" "$INCLUDE_DIR"
  done

  REFRESHED_CPP_FILES=""
  # Replace any stale cpp files.
  for GENERATED_FILE in $(find "$CPP_TEMP_DIR" -type f); do    
    # Construct relative path in CPP/PUBLIC_DEST_DIR
    local RELATIVE_PATH="${GENERATED_FILE#$CPP_TEMP_DIR}"
    RELATIVE_PATH="${RELATIVE_PATH:1}"
    if [[ $GENERATED_FILE == *pb.h ]]; then
      local POSSIBLY_STALE_FILE="$PUBLIC_DEST_DIR/$RELATIVE_PATH"
    else
      local POSSIBLY_STALE_FILE="$PRIVATE_DEST_DIR/$RELATIVE_PATH"
    fi
    REPLACE_IF_STALE "$GENERATED_FILE" "$POSSIBLY_STALE_FILE"
    REFRESHED_CPP_FILES="$REFRESHED_CPP_FILES $POSSIBLY_STALE_FILE"
  done
  
  # Remove any protobuf-generated files that were not refreshed this time.
  for CPP_FILE in $(find "$MODULE_PATH" -type f -name "*.pb.*"); do
    if [[ ! $REFRESHED_CPP_FILES =~ $CPP_FILE ]]; then
      echo "Removing $CPP_FILE"
      rm "$CPP_FILE"
    fi
  done
  
  REFRESHED_PY_FILES=""
  # Replace any stale Python files.
  for GENERATED_FILE in $(find "$PYTHON_TEMP_DIR" -type f); do    
    # Construct relative path in PYTHON_DEST_DIR
    local RELATIVE_PATH="${GENERATED_FILE#$PYTHON_TEMP_DIR}"
    RELATIVE_PATH="${RELATIVE_PATH:1}"
    local POSSIBLY_STALE_FILE="$PYTHON_DEST_DIR/$RELATIVE_PATH"
    REPLACE_IF_STALE "$GENERATED_FILE" "$POSSIBLY_STALE_FILE"
    REFRESHED_PY_FILES="$REFRESHED_PY_FILES $POSSIBLY_STALE_FILE"
  done
  
  # Remove any protobuf-generated files that were not refreshed this time.
  if [ -d "$PYTHON_DEST_DIR/$MODULE_NAME" ]; then
    for PY_FILE in $(find "$PYTHON_DEST_DIR/$MODULE_NAME" -type f -name "*_pb2*.py"); do
      if [[ ! $REFRESHED_PY_FILES =~ $PY_FILE ]]; then
        rm "$PY_FILE"
      fi
    done
  fi

  # Remove any protobuf-generated files at the top level of the Python dir.
  for PY_FILE in $(find "$PYTHON_DEST_DIR" -type f -name "*_pb2*" -maxdepth 1); do
      rm "$PY_FILE"
  done
}

echo "Generating protobuf code..."

# Find dotnet
cd "$ENGINE_DIR"
if [[ "$OSTYPE" = "msys" ]]; then
  DOTNET=$(find ./Binaries/ThirdParty/DotNet -type f -name dotnet.exe)
elif [[ "$OSTYPE" = "darwin"* ]]; then
  DOTNETS=$(find ./Binaries/ThirdParty/DotNet -type f -name dotnet)
  ARCH=$(arch)
  if [[ "$ARCH" = "arm64" ]]; then
    DOTNET=$(echo "${DOTNETS[@]}" | grep -E "mac-arm64/dotnet")
  elif [[ "$ARCH" = "i386" ]]; then
    DOTNET=$(echo "${DOTNETS[@]}" | grep -E "mac-x64/dotnet")
  fi
elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
  DOTNET=$(find ./Binaries/ThirdParty/DotNet -type f -name dotnet)
fi

if [ -z ${DOTNET+x} ]; then
  echo -e "Unable generate protos. Couldn't find dotnet.\n"
  exit 1
fi

# First iterate through all the modules to:
# - Copy all protos to a temp source directory - one per module!
# - Check that no module names are repeated
# - Check that no protos have package specifiers already
# - Add a package specifier for the module-relative location to every proto
for BUILD_CS_FILE in $(find "$PROJECT_ROOT" -name '*.Build.cs' -type f); do
  BUILD_CS_FILENAME="$(basename "$BUILD_CS_FILE")"
  MODULE_NAME="${BUILD_CS_FILENAME%%.*}"
  MODULE_PATH="$(dirname "$BUILD_CS_FILE")"
  MODULE_SRC_TEMP_DIR="$SRC_TEMP_DIR/$MODULE_NAME"
  if [ -d "$MODULE_SRC_TEMP_DIR" ]; then
    echo "Multiple modules named $MODULE_NAME found. Please rename one."
    exit 1
  fi
  mkdir -p "$MODULE_SRC_TEMP_DIR/Public"
  mkdir -p "$MODULE_SRC_TEMP_DIR/Private"
  if [ -d "$MODULE_PATH/Public" ]; then
    for PROTO_FILE in $(find "$MODULE_PATH/Public" -name '*.proto' -type f); do
      RELATIVE_PATH="${PROTO_FILE#$MODULE_PATH}"
      RELATIVE_PATH="${RELATIVE_PATH:1}"
      MODULE_SRC_TEMP_DIR_DEST="$MODULE_SRC_TEMP_DIR/$RELATIVE_PATH"
      mkdir -p "$(dirname $MODULE_SRC_TEMP_DIR_DEST)"
      cp "$PROTO_FILE" "$MODULE_SRC_TEMP_DIR/$RELATIVE_PATH"
    done
  fi
  if [ -d "$MODULE_PATH/Private" ]; then
    for PROTO_FILE in $(find "$MODULE_PATH/Private" -name '*.proto' -type f); do
      RELATIVE_PATH="${PROTO_FILE#$MODULE_PATH}"
      RELATIVE_PATH="${RELATIVE_PATH:1}"
      MODULE_SRC_TEMP_DIR_DEST="$MODULE_SRC_TEMP_DIR/$RELATIVE_PATH"
      mkdir -p "$(dirname "$MODULE_SRC_TEMP_DIR_DEST")"
      cp "$PROTO_FILE" "$MODULE_SRC_TEMP_DIR/$RELATIVE_PATH"
    done
  fi
  for PROTO_FILE in $(find "$MODULE_SRC_TEMP_DIR" -name '*.proto' -type f); do
    PROTO_FILENAME=$(basename "$PROTO_FILE")
    if grep -q '^\s*package ' "$PROTO_FILE"; then
      echo "Proto $PROTO_FILENAME in module $MODULE_NAME defines a package. This is not allowed. We will add a package based on the folder structure automatically."
      exit 1
    fi
    PROTO_NAME="${PROTO_FILENAME%%.*}"
    PROTO_PATH=$(dirname "$PROTO_FILE")
    RELATIVE_PATH="${PROTO_PATH#$SRC_TEMP_DIR}"
    RELATIVE_PATH="${RELATIVE_PATH:1}"
    PACKAGE="${RELATIVE_PATH////.}"
    PACKAGE="${PACKAGE//$MODULE_NAME.Public/$MODULE_NAME}"
    PACKAGE="${PACKAGE//$MODULE_NAME.Private/$MODULE_NAME}"
    PACKAGE="$PACKAGE.$PROTO_NAME"
    echo "package $PACKAGE;" >> "$PROTO_FILE"
  done
done

# Then dump a json describing all module dependencies.
eval "$DOTNET" "./Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll" -Mode=JsonExport "$TARGET_NAME" "$TARGET_PLATFORM" "$TARGET_CONFIG" -Project="$PROJECT_FILE" -OutputFile="$TEMP/TempoModules.json" -NoMutex > /dev/null 2>&1

GET_MODULE_INCLUDES_PUBLIC_ONLY() {
  local MODULE_NAME="$1"
  local INCLUDES_DIR="$2"
  PUBLIC_DEPENDENCIES=$(jq -r --arg TargetName "$TARGET_NAME" --arg ModuleName "$MODULE_NAME" \
    'select(.Name == $TargetName)["Modules"][$ModuleName]["PublicDependencyModules"][]' < "$TEMP/TempoModules.json")
  for PUBLIC_DEPENDENCY in $PUBLIC_DEPENDENCIES; do
    if [ -d "$SRC_TEMP_DIR/$PUBLIC_DEPENDENCY" ]; then # Only consider project modules
      if [ -d "$INCLUDES_DIR/$PUBLIC_DEPENDENCY" ]; then
        # We already have this dependency - but still add its public dependencies.
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PUBLIC_DEPENDENCY" "$INCLUDES_DIR"
      else
        # This is a new dependency - add its public protos and those of its public dependencies.
        mkdir -p "$INCLUDES_DIR/$PUBLIC_DEPENDENCY"
        rsync -av --include="*.proto" --exclude="*" "$SRC_TEMP_DIR/$PUBLIC_DEPENDENCY/Public/" "$INCLUDES_DIR/$PUBLIC_DEPENDENCY" > /dev/null 2>&1
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PUBLIC_DEPENDENCY" "$INCLUDES_DIR"
      fi
    fi
  done
}

GET_MODULE_INCLUDES() {
  local MODULE_NAME="$1"
  local INCLUDES_DIR="$2"
#  # First copy everything from this modules public and private folders.
  mkdir -p "$INCLUDES_DIR/$MODULE_NAME"
  rsync -av --include="*.proto" --exclude="*" "$SRC_TEMP_DIR/$MODULE_NAME/Public/" "$INCLUDES_DIR/$MODULE_NAME" > /dev/null 2>&1
  rsync -av --include="*.proto" --exclude="*" "$SRC_TEMP_DIR/$MODULE_NAME/Private/" "$INCLUDES_DIR/$MODULE_NAME" > /dev/null 2>&1
  PUBLIC_DEPENDENCIES=$(jq -r --arg TargetName "$TARGET_NAME" --arg ModuleName "$MODULE_NAME" \
    'select(.Name == $TargetName)["Modules"][$ModuleName]["PublicDependencyModules"][]' < "$TEMP/TempoModules.json")
  PRIVATE_DEPENDENCIES=$(jq -r --arg TargetName "$TARGET_NAME" --arg ModuleName "$MODULE_NAME" \
    'select(.Name == $TargetName)["Modules"][$ModuleName]["PrivateDependencyModules"][]' < "$TEMP/TempoModules.json")
  for PUBLIC_DEPENDENCY in $PUBLIC_DEPENDENCIES; do
    if [ -d "$SRC_TEMP_DIR/$PUBLIC_DEPENDENCY" ]; then # Only consider project modules
      if [ -d "$INCLUDES_DIR/$PUBLIC_DEPENDENCY" ]; then
        # We already have this dependency - but still add its public dependencies.
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PUBLIC_DEPENDENCY" "$INCLUDES"
      else
        # This is a new dependency - add its public protos and those of its public dependencies.
        mkdir -p "$INCLUDES_DIR/$PUBLIC_DEPENDENCY"
        rsync -av --include="*.proto" --exclude="*" "$SRC_TEMP_DIR/$PUBLIC_DEPENDENCY/Public/" "$INCLUDES_DIR/$PUBLIC_DEPENDENCY" > /dev/null 2>&1
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PUBLIC_DEPENDENCY" "$INCLUDES_DIR"
      fi
    fi
  done
  
  for PRIVATE_DEPENDENCY in $PRIVATE_DEPENDENCIES; do
    if [ -d "$SRC_TEMP_DIR/$PRIVATE_DEPENDENCY" ]; then # Only consider project modules
      if [ -d "$INCLUDES_DIR/$PRIVATE_DEPENDENCY" ]; then
        # We already have this dependency - but still add its public dependencies.
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$MODULE_NAME" "$INCLUDES"
      else
        # This is a new dependency - add its public protos and those of its public dependencies.
        mkdir -p "$INCLUDES_DIR/$PRIVATE_DEPENDENCY"
        rsync -av --include="*.proto" --exclude="*" "$SRC_TEMP_DIR/$PRIVATE_DEPENDENCY/Public/" "$INCLUDES_DIR/$PRIVATE_DEPENDENCY" > /dev/null 2>&1
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PRIVATE_DEPENDENCY" "$INCLUDES_DIR"
      fi
    fi
  done
}

# Lastly, iterate over all the modules again to
# - Generate the protos for the module (from the copy in the temporary source directory we created)
for BUILD_CS_FILE in $(find "$PROJECT_ROOT" -name '*.Build.cs' -type f); do
  BUILD_CS_FILENAME="$(basename "$BUILD_CS_FILE")"
  MODULE_NAME="${BUILD_CS_FILENAME%%.*}"
  MODULE_PATH="$(dirname "$BUILD_CS_FILE")"
  MODULE_TYPE=$(jq -r --arg TargetName "$TARGET_NAME" --arg ModuleName "$MODULE_NAME" \
    'select(.Name == $TargetName)["Modules"][$ModuleName]["Type"]' < "$TEMP/TempoModules.json")
  if [ "$MODULE_TYPE" != "CPlusPlus" ]; then
    continue
  fi
  MODULE_INCLUDES_TEMP_DIR="$INCLUDES_TEMP_DIR/$MODULE_NAME"
  GET_MODULE_INCLUDES "$MODULE_NAME" "$MODULE_INCLUDES_TEMP_DIR"
  INCLUDES_ARG="-I $MODULE_INCLUDES_TEMP_DIR"
  GEN_MODULE_PROTOS "$MODULE_INCLUDES_TEMP_DIR/$MODULE_NAME" "$MODULE_PATH" "$MODULE_NAME" "$INCLUDES_ARG"
done

rm -rf "$TEMP"
