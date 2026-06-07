# AGENTS.md — Tempo

Orientation for AI agents (and humans) working in this repository. Read this before making changes.

> Tempo is a collection of **Unreal Engine 5.6 / 5.7** plugins that turn Unreal into a
> programmable simulator for robotics and autonomy. It is not an application — it is a set
> of plugins that live in a host project's `Plugins/` directory (the reference host is
> [TempoSample](https://github.com/tempo-sim/TempoSample)). The current working directory
> is `.../<Project>/Plugins/Tempo`.

---

## 1. What Tempo is

Tempo exposes Unreal to external clients (Python / Rust / C++ / ROS 2) so they can drive a
simulation deterministically: control time, spawn and configure actors, command vehicles,
and stream synthetic sensor data. The design goal is to make Unreal's rendering and world
simulation accessible behind a clean, code-generated, language-agnostic API.

The two pillars to understand first:

1. **A gRPC server inside the engine.** `TempoCore` hosts one `FTempoServer` (default port
   `10001`). Every plugin registers RPC services on it. Clients talk to the sim over gRPC.
2. **A code-generation pipeline.** `.proto` files are the source of truth. A pre-build step
   compiles them into C++ stubs *and* ergonomic Python / Rust client libraries. You almost
   never hand-write client code or wire serialization.

---

## 2. Plugin map

Each top-level `Tempo*` directory is an Unreal plugin (`<Name>/<Name>.uplugin`,
`<Name>/Source/<Module>/`). Source files: ~ values exclude vendored third-party.

| Plugin | Purpose | Depends on |
|---|---|---|
| **TempoCore** | gRPC server, time control (pause/play/step, wall-clock vs fixed-step), settings, subsystem base classes, the proto→client codegen, vendored gRPC. The foundation everything else builds on. | — |
| **TempoSensors** | Synthetic sensors: camera (RGB / depth / semantic+instance labels / 2D bounding boxes / H.264 video) and lidar (point clouds, per-beam calibration, reflectivity). Multi-tile wide-FOV / fisheye lens models, GPU readback, streaming. | TempoCore, AV/NV/AMF/WMF/VT codecs |
| **TempoWorld** | World manipulation by reflection: spawn/destroy actors & components, get/set *any* `UProperty`, query actor state (pose/velocity/bounds), overlaps, raycasts. | TempoCore |
| **TempoMovement** | Drive pawns/vehicles: normalized throttle/steer (open-loop), velocity/acceleration commands (closed-loop), kinematic bicycle/unicycle + Chaos vehicle models, AI move-to. | TempoCore |
| **TempoAgents** | Large-scale traffic/crowd agents on Unreal **Mass** (ECS) + **ZoneGraph**; StateTree behaviors; gRPC map-query service (lanes, zones, traffic-light state). | TempoCore, External/Traffic, External/ZoneGraph |
| **TempoGeographic** | Georeferencing (WGS84 ↔ Unreal cartesian), sim date/time, sun position. | TempoCore |
| **TempoPCG** | Custom Procedural Content Generation nodes (runtime grass LOD, debris scatter). | TempoCore |
| **TempoROS** | Native ROS 2 (rclcpp) embedded in Unreal — no external bridge process. Vendors a large ROS tree (~thousands of files) under `Source/ThirdParty/rclcpp`. Custom `.msg`/`.srv` codegen. *Optional.* | — |
| **TempoROSBridge** | Maps Tempo gRPC services ↔ ROS topics/services. One submodule per domain (Core/Sensors/Movement/Geographic). *Optional; remove to run without ROS.* | TempoROS + the bridged plugin |

`External/` holds **vendored / forked Epic plugins**: `Traffic` (MassTraffic sample),
`ZoneGraph`, `RuleProcessor` (PointCloud). Treat these as third-party — they have their own
conventions and the only existing automation tests in the repo (in `RuleProcessor`). Don't
restyle them to match Tempo.

---

## 3. The core architecture pattern (RPC services)

This is the single most important pattern. Every client-facing capability is an RPC service
implemented by a UE **subsystem** that registers handlers on the gRPC server.

**Flow:** `client stub → gRPC channel (:10001) → FTempoServer completion queue → handler delegate on a UObject subsystem → ResponseDelegate → client`

To add or understand a service:

1. **Proto** in `<Module>/Public/*.proto` or `Private/*.proto`. Conventions enforced by
   `gen_protos.py`: package defaults to the module name; **RPC names must be unique within a
   module** (the Python API is flattened per-module, no service prefix). Avoid Rust-keyword
   field names (`type`, `match`, `move`) — use qualified names like `actor_type`.
2. **Service provider**: a class implementing `ITempoServiceProvider`
   (`TempoCore/.../TempoServiceProvider.h`) — usually a subsystem deriving one of Tempo's
   base classes in `TempoSubsystems.h` (`UTempoGameWorldSubsystem`, etc., which guard against
   CDO/duplicate instantiation).
3. **Register** in `RegisterServices(FTempoServer&)` using `SimpleRequestHandler` (unary) or
   `StreamingRequestHandler`, binding the generated `AsyncService::RequestXxx` to a member
   function with signature
   `void Handler(const ReqType&, const TResponseDelegate<RespType>&) const`.
4. **Respond** by calling `ResponseContinuation.ExecuteIfBound(response, grpc::Status_OK)` —
   may be deferred (async).
5. The **codegen runs automatically** on the next build (PreBuildSteps in the `.uplugin`),
   producing C++/Python/Rust clients.

Reference implementations: `UTempoTimeServiceSubsystem`, `UTempoCoreServiceSubsystem`,
`UTempoWorldControlServiceSubsystem`, `UTempoSensorServiceSubsystem`.

---

## 4. Codegen pipeline

`.proto` is the source of truth; generated code is committed (see
`TempoCore/Content/Python/API/tempo_sim/`, `TempoCore/Content/Rust/`).

- `TempoCore/Content/Python/gen_protos.py` — discovers all protos across plugins (respecting
  module deps), validates naming, runs `protoc` → C++ `.pb.{h,cc}` + Python `_pb2`/`_pb2_grpc`,
  and re-exports protos for the C++/Rust generators.
- `TempoCore/Content/Python/gen_api.py` — introspects proto descriptors, uses Jinja2 to emit
  ergonomic per-module wrappers with sync + async variants (`tempo_sim.tempo_core`, etc.).
- Rust (`gen_rust_api.py`, opt-in `TEMPO_GEN_RUST_API=1`) and C++ (`gen_cpp_api.py`) generators.
- `TempoCore/Scripts/GenAPI.sh` builds/refreshes the `TempoEnv` venv and installs the Python pkg.
- A `.tempo_prebuild_cache.json` skips unnecessary regeneration.

**Direction in flight** (see `PYTHON_API_SPLIT_PLAN.md`): splitting the Python API into a
canonical `tempo-sim` distribution + per-project `tempo-sample`, and namespacing protos under
the package (`import tempo_sim.TempoSensors`).

---

## 5. Build / run / package

All via `Scripts/` (`.sh` + `.bat` mirrors; run `.sh` under Git Bash on Windows). See also
the `reference_build_scripts` memory.

- **`Setup.sh`** (once): installs the **Tempo toolchain** (`UseTempoToolchain.sh` edits the
  host `*.Target.cs`), applies **EngineMods**, downloads deps, installs git hooks that keep
  mods/deps synced across checkouts. `-skip-hooks` only for active Tempo devs.
- **`Build.sh`** → UBT `<Project>Editor`. **`Run.sh`** → opens the editor. **`Package.sh`** →
  `RunUAT BuildCookRun` to `Packaged/`. **`Clean.sh`** wipes artifacts.
- **Engine path**: UE 5.7 lives at `/Users/Shared/Epic Games/UE_5.7` (path has a space —
  quote it). On Linux set `UNREAL_ENGINE_PATH`; Mac/Win auto-detect.

**Why the toolchain & engine mods exist:** gRPC/Protobuf static libs are vendored into
TempoCore and re-exported to other modules; custom toolchains
(`TempoVCToolChain`/`TempoMacToolChain`/`TempoLinuxToolChain`) fix symbol re-export so
duplicate globals don't crash. `EngineMods/{5.6,5.7}/` patch UBT, AutomationTool, ZoneGraph,
MassCrowd, PCG **in place** (idempotently, via `InstallEngineMods.sh` reading
`EngineMods.json`) so users don't need a custom-built engine. `TempoModuleRules` (added as a
mod) auto-adds the `ProtobufGenerated` include paths and is the base class for every Tempo
`*.Build.cs`.

