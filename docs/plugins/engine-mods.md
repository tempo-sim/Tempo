# Engine Mods

Tempo patches a few engine plugins and build-tool files in place, rather than shipping a custom
engine. This page describes **what those mods change and why**.

!!! note "Looking for how they are applied?"

    The mechanism — when mods run, how patches are stacked, how to author a new one — lives in the
    [Engine Mods guide](../guides/engine-mods.md). This page is about their content.

## What gets patched

On the supported engine versions (5.7 and 5.8):

| Target | Kind | Why |
|---|---|---|
| `Engine/Plugins/Runtime/ZoneGraph` | Plugin, 7 patches | Lane-graph extensibility and queries Tempo's road building and traffic rules need. |
| `Engine/Plugins/AI/MassCrowd` | Plugin, 4 patches | Per-lane crowd tracking, so vehicles can see where pedestrians are. |
| `Engine/Source/Programs/UnrealBuildTool` | 4 files added, 4 patches | Protobuf include paths, link arguments, and permission to rebuild the patched engine plugins. |
| `Engine/Source/Programs/AutomationTool` | 1 file added | Build configuration. |
| `Engine/Source/Programs/Shared/EpicGames.Perforce` | 1 patch | Build configuration. |

UE 5.6 additionally patched `Engine/Plugins/PCG`; that mod is not needed on 5.7 or later.

## ZoneGraph

ZoneGraph is Unreal's lane-graph representation. Stock, it is built from hand-placed zone shapes
and offers a fixed set of queries. Tempo needs to build lane graphs **procedurally** and to ask
questions about lanes that the stock API cannot answer.

### Extensibility for a custom builder

The builder is opened up so Tempo can drive it:

| Addition | What it enables |
|---|---|
| `UZoneGraphSubsystem::RegisterBuilder` / `ResetBuilder` | Substitute a custom builder for the subsystem's own. |
| `FZoneGraphBuilder::BuildSingleShape`, `AppendShapeToZoneStorage` | Build incrementally instead of only all-at-once. |
| `ShouldFilterLaneConnection` (virtual) | Reject specific lane connections during polygon tessellation. A second overload also receives every candidate connection, so a decision can consider the whole intersection. |
| `FZoneGraphSettings::DynamicLaneProfiles`, `ClearDynamicLaneProfiles` | Register lane profiles at runtime rather than only as saved settings. |

`TempoZoneGraphBuilder` and `TempoRoadLaneGraphSubsystem` in
[TempoAgents](tempo-agents.md) are the consumers — this is what makes procedurally generated roads
produce a valid lane graph.

### Turn types

Stock ZoneGraph knows lanes connect; it does not classify *how*. Tempo adds:

```cpp
UENUM(BlueprintType)
enum class EZoneGraphTurnType : uint8
{
    Right,
    Left,
    NoTurn,
};
```

Turn type is computed during tessellation (`GetTurnTypeBetweenSourceDest`) and stored on each lane
connection. `bSingleTurningConnectionPerTurnType` keeps an intersection from emitting several
redundant connections for the same maneuver.

This is the foundation for [Traffic](traffic.md)'s turn restrictions and per-direction yield
tuning, and for TempoAgents' intersection queries.

### Lane intersection queries

```cpp
ZONEGRAPH_API bool FindFirstIntersectionBetweenLanes(...);
bool FindIntersectionBetweenSegments(...);
```

Where two lanes cross, and how far along the first that happens. Traffic's yield logic is built
directly on this — a vehicle cannot decide whether to yield to a crossing vehicle without knowing
where the conflict point is.

### Tag-driven connection control

`FZoneGraphCompatibleTags` plus `SourceTag`/`DestTag` on connection candidates let lane connections
be filtered by tag pairs, with `bRemoveOverlap`, `bRemoveSameDestination` and
`bFillEmptyDestination` controlling how the candidate set is pruned. `FZoneGraphTagFilter` also
gains a `GetTypeHash`, so tag filters can be used as map keys.

## MassCrowd

MassCrowd tracks pedestrians on lanes but does not expose *where on a lane* the nearest ones are.
Traffic needs that to yield at crosswalks.

The mods add a `UMassCrowdUpdateTrackingLaneProcessor` that, each frame, walks the crowd entities
on every tracked lane and records the **lead** and **tail** entity — the extremes in each direction
— onto the lane's tracking data:

| Field | |
|---|---|
| `LeadEntityHandle` / `TailEntityHandle` | Which entity. |
| `LeadEntityDistanceAlongLane` / `TailEntityDistanceAlongLane` | Absolute position along the lane. |
| `LeadEntityNormalizedDistanceAlongLane` / `TailEntity…` | The same, normalized by lane length. |
| `LeadEntitySpeedAlongLane` / `TailEntitySpeedAlongLane` | Velocity projected onto the lane tangent. |
| `LeadEntityAccelerationAlongLane` / `TailEntity…` | Acceleration projected onto the lane tangent. |
| `LeadEntityRadius` / `TailEntityRadius` | Agent radius. |

Every field is a `TOptional`, so "no pedestrian on this lane" is distinct from "at distance zero".

Traffic consumes these in `MassTrafficLaneChange` and
`MassTrafficLightUpdateIntersectionsProcessor` — a vehicle approaching a crosswalk can ask how far
along the crossing the nearest pedestrian is, how fast they are moving, and therefore whether it
needs to stop.

One further patch adds runtime library paths to `MassCrowd.Build.cs`. That is a build fix, not
behavior: because the plugin is patched, rebuilt outside the engine tree, and copied back, its
libraries' rpaths would otherwise point at the build location rather than the install location.

## Build tooling

These do not change engine behavior — they make Tempo buildable against an installed engine.

| Mod | |
|---|---|
| `TempoModuleRules.cs` | A `ModuleRules` subclass that automatically adds the include paths for generated protobuf code, so every Tempo module does not have to. |
| `TempoMacToolChain.cs`, `TempoLinuxToolChain.cs`, `TempoVCToolChain.cs` | Toolchain subclasses overriding `LinkFiles` and `ModifyFinalLinkArguments`, for the link-time handling Tempo's third-party dependencies need. |
| `UEBuildTarget.cs` patches | Let UBT build `ZoneGraph`, `MassCrowd` (and on 5.6, `PCG`) and their editor modules from source in an installed engine — without this, the patched sources above would never be compiled. |
| `AutomationTool`, `EpicGames.Perforce` | Small build-configuration adjustments. |

## See also

- [Engine Mods guide](../guides/engine-mods.md) — when mods are applied, and how to author one
- [Traffic](traffic.md) — the main consumer of the ZoneGraph and MassCrowd additions
- [TempoAgents](tempo-agents.md) — procedural road building and the lane-graph API
