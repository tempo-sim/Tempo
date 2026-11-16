# Shared by the machine scripts that run as the CI user (heartbeat, daily, notify). Sourced, not run.
#
# Written for bash 3.2, which is /bin/bash on macOS: no associative arrays, no ${x,,}, and no
# "${arr[@]}" of an empty array under set -u.

TEMPO_CI_HOME=${TEMPO_CI_HOME:-$HOME/tempo-ci}
TEMPO_CI_OS=$(uname -s)
case "$TEMPO_CI_OS" in
  MINGW*|MSYS*|CYGWIN*)
    TEMPO_CI_OS=Windows
    # Task Scheduler starts the scripts with the system PATH. Use the runners' (written by
    # windows/bootstrap.ps1 to $TEMPO_CI_HOME/path, in Git Bash form), so both see the same tools.
    [ -f "$TEMPO_CI_HOME/path" ] && PATH=$(cat "$TEMPO_CI_HOME/path")
    export PATH RUSTUP_HOME=/c/ci-tools/rustup
    ;;
  Linux)
    # systemd starts the timers' services with a bare PATH. This is the same PATH the runners get (their
    # .path file, written by linux/bootstrap.sh).
    export PATH="$HOME/.cargo/bin:/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin"
    ;;
  *)
    # launchd starts jobs with a bare PATH. This is the same PATH the runners get (their .path file).
    export PATH="$HOME/.cargo/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
    ;;
esac

TEMPO_CI_MACHINE_ENV=${TEMPO_CI_MACHINE_ENV:-$TEMPO_CI_HOME/machine.env}
# shellcheck source=/dev/null
[ -f "$TEMPO_CI_MACHINE_ENV" ] && . "$TEMPO_CI_MACHINE_ENV"
# Not `hostname -s`: Windows' hostname has no -s.
MACHINE_NAME=${TEMPO_CI_MACHINE_NAME:-$(hostname | cut -d. -f1)}

STATE_DIR=$TEMPO_CI_HOME/state
mkdir -p "$STATE_DIR"

log() { echo "$(date '+%Y-%m-%d %H:%M:%S') $*"; }

# notify TEXT: post TEXT to Slack, prefixed with the machine's name. The same text is posted at most once
# every NOTIFY_REPEAT_HOURS (default 12), so a problem every job trips over is one alert, not fifty.
notify() {
  local text="$MACHINE_NAME: $1" webhook key stamp
  webhook=$(cat "$TEMPO_CI_HOME/slack-webhook" 2>/dev/null)
  if [ -z "$webhook" ]; then
    log "notify (no $TEMPO_CI_HOME/slack-webhook): $text"
    return 1
  fi
  mkdir -p "$STATE_DIR/notified"
  key=$(printf '%s' "$1" | cksum | awk '{print $1}')
  stamp="$STATE_DIR/notified/$key"
  if [ -n "$(find "$stamp" -mmin -$(( ${NOTIFY_REPEAT_HOURS:-12} * 60 )) 2>/dev/null)" ]; then
    log "notify (already sent): $text"
    return 0
  fi
  python3 -c 'import json, sys; print(json.dumps({"text": sys.argv[1]}))' "$text" \
    | curl -fsS -m 20 --retry 3 -o /dev/null -H 'Content-Type: application/json' --data-binary @- "$webhook" \
    && touch "$stamp" && log "notify: $text"
}

# runner_problems: print one line per runner service that is not running. Empty output means all are.
runner_problems() {
  case "$(uname -s)" in
    Darwin)
      local plist label state found=0
      for plist in "$HOME"/Library/LaunchAgents/actions.runner.*.plist; do
        [ -f "$plist" ] || continue
        found=1
        label=$(basename "$plist" .plist)
        state=$(launchctl print "gui/$(id -u)/$label" 2>/dev/null | awk '/^\tstate = /{print $3; exit}')
        [ "$state" = running ] || echo "$label: ${state:-not loaded}"
      done
      [ "$found" = 1 ] || echo "no runner LaunchAgents in $HOME/Library/LaunchAgents"
      ;;
    MINGW*|MSYS*|CYGWIN*)
      # Every runner directory with a .service file (config.cmd --runasservice writes the service name
      # there) must have its service running. The scheduled tasks log on as a batch user, whom Windows'
      # default service ACL does not let query a service: windows/bootstrap.ps1 grants the CI user query
      # rights on its runners' services, so "not queryable" means the bootstrap has not run since.
      local dir svc state found=0
      for dir in "$HOME"/actions-runner*/; do
        [ -f "$dir/.service" ] || continue
        found=1
        svc=$(tr -d '\r\n' < "$dir/.service")
        state=$(sc.exe query "$svc" 2>/dev/null | awk '/STATE/ {print $4; exit}')
        [ "$state" = RUNNING ] || echo "$svc: ${state:-not installed, or not queryable by $(whoami)}"
      done
      [ "$found" = 1 ] || echo "no runner services in $HOME/actions-runner*"
      ;;
    Linux)
      # One system unit per runner directory, named as linux/bootstrap.sh writes it into the directory's
      # .service (svc.sh's convention). Any user may ask systemd whether a system unit is active.
      local dir unit state found=0
      for dir in "$HOME"/actions-runner*/; do
        [ -f "$dir/.service" ] || continue
        found=1
        unit=$(cat "$dir/.service")
        state=$(systemctl is-active "$unit" 2>/dev/null)
        [ "$state" = active ] || echo "$unit: ${state:-unknown}"
      done
      [ "$found" = 1 ] || echo "no runner services in $HOME/actions-runner*"
      ;;
    *)
      echo "runner_problems: $(uname -s) is not supported yet"
      ;;
  esac
}

# runners_idle: true when no runner on this machine is running a job.
runners_idle() {
  if [ "$TEMPO_CI_OS" = Windows ]; then
    ! tasklist.exe //FI "IMAGENAME eq Runner.Worker.exe" //NH 2>/dev/null | grep -qi 'Runner.Worker'
  else
    ! pgrep -f 'Runner.Worker' > /dev/null
  fi
}
