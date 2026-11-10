#!/bin/bash
# update.sh: the weekly tool update (a root LaunchDaemon runs it). Waits until no job is running, stops the
# runners so none starts mid-update, upgrades the tools the jobs use, and starts the runners again. A
# failure is posted to Slack; success is silent.
#
# It runs as root, so it is installed root-owned (/usr/local/libexec/tempo-ci) and reads nothing the CI
# user can write: jobs run PR code as that user. Whatever runs as the CI user or the Homebrew owner is
# started through sudo -u, which drops to that user.
#
# macOS and Xcode are not updated here: they change the toolchain under every build, so daily.sh reports
# them and a human installs them.
set -u
CI_USER=${TEMPO_CI_USER:-ci}
CI_UID=$(id -u "$CI_USER") || exit 1
CI_HOME=$(dscl . -read "/Users/$CI_USER" NFSHomeDirectory | awk '{print $2}')
BREW=/opt/homebrew/bin/brew
# The tools the jobs use. Everything else in Homebrew belongs to whoever owns it.
FORMULAS="uv cmake jq gh"
MAX_WAIT_MINUTES=${TEMPO_CI_UPDATE_MAX_WAIT_MINUTES:-360}

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*"; }
notify() { sudo -u "$CI_USER" -H "$CI_HOME/tempo-ci/bin/notify.sh" "$1" || log "notify failed: $1"; }

waited=0
while pgrep -f 'Runner.Worker' > /dev/null; do
  if [ "$waited" -ge "$MAX_WAIT_MINUTES" ]; then
    notify "weekly update skipped: a job was running for all of ${MAX_WAIT_MINUTES} minutes."
    exit 1
  fi
  sleep 300
  waited=$(( waited + 5 ))
done

plists=$(ls "$CI_HOME"/Library/LaunchAgents/actions.runner.*.plist 2>/dev/null)
start_runners() {
  local plist
  for plist in $plists; do
    launchctl bootstrap "gui/$CI_UID" "$plist" 2>/dev/null \
      || launchctl print "gui/$CI_UID/$(basename "$plist" .plist)" > /dev/null 2>&1 \
      || notify "weekly update: could not restart $(basename "$plist" .plist)."
  done
}
log "update: stopping runners"
for plist in $plists; do
  launchctl bootout "gui/$CI_UID/$(basename "$plist" .plist)" 2>/dev/null
done
trap start_runners EXIT

failures=""
if [ -x "$BREW" ]; then
  owner=$(stat -f %Su "$(dirname "$(dirname "$BREW")")")
  installed=$(sudo -u "$owner" -H "$BREW" list --formula -1)
  upgrade=""
  for f in $FORMULAS; do echo "$installed" | grep -qxF "$f" && upgrade="$upgrade $f"; done
  log "update: brew upgrade$upgrade (as $owner)"
  # shellcheck disable=SC2086 # $upgrade is a list of formulas
  sudo -u "$owner" -H env HOMEBREW_NO_ANALYTICS=1 "$BREW" update --quiet \
    && sudo -u "$owner" -H env HOMEBREW_NO_ANALYTICS=1 "$BREW" upgrade --quiet $upgrade \
    || failures="$failures brew"
fi
if [ -x "$CI_HOME/.cargo/bin/rustup" ]; then
  log "update: rustup update"
  sudo -u "$CI_USER" -H "$CI_HOME/.cargo/bin/rustup" update stable --no-self-update || failures="$failures rustup"
fi

if [ -n "$failures" ]; then
  notify "weekly update failed:$failures. See /var/log/tempo-ci-update.log."
  exit 1
fi
log "update: done"