**Third-party deps**: `SyncDeps.sh` hash-verifies and downloads prebuilt gRPC (TempoCore) and
rclcpp (TempoROS) from GitHub releases (`ttp_manifest.json` per dep). Not committed; fetched.

---

## 6. Conventions

- **Modules**: `<Plugin>` (Runtime) and `<Plugin>Editor` (Editor) under `Source/<Module>/`
  with `Public/` + `Private/`. Every `*.Build.cs` extends `TempoModuleRules`, sets
  `PCHUsage = UseExplicitOrSharedPCHs`. TempoCore is `bCanHotReload = false` (gRPC statics).
- **Copyright header**: `// Copyright Tempo Simulation, LLC. All Rights Reserved`.
- **Naming**: Unreal conventions — `U`/`A`/`F`/`I`/`E` prefixes, PascalCase. Service
  subsystems are `U<Domain>ServiceSubsystem`.
- **Units / frames at the API boundary**: internally Unreal is cm, degrees, left-handed.
  Proto APIs are **meters, radians, right-handed**. Conversions go through
  `TempoConversion.h` (`QuantityConverter<CM2M, L2R>`, etc.). Note the documented exception:
  scalar `set_float_property` / vector `set_*_property` do **not** auto-convert (only
  transforms do) — they take Unreal-native units.
