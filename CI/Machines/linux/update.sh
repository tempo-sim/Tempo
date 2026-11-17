#!/bin/bash
# update.sh: the weekly tool update (a root systemd timer, tempo-ci-update.timer, runs it). Waits until no
# job is running, stops the runners so none starts mid-update, upgrades the tools the jobs use, and starts
# the runners again. A failure is posted to Slack; success is silent.
#
# It runs as root, so it is installed root-owned (/usr/local/libexec/tempo-ci) and reads nothing the CI
# user can write: jobs run PR code as that user. Whatever runs as the CI user is started through runuser,
# which drops to that user.
#
# The OS is not upgraded here beyond the jobs' own packages, and never rebooted: daily.sh reports pending
# updates and reboots, and a human installs them.
set -u
CI_USER=${TEMPO_CI_USER:-ci}
CI_HOME=/home/$CI_USER
# The jobs' packages (bootstrap.sh's JOB_PACKAGES). Everything else belongs to whoever installed it.
PACKAGES="git curl jq cmake clang libc++-dev libc++abi-dev binutils python3 python3-venv rsync unzip gh"
MAX_WAIT_MINUTES=${TEMPO_CI_UPDATE_MAX_WAIT_MINUTES:-360}
export DEBIAN_FRONTEND=noninteractive

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*"; }
notify() { runuser -u "$CI_USER" -- "$CI_HOME/tempo-ci/bin/notify.sh" "$1" || log "notify failed: $1"; }

waited=0
while pgrep -f 'Runner.Worker' > /dev/null; do
  if [ "$waited" -ge "$MAX_WAIT_MINUTES" ]; then
    notify "weekly update skipped: a job was running for all of ${MAX_WAIT_MINUTES} minutes."
    exit 1
  fi
  sleep 300
  waited=$(( waited + 5 ))
done

units=$(systemctl list-unit-files --no-legend --plain 'actions.runner.*.service' | awk '{print $1}')
start_runners() {
  local unit
  for unit in $units; do
    systemctl start "$unit" || notify "weekly update: could not restart $unit."
  done
}
log "update: stopping runners:" $units
# shellcheck disable=SC2086 # a list of units
[ -z "$units" ] || systemctl stop $units
trap start_runners EXIT

failures=""
installed=""
for p in $PACKAGES; do
  dpkg-query -W -f '${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' && installed="$installed $p"
done
log "update: apt-get install --only-upgrade$installed"
# shellcheck disable=SC2086 # a list of packages
apt-get update -q && apt-get install -y -q --only-upgrade -o Dpkg::Options::=--force-confold $installed \
  || failures="$failures apt"
if [ -x /usr/local/bin/uv ]; then
  log "update: uv self update"
  /usr/local/bin/uv self update || failures="$failures uv"
fi
if [ -x "$CI_HOME/.cargo/bin/rustup" ]; then
  log "update: rustup update"
  runuser -u "$CI_USER" -- env HOME="$CI_HOME" "$CI_HOME/.cargo/bin/rustup" update stable --no-self-update \
    || failures="$failures rustup"
fi

if [ -n "$failures" ]; then
  notify "weekly update failed:$failures. See /var/log/tempo-ci-update.log."
  exit 1
fi
log "update: done"
