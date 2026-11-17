#!/bin/bash
# bootstrap.sh: set up (or repair) a Linux (Ubuntu) Tempo CI machine. Run it from an admin account, from
# this checkout; it uses sudo. It is idempotent, so re-running it after changing anything here is how a
# change reaches the machine.
#
#   CI/Machines/linux/bootstrap.sh            everything below
#   CI/Machines/linux/bootstrap.sh runners    only step 8 (after register-runner.sh, or a runner update)
#
# What it cannot do is listed at the end, as checks: install the engines, set the firmware to power on
# after a power failure, and supply the alerting secrets. See CI/Machines/README.md.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
MACHINES=$(dirname "$HERE")
CI_USER=${TEMPO_CI_USER:-ci}
CI_PATH_TAIL="/usr/local/bin:/usr/bin:/bin:/usr/local/sbin:/usr/sbin:/sbin"
LIBEXEC=/usr/local/libexec/tempo-ci
UNITS=/etc/systemd/system
# The loop image behind TEMPO_CI_ROOT, when the disk under it cannot reflink (step 5).
IMAGE=${TEMPO_CI_IMAGE:-/var/lib/tempo-ci/ci.xfs}
IMAGE_SIZE=${TEMPO_CI_IMAGE_SIZE:-1200G}
# The tools the jobs use (update.sh upgrades the same list), and what the runner and this script need.
JOB_PACKAGES="git curl jq cmake clang libc++-dev libc++abi-dev binutils python3 python3-venv rsync unzip gh"
MACHINE_PACKAGES="acl xfsprogs libicu-dev locales ca-certificates"

step() { printf '\n== %s\n' "$*"; }
as_ci() { sudo -u "$CI_USER" -H "$@"; }
# put_ci SRC DEST [MODE]: install a file as the CI user (whose home this account can't write).
put_ci() { as_ci tee "$2" < "$1" > /dev/null; as_ci chmod "${3:-644}" "$2"; }
# put_root SRC DEST: install a root-owned file; returns 1 when DEST already had this content.
put_root() { sudo cmp -s "$1" "$2" 2>/dev/null && return 1; sudo install -o root -g root -m 644 "$1" "$2"; }
ci_env() { as_ci bash -c '. ~/tempo-ci/machine.env 2>/dev/null; eval "echo \"\${$1:-}\""' _ "$1"; }
ci_engines() { as_ci bash -c '. ~/tempo-ci/machine.env 2>/dev/null; for v in $(compgen -v | grep "^TEMPO_CI_UE_[0-9]*_[0-9]*_ENGINE$"); do echo "${!v}"; done'; }

user() {
  step "1. The CI user ($CI_USER): no password, no sudo, the GPU's render nodes"
  if ! id "$CI_USER" > /dev/null 2>&1; then
    sudo useradd --create-home --user-group --shell /bin/bash "$CI_USER"
  fi
  # The sensors tests render (Vulkan) on the GPU's render node, which belongs to these groups.
  local g
  for g in render video; do
    getent group "$g" > /dev/null && ! id -nG "$CI_USER" | tr ' ' '\n' | grep -qx "$g" && sudo usermod -aG "$g" "$CI_USER"
  done
  return 0
}

power_and_updates() {
  step "2. Power: never sleep. Updates: security only, never restart a runner or reboot"
  sudo systemctl mask --quiet sleep.target suspend.target hibernate.target hybrid-sleep.target
  # unattended-upgrades installs security updates (Ubuntu's default, the counterpart of Apple's security
  # responses), and needrestart then restarts the services using an updated library. A runner restarted
  # mid-job loses the job, so never those. Everything else, and every reboot, daily.sh reports to a human.
  if [ -d /etc/needrestart ]; then
    local tmp; tmp=$(mktemp)
    cat > "$tmp" <<'EOF'
# Tempo CI (CI/Machines/linux/bootstrap.sh): restarting a runner kills the job it is running. A human
# reboots when daily.sh reports one is due.
$nrconf{override_rc}{qr(^actions\.runner\.)} = 0;
EOF
    sudo mkdir -p /etc/needrestart/conf.d
    put_root "$tmp" /etc/needrestart/conf.d/tempo-ci.conf || true
    rm -f "$tmp"
  fi
}

