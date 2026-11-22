#!/usr/bin/env bash

set -e

# Fixes in Epic's CitySample source that a CitySample project needs before it builds and runs.
# Setup.sh runs this; in any other project it finds none of the files and does nothing. Each fix is
# an exact replacement of whole lines, so running it again changes nothing, and -restore puts Epic's
# lines back. See docs/guides/citysample.md.
#
# - CitySampleBlueprintLibrary.cpp: GetWorldInfo's `for` loop over TActorIterator returns from its
#   body unconditionally, so `++It` never runs. CitySample's targets build at BuildSettingsVersion.V7,
#   which (new in UE 5.8) makes unreachable code an error on Clang too, so a stock CitySample fails
#   on macOS and Linux with -Wunreachable-code-loop-increment. The fix is the equivalent `if` with an
#   initializer: TActorIterator's constructor already advances to the first match, and its explicit
#   operator bool is the same check the loop condition made.
#
# - CitySampleVehicleBase.cpp: a packaged game crashes when the player gets into a car.
#   ACitySampleVehicleBase::PossessedBy moves FMassTrafficPlayerVehicleTag between Mass entities with
#   Mass's synchronous API, which asserts while Mass is processing, and in UE 5.8 game builds a Mass
#   phase can still be in flight when gameplay code runs (the editor waits for it, so PIE never
#   crashes). The fix defers the tag changes to Mass's command buffer while it is processing, and
#   makes them at once otherwise, as before.

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)

RESTORE=0
for ARG in "$@"; do
  case "$ARG" in
    -restore) RESTORE=1 ;;
  esac
done

# Each fix: file (relative to the project root), Epic's lines, Tempo's lines, and what it fixes. A fix
# replaces consecutive whole lines. They are written here with tabs for indentation relative to the
# first line. In the file the first line may be indented any amount, the lines after it must be
# indented that much plus what is written here, and line endings may be LF or CRLF. Replacement
# lines get the first line's indentation and line ending, so both survive a patch and a restore.
FIX_FILES=(
  "Source/CitySample/Util/CitySampleBlueprintLibrary.cpp"
  "Source/CitySample/Vehicles/CitySampleVehicleBase.cpp"
  "Source/CitySample/Vehicles/CitySampleVehicleBase.cpp"
  "Source/CitySample/Vehicles/CitySampleVehicleBase.cpp"
  "Source/CitySample/Vehicles/CitySampleVehicleBase.cpp"
)
FIX_ORIGINALS=(
  "for (TActorIterator<ACitySampleWorldInfo> It(World, ACitySampleWorldInfo::StaticClass()); It; ++It)"
  '#include "MassEntityManager.h"'
  "EntitySubsystem->GetMutableEntityManager().RemoveTagFromEntity(MassAgentHandle, FMassTrafficPlayerVehicleTag::StaticStruct());"
  "if (UMassActorSubsystem* MassActorSubsystem = UWorld::GetSubsystem<UMassActorSubsystem>(World))"
  "UE::MassActor::AddEntityTagToActor<FMassTrafficPlayerVehicleTag>(*this);"
)
FIX_REPLACEMENTS=(
  "if (TActorIterator<ACitySampleWorldInfo> It(World, ACitySampleWorldInfo::StaticClass()); It)"
  $'#include "MassCommandBuffer.h"\n#include "MassEntityManager.h"'
  $'FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
// PossessedBy can run while a Mass phase is still in flight (in UE 5.8 game builds, phases
// don\'t block the game thread), where synchronous composition changes assert. Defer in that case.
if (EntityManager.IsProcessing())
{
\tEntityManager.Defer().RemoveTag<FMassTrafficPlayerVehicleTag>(MassAgentHandle);
}
else
{
\tEntityManager.RemoveTagFromEntity(MassAgentHandle, FMassTrafficPlayerVehicleTag::StaticStruct());
}'
  $'UMassEntitySubsystem* EntitySubsystem = UWorld::GetSubsystem<UMassEntitySubsystem>(World);
UMassActorSubsystem* MassActorSubsystem = UWorld::GetSubsystem<UMassActorSubsystem>(World);
if (EntitySubsystem && MassActorSubsystem)'
  $'// Not using UE::MassActor::AddEntityTagToActor here: it calls the synchronous
// AddTagToEntity, which asserts if this runs during Mass processing.
FMassEntityManager& EntityManager = EntitySubsystem->GetMutableEntityManager();
if (EntityManager.IsProcessing())
{
\tEntityManager.Defer().AddTag<FMassTrafficPlayerVehicleTag>(MassAgentHandle);
}
else
{
\tEntityManager.AddTagToEntity(MassAgentHandle, FMassTrafficPlayerVehicleTag::StaticStruct());
}'
)
FIX_PURPOSES=(
  "so CitySample compiles on Clang"
  "so a packaged game doesn't crash when the player gets into a car"
  "so a packaged game doesn't crash when the player gets into a car"
  "so a packaged game doesn't crash when the player gets into a car"
  "so a packaged game doesn't crash when the player gets into a car"
)

