#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

set -e

ENGINE_DIR="${1//\\//}"
PROJECT_FILE="${2//\\//}"
PROJECT_ROOT="${3//\\//}"
PLUGIN_ROOT="${4//\\//}"
TARGET_NAME="$5"
TARGET_CONFIG="$6"
TARGET_PLATFORM="$7"
TOOL_DIR="${8//\\//}"
UBT_DIR="${9//\\//}"

PYTHON_DEST_DIR="$PLUGIN_ROOT/Content/Python/API/tempo"
touch "$PYTHON_DEST_DIR/__init__.py"

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
  local INCLUDE_DIR="$1"
  local MODULE_PATH="$2"
  local PRIVATE_DEST_DIR="$MODULE_PATH/Private/ProtobufGenerated"
  local PUBLIC_DEST_DIR="$MODULE_PATH/Public/ProtobufGenerated"
  local MODULE_NAME="$3"
  local SOURCE_DIR="$INCLUDE_DIR"/"$MODULE_NAME"
  local EXPORT_MACRO
  EXPORT_MACRO=$(echo "$MODULE_NAME" | tr '[:lower:]' '[:upper:]')_API
  
  mkdir -p "$PYTHON_DEST_DIR/$MODULE_NAME"
  touch "$PYTHON_DEST_DIR/$MODULE_NAME/__init__.py"
  
  local MODULE_GEN_TEMP_DIR
  MODULE_GEN_TEMP_DIR="$GEN_TEMP_DIR/$MODULE_NAME"
  local CPP_TEMP_DIR="$MODULE_GEN_TEMP_DIR/CPP"
  local PYTHON_TEMP_DIR="$MODULE_GEN_TEMP_DIR/Python"
  mkdir -p "$CPP_TEMP_DIR"
  mkdir -p "$PYTHON_TEMP_DIR"
  if [[ "$OSTYPE" = "msys" ]]; then
    CPP_TEMP_DIR=$(cygpath -m "$CPP_TEMP_DIR")
    PYTHON_TEMP_DIR=$(cygpath -m "$PYTHON_TEMP_DIR")
    INCLUDE_DIR=$(cygpath -m "$INCLUDE_DIR")
    GRPC_CPP_PLUGIN=$(cygpath -m "$GRPC_CPP_PLUGIN")
    GRPC_PYTHON_PLUGIN=$(cygpath -m "$GRPC_PYTHON_PLUGIN")
  fi
  for PROTO in $(find "$SOURCE_DIR" -name "*.proto" -type f); do
    if [[ "$OSTYPE" = "msys" ]]; then
      PROTO=$(cygpath -m "$PROTO")
    fi
    if grep -q "service " "$PROTO"; then
      eval "$PROTOC" --cpp_out=dllexport_decl="$EXPORT_MACRO":"$CPP_TEMP_DIR" --grpc_out="$CPP_TEMP_DIR" --plugin=protoc-gen-grpc="$GRPC_CPP_PLUGIN" "$PROTO" "-I $INCLUDE_DIR"
      eval "$PROTOC" --python_out="$PYTHON_TEMP_DIR" --grpc_out="$PYTHON_TEMP_DIR" --plugin=protoc-gen-grpc="$GRPC_PYTHON_PLUGIN" "$PROTO" "-I $INCLUDE_DIR"
    else
      eval "$PROTOC" --cpp_out=dllexport_decl="$EXPORT_MACRO":"$CPP_TEMP_DIR" "$PROTO" "-I $INCLUDE_DIR"
      eval "$PROTOC" --python_out="$PYTHON_TEMP_DIR" "$PROTO" "-I $INCLUDE_DIR"
    fi
  done

  REFRESHED_CPP_FILES=""
  # Replace any stale cpp files.
  for GENERATED_FILE in $(find "$CPP_TEMP_DIR" -type f); do    
    # Construct relative path in CPP/PUBLIC_DEST_DIR
    local RELATIVE_PATH="${GENERATED_FILE#$CPP_TEMP_DIR}"
    RELATIVE_PATH="${RELATIVE_PATH:1}"
    MODULE_SRC_TEMP_DIR="$SRC_TEMP_DIR/$MODULE_NAME"
    if [[ $GENERATED_FILE == *pb.h ]]; then
      # Header files go in the Public directory *if* they are Public
      STRIPPED_PATH="${RELATIVE_PATH#"$MODULE_NAME"}"
      STRIPPED_PATH="${STRIPPED_PATH%%.*}.proto"
      if [[ -f "$MODULE_SRC_TEMP_DIR/Public$STRIPPED_PATH" ]]; then
        local POSSIBLY_STALE_FILE="$PUBLIC_DEST_DIR/$RELATIVE_PATH"
      else
        local POSSIBLY_STALE_FILE="$PRIVATE_DEST_DIR/$RELATIVE_PATH"
      fi
    else
      local POSSIBLY_STALE_FILE="$PRIVATE_DEST_DIR/$RELATIVE_PATH"
    fi
    REPLACE_IF_STALE "$GENERATED_FILE" "$POSSIBLY_STALE_FILE"
    REFRESHED_CPP_FILES="$REFRESHED_CPP_FILES $POSSIBLY_STALE_FILE"
  done
  
  # Remove any protobuf-generated files that were not refreshed this time.
  for CPP_FILE in $(find "$MODULE_PATH" -type f -name "*.pb.*"); do
    if [[ ! $REFRESHED_CPP_FILES =~ $CPP_FILE ]]; then
      rm "$CPP_FILE"
    fi
  done
  
  # And any directories within the generated directories that are now empty
  if [ -d "$MODULE_PATH/Public/ProtobufGenerated" ]; then
    find "$MODULE_PATH/Public/ProtobufGenerated" -type d -empty -delete
  fi
  if [ -d "$MODULE_PATH/Private/ProtobufGenerated" ]; then
    find "$MODULE_PATH/Private/ProtobufGenerated" -type d -empty -delete
  fi
  
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
  for PY_FILE in $(find "$PYTHON_DEST_DIR"  -maxdepth 1 -type f -name "*_pb2*"); do
      rm "$PY_FILE"
  done
  
  # And any directories with the generated directory that are now empty
  find "$PYTHON_DEST_DIR" -type d -empty -delete
}

