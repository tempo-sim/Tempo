#!/usr/bin/env bash

set -e

# FastGeoStreaming (UE 5.6+) converts a World Partition level's static actors at cook time into
# lightweight primitives that are not UPrimitiveComponents. Tempo's labeler works by setting each
# component's custom-depth stencil, so it cannot reach them, and worse than missing labels: those
# primitives do not draw into custom depth at all, so whatever labeled object is behind one shows
# through it -- the sky's label where a building stands. A project that cooks with FastGeo gets
# label images and lidar labels that are wrong across most of its static world.
#
# FastGeo.Enable is read at cook time, so this turns it off in the project's own config (a later
# key in an ini overrides an earlier one, so appending wins over the project's own setting) and the
# next package cooks the level as ordinary actors. CitySample shipped that way before 5.6. It
# costs some streaming performance.
#
# The block is marker-delimited, so re-running this is a no-op and -restore removes it. Setup.sh
# runs it unless given -keep-fastgeo.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)
ENGINE_INI="$PROJECT_ROOT/Config/DefaultEngine.ini"

BEGIN_MARKER="; TEMPO_FASTGEO_BLOCK BEGIN"
END_MARKER="; TEMPO_FASTGEO_BLOCK END"

RESTORE=0
for ARG in "$@"; do
  case "$ARG" in
    -restore)
      RESTORE=1
      ;;
  esac
done

if [ ! -f "$ENGINE_INI" ]; then
  exit 0
fi

HAS_BLOCK=0
if grep -qF "$BEGIN_MARKER" "$ENGINE_INI" && grep -qF "$END_MARKER" "$ENGINE_INI"; then
  HAS_BLOCK=1
fi

if [ "$RESTORE" -eq 1 ]; then
  if [ "$HAS_BLOCK" -eq 1 ]; then
    sed "/$BEGIN_MARKER/,/$END_MARKER/d" "$ENGINE_INI" > "$ENGINE_INI.tmp"
    cat "$ENGINE_INI.tmp" > "$ENGINE_INI"
    rm -f "$ENGINE_INI.tmp"
    echo "Removed Tempo's FastGeo override from $ENGINE_INI. Re-package for it to take effect."
  else
    echo "No Tempo FastGeo override in $ENGINE_INI."
  fi
  exit 0
fi

if [ "$HAS_BLOCK" -eq 1 ]; then
  exit 0
fi

# Only a project that turns FastGeo on needs turning it off. The cvar defaults to on whenever the
# plugin is loaded, but a project only gets FastGeo-cooked levels by enabling the plugin, and the
# projects that do so (CitySample) also set the cvar explicitly; match either.
UPROJECT_FILE=$(find "$PROJECT_ROOT" -maxdepth 1 -name "*.uproject" -print -quit)
PLUGIN_ENABLED=0
if [ -n "$UPROJECT_FILE" ] && tr -d '\r\n\t ' < "$UPROJECT_FILE" | grep -q '"Name":"FastGeoStreaming","Enabled":true'; then
  PLUGIN_ENABLED=1
fi
CVAR_ENABLED=0
if grep -qiE '^[[:space:]]*FastGeo\.Enable[[:space:]]*=[[:space:]]*(true|1)[[:space:]]*\r?$' "$ENGINE_INI"; then
  CVAR_ENABLED=1
fi
if [ "$PLUGIN_ENABLED" -eq 0 ] && [ "$CVAR_ENABLED" -eq 0 ]; then
  exit 0
fi

{
  echo ""
  echo "$BEGIN_MARKER -- added by Tempo's Setup.sh; remove with Plugins/Tempo/Scripts/DisableFastGeo.sh -restore"
  echo "; Tempo's labeler cannot label FastGeo primitives, and they corrupt the labels behind them, so cook"
  echo "; the level as ordinary actors. Overrides FastGeo.Enable above: in an ini, the later key wins."
  echo "[ConsoleVariables]"
  echo "FastGeo.Enable=False"
  echo "$END_MARKER"
} >> "$ENGINE_INI"

echo "  ✓ Turned off FastGeo for this project's cook ($ENGINE_INI)."
echo "    (Tempo's labeler cannot label FastGeo primitives.) It takes effect at the next package."
echo "    To undo: $SCRIPT_DIR/DisableFastGeo.sh -restore"
