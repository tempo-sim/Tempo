# Getting Started

Getting from nothing to a simulation you drive from Python takes four steps. Budget an hour,
most of which is Unreal building.

<div class="grid cards" markdown>

-   :material-numeric-1-circle:{ .lg .middle } **[Prerequisites](prerequisites.md)**

    ---

    Check your platform and Unreal version, install `jq` and friends, set
    `UNREAL_ENGINE_PATH` if you're on Linux.

-   :material-numeric-2-circle:{ .lg .middle } **[Installation](installation.md)**

    ---

    Start from TempoSample or add Tempo to an existing project, run `Setup.sh` once, then build
    and run.

-   :material-numeric-3-circle:{ .lg .middle } **[Hello World](hello-world.md)**

    ---

    Play the level, spawn a sensor rig, stream images, change a camera property, and step time —
    all from a Python REPL.

-   :material-numeric-4-circle:{ .lg .middle } **[Next steps](next-steps.md)**

    ---

    Where to go once it runs: the plugin guides, the example clients, packaging, and CI.

</div>

## Follow along on video

The whole path below is narrated end to end in the
[getting started video](https://github.com/user-attachments/assets/849cde96-a5a0-46e7-ab18-4fcbbc9fea8d).
Sound on!

## Start from TempoSample

[TempoSample](https://github.com/tempo-sim/TempoSample) is a working Unreal project with Tempo
already wired up — recommended project settings, a game mode and HUD, a drivable environment, and
a street sweeper and "block bot" pawns to possess. Nearly every example in this documentation is
something you can run in TempoSample without writing an asset yourself.

If you are starting a new project, create your repo from TempoSample as a
[template](https://docs.github.com/en/repositories/creating-and-managing-repositories/creating-a-repository-from-a-template)
and rename it with `Scripts/Rename.sh`. If you already have a project, follow
[Installation](installation.md) to add Tempo to it.

!!! tip "Tempo ships plugins, not content"

    Tempo itself carries almost no 3D content — a sensor rig blueprint, some materials, a grass
    PCG graph. The environments, vehicles and characters you see in the screenshots throughout
    this site come from TempoSample, which bundles Creative Commons content for demonstration.
    Most projects start by replacing it with their own.
