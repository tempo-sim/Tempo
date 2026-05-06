#!/bin/bash

# Finds the Unreal Engine installation path by parsing the .uproject file
# and using Epic Games' engine registry, just like the Epic Games Launcher does.
# Works on Windows, Mac, and Linux.

set -e

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
    UNSUCCESSFUL_EXIT 1
fi

# Function to find .uproject file
FIND_UPROJECT_FILE() {
    SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
    PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)

    local search_path="$PROJECT_ROOT"

    if [[ -f "$search_path" && "$search_path" == *.uproject ]]; then
        echo "$search_path"
        return 0
    fi

    local UPROJECT_FILEs=($(find "$search_path" -maxdepth 1 -name "*.uproject" 2>/dev/null))

    if [[ ${#UPROJECT_FILEs[@]} -eq 0 ]]; then
        echo "Error: No .uproject file found in $search_path" >&2
        exit 1
    elif [[ ${#UPROJECT_FILEs[@]} -gt 1 ]]; then
        echo "Error: Multiple .uproject files found in $search_path" >&2
        echo "Please specify which one to use:" >&2
        printf '%s\n' "${UPROJECT_FILEs[@]}" >&2
        exit 1
    fi

    echo "${UPROJECT_FILEs[0]}"
}

# Function to extract engine association from .uproject file
EXTRACT_ENGINE_ASSOCIATION() {
    local UPROJECT_FILE="$1"
    
    if [[ ! -f "$UPROJECT_FILE" ]]; then
        echo "Error: .uproject file not found: $UPROJECT_FILE" >&2
        exit 1
    fi
    
    # Parse JSON to extract EngineAssociation
    # Handle both quoted strings and unquoted identifiers
    local ENGINE_ASSOC=$(grep -o '"EngineAssociation"[[:space:]]*:[[:space:]]*"[^"]*"' "$UPROJECT_FILE" 2>/dev/null | \
                        sed 's/.*"EngineAssociation"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/')
    
    if [[ -z "$ENGINE_ASSOC" ]]; then
        echo "Error: No EngineAssociation found in $UPROJECT_FILE" >&2
        echo "This project may be using a custom engine build or missing engine association." >&2
        exit 1
    fi
    
    echo "$ENGINE_ASSOC"
}

# Function to get the Epic Games engine registry path based on OS
GET_REGISTRY_PATH() {
    if [[ "$OSTYPE" = "msys" ]]; then
        # Possible LauncherInstalled.dat locations on Windows (may not always exist)
        local POSSIBLE_PATHS=(
            "$APPDATA/Epic/UnrealEngineLauncher/LauncherInstalled.dat"
            "$ProgramData/Epic/UnrealEngineLauncher/LauncherInstalled.dat"
        )
        for PATH in "${POSSIBLE_PATHS[@]}"; do
            if [[ -f "$PATH" ]]; then
                echo "$PATH"
                return 0
            fi
        done
        echo "" # Not found
    elif [[ "$OSTYPE" = "darwin"* ]]; then
        echo "$HOME/Library/Application Support/Epic/UnrealEngineLauncher/LauncherInstalled.dat"
    elif [[ "$OSTYPE" = "linux-gnu"* ]]; then
        echo "Error: UNREAL_ENGINE_PATH environment variable must be explicitly set on Linux"
        exit 1
    else
        echo "Error: Unsupported operating system: $(uname -s)" >&2
        exit 1
  fi
}

# Function to parse LauncherInstalled.dat file
PARSE_LAUNCHER_INSTALLED() {
    local DAT_FILE="$1"
    local ENGINE_ID="$2"
    
    if [[ ! -f "$DAT_FILE" ]]; then
        return 1
    fi
    
    # Parse the JSON-like structure in LauncherInstalled.dat
    # Look for InstallationList entries with matching AppName
    # Try using jq first (most robust)
    if command -v jq >/dev/null 2>&1; then
        ENGINE_PATH=$(jq -r --arg ENGINE_ID "$ENGINE_ID" '
            .InstallationList[]?
            | select(.AppName == $ENGINE_ID)
            | .InstallLocation // empty
        ' "$DAT_FILE" 2>/dev/null | head -1)
    fi
    
    if [[ -n "$ENGINE_PATH" && -d "$ENGINE_PATH" ]]; then
        echo "$ENGINE_PATH"
        return 0
    fi
    
    return 1
}

# Function to find engine installation path
FIND_ENGINE_PATH() {
    local ENGINE_ID="$1"
    local REGISTRY_PATH=$(GET_REGISTRY_PATH)
    
    # If it's a version number (like 4.27, 5.1), convert to expected format
    if [[ "$ENGINE_ID" =~ ^[0-9]+\.[0-9]+$ ]]; then
        ENGINE_ID="UE_${ENGINE_ID}"
    fi
    
    # Try parsing LauncherInstalled.dat
    if [[ -f "$REGISTRY_PATH" ]]; then
        local ENGINE_PATH=$(PARSE_LAUNCHER_INSTALLED "$REGISTRY_PATH" "$ENGINE_ID")
        if [[ $? -eq 0 ]]; then
            echo "$ENGINE_PATH"
            return 0
        fi
    fi
    
    return 1
}

# Function to validate engine installation
VALIDATE_ENGINE_PATH() {
    local ENGINE_PATH="$1"
    
    if [[ ! -d "$ENGINE_PATH" ]]; then
        return 1
    fi
    
    # Check for key engine files/directories
    local KEY_PATHS=(
        "Engine/Binaries"
        "Engine/Build"
        "Engine/Config"
    )

    for key_path in "${KEY_PATHS[@]}"; do
        if [[ ! -d "$ENGINE_PATH/$key_path" ]]; then
            return 1
        fi
    done
    
    return 0
}

# Main execution
main() {
    local ENGINE_PATH

    # Check for explicit UNREAL_ENGINE_PATH environment variable
    if [ -z ${UNREAL_ENGINE_PATH+x} ]; then
        if [[ "$OSTYPE" = "linux-gnu"* ]]; then
            echo "UNREAL_ENGINE_PATH environment variable must be explicitly set on Linux"
            exit 1
        fi

        local UPROJECT_FILE
        UPROJECT_FILE=$(FIND_UPROJECT_FILE)
        local ENGINE_ASSOCIATION=$(EXTRACT_ENGINE_ASSOCIATION "$UPROJECT_FILE")
        ENGINE_PATH=$(FIND_ENGINE_PATH "$ENGINE_ASSOCIATION")
    else
        ENGINE_PATH="$UNREAL_ENGINE_PATH"
    fi

    if [[ $? -ne 0 || -z "$ENGINE_PATH" ]]; then
      echo "Error: Could not find engine installation for: $ENGINE_ASSOCIATION" >&2
      echo "Make sure the engine is installed via Epic Games Launcher" >&2
      exit 1
    fi
    
    if ! VALIDATE_ENGINE_PATH "$ENGINE_PATH"; then
        echo "Error: Found engine path but installation appears incomplete: $ENGINE_PATH" >&2
        exit 1
    fi
    
    echo "$ENGINE_PATH"
}

# Run main function with all arguments
main "$@"
