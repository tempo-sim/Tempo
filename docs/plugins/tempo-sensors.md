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
- **Labels down to the individual component.** The Label Table matches on Actor class, Actor tag,
  Static Mesh asset, Skeletal Mesh asset, or component tag, most specific winning — so a lane-line
  decal can be `LaneLine` on top of a road actor labeled `Road`. On top of that, visually
  imperceptible per-pixel overrides via subsurface color. The whole table can be replaced at
  runtime from JSON, without restarting the sim.
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
each with a stencil value plus the things that should receive that label. TempoSample's
`Content/Labels/TempoSampleLabelTable` is a worked example of such a table.

Each row matches on five columns, and the most specific match wins:

| Column | Matches | Beats |
|---|---|---|
| `ActorTypes` | Every Actor of a class, and its subclasses | — |
| `ActorTags` | Actors carrying that tag | `ActorTypes` |
| `StaticMeshTypes` | Components rendering that static mesh — ISMC / foliage and Niagara mesh renderers included | `ActorTags`, `ActorTypes` |
| `SkeletalMeshTypes` | Skinned components rendering that skeletal mesh | `ActorTags`, `ActorTypes` |
| `ComponentTags` | Components carrying that tag | everything above |

So you can label a base-mesh actor one way and selected meshes on it another — lane decals as
`LaneLine` on top of road actors labeled `Road`, for instance. The two tag columns are the escape
hatches for what no class or asset can pick out: one instance of a class labeled differently from
the rest, or geometry built at runtime. An Actor carrying tags for two different labels resolves
to whichever its `Tags` array lists first.

Static and skeletal meshes get separate columns because the two asset types share no base class
narrower than `UStreamableRenderAsset`, which textures also derive from. They resolve through one
path-keyed lookup, so a mesh asset carries at most one label either way.

!!! warning "Label IDs run 0–253, not 0–255"

    The camera packs the label into the exponent field of its fp32 alpha channel, biased by `+1`,
    which keeps the alpha a normal, finite float for every `(label, depth)` pair — at the cost of
    the top two IDs. See `TempoSensorsConstants.h`. An out-of-range ID corrupts both the label
    *and* the depth of every pixel it covers, so the labeler logs an error for one at startup.

In `Instance` label mode (`Project Settings → Tempo → Sensors → Label Type`), each labeled actor
also gets a unique 1–253 instance ID. Two flags control reuse:

- **Globally Unique Instance Labels** — don't reclaim IDs of destroyed actors.
- **Instantaneously Unique Instance Labels** — don't repeat IDs even after exhausting all 253.

Bounding-box requests use the instance label image to compute axis-aligned 2D boxes per instance,
attaching the corresponding semantic ID via the labeler's instance→semantic map.

#### Inspecting and editing labels from a client

Everything above is also reachable over the API, which is what you want when generating datasets
across many scenes rather than hand-editing a `DataTable`. Reading what's in effect:

| RPC | What it does |
|---|---|
| `get_semantic_classes` | List the semantic classes in the label table, and what each one matches. |
| `get_all_actor_labels` | Every actor's current label. |
| `get_labeled_actor_types` | The actor classes the table assigns labels to. |
| `get_all_static_mesh_types` | Every static mesh rendered in the world, with instance counts and current label. |
| `get_all_skeletal_mesh_types` | The same for skeletal meshes. |
| `get_instance_to_semantic_id_map` | Map instance IDs back to semantic IDs, for decoding instance label images. |
| `get_label_table_as_json` | The whole table, in exactly the format `load_label_table` reads. |

Per-entry overrides, each taking `-1` to revert to whatever the table says:

| RPC | What it does |
|---|---|
| `set_actor_type_semantic_id` | Assign a semantic ID to an actor class. |
| `set_actor_tag_semantic_id` | Assign a semantic ID to an Actor tag — beats the actor's class. |
| `set_static_mesh_type_semantic_id` | Assign a semantic ID to a static mesh — beats both of the above. |
| `set_skeletal_mesh_type_semantic_id` | The same for a skeletal mesh. |

