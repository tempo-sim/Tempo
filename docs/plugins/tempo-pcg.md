# TempoPCG

TempoPCG expands on Epic's
[PCG](https://dev.epicgames.com/community/learning/tutorials/j4xJ/unreal-engine-introduction-to-procedural-generation-plugin-in-ue5-4)
plugin with custom nodes, specialized graphs, and some sample content.

Unlike the other Tempo plugins, TempoPCG does not depend on TempoCore — it adds no gRPC services.
It is content and graph tooling.

## Grass

To add procedurally generated grass to your level, add a `BP_PCGGrass` actor.

The `PCG_Grass` graph spawns grass on any landscape, except where a `WorldStatic` object covers
the landscape. It is intended for runtime generation and uses a hierarchical partition to spawn
higher-density grass near the player, with density decreasing with distance.

## Debris

Add a PCG component with a `PCG_Debris` graph to an actor to spawn procedurally generated
"debris" on it.

!!! note "Runtime generation in progress"

    With the improvements to runtime generation in UE 5.5/5.6, we are making this
    runtime-compatible too. More instructions will follow when that lands.
