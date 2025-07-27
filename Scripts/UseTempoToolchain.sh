#!/bin/bash

set -e

PROCESS_TARGET_FILE() {
    local FILE="$1"
    local FILENAME=$(basename $FILE)

    echo "Processing: $FILE"

    if [[ ! -f "$FILE" ]]; then
        echo "Error: File '$FILE' not found!"
        return 1
    fi

    if grep -q "TEMPO_TOOLCHAIN_BLOCK" "$FILE"; then
        echo "  ✓ Custom toolchain block already present in $FILE"
        return 0
    fi

    TEMP_DIR=$(mktemp -d)
    TEMP_FILE="${TEMP_DIR}${FILENAME}.tmp"

    # Find the constructor and add the toolchain block before the closing brace
    # We'll look for the pattern of a constructor ending with a closing brace
    # and insert our code before that closing brace

    # Use AWK to process the file
    awk '
    BEGIN {
        in_constructor = 0
        brace_count = 0
        found_constructor = 0
        inserted = 0
    }

    # Look for constructor pattern with flexible whitespace: "public ClassName( TargetInfo Target) : base(Target)"
    /public [A-Za-z0-9_]+Target[ \t]*\([ \t]*TargetInfo Target[ \t]*\)[ \t]*:[ \t]*base[ \t]*\([ \t]*Target[ \t]*\)/ {
        found_constructor = 1
        in_constructor = 1
        brace_count = 0
        print $0
        next
    }

    # Track braces when inside constructor
    {
        if (in_constructor) {
            # Store original line for counting
            line = $0

            # Count opening braces
            open_braces = gsub(/\{/, "{", line)

            # Reset line and count closing braces
            line = $0
            close_braces = gsub(/\}/, "}", line)

            brace_count += open_braces - close_braces

            # If this line contains a closing brace and brings us to 0 or below
            # and we haven'\''t inserted yet, insert before printing this line
            if (close_braces > 0 && brace_count <= 0 && !inserted && found_constructor) {
                # Insert the toolchain block with proper indentation
                print "\t\t// TEMPO_TOOLCHAIN_BLOCK BEGIN - Added by UseTempoToolChain.sh script"
                print "\t\tif (Platform == UnrealTargetPlatform.Win64)"
                print "\t\t{"
                print "\t\t\tToolChainName = \"TempoVCToolChain\";"
                print "\t\t}"
                print "\t\telse if (Platform == UnrealTargetPlatform.Mac)"
                print "\t\t{"
                print "\t\t\tToolChainName = \"TempoMacToolChain\";"
                print "\t\t}"
                print "\t\telse if (Platform == UnrealTargetPlatform.Linux)"
                print "\t\t{"
                print "\t\t\tToolChainName = \"TempoLinuxToolChain\";"
                print "\t\t}"
                print "\t\t// TEMPO_TOOLCHAIN_BLOCK END"
                inserted = 1
                in_constructor = 0
                found_constructor = 0
            }
        }
        print $0
    }
    ' "$FILE" > "${TEMP_FILE}"

    # Check if the insertion was successful
    if grep -q "TEMPO_TOOLCHAIN_BLOCK" "${TEMP_FILE}"; then
        mv "${TEMP_FILE}" "$FILE"
        echo "  ✓ Successfully added custom toolchain block to $FILE"
    else
        echo "  ✗ Failed to add toolchain block to $FILE"
        return 1
    fi

    # Cleanup
    rm -rf "$TEMP_DIR"

    return 0
}

MAIN() {
    SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
    PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)

    TARGET_FILES=($(find "$PROJECT_ROOT/Source" -maxdepth 1 -name "*Target.cs" -type f))

    if [[ ${#TARGET_FILES[@]} -eq 0 ]]; then
        echo "No *Target.cs files found."
        exit 1
    fi

    echo "Found ${#TARGET_FILES[@]} Target.cs file(s):"
    for FILE in "${TARGET_FILES[@]}"; do
        echo "  - $FILE"
    done
    echo

    for FILE in "${TARGET_FILES[@]}"; do
        PROCESS_TARGET_FILE "$FILE"
        echo
    done
}

MAIN "$@"
