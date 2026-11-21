#!/usr/bin/env bash

set -e

# Fixes in Epic's CitySample source that a CitySample project needs before it builds. Setup.sh runs
# this; in any other project it finds none of the files and does nothing. Each fix is an exact
# one-line replacement, so running it again changes nothing, and -restore puts Epic's line back.
#
# - CitySampleBlueprintLibrary.cpp: GetWorldInfo's `for` loop over TActorIterator returns from its
#   body unconditionally, so `++It` never runs. CitySample's targets build at BuildSettingsVersion.V7,
#   which (new in UE 5.8) makes unreachable code an error on Clang too, so a stock CitySample fails
#   on macOS and Linux with -Wunreachable-code-loop-increment. The fix is the equivalent `if` with an
#   initializer: TActorIterator's constructor already advances to the first match, and its explicit
#   operator bool is the same check the loop condition made. See docs/guides/citysample.md.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)

RESTORE=0
for ARG in "$@"; do
  case "$ARG" in
    -restore) RESTORE=1 ;;
  esac
done

# Each fix: file (relative to the project root), Epic's line, Tempo's line. Lines are matched after
# leading whitespace and before an optional trailing \r, so indentation and CRLF line endings survive.
FIX_FILES=(
  "Source/CitySample/Util/CitySampleBlueprintLibrary.cpp"
)
FIX_ORIGINALS=(
  "for (TActorIterator<ACitySampleWorldInfo> It(World, ACitySampleWorldInfo::StaticClass()); It; ++It)"
)
FIX_REPLACEMENTS=(
  "if (TActorIterator<ACitySampleWorldInfo> It(World, ACitySampleWorldInfo::StaticClass()); It)"
)

# Counts lines that are exactly $2 (modulo indentation and \r) in file $1.
COUNT_LINE() {
  LINE="$2" perl -ne '$n++ if /^[ \t]*\Q$ENV{LINE}\E\r?$/; END { print $n + 0 }' "$1"
}

# Replaces lines that are exactly $2 with $3 in file $1, keeping indentation and line endings.
REPLACE_LINE() {
  FROM="$2" TO="$3" perl -pi -e 's/^([ \t]*)\Q$ENV{FROM}\E(\r?)$/$1$ENV{TO}$2/' "$1"
}

for i in "${!FIX_FILES[@]}"; do
  FILE="$PROJECT_ROOT/${FIX_FILES[$i]}"
  [ -f "$FILE" ] || continue

  if [ "$RESTORE" -eq 1 ]; then
    FROM="${FIX_REPLACEMENTS[$i]}"
    TO="${FIX_ORIGINALS[$i]}"
  else
    FROM="${FIX_ORIGINALS[$i]}"
    TO="${FIX_REPLACEMENTS[$i]}"
  fi

  if [ "$(COUNT_LINE "$FILE" "$FROM")" -gt 0 ]; then
    REPLACE_LINE "$FILE" "$FROM" "$TO"
    if [ "$RESTORE" -eq 1 ]; then
      echo "  ✓ Restored Epic's ${FIX_FILES[$i]}"
    else
      echo "  ✓ Patched ${FIX_FILES[$i]} so CitySample compiles (to undo: $SCRIPT_DIR/PatchCitySample.sh -restore)"
    fi
  elif [ "$(COUNT_LINE "$FILE" "$TO")" -eq 0 ]; then
    # Neither line: Epic has changed this code, and the fix may no longer apply or be needed.
    echo "  ! ${FIX_FILES[$i]} no longer has the line Tempo patches; left it alone."
    echo "    If CitySample fails to build there, see Plugins/Tempo/docs/guides/citysample.md."
  fi
done
