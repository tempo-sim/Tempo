# Adding a Linux or Windows CI machine

Instructions for an agent (or a human) turning a dedicated Linux or Windows machine into one of Tempo's
native CI runners. The first machine, a Mac mini, is set up from `macos/`. Read `README.md` here first,
then the header of `.github/workflows/build_and_package_native.yml`: its **machine contract** is the
specification you are implementing, and this file is the advice that comes with it.

Work on a branch, and open a PR against `tempo-sim/Tempo` with what you add here. Ask the human for
anything marked **(human)**. Never commit or paste a secret: the registration token, the Slack webhook
and the healthchecks.io URL stay on the machine.

## What the machine must end up as

- A dedicated, non-admin user (`ci`) that the runners and every job run as. The machine logs it in, or
  starts its services, on its own after a power failure, with nobody at the keyboard.
- **One runner per Unreal version** (5.7 and 5.8), registered with the `tempo-sim` **organization**, in
  runner group **`tempo-ci`**, each with the labels `tempo-ci` and `tempo-ue-<version>`. The runner adds
  `self-hosted`, the OS (`Linux` / `Windows`) and the architecture (`X64`) itself. Name them
  `<machine>-<version>`, e.g. `tempo-linux-ci-5.8`. The workflow adds `tempo-ue-<version>` to every job's
  labels, so each runner only ever takes its own engine's jobs: a version's build tree is at a fixed path
  and is not locked, so two jobs must never build the same version on one machine at once.
- Each runner runs as a service (systemd on Linux, a Windows service on Windows), restarts on failure,
  and starts at boot.
- The engines installed natively (no Docker, and no changes to the engine: Tempo builds against Unreal
  exactly as Epic ships it).
- The machine contract file (`machine.env`), named by `TEMPO_CI_MACHINE_ENV` in each runner's `.env`.
  `macos/machine.env.example` shows its shape; the variables are listed in the workflow header.
- On the runners' PATH: `git`, `python3`, `uv`, `cargo`, `cmake`, `curl`, and `bash`. On Linux the C++
  tests also need `clang` with `libc++` (see `AGENTS.md`, Testing).
- At least `TEMPO_CI_MIN_FREE_GB` (default 100) free under `TEMPO_CI_ROOT`, on fast local disk.
- No GitHub secrets, ever, in the native job. It runs PR code on a machine that outlives the job.

## Steps

1. **Read** `README.md`, `macos/bootstrap.sh`, `macos/register-runner.sh`, `lib.sh`, `heartbeat.sh` and
   `daily.sh`, and `build_and_package_native.yml` end to end. The macOS scripts are the reference: port
   what they do, not how.
2. **Prerequisites.** Create the `ci` user. Install the engines, the tools above, and the runner's
   dependencies. Set the machine never to sleep, and to power back on after a power failure (in its
   firmware) and resume the services.
3. **Build-tree disk.** Put `TEMPO_CI_ROOT` on a copy-on-write filesystem if at all possible. Every job
   starts by cloning a tree of tens of gigabytes, and a copy-on-write clone is instant and free; a plain
   copy is minutes of I/O and doubles the disk.
   - Linux: XFS created with `reflink=1` (the default in current mkfs.xfs) or btrfs. The workflow already
     clones with `cp -a --reflink=auto`.
   - Windows: a Dev Drive (ReFS) supports block cloning, but the workflow's `cp -a` (Git Bash) does not
     use it. Measure the clone; if it is slow, propose a Windows branch of `clone_tree` in the workflow
     (both copies of it: Prepare Build Tree and Update Golden Tree) that does.
4. **Machine contract.** Write `~ci/tempo-ci/machine.env` with `TEMPO_CI_MACHINE_NAME`, `TEMPO_CI_ROOT`,
   `TEMPO_CI_MIN_FREE_GB`, `TEMPO_CI_ALERT_COMMAND` and `TEMPO_CI_UE_<maj>_<min>_ENGINE` for each version.
   No `DEVELOPER_DIR` lines: those are Mac-only. On Windows, write paths the way Git Bash sees them
   (`/c/Program Files/Epic Games/UE_5.8`).
5. **Registration token (human).** Ask the human to run
   `gh api -X POST orgs/tempo-sim/actions/runners/registration-token -q .token` and give you the
   result. It expires after an hour.
6. **Register the runners**, one per version, each in its own directory (`~ci/actions-runner-<version>`),
   from the latest release of `actions/runner`:
   `./config.sh --unattended --url https://github.com/tempo-sim --token <token> --name <machine>-<version> --runnergroup tempo-ci --labels tempo-ci,tempo-ue-<version> --work _work`
   (`config.cmd` on Windows, with `--runasservice`, running the service as `ci`).
