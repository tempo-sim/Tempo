# ROS plugins are opt-in

TempoROS and TempoROSBridge used to be enabled implicitly, the way Unreal enables any plugin it
finds in a project's `Plugins` folder. Both descriptors now set `"EnabledByDefault": false`, so
they stay off until a project's `.uproject` names them.

Nothing about the plugins themselves changed — no API, no module names, no protos. Only whether
Unreal builds them for you by default.

## Why

Tempo does not require ROS, but shipping the plugins enabled made it look as though it did. A
project that never wanted ROS still built both plugins, still downloaded the ~500 MB `rclcpp`
dependency, and on Windows still could not run its packaged game without adding the `rclcpp`
binaries to `PATH`. The only way out was to know to opt *out*.

## Who is affected

| Your project | What happens after upgrading |
|---|---|
| Never mentioned TempoROS / TempoROSBridge in its `.uproject`, and does not use ROS | Nothing to do. Faster builds, smaller packages, no `rclcpp` download. |
| Never mentioned them, but **does** use ROS | :material-alert: **Silent.** The project builds and runs, without ROS. See below. |
| Sets `"Enabled": true` for them (TempoSample does) | Nothing to do. An explicit entry still wins. |
| Sets `"Enabled": false` for them | Nothing to do, though the entries are now redundant. |

The second row is the one to watch: nothing fails to build. Your ROS topics and services simply
are not there at runtime.

## What you need to do

Only if you use ROS and never named the plugins in your `.uproject`.

Run TempoROS's setup script once. It adds the `.uproject` entry for you, installs the `rclcpp`
dependencies, and reminds you about the bridge:

```sh
Plugins/Tempo/TempoROS/Setup.sh
```

Then enable the bridge, if you use it:

```json title=".uproject"
{
    "Name": "TempoROS",
    "Enabled": true
},
{
    "Name": "TempoROSBridge",
    "Enabled": true
}
```

Enabling `TempoROSBridge` alone is enough to get `TempoROS` too — the bridge's descriptor
requires it — but naming both is clearer, and it is what the setup and packaging scripts look
for when deciding whether to do ROS work.

## Knock-on effects

- **`Setup.sh` no longer downloads `rclcpp` for everyone.** Tempo's `Setup.sh` now invokes a
  nested plugin's `Setup.sh` with `-if-enabled`, and `TempoROS/Scripts/SyncDeps.sh` skips its
  download unless TempoROS is enabled (or you pass `-force`). Running
  `Plugins/Tempo/TempoROS/Setup.sh` yourself is what opts in.
- **Packaging.** `Package.sh` already built the `TempoROSCopyHandler` only when TempoROS was
  enabled, so it now skips that work for projects that have not opted in. If you add
  `CustomStageCopyHandler=TempoROSCopyHandler` to `Config/DefaultGame.ini`, add it only alongside
  an enabled TempoROS — see [Packaging](../guides/packaging.md#packaging-with-temporos).
- **The editor's Plugins browser** still lists both, unchecked, under the Tempo category. Ticking
  the box writes the same `.uproject` entry.
