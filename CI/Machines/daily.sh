#!/bin/bash
# daily.sh: the machine's daily housekeeping (launchd runs it once a day). Cleans up what grows without
# bound, checks the machine contract, and reports what needs a human. Silent when nothing does: a
# problem is posted to Slack (at most once every 12h, see lib.sh), and an available macOS or Xcode
# update is posted once, when it first appears.
set -u
# shellcheck source=lib.sh
. "$(dirname "$0")/lib.sh"

# ---- Cleanup ---------------------------------------------------------------------------------------
# Only what nothing reads back. The build trees are not touched: each job resets its own, and golden is
# only ever replaced. Unreal's local DDC prunes itself.
cleanup() {
  find "$TEMPO_CI_HOME/logs" -type f -mtime +30 -delete 2>/dev/null
  find "$TEMPO_CI_HOME/logs" -mindepth 1 -type d -empty -delete 2>/dev/null
  find "$STATE_DIR/notified" -type f -mtime +7 -delete 2>/dev/null
  local runner
  for runner in "$HOME"/actions-runner*/; do
    find "$runner/_diag" -type f -mtime +14 -delete 2>/dev/null
  done
  # The packaged sim's exit-time crashes leave a report per test group per job.
  find "$HOME/Library/Logs/DiagnosticReports" -type f -mtime +14 -delete 2>/dev/null
  if runners_idle; then
    for runner in "$HOME"/actions-runner*/; do
      [ -d "$runner/_work/_temp" ] && find "$runner/_work/_temp" -mindepth 1 -maxdepth 1 -mtime +1 -exec rm -rf {} + 2>/dev/null
    done
    command -v uv > /dev/null && uv cache prune --quiet 2>/dev/null
  fi
  return 0
}

# ---- Checks ----------------------------------------------------------------------------------------
# The same contract a job's Check Machine step validates, so it breaks here, overnight, rather than in
# someone's PR.
PROBLEMS=""
problem() { PROBLEMS="$PROBLEMS
- $1"; }

check() {
  [ -f "$TEMPO_CI_MACHINE_ENV" ] || problem "machine contract $TEMPO_CI_MACHINE_ENV is missing"
  local free min cmd var engine dev ver
  if [ -n "${TEMPO_CI_ROOT:-}" ]; then
    free=$(df -Pk "$TEMPO_CI_ROOT" | awk 'NR==2 {print int($4/1048576)}')
    min=${TEMPO_CI_MIN_FREE_GB:-100}
    # Warn at 1.5x the minimum, before jobs start failing on it.
    [ "$free" -ge $(( min * 3 / 2 )) ] || problem "only ${free}GB free under $TEMPO_CI_ROOT (jobs fail below ${min}GB)"
  else
    problem "TEMPO_CI_ROOT is not set in $TEMPO_CI_MACHINE_ENV"
  fi
  for cmd in git python3 uv cargo cmake curl; do
    command -v "$cmd" > /dev/null || problem "$cmd is not on the PATH"
  done
  for var in $(compgen -v | grep '^TEMPO_CI_UE_[0-9]*_[0-9]*_ENGINE$'); do
    engine=${!var}
    ver=$(echo "$var" | sed -E 's/^TEMPO_CI_UE_([0-9]+)_([0-9]+)_ENGINE$/\1.\2/')
    [ -f "$engine/Engine/Build/Build.version" ] || problem "Unreal $ver is missing at $engine"
    if [ "$(uname -s)" = Darwin ]; then
      var="TEMPO_CI_UE_${ver/./_}_DEVELOPER_DIR"
      dev=${!var:-}
      if [ -z "$dev" ] || [ ! -d "$dev" ]; then
        problem "the Xcode for Unreal $ver is missing (${var}=${dev})"
      elif ! DEVELOPER_DIR=$dev xcodebuild -version > /dev/null 2>&1; then
        problem "xcodebuild fails for Unreal $ver's Xcode at $dev (license not accepted, or first launch not run?)"
      elif ! DEVELOPER_DIR=$dev xcrun -find metal-pack > /dev/null 2>&1; then
        problem "the Metal toolchain is missing for $dev (xcodebuild -downloadComponent MetalToolchain)"
      fi
    fi
  done
  local runners
  runners=$(runner_problems)
  [ -z "$runners" ] || problem "runner service down: $runners"
}

# ---- Updates (macOS) -------------------------------------------------------------------------------
# Nothing here installs: macOS and Xcode updates change the toolchain under every build, so a human
# picks the time. Each available update is reported once.
report_once() {   # report_once KEY TEXT
  local seen="$STATE_DIR/seen-updates"
  grep -qxF "$1" "$seen" 2>/dev/null && return 0
  notify "$2" && echo "$1" >> "$seen"
}

check_updates() {
  [ "$(uname -s)" = Darwin ] || return 0
  local label latest installed x
  softwareupdate -l 2>/dev/null | sed -n 's/^\* Label: //p' | while read -r label; do
    report_once "macos:$label" "macOS update available: $label. Install it when no job is running (Software Update, or sudo softwareupdate -i \"$label\" -R), then check the next CI run."
  done
  command -v xcodes > /dev/null || return 0
  latest=$(xcodes list 2>/dev/null | grep -E '^[0-9][0-9.]* \(' | grep -vE 'Beta|Release Candidate|RC' | tail -1 | awk '{print $1}')
  [ -n "$latest" ] || return 0
  installed=$(for x in /Applications/Xcode*.app; do defaults read "$x/Contents/Info" CFBundleShortVersionString 2>/dev/null; done)
  echo "$installed" | grep -qxF "$latest" && return 0
  report_once "xcode:$latest" "Xcode $latest is available (installed: $(echo "$installed" | paste -sd ' ' -)). $(engine_xcode_ranges)"
}

engine_xcode_ranges() {
  local var engine ver
  for var in $(compgen -v | grep '^TEMPO_CI_UE_[0-9]*_[0-9]*_ENGINE$'); do
    engine=${!var}
    ver=$(echo "$var" | sed -E 's/^TEMPO_CI_UE_([0-9]+)_([0-9]+)_ENGINE$/\1.\2/')
    python3 - "$engine/Engine/Config/Apple/Apple_SDK.json" "$ver" <<'EOF' 2>/dev/null
import json, sys
sdk = json.load(open(sys.argv[1]))
print(f"Unreal {sys.argv[2]} supports Xcode {sdk['MinVersion']}..{sdk['MaxVersion']}.", end=" ")
EOF
  done
}

log "daily: start"
cleanup
check
if [ -n "$PROBLEMS" ]; then
  log "problems:$PROBLEMS"
  notify "needs attention:$PROBLEMS"
fi
check_updates
log "daily: done ($(df -Pk "${TEMPO_CI_ROOT:-$HOME}" | awk 'NR==2 {print int($4/1048576)}')GB free)"
