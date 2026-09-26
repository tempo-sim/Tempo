#!/usr/bin/env bash
# Copyright Tempo Simulation, LLC. All Rights Reserved

# Prints the directory of this project's TempoROS plugin, if it has one.
#
# TempoROS is a separate repository that Tempo does not vendor. A project that wants ROS adds
# TempoROS to its own Plugins folder, alongside Tempo rather than inside it, so it can be anywhere
# Unreal scans for plugins and Tempo must not assume a path.
#
# Exit: 0 and print the directory, 1 if this project has no TempoROS.

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)

# Skips the directories Unreal ignores, and descriptors DisableConflictingPlugins.sh has renamed
# out of Unreal's sight -- if Unreal cannot see it, neither should we.
DESCRIPTOR=$(find "$PROJECT_ROOT/Plugins" \
  \( -name Intermediate -o -name Saved -o -name Binaries -o -name DerivedDataCache -o -name .git \) -prune \
  -o -name "TempoROS.uplugin" -print -quit)

if [ -z "$DESCRIPTOR" ]; then
  echo "No TempoROS plugin found under $PROJECT_ROOT/Plugins" >&2
  exit 1
fi

dirname "$DESCRIPTOR"
