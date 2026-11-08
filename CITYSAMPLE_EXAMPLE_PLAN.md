# CitySample AV Example: Plan

A turnkey example of using Epic's **CitySample** project to make synthetic autonomous-vehicle sensor data
with Tempo. Someone downloads CitySample, clones Tempo into `Plugins/`, and runs `Setup.sh`, `Build.sh`
and `Package.sh`. They then launch the packaged game and run one Python client. The client:

1. takes one **vehicle that Mass traffic drives** as the ego vehicle, chosen by type or at random;
2. mounts a **sensor rack** on it, described in JSON per vehicle type: by default one lidar, one pinhole
   camera and one fisheye camera;
3. configures those sensors through the **reflected API** (`city_sample.reflected`, generated at
   package time);
4. loads a **label table** so that camera label images, lidar returns and 2D boxes carry semantic and
   instance classes;
5. **visualizes** everything in Rerun (on by default) and/or **records** it to disk.

This is meant as a starting point to iterate from, not a finished product.

Status: **implemented and verified end to end** on a FastGeo-free package (Windows, UE 5.8): adopt,
mount, label, view and record, with the ego relabeled as its own class. The spikes ran against a
packaged build (§8, with results). Decisions the spikes changed are marked **[revised]**.

## 0. What the spikes changed

- **The ego works as planned (§2a).** View-targeting a Mass traffic car keeps it a physics actor
  while it drives, and the city streams in around it. Verified over 380 m and 4 minutes, with lights
  and a turn, 380 m from the on-foot character. The Traffic-fork helper (§2a(iii)) is not needed, and
  the spawn mode (§2b) is deferred.
- **FastGeo [revised].** CitySample 5.6+ cooks its near-field city with FastGeoStreaming. Those
  primitives aren't components, so Tempo can't label them. They also don't write custom depth, so
  labels from behind show through them. Decision: `Setup.sh` turns FastGeo off for the
  cook (`Scripts/DisableFastGeo.sh`).
- **Labels [revised].** Table validation loads every mesh it names, so a catalog-generated table
  is out. Instead: class and tag rows plus the meshes loaded at startup go in the table, and meshes
  that stream in later get `SetStaticMeshTypeSemanticId` overrides from regex rules. Semantic is the
  default label type. Instance mode can't keep the ids of a city's mesh-labeled parts apart.
- **Two Tempo bugs found and fixed:**
  - `LoadLabelTable` crashed any packaged game at its next GC: an unrooted table hung off the
    disregard-for-GC settings CDO. Now rooted, with a test.
  - `GenReflectedAPI.sh` (POSIX path inside `python -c`) and the schema scripts (platform-codepage
    `open`) failed on Windows. So the earlier package had silently shipped no `city-sample` wheel.
- **Vehicle classes.** `BP_<model>_C` is Mass's non-physics stand-in. `BP_<model>_Sandbox_C` is the
  physics car that carries the sensors.

---

## 1. What's already there

Research summary. File references are into this repo unless they start with `CitySample/`.

**CitySample**
- Default map: `/Game/Map/Big_City_LVL` (`CitySample/Config/DefaultEngine.ini:13`).
- GameMode: `BP_CitySampleGameMode` (`ACitySampleGameMode : AGameMode`), not a `TempoGameMode`.
  - The default pawn is the on-foot character `BP_CitySamplePlayerCharacter`.
  - `-DisableSandboxIntro` skips the intro fly-in.
- Vehicles derive from `ACitySampleVehicleBase : AWheeledVehiclePawn`, which implements
  `IMassTrafficVehicleControlInterface`. It uses the stock Chaos movement component.
- There are 13 models under `/Game/Vehicle/<dir>/BP_<dir>`:
  - 7 cars: `vehCar_vehicle02/03/05/06/07/12/13`
  - 2 vans: `vehVan_vehicle01/09`
  - 3 trucks: `vehTruck_vehicle04/08/11`
  - 1 bus: `vehBus_vehicle10`
