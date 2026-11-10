#!/bin/bash
# bootstrap.sh: set up (or repair) a macOS Tempo CI machine. Run it from an admin account, from this
# checkout; it uses sudo. It is idempotent, so re-running it after changing anything here is how a
# change reaches the machine.
#
#   CI/Machines/macos/bootstrap.sh            everything below
#   CI/Machines/macos/bootstrap.sh runners    only step 6 (after register-runner.sh, or a runner update)
#
# What it cannot do is listed at the end, as checks: create the CI user, install engines and Xcodes,
# and supply the alerting secrets. See CI/Machines/README.md.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
MACHINES=$(dirname "$HERE")
CI_USER=${TEMPO_CI_USER:-ci}
CI_UID=$(id -u "$CI_USER") || { echo "No user '$CI_USER'. Create it first (README.md)." >&2; exit 1; }
CI_HOME=$(dscl . -read "/Users/$CI_USER" NFSHomeDirectory | awk '{print $2}')
CI_TEMPO=$CI_HOME/tempo-ci
CI_PATH="$CI_HOME/.cargo/bin:/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
LIBEXEC=/usr/local/libexec/tempo-ci
BREW_FORMULAS="uv cmake jq gh xcodes"

step() { printf '\n== %s\n' "$*"; }
as_ci() { sudo -u "$CI_USER" -H "$@"; }
# put_ci SRC DEST [MODE]: install a file as the CI user (who can't read this checkout's directory).
put_ci() { as_ci tee "$2" < "$1" > /dev/null; as_ci chmod "${3:-644}" "$2"; }

# load_agent PLIST: (re)load a LaunchAgent into the CI user's GUI session.
load_agent() {
  local label; label=$(basename "$1" .plist)
  sudo launchctl bootout "gui/$CI_UID/$label" 2>/dev/null || true
  sudo launchctl bootstrap "gui/$CI_UID" "$1"
}

# agent_plist LABEL INTERVAL-KEYS... : a LaunchAgent that runs one of the CI user's scripts.
agent_plist() {
  local label=$1 script=$2 schedule=$3
  cat <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Label</key><string>$label</string>
	<key>ProgramArguments</key><array><string>/bin/bash</string><string>$CI_TEMPO/bin/$script</string></array>
	$schedule
	<key>StandardOutPath</key><string>$CI_TEMPO/logs/${script%.sh}.log</string>
	<key>StandardErrorPath</key><string>$CI_TEMPO/logs/${script%.sh}.log</string>
</dict>
</plist>
EOF
}

power_and_updates() {
  step "1. Power: never sleep, restart after a power failure"
  sudo pmset -a sleep 0 disksleep 0 standby 0 powernap 0 autorestart 1 womp 1

  step "2. Software Update: download, install security responses, never install macOS on its own"
  local su=/Library/Preferences/com.apple.SoftwareUpdate
  sudo defaults write $su AutomaticCheckEnabled -bool true
  sudo defaults write $su AutomaticDownload -bool true
  sudo defaults write $su CriticalUpdateInstall -bool true
  sudo defaults write $su ConfigDataInstall -bool true
  sudo defaults write $su AutomaticallyInstallMacOSUpdates -bool false
  sudo defaults write /Library/Preferences/com.apple.commerce AutoUpdate -bool false
}

tools() {
  step "3. Tools: Homebrew ($BREW_FORMULAS), Rust for $CI_USER"
  [ -x /opt/homebrew/bin/brew ] || { echo "Install Homebrew first (https://brew.sh)." >&2; exit 1; }
  local owner missing="" f
  owner=$(stat -f %Su /opt/homebrew)
  # Missing ones only: installing one that is already there upgrades it, under any job that is running.
  # Upgrades are update.sh's, which waits for the runners to be idle.
  for f in $BREW_FORMULAS; do
    sudo -u "$owner" -H /opt/homebrew/bin/brew list --formula "$f" > /dev/null 2>&1 || missing="$missing $f"
  done
  # shellcheck disable=SC2086
  [ -z "$missing" ] || sudo -u "$owner" -H /opt/homebrew/bin/brew install --quiet $missing
  if [ ! -x "$CI_HOME/.cargo/bin/cargo" ]; then
    curl -fsSL https://sh.rustup.rs | as_ci sh -s -- -y --no-modify-path --profile minimal
  fi
}