- **Actor identity over RPC**: use `UTempoCoreUtils::GetActorIdentifier`, *not*
  `GetActorNameOrLabel` (editor label race adds/drops `_C`). See `actor_identifier_rpc_naming`
  memory.
- **Config**: `UTempoCoreSettings` (`UDeveloperSettings`), stored under `Config/` with
  command-line overrides. Plugin-owned config (incl. CoreRedirects) belongs in the **plugin's**
  Config, not the project's (`plugin_config_scope` memory).
- **Render-thread rule**: `OnRenderCompleted` runs on the render thread — never block on a GPU
  fence there (deadlocks). Synchronous waits belong on the game thread via
  `FlushRenderingCommands` (`sensor_readback_thread_model` memory; see `PROGRESS.md`).

---

## 7. Testing — current state: **none**

There is **zero automated test coverage** of Tempo's own code. No Unreal automation tests
(`IMPLEMENT_*_AUTOMATION_TEST`, `DEFINE_SPEC`), no Python tests, no `Tests/` dirs, and CI
(`.github/workflows/build_and_package.yml`) only **builds and packages** — it runs no tests.
The only test code in the tree is inside vendored `External/RuleProcessor`.

The `ExampleClients/Python` scripts (`SensorPlayground.py`, `WorldPlayground.py`,
`MovementPlayground.py`, `LidarPreview.py`) are **manual demos, not tests** — but they are the
natural seed for an end-to-end Python test suite.

If you add tests, see the strategy notes the team is developing; broadly: pure logic (lens
models, kinematic models, unit/frame conversions, instance-ID allocation, property-path
parsing) → C++ automation specs; client API contracts → pytest against the generated
`tempo_sim` package; full behavior (rendering, agents, physics) → headless functional tests /
Gauntlet driven over the gRPC API. **When you add the first test, also add a CI job to run it**
— today nothing would catch a regression.

---

## 8. Gotchas

- Don't edit generated code (`*_pb2.py`, `*.pb.cc`, `Content/Rust/`, `ProtobufGenerated/`).
  Change the `.proto` and rebuild.
- Don't restyle `External/` — it's vendored Epic code.
- Quote the engine path (it contains a space).
- Changing a `.proto` is a client-facing API change: it regenerates Python/Rust/C++ clients
  and may need CoreRedirects (in the plugin's Config) for renamed assets/classes.
- `MIGRATION_v1.md` documents the v0→v1 module consolidation (e.g. TempoCamera+TempoLidar →
  TempoSensors); proto packages were renamed, so old external clients must regenerate.
- Half-pixel offset: the distortion-map UV loop in `TempoCamera.cpp` must use `U + 0.5`
  (pixel center) or TAA breaks (`distortion_map_half_pixel` memory).

---

## 9. Where to look first

- New capability over the API → §3, copy an existing `*ServiceSubsystem`.
- Sensor work → `TempoSensors/Source/TempoSensors/{Public,Private}` (`TempoCamera`,
  `TempoTiledSceneCaptureComponent`, `TempoLensModels`, `TempoSensorInterface`).
- Build/toolchain/deps → `Scripts/`, `EngineMods/`, `*.Build.cs`.
- Client examples → `ExampleClients/`.
- Project status & direction → `PROGRESS.md`, `MIGRATION_v1.md`, `PYTHON_API_SPLIT_PLAN.md`,
  and each plugin's `README.md`.
</content>
</invoke>