# The regex matching the lines in $_[0] (see above), capturing the first line's indentation as `ind`
# and its line ending as `eol`. The last line's ending is left unmatched, so it stays in the file.
# Shared by COUNT_LINES and REPLACE_LINES.
MATCH_LINES_PL='
  sub lines_re {
    my @lines = split /\n/, $_[0], -1;
    my $re = "^(?<ind>[ \\t]*)";
    for my $i (0 .. $#lines) {
      my ($ws, $text) = $lines[$i] =~ /^([ \t]*)(.*)$/;
      $re .= $i == 0 ? quotemeta($text) . "(?=(?<eol>\\r?)\$)"
                     : "\\r?\\n" . ($text eq "" ? "[ \\t]*" : "\\g{ind}" . quotemeta($ws . $text));
    }
    return $re . "(?=\\r?\$)";
  }
'

# Counts the places file $1 has the lines $2.
COUNT_LINES() {
  LINES="$2" perl -0777 -ne "$MATCH_LINES_PL"'
    my $re = lines_re($ENV{LINES}); my $n = () = /$re/mg; print $n' "$1"
}

# Replaces the lines $2 with the lines $3 in file $1, keeping indentation and line endings.
REPLACE_LINES() {
  FROM="$2" TO="$3" perl -0777 -pi -e "$MATCH_LINES_PL"'
    my $re = lines_re($ENV{FROM});
    s{$re}{
      my ($ind, $eol) = ($+{ind}, $+{eol});
      join "$eol\n", map { $_ eq "" ? "" : $ind . $_ } split /\n/, $ENV{TO}, -1
    }mge' "$1"
}

# The entries with the same file and purpose are one fix, applied whole or not at all. (Plain
# strings of "|key|" rather than associative arrays, which macOS's bash 3.2 lacks.)
REPORTED=""
REPORT() {
  case "$REPORTED" in *"|$1|"*) return ;; esac
  REPORTED="$REPORTED|$1|"
  echo "$1"
}

# First pass: each entry's state. Whether it's applied is decided by Tempo's lines alone: they may
# contain Epic's (an added #include does), so Epic's line can still be there after patching.
STATES=()
BLOCKED=""
for i in "${!FIX_FILES[@]}"; do
  FILE="$PROJECT_ROOT/${FIX_FILES[$i]}"
  if [ ! -f "$FILE" ]; then
    STATES[$i]=absent
  elif [ "$(COUNT_LINES "$FILE" "${FIX_REPLACEMENTS[$i]}")" -gt 0 ]; then
    STATES[$i]=patched
  elif [ "$(COUNT_LINES "$FILE" "${FIX_ORIGINALS[$i]}")" -gt 0 ]; then
    STATES[$i]=original
  else
    # Neither: Epic has changed this code, and the fix may no longer apply or be needed.
    STATES[$i]=changed
    BLOCKED="$BLOCKED|${FIX_FILES[$i]}:${FIX_PURPOSES[$i]}|"
  fi
done

for i in "${!FIX_FILES[@]}"; do
  if [ "$RESTORE" -eq 1 ]; then
    if [ "${STATES[$i]}" = patched ]; then
      REPLACE_LINES "$PROJECT_ROOT/${FIX_FILES[$i]}" "${FIX_REPLACEMENTS[$i]}" "${FIX_ORIGINALS[$i]}"
      REPORT "  ✓ Restored Epic's ${FIX_FILES[$i]}"
    fi
    continue
  fi
  case "$BLOCKED" in
    *"|${FIX_FILES[$i]}:${FIX_PURPOSES[$i]}|"*)
      REPORT "  ! ${FIX_FILES[$i]} no longer has the lines Tempo patches ${FIX_PURPOSES[$i]}; left it alone."
      REPORT "    If CitySample fails there, see Plugins/Tempo/docs/guides/citysample.md."
      continue ;;
  esac
  if [ "${STATES[$i]}" = original ]; then
    REPLACE_LINES "$PROJECT_ROOT/${FIX_FILES[$i]}" "${FIX_ORIGINALS[$i]}" "${FIX_REPLACEMENTS[$i]}"
    REPORT "  ✓ Patched ${FIX_FILES[$i]} ${FIX_PURPOSES[$i]} (to undo: $SCRIPT_DIR/PatchCitySample.sh -restore)"
  fi
done