- Mass traffic drives the vehicles near a viewer with real physics actors:
  - `UMassTrafficActorVehiclePhysicsProcessor` pushes PID throttle, brake and steer through
    `SetVehicleInputs` every frame.
  - Far away, the actors are released back to Mass (ISM or low-res).
  - Parked cars are separate Mass entities (`FMassTrafficParkedVehicleTag`). Mass does not drive them.
- The player enters a car through `UDrivableVehicleComponent::DriverGetIn` → `Possess`.
  - `ACitySampleVehicleBase::PossessedBy` tags the entity `FMassTrafficPlayerVehicleTag`, which means
    LOD0 forever, and treats it as an obstacle. It only does this for the CitySample PlayerController.
- `r.CustomDepth=3` is already set, which the labeler needs.
- No `Config/*.ini` has any Tempo settings.

**Tempo**
- **Sensors** need no registration.
  - A `TempoCamera` or `TempoLidar` added at runtime auto-activates.
  - The sensor service finds it by `(GetActorIdentifier(owner), component name)`.
  - Changed properties are picked up on the next tick, with one reconfigure per batch.
- **Reflected surface** (Blueprint-visible), enough for everything here:
  - Camera: `SizeXY`, `RateHz`, `FOVAngle`, and `LensParameters`, an `FTempoLensParameters` struct with
    `LensModel` ∈ Pinhole, BrownConrady, Rational, KannalaBrandt and DoubleSphere. Fisheye goes up to
    240° (tiled).
  - Lidar: `Horizontal/VerticalBeams`, `Horizontal/VerticalFOV`, `Min/MaxDistance`, `SamplingStrategy`,
    `ReturnMode`, `BeamCalibration` (`TArray<FLidarBeamCalibration{ElevationDeg, AzimuthOffsetDeg}>`)
    and the participating-media settings.
  - Measurement types are read-only, and they are selected by the stream request anyway.
- **Labels** are a `UDataTable` of `FSemanticLabel` rows.
  - Rows match on actor class (most-derived wins), actor tag, component tag, static mesh, skeletal mesh
    and Niagara system.
  - **No table is configured by default**, so out of the box nothing is labeled.
  - `LabelService.LoadLabelTable` swaps in a **JSON** table at runtime (inline, or a file relative to the
    project). There are per-entry overrides: actor type, actor tag and mesh.
  - None of this is reachable through the reflected API; it uses RPCs only.
  - `LabelType` (semantic vs instance) is global, so cameras and lidar always match.
  - Instance IDs are 1..253 and reused least-shared first.
- **Movement**: there is no possess RPC.
  - Possession is reachable through the reflected API: `AController.Possess`, or
    `Pawn.AIControllerClass` + `SpawnDefaultController`.
  - `ATempoWheeledVehicleController` drives any Chaos vehicle.
  - Lane following is available as trajectory following: `get_lanes` → spline →
    `TrajectoryFollowingComponent`.
- **Clients**:
  - `ExampleClients/Python/RerunPlayground` already streams color, depth, label, boxes, video, lidar and
    ground truth into Rerun. It has drop-oldest backpressure (`streaming.pump`), a semantic
    `AnnotationContext`, entity conventions, and a `.rrd` sink (record or view, not both).
  - Nothing in `ExampleClients/` uses the reflected API yet, so this would be the first example that does.
- **Package names**: the project's Python package will be **`city_sample`** (`gen_naming.project_package_name`
  derives it from the directory name), so the classes are `city_sample.reflected.*`.

---

## 2. Requirement 1: the ego vehicle

Plan for **two acquisition modes**, both done entirely over the API. Mass-driven is the default.

### 2a. `--ego traffic` (default): adopt a Mass traffic vehicle

Mass keeps driving: lane following, lights, yielding and interacting with the rest of traffic, all
without Tempo controlling the car. This is the closest thing to "a single Mass-controlled vehicle", and
the car behaves like part of the city, not an intruder in it.

