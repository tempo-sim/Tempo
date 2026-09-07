# Installation

## Add Tempo to a project

=== "Starting fresh (recommended)"

    Create your repository from [TempoSample](https://github.com/tempo-sim/TempoSample) as a
    [template](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-repository-from-a-template),
    then rename the project:

    ```bash
    git clone <your_repo> --recurse-submodules
    cd <your_repo>
    Scripts/Rename.sh MyProject
    ```

    !!! warning "Recurse submodules"

        Tempo is a submodule of TempoSample. If you forget `--recurse-submodules`, fix it with
        `git submodule update --init --recursive Plugins/Tempo`.

    !!! warning "Rename early"

        `Scripts/Rename.sh` is only meant for a freshly cloned TempoSample. It will not help you
        rename a project you have already started adding files to.

=== "Existing project"

    Clone Tempo into your project's `Plugins` directory:

    ```bash
    cd <your_project>/Plugins
    git submodule add https://github.com/tempo-sim/Tempo.git
    git submodule update --init --recursive
    ```

## One-time setup

Run `Setup.sh` (or `Setup.bat` on Windows) once, from the Tempo root or from `Scripts/`:

```bash
Plugins/Tempo/Setup.sh
```

This script:

- Disables any plugin already in your project that shares a name with one Tempo ships, by renaming
  its `*.uplugin`. Unreal cannot build two plugins of the same name, so this is what lets Tempo's
  `Traffic` stand in for the one a [CitySample](../plugins/traffic.md) project already has
- Modifies your project's `*.Target.cs` files to use Tempo's custom toolchain, which is necessary
  for linking certain third-party dependencies properly
- Installs the Tempo [engine mods](../guides/engine-mods.md), patching your installed Engine
  in place
- Downloads third-party dependencies
- Adds git hooks that keep engine mods and third-party dependencies up to date automatically as
  you check out different Tempo commits

!!! warning "`-skip-hooks` is for Tempo developers only"

    `Setup.sh` accepts a `-skip-hooks` flag which suppresses installing the `post-checkout` and
    `post-merge` git hooks. This is intended only for developers actively modifying Tempo
    itself — when iterating on Tempo source while not touching `EngineMods/` or third-party
    dependencies, the hooks can add noticeable overhead to every `git checkout` / `git merge`.

    Without the hooks, engine mods and third-party deps will **not** re-sync automatically when
    you change Tempo commits, and you must run `Scripts/InstallEngineMods.sh` and
    `Scripts/SyncDeps.sh` manually to keep them in sync. If you are simply using Tempo as a
    dependency in your project, do not use this flag.

## Build and run

```bash
Plugins/Tempo/Scripts/Build.sh    # or Build.bat on Windows
Plugins/Tempo/Scripts/Run.sh      # opens the project in Unreal Editor
```

TempoSample wraps both in project-level `Scripts/Build.sh` and `Scripts/Run.sh`, so from a
TempoSample-derived project you can just run `Scripts/Build.sh`.

The build does more than compile C++. It also:

- Generates C++ and Python code from every `.proto` file in the project
- Generates the `tempo_sim` Python package (plus a project package, if your project defines its
  own services) and installs both into a virtual environment at `<project_root>/TempoEnv`
- Optionally generates Rust and C++ client libraries — see
  [Client APIs](../clients/index.md)

The full set of scripts is documented in the [scripts reference](../reference/scripts.md).

## Verify it worked

Open the project in Unreal Editor and press Play. If the Tempo gRPC server started, the Editor
log contains:

```text
LogTempoCore: Display: Tempo gRPC server listening on 0.0.0.0:10001
```

From another terminal:

```bash
source ./TempoEnv/bin/activate            # TempoEnv/Scripts/activate on Windows
python -c "import tempo_sim.tempo_core as tc; print(tc.get_current_level_name())"
```

If that prints your level's name, the server is up and you are ready for
[Hello World](hello-world.md).
