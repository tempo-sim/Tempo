# TempoSensors

TempoSensors simulates synthetic robotics sensors — RGB cameras (with depth, semantic labels,
instance labels and 2D bounding boxes) and rotating lidars — by repurposing Unreal's renderer.

Sensors are `USceneComponent` types you drop on any actor. Clients stream data over Tempo's gRPC
API at whatever rate you configure.

| Component | What it does |
| --- | --- |
| `TempoSensorServiceSubsystem` | gRPC service that routes client requests to active sensors. |
| `UTempoSceneCaptureComponent2D` / `UTempoTiledSceneCaptureComponent` | Shared infrastructure: multi-view rendering, lens-distortion models, plugin settings. |
| `UTempoCamera` | Camera component. Color / label / depth / 2D bounding boxes / H.264 video. |
| `UTempoActorLabeler` | World subsystem. Tags meshes via the custom-depth stencil from a Label Table data table. |
| `UTempoLidar` | Lidar component. Spherical scan rendered via 1–3 perspective tiles. |

## Notable features

- **Multiple measurement types per camera in one render.** Color, semantic / instance label,
  depth, and 2D bounding boxes all come out of a single capture per camera per frame; depth is
  bit-packed into the alpha channel of an HDR atlas via the `M_TempoCamera_Distort_WithDepth`
  material and unpacked on the GPU. Depth disables itself automatically when no client is
  requesting it, saving about half the readback bandwidth.
- **Tiled rendering for wide-FOV / fisheye cameras.** Pinhole, Brown-Conrady, and rational-radial
  models render in a single perspective pass. Kannala-Brandt and Double Sphere fisheye models
  split into 1, 2, or 4 perspective tiles when the FOV crosses 120° (per axis), with tile
  rotations and off-axis frustums chosen to evenly distribute pixel density across the field. Up
  to 240° FOV. All tiles render in **one** `FSceneViewFamily`, so they share scene setup, GPU
  scene update, Lumen wire-up, and ray tracing — versus one `FSceneRenderer` per tile, which
  costs 2–4× more.
- **Tiled rendering for wide-FOV lidars.** A single perspective render covers up to 120°
  horizontal FOV. Up to 240° splits left/right; up to 360° splits left/center/right. Per-beam
  intrinsic calibration (`FLidarBeamCalibration`) supports vendor-style channel files (per-channel
  elevation + azimuth offset).
- **Tile seam handling.** Multi-tile cameras feather across seams (`FeatherPixels`, default 16)
  using a precomputed resolve map, hiding per-tile TAA / auto-exposure history discontinuities.
  Depth and label channels — neither safely averageable — switch ownership at the centerline
  rather than blending. A shared exposure bias (a P-controller fed by the proxy capture's
  auto-exposure) keeps tile brightness consistent.
- **Single-tile fast path.** When exactly one tile is active, depth is off, and
  `UpsamplingFactor=1`, the camera renders straight to the final render target — no aux-unpack, no
  proxy-tonemap pass, no merge — saving one `FSceneRenderer` and two Canvas blits per frame.
- **Runtime reconfiguration.** Lens model, FOV, resolution, beam counts, beam calibration and
  feather can all change at runtime (via Blueprint, the Editor, or `set_*_property` over the API).
  A reconfigure is applied at a safe point — when no readback is in flight — so it never tears
  mid-frame.
- **Pixel-perfect distortion.** Pinhole gets `Nearest` filtering by default (1:1 sampling, no
  blur); narrow non-pinhole gets `Bilinear`; wide (>120°) equidistant gets `Bicubic` to handle the
  highly non-uniform sampling density at the optical center. Override via
  `bAutoTextureFilterType` / `TextureFilterType`. Depth on equidistant lens models is reported as
  Euclidean distance from the camera origin, not depth along the camera axis, avoiding seam
  discontinuities.
- **Two label-override mechanisms.** Per-mesh labels via the Label Table data table (Static Mesh
  and Actor-class entries; Static Mesh wins). Visually imperceptible per-pixel overrides via
  subsurface color — used, for example, to label lane-line decals differently from the road they
  live on.