1. Wait for the level to load and traffic to spawn. Then `world.get_actors(CitySampleVehicleBase)` near
   the viewer, filtered to traffic vehicles (no `ParkedVehicle` tag) of the requested type (§3), or
   picked at random.
2. **Keep it from being despawned.** The HighRes actor exists only while it's near a Mass LOD viewer, so
   the viewer has to travel with it. In order of preference:
   - **(i)** `PlayerController.SetViewTargetWithBlend(car)` through the reflected API. Mass LOD reads
     viewers from player view points. **[spike]**: does Mass LOD follow the view target? Does World
     Partition streaming follow the view, or the pawn?
   - **(ii)** Also move the character (hidden, collision off) along with the car, by attaching it or
     teleporting it each second. This only covers streaming.
   - **(iii)** Add `FMassTrafficPlayerVehicleTag` to keep the entity at LOD0. There is no API for that, so
     it would need a small BlueprintCallable helper in the Traffic fork or TempoAgents. Only do this if
     (i) and (ii) fall short.
3. **Watch for loss.** A pooled actor can be released or reassigned, for example if Mass despawns the
   entity at the end of a lane. The client watches `stream_actor_state(ego)`. On loss it re-adopts the
   nearest vehicle of the same type and re-mounts the rack. Sensors are cheap to re-add.
4. Hide the on-foot character and disable its input (`SetActorHiddenInGame`, `DisableInput`), or
   possess the hover drone if that is cleaner.

### 2b. `--ego spawn`: a fresh vehicle that Tempo drives

This is the fallback if 2a's spikes fail. It is also deterministic, which is useful for recording.

1. `world.spawn_actor(BP_vehCar_vehicle02_C, transform=<on a lane>)`. The spawn pose comes from
   `get_lanes`, choosing a lane near the requested start.
2. Spawn `TempoWheeledVehicleController` and call `Possess(car)` through the reflected API.
   - Release the handbrake and the parked state if needed (`SetParked(false)` **[spike]**).
3. Drive with trajectory following along a lane route stitched from `get_lanes` connections
   (`configure_trajectory_following`, DRIVE mode). This car is not in Mass, so traffic won't yield to it.
   That is acceptable for a demo.
   - **[spike]**: does `get_lanes` work on CitySample's ZoneGraph, which has no Tempo lane tags?

### Not doing: possessing a parked car

The CitySample PC's `PossessedBy` hooks would make it the player vehicle (always LOD0, which is nice).
But then input comes from the PlayerController, and nothing drives it without a human. A Tempo controller
possessing it doesn't get those hooks. If we add a LOD-pinning helper as in 2a(iii), 2b on a parked car
becomes possible too.

---

## 3. Requirement 2: the sensor rack and per-vehicle layouts (JSON)

Three kinds of JSON, kept apart so a user edits one thing at a time.

```
ExampleClients/Python/CitySampleAV/config/
  sensors/              # what a sensor IS: intrinsics, beam pattern, rate
    lidar_64ch.json
    lidar_32ch_nonuniform.json      # BeamCalibration preset
    camera_front_90.json            # pinhole
    camera_fisheye_170.json         # KannalaBrandt
  rigs/                 # which sensors, and WHERE, relative to vehicle anchors
    default.json
  vehicles.json         # vehicle catalog: class, category, which rig, anchor overrides, sampling weight
```

**Placement uses anchors derived from the vehicle's bounds, not raw coordinates.** At mount time the
client reads the ego's `local_bounds` (TempoWorld actor state). It derives the anchors `roof_center`,
`front_bumper`, `rear_bumper` and the left and right mirrors. A rig entry says, for example:
`{ "sensor": "lidar_64ch", "anchor": "roof_center", "offset_m": [0, 0, 0.25], "rpy_deg": [0, 0, 0] }`.
One `default.json` then fits a coupe and a bus. `vehicles.json` can still point a category at its own rig
(`bus` → `rigs/bus.json`), or override an anchor where bounds mislead (roof racks, mirrors).

