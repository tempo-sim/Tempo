#!/usr/bin/env bash

set -e

TEMPO_ROOT=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

cd $TEMPO_ROOT

if [ -z "$GIT_DIR" ]; then
	GIT_DIR=$(git rev-parse --git-common-dir);
	if [ $? -ne 0 ]; then
                echo "Failed to find .git folder"
                exit 1
	fi
fi

ADD_COMMAND_TO_HOOK() {
  COMMAND=$1
  HOOK=$2
  HOOK_FILE="$GIT_DIR/hooks/$HOOK"

  if [ ! -f "$HOOK_FILE" ]; then
    touch "$HOOK_FILE"
    echo -e "#!/usr/bin/env bash\n" > "$HOOK_FILE"
    chmod +x "$HOOK_FILE"
  fi

  if ! grep -qF "$COMMAND" "$HOOK_FILE"; then
    echo "$COMMAND" >> "$HOOK_FILE"
  fi
}

SYNC_DEPS="$TEMPO_ROOT/Scripts/SyncDeps.sh"
INSTALL_ENGINE_MODS="$TEMPO_ROOT/Scripts/InstallEngineMods.sh"

# Put SyncDeps.sh and InstallEngineMods.sh scripts in appropriate git hooks
if [ -d "$GIT_DIR/hooks" ]; then
  ADD_COMMAND_TO_HOOK "$SYNC_DEPS" post-checkout
  ADD_COMMAND_TO_HOOK "$SYNC_DEPS" post-merge
  ADD_COMMAND_TO_HOOK "$INSTALL_ENGINE_MODS" post-checkout
  ADD_COMMAND_TO_HOOK "$INSTALL_ENGINE_MODS" post-merge
fi

# Run the steps once (adding -force if specified)
if [ "$1" = "-force" ]; then
  SYNC_DEPS="$SYNC_DEPS -force"
  INSTALL_ENGINE_MODS="$INSTALL_ENGINE_MODS -force"
fi
echo -e "\nInstalling Tempo Engine Mods\n"
eval "$INSTALL_ENGINE_MODS"
echo -e "Checking ThirdParty dependencies...\n"
eval "$SYNC_DEPS"

PLUGIN_SETUP_SCRIPTS=$(find "$TEMPO_ROOT" -mindepth 2 -maxdepth 2 -name "Setup.sh" -print -quit)
for PLUGIN_SETUP_SCRIPT in ${PLUGIN_SETUP_SCRIPTS[@]}; do
  eval "$PLUGIN_SETUP_SCRIPT"
done
