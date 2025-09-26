# TempoPCG
TempoPCG expands on Epic's [PCG](https://dev.epicgames.com/community/learning/tutorials/j4xJ/unreal-engine-introduction-to-procedural-generation-plugin-in-ue5-4) plugin adding custom nodes, specialized graphs, and some sample content.

## Grass
To add procedurally-generated grass to your level simply add a `BP_PCGGrass` Actor. The `PCG_Grass` graph will spawn grass on any landscape, except where any `WorldStatic` objects are covering the landscape. It's intended to be used with runtime generation and uses a hierarchical partition to spawn higher-density grass near the player and decreasing density with distance.

## Debris
Add a PCG component with a `PCG_Debris` graph to an actor to spawn procedurally-generated "debris" to it. With the improvements to runtime generation in 5.5/5.6 we're going to make this runtime-compatible too. Will add more instructions when we finish that.