ci_files() {
  step "4. $CI_TEMPO: scripts, machine contract, Unreal build configuration"
  as_ci mkdir -p "$CI_TEMPO/bin" "$CI_TEMPO/logs" "$CI_TEMPO/state" "$CI_TEMPO/trees" "$CI_HOME/Library/LaunchAgents"
  local f
  for f in lib.sh notify.sh heartbeat.sh daily.sh; do
    put_ci "$MACHINES/$f" "$CI_TEMPO/bin/$f" 755
  done
  if ! as_ci test -f "$CI_TEMPO/machine.env"; then
    put_ci "$HERE/machine.env.example" "$CI_TEMPO/machine.env"
    echo "Wrote $CI_TEMPO/machine.env from the example: edit it for this machine's engines and Xcodes."
  fi
  # UBA's local executor segfaults in libUbaDetours on Mac (fatally with two builds at once), and with
  # no remote helpers it buys nothing. .NET's ApplicationData on macOS is ~/Library/Application Support.
  local ubt="$CI_HOME/Library/Application Support/Unreal Engine/UnrealBuildTool"
  as_ci mkdir -p "$ubt"
  put_ci "$HERE/BuildConfiguration.xml" "$ubt/BuildConfiguration.xml"
}

runners() {
  step "6. Runners: environment, PATH and LaunchAgent for each registered runner"
  local dir name label plist found=0 tmp
  tmp=$(mktemp -d)
  for dir in "$CI_HOME"/actions-runner*; do
    as_ci test -f "$dir/.runner" || continue
    found=1
    name=$(as_ci python3 -c 'import json, sys; print(json.load(open(sys.argv[1], encoding="utf-8-sig"))["agentName"])' "$dir/.runner")
    label="actions.runner.$(as_ci python3 -c 'import json, sys, urllib.parse; print(urllib.parse.urlparse(json.load(open(sys.argv[1], encoding="utf-8-sig"))["gitHubUrl"]).path.strip("/").replace("/", "."))' "$dir/.runner").$name"
    echo "$name ($dir)"
    printf '%s\n' LANG=en_US.UTF-8 HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_ANALYTICS=1 \
      "TEMPO_CI_MACHINE_ENV=$CI_TEMPO/machine.env" > "$tmp/env"
    put_ci "$tmp/env" "$dir/.env"
    # config.sh writes the PATH of whoever ran it into .path. Every job gets this one instead.
    echo "$CI_PATH" > "$tmp/path"
    put_ci "$tmp/path" "$dir/.path"
    as_ci rm -f "$dir/.service"   # svc.sh's marker; this LaunchAgent replaces its service
    as_ci mkdir -p "$CI_HOME/Library/Logs/$label"
    # Not svc.sh's plist: that one sets SessionCreate, under which the packaged sim intermittently dies
    # at launch with no output.
    plist="$CI_HOME/Library/LaunchAgents/$label.plist"
    cat > "$tmp/plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Label</key><string>$label</string>
	<key>ProgramArguments</key><array><string>$dir/runsvc.sh</string></array>
	<key>WorkingDirectory</key><string>$dir</string>
	<key>EnvironmentVariables</key><dict><key>ACTIONS_RUNNER_SVC</key><string>1</string></dict>
	<key>RunAtLoad</key><true/>
	<key>KeepAlive</key><true/>
	<key>ProcessType</key><string>Interactive</string>
	<key>StandardOutPath</key><string>$CI_HOME/Library/Logs/$label/stdout.log</string>
	<key>StandardErrorPath</key><string>$CI_HOME/Library/Logs/$label/stderr.log</string>
</dict>
</plist>
EOF
    if ! as_ci cmp -s "$plist" - < "$tmp/plist"; then
      put_ci "$tmp/plist" "$plist"
      if pgrep -u "$CI_USER" -f "$dir/bin/Runner.Worker" > /dev/null; then
        echo "  $name is running a job: its new LaunchAgent loads at the next restart."
      else
        load_agent "$plist"
      fi
    elif ! sudo launchctl print "gui/$CI_UID/$label" > /dev/null 2>&1; then
      load_agent "$plist"
    fi
  done
  rm -rf "$tmp"
  [ "$found" = 1 ] || echo "No registered runners yet: run register-runner.sh."
}

