#!/usr/bin/env bash

set -e

SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )
TEMPO_ROOT=$( cd -- "$SCRIPT_DIR/.." &> /dev/null && pwd )

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
    # https://stackoverflow.com/questions/3417896/how-do-i-prompt-the-user-from-within-a-commit-msg-hook
    echo "exec < /dev/tty" >> "$HOOK_FILE"
    chmod +x "$HOOK_FILE"
  fi

  if ! grep -qF "$COMMAND" "$HOOK_FILE"; then
    echo "$COMMAND" >> "$HOOK_FILE"
  fi
}

SYNC_DEPS="$SCRIPT_DIR/SyncDeps.sh"
INSTALL_ENGINE_MODS="$SCRIPT_DIR/InstallEngineMods.sh"

if [ "$SKIP_HOOKS" -ne 1 ]; then
  if [ -z "$GIT_DIR" ]; then
    GIT_DIR=$(git rev-parse --git-common-dir);
    if [ $? -ne 0 ]; then
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
echo -e "\nAdding Tempo toolchain to Target.cs files\n"
bash "$SCRIPT_DIR/UseTempoToolchain.sh"
echo -e "\nInstalling Tempo Engine Mods\n"
bash "$INSTALL_ENGINE_MODS" "${EXTRA_ARGS[@]}"
echo -e "Checking ThirdParty dependencies...\n"
bash "$SYNC_DEPS" "${EXTRA_ARGS[@]}"

PLUGIN_SETUP_SCRIPTS=$(find "$TEMPO_ROOT" -mindepth 2 -maxdepth 2 -name "Setup.sh" -not -path "$SCRIPT_DIR/Setup.sh" -print -quit)
if [ -n "$PLUGIN_SETUP_SCRIPTS" ]; then
  bash "$PLUGIN_SETUP_SCRIPTS"
fi