`vehicles.json` sketch:
```json
{
  "vehicles": [
    { "id": "car02", "class": "/Game/Vehicle/vehCar_vehicle02/BP_vehCar_vehicle02", "category": "car" },
    { "id": "van01", "class": "/Game/Vehicle/vehVan_vehicle01/BP_vehVan_Vehicle01", "category": "van" },
    { "id": "bus10", "class": "/Game/Vehicle/vehBus_vehicle10/BP_vehBus_vehicle10", "category": "bus", "rig": "bus" }
  ],
  "rigs_by_category": { "default": "default" }
}
```
The CLI chooses the vehicle: `--vehicle car02 | --vehicle-category truck | --vehicle random`.

**[spike]**: are the traffic actors the plain `BP_<dir>` classes or the `_Sandbox` / `_Drivable` ones?
Matching should accept a class and its subclasses (`is_a`).

Sensor JSON uses **the example's own, SI-friendly schema** (`fov_deg`, `resolution`, `rate_hz`,
`lens: {model, k: [...], xi, alpha}`, `beams: {horizontal, vertical, vertical_fov_deg | elevations_deg:
[...]}`, `range_m`). It does not use raw Unreal property names. That keeps the JSON readable and lets the
code that applies it be typed (§4). An optional `"unreal_properties": {...}` passthrough sets arbitrary
native-unit properties through `DynamicObject`. It is the escape hatch, documented as untyped.

---

## 4. Requirement 3: configuring through the reflected API

```python
import city_sample.reflected
from tempo_sim import world
from city_sample.reflected.TempoSensors import TempoCamera, TempoLidar
from city_sample.reflected.structs import FTempoLensParameters, FLidarBeamCalibration

def apply_lidar(lidar: TempoLidar, cfg: LidarConfig) -> None:      # typed, so Pyright checks every write
    lidar.HorizontalBeams = cfg.horizontal_beams
    lidar.MaxDistance = cfg.range_m * 100.0                          # reflected API is Unreal-native: cm
    if cfg.elevations_deg:
        lidar.BeamCalibration = [FLidarBeamCalibration(ElevationDeg=e, AzimuthOffsetDeg=0.0)
                                 for e in cfg.elevations_deg]
    ...

lidar = world.add_component(TempoLidar, ego, name="RoofLidar", transform=mount_tf)  # SI transform
with world.batch():           # one RunBatch, so one reconfigure
    apply_lidar(lidar, cfg)
```

- The JSON loads into dataclasses (`LidarConfig`, `CameraConfig`, `RigEntry`, `VehicleEntry`).
  Validation errors name the file and the key.
- **Unit conversion is in one place**, in the `apply_*` functions, with the reflected API's units stated
  in their docstrings. Angles in degrees match Unreal's, so only lengths convert.
