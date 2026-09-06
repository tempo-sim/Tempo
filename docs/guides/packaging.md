# Packaging

Use `Scripts/Package.sh` (or `Package.bat` on Windows) to package your project into a standalone
binary. It lands in the `Packaged` folder under your project root.

```bash
Plugins/Tempo/Scripts/Package.sh
```

TempoSample wraps this in a project-level `Scripts/Package.sh`, so from a TempoSample-derived
project you can just run `Scripts/Package.sh`.

## What changes in a packaged build

### Actor names

This is the one that bites people. In the Editor, actor names match World Outliner labels; in a
packaged build, actors get unique names that are **not** the Editor labels.

Client code that hard-codes an Editor actor name works in PIE and fails after packaging. Query
for actors and use the names from the responses instead.

[:octicons-arrow-right-24: Naming](../concepts/naming.md#actors)

### Blueprint parameter defaults

Blueprint parameter defaults live in editor-only metadata, so a packaged sim cannot honor them.
This is why [`call_function`](../plugins/tempo-world.md#calling-functions) requires **every** input
parameter to be supplied — so a call behaves identically in the Editor and packaged.

### Running it

```bash
Packaged/<platform>/MyGame.sh                  # or .exe / .app
Packaged/<platform>/MyGame.sh -ServerPort=10002
```

Headless, for CI or a machine with no display:

```bash
MyGame.sh -nullrhi -unattended -ServerPort=10001
```

Note that `-nullrhi` disables rendering, so sensors will not produce data. Sensor tests need a
real GPU.

## Client packages

Packaging also stages the generated client packages, so you can ship them to your users:

```text
Packaged/API/Python/tempo-sim/        # Tempo's own services
Packaged/API/Python/<your-project>/   # your project's services, if any
Packaged/API/Rust/tempo-sim/          # if built with TEMPO_GEN_RUST_API=1
```

See [Python](../clients/python.md#publishing-your-own-package) and
[Rust](../clients/rust.md#publishing-your-own-crate) for publishing them.

## Packaging settings

`Assign Levels To Individual Chunks` puts each level in its own chunk during packaging. It
requires the project packaging settings `UsePakFile` and `GenerateChunks` to be enabled. See the
[settings reference](../reference/settings.md#tempo-core).

!!! warning "Cold CI packages need `-skipiostore`"

    A clean CI package can crash because iostore cannot find engine content under `Saved/Temp`.
    Pass `-skipiostore` in CI. Note that iostore is enabled by `bUseIoStore` in
    `Config/DefaultGame.ini`, not by an `-iostore` flag.

## Packaging with TempoROS

If you use [TempoROS](../plugins/tempo-ros.md), your `Config/DefaultGame.ini` must specify its
custom stage copy handler, so symbolic links in the `rclcpp` libraries are copied correctly on all
platforms:

```ini
CustomStageCopyHandler=TempoROSCopyHandler
```

`Package.sh` builds that handler automatically. If you package by other means, build it yourself
first by running `Scripts/BuildAutomation.sh` in TempoROS.

If you are **not** using ROS, remove `TempoROSCopyHandler` from `DefaultGame.ini` and disable the
`TempoROS` and `TempoROSBridge` plugins in your `.uproject`.

!!! note "Windows PATH"

    To run a packaged game with TempoROS on Windows, add
    `<package_root>/<YourProjectName>/Plugins/Tempo/TempoROS/Source/ThirdParty/rclcpp/Binaries/Windows`
    to your `PATH`.