These sit in a layer *above* the table and survive a `load_label_table`, so clear one with `-1` if
you want a newly loaded table to decide.

The two mesh RPCs are each strict about their asset type: a skeletal mesh path handed to
`set_static_mesh_type_semantic_id` is rejected rather than silently accepted, so an override always
shows up in the `get_all_*_mesh_types` call that can read it back.

Whole-table and mode changes, each of which re-labels the world in place:

| RPC | What it does |
|---|---|
| `load_label_table` | Replace the entire table from a JSON string (`json`) or a file (`json_file`) — Unreal's DataTable JSON format, an array of rows keyed by `"Name"`. Supersedes the configured `SemanticLabelTable` until the world is torn down. Send it with neither field set to go back to the configured table. A table that fails to import *or* to validate is rejected whole, leaving the active one in place. |
| `set_label_type` | Switch between `LT_SEMANTIC` and `LT_INSTANCE`. |
| `set_instance_label_uniqueness` | Set the two instance-ID reuse flags. Governs future allocations only — already-labeled objects keep their IDs. |
| `set_label_row_overrides` | Set the `OverridableLabelRowName` / `OverridingLabelRowName` pair driving the subsurface-color per-pixel override, or clear both to disable it. Live sensors pick it up without a capture restart. |

!!! tip "Fetch, edit, load"

    `get_label_table_as_json` emits rows — and the entries within each row — in sorted order, so
    two fetches of the same table are byte-identical and a diff shows only what you changed. Round
    trip it: fetch, edit, `load_label_table`.

    `load_label_table` checks the table before installing it and rejects the whole request with the
    list of problems, so a typo'd asset path, a label ID outside 0–253, a missing or non-zero
    `NoLabel` row, or an actor type / mesh / tag two rows both claim comes back as an error rather
    than as quietly wrong images.

    Note that `set_label_row_overrides` names its rows by name, and those names are held separately
    from the table. Loading a table without those rows disables the per-pixel override and logs a
    warning — re-send `set_label_row_overrides` after a load that renames them.

## Timing: pipelined or synchronous

In `FixedStep` time mode the default is to block the game thread until each frame's sensor data is
ready, so gRPC clients receive data with the simulation frame it was captured in.

Setting `Project Settings → Tempo → Sensors → Pipelined Rendering = true` lets the game thread
continue while game / render / readback run in parallel — higher throughput at the cost of 1–2
frames of latency. Each measurement carries the correct `capture_time_s` and `sequence_id`
regardless, so a client always knows which simulation frame it is looking at.

`set_pipelined_rendering_enabled` flips the same switch at runtime. The barrier reads it fresh
every frame, so the change lands on the next one — no sensor teardown, no reconfigure. Useful for
running a scene fast and then dropping into lockstep for the frames you actually want to capture.

## Render grouping

Every scene render pays a large fixed cost before it shades a single pixel: visibility and mesh
draw command generation, shadow setup and shadow depth passes, the virtual shadow map array, the
Lumen scene and radiance cache update, the ray tracing scene build, Nanite setup, and a render graph
compile. At sensor resolutions those fixed costs dominate, so rendering every sensor on its own
leaves the GPU under-occupied and pays them once per sensor.

By default the tiled sensors (cameras and lidars) on one actor that capture at the same rate
render **together**: their tiles become the views of one scene render into an atlas the group owns,
exactly as the tiles of one wide-FOV camera already do. Each sensor's block is then copied back
into that sensor's own render target, so everything downstream — stitching, readback, decoding —
is unchanged. Cameras on the multi-tile path also need a proxy render per capture to tonemap and
meter the stitched image; the group batches those into one render too. The fixed per-render costs
(visibility, shadow setup, the Lumen and ray tracing scene updates, the render graph) are paid once
per group per stage instead of once per sensor; the per-pixel work is unchanged.

What grouping preserves:

- **Independent exposure.** Eye adaptation state is per view. The tiles of one camera share that
  camera's state so they cannot drift apart; different sensors in one group each meter their own
  scene, so a camera facing the sun and one facing shadow expose independently.
