# Engine Plugin Mods Removal

Tempo no longer modifies the engine's `ZoneGraph` and `MassCrowd` plugins. What those mods did now
lives in Tempo's own plugins ([what replaced them](../plugins/engine-mods.md#what-replaced-the-plugin-mods)),
so setting up Tempo no longer rebuilds two engine plugins. This affects projects that build lane
graphs with TempoAgents or run Traffic; everything else only sees a faster setup.

## What changed at a glance

| Change | Affects | Breaks |
|---|---|---|
| Generated lane profiles are saved in an `ATempoLaneProfileStore`, not on `AZoneGraphData` | Levels with a lane graph Tempo built | silently, but only for a level never opened and saved with an engine that still has the mods: its lane graph stops rebuilding correctly |
| Tempo's zone graph build settings moved out of the ZoneGraph settings | Projects that set them | Nothing: Tempo still reads them from where they were until you set them in their new place |
| `EZoneGraphTurnType` is `EMassTrafficTurnType`; `FLaneConnectionCandidate` is `FTempoLaneConnectionCandidate` | C++ that names them | compile (Blueprints and assets are redirected) |
| `FTempoLaneConnectionInfo` no longer has an `FLaneConnectionSlot` constructor | C++ that constructed one | compile |
| `UE::ZoneGraph::Query::FindFirstIntersectionBetweenLanes` is `UE::MassTraffic::FindFirstIntersectionBetweenLanes` | C++ that calls it | compile |
| `FCrowdTrackingLaneData`'s lead and tail entity fields are `UMassTrafficSubsystem::GetCrowdLaneEnds` | C++ that reads them | compile |
| `EZoneShapePolygonRoutingType::TempoBezier` is gone; **Bezier** routing gets Tempo's arcs | C++ that names it (zone shapes saved with it load as **Bezier**) | compile |
| `UZoneGraphSubsystem`'s `GetTagByName`, `GetTagName` and `GetTagNamesFromTagMask` are not callable from Blueprint in an unmodified engine | Blueprints that call them | Blueprint compile, once the engine no longer has the mods |
| `UTempoZoneGraphUtils::GenerateTagFilter` returns an `FTempoZoneGraphTagFilter` | Blueprints that use its result | Blueprint compile |
| `FMassTrafficLanePriorityFilters::LaneTagFilters` is not a Blueprint-visible property | Blueprint graphs that read or write it (assets that set it are unaffected) | Blueprint compile |

## What you need to do

### 1. Nothing, for your engine

An engine an earlier version of Tempo modified keeps working as it is. Tempo builds against
`ZoneGraph` and `MassCrowd` either as Epic released them or as Tempo's v0.3.1 mods left them, and no
longer uses the modifications. They go away when you next update the engine, or when you verify the
installation (in the Epic Games Launcher, or by re-extracting the engine on Linux). You do not have
to, and if you mean to, do steps 2 and 4 first: they are easier while the engine still has the mods.

An engine modified by a version of Tempo older than v0.3.1 may hold different versions of the
files Tempo reads. Tempo's build says so if it does, and verifying the installation fixes it.

### 2. Open and save your levels, or rebuild their lane graphs

The lane profiles Tempo generated for a level were saved in a property of `AZoneGraphData` that
only Tempo's modified ZoneGraph has. They now belong in the level's `ATempoLaneProfileStore`.

While your engine still has the mods, it still loads that property, and opening a level in the
editor moves its lane profiles into a store (the log says so). Save the level and it is done, for
an unmodified engine too. **Do this for each level before you verify or update the engine.**

A level first opened with an unmodified engine has lost them. Its baked zone graph still works at
runtime, but rebuilding it in the editor will not find its lane profiles until you regenerate its
zone shapes (the **TempoAgents** toolbar button, "Run Tempo Zone Graph Builder Pipeline", or
`TempoAgentsEditor`'s API).

### 3. Move Tempo's zone graph build settings, when convenient

`CompatibleTags`, `bRemoveOverlap`, `bRemoveSameDestination`, `bFillEmptyDestination`,
`bSingleTurningConnectionPerTurnType` and `TempoBezierTangentLengthMultiplier` now live under
**Project Settings → Tempo → Zone Graph Build**. If your project's `DefaultZoneGraph.ini` sets any of
them in `BuildSettings`, Tempo keeps using those values, and says so in the log, until the new
section has any of its own. Setting them there and removing them from `BuildSettings`, where the
unmodified engine ignores them, tidies that up.

### 4. Reconnect Blueprints that used tag filters

The engine's `FZoneGraphTagFilter` is not visible to Blueprint without Tempo's old mods.
`GenerateTagFilter` now returns Tempo's own `FTempoZoneGraphTagFilter`, with the same three tag masks,
which C++ converts to and from the engine's. A Blueprint that broke the old struct apart, or kept one
in a variable, needs the pin or variable changed to the new type. Lane priority filters set on
traffic entity configs load and edit as before; a Blueprint graph that read or wrote
`LaneTagFilters` directly uses `GetLaneTagFilters` and `SetLaneTagFilters` instead.

Tempo's mods had also made `UZoneGraphSubsystem`'s `GetTagByName`, `GetTagName` and
`GetTagNamesFromTagMask` callable from Blueprint. Those nodes keep working while your engine has the
mods. `UTempoZoneGraphUtils` has functions of the same names that work in any engine; switch to
them when convenient, and before you verify or update the engine.

### 5. Update C++ that used the engine mods

Follow the renames in the table above. `GetCrowdLaneEnds` returns null for a lane with no crowd
entities, which replaces checking each `TOptional` field.