echo "Generating protobuf code..."

# Get engine release (e.g. 5.4)
if [ -f "$ENGINE_DIR/Intermediate/Build/BuildRules/UE5RulesManifest.json" ]; then
  RELEASE_WITH_HOTFIX=$(jq -r '.EngineVersion' "$ENGINE_DIR/Intermediate/Build/BuildRules/UE5RulesManifest.json")
  RELEASE="${RELEASE_WITH_HOTFIX%.*}"
fi

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
  DOTNETS=$(find ./Binaries/ThirdParty/DotNet -type f -name dotnet)
  if [[ "$RELEASE" == "5.4" ]]; then
    # In UE 5.4 there is only one dotnet on Linux. 5.5 added arm64 support.
    DOTNET="$DOTNETS"
  else
    ARCH=$(arch)
    if [[ "$ARCH" = "arm64" ]]; then
      DOTNET=$(echo "${DOTNETS[@]}" | grep -E "linux-arm64/dotnet")
    elif [[ "$ARCH" = "x86_64" ]]; then
      DOTNET=$(echo "${DOTNETS[@]}" | grep -E "linux-x64/dotnet")
    fi
  fi
fi

if [ -z ${DOTNET+x} ]; then
  echo -e "Unable generate protos. Couldn't find dotnet.\n"
  exit 1
fi