housekeeping() {
  step "5. Heartbeat (every 5 min), daily check (07:00), weekly update (Sun 03:00)"
  local tmp; tmp=$(mktemp -d)
  agent_plist com.temposim.ci.heartbeat heartbeat.sh \
    '<key>StartInterval</key><integer>300</integer><key>RunAtLoad</key><true/>' > "$tmp/heartbeat"
  agent_plist com.temposim.ci.daily daily.sh \
    '<key>StartCalendarInterval</key><dict><key>Hour</key><integer>7</integer><key>Minute</key><integer>0</integer></dict>' > "$tmp/daily"
  put_ci "$tmp/heartbeat" "$CI_HOME/Library/LaunchAgents/com.temposim.ci.heartbeat.plist"
  put_ci "$tmp/daily" "$CI_HOME/Library/LaunchAgents/com.temposim.ci.daily.plist"
  load_agent "$CI_HOME/Library/LaunchAgents/com.temposim.ci.heartbeat.plist"
  load_agent "$CI_HOME/Library/LaunchAgents/com.temposim.ci.daily.plist"

  # Root's half: installed root-owned, outside anything the CI user (and so a PR) can write.
  sudo install -d -o root -g wheel -m 755 "$LIBEXEC"
  sudo install -o root -g wheel -m 755 "$HERE/update.sh" "$LIBEXEC/update.sh"
  cat > "$tmp/update" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
	<key>Label</key><string>com.temposim.ci.update</string>
	<key>ProgramArguments</key><array><string>/bin/bash</string><string>$LIBEXEC/update.sh</string></array>
	<key>EnvironmentVariables</key><dict><key>TEMPO_CI_USER</key><string>$CI_USER</string></dict>
	<key>StartCalendarInterval</key><dict><key>Weekday</key><integer>0</integer><key>Hour</key><integer>3</integer><key>Minute</key><integer>0</integer></dict>
	<key>StandardOutPath</key><string>/var/log/tempo-ci-update.log</string>
	<key>StandardErrorPath</key><string>/var/log/tempo-ci-update.log</string>
</dict>
</plist>
EOF
  sudo install -o root -g wheel -m 644 "$tmp/update" /Library/LaunchDaemons/com.temposim.ci.update.plist
  sudo launchctl bootout system/com.temposim.ci.update 2>/dev/null || true
  sudo launchctl bootstrap system /Library/LaunchDaemons/com.temposim.ci.update.plist
  rm -rf "$tmp"
}

checks() {
  step "7. Checks (fix these by hand; README.md says how)"
  local ok=1 x
  warn() { echo "  ! $*"; ok=0; }
  [ "$(defaults read /Library/Preferences/com.apple.loginwindow autoLoginUser 2>/dev/null)" = "$CI_USER" ] \
    || warn "automatic login is not $CI_USER: after a restart nothing would run"
  fdesetup status | grep -q "Off" || warn "FileVault is on: a restart stops at the unlock screen"
  as_ci test -s "$CI_TEMPO/slack-webhook" || warn "no $CI_TEMPO/slack-webhook: alerts go nowhere"
  as_ci test -s "$CI_TEMPO/healthchecks-url" || warn "no $CI_TEMPO/healthchecks-url: no heartbeat"
  for x in /Applications/Xcode*.app; do
    [ -d "$x" ] || continue
    DEVELOPER_DIR="$x/Contents/Developer" xcrun -find metal-pack > /dev/null 2>&1 \
      || warn "no Metal toolchain for $x (DEVELOPER_DIR=$x/Contents/Developer xcodebuild -downloadComponent MetalToolchain)"
  done
  echo "  Running the daily check now (it posts to Slack only if something is wrong):"
  as_ci bash "$CI_TEMPO/bin/daily.sh" | sed 's/^/    /'
  [ "$ok" = 1 ] && echo "  OK"
}

case "${1:-all}" in
  runners) runners ;;
  all) power_and_updates; tools; ci_files; housekeeping; runners; checks ;;
  *) echo "usage: $0 [all|runners]" >&2; exit 2 ;;
esac