- Component names are fixed and unique (`RoofLidar`, `FrontCamera`, `FisheyeCamera`), so stream
  requests can always name the owner. The server rejects a name that's already taken
  (`world.add_component`'s docstring says otherwise; fix the docstring).
- Vehicle classes come from `city_sample.reflected.Game.Vehicle...`. The catalog in `vehicles.json`
  holds paths, and the client resolves them through `unreal.registered_classes()` with a clear error if
  the package lacks one.
- `check_schema` runs behind `--check-schema` (it takes about a minute) and is not on by default.
- `scripts/typecheck.sh` runs Pyright over the example, so this example is itself a demonstration of the
  drift guarantee.
- **[spike]**: `ETempoLidarSamplingStrategy` and `ETempoLidarReturnMode` are plain `UENUM()`. Confirm
  they reach the generated stubs.

**Dependency to note in the README.** The client depends on `city-sample`, which exists only after
`Package.sh` (run under Git Bash; `Package.bat` doesn't build wheels). The README says so up front, and
the client's import error says "run Package.sh".

---

## 5. Requirement 4: labels

**Recommendation: a JSON label table, loaded at runtime with `load_label_table`, built mostly from the
cooked asset catalog. Also runtime tags for the few things the table can't express. No binary asset, and
no change to CitySample's config.**

Why not the alternatives:
- **A binary `UDataTable` in Tempo** would reference `/Game/...` CitySample assets from a plugin that
  must build without CitySample. Its row matching is invisible in a diff, and there's no merge path.
- **Engine classes only** (Character, WheeledVehiclePawn, ...) label the dynamic actors well but miss most
  of the city. The city is static meshes, ISMs and HLODs, mostly inside a few generic actor classes.
  Engine classes are the right *first layer*, not the whole answer.
- **Tags at runtime alone** would mean visiting every actor across World Partition streaming. The labeler
  already re-labels on spawn, but a client can't keep up with streaming.

Layers, most specific first:

| Layer | Matches | Covers | Source |
|---|---|---|---|
| Actor tag | `TempoEgo` (added at runtime) | the ego car, as its own class so it can be masked | client, `set_actor_tag_semantic_id` |
| Actor class | `CitySampleVehicleBase`, `CitySampleMassVehicleBase`, crowd character BPs, `Character`, `WheeledVehiclePawn` | vehicles, pedestrians | hand-written `label_rules.json` |
| Static / skeletal mesh path | every cooked mesh, classified by path regex: `/Game/Vehicle/` → vehicle, `.../Building` → building, `Road`/`Sidewalk`/`Curb`, `Tree`/`Foliage`, `TrafficLight`, `Sign`, `StreetLight`, `Pole`, ... | static city | **generated** |
| none | label 0 | sky, unmatched | — |

- **`tools/make_label_table.py`** reads `label_rules.json`: a class list with names, colors, actor
  classes and mesh-path regexes. It enumerates meshes from `city_sample.reflected.assets`, the cooked
  `StaticMesh`/`SkeletalMesh` catalog that `filter_schema.py` writes at package time. It emits
  `label_table.json`, plus a report of the meshes nothing matched, for tuning the rules.
  - This is a second, nice use of a package-time artifact: no live scan, and it covers streamed-out
    cells too.
  - The generated table is committed in the example, so a user can just run the client. The rules and
    the generator are there for iterating.
- The client calls `load_label_table(json=...)` on connect, before mounting sensors, and logs
  `get_semantic_classes()` into Rerun's `AnnotationContext`.
- **Instance mode by default.** Bounding boxes need it, and the instance→semantic map
  (`get_instance_to_semantic_id_map`) lets the client derive semantic images and semantic lidar classes
  from instance IDs. So one mode gives both. Caveat for the README: instance IDs are 1..253 and reused,
  so they are unique per frame in practice, not globally. `--label-type semantic` is the switch for
  users who don't need boxes.
- **[spike]**:
  - Are Mass crowd agents (vertex-animated ISMs far away, actors near) and Mass traffic ISMs labeled by
    mesh? ISM components go through the mesh path. Check whether CitySample's Mass ISMs fire the labeler's
    register events. The TempoISM hooks may be Tempo-ISM-only.
  - Does `StaticMeshTypes` match by full path or by name? That decides what the generator emits.
  - Do HLOD / Nanite proxies resolve to their source mesh? Distant buildings may be HLOD meshes under
    `/Game/__ExternalActors__` or generated packages, which the catalog filters out.

---

## 6. Requirement 5: the client (visualize and/or record)

Location: `ExampleClients/Python/CitySampleAV/` (`python -m CitySampleAV` / `CitySampleAV.py`
launcher, like RerunPlayground).

```
CitySampleAV/
  __main__.py        # CLI: --vehicle/--vehicle-category, --ego traffic|spawn, --rig, --label-type,
                     #      --no-viz, --record DIR, --save-rrd PATH, --launch PATH_TO_EXE, --seed
  config/            # §3 JSON + label_rules.json + label_table.json
  rig.py             # JSON → dataclasses, anchors from bounds, apply_* via the reflected API
  ego.py             # acquisition (traffic | spawn), keep-alive, loss detection + re-adopt
  labels.py          # load table, ego tag, instance→semantic map refresh
  recorder.py        # on-disk dataset writer
  tools/make_label_table.py
  README.md
```

**Reuse RerunPlayground instead of forking it.** Pull its reusable half (`streaming.pump`,
`conventions`, `colormap`, `_compat`, `loggers/*`) into a small shared `ExampleClients/Python/tempo_rerun/`
that both clients import. RerunPlayground becomes a thin client over it, with no change in behavior.
- Changes needed in the shared code:
  - **Live and `.rrd` at once**, through `rr.set_sinks(GrpcSink, FileSink)`.
  - **Fix `_compat.connect`** for newer `rerun+http://` URLs.
  - **Don't lose a lidar revolution** to one dropped segment: emit the partial scan when a newer
    `sequence_id` arrives.
- **[decision]**: if you'd rather not touch RerunPlayground, import it via `sys.path` instead. That
  works, but it couples the new client to a playground's internals.

**Visualization** (default):
- A 3D view that follows the ego, with lidar colored by semantic class (or intensity) and ground-truth
  boxes from `stream_actor_states_near`.
- Pinhole camera color with the label overlay and 2D boxes, and the pinhole frustum shown in 3D.
- Fisheye color and labels in 2D only. Rerun's Pinhole can't represent the projection, so no 3D frustum.

**Recording** (`--record DIR`): a raw dataset, independent of Rerun, laid out for easy loading:
```
DIR/
  meta.json                    # sim + Tempo versions, vehicle, rig (resolved), label classes, CLI
  calib/<sensor>.json          # intrinsics + lens params + extrinsics to vehicle frame (SI, RH)
  ego_poses.jsonl              # sim_time, pose, velocity
  <camera>/{color,label,depth}/<seq>.png|.npy
  <camera>/bboxes/<seq>.json
  <lidar>/<seq>.npz            # xyz, intensity, distance, label, instance, ring, azimuth
  actors/<seq>.jsonl           # ground-truth actor states
```
- Writes run on a thread pool, off the event loop. The stream queues drop the oldest frames, so a slow
  disk loses frames rather than stalling the sim. Drops are logged and counted in `meta.json`.
- **Synchronized capture** (`--sync`, recording only): FixedStep time and stepping, so every sensor of a
  step is collected before the next. **[spike]**: Tempo time control relies on `ATempoWorldSettings` /
  `ATempoGameMode`. Check what still works with CitySample's own GameMode and WorldSettings. Wall-clock
  recording is the fallback, timestamped by `capture_time_s`.

**`--launch`** (optional) starts the packaged exe with `-DisableSandboxIntro` (and `-RenderOffscreen`
when `--no-viz`), and waits for the server. Without it, the client connects to a running game.

---

## 7. Turnkey path and docs

Extend `docs/guides/citysample.md` (currently WIP) into the end-to-end guide:
1. Get CitySample, clone Tempo with `--recurse-submodules`, and apply the Clang-only source patch on
   macOS and Linux.
2. `Setup.sh` → `Build.sh` → `Package.sh` (Git Bash on Windows; the reflected API needs the `.sh`).
3. `pip install Packaged/API/Python/city-sample/*.whl -r ExampleClients/Python/CitySampleAV/requirements.txt`
4. `python ExampleClients/Python/CitySampleAV.py --launch Packaged/Windows/CitySample.exe`

Troubleshooting entries to add:
- The Traffic plugin conflict.
- A stale `TEMPO_TOOLCHAIN_BLOCK` in a target file. This local checkout's
  `Source/CitySampleCookedEditor.Target.cs` still has one, so rerun `Setup.sh`.
- Missing `city_sample` (package first).
- An empty label table.

---

## 8. Spikes (first, once the package is ready)

Each is a short script against the packaged sim. The answers decide §2, §5 and §6.

1. **Reflected surface**: `city_sample.reflected` has `TempoCamera`, `TempoLidar`, their structs and enums,
   the `/Game/Vehicle` BPs, `CitySampleVehicleBase`, `PlayerController.SetViewTargetWithBlend`,
   `Controller.Possess`, and the mesh catalog.
2. **Sensors on a traffic car**: add the three sensors to a nearby traffic `CitySampleVehicleBase` and
   stream from it. Do the component names survive? Does capture run while the actor is pooled and
   reused?
3. **Keep-alive**: set the view target to that car. Does it stay HighRes for 5+ minutes of driving, and
   does World Partition stream around it while the character stays put?
4. **Which classes traffic spawns** (§3) and their `local_bounds`, for the anchors.
5. **Labels**: load a 3-row JSON table (vehicle class, one building mesh path, crowd class). Check the
   camera and lidar labels, how mesh paths match, and whether ISM / HLOD / crowd are covered.
6. **`get_lanes` on CitySample's ZoneGraph** (only needed for `--ego spawn`).
7. **Time control** under CitySample's GameMode: pause, step, and FixedStep.

### Spike results

1. **Reflected surface: yes.** Everything listed is generated, including both lidar enums and
   `SetViewTargetWithBlend`, `Possess` and `K2_GetPawn`. 7,333 classes, 26,944 cooked assets.
2. **Sensors on a traffic car: yes.** `add_component` plus batched writes, discovered and streamed
   at once (20 images in 2 s).
3. **Keep-alive: yes**, over 4 minutes and 380 m. World Partition streams around the view target.
4. **Classes:** traffic physics actors are `BP_<model>_Sandbox_C`. Car bounds run from 4.4×1.9×1.4 m
   to 5.6×2.4×2.0 m.
5. **Labels:** class rows work (pedestrians, HLOD skyline). The near-field city didn't label because
   of FastGeo (see §0). Mesh rows match full soft-object paths.
6. **`get_lanes`:** not run. It's only needed for the deferred spawn mode.
7. **Time control:** not run. The sim crashed first, on the GC bug above. Still open, for `--sync`.

## 9. Phases

Done: 1 (spikes), 2 (`tempo_rerun` extraction), 3 (ego, rig JSON, reflected `apply_*`), 4 (label
rules, table and runtime mesh sync), 5 (recorder, without `--sync`), 7 (README and guide).
Deferred: `--ego spawn`, `--sync`. Open: tune `labels.json` against the FastGeo-free build with
`--coverage`.

Original list:

1. Spikes (§8). Revise this plan.
2. `tempo_rerun` extraction plus the RerunPlayground port, with no change in behavior.
3. Ego acquisition (traffic mode), the rig JSON, and the reflected `apply_*`, viewable in Rerun.
4. The label table generator plus the committed table, instance mode, and the semantic derivation.
5. The recorder, and `--sync` if spike 7 allows it.
6. `--ego spawn`, the vehicle categories and random choice, and `--launch`.
7. The guide, the README, and the Pyright check script.

## 10. Open questions

- **"Mass-controlled"**: this plan reads it as *Mass traffic drives the ego* (§2a). If you meant
  *spawned outside Mass and driven by Tempo*, §2b becomes the default and spikes 2 and 3 matter less.
- Is it OK to extract `tempo_rerun` from RerunPlayground (§6)?
- Should the example live in Tempo (`ExampleClients/Python/CitySampleAV`), as planned? It references
  CitySample asset paths only in JSON, so Tempo still builds without CitySample. Or should it live in its
  own repo?
- Is a small C++ helper acceptable if view-target keep-alive fails (§2a(iii))? It would be the only
  engine-side change.
