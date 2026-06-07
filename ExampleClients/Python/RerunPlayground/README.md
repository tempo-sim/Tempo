# RerunPlayground — Tempo × rerun

An example client that streams **every available Tempo sensor** and the
**ground-truth world state** into the [rerun](https://rerun.io) viewer, builds a
viewer layout automatically, and exposes a small web control panel for editing
actor/component properties (e.g. camera lens distortion) live over the
TempoWorld API.

It talks to Tempo over the same gRPC API as the other example clients, so it
works on **Linux, macOS, and Windows** — rerun ships native viewers for all
three.

## What it does

- **Auto-discovery** — enumerates sensors (`GetAvailableSensors`) and actors and
  builds the visualization with no per-scene configuration.
- **All sensors** — color / depth / label images, 2D bounding boxes, H.264 video
  (optional), and lidar point clouds. Each is placed in 3D using the sensor's
  `capture_transform` and timestamped on a shared `sim_time` timeline.
- **3D ground truth** — nearby actors are drawn as oriented boxes with labels;
  ego speed is logged to a telemetry time-series; overlap events go to an
  events log.
- **Auto layout, persistent** — a 3D world view plus a grid of per-sensor 2D
  views, a telemetry plot, and an events log. The layout is sent as the viewer's
  *default*, so once you rearrange/save your own it survives restarts. Save a
  layout from the viewer's **File → Save blueprint…** (`.rbl`) and reload it any
  time; `--reset-layout` restores the generated one.
- **Live control** — a Gradio page (http://localhost:7860) with two sections:
  - **Streams**: choose each lidar's signal (intensity / distance / reflectivity /
    label / color) and toggle which images each camera streams — at runtime.
  - **Properties**: pick an actor/component, load its properties, and edit them.
    Filter by `distort` to reach a camera's lens-distortion parameters.

By default only **color images and lidar** stream; depth, labels, and bounding
boxes are off (they cost bandwidth/CPU and crowd out color real estate with many
cameras). Enable them with `--depth` / `--labels` / `--bboxes`, or per-camera from
the Streams panel.

## Install

```bash
pip install -r requirements.txt
```

The Tempo API package (`tempo_sim`) ships with the plugin and must be importable.
From this directory:

```bash
# macOS/Linux
export PYTHONPATH="$PYTHONPATH:../../../TempoCore/Content/Python/API"
# Windows (PowerShell)
$env:PYTHONPATH += ";..\..\..\TempoCore\Content\Python\API"
```

## Run

Run the launcher script directly (works from any directory — it puts the package
on `sys.path` for you), just like the other example clients:

```bash
python ExampleClients/Python/RerunPlayground.py --auto
```

Equivalently, run the package form from `ExampleClients/Python`:

```bash
python -m RerunPlayground --auto
```

Either spawns the rerun viewer, starts streaming, and opens the control panel at
http://localhost:7860.

Common options:

```bash
python -m RerunPlayground --ip 192.168.1.10 --port 10001   # remote Tempo
python -m RerunPlayground --colorize-lidar-by reflectivity   # initial lidar signal
python -m RerunPlayground --depth --labels --bboxes          # also stream these (off by default)
python -m RerunPlayground --sensors BP_SensorRig:TempoCamera BP_SensorRig:TempoLidar
python -m RerunPlayground --no-control                     # viewer only
python -m RerunPlayground --reset-layout                   # discard saved layout
python -m RerunPlayground --save-rrd session.rrd           # record instead of live view
python -m RerunPlayground --connect 127.0.0.1:9876         # use an already-running viewer
python -m RerunPlayground --config my_setup.yaml           # YAML config (keys mirror flags)
```

Run `python -m RerunPlayground --help` for the full list.

## Notes & limitations

- **Frames** — Tempo streams are right-handed / meters / radians, matching
  rerun, so no conversion is applied. All convention logic (Euler→quaternion,
  the Z-up world axis) lives in `conventions.py`; that's the one place to adjust
  if anything looks mirrored.
- **Lens distortion** — `rr.Pinhole` models an ideal pinhole (no distortion
  coefficients); images are shown as the simulator produces them. The control
  panel lets you *change* distortion parameters and watch the streamed image
  respond.
- **Cameras in 3D** — off by default. Cameras are placed in the 3D view only with
  `--images-in-3d`, which logs a `Pinhole` per camera (drawing its frustum and
  back-projecting images into 3D). For distorted/wide cameras that projection is
  wrong (ideal-pinhole only), so by default no Pinhole is logged — meaning no
  frustums and no images can appear in 3D at all; the 3D view shows lidar + ground
  truth, and images stay in their 2D views. When enabled, the camera's local axis
  convention is assumed forward-left-up (`camera_xyz` in `loggers/image.py`).
- **No native widgets in rerun** — controls live in the Gradio panel by design.
  Property reads/writes go through `set_*_property`, which operate on raw
  UProperties in Unreal-native units (cm/degrees), independent of the
  right-handed streaming frame.

## Layout

```
ExampleClients/Python/
  tempo_panel.py       shared Gradio building blocks (AsyncBridge + property editor)
  WorldPlaygroundGUI.py  standalone property-editor GUI (uses tempo_panel; no rerun)
  RerunPlayground/
    __main__.py        entrypoint (python -m RerunPlayground)
    config.py          CLI + YAML config
    discovery.py       sensor/actor discovery -> SceneModel
    blueprint.py       SceneModel -> rerun viewer layout
    client.py          viewer init + per-stream task supervisor
    conventions.py     frames, transforms, entity paths
    colormap.py        viridis + label palettes (no matplotlib)
    streaming.py       bounded-queue producer/consumer pump
    _compat.py         rerun version shims
    loggers/
      image.py         color/depth/label/bbox/video -> rerun
      lidar.py         scan segments -> Points3D
      world.py         ActorState -> Boxes3D ground truth
    control/
      panel.py         Streams controls + the shared tempo_panel property editor
```

The property editor lives in `tempo_panel.py` and is shared with the standalone
`WorldPlaygroundGUI.py` — a get/set GUI for any running sim that needs no rerun.
