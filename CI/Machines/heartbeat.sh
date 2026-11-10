#!/bin/bash
# heartbeat.sh: ping the machine's healthchecks.io check (launchd runs it every 5 minutes). Success when
# every runner service is running; /fail, with the reason as the body, when one is not. healthchecks.io
# alerts on a /fail, and on silence - the machine off, hung, offline, or the CI user logged out - which
# is the one failure nothing on the machine itself can report.
set -u
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"
URL=$(cat "$TEMPO_CI_HOME/healthchecks-url" 2>/dev/null) || { log "no $TEMPO_CI_HOME/healthchecks-url"; exit 1; }
problems=$(runner_problems)
if [ -z "$problems" ]; then
  curl -fsS -m 10 --retry 3 -o /dev/null "$URL"
else
  log "unhealthy: $problems"
  printf '%s\n' "$problems" | curl -fsS -m 10 --retry 3 -o /dev/null --data-binary @- "$URL/fail"
fi
