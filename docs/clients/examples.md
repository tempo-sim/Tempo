# Example Clients

Tempo ships working clients you can run against any Tempo sim. They double as reference code —
each one is a readable demonstration of one part of the API.

They live under `Plugins/Tempo/ExampleClients/`.

!!! tip "Install the extras first"

    The richer examples need visualization dependencies:

    ```bash
    source ./TempoEnv/bin/activate
    pip install "tempo-sim[examples]"
    ```

## Python

### Interactive playgrounds

Each is a REPL with fuzzy tab-completion over the live simulation, built on `prompt_toolkit`.

| Client | What it exercises |
|---|---|
| **`SensorPlayground.py`** | List available sensors, add a sensor to an actor at runtime, randomize post-process settings, start and stop streams, record streams to disk. |
| **`WorldPlayground.py`** | Query actors and components, read and write properties, spawn, move and destroy things. |
| **`MovementPlayground.py`** | List commandable pawns and vehicles, then drive them. |

```bash
python ./Plugins/Tempo/ExampleClients/Python/SensorPlayground.py
python ./Plugins/Tempo/ExampleClients/Python/WorldPlayground.py --ip localhost --port 10001
```

### Visualizers

**`LidarPreview.py`** — a minimal example: stream a single lidar at a target rate and visualize
returns colorized by distance, intensity, or label.

```bash
python ./Plugins/Tempo/ExampleClients/Python/LidarPreview.py \
    --name TempoLidar --colorize_by intensity --update_rate 30
```

**`WorldPlaygroundGUI.py`** — the graphical counterpart to `WorldPlayground.py`. A standalone
[Gradio](https://gradio.app) page: pick an actor and optionally a component, load its properties,
and edit them live.

```bash
python ./Plugins/Tempo/ExampleClients/Python/WorldPlaygroundGUI.py
# then open the printed http://localhost:<port> URL
```

### RerunPlayground

The most complete example. It streams **every available sensor** plus the **ground-truth world
state** into the [rerun](https://rerun.io) viewer, builds a viewer layout automatically, and
exposes a small web control panel for editing properties live.

```bash
python ./Plugins/Tempo/ExampleClients/Python/RerunPlayground.py --auto
# or, from that directory:  python -m RerunPlayground
```

What it does:

- **Auto-discovery** — enumerates sensors and actors and builds the visualization with no
  per-scene configuration.
- **All sensors** — color / depth / label images, 2D bounding boxes, H.264 video (optional), and
  lidar point clouds. Each is placed in 3D using the sensor's `capture_transform` and timestamped
  on a shared `sim_time` timeline.
- **3D ground truth** — nearby actors drawn as oriented boxes with labels; ego speed logged to a
  telemetry time series; overlap events to an events log.
- **Auto layout, persistent** — a 3D world view plus a grid of per-sensor 2D views, a telemetry
  plot, and an events log. The layout is sent as the viewer's *default*, so once you rearrange and
  save your own it survives restarts. `--reset-layout` restores the generated one.
- **Live control** — a Gradio page at `http://localhost:7860` with two sections. **Streams**:
  choose each lidar's signal (intensity / distance / reflectivity / label / color) and toggle which
  images each camera streams, at runtime. **Properties**: pick an actor or component, load its
  properties, and edit them — filter by `distort` to reach a camera's lens-distortion parameters.

By default only **color images and lidar** stream; depth, labels and bounding boxes are off, since
they cost bandwidth and CPU and crowd out color real estate when there are many cameras. Enable
them with `--depth` / `--labels` / `--bboxes`, or per-camera from the Streams panel.

It works on Linux, macOS and Windows — rerun ships native viewers for all three.

### Notebook

TempoSample includes `Content/Python/ExampleClients/TempoSimExamples.ipynb`:

```bash
source ./TempoEnv/bin/activate
pip install jupyter
jupyter lab
```

Remember that all notebook code runs in an `async` context, so use the asynchronous form of every
Tempo call — see [Connecting](connecting.md#notebooks).

## Rust

`ExampleClients/Rust/` mirrors three of the Python playgrounds: `SensorPlayground`,
`WorldPlayground` and `MovementPlayground`. `SensorPlayground` additionally demonstrates H.264
decode via `ffmpeg-next`.

```bash
cd Plugins/Tempo/ExampleClients/Rust/WorldPlayground
cargo run
```

Requires the Rust client to have been generated — see [Rust](rust.md).

## C++

`ExampleClients/cpp/` contains the same three, as small stdout-only programs. Build instructions
are on the [C++ client page](cpp.md#example-clients).

## Reading them as reference

If you are unsure how to do something over the API, these are the fastest answer:

| Question | Look at |
|---|---|
| How do I add a camera at runtime? | `SensorPlayground.py`, the `flow_add_sensor` flow |
| How do I decode a lidar scan? | `LidarPreview.py` and `tempo_sim.TempoLidarUtils` |
| How do I decode depth or label images? | `tempo_sim.TempoImageUtils` |
| How do I edit properties from a GUI? | `tempo_panel.py`, shared by `WorldPlaygroundGUI` and `RerunPlayground` |
| How do I call the API from another thread? | `tempo_panel.py`'s `AsyncBridge` |
