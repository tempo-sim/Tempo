#!/usr/bin/env bash

set -e

# Project settings Tempo needs and a host project may not have, written as one marker-delimited
# block at the end of the project's Config/DefaultEngine.ini. In an ini a later key overrides an
# earlier one, so the block wins over the project's own values without editing them, and -restore
# takes it out again. Setup.sh runs this.
#
# Settings Tempo can impose on every project unconditionally (physics substepping, FastGeo off) live
# in TempoCore/Config/Engine.ini instead, which Unreal layers over the project's config. Only what
# must depend on the project's own value belongs here:
#
# - WorldSettingsClassName -> ATempoWorldSettings, when the project uses the engine's AWorldSettings.
#   Tempo's time control (pause, step, Fixed Step) lives on ATempoWorldSettings, and every time RPC
#   fails without it. A map saved with AWorldSettings gets the configured class when it is loaded in
#   the editor or the cook (UWorld::RepairWorldSettings copies its properties across), so a package
#   made after this has Tempo's world settings in every level. A project with a world-settings class
#   of its own is left alone (with a warning), since forcing ours would replace it in its maps.
#
# It is read when the project is packaged: re-package after running this.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
ENGINE_INI="$PROJECT_ROOT/Config/DefaultEngine.ini"

BEGIN_MARKER="; TEMPO_PROJECT_CONFIG BEGIN"
END_MARKER="; TEMPO_PROJECT_CONFIG END"
# The block this script used to write when it only handled FastGeo (DisableFastGeo.sh).
OLD_BEGIN_MARKER="; TEMPO_FASTGEO_BLOCK BEGIN"
OLD_END_MARKER="; TEMPO_FASTGEO_BLOCK END"

RESTORE=0
for ARG in "$@"; do
  case "$ARG" in
    -restore) RESTORE=1 ;;
  esac
done

if [ ! -f "$ENGINE_INI" ]; then
  exit 0
fi

REMOVE_BLOCK() {
  local BEGIN="$1" END="$2"
  if grep -qF "$BEGIN" "$ENGINE_INI" && grep -qF "$END" "$ENGINE_INI"; then
    sed "/$BEGIN/,/$END/d" "$ENGINE_INI" > "$ENGINE_INI.tmp"
    cat "$ENGINE_INI.tmp" > "$ENGINE_INI"
    rm -f "$ENGINE_INI.tmp"
    return 0
  fi
  return 1
}

# Start from the project's own settings: take out whatever this script wrote before (including the
# substepping and FastGeo lines older versions wrote, now in TempoCore/Config/Engine.ini).
HAD_BLOCK=0
REMOVE_BLOCK "$BEGIN_MARKER" "$END_MARKER" && HAD_BLOCK=1
REMOVE_BLOCK "$OLD_BEGIN_MARKER" "$OLD_END_MARKER" && HAD_BLOCK=1
# Drop the blank line the block was appended after, so -restore leaves the file as it found it.
if [ "$HAD_BLOCK" -eq 1 ]; then
  sed -e :a -e '/^\n*$/{$d;N;ba' -e '}' "$ENGINE_INI" > "$ENGINE_INI.tmp"
  cat "$ENGINE_INI.tmp" > "$ENGINE_INI"
  rm -f "$ENGINE_INI.tmp"
fi

if [ "$RESTORE" -eq 1 ]; then
  if [ "$HAD_BLOCK" -eq 1 ]; then
    echo "Removed Tempo's settings from $ENGINE_INI. Re-package for that to take effect."
  else
    echo "No Tempo settings in $ENGINE_INI."
  fi
  exit 0
fi

# The value the project gives a key, if any (last one wins, as in Unreal). Section-blind, which is
# enough for this key: its name is unique to its section.
PROJECT_VALUE() {
  grep -iE "^[[:space:]]*$1[[:space:]]*=" "$ENGINE_INI" | tail -1 | sed -E 's/^[^=]*=[[:space:]]*//; s/[[:space:]]*\r?$//'
}

LINES=()
APPLIED=()

WORLD_SETTINGS=$(PROJECT_VALUE "WorldSettingsClassName")
if [ -z "$WORLD_SETTINGS" ] || [ "$WORLD_SETTINGS" = "/Script/Engine.WorldSettings" ]; then
  LINES+=("[/Script/Engine.Engine]" "WorldSettingsClassName=/Script/TempoCore.TempoWorldSettings")
  APPLIED+=("world settings -> ATempoWorldSettings (Tempo's time control needs it)")
elif [ "$WORLD_SETTINGS" != "/Script/TempoCore.TempoWorldSettings" ]; then
  echo "  ! This project's world settings class is $WORLD_SETTINGS. Tempo's time control needs"
  echo "    ATempoWorldSettings: derive that class from it, or pause, step and Fixed Step won't work."
fi

if [ "${#LINES[@]}" -eq 0 ]; then
  exit 0
fi

{
  echo ""
  echo "$BEGIN_MARKER -- added by Tempo's Setup.sh; remove with Plugins/Tempo/Scripts/ConfigureProject.sh -restore"
  echo "; Settings Tempo needs. In an ini the later key wins, so these override the project's own above."
  printf '%s\n' "${LINES[@]}"
  echo "$END_MARKER"
} >> "$ENGINE_INI"

echo "  ✓ Configured this project for Tempo ($ENGINE_INI):"
printf '      - %s\n' "${APPLIED[@]}"
echo "    These take effect at the next package. To undo: $SCRIPT_DIR/ConfigureProject.sh -restore"
