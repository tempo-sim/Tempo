# Shared by the machine scripts that run as the CI user (heartbeat, daily, notify). Sourced, not run.
#
# Written for bash 3.2, which is /bin/bash on macOS: no associative arrays, no ${x,,}, and no
# "${arr[@]}" of an empty array under set -u.

TEMPO_CI_HOME=${TEMPO_CI_HOME:-$HOME/tempo-ci}
# launchd starts jobs with a bare PATH. This is the same PATH the runners get (their .path file).
export PATH="$HOME/.cargo/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"

TEMPO_CI_MACHINE_ENV=${TEMPO_CI_MACHINE_ENV:-$TEMPO_CI_HOME/machine.env}
# shellcheck source=/dev/null
[ -f "$TEMPO_CI_MACHINE_ENV" ] && . "$TEMPO_CI_MACHINE_ENV"
MACHINE_NAME=${TEMPO_CI_MACHINE_NAME:-$(hostname -s)}

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
    *)
      echo "runner_problems: $(uname -s) is not supported yet"
      ;;
  esac
}

# runners_idle: true when no runner on this machine is running a job.
runners_idle() { ! pgrep -f 'Runner.Worker' > /dev/null; }
