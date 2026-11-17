# Tempo CI machines

Tempo's native CI runs on dedicated machines of our own (runner group `tempo-ci`), alongside the Docker
builds on GitHub's Linux runners. The workflow side is `.github/workflows/build_and_package_native.yml`,
which states the **machine contract**: what a machine must provide. This directory is the machine side:
how a machine is set up, how it watches itself, and how it stays up to date with little attention.

A machine should need a human only when it says so, in Slack.

## What runs where

| What | Where | When | Tells you |
|---|---|---|---|
| `heartbeat.sh` | CI user's LaunchAgent (Windows: scheduled task; Linux: systemd timer) | every 5 min | healthchecks.io alerts when a runner service is down, or when pings stop (machine off, hung, offline, CI user logged out) |
| `daily.sh` | CI user's LaunchAgent | 07:00 | cleans up logs and temp files; checks the machine contract (disk, engines, Xcodes, Metal toolchain, tools); reports a new macOS or Xcode update once |
| `macos/update.sh` | root LaunchDaemon | Sun 03:00 | waits for the runners to be idle, stops them, upgrades the jobs' Homebrew tools and Rust, restarts them; reports only failures |
| Check Machine step | every native job | each job | runs `TEMPO_CI_ALERT_COMMAND` (`notify.sh`) when the job finds the machine in trouble |
| `ci_machine_watchdog.yml` | GitHub-hosted runner | every 15 min | a runner GitHub sees as offline, or a job queued for the group for over an hour |

Slack alerts repeat at most every 12 hours while a problem persists. macOS and Xcode are never updated
automatically, because they change the toolchain under every build: the daily check reports each update,
and a human installs it when no job is running. Apple's security responses do install automatically.

## Files on a machine

```text
~ci/tempo-ci/machine.env        the machine contract (from macos/machine.env.example)
~ci/tempo-ci/slack-webhook      Slack incoming webhook URL (mode 600)
~ci/tempo-ci/healthchecks-url   this machine's healthchecks.io ping URL (mode 600)
~ci/tempo-ci/bin/               lib.sh, notify.sh, heartbeat.sh, daily.sh (installed by bootstrap.sh)
~ci/tempo-ci/trees/             build trees: <owner>_<repo>/<unreal version>/{build,golden}
~ci/tempo-ci/logs/, state/      the scripts' logs, and what they have already reported
~ci/actions-runner*/            one runner per Unreal version
/usr/local/libexec/tempo-ci/    update.sh, root-owned
/var/log/tempo-ci-update.log    the weekly update's log
```

Anything the CI user can read, a job can read, and jobs run PR code. That is why the native job gets no
GitHub secrets, why fork PRs wait for approval, and why the root half (`update.sh`) is installed where the
CI user cannot write. The two files above that the CI user can read can only post to the alert channel
or fake a heartbeat.

## Setting up a new Mac

1. Create a standard user `ci`, turn on automatic login for it (Users & Groups), and leave FileVault off:
   after a power failure the machine must come back to a logged-in `ci` with no one at the keyboard.
2. From an admin account: install Homebrew, the engines (Epic Games Launcher, signed in as `ci`, or
   copied to `/Users/Shared/Epic Games`), and each engine's Xcode (`xcodes install`). Then accept each
   Xcode's license, run its first launch, and download its Metal toolchain
   (`DEVELOPER_DIR=… sudo xcodebuild -license accept; … -runFirstLaunch; … -downloadComponent MetalToolchain`).
3. Clone Tempo and run `CI/Machines/macos/bootstrap.sh`. On a first run it writes
   `~ci/tempo-ci/machine.env` from the example: edit it, then run the bootstrap again.
4. Put the Slack webhook URL in `~ci/tempo-ci/slack-webhook`. Create a healthchecks.io check for the
   machine (period 15 min, grace 15 min, with the Slack integration) and put its ping URL in
   `~ci/tempo-ci/healthchecks-url`. Both mode 600, owned by `ci`.
5. For each Unreal version, register a runner:
   `CI/Machines/macos/register-runner.sh 5.8 "$(gh api -X POST orgs/tempo-sim/actions/runners/registration-token -q .token)"`.
6. Run the bootstrap once more. Its checks at the end should say `OK`.

The bootstrap is idempotent. When something here changes, pull and re-run it on each machine: it is
safe while jobs run, and does not restart a runner that is busy.

## Changing the machine

- **New Unreal version:** install the engine (and its Xcode), add its two lines to `machine.env`, and
  register a runner for it.
- **New Xcode:** install it next to the old one, point the engine's `DEVELOPER_DIR` line at it, and check
  it against the engine's range in `Engine/Config/Apple/Apple_SDK.json` (the job checks too). Changing
  the Xcode cleans that engine's build trees on their next job.
- **macOS update:** when no job is running (`pgrep -f Runner.Worker` prints nothing), install it and
  restart. The runners come back on their own after automatic login.

## Setting up a new Windows machine

