# TempoSensors

`TempoSensors` simulates synthetic robotics sensors — RGB cameras (with depth, semantic labels, instance labels, and 2D bounding boxes) and rotating Lidars — by repurposing Unreal's renderer. Sensors are `USceneComponent` types you drop on any actor; clients stream data over Tempo's gRPC API at the rate you configure.

The five modules in this plugin:

| Module | What it does |
| --- | --- |
| `TempoSensors` | gRPC service (`TempoSensorServiceSubsystem`) that routes client requests to active sensors. |
| `TempoSensorsShared` | Shared infrastructure: `UTempoSceneCaptureComponent2D`, `UTempoTiledSceneCaptureComponent`, multi-view rendering, lens-distortion models, plugin settings. |
| `TempoCamera` | `UTempoCamera` component. Color / label / depth / 2D bounding boxes. |
| `TempoLabels` | `UTempoActorLabeler` world subsystem. Tags meshes via the custom-depth stencil from a Label Table data table. |
| `TempoLidar` | `UTempoLidar` component. Spherical scan rendered via 1–3 perspective tiles. |

## Notable features

- **Multiple measurement types per camera in one render.** Color, semantic / instance label, depth, and 2D bounding boxes all come out of a single capture per camera per frame; depth is bit-packed into the alpha channel of an HDR atlas via the `M_TempoCamera_Distort_WithDepth` material and unpacked on the GPU. Depth disables itself automatically when no client is requesting it (saves about half the readback bandwidth).
- **Tiled rendering for wide-FOV / fisheye cameras.** Pinhole, Brown-Conrady, and rational-radial models render in a single perspective pass. Kannala-Brandt and Double Sphere fisheye models split into 1, 2, or 4 perspective tiles when the FOV crosses 120° (per axis), with tile rotations and off-axis frustums chosen to evenly distribute pixel density across the field. Up to 240° FOV (Kannala-Brandt and Double Sphere). All tiles render in **one** `FSceneViewFamily` so they share scene setup, GPU scene update, Lumen wire-up, and ray-tracing — vs. one `FSceneRenderer` per tile, which costs 2-4× more.
- **Tiled rendering for wide-FOV Lidars.** A single perspective render covers up to 120° horizontal FOV. Up to 240° splits left/right; up to 360° splits left/center/right. Per-beam intrinsic calibration (`FLidarBeamCalibration`) supports vendor-style channel files (per-channel elevation + azimuth offset).
- **Tile seam handling.** Multi-tile cameras feather across seams (`FeatherPixels`, default 16) using a precomputed resolve map, hiding per-tile TAA / auto-exposure history discontinuities. Depth and label channels — neither safely averageable — switch ownership at the centerline rather than blending. A shared exposure bias (P-controller fed by the proxy capture's auto-exposure) keeps tile brightness consistent.
- **Single-tile fast path.** When exactly one tile is active, depth is off, and `UpsamplingFactor=1`, the camera renders straight to the final RT (no aux-unpack, no proxy-tonemap pass, no merge) — saving one `FSceneRenderer` and two Canvas blits per frame.
- **Runtime reconfiguration.** Lens model, FOV, resolution, beam counts, beam calibration, and feather can all change at runtime (via Blueprint, editor, or `set_*_property` over the Tempo API). Reconfigure is applied at a safe point — when no readback is in flight — so it never tears mid-frame.
- **Pipelined or synchronous timing.** In `FixedStep` time mode the default is to block the game thread until each frame's sensor data is ready (gRPC clients receive data with the simulation frame it was captured in). Setting `Project Settings → Tempo → Sensors → Pipelined Rendering = true` lets the game thread continue while game / render / readback run in parallel — higher throughput at the cost of 1-2 frames of latency. Each measurement carries the correct `capture_time` and `sequence_id` regardless.
- **Two label-override mechanisms.** Per-mesh labels via the Label Table data table (Static Mesh and Actor-class entries; Static Mesh wins). Visually-imperceptible per-pixel overrides via subsurface color — used, for example, to label lane-line decals differently from the road they live on (`OverridableLabelRowName` / `OverridingLabelRowName` settings).
- **Pixel-perfect distortion.** Pinhole gets `Nearest` filtering by default (1:1 sampling, no blur); narrow non-pinhole gets `Bilinear`; wide (>120°) equidistant gets `Bicubic` to handle the highly non-uniform sampling density at the optical center. Override via `bAutoTextureFilterType` / `TextureFilterType`. Depth on equidistant lens models is reported as Euclidean distance from the camera origin (not depth along the camera axis), avoiding seam discontinuities.

## Getting started

### Add a sensor to an actor

`UTempoCamera` and `UTempoLidar` are spawnable scene components. Add either to any actor in the editor, or spawn one at runtime via the Tempo API (see the `flow_add_sensor` flow in `Tempo/ExampleClients/SensorPlayground.py`). A pre-built `BP_SensorRig` blueprint is included in `TempoSensors/Content/SensorRig/` if you just want to drop one in.

`Project Settings → Tempo → Sensors` is where you point the plugin at your project's Label Table and tweak max camera depth, color encoding (RGB8 / BGR8), max Lidar depth, Lidar upsampling factor, max camera render-buffer size (drop captures past this depth, default 4 frames), and the various stitch / distortion materials (defaults from `TempoSensors/Content/Materials/` are used when these are unset).

### Stream data to a client

Python clients live under `Tempo/ExampleClients`:

- `SensorPlayground.py` — interactive REPL: list available sensors, randomize post-process settings, start / stop streams, record streams to `tempfile.mkdtemp()`-ed directories.
- `LidarPreview.py` — minimal example that streams a single Lidar at a target rate and visualizes returns colorized by distance / intensity / label.

The streaming and decoding helpers (`tempo.TempoImageUtils`, `tempo.TempoLidarUtils`) cover the common cases — start a stream, decode color / depth / label / Lidar frames, save to disk.

### Configure a camera

The properties most worth knowing about (all `EditAnywhere` / `BlueprintReadWrite`, all hot-reconfigurable):

- `LensParameters` (`FTempoLensParameters`): `LensModel` (Pinhole / Brown-Conrady / Rational / Kannala-Brandt / Double Sphere) plus the K-coefficients / Xi / Alpha consumed by that model. The editor hides parameters that aren't used by the selected model.
- `FOVAngle` (inherited from `USceneCaptureComponent2D`): horizontal FOV. Ceilings vary by model: 170° pinhole / Brown-Conrady / rational, 240° Kannala-Brandt, 280° Double Sphere. Out-of-range values produce both a log error and an on-screen warning.
- `SizeXY`: output image size (the equidistant output for fisheye lenses, the perspective render directly for pinhole / Brown-Conrady / rational).
- `FeatherPixels`: seam-blend width for multi-tile lenses (default 16, ignored when there's only one tile).
- `bEnableScreenPercentage` / `ScreenPercentage`: per-tile rasterization fraction (TSR / TAAU upscale to view-rect when `<100`). Trades shading detail for GPU.
- `UpsamplingFactor` (1.0–4.0): scales the perspective render's view-rect by this factor before bilinear-downsampling to `SizeXY` in the stitch. Useful when distortion concentrates pixels in a small angular region (wide fisheye) and 1:1 sampling looks pixelated. Atlas memory grows by K².
- `RateHz`: capture rate.

### Configure a Lidar

- `HorizontalFOV` / `VerticalFOV` (degrees), `HorizontalBeams` / `VerticalBeams` (counts).
- `BeamCalibration` (optional): array of `FLidarBeamCalibration` (per-channel `ElevationDeg` + `AzimuthOffsetDeg`). When non-empty it replaces the uniform `VerticalBeams` × `VerticalFOV` grid; vertical FOV is derived from the elevations + one beam-spacing of padding.
- `MinDistance` / `MaxDistance` (cm), `IntensitySaturationDistance` (cm), `MaxAngleOfIncidence` (deg) — beyond which a return is dropped.

The output `LidarScanSegment` (one per active tile / segment per scan; `scan_count` tells you how many you'll receive per frame) carries per-return `distances`, `intensities`, `labels`, `azimuths`, and `elevations`. Azimuths and elevations are negated from Unreal's internal left-handed Z-down convention so client-side point-cloud math renders right-handed Z-up directly.

### Working with labels

`UTempoActorLabeler` (a world subsystem in the `TempoLabels` module) writes labels into the custom-depth stencil at `BeginPlay` and whenever a primitive component registers. It reads the mapping from the `SemanticLabelTable` you configure in Project Settings — a `DataTable` of `FSemanticLabel` rows, each with a stencil value plus a set of Actor classes and / or Static Mesh assets that should receive that label.

Static Mesh entries take precedence over Actor entries, so you can label a base-mesh actor one way and selected meshes on it another (e.g. lane decals as `LaneLine` on top of road actors labeled as `Road`).

In `Instance` label mode (Project Settings → Tempo → Sensors → Label Type), each labeled actor also gets a unique 1–255 instance ID. Two flags control reuse: `Globally Unique Instance Labels` (don't reclaim IDs of destroyed actors) and `Instantaneously Unique Instance Labels` (don't repeat IDs even after exhausting all 256). Bounding-box requests use the instance label image to compute axis-aligned 2D boxes per instance, attaching the corresponding semantic ID via the labeler's instance→semantic map.

### Performance notes

- Camera `bDepthEnabled` is automatically toggled by request demand — there's no point asking clients to opt out, but if no client is requesting depth the camera transparently drops to the smaller (4-byte) pixel format.
- The sensor Tick path defers reconfigures until reads have drained, so changing `LensParameters`, `SizeXY`, etc. mid-stream is safe but doesn't take effect until the in-flight queue empties.
- `Project Settings → Tempo → Sensors → Max Camera Render Buffer Size` (default 4) caps how far a sensor can fall behind. Captures past this are skipped with a warning.
- The plugin patches a `FRayTracingScene` engine bug (`bEnableRayTracingSceneReadbackBuffersOverrunWorkaround`, on by default) that otherwise crashes when many ray-tracing-using scene captures run in one frame.

## Architecture, briefly

Every Tempo sensor inherits `UTempoSceneCaptureComponent2D` (which extends `USceneCaptureComponent2D` with dynamic pixel-buffer formats, time-mode-aware blocking, a ring of staging textures for GPU→CPU readback, and a distortion-map texture utility). Sensors that need multiple perspective views per capture (today: `UTempoCamera` and `UTempoLidar`) further inherit `UTempoTiledSceneCaptureComponent`, which owns the shared atlas RT, the texture-read queue, the capture timer, and the per-tile reconfigure / retention plumbing.

Multi-tile rendering goes through `TempoMultiViewCapture::RenderTiles` — a small wrapper that mirrors engine-private `SceneCaptureRendering` logic to assemble one `FSceneViewFamily` with N views (each with its own view rect, view state, post-process settings, and projection matrix), then renders it through one `FSceneRenderer`. This is the single biggest performance win in this plugin vs. the more obvious "one `USceneCaptureComponent2D` per tile" design.

The full sensor frame for a camera is approximately:
1. **Tile multi-view render** → atlas RT (HDR, label+depth bit-packed in alpha; or LDR direct-to-final-RT in single-tile fast path).
2. **Aux unpack pass** → label+depth bytes RT.
3. **Color stitch + feather pass** → equidistant HDR RT.
4. **Proxy capture** → the camera's main capture, with a post-process material that swaps the HDR stitched output in for scene color before bloom / AE / tonemap. This runs the user's post-process settings on the stitched image.
5. **Merge pass** → packs (LDR color, label, optional depth) into the final RT.
6. **Staging copy + GPU fence** → readback target.

For Lidar it's simpler — one multi-view render straight into a packed atlas, one staging copy.

## Pinned engine version

`TempoMultiViewCapture` reproduces logic from engine-private `SceneCaptureRendering.cpp` and is pinned to UE 5.6 / 5.7 with a `#error` guard. When upgrading the engine, re-diff against `SetupViewFamilyForSceneCapture`, `SetupSceneViewExtensionsForSceneCapture`, `CreateSceneRendererForSceneCapture`, and `UpdateSceneCaptureContent_RenderThread` — line numbers were byte-identical between 5.6 and 5.7.
