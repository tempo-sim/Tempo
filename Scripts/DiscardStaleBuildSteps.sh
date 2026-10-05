#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

# Discards UnrealBuildTool makefiles whose cached custom build steps run a program that no longer
# exists, so the next build regenerates them.
#
# UBT writes each target's custom build steps to Intermediate/Build/<Platform>/<Arch>/<Target>/
# <Config>/PreBuild-N.{sh,bat} when it creates that target's makefile, with $(PluginDir) and friends
# already expanded to absolute paths. On a later build it runs those cached scripts BEFORE
# validating the makefile: in BuildMode.cs, ExecuteCustomBuildSteps(Makefile.PreBuildScripts) comes
# ahead of TargetMakefile.IsValidForSourceFiles, which is where a deleted .uplugin would be noticed
# (TargetMakefile.Load, which runs first, only checks Build.version, the .uproject timestamp, the
# UBT assembly, the command line and config).
#
# So when a plugin contributing pre-build steps moves -- as TempoROS did, out of Tempo and into the
# project's own Plugins folder -- the cached script runs with its old paths, fails, and aborts the
# build before the makefile can be invalidated. Every later build fails identically, because nothing
# rewrites those scripts while the makefile that owns them still loads. Deleting the makefile breaks
# the cycle; the object files and dependency cache beside it stay, so the next build is incremental.
#
# Cheap and silent in the normal case: a handful of small files per target.

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)

BUILD_DIR="$PROJECT_ROOT/Intermediate/Build"
if [ ! -d "$BUILD_DIR" ]; then
  exit 0
fi

# Paths in a generated script are native, so on Windows they are backslashed -- which Git Bash's
# file tests only understand with forward slashes.
NORMALIZE() {
  printf '%s' "${1//\\//}"
}

# The program a generated command line runs: its first token, or, when that is a Python
# interpreter, the script handed to it. UBT quotes every expanded path, so quoted tokens are the
# common case; an unquoted first word is handled too.
STEP_PROGRAM() {
  local LINE="$1"
  local FIRST SECOND
  FIRST=$(printf '%s' "$LINE" | sed -n 's/^"\([^"]*\)".*/\1/p')
  SECOND=$(printf '%s' "$LINE" | sed -n 's/^"[^"]*"[[:space:]]*"\([^"]*\)".*/\1/p')
  if [ -z "$FIRST" ]; then
    FIRST="${LINE%% *}"
    SECOND=""
  fi
  case $(basename "$(NORMALIZE "$FIRST")") in
    python|python3|python.exe|python3.exe)
      printf '%s' "$SECOND"
      ;;
    *)
      printf '%s' "$FIRST"
      ;;
  esac
}

while IFS= read -r MAKEFILE <&3; do
  STEP_DIR=$(dirname "$MAKEFILE")
  for STEP_SCRIPT in "$STEP_DIR"/PreBuild-*.sh "$STEP_DIR"/PreBuild-*.bat \
                     "$STEP_DIR"/PostBuild-*.sh "$STEP_DIR"/PostBuild-*.bat; do
    if [ ! -f "$STEP_SCRIPT" ]; then
      continue
    fi
    while IFS= read -r LINE || [ -n "$LINE" ]; do
      LINE="${LINE%$'\r'}"
      # Skip blank lines and the "@echo off" a generated .bat starts with.
      case "$LINE" in
        ''|@*) continue ;;
      esac
      PROGRAM=$(NORMALIZE "$(STEP_PROGRAM "$LINE")")
      # Only an absolute path can be checked without guessing at the step's PATH or working
      # directory. Everything else (a bare command name, a relative path) is left alone.
      case "$PROGRAM" in
        /*|[A-Za-z]:/*) ;;
        *) continue ;;
      esac
      if [ ! -e "$PROGRAM" ]; then
        echo "Discarding stale build steps in ${STEP_DIR#"$PROJECT_ROOT"/}:"
        echo "  $(basename "$STEP_SCRIPT") runs $PROGRAM, which no longer exists."
        echo "  Deleting $(basename "$MAKEFILE") so this build regenerates them (object files are kept)."
        rm -f "$MAKEFILE"
        break 2
      fi
    done < "$STEP_SCRIPT"
  done
done 3< <(find "$BUILD_DIR" -maxdepth 6 -type f -name Makefile.bin)