tools() {
  step "3. Tools: apt ($JOB_PACKAGES $MACHINE_PACKAGES), uv, Rust for $CI_USER"
  local missing="" p
  # Missing ones only: upgrades are update.sh's, which waits for the runners to be idle.
  for p in $JOB_PACKAGES $MACHINE_PACKAGES; do
    dpkg-query -W -f '${Status}' "$p" 2>/dev/null | grep -q 'install ok installed' || missing="$missing $p"
  done
  if [ -n "$missing" ]; then
    sudo apt-get update -q
    # shellcheck disable=SC2086
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q $missing
  fi
  # The runner's .env sets LANG=en_US.UTF-8, which needs the locale generated.
  locale -a | grep -qix 'en_US.utf8' || { sudo locale-gen en_US.UTF-8 && sudo update-locale; }
  # uv is not packaged for Ubuntu: Astral's standalone installer, into /usr/local/bin (root-owned, so a job
  # cannot replace it). update.sh keeps it current with `uv self update`.
  if [ ! -x /usr/local/bin/uv ]; then
    curl -fsSL https://astral.sh/uv/install.sh | sudo env UV_INSTALL_DIR=/usr/local/bin UV_NO_MODIFY_PATH=1 sh
  fi
  if ! as_ci test -x "/home/$CI_USER/.cargo/bin/cargo"; then
    curl -fsSL https://sh.rustup.rs | as_ci sh -s -- -y --no-modify-path --profile minimal
  fi
}

engines() {
  step "5. Engines: readable by $CI_USER, writable only where AutomationTool insists"
  local engine dir
  while IFS= read -r engine <&3; do
    [ -f "$engine/Engine/Build/Build.version" ] || { echo "  $engine: not installed yet"; continue; }
    # The CI user must be able to list every directory above it, not just pass through: Unreal looks up a
    # path whose case is wrong (the engine has a few) by reading each directory from / down
    # (UnixPlatformFile.cpp's MapFileRecursively), and fails a shader compile when it cannot. A home
    # directory is 750 on Ubuntu: put the engines somewhere shared, such as /opt/UnrealEngine.
    dir=$(dirname "$engine")
    while [ "$dir" != / ]; do
      as_ci ls "$dir" > /dev/null 2>&1 || { echo "  ! $CI_USER cannot list $dir, above $engine: move the engine somewhere shared (machine.env.example)"; break; }
      dir=$(dirname "$dir")
    done
    if [ -n "$(as_ci find "$engine" ! -readable -print -quit 2>/dev/null)" ]; then
      echo "  letting $CI_USER read $engine"
      sudo setfacl -R -m "u:$CI_USER:rX" "$engine"
    fi
    # The two scratch directories AutomationTool writes inside the engine, hard-coded, installed or not:
    # Turnkey's state, and the StagedBuild_<project>.ini every package stages
    # (CopyBuildToStagingDirectory.Automation.cs). Only these are the CI user's to write.
    for dir in "$engine/Engine/Intermediate/Turnkey" "$engine/Engine/Intermediate/Build/BuildCookRun"; do
      sudo mkdir -p "$dir"
      sudo setfacl -m "u:$CI_USER:rwx,d:u:$CI_USER:rwx" "$dir"
    done
    echo "  $engine: OK"
  done 3< <(ci_engines)
}

ci_files() {
  step "4. /home/$CI_USER/tempo-ci: scripts and machine contract"
  local home=/home/$CI_USER/tempo-ci f
  as_ci mkdir -p "$home/bin" "$home/logs" "$home/state"
  # The XDG directories a desktop login would have made. .NET's Environment.GetFolderPath returns "" for
  # one that does not exist, and UnrealBuildTool then creates its Epic/ settings directory relative to
  # its working directory - the engine, which the CI user cannot write.
  as_ci mkdir -p "/home/$CI_USER/.local/share" "/home/$CI_USER/.config" "/home/$CI_USER/.cache"
  for f in lib.sh notify.sh heartbeat.sh daily.sh; do
    put_ci "$MACHINES/$f" "$home/bin/$f" 755
  done
  if ! as_ci test -f "$home/machine.env"; then
    put_ci "$HERE/machine.env.example" "$home/machine.env"
    echo "Wrote $home/machine.env from the example: check it for this machine's engines."
  fi
  # Unreal Build Accelerator is left on (it crashed on the Mac, not here). To turn it off, or to cap a
  # build's parallelism, see macos/BuildConfiguration.xml; on Linux the file goes in
  # ~/.config/Unreal Engine/UnrealBuildTool/ of the CI user.
  as_ci chmod 600 "$home/slack-webhook" "$home/healthchecks-url" 2>/dev/null || true
}