7. **Fix what `config.sh` leaves behind.** It writes the PATH of whoever ran it into the runner's `.path`,
   and every job then gets that PATH: rewrite `.path` with exactly the PATH the jobs need. Put
   `TEMPO_CI_MACHINE_ENV=<path to machine.env>` (plus `LANG=en_US.UTF-8` on Linux) in each runner's
   `.env`. Restart the services after changing either.
8. **Monitoring.** Port the machine scripts:
   - `lib.sh`'s `runner_problems` has a Darwin branch only; add one for your OS (`systemctl is-active`
     for the `actions.runner.*` units; `Get-Service` / `sc query` on Windows).
   - Run `heartbeat.sh` every 5 minutes and `daily.sh` daily (a systemd timer or cron; Task Scheduler
     running Git Bash on Windows), as `ci`. `daily.sh`'s Xcode and macOS-update parts are already
     Mac-only; consider an equivalent report of pending OS updates.
   - **(human)** Ask for a healthchecks.io check for this machine (15 min period, 15 min grace) and its
     ping URL, and for the Slack webhook. Write them to `~ci/tempo-ci/healthchecks-url` and
     `~ci/tempo-ci/slack-webhook`, readable only by `ci`.
   - Port `macos/update.sh` for your OS (apt/dnf, or winget): it must wait until no `Runner.Worker` is
     running, stop the runners, update the jobs' tools, and start them again. If it runs as root/admin,
     install it where `ci` cannot write, and have it read nothing `ci` can write, because jobs run PR
     code as `ci`. Don't update the OS automatically; report pending updates instead.
   - Put the new scripts in `CI/Machines/linux/` or `CI/Machines/windows/`, with a `bootstrap` that
     makes all of the above repeatable, as `macos/bootstrap.sh` does for the Mac.
9. **Wire it into CI.** In `.github/workflows/tempo_build_and_package.yml`, add the machine to the
   `build_and_test_native` matrix:
   `- { name: linux, runner: '["self-hosted","tempo-ci","Linux"]' }` (or `windows` / `Windows`).
   - **Windows has no Unix socket transport**, so the `python-core-socket` line in `test_commands` must
     not run there. Either give each machine its own test list (a `test_commands` field in its matrix
     entry) or make that line skip itself on Windows; don't drop it for the other machines.
   - `ci_machine_watchdog.yml` needs no change: it watches the whole `tempo-ci` group.
10. **Test it before merging.** `pull_request_target` runs main's workflow, so your PR's own runs won't
    use your changes. Push your branch, then dispatch the workflow on it:
    `gh workflow run tempo_build_and_package.yml --ref <branch>`. Both engines must pass Build, Run
    Automation Tests, Package, and Test Package. The first run builds from scratch (expect over an
    hour); the cached trees only fill once a build of main passes after your PR merges.

## Things the Mac taught us

- **Missing tools are machine trouble, not build failures.** The Check Machine step validates the
  contract and runs `TEMPO_CI_ALERT_COMMAND` when it fails. When you hit a new way the machine can be
  wrong, add a check there, not a workaround in the build.
- **Don't use `actions/setup-python`.** It needs a writable tool cache at a fixed path (on Linux
  `/opt/hostedtoolcache`) and, on Mac, sudo. The workflow installs the tests' Python with `uv` instead,
  which is why `uv` is part of the contract.
- **Two engines build at once,** so expect roughly double the memory and CPU of one build. The Mac
  (24 GB) coped with swap. On a machine with less, cap parallelism through the machine's own
  `BuildConfiguration.xml`, never through the repo.
- **The tests' endpoints are per engine:** ports 10057 and 10058, and on Linux sockets in
  `/tmp/tempo-ci-<version>.sock`. Nothing else on the machine may hold those ports.
- **Unreal Build Accelerator** crashed on the Mac and is disabled there in the user's
  `BuildConfiguration.xml` (`macos/BuildConfiguration.xml`). On Linux and Windows, try it first;
  disable it the same way only if it misbehaves. That file's location is per OS (`~/.config/Unreal Engine/UnrealBuildTool/` on Linux,
  `%APPDATA%\Unreal Engine\UnrealBuildTool\` on Windows).
- **Service environment matters.** On the Mac, the stock runner service's session setting made the
  packaged sim die intermittently at launch with no output. If a sim crashes at launch with an empty
  log under the service but never in an interactive login, suspect the service's environment first.
- **Windows paths:** enable long paths (`LongPathsEnabled`, and `git config --system core.longpaths true`),
  and keep `TEMPO_CI_ROOT` short (e.g. `C:\ci`): Unreal intermediate paths get long.

## When you're done

Report to the human:
- The runner names, the labels each runner shows in GitHub, and the dispatched run's link and result.
- What you added under `CI/Machines/`.
- Anything you couldn't do, and anything you changed in the workflows, with why.
- Where on the machine the secrets are. Name the files only; never put their contents in the report.
