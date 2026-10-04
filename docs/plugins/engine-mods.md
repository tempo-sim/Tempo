# Engine Mods

Tempo used to modify your Unreal installation in place, and no longer does: it builds against the
engine exactly as Epic ships it. This page records **what the mods did and what replaced them**.

## What Tempo used to change

| Target | What it did | What replaced it |
|---|---|---|
| `ZoneGraph` and `MassCrowd` plugins | Procedural lane graphs; crowd positions along lanes | Tempo's own code, [below](#what-replaced-the-plugin-mods) |
| UnrealBuildTool | Three toolchains that re-exported gRPC and Protobuf from TempoCore | The `tempogrpc` shared library, [below](#build-tooling) |
| AutomationTool, `EpicGames.Perforce` | Let the engine's own C# projects build on an installed engine, for UnrealBuildTool's rebuild and TempoROS's copy handler | Nothing to build: TempoROS's copy handler compiles against the assemblies the engine ships |

An engine an earlier version of Tempo modified should be reinstalled; see
[Engine mods removal](../migration/engine-mods-removal.md). The `ZoneGraph` and `MassCrowd`
modifications alone would be harmless (Tempo builds against those plugins as Epic released them or
as its old mods left them), but the UnrealBuildTool ones are not.

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

gRPC, Protobuf and Abseil keep global state, so a process must hold exactly one copy of them for
every Tempo module to share. TempoThirdParty releases now ship that copy as a shared library,
`tempogrpc`, beside the static libraries, and every Tempo module links it (through
`TempoModuleRules`). Nothing else may link the static libraries: given both, a linker takes what it
finds in a static library from there even when a shared library listed before it exports the same
symbol, which makes a second copy. On Windows, `TempoCoreBootstrap` loads first and registers the
library's directory with the loader, which otherwise only looks beside the executable.

Packaged (monolithic) builds hold the one copy in the executable and do not need the shared
library. They link the static libraries like any others.

Neither needs anything from UnrealBuildTool beyond what Epic ships. Tempo used to link the static
libraries whole into TempoCore and re-export them from it, which took custom toolchains; a
TempoThirdParty release from before the shared library is no longer supported.

`TempoModuleRules`, the `ModuleRules` subclass that adds the include paths for generated Protobuf
code, used to be compiled into the build tool. It now lives in
`TempoCore/Source/TempoModuleRules`, where the build tool compiles it along with the module rules
that derive from it.

TempoROS's custom stage copy handler, `TempoROS.Automation.csproj`, used to reference the engine's
AutomationTool projects. Building it built them too, which an installed engine is not set up for,
and two engine mods worked around that. It now references the assemblies AutomationTool runs,
from `Engine/Binaries/DotNET/AutomationTool`, so nothing of the engine's is built.

## See also

- [Engine mods removal](../migration/engine-mods-removal.md) — what to do about an engine Tempo modified
- [Traffic](traffic.md) — the main consumer of the lane graph and crowd lane data
- [TempoAgents](tempo-agents.md) — procedural road building and the lane-graph API
