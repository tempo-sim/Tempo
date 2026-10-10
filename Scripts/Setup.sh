#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$( cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd )
PROJECT_ROOT=$("$SCRIPT_DIR"/FindProjectRoot.sh)

cd "$TEMPO_ROOT"

SKIP_HOOKS=0
EXTRA_ARGS=()
for ARG in "$@"; do
  case "$ARG" in
    -skip-hooks)
      SKIP_HOOKS=1
      ;;
    -force)
      EXTRA_ARGS+=("-force")
      ;;
  esac
done

ADD_COMMAND_TO_HOOK() {
  COMMAND=$1
  HOOK=$2
  HOOK_FILE="$GIT_DIR/hooks/$HOOK"

  if [ ! -f "$HOOK_FILE" ]; then
    touch "$HOOK_FILE"
    echo -e "#!/usr/bin/env bash\n" > "$HOOK_FILE"
    # Prompting from a hook needs the terminal on stdin, but opening it must not
    # fail the hook (and so the checkout) where there is no controlling
    # terminal, e.g. CI or a GUI git client. Testing for existence is not
    # enough: the node exists there, and only opening it fails.
    # https://stackoverflow.com/questions/3417896/how-do-i-prompt-the-user-from-within-a-commit-msg-hook
    echo 'if { : < /dev/tty; } 2>/dev/null; then exec < /dev/tty; fi' >> "$HOOK_FILE"
    chmod +x "$HOOK_FILE"
  fi

  # Match on the bare path so an existing hook line is recognised whether it was
  # written quoted or, by an older Setup.sh, unquoted. Matching the quoted form
  # would miss a legacy unquoted line and append a duplicate beside it.
  if ! grep -qF "$COMMAND" "$HOOK_FILE"; then
    echo "\"$COMMAND\"" >> "$HOOK_FILE"
  fi
}

# Earlier versions of this script also put InstallEngineMods.sh in the hooks. Tempo no longer
# modifies the engine and that script is gone, so take it out of any hook that still runs it.
REMOVE_ENGINE_MODS_FROM_HOOK() {
  HOOK_FILE="$GIT_DIR/hooks/$1"
  if [ -f "$HOOK_FILE" ] && grep -qF "InstallEngineMods.sh" "$HOOK_FILE"; then
    grep -vF "InstallEngineMods.sh" "$HOOK_FILE" > "$HOOK_FILE.tmp" || true
    cat "$HOOK_FILE.tmp" > "$HOOK_FILE"
    rm -f "$HOOK_FILE.tmp"
  fi
}

# Earlier versions of this script also pointed the project's *.Target.cs files at Tempo's own
# toolchains, which no longer exist: an engine without them fails to build such a target with
# "Unable to create toolchain 'TempoVCToolChain'". The block it added is marker-delimited, so take
# it back out, and warn about any selection of a Tempo toolchain made without the markers.
REMOVE_TOOLCHAIN_BLOCK_FROM_TARGET_FILE() {
  TARGET_FILE="$1"
  if grep -q "TEMPO_TOOLCHAIN_BLOCK BEGIN" "$TARGET_FILE" && grep -q "TEMPO_TOOLCHAIN_BLOCK END" "$TARGET_FILE"; then
    sed '/TEMPO_TOOLCHAIN_BLOCK BEGIN/,/TEMPO_TOOLCHAIN_BLOCK END/d' "$TARGET_FILE" > "$TARGET_FILE.tmp"
    cat "$TARGET_FILE.tmp" > "$TARGET_FILE"
    rm -f "$TARGET_FILE.tmp"
    echo "Removed the Tempo toolchain block an earlier Setup.sh added to $(basename "$TARGET_FILE"). Tempo no longer provides toolchains."
  fi
  if grep -q 'ToolChainName[[:space:]]*=[[:space:]]*"Tempo' "$TARGET_FILE"; then
    echo "WARNING: $(basename "$TARGET_FILE") still selects a Tempo toolchain, which Tempo no longer provides."
    echo "Remove the lines setting ToolChainName, or builds will fail with \"Unable to create toolchain\"."
    echo "See Tempo's docs/migration/engine-mods-removal.md."
  fi
}

# Warn if the engine itself still carries Tempo's old modifications. Builds work with them (the
# superseded pieces warn and go unused), but the engine should go back to the way Epic ships it.
WARN_IF_ENGINE_STILL_MODIFIED() {
  UNREAL_ENGINE_ROOT=$("$SCRIPT_DIR"/FindUnreal.sh 2>/dev/null) || UNREAL_ENGINE_ROOT=""
  if [ -d "$UNREAL_ENGINE_ROOT" ]; then
    if [ -d "$UNREAL_ENGINE_ROOT/TempoMods" ]; then
      echo -e "\nWARNING: your Unreal installation at"
      echo "  $UNREAL_ENGINE_ROOT"
      echo "still carries the modification record an earlier Tempo installed (TempoMods folder). Tempo no longer uses or needs engine mods."
      echo "They are harmless for now, but restore the engine to the way Epic ships it when convenient:"
      echo "On Mac or Windows: verify the installation in the Epic Games Launcher and remove the TempoMods folder"
      echo "  $UNREAL_ENGINE_ROOT/TempoMods"
      echo "On Linux, re-download and re-extract the engine from the Linux download page"
      echo "See Tempo's docs/migration/engine-mods-removal.md."
    fi
  fi
}