The same scripts, scheduled by Task Scheduler instead of launchd, under `\TempoCI\`: `Heartbeat` (every
5 min) and `Daily` (07:00) run Git Bash as `ci` whether or not anyone is logged on (they store `ci`'s
password, as the runner services do: Windows does not let an admin create a no-password task for another
user; change the password and both must be re-registered, by re-running the bootstrap), and
`Update` (`windows/update.ps1`, Sun 03:00) runs as SYSTEM. The runners are Windows services running as
`ci`, so they start at boot without anyone logging on: no automatic login is needed.

```text
C:\Users\ci\tempo-ci\           machine.env, slack-webhook, healthchecks-url, bin\, logs\, state\, path
C:\Users\ci\actions-runner-<v>\ one runner (a Windows service) per Unreal version
C:\ci\                          TEMPO_CI_ROOT, the build trees (short: Unreal's paths get long)
C:\ci-tools\                    python3 (uv's), Rust's toolchains, and copies of winget's uv and jq
                                (its own copies are readable only by admins); admin-owned, read-only to ci
C:\Program Files\TempoCI\       update.ps1, admin-owned
C:\ProgramData\TempoCI\         update.log, update-status (read by daily.sh), bootstrap.log
```

1. Install the engines with the Epic Games Launcher (to `C:\Program Files\Epic Games`), and set the
   firmware to power on after a power failure.
2. From an elevated PowerShell in a Tempo checkout:
   `powershell -ExecutionPolicy Bypass -File CI\Machines\windows\bootstrap.ps1`. It creates `ci` (asking
   for its password), installs Visual Studio 2022 Build Tools and the jobs' tools, and writes
   `C:\Users\ci\tempo-ci\machine.env` from `windows/machine.env.example`: check it.
3. Put the Slack webhook and healthchecks.io ping URL in `C:\Users\ci\tempo-ci\slack-webhook` and
   `healthchecks-url`. The next bootstrap run restricts them to `ci`.
4. Register the runners: `powershell -ExecutionPolicy Bypass -File CI\Machines\windows\register-runner.ps1 5.7,5.8`.
   It asks for a registration token and `ci`'s password, and keeps neither.
5. Run the bootstrap once more. Its checks at the end should say `OK`.

Windows updates download on their own and are never installed automatically: the daily check reports each
one once. Install it when no job is running (`tasklist | findstr Runner.Worker` prints nothing) and restart;
the runner services come back at boot.

## Setting up a new Linux machine

Ubuntu (set up first on 26.04). The same scripts, run as `ci` by systemd timers: `tempo-ci-heartbeat.timer`
(every 5 min) and `tempo-ci-daily.timer` (07:00); `tempo-ci-update.timer` (`linux/update.sh`, Sun 03:00)
runs as root. The runners are system services (`actions.runner.tempo-sim.<name>.service`) running as `ci`,
so they start at boot without anyone logging on: no automatic login is needed.

```text
/home/ci/tempo-ci/              machine.env, slack-webhook, healthchecks-url, bin/, logs/, state/,
                                uat/<v>/ (AutomationTool's Saved folder, uebp_EngineSavedFolder in each runner's .env)
/home/ci/actions-runner-<v>/    one runner (a systemd service) per Unreal version
/opt/UnrealEngine/UE_<v>/       the engines: read-only to ci, but for the two scratch folders AutomationTool
                                insists on (Engine/Intermediate/Turnkey and Engine/Intermediate/Build/BuildCookRun)
/ci                             TEMPO_CI_ROOT, the build trees: XFS, for reflinks
/var/lib/tempo-ci/ci.xfs        the sparse image behind /ci when the disk cannot reflink (ext4), mounted by ci.mount
/usr/local/libexec/tempo-ci/    update.sh, root-owned
/var/log/tempo-ci-update.log    the weekly update's log
```

1. Unzip Epic's Linux binary build of each engine to `/opt/UnrealEngine/UE_<version>`, and set the
   firmware to power on after a power failure. Not in a home directory: `ci` must be able to list every
   directory above an engine, because Unreal resolves a path whose case is wrong by reading each directory
   from `/` down (the bootstrap grants `ci` read access to the engine itself, and says when it can't).
2. From an admin account, in a Tempo checkout: `CI/Machines/linux/bootstrap.sh`. It creates `ci`, installs
   the jobs' tools, makes `/ci` (an XFS image if the disk has no reflinks) and writes
   `/home/ci/tempo-ci/machine.env` from `linux/machine.env.example`: check it, then run the bootstrap again.
3. Put the Slack webhook and healthchecks.io ping URL in `/home/ci/tempo-ci/slack-webhook` and
   `healthchecks-url`, owned by `ci`. The next bootstrap run makes them mode 600.
4. For each Unreal version, register a runner:
   `CI/Machines/linux/register-runner.sh 5.8 "$(gh api -X POST orgs/tempo-sim/actions/runners/registration-token -q .token)"`.
5. Run the bootstrap once more. Its checks at the end should say `OK`.

Ubuntu installs security updates on its own (unattended-upgrades), and the bootstrap keeps needrestart from
restarting the runners for them. Everything else, and every reboot, the daily check reports once. Install
them when no job is running (`pgrep -f Runner.Worker` prints nothing) and reboot; the runners come back at
boot.