- **Independent temporal history.** TSR / TAA history is per tile and bounded to the tile's rect;
  neighbouring blocks in the atlas do not bleed into each other.
- **Per-sensor origins.** Each view is rendered from its own sensor's pose. The one thing a group
  collapses is Lumen's surface cache prioritization, which uses the first view's origin — fine for
  sensors meters apart on one vehicle, which is why grouping is per actor.

What determines a group:

- Owning actor, capture rate, and sensor class. Sensors that differ in any of these never share.
- Within a group, sensors also have to agree on family-level render settings (capture source,
  show flags, render target format, screen percentage). A camera on the single-tile fast path and
  a multi-tile camera, for instance, render as two families of the same group. A sensor whose
  settings match no other renders on its own, exactly as before.

What grouping constrains:

- **Rates are fixed while the simulation runs.** The group owns the capture timer, so all its
  members capture on the same frames. A `RateHz` change on a grouped sensor is logged as an error
  and reverted. Set rates before the sensor activates, or turn grouping off.
- A sensor whose block cannot fit in a single atlas (larger than the GPU's maximum texture size,
  or a group so large the atlas would exceed it) renders on its own with a one-time warning.

`Project Settings → Tempo → Sensors → Enable Sensor Render Grouping` turns this off, restoring one
render per sensor and per-sensor capture timers with runtime-changeable rates.

## Performance notes

- Render grouping (above) is the biggest lever: the more sensors on an actor at one rate, the
  more fixed per-render cost is amortized.
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
views — each with its own view rect, view state, exposure state, post-process settings, projection
matrix and owning component — then renders it through one `FSceneRenderer`. This is the single
biggest performance win in the plugin versus the more obvious "one `USceneCaptureComponent2D` per
tile" design.

A tiled sensor's capture is a sequence of render stages, each split in three so a group can render
one stage of several sensors at once: `GetRenderStageDesc` names the stage's block render target
and family-level settings, `PrepareRenderStage` builds its views relative to that block, and
`FinishRenderStage` runs whatever follows the render. The lidar has one stage; a camera on the
multi-tile path has two, its tiles and then its proxy tonemap view. `UTempoSensorRenderGroup` (one
per actor, rate and sensor class, owned by `UTempoSensorRenderGroupSubsystem`) runs stage by
stage: it packs the members' blocks into an atlas per family signature, offsets their view rects,
renders each family with one `RenderTiles` call, and copies each block back before calling the
member's `FinishRenderStage`. A sensor rendering on its own runs the same steps per stage around a
`RenderTiles` into its own block.

The full sensor frame for a camera is approximately:

1. **Tile multi-view render** → atlas RT (HDR, label+depth bit-packed in alpha; or LDR
   direct-to-final-RT in the single-tile fast path).
2. **Aux unpack pass** → label+depth bytes RT.
3. **Color stitch + feather pass** → equidistant HDR RT.
4. **Proxy render** → a second multi-view render (one view, no geometry, no ray tracing or Lumen)
   with a post-process material that swaps the HDR stitched output in for scene color before
   bloom / auto-exposure / tonemap. This runs your post-process settings on the stitched image and
   meters it for the exposure controller. In a render group the proxies of every camera on the
   actor are one family, just like their tiles.
5. **Merge pass** → packs (LDR color, label, optional depth) into the final RT.
6. **Staging copy + GPU fence** → readback target.

For lidar it is simpler: one multi-view render straight into a packed atlas, one staging copy.

!!! warning "Pinned engine version"

    `TempoMultiViewCapture` reproduces logic from engine-private `SceneCaptureRendering.cpp` and
    is pinned to UE 5.7 / 5.8 behind a `#error` guard. When upgrading the engine, re-diff
    against `SetupViewFamilyForSceneCapture`, `SetupSceneViewExtensionsForSceneCapture`,
    `CreateSceneRendererForSceneCapture`, and `UpdateSceneCaptureContent_RenderThread`.

## API reference

[:octicons-arrow-right-24: `TempoSensors` RPCs](../reference/api/tempo-sensors.md)