build_disk() {
  local root; root=$(ci_env TEMPO_CI_ROOT)
  step "6. Build trees: $root on a filesystem with reflinks"
  [ -n "$root" ] || { echo "TEMPO_CI_ROOT is not set in machine.env." >&2; exit 1; }
  sudo mkdir -p "$root"
  if ! mountpoint -q "$root" && ! reflinks_in "$(dirname "$root")"; then
    # The disk cannot reflink (ext4): an XFS image on it can. Sparse, so it takes only what the trees use,
    # and mounted with discard, so space freed in the image is freed on the disk too.
    if ! sudo test -f "$IMAGE"; then
      echo "  creating $IMAGE ($IMAGE_SIZE, sparse) for $root"
      sudo mkdir -p "$(dirname "$IMAGE")"
      sudo chmod 700 "$(dirname "$IMAGE")"
      sudo truncate -s "$IMAGE_SIZE" "$IMAGE"
      sudo mkfs.xfs -q -m reflink=1 -L tempo-ci "$IMAGE"
    fi
  fi
  if sudo test -f "$IMAGE"; then
    # The CI user may look the image up (daily.sh checks the free space under it) but not read it.
    sudo chmod 711 "$(dirname "$IMAGE")"
    sudo chmod 600 "$IMAGE"
  fi
  if sudo test -f "$IMAGE"; then
    local unit tmp
    unit=$(systemd-escape --path --suffix=mount "$root")
    tmp=$(mktemp)
    cat > "$tmp" <<EOF
# Tempo CI (CI/Machines/linux/bootstrap.sh): the build trees, on XFS for reflinks.
[Unit]
Description=Tempo CI build trees ($IMAGE)

[Mount]
What=$IMAGE
Where=$root
Type=xfs
Options=loop,discard,noatime

[Install]
WantedBy=local-fs.target
EOF
    put_root "$tmp" "$UNITS/$unit" && sudo systemctl daemon-reload
    rm -f "$tmp"
    sudo systemctl enable --quiet "$unit"
    mountpoint -q "$root" || sudo systemctl start "$unit"
  fi
  sudo chown "$CI_USER:$CI_USER" "$root"
  sudo chmod 750 "$root"
}

# reflinks_in DIR: true when a file in DIR can be cloned (as the job does: cp --reflink).
reflinks_in() {
  local t; t=$(sudo mktemp -p "$1" .reflink-test.XXXXXX) || return 1
  echo x | sudo tee "$t" > /dev/null
  if sudo cp --reflink=always "$t" "$t.clone" 2>/dev/null; then sudo rm -f "$t" "$t.clone"; return 0; fi
  sudo rm -f "$t" "$t.clone"; return 1
}

