# Scripts

Everything in `Plugins/Tempo/Scripts/`. Each `*.sh` has a `*.bat` counterpart for Windows; on
Windows you can run either the `.bat` versions or the `.sh` versions from
[Git Bash](https://gitforwindows.org/).

TempoSample-derived projects wrap the common ones at the project level, so `Scripts/Build.sh` from
your project root works.

!!! note "Scripts are BSD-portable"

    These run on macOS as well as Linux, so they avoid GNU-only behavior — no `\s` in `sed`
    (`[[:space:]]` instead), no `timeout`, quoted arrays.

## Everyday

`Setup.sh`

:   **Run once, at the start.** From the Tempo root or from `Scripts/`. Switches your
    `*.Target.cs` files to Tempo's toolchain, installs the engine mods, downloads third-party
    dependencies, and installs git hooks that keep the last two in sync as you change Tempo
    commits.

    `-skip-hooks` suppresses the hooks. **Only for developers modifying Tempo itself** — see
    [Installation](../getting-started/installation.md#one-time-setup).

    `-force` re-runs steps that would otherwise be skipped.

`Build.sh`

:   Builds the project, including the code generation prebuild that produces the protobuf code and
    the client packages.

`Run.sh`

:   Opens the project in Unreal Editor.

`Clean.sh`

:   Removes previous build artifacts.

`Package.sh`

:   Packages the project into a standalone binary under `Packaged/`, and stages the generated
    client packages under `Packaged/API/`.

    [:octicons-arrow-right-24: Packaging](../guides/packaging.md)

## Testing

`Test.sh [filter]`

:   Runs Tempo's Unreal automation tests headlessly. Exits non-zero if any test fails, if **no
    matching tests ran**, or if the editor crashed. Builds nothing — run `Build.sh` first. Reports
    land in `Saved/TempoTestReport/`.

    ```bash
    Scripts/Test.sh                 # all "Tempo." tests
    Scripts/Test.sh Tempo.Sensors   # a subset, by name prefix
    ```

`TestPythonAPI.sh [group]`

:   Runs the Python client API tests against a **packaged** build. Installs every wheel in
    `Packaged/API/Python` into a venv, then runs pytest for the requested group. Run `Package.sh`
    first.

    ```bash
    Scripts/TestPythonAPI.sh          # all groups except the GPU-only 'sensors'
    Scripts/TestPythonAPI.sh core     # contract | core | world | movement | sensors
    ```

`TestRustAPI.sh <group>`

:   The Rust analog. Requires packaging with `TEMPO_GEN_RUST_API=1`. Groups are `contract` and
    `integration`.

    [:octicons-arrow-right-24: Testing](../guides/testing.md)

## Dependencies and engine mods

`SyncDeps.sh`

:   Checks the hash of third-party libraries and downloads the correct version when a mismatch is
    found. After `Setup.sh` this runs automatically via git hooks; you only run it by hand if you
    set up with `-skip-hooks`.

`InstallEngineMods.sh`

:   Applies the mods in `EngineMods/` to your Unreal installation. Also normally automatic via git
    hooks.

`ExtractPatch.sh` / `ApplyPatch.sh`

:   Author and apply an engine mod patch.

    [:octicons-arrow-right-24: Engine mods](../guides/engine-mods.md)

`UseTempoToolchain.sh`

:   Modifies your project's `*.Target.cs` files to use Tempo's custom toolchain, which is needed to
    link certain third-party dependencies properly. Called by `Setup.sh`.

`DisableConflictingPlugins.sh`

:   Disables any plugin in your project that shares a name with one Tempo ships, by renaming its
    `*.uplugin` to `*.uplugin.disabled-by-tempo`. Unreal builds all of a project's plugin rules
    into one C# assembly before it arbitrates between same-named plugins, so two copies are a build
    error rather than a contest. Called by `Setup.sh`; pass `-restore` to undo it.

    [:octicons-arrow-right-24: Traffic as a drop-in replacement](../plugins/traffic.md#drop-in-replacement-for-citysamples-traffic)

## Discovery helpers

These print a path and exit. They're used by the other scripts, and are useful in your own
automation.

| Script | Prints |
|---|---|
| `FindUnreal.sh` | The Unreal Engine installation path, by parsing the `.uproject` and Epic's engine registry — the same way the Epic Games Launcher does. Works on all three platforms. |
| `FindProjectRoot.sh` | The project root (the directory containing the `.uproject`). |
| `FindPackagedBinary.sh <packaged_dir>` | The packaged executable for the **host** platform, so test scripts run the right binary on Linux, macOS and Windows. |
| `_FindBash.bat` | (Windows) Locates a Bash to run the `.sh` scripts with. |

## Packaging helpers

| Script | What it does |
|---|---|
| `ExtractReleasePaks.sh` | Extracts the unique level names from a chunk manifest, for release packaging. |
| `RenamePakChunks.sh` | Renames pak chunks to their level names. |

Both are relevant when `Assign Levels To Individual Chunks` is enabled — see the
[settings reference](settings.md#packaging).

## Migration

`migrate_settings_sections.py`

:   Rewrites `[/Script/<Module>.<Class>]` section headers in your config files when a settings
    class moves between modules. `CoreRedirects` do not fix config sections, because Unreal looks
    those up by literal path.

    [:octicons-arrow-right-24: Migrating to v0.1.0](../migration/v0.1.0.md)

## TempoROS

`TempoROS/Scripts/ROSEnv.sh`

:   Activates TempoROS's bundled minimal ROS environment, for CLI debugging with `ros2 topic list`
    and friends.

`TempoROS/Scripts/BuildAutomation.sh`

:   Builds the `TempoROSCopyHandler` custom stage copy handler needed to package with TempoROS.
    `Package.sh` does this for you.
