#!/usr/bin/env bash

set -e

# Unreal compiles every *.Build.cs under a project's plugins into one C# assembly, and it does so
# before it ever arbitrates between same-named plugins. Two plugins with the same module names
# therefore collide at C# compile time (CS0101/CS0111) no matter which one the engine would
# ultimately prefer. Tempo forks a few plugins that host projects (CitySample, most notably) also
# ship, so the host's copy has to be taken out of Unreal's sight entirely. Renaming its descriptor
# does that for both UnrealBuildTool and the runtime plugin manager, and is trivially reversible.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$( cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)

SUFFIX=".disabled-by-tempo"

RESTORE=0
for ARG in "$@"; do
  case "$ARG" in
    -restore)
      RESTORE=1
      ;;
  esac
done

# The plugin descriptors Tempo itself ships, by plugin name.
TEMPO_PLUGIN_NAMES=()
while IFS= read -r -d '' FILE; do
  TEMPO_PLUGIN_NAMES+=("$(basename "$FILE" .uplugin)")
done < <(find "$TEMPO_ROOT" -name "*.uplugin" -print0)

IS_TEMPO_PLUGIN_NAME() {
  local NAME="$1"
  local TEMPO_NAME
  for TEMPO_NAME in "${TEMPO_PLUGIN_NAMES[@]}"; do
    if [[ "$TEMPO_NAME" == "$NAME" ]]; then
      return 0
    fi
  done
  return 1
}

# Everything in the project except Tempo itself and directories Unreal doesn't scan for plugins.
FIND_PROJECT_FILES() {
  find "$PROJECT_ROOT" \
    \( -path "$TEMPO_ROOT" -o -name Intermediate -o -name Saved -o -name Binaries \
       -o -name DerivedDataCache -o -name .git \) -prune \
    -o -name "$1" -print0
}

if [[ "$RESTORE" -eq 1 ]]; then
  RESTORED=0
  while IFS= read -r -d '' FILE; do
    ORIGINAL="${FILE%$SUFFIX}"
    mv "$FILE" "$ORIGINAL"
    echo "  ✓ Restored $ORIGINAL"
    RESTORED=$((RESTORED + 1))
  done < <(FIND_PROJECT_FILES "*.uplugin$SUFFIX")

  if [[ "$RESTORED" -eq 0 ]]; then
    echo "No plugins were disabled by Tempo."
  else
    echo
    echo "Restored $RESTORED plugin(s). These will now conflict with Tempo's copies until you"
    echo "remove Tempo or re-run this script without -restore."
  fi
  exit 0
fi

DISABLED=0
while IFS= read -r -d '' FILE; do
  NAME=$(basename "$FILE" .uplugin)
  if ! IS_TEMPO_PLUGIN_NAME "$NAME"; then
    continue
  fi

  mv "$FILE" "$FILE$SUFFIX"
  echo "  ✓ Disabled $FILE"
  echo "    (Tempo ships its own $NAME, and Unreal cannot build both.)"
  DISABLED=$((DISABLED + 1))
done < <(FIND_PROJECT_FILES "*.uplugin")

if [[ "$DISABLED" -eq 0 ]]; then
  echo "No conflicting plugins found."
else
  echo
  echo "Disabled $DISABLED plugin(s) that Tempo replaces. Their files are untouched — only the"
  echo "*.uplugin descriptor was renamed, which hides them from UnrealBuildTool and the engine."
  echo "To undo: $SCRIPT_DIR/DisableConflictingPlugins.sh -restore"
fi