- **Hardware-encoded H.264 video**, alongside raw color. [See below](#stream-h264-video).

## Getting started

### Add a sensor to an actor

`UTempoCamera` and `UTempoLidar` are spawnable scene components. Add either to any actor in the
Editor, or spawn one at runtime via the Tempo API (see the `flow_add_sensor` flow in
`ExampleClients/Python/SensorPlayground.py`).

A pre-built `BP_SensorRig` blueprint ships in `TempoSensors/Content/SensorRig/` if you just want
to drop one in — this is what [Hello World](../getting-started/hello-world.md) spawns.

`Project Settings → Plugins → Tempo Sensors` is where you point the plugin at your project's Label
Table and tune the rest. See the [settings reference](../reference/settings.md#tempo-sensors).

### Stream data to a client

Python clients live under `ExampleClients/Python`:

- **`SensorPlayground.py`** — interactive REPL: list available sensors, randomize post-process
  settings, start/stop streams, record streams to temporary directories.
- **`LidarPreview.py`** — a minimal example that streams a single lidar at a target rate and
  visualizes returns colorized by distance / intensity / label.
- **`RerunPlayground`** — streams *every* available sensor plus ground-truth world state into the
  [rerun](https://rerun.io) viewer, with a web control panel for editing properties live.

[:octicons-arrow-right-24: Example clients](../clients/examples.md)

The streaming and decoding helpers (`tempo_sim.TempoImageUtils`, `tempo_sim.TempoLidarUtils`)
cover the common cases: start a stream, decode color / depth / label / lidar frames, save to disk.

Every measurement is its own streaming RPC, and `get_available_sensors()` tells you what exists:

| RPC | Yields |
|---|---|
| `get_available_sensors` | Every active sensor, with its owner, name, rate, and measurement types. |
| `stream_color_images` | Color frames. |
| `stream_depth_images` | Depth frames (float32 metres per pixel). |
| `stream_label_images` | Semantic or instance label frames. |
| `stream_bounding_boxes` | Axis-aligned 2D boxes per instance. |
| `stream_lidar_scans` | `LidarScanSegment`s. |
| `stream_video` | H.264 `VideoFrame`s. |

### Configure a camera

The properties most worth knowing about — all `EditAnywhere` / `BlueprintReadWrite`, all
hot-reconfigurable:

`LensParameters` (`FTempoLensParameters`)

:   `LensModel` (Pinhole / Brown-Conrady / Rational / Kannala-Brandt / Double Sphere) plus the
    K-coefficients / Xi / Alpha consumed by that model. The Editor hides parameters the selected
    model does not use.

`FOVAngle`

:   Horizontal FOV, inherited from `USceneCaptureComponent2D`. Ceilings vary by model: 170°
    pinhole / Brown-Conrady / rational, 240° Kannala-Brandt, 280° Double Sphere. Out-of-range
    values produce both a log error and an on-screen warning.

`SizeXY`

:   Output image size — the equidistant output for fisheye lenses, the perspective render directly
    for pinhole / Brown-Conrady / rational.

`FeatherPixels`

:   Seam-blend width for multi-tile lenses (default 16, ignored when there is only one tile).

`bEnableScreenPercentage` / `ScreenPercentage`

:   Per-tile rasterization fraction (TSR / TAAU upscale to view rect when `<100`). Trades shading
    detail for GPU.

`UpsamplingFactor` (1.0–4.0)

:   Scales the perspective render's view rect by this factor before bilinear-downsampling to
    `SizeXY` in the stitch. Useful when distortion concentrates pixels in a small angular region
    (wide fisheye) and 1:1 sampling looks pixelated. Atlas memory grows by K².

`RateHz`

:   Capture rate.

### Stream H.264 video { #stream-h264-video }

Cameras expose a `Video` measurement alongside `ColorImage`. Subscribe via `VideoRequest` and
consume the resulting `VideoFrame` stream (Annex-B NAL units; `key_frame=true` marks IDRs, which
carry SPS+PPS so a fresh decoder can sync on the next keyframe).

| Field | Meaning |
|---|---|
| `codec` | H.264 today (one entry in the enum, room to grow). |
| `bitrate_kbps` | `0` for the default (~8000 at 1080p). |
| `keyframe_interval` | Frames between IDRs; `0` for the default (30). Lower values reduce join latency at the cost of bitrate. |
| `profile` | `H264_BASELINE` / `H264_MAIN` / `H264_HIGH`. |

Video is color-only — depth and labels need lossless — and opt-in per request, so clients that
want raw pixels keep the existing `ColorImage` path. Encoding goes through Unreal's experimental
`AVCodecs` plugin (NVENC on Win64/Linux, VideoToolbox on Mac, AMF/WMF on Win64), one encoder per
camera, with `RepeatSPSPPS` so SPS/PPS prepend every IDR.

The encoder is created lazily on first request and reopens automatically when resolution or any
per-request parameter changes. Multiple subscribers to the same camera share one encoder — every
subscriber receives the same encoded bytes — so adding clients is cheap, but they all share the
same bitrate / keyframe interval / profile (last writer wins on reconfigure).

Python clients use `tempo_sim.TempoImageUtils.stream_video_images(...)` (PyAV decoder); Rust
clients see the wiring in `ExampleClients/Rust/SensorPlayground` (ffmpeg-next decoder, which needs
FFmpeg 8 dev headers locally). C++ client decode is not yet provided.

### Configure a lidar

- `HorizontalFOV` / `VerticalFOV` (degrees), `HorizontalBeams` / `VerticalBeams` (counts).
- `BeamCalibration` (optional): an array of `FLidarBeamCalibration` (per-channel `ElevationDeg` +
  `AzimuthOffsetDeg`). When non-empty it replaces the uniform `VerticalBeams` × `VerticalFOV`
  grid; vertical FOV is derived from the elevations plus one beam-spacing of padding.
- `MinDistance` / `MaxDistance` (cm), `IntensitySaturationDistance` (cm), `MaxAngleOfIncidence`
  (deg) — beyond which a return is dropped.

The output `LidarScanSegment` (one per active tile / segment per scan; `scan_count` tells you how
many to expect per frame) carries per-return `distances`, `intensities`, `labels`, `azimuths` and
`elevations`. Azimuths and elevations are negated from Unreal's internal left-handed Z-down
convention so client-side point-cloud math renders right-handed Z-up directly.

!!! warning "Per-return payloads are `bytes`, not repeated floats"

    Since API v0.2.0 the per-return and per-pixel arrays are opaque `bytes` blobs, so a client can
    reinterpret them in place (`np.frombuffer`) instead of materializing every element. If you are
    upgrading from v0.1.1, read [the migration guide](../migration/sensors-v0.2.0.md) — one field
    (`labels`) breaks silently.

### Working with labels

`UTempoActorLabeler`, a world subsystem, writes labels into the custom-depth stencil at
`BeginPlay` and whenever a primitive component registers. It reads the mapping from the
`SemanticLabelTable` you configure in Project Settings — a `DataTable` of `FSemanticLabel` rows,
each with a stencil value plus a set of Actor classes and/or Static Mesh assets that should
receive that label.

Static Mesh entries take precedence over Actor entries, so you can label a base-mesh actor one way
and selected meshes on it another — lane decals as `LaneLine` on top of road actors labeled
`Road`, for instance. TempoSample's `Content/Labels/TempoSampleLabelTable` is a worked example of such a table.

In `Instance` label mode (`Project Settings → Tempo → Sensors → Label Type`), each labeled actor
also gets a unique 1–255 instance ID. Two flags control reuse:

- **Globally Unique Instance Labels** — don't reclaim IDs of destroyed actors.
- **Instantaneously Unique Instance Labels** — don't repeat IDs even after exhausting all 256.

Bounding-box requests use the instance label image to compute axis-aligned 2D boxes per instance,
attaching the corresponding semantic ID via the labeler's instance→semantic map.

#### Inspecting and editing labels from a client

The label table is also reachable over the API, which is what you want when generating datasets
across many scenes rather than hand-editing a `DataTable`:

| RPC | What it does |
|---|---|
| `get_semantic_classes` | List the semantic classes in the label table. |
| `get_all_actor_labels` | Every actor's current label. |
| `get_labeled_actor_types` | The actor classes the table assigns labels to. |
| `get_all_static_mesh_types` | The static meshes the table knows about. |
| `set_actor_type_semantic_id` | Assign a semantic ID to an actor class. |
| `set_static_mesh_type_semantic_id` | Assign a semantic ID to a static mesh — takes precedence over the actor-class entry. |
| `get_instance_to_semantic_id_map` | Map instance IDs back to semantic IDs, for decoding instance label images. |

## Timing: pipelined or synchronous

In `FixedStep` time mode the default is to block the game thread until each frame's sensor data is
ready, so gRPC clients receive data with the simulation frame it was captured in.

Setting `Project Settings → Tempo → Sensors → Pipelined Rendering = true` lets the game thread
continue while game / render / readback run in parallel — higher throughput at the cost of 1–2
frames of latency. Each measurement carries the correct `capture_time_s` and `sequence_id`
regardless, so a client always knows which simulation frame it is looking at.

## Performance notes

- Camera `bDepthEnabled` is toggled automatically by request demand. If no client is asking for
  depth, the camera transparently drops to the smaller (4-byte) pixel format.
- The sensor tick path defers reconfigures until reads have drained, so changing `LensParameters`,
  `SizeXY` and friends mid-stream is safe — but does not take effect until the in-flight queue
  empties.
- `Max Camera Render Buffer Size` (default 4) caps how far a sensor can fall behind. Captures past
  this are skipped with a warning.
- The plugin patches an `FRayTracingScene` engine bug
  (`bEnableRayTracingSceneReadbackBuffersOverrunWorkaround`, on by default) that otherwise crashes
  when many ray-tracing-using scene captures run in one frame.

## Architecture, briefly

Every Tempo sensor inherits `UTempoSceneCaptureComponent2D`, which extends
`USceneCaptureComponent2D` with dynamic pixel-buffer formats, time-mode-aware blocking, a ring of
staging textures for GPU→CPU readback, and a distortion-map texture utility.

Sensors that need multiple perspective views per capture — today `UTempoCamera` and
`UTempoLidar` — further inherit `UTempoTiledSceneCaptureComponent`, which owns the shared atlas
render target, the texture-read queue, the capture timer, and the per-tile reconfigure/retention
plumbing.

Multi-tile rendering goes through `TempoMultiViewCapture::RenderTiles`, a small wrapper that
mirrors engine-private `SceneCaptureRendering` logic to assemble one `FSceneViewFamily` with N
views — each with its own view rect, view state, post-process settings and projection matrix —
then renders it through one `FSceneRenderer`. This is the single biggest performance win in the
plugin versus the more obvious "one `USceneCaptureComponent2D` per tile" design.

The full sensor frame for a camera is approximately:

1. **Tile multi-view render** → atlas RT (HDR, label+depth bit-packed in alpha; or LDR
   direct-to-final-RT in the single-tile fast path).
2. **Aux unpack pass** → label+depth bytes RT.
3. **Color stitch + feather pass** → equidistant HDR RT.
4. **Proxy capture** → the camera's main capture, with a post-process material that swaps the HDR
   stitched output in for scene color before bloom / auto-exposure / tonemap. This runs your
   post-process settings on the stitched image.
5. **Merge pass** → packs (LDR color, label, optional depth) into the final RT.
6. **Staging copy + GPU fence** → readback target.

For lidar it is simpler: one multi-view render straight into a packed atlas, one staging copy.

!!! warning "Pinned engine version"

    `TempoMultiViewCapture` reproduces logic from engine-private `SceneCaptureRendering.cpp` and
    is pinned to UE 5.6 / 5.7 / 5.8 behind a `#error` guard. When upgrading the engine, re-diff
    against `SetupViewFamilyForSceneCapture`, `SetupSceneViewExtensionsForSceneCapture`,
    `CreateSceneRendererForSceneCapture`, and `UpdateSceneCaptureContent_RenderThread`.

## API reference

[:octicons-arrow-right-24: `TempoSensors` RPCs](../reference/api/tempo-sensors.md)
