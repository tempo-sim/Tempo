# TempoSensors
`TempoSensors` includes the `TempoCamera` and `TempoLabels` modules and will be the home for any features using Unreal's rendering to simulate synthetic sensor data or ground truth labels.

## SensorPlayground
An example client demonstrating streaming sensor data and manipulating sensors via the Tempo API is included in `Tempo/ExampleClients/SensorPlayground.py`. 

## Scene Capture
`TempoSensors` uses the Unreal's `USceneCaptureComponent2D` to capture sensor data from different perspectives in the simulated scene.

`UTempoSceneCaptureComponent2D` expands `USceneCaptureComponent2D` in a few ways:
- Adds support for a dynamic pixel buffer type, so that sensors can change render target formats at runtime
- Adds logic to handle Tempo's different time modes (Real Time and Fixed Step), blocking to ensure all sensor measurements are sent in the frame they are captured in Fixed Step mode
- Adds virtual methods to allow derived, concrete sensor types to declare whether they have pending requests, and to implement the logic to decode scene captures into sensor measurements.

## Sensor Types
### TempoCamera
`TempoCamera` can capture three different types of images: color, semantic label, and depth. `TempoCamera` uses two tricks to improve usability and performance:
- It can capture all three of the above image types in a single render pass, thanks to a custom 8-byte pixel buffer and a "reinterpret cast" from floating point to fixed point depth in the `M_CameraPostProcess_WithDepth` post-process material.
- It uses a custom stencil buffer to capture labels, but it can also use visually-imperceptible changes in a material's subsurface color to override the label on certain parts of an object (for example, to label lane lines differently from the road itself).

`TempoCamera` leverages `UTempoSceneCaptureComponent2D`'s ability to dynamically change render target format and pixel buffer type to enable or disable depth rendering as needed, since adding depth incurs a not-insignificant performance hit (depth is rendered either way, but copying it doubles the size of the pixel buffer). Semantic labels are effectively free (since there are no 3-byte pixel buffers), so `TempoCamera` has only two modes (depth and no-depth).

Some helpful client utilities for streaming, decoding, and displaying images are available in `tempo.TempoImageUtils`.

### TempoLidar
`TempoLidar` captures distances, labels, and intensities by sampling points from the 3D scene using a spherical pattern (a grid of constant elevation and azimuth angles). It does so using one, two, or three scene capture components to capture scene depths and normals, and then resampling the implied "surface" with 3D rays. Each of these components produces "scan segments" which can be streamed via the TempoSensors API. Like TempoCamera, it leverages the same custom pixel buffer and depth "reinterpret cast" to capture the depth and normals from a single render pass. Unlike TempoCamera, which has different image types for color, label, and depth, all three of distance, label, and intensity are included in each Lidar scan.

Some helpful client utilities for streaming, decoding, and displaying Lidar scans are available in `tempo.TempoLidarUtils`. There is also a standalone `LidarPreview.py` example client under `Tempo/ExampleClients`.

## TempoLabels
`TempoLabels` includes the `UTempoActorLabeler` subsystem, which "labels" meshes in the scene by assigning values to their custom depth stencil on `BeginPlay` and whenever a component is created.

To specify which meshes should be labeled with which labels, the user must create a Label Table. The Label Table is a `DataTable` with `SemanticLabel` row type. Each row can specify Actor classes and/or Static Mesh types that should get certain label.

When a Static Mesh is specified, all components with that Static Mesh type will be labeled. When an Actor class is specified, all meshes on that actor will get the label type, **except** those that have an explicit Static Mesh label.
