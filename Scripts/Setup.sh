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

SYNC_DEPS="$SCRIPT_DIR/SyncDeps.sh"
INSTALL_ENGINE_MODS="$SCRIPT_DIR/InstallEngineMods.sh"

if [ "$SKIP_HOOKS" -ne 1 ]; then
  if [ -z "$GIT_DIR" ]; then
    GIT_DIR=$(git rev-parse --git-common-dir) || GIT_DIR="";
    if [ -z "$GIT_DIR" ]; then
      echo "Failed to find .git folder"
      exit 1
    fi
  fi

  # Put SyncDeps.sh and InstallEngineMods.sh scripts in appropriate git hooks
  if [ -d "$GIT_DIR/hooks" ]; then
    ADD_COMMAND_TO_HOOK "$SYNC_DEPS" post-checkout
    ADD_COMMAND_TO_HOOK "$SYNC_DEPS" post-merge
    ADD_COMMAND_TO_HOOK "$INSTALL_ENGINE_MODS" post-checkout
    ADD_COMMAND_TO_HOOK "$INSTALL_ENGINE_MODS" post-merge
  fi
fi

# Run the steps once (adding -force if specified)
echo -e "\nDisabling project plugins that Tempo replaces\n"
bash "$SCRIPT_DIR/DisableConflictingPlugins.sh"
echo -e "\nAdding Tempo toolchain to Target.cs files\n"
bash "$SCRIPT_DIR/UseTempoToolchain.sh"
echo -e "\nInstalling Tempo Engine Mods\n"
bash "$INSTALL_ENGINE_MODS" "${EXTRA_ARGS[@]}"
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
  bash "$TEMPOROS_DIR/Setup.sh" "${EXTRA_ARGS[@]}"
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