# First dump a json describing all module dependencies.
eval "$DOTNET" "\"$UBT_DIR/Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll\"" -Mode=JsonExport "$TARGET_NAME" "$TARGET_PLATFORM" "$TARGET_CONFIG" -Project="$PROJECT_FILE" -OutputFile="$TEMP/TempoModules.json" -NoMutex -rootdirectory="'$ENGINE_DIR/..'" > /dev/null 2>&1
JSON_DATA=$(cat "$TEMP/TempoModules.json")
# Extract the public and private dependencies of all C++ project modules.
FILTERED_MODULES=$(echo "$JSON_DATA" | jq --arg project_root "$PROJECT_ROOT" 'def normalize_path: gsub("\\\\"; "/") | if startswith("/") then . else "/" + . end; .Modules | to_entries[] | select(.value.Type == "CPlusPlus") | select((.value.Directory | normalize_path) | startswith($project_root | normalize_path)) | {(.key): {Directory: .value.Directory, PublicDependencyModules: .value.PublicDependencyModules, PrivateDependencyModules: .value.PrivateDependencyModules}}')
MODULE_INFO=$(echo "$FILTERED_MODULES" | jq -s 'add')

# Then iterate through all the modules to:
# - Copy all protos to a temp source directory - one per module!
# - Check that no module names are repeated
# - Add a package specifier for the module to every proto (prepend it to any existing package)
echo "$MODULE_INFO" | jq -r -c 'to_entries[] | [.key, (.value.Directory // "")] | @tsv' | while IFS=$'\t' read -r MODULE_NAME MODULE_PATH; do
  # Remove surrounding single quotes and replace any \\ with /
  MODULE_PATH=$(echo "$MODULE_PATH" | sed 's/^"//; s/"$//; s/\\\\/\//g')
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
      mkdir -p "$(dirname "$MODULE_SRC_TEMP_DIR_DEST")"
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
    if grep -q '^\s*package ' "$PROTO_FILE"; then
      SEARCH="^[[:space:]]*package ([^[:space:];]+);"
      REPLACE="package $MODULE_NAME.\1;"
      sed -E -i '' "s/$SEARCH/$REPLACE/" "$PROTO_FILE"
    else
      echo "package $MODULE_NAME;" >> "$PROTO_FILE"
    fi
  done
done

SYNCPROTOS() {
  SRC="$1"
  DEST="$2"
  if [[ "$OSTYPE" = "msys" ]]; then
    # robocopy uses non-zero exit codes even for successful exit conditions.
    set +e
    SRC=$(cygpath -m "$SRC")
    DEST=$(cygpath -m "$DEST")
    robocopy "$SRC" "$DEST" "*.proto" -s > /dev/null 2>&1
    set -e
  else
    rsync -av --include="*/" --include="*.proto" --exclude="*" "$SRC" "$DEST" > /dev/null 2>&1
  fi
}

GET_MODULE_INCLUDES_PUBLIC_ONLY() {
  local MODULE_NAME="$1"
  local INCLUDES_DIR="$2"
  
  PUBLIC_DEPENDENCIES=$(echo "$MODULE_INFO" | jq -r --arg module_name "$MODULE_NAME" '.[$module_name] | try .PublicDependencyModules[] // []')
  for PUBLIC_DEPENDENCY in $PUBLIC_DEPENDENCIES; do
    # Remove any trailing carriage return character
    PUBLIC_DEPENDENCY="${PUBLIC_DEPENDENCY%$'\r'}"
    if [ -d "$SRC_TEMP_DIR/$PUBLIC_DEPENDENCY" ]; then # Only consider project modules
      if [ -d "$INCLUDES_DIR/$PUBLIC_DEPENDENCY" ]; then
        # We already have this dependency - but still add its public dependencies.
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PUBLIC_DEPENDENCY" "$INCLUDES_DIR"
      else
        # This is a new dependency - add its public protos and those of its public dependencies.
        mkdir -p "$INCLUDES_DIR/$PUBLIC_DEPENDENCY"
        SYNCPROTOS "$SRC_TEMP_DIR/$PUBLIC_DEPENDENCY/Public/" "$INCLUDES_DIR/$PUBLIC_DEPENDENCY"
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PUBLIC_DEPENDENCY" "$INCLUDES_DIR"
      fi
    fi
  done
}

