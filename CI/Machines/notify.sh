#!/bin/bash
# notify.sh TEXT: post TEXT to the machine's Slack channel (see lib.sh's notify for the repeat limit).
# The native workflow runs it through TEMPO_CI_ALERT_COMMAND when a job finds the machine in trouble.
set -u
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"
[ $# -ge 1 ] || { echo "usage: $0 TEXT" >&2; exit 2; }
notify "$*"