SYNC_DEPS="$SCRIPT_DIR/SyncDeps.sh"

if [ "$SKIP_HOOKS" -ne 1 ]; then
  if [ -z "$GIT_DIR" ]; then
    GIT_DIR=$(git rev-parse --git-common-dir) || GIT_DIR="";
    if [ -z "$GIT_DIR" ]; then
      echo "Failed to find .git folder"
      exit 1
    fi
  fi

  # Put the SyncDeps.sh script in appropriate git hooks
  if [ -d "$GIT_DIR/hooks" ]; then
    ADD_COMMAND_TO_HOOK "$SYNC_DEPS" post-checkout
    ADD_COMMAND_TO_HOOK "$SYNC_DEPS" post-merge
    REMOVE_ENGINE_MODS_FROM_HOOK post-checkout
    REMOVE_ENGINE_MODS_FROM_HOOK post-merge
  fi
fi

# Run the steps once (adding -force if specified)
echo -e "\nDisabling project plugins that Tempo replaces\n"
bash "$SCRIPT_DIR/DisableConflictingPlugins.sh"
for TARGET_FILE in "$PROJECT_ROOT/Source/"*.Target.cs; do
  [ -f "$TARGET_FILE" ] || continue
  REMOVE_TOOLCHAIN_BLOCK_FROM_TARGET_FILE "$TARGET_FILE"
done
WARN_IF_ENGINE_STILL_MODIFIED
echo -e "Checking ThirdParty dependencies...\n"
bash "$SYNC_DEPS" "${EXTRA_ARGS[@]}"

# TempoROS is a separate repository, added to the project alongside Tempo rather than inside it, so
# a project only has one if it asked for ROS. If this one does, set it up as well: it manages its
# own third party dependencies and Unreal builds it like any other project plugin. Pass the same
# arguments on: without them, `Setup.sh -force` would stop being forced at the plugin boundary, and
# a plugin whose dependencies need updating drops back to an interactive prompt - which is exactly
# what the caller used -force to avoid.
TEMPOROS_DIR=$("$SCRIPT_DIR"/FindTempoROS.sh 2>/dev/null) || TEMPOROS_DIR=""

if [ -n "$TEMPOROS_DIR" ] && [ -f "$TEMPOROS_DIR/Setup.sh" ]; then
  echo -e "\nSetting up TempoROS\n"
  if [ "$SKIP_HOOKS" -eq 1 ] && [ -f "$TEMPOROS_DIR/Scripts/SyncDeps.sh" ]; then
    # TempoROS's Setup.sh is its git hook installation plus its SyncDeps.sh, and it needs a git
    # context for the former. -skip-hooks runs where there may be none (CI containers, chiefly),
    # so go straight to the part that is wanted.
    bash "$TEMPOROS_DIR/Scripts/SyncDeps.sh" "${EXTRA_ARGS[@]}"
  else
    bash "$TEMPOROS_DIR/Setup.sh" "${EXTRA_ARGS[@]}"
  fi
elif [ -z "$TEMPOROS_DIR" ]; then
  # TempoROSBridge is the only Tempo plugin that requires TempoROS, and it is opt-in. If this
  # project has opted into it without adding TempoROS, say so now: the build would otherwise fail
  # later with UnrealBuildTool's "Unable to find plugin 'TempoROS'", which says less about the fix.
  UPROJECT_FILE=$(find "$PROJECT_ROOT" -maxdepth 1 -name "*.uproject" -print -quit)
  BRIDGE_ENABLED=$(jq -r 'first(.Plugins[]?
      | select((.Name // "") == "TempoROSBridge")
      | (if (has("Enabled") | not) or .Enabled then "true" else "false" end)) // "false"' "$UPROJECT_FILE")
  BRIDGE_ENABLED="${BRIDGE_ENABLED%$'\r'}"
  if [ "$BRIDGE_ENABLED" = "true" ]; then
    echo -e "\nWARNING: $(basename "$UPROJECT_FILE") enables TempoROSBridge, but this project has no TempoROS plugin."
    echo "TempoROS is a separate repository. Add it beside Tempo, from $PROJECT_ROOT/Plugins:"
    echo -e "\n\tgit submodule add https://github.com/tempo-sim/TempoROS.git\n"
    echo -e "then re-run this script. Compatibility is guaranteed between Tempo main and TempoROS main.\n"
  fi
fi
