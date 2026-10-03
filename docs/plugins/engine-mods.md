# Engine Mods

Tempo patches a few of the engine's build-tool files in place, rather than shipping a custom
engine. This page describes **what those mods change and why**.

!!! note "Looking for how they are applied?"

    The mechanism — when mods run, how patches are stacked, how to author a new one — lives in the
    [Engine Mods guide](../guides/engine-mods.md). This page is about their content.

## What gets patched

On the supported engine versions (5.7 and 5.8):

| Target | Kind | Why |
|---|---|---|
| `Engine/Source/Programs/UnrealBuildTool` | 3 files added | Link arguments for gRPC and Protobuf on Windows and Linux (and packaged Mac builds). |
| `Engine/Source/Programs/AutomationTool` | 1 file added | Build configuration. |
| `Engine/Source/Programs/Shared/EpicGames.Perforce` | 1 patch | Build configuration. |

Tempo used to patch and rebuild two engine plugins as well, `ZoneGraph` and `MassCrowd`. It no
longer does, and an engine an earlier version of Tempo modified needs no repair: Tempo builds
against those plugins as Epic released them or as its old mods left them. The old modifications
are unused either way. Verifying the engine installation removes them, if you want them gone.

## What replaced the plugin mods

### ZoneGraph

ZoneGraph is Unreal's lane-graph representation. Stock, it is built from hand-placed zone shapes
with lane profiles saved in the project's settings, and it decides for itself which lanes connect
through an intersection. Tempo builds lane graphs **procedurally**, from roads and intersections
that know which connections they allow.

The code that lays lanes through an intersection is private to the ZoneGraph module and has no
extension points, so `TempoAgentsEditor` compiles its own edited copy of it. The copy is not in the
repo: it is generated when Tempo is built, from your engine's source and edits that contain only
Tempo's lines. See
[`EngineDerived/README.md`](https://github.com/tempo-sim/Tempo/tree/main/TempoAgents/Source/TempoAgentsEditor/EngineDerived)
for how that works and how to support a new engine version.

| Tempo's change | Where it lives now |
|---|---|
| Letting the intersection reject lane connections, seeing every candidate at once | `FTempoZoneGraphBuilder::ShouldFilterLaneConnection`, called from the generated tessellation |
| Turn types, connecting every lane of a turn, compatible tags, turns along circular arcs | The generated tessellation, configured in **Project Settings → Tempo → Zone Graph Build** |
| Adjacent lanes on either side of a spacer; spacers surviving a reversed lane profile | The generated tessellation |
| Lane profiles generated per level instead of saved in the project's settings | `ATempoLaneProfileStore`, an editor-only Actor saved with the level |
| `FindFirstIntersectionBetweenLanes` | `UE::MassTraffic`, in `MassTrafficUtils.h` |
| `EZoneGraphTurnType` | `EMassTrafficTurnType` |

The engine's `UZoneGraphSubsystem` owns its builder and cannot be given another, so Tempo lets it
build, then rebuilds from the same zone shapes and overwrites the result. Polygon zone shapes that
use **Bezier** routing get Tempo's arcs.

!!! warning "Rebuild lane graphs made with an earlier version of Tempo"

    Lane profiles Tempo generated used to be saved on the level's `AZoneGraphData`, in a property
    the unmodified engine does not have. Regenerate the zone shapes and rebuild the zone graph of
    each level once. Build settings Tempo had added to the project's ZoneGraph settings
    (`CompatibleTags`, `bRemoveOverlap` and so on) move to Tempo's own section, above.

### MassCrowd

MassCrowd tracks how many pedestrians are on a lane, but not *where on it* they are. Traffic needs
that to yield at crosswalks.

`UMassTrafficCrowdLaneEndsProcessor` finds, each frame, the **lead** and **tail** crowd entity on
every lane that has any: the ones farthest and least far along it, with their distance, speed and
acceleration along the lane, and their radius. `UMassTrafficSubsystem::GetCrowdLaneEnds` returns
them for a lane, or nothing when no pedestrian is on it. `MassTrafficLaneChange` uses them to decide
whether a vehicle approaching a crosswalk needs to stop.

## Build tooling

These do not change engine behavior — they make Tempo buildable against an installed engine.

gRPC, Protobuf and Abseil keep global state, so a process must hold exactly one copy of them for
every Tempo module to share. TempoThirdParty releases now ship that copy as a shared library,
`tempogrpc`, beside the static libraries, and every Tempo module links it (through
`TempoModuleRules`). Nothing else may link the static libraries: given both, a linker takes what it
finds in a static library from there even when a shared library listed before it exports the same
symbol, which makes a second copy. On Windows, `TempoCoreBootstrap` loads first and registers the
library's directory with the loader, which otherwise only looks beside the executable.

Where a TempoThirdParty release from before the shared library is installed, Tempo falls back to
what it always did: the static libraries are linked whole into TempoCore and re-exported from it,
which needs Tempo's toolchains. That is the only reason the toolchains are still installed.
Packaged (monolithic) builds hold the one copy in the executable and do not need the shared
library. They link the static libraries plainly, which needs no toolchain either (verified on Mac);
they still go through the toolchains until Linux and Windows are verified the same way.

`TempoModuleRules`, the `ModuleRules` subclass that adds the include paths for generated Protobuf
code, used to be compiled into the build tool. It now lives in
`TempoCore/Source/TempoModuleRules`, where the build tool compiles it along with the module rules
that derive from it.

| Mod | |
|---|---|
| `TempoMacToolChain.cs`, `TempoLinuxToolChain.cs`, `TempoVCToolChain.cs` | Toolchain subclasses overriding `LinkFiles` and `ModifyFinalLinkArguments`, for the link-time handling Tempo's third-party dependencies need. |
| `AutomationTool`, `EpicGames.Perforce` | Small build-configuration adjustments. |

## See also

- [Engine Mods guide](../guides/engine-mods.md) — when mods are applied, and how to author one
- [Traffic](traffic.md) — the main consumer of the lane graph and crowd lane data
- [TempoAgents](tempo-agents.md) — procedural road building and the lane-graph API
