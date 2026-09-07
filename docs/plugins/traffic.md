# Traffic

Traffic is Tempo's fork of the **Traffic plugin from Epic's
[CitySample](https://www.unrealengine.com/marketplace/en-US/product/city-sample)** project. It
provides the MassEntity-based vehicle simulation — lane following, intersections, traffic lights,
parked vehicles — that [TempoAgents](tempo-agents.md) builds on.

It ships inside Tempo at `External/Traffic` rather than as a Tempo-authored plugin, because it is
Epic's code with Tempo's changes layered on top.

!!! info "Traffic is the simulation, TempoAgents is the API"

    Traffic runs the vehicles. TempoAgents exposes the road network and traffic-control state to
    clients over gRPC, and adds the road authoring tools. If you want to *query* traffic, you want
    [TempoAgents](tempo-agents.md).

## Where it came from

The fork point is CitySample as it shipped for **UE 5.4**. Epic has since refreshed CitySample, and
those changes are merged in — Epic's refresh was almost entirely a port to the UE 5.8 Mass API
(the `FEntityIterator` migration, `ConfigureQueries`/`InitializeInternal` signature changes, and an
include-what-you-use pass) rather than new behavior.

Tempo's own changes are the substance below.

## What Tempo adds

### Traffic controllers

Stock CitySample models intersections as traffic-light periods baked from a Houdini point cloud.
Tempo replaces that with a **traffic controller registry** — a world subsystem that traffic lights
and traffic signs register themselves with at build time.

| | |
|---|---|
| `UMassTrafficControllerRegistrySubsystem` | Registration point for controllers. `RegisterTrafficLight`, `RegisterTrafficSign`, and the corresponding getters are Blueprint-callable. |
| `EMassTrafficControllerSignType` | `YieldSign` and `StopSign`. |

This is what lets a road network built procedurally — rather than baked from a point cloud — drive
intersection behavior.

### Light- and sign-controlled intersections

The single intersection system is split in two, so signalized and sign-controlled intersections can
behave differently:

| Stock CitySample | Tempo |
|---|---|
| `MassTrafficIntersectionSimulationTrait` | `MassTrafficLightIntersectionSimulationTrait` **+** `MassTrafficSignIntersectionSimulationTrait` |
| `MassTrafficInitIntersectionsProcessor` | `MassTrafficLightInitIntersectionsProcessor` **+** `MassTrafficSignInitIntersectionsProcessor` |
| `MassTrafficUpdateIntersectionsProcessor` | `MassTrafficLightUpdateIntersectionsProcessor` **+** `MassTrafficSignUpdateIntersectionsProcessor` |
| `FMassTrafficIntersectionFragment` | `FMassTrafficLightIntersectionFragment` |

Sign-controlled intersections support **all-way and non-all-way stops**, yield signs, and vehicle
**merge** behavior — cases a fixed light cycle cannot express.

### Yielding

The largest addition. Vehicles yield in two modes:

- **Pre-emptive** — a vehicle decides before entering an intersection that it must wait.
- **Reactive** — a vehicle already committed to a maneuver yields to something it encounters.

On top of that:

- **Vehicle-to-vehicle yielding**, so a vehicle can yield to any other vehicle independently of the
  intersection's own rules.
- **Pedestrian yielding at crosswalks**, both at intersections and mid-road, via
  `UMassTrafficCrowdYieldProcessor`.
- **Deadlock detection and resolution** — `UMassTrafficYieldDeadlockFrameInitProcessor` and
  `UMassTrafficYieldDeadlockResolutionProcessor` break the cycles that mutual yielding can
  otherwise produce at a four-way stop.

Yield timing is tunable per turn direction — the `NormalizedYieldCutoffLaneDistance_*` and
`NormalizedYieldResumeLaneDistance_*` settings take separate values for left, right and straight.

### Turning and lane choice

- **Turn restrictions** per entity config: `bAllowLeftTurnsAtIntersections`,
  `bAllowRightTurnsAtIntersections`, `bAllowGoingStraightAtIntersections`. A delivery van and a bus
  can be given different maneuver sets.
- **Priority filters** — `LaneChangePriorityFilters`, `NextLanePriorityFilters` and
  `TurningLanePriorityFilters` bias lane-change candidates and next-lane selection per entity
  config.
- **Lane metadata** (`UMassTrafficLaneMetadataProcessor`) annotates lanes with the information the
  yield and turn logic needs.

### Vehicles and visuals

- **Headlights driven by environmental brightness** — a brightness meter in the level is synced into
  Mass, and vehicles switch their headlights on and off around
  `VehicleTurnOnHeadlightsBrightnessThreshold`.
- **Physics substepping** for traffic vehicles.
- **Box-based collision prediction** — `TimeToCollisionBox2D` alongside the stock sphere test, so
  long vehicles are not treated as spheres when predicting conflicts.
- Fixed wheel rotation for medium-resolution vehicle representations.
- A **parked vehicle spawner**, and a `ParkedVehicle` tag on spawned parked-vehicle actors.

### Engine version support

The fork tracks current Unreal releases. It builds against **UE 5.7 and 5.8**; support for 5.6 and
earlier has been removed. Where the two supported versions genuinely differ, the code is guarded on
`ENGINE_MINOR_VERSION >= 8` — for example `MassCore`, which 5.8 split out of the MassEntity plugin.

## What Tempo removes

The Houdini point-cloud authoring path is gone: `PopulateTrafficLightsFromPointCloud` and friends
are no longer how a Tempo level gets its traffic controls. The registry above replaces it.

The **data asset classes are still there**, though — `UMassTrafficLightTypesDataAsset`,
`UMassTrafficLightInstancesDataAsset`, `UMassTrafficParkingSpacesDataAsset` and
`UMassTrafficParkedVehicleSpawnDataGenerator` are retained so that CitySample content keeps loading.
See below.

## Drop-in replacement for CitySample's Traffic

Unreal allows only one plugin per name, and a CitySample project already ships one called
`Traffic`. Tempo's copy has to displace it — and the engine's own same-name arbitration cannot do
that on its own.

### The host's copy has to be disabled

UnrealBuildTool compiles every `*.Build.cs` under a project's plugins into a single C# assembly.
It does that in `CreateProjectRulesAssembly`, from the raw list of discovered project plugins,
*before* anything arbitrates between plugins sharing a name. Two copies of `MassTraffic.Build.cs`
are therefore two definitions of the same C# class:

```text
MassTraffic.Build.cs(5,14): error CS0101: The namespace '<global namespace>' already
    contains a definition for 'MassTraffic'
```

The build stops there, so the arbitration that would have preferred Tempo's copy never runs. It is
not a resolvable contest — one of the two plugins has to be invisible to Unreal.

`Setup.sh` makes it so, by running
[`DisableConflictingPlugins.sh`](../reference/scripts.md#dependencies-and-engine-mods): any project
plugin whose name matches one Tempo ships has its descriptor renamed, so `Traffic.uplugin` becomes
`Traffic.uplugin.disabled-by-tempo`. Renaming the descriptor is enough to hide the plugin from both
UnrealBuildTool and the runtime plugin manager, nothing else on disk is touched, and `-restore`
puts it back.

### Standing in for it

Once CitySample's copy is out of the way, Tempo's plugin has to answer to everything that referred
to the original:

| Mechanism | Detail |
|---|---|
| **Name** | The plugin is still called `Traffic`, so a `.uproject` that enables `Traffic` by name gets Tempo's, and its content still mounts at `/Traffic/`. |
| **Display name** | `FriendlyName` is `Traffic (Tempo)`, so it is recognisable in the plugin browser. It is display-only. |
| **CoreRedirects** | `Config/DefaultTraffic.ini` maps the pre-split intersection names onto their light-controlled equivalents, so `CitySampleIntersectionAgentConfig` and similar assets still resolve. |
| **Content** | The plugin carries `MF_UnpackTrafficVehicleInstanceCustomData` — byte-identical to CitySample's, and referenced by five of its vehicle materials through the `/Traffic/` path. |
| **Version** | `"Version": 2` against CitySample's `1`. This is how `FPluginManager` and UnrealBuildTool break a same-name tie, and it makes Tempo's copy the preferred one wherever that comparison is actually reached. |

!!! note "`Version` alone is not the mechanism"

    Version arbitration is real, but it runs after the rules assembly has already been compiled, so
    it cannot rescue a project that has both copies on disk. It is a correctness backstop, not a
    substitute for disabling the host's copy.

!!! warning "`Version` must stay numeric"

    It is an `int32` read with `TryGetNumberField`. A non-numeric value silently falls back to the
    default of `0` — which would *lose* to CitySample's `1`. Put readable markers in `VersionName`,
    which the engine never uses for ordering.

The same treatment is applied to [RuleProcessor](#rule-processor), the other name collision between
Tempo and CitySample.

## Rule Processor

`External/RuleProcessor` is a fork of the CitySample plugin of the same name, which provides the
point-cloud tooling (`PointCloud`, `SliceAndDice`) that Traffic's data assets are authored with. It
carries the same drop-in treatment: `Setup.sh` disables CitySample's copy, its content is
byte-identical to the original's, and the descriptor declares `"Version": 9` against CitySample's
`8` under a `Rule Processor (Tempo)` friendly name.

## Settings

`Project Settings → Plugins → Mass Traffic` exposes the traffic tuning, including the yield
distances, stop-sign rest times, crosswalk buffers and headlight thresholds referenced above.

## See also

- [TempoAgents](tempo-agents.md) — the gRPC surface over the road network and traffic controls
- [Engine Mods](engine-mods.md) — the ZoneGraph and MassCrowd changes Traffic depends on