runners() {
  step "8. Runners: environment, PATH and systemd service for each registered runner"
  local dir name unit tmp found=0 changed root sim_args
  root=$(ci_env TEMPO_CI_ROOT)
  tmp=$(mktemp -d)
  # Listed as the CI user: its home is 750, so a glob here would match nothing.
  for dir in $(as_ci bash -c 'for d in ~/actions-runner*; do [ -f "$d/.runner" ] && echo "$d"; done'); do
    found=1
    name=$(as_ci python3 -c 'import json, sys; print(json.load(open(sys.argv[1], encoding="utf-8-sig"))["agentName"])' "$dir/.runner")
    # svc.sh's unit name, so the runner's own tooling recognizes it.
    unit="actions.runner.$(as_ci python3 -c 'import json, sys, urllib.parse; print(urllib.parse.urlparse(json.load(open(sys.argv[1], encoding="utf-8-sig"))["gitHubUrl"]).path.strip("/").replace("/", "-"))' "$dir/.runner").$name.service"
    echo "$name ($dir): $unit"
    changed=0
    printf '%s\n' LANG=en_US.UTF-8 "TEMPO_CI_MACHINE_ENV=/home/$CI_USER/tempo-ci/machine.env" > "$tmp/env"
    # AutomationTool keeps its Saved folder (commandlet logs: the cook's) in the engine, installed or not,
    # unless told otherwise, and the engine is read-only to the CI user. One per runner, so per engine.
    case "${name##*-}" in
      [0-9]*.[0-9]*) echo "uebp_EngineSavedFolder=/home/$CI_USER/tempo-ci/uat/${name##*-}" >> "$tmp/env" ;;
    esac
    # The machine's own sim arguments (machine.env.example says why there might be some).
    sim_args=$(ci_env TEMPO_CI_SIM_EXTRA_ARGS)
    [ -z "$sim_args" ] || echo "TEMPO_SIM_EXTRA_ARGS=$sim_args" >> "$tmp/env"
    as_ci cmp -s "$dir/.env" - < "$tmp/env" || { put_ci "$tmp/env" "$dir/.env"; changed=1; }
    # config.sh writes the PATH of whoever ran it into .path. Every job gets this one instead.
    echo "/home/$CI_USER/.cargo/bin:$CI_PATH_TAIL" > "$tmp/path"
    as_ci cmp -s "$dir/.path" - < "$tmp/path" || { put_ci "$tmp/path" "$dir/.path"; changed=1; }
    # svc.sh install copies the service entry point out of bin/; this unit replaces svc.sh, so copy it here.
    as_ci cmp -s "$dir/bin/runsvc.sh" "$dir/runsvc.sh" || { as_ci install -m 755 "$dir/bin/runsvc.sh" "$dir/runsvc.sh"; changed=1; }
    echo "$unit" > "$tmp/service"
    put_ci "$tmp/service" "$dir/.service"   # svc.sh's marker; lib.sh's runner_problems reads it
    # svc.sh's unit, plus: restart on failure, wait for the build trees' filesystem, and room for the
    # files a cook opens. KillMode=process lets the runner cancel its job itself; the job's own last step
    # stops whatever is left.
    cat > "$tmp/unit" <<EOF
# Tempo CI (CI/Machines/linux/bootstrap.sh)
[Unit]
Description=GitHub Actions Runner ($name)
After=network-online.target
Wants=network-online.target
RequiresMountsFor=$root

[Service]
User=$CI_USER
WorkingDirectory=$dir
ExecStart=$dir/runsvc.sh
KillMode=process
KillSignal=SIGTERM
TimeoutStopSec=5min
Restart=always
RestartSec=15
LimitNOFILE=1048576

[Install]
WantedBy=multi-user.target
EOF
    put_root "$tmp/unit" "$UNITS/$unit" && { sudo systemctl daemon-reload; changed=1; }
    sudo systemctl enable --quiet "$unit"
    if [ "$changed" = 1 ] || ! systemctl is-active --quiet "$unit"; then
      if pgrep -u "$CI_USER" -f "$dir/bin/Runner.Worker" > /dev/null; then
        echo "  $name is running a job: its changes take effect at its next restart."
      else
        sudo systemctl restart "$unit"
      fi
    fi
  done
  rm -rf "$tmp"
  [ "$found" = 1 ] || echo "No registered runners yet: run register-runner.sh."
}