GET_MODULE_INCLUDES() {  
  local MODULE_NAME="$1"
  local INCLUDES_DIR="$2"
  # First copy everything from this modules public and private folders.
  mkdir -p "$INCLUDES_DIR/$MODULE_NAME"
  SYNCPROTOS "$SRC_TEMP_DIR/$MODULE_NAME/Public/" "$INCLUDES_DIR/$MODULE_NAME"
  SYNCPROTOS "$SRC_TEMP_DIR/$MODULE_NAME/Private/" "$INCLUDES_DIR/$MODULE_NAME"
  PUBLIC_DEPENDENCIES=$(echo "$MODULE_INFO" | jq -r --arg module_name "$MODULE_NAME" '.[$module_name] | try .PublicDependencyModules[] // []')
  for PUBLIC_DEPENDENCY in $PUBLIC_DEPENDENCIES; do
    # Remove any trailing carriage return character
    PUBLIC_DEPENDENCY="${PUBLIC_DEPENDENCY%$'\r'}"
    if [ -d "$SRC_TEMP_DIR/$PUBLIC_DEPENDENCY" ]; then # Only consider project modules
      if [ -d "$INCLUDES_DIR/$PUBLIC_DEPENDENCY" ]; then
        # We already have this dependency - but still add its public dependencies.
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PUBLIC_DEPENDENCY" "$INCLUDES_DIR"
      else
        # This is a new dependency - add its public protos and those of its public dependencies.
        mkdir -p "$INCLUDES_DIR/$PUBLIC_DEPENDENCY"
        SYNCPROTOS "$SRC_TEMP_DIR/$PUBLIC_DEPENDENCY/Public/" "$INCLUDES_DIR/$PUBLIC_DEPENDENCY"
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PUBLIC_DEPENDENCY" "$INCLUDES_DIR"
      fi
    fi
  done
  
  PRIVATE_DEPENDENCIES=$(echo "$MODULE_INFO" | jq -r --arg module_name "$MODULE_NAME" '.[$module_name] | try .PrivateDependencyModules[] // []')
  for PRIVATE_DEPENDENCY in $PRIVATE_DEPENDENCIES; do
    # Remove any trailing carriage return character
    PRIVATE_DEPENDENCY="${PRIVATE_DEPENDENCY%$'\r'}"
    if [[ -d "$SRC_TEMP_DIR/$PRIVATE_DEPENDENCY" ]]; then # Only consider project modules
      if [[ -d "$INCLUDES_DIR/$PRIVATE_DEPENDENCY" ]]; then
        # We already have this dependency - but still add its public dependencies.
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$MODULE_NAME" "$INCLUDES_DIR"
      else
        # This is a new dependency - add its public protos and those of its public dependencies.
        mkdir -p "$INCLUDES_DIR/$PRIVATE_DEPENDENCY"
        SYNCPROTOS "$SRC_TEMP_DIR/$PRIVATE_DEPENDENCY/Public/" "$INCLUDES_DIR/$PRIVATE_DEPENDENCY" > /dev/null 2>&1
        GET_MODULE_INCLUDES_PUBLIC_ONLY "$PRIVATE_DEPENDENCY" "$INCLUDES_DIR"
      fi
    fi
  done
}

# Lastly, iterate over all the modules again to
# - Generate the protos for the module (from the copy in the temporary source directory we created)
echo "$MODULE_INFO" | jq -r -c 'to_entries[] | [.key, (.value.Directory // "")] | @tsv' | while IFS=$'\t' read -r MODULE_NAME MODULE_PATH; do
  # Remove surrounding single quotes and replace any \\ with /
  MODULE_PATH=$(echo "$MODULE_PATH" | sed 's/^"//; s/"$//; s/\\\\/\//g')
  MODULE_INCLUDES_TEMP_DIR="$INCLUDES_TEMP_DIR/$MODULE_NAME"
  GET_MODULE_INCLUDES "$MODULE_NAME" "$MODULE_INCLUDES_TEMP_DIR"
  GEN_MODULE_PROTOS "$MODULE_INCLUDES_TEMP_DIR" "$MODULE_PATH" "$MODULE_NAME"
done

rm -rf "$TEMP"

echo "Done"
