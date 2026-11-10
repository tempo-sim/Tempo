# Tempo CI machines

Tempo's native CI runs on dedicated machines of our own (runner group `tempo-ci`), alongside the Docker
builds on GitHub's Linux runners. The workflow side is `.github/workflows/build_and_package_native.yml`,
which states the **machine contract**: what a machine must provide. This directory is the machine side:
how a machine is set up, how it watches itself, and how it stays up to date with little attention.

A machine should need a human only when it says so, in Slack.

## What runs where

| What | Where | When | Tells you |
|---|---|---|---|
| `heartbeat.sh` | CI user's LaunchAgent | every 5 min | healthchecks.io alerts when a runner service is down, or when pings stop (machine off, hung, offline, CI user logged out) |
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

## Windows and Linux

Not yet: `NEW_MACHINE.md` is the guide for adding one. The workflow is OS-agnostic and the contract is the same; what is macOS-only is the service
manager (launchd here; systemd and a Windows service there), `runner_problems` in `lib.sh`, and the
`macos/` directory.