housekeeping() {
  step "7. Heartbeat (every 5 min), daily check (07:00), weekly update (Sun 03:00)"
  local tmp home=/home/$CI_USER/tempo-ci reload=0 s
  tmp=$(mktemp -d)
  # The CI user's half: run as the CI user, logging into its own directory.
  for s in heartbeat daily; do
    cat > "$tmp/$s.service" <<EOF
# Tempo CI (CI/Machines/linux/bootstrap.sh)
[Unit]
Description=Tempo CI $s ($s.sh)

[Service]
Type=oneshot
User=$CI_USER
ExecStart=/bin/bash -c 'exec /bin/bash $home/bin/$s.sh >> $home/logs/$s.log 2>&1'
EOF
  done
  cat > "$tmp/heartbeat.timer" <<'EOF'
[Unit]
Description=Tempo CI heartbeat, every 5 minutes

[Timer]
OnBootSec=2min
OnUnitActiveSec=5min
AccuracySec=30s

[Install]
WantedBy=timers.target
EOF
  cat > "$tmp/daily.timer" <<'EOF'
[Unit]
Description=Tempo CI daily check, 07:00

[Timer]
OnCalendar=*-*-* 07:00:00
Persistent=true

[Install]
WantedBy=timers.target
EOF
  # Root's half: installed root-owned, outside anything the CI user (and so a PR) can write.
  sudo install -d -o root -g root -m 755 "$LIBEXEC"
  sudo install -o root -g root -m 755 "$HERE/update.sh" "$LIBEXEC/update.sh"
  cat > "$tmp/update.service" <<EOF
# Tempo CI (CI/Machines/linux/bootstrap.sh)
[Unit]
Description=Tempo CI weekly tool update ($LIBEXEC/update.sh)
After=network-online.target
Wants=network-online.target

[Service]
Type=oneshot
Environment=TEMPO_CI_USER=$CI_USER
ExecStart=$LIBEXEC/update.sh
StandardOutput=append:/var/log/tempo-ci-update.log
StandardError=inherit
# It waits up to 6 hours for a job to finish.
TimeoutStartSec=8h
EOF
  cat > "$tmp/update.timer" <<'EOF'
[Unit]
Description=Tempo CI weekly tool update, Sunday 03:00

[Timer]
OnCalendar=Sun *-*-* 03:00:00

[Install]
WantedBy=timers.target
EOF
  for s in heartbeat.service heartbeat.timer daily.service daily.timer update.service update.timer; do
    put_root "$tmp/$s" "$UNITS/tempo-ci-$s" && reload=1
  done
  [ "$reload" = 0 ] || sudo systemctl daemon-reload
  sudo systemctl enable --quiet --now tempo-ci-heartbeat.timer tempo-ci-daily.timer tempo-ci-update.timer
  rm -rf "$tmp"
}

checks() {
  step "9. Checks (fix these by hand; README.md says how)"
  local ok=1 home=/home/$CI_USER/tempo-ci engine root
  warn() { echo "  ! $*"; ok=0; }
  as_ci test -s "$home/slack-webhook" || warn "no $home/slack-webhook: alerts go nowhere"
  as_ci test -s "$home/healthchecks-url" || warn "no $home/healthchecks-url: no heartbeat"
  while IFS= read -r engine <&3; do
    as_ci head -c1 "$engine/Engine/Build/Build.version" > /dev/null 2>&1 || warn "$CI_USER cannot read an engine at $engine (install it, then re-run this)"
  done 3< <(ci_engines)
  root=$(ci_env TEMPO_CI_ROOT)
  if [ -n "$root" ] && as_ci test -w "$root"; then
    as_ci bash -c 'cd "$1" && echo x > .reflink-test && cp --reflink=always .reflink-test .reflink-test.clone' _ "$root" 2>/dev/null \
      || warn "$root cannot reflink: every job will copy its build tree in full"
    as_ci rm -f "$root/.reflink-test" "$root/.reflink-test.clone"
  else
    warn "TEMPO_CI_ROOT ($root) is not writable by $CI_USER"
  fi
  ls /dev/dri/renderD* > /dev/null 2>&1 || warn "no GPU render node (/dev/dri/renderD*): the sensors tests cannot render"
  as_ci bash -c 'ls ~/actions-runner*/.runner' > /dev/null 2>&1 || warn "no runners registered (register-runner.sh)"
  if apt-config dump 2>/dev/null | grep -q '^Unattended-Upgrade::Automatic-Reboot "true"'; then
    warn "unattended-upgrades reboots on its own (Unattended-Upgrade::Automatic-Reboot): that kills running jobs"
  fi
  echo "  Not checkable from here: the firmware must power the machine on after a power failure."
  echo "  Running the daily check now (it posts to Slack only if something is wrong):"
  as_ci bash -c "bash $home/bin/daily.sh 2>&1" | sed 's/^/    /'
  [ "$ok" = 1 ] && echo "  OK"
  return 0
}

case "${1:-all}" in
  runners) runners ;;
  all) user; power_and_updates; tools; ci_files; engines; build_disk; housekeeping; runners; checks ;;
  *) echo "usage: $0 [all|runners]" >&2; exit 2 ;;
esac
