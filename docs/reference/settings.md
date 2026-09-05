# Settings

Tempo's plugin settings live in **Project Settings → Plugins**, and are stored in your project's
`Config/DefaultPlugins.ini` under `[/Script/<Module>.<Class>]` section headers.

!!! note "Some settings are also RPCs"

    Time mode, sim steps per second, main-viewport rendering, and control mode can all be changed
    at runtime over the API. The Project Settings value is the startup default.

## Tempo Core

`[/Script/TempoCore.TempoCoreSettings]`

### Time

| Setting | Type | Default | What it does |
|---|---|---|---|
| `TimeMode` | enum | `WallClock` | `WallClock` or `FixedStep`. See [Time](../concepts/time.md). |
| `SimulatedStepsPerSecond` | `int32` | `10` | Evenly-spaced steps per simulated second executed in Fixed Step mode. Clamped ≥ 1. |
| `MaxWallClockTimeStep` | `double` | `0.0` | Largest time step allowed in Wall Clock mode; `0.0` means no limit. **Non-zero violates Wall Clock's strict guarantee** at the step that would have exceeded the maximum. |

### Server

| Setting | Type | Default | What it does |
|---|---|---|---|
| `ServerPort` | `int32` | `10001` | The port the Tempo gRPC server listens on. 1024–65535. Overridable with `-ServerPort=` on the command line. |
| `ServerCompressionLevel` | enum | `None` | Compression for server messages. **No compression is fastest when the client is on the same machine**; compression may reduce bandwidth across a network. |
| `MaxEventProcessingTimeMicroSeconds` | `int32` | `1000` | How long to spend processing gRPC events each tick. **Ignored in Fixed Step mode**, where all received events are processed every tick. 1–10000. |
| `MaxEventWaitTimeNanoSeconds` | `int32` | `1000` | How long to wait for an event to arrive on each check. 1–10000. |

### Packaging

| Setting | Type | Default | What it does |
|---|---|---|---|
| `bAssignLevelsToIndividualChunks` | `bool` | `false` | Assign each level its own chunk during packaging. **Requires** the project packaging settings `UsePakFile` and `GenerateChunks`. |

### Rendering and control

| Setting | Type | Default | What it does |
|---|---|---|---|
| `bRenderMainViewport` | `bool` | — | Whether to render the main viewport. Turning it off saves performance when only sensors matter. |
| `DefaultControlMode` | enum | `None` | The control mode to start in. |

### World settings

`ATempoWorldSettings` extends `AWorldSettings`, so these are per-level, under **World Settings**:

| Setting | Type | Default | What it does |
|---|---|---|---|
| `Manual Default Exposure Compensation` | `bool` | `false` | Whether to use a manually specified default exposure compensation. |
| `Default Exposure Compensation` | `float` | `0.0` | The manual value, −15 to 15. The **Set Default Exposure Compensation** button derives one from the level's directional light. |

## Tempo Sensors

`[/Script/TempoSensors.TempoSensorsSettings]`

### Labels

| Setting | Type | Default | What it does |
|---|---|---|---|
| `SemanticLabelTable` | `DataTable` | — | Maps Actor classes and Static Meshes to semantic labels. In `Instance` mode, every instance of an object appearing in this table is labeled. |
| `LabelType` | enum | `Semantic` | `Semantic` or `Instance`. |
| `bGloballyUniqueInstanceLabels` | `bool` | `false` | Don't reclaim instance IDs of destroyed actors. (Instance mode only.) |
| `bInstantaneouslyUniqueInstanceLabels` | `bool` | `false` | Don't repeat instance IDs even after exhausting all 256. (Instance mode only.) |

[:octicons-arrow-right-24: Working with labels](../plugins/tempo-sensors.md#working-with-labels)

### Camera

| Setting | Type | Default | What it does |
|---|---|---|---|
| `ColorImageEncoding` | enum | `BGR8` | Encoding for color images — `RGB8` or `BGR8`. |
| `MaxCameraDepth` | `float` | `100000.0` (1 km) | Expected maximum depth for a camera depth image, in cm. |
| `SceneCaptureGamma` | `float` | `2.2` | Gamma for simulated scene captures. |
| `OverridableLabelRowName` | `FName` | none | A label row that a per-pixel subsurface-color value may override. |
| `OverridingLabelRowName` | `FName` | none | The label used wherever a non-zero subsurface color is found on an object labeled `OverridableLabelRowName`. |

### Lidar

| Setting | Type | Default | What it does |
|---|---|---|---|
| `MaxLidarDepth` | `float` | `40000.0` (400 m) | Expected maximum depth for a lidar return, in cm. |

### Advanced

| Setting | Type | Default | What it does |
|---|---|---|---|
| `bPipelinedRendering` | `bool` | `false` | In Fixed Step mode, let the game thread advance without waiting for sensor readback. Higher throughput, 1–2 frames of latency. Each measurement still carries the correct `CaptureTime` and `SequenceId`. |
| `MaxRenderBufferSize` | `int32` | `4` | Max frames per camera to buffer before dropping, with a warning. |
| `bEnableRayTracingSceneReadbackBuffersOverrunWorkaround` | `bool` | `true` | Works around an `FRayTracingScene` buffer-overrun bug that otherwise asserts when many ray-tracing scene captures run in one frame. |
| `RayTracingSceneMaxReadbackBuffersOverride` | `uint32` | `128` | Size of the `FRayTracingScene` readback rings when the workaround is on. Must exceed (RT renders per frame) × (GPU frames in flight + margin). |
| `bEnableRayTracingSceneReadbackBuffersReleaseWorkaround` | `bool` | `true` | Stops `FRayTracingScene::EndFrame` deleting readback buffers with copies still in flight. Needed when a sensor renders *without* ray tracing while something else builds the RT scene for the same `FScene`. Costs some retained render memory. |
| Materials (`CameraPostProcessMaterial*`, `CameraStitch*`, `CameraProxyTonemapMaterial`, `LidarPostProcessMaterial*`) | `MaterialInterface` | from `TempoSensors/Content/Materials/` | The materials driving the capture pipeline. Defaults are used when unset; override only if you know the exact texture-parameter contract each one expects. |

## Tempo Agents

`[/Script/TempoAgents.TempoAgentsSettings]`

| Setting | Type | Default | What it does |
|---|---|---|---|
| `MaxThroughRoadAngleDegrees` | `float` | `15.0` | Maximum angle between roads entering an intersection before they are rejected as potential "through" roads. |

## Tempo ROS

`[/Script/TempoROS.TempoROSSettings]`

| Setting | Type | Default | What it does |
|---|---|---|---|
| `FixedFrameName` | `FString` | `map` | The "fixed" coordinate frame all ROS TF transforms are relative to. |
| `bPublishClock` | `bool` | `true` | Publish world time on `/clock`. **Disable when another node is the ROS time authority** — only one publisher is allowed on `/clock`. |
| `ROSDomainID` | `int32` | `0` | Isolates ROS domains on one local network. 0–101. |
| `RMWImplementation` | enum | — | The middleware implementation. **On macOS only CycloneDDS is supported.** |
| `CycloneDDS_URI` | `FilePath` | — | Path to a CycloneDDS XML config. Required for [shared-memory transport](../plugins/tempo-ros.md). |

## Setting them from a client

Any of these can also be read and written at runtime through TempoWorld's property API, since they
are ordinary `UPROPERTY`s on a `UDeveloperSettings` object — with the usual caveat that raw
property values are **not** unit-converted.

[:octicons-arrow-right-24: Getting and setting properties](../plugins/tempo-world.md#getting-and-setting-properties)
