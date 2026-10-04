# ROS is now opt-in

Tempo used to bundle ROS support and enable it for you. Two things changed:

- **TempoROS is no longer a submodule of Tempo.** It is a separate repository,
  [tempo-sim/TempoROS](https://github.com/tempo-sim/TempoROS), that you add to your own project's
  `Plugins` folder — beside Tempo, not inside it. Once it is there, Unreal enables it like any other
  project plugin.
- **TempoROSBridge still ships with Tempo, but is opt-in.** Its descriptor now sets
  `"EnabledByDefault": false`, so it stays off until your `.uproject` names it.

Neither plugin's API changed — no module names, no protos, no C++ signatures. Only how you get them.

## Why

Tempo does not require ROS, but bundling it enabled made it look as though it did. A project that
never wanted ROS still cloned TempoROS, built both plugins, downloaded the ~500 MB `rclcpp`
dependency, and on Windows could not run its packaged game without adding `rclcpp` to `PATH`. The
only way out was to know to opt *out*.

Making TempoROS opt-in inside Tempo would only have moved the problem onto people who use TempoROS
*without* Tempo — for them it is an ordinary plugin that should behave like one. So Tempo stopped
vendoring it instead.

## Who is affected

| Your project | What happens after upgrading |
|---|---|
| Does not use ROS | Nothing to do. Faster builds, smaller packages, no `rclcpp` download, and no TempoROS in your checkout. |
| Uses ROS, and named the plugins in its `.uproject` | Add TempoROS to your own `Plugins` folder — Tempo no longer supplies it. The bridge's existing `"Enabled": true` entry still works. |
| Uses ROS, and never named the plugins in its `.uproject` | :material-alert: **Silent.** The project builds and runs without ROS. |

The last row is the one to watch: nothing fails to build, your ROS topics and services simply are
not there.

## What you need to do

Only if you use ROS.

**1. Add TempoROS to your project.** From your project's `Plugins` directory:

```sh
git submodule add https://github.com/tempo-sim/TempoROS.git
```

If Tempo is a submodule of your project, this is a second, independent submodule — do not put it
inside Tempo.

**2. Enable the bridge** in your `.uproject`, if you use it:

```json title=".uproject"
{
    "Name": "TempoROSBridge",
    "Enabled": true
}
```

You do not need a `TempoROS` entry; it is enabled by default once present. An existing one does no
harm.

**3. Re-run `Setup.sh`.** It finds TempoROS under `Plugins` and installs its `rclcpp` dependencies.

## Cleaning up the old submodule

Pulling this change does **not** delete `Plugins/Tempo/TempoROS`. Git refuses to remove a non-empty
submodule working tree, so the directory stays exactly where it was — with your `rclcpp` download,
your local edits and its own `.git` intact — no longer tracked by Tempo:

```text
warning: unable to rmdir 'TempoROS': Directory not empty
```

Unreal does not care that git stopped tracking it. It scans for `.uplugin` files, so the leftover is
still a live plugin. **New clones have nothing to clean up** — this section is only for checkouts
that predate the change.

### If you do not use ROS

If your `.uproject` already sets `"Enabled": false` for `TempoROS`, that still wins and nothing
builds — the leftover is just ~500 MB of wasted disk. If it does *not* say that, the leftover is
still enabled and still being built, so this cleanup is what actually stops it:

```sh
rm -rf Plugins/Tempo/TempoROS
rm -rf "$(git -C Plugins/Tempo rev-parse --git-common-dir)/modules/TempoROS"
```

The second command removes the submodule's now-orphaned git directory. Resolve it with `rev-parse`
rather than assuming a path: if Tempo is itself a submodule of your project, it lives somewhere like
`<Project>/.git/modules/Plugins/Tempo/modules/TempoROS`.

### If you do use ROS

Either is fine:

- **Leave it.** Tempo locates TempoROS anywhere under `Plugins`, so `Plugins/Tempo/TempoROS` keeps
  working indefinitely. Tempo's `.gitignore` covers the path, so it stays out of `git status` and
  survives `git clean -fd` — but `git clean -xfd` would still take it, `rclcpp` download and all.
- **Move it out**, which is tidier. Add TempoROS as your own submodule at `Plugins/TempoROS`, copy
  `Source/ThirdParty/rclcpp` over from the old location so you do not re-download ~500 MB, then
  remove the old directory and its orphaned git directory with the two commands above.

Moving it leaves the old path behind in two generated places. Neither is in your source tree, and
each is one command to clear:

- **The build steps UnrealBuildTool cached for it.** Your next `Build.sh` or `Package.sh` discards
  them for you. Building another way first fails on the old
  `TempoROS/Content/Python/gen_ros_idl.py` —
  [see Troubleshooting](../guides/troubleshooting.md#a-prebuild-step-fails-on-a-path-that-no-longer-exists).
- **The copy inside an old `Packaged` folder.** Packaging does not clear the folder first, so a
  package built before the move keeps its `Plugins/Tempo/TempoROS` beside the `Plugins/TempoROS` the
  next package stages. TempoROS finds `rclcpp` by scanning the project directory, finds both, and the
  packaged game dies on startup with `Expected to find exactly one rclcpp module`. Delete `Packaged`
  and package again, or remove just the leftover:

    ```sh
    find Packaged -type d -path '*/Plugins/Tempo/TempoROS' -prune -exec rm -rf {} +
    ```

## Compatibility

**Tempo `main` is guaranteed compatible with TempoROS `main`, and nothing else.** Tempo's CI proves
it by building TempoSample with TempoROS added as its own submodule. Pairing a release branch of one
with the other is untested — track `main` on both, or pin a pair you have verified yourself.

## Knock-on effects

- **Nothing in Tempo assumes a TempoROS path any more.** `Setup.sh` and `SyncDeps.sh` locate it
  through `Scripts/FindTempoROS.sh`, so it works wherever you put it under `Plugins`.
- **Packaging with ROS no longer needs a pre-build step.** TempoROS's stage copy handler moved to
  `TempoROS/Build/`, where AutomationTool discovers it by itself, so `Package.sh` no longer passes
  `-ScriptDir` and `TempoROS/Scripts/BuildAutomation.sh` is gone. Packaging from the editor's
  **Package Project** menu now applies the handler too, which it never did before.
- **`Setup.sh` warns** if your `.uproject` enables `TempoROSBridge` but the project has no TempoROS,
  rather than letting the build fail later with UnrealBuildTool's `Unable to find plugin 'TempoROS'`.
- **Windows packaged games no longer need a `PATH` entry.** TempoROS stages the `rclcpp` DLLs next to
  the executable — see [Packaging](../guides/packaging.md#packaging-with-temporos).
- **`CustomStageCopyHandler=TempoROSCopyHandler`** in `Config/DefaultGame.ini` is still required to
  package with ROS, and is still a line you only add if you use ROS.
