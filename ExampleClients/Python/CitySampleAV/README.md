# CitySampleAV — synthetic AV sensor data from Epic's CitySample

A turnkey starting point for using [CitySample](https://www.unrealengine.com/marketplace/en-US/product/city-sample)
as an autonomous-vehicle sensor simulator. One Python client:

1. **adopts a car CitySample's Mass traffic is already driving** — it follows its lanes, stops at
   lights and yields to other traffic, with no driving code here — choosing the vehicle by model,
   by category, or at random;
2. **mounts a sensor rig on it**, described in JSON per vehicle category: by default a 64-channel
   lidar, a 90° pinhole camera and a 170° fisheye;
3. **configures the sensors through the reflected API** (`city_sample.reflected`, generated for
   this exact build when you package), so every property it writes is type-checked;
4. **labels the city** with semantic classes (road, sidewalk, building, vehicle types,
   pedestrian, sky, the ego car itself, …) and per-object instance ids, from a JSON rule file, no
   binary assets, with 2D and 3D bounding boxes of the vehicles and pedestrians;
5. **visualizes** everything live in [Rerun](https://rerun.io) and/or **records** a plain dataset
   to disk.

It is a bootstrap, not a finished product: read it, change the JSON, and iterate.

## Setup

From a fresh CitySample (UE 5.8) with Tempo cloned into `Plugins/Tempo`, under Git Bash on Windows:

```bash
cd Plugins/Tempo
./Scripts/Setup.sh      # also turns off FastGeo for CitySample's cook (see "Why no FastGeo")
./Scripts/Build.sh
./Scripts/Package.sh    # the .sh, not the .bat: it also generates the reflected API and the wheels
```

Then install the two Python clients the package produced, and this example's dependencies
(Python 3.10+):

```bash
cd ../..                # the project root
pip install Packaged/API/Python/tempo-sim/*.whl Packaged/API/Python/city-sample/*.whl
pip install -r Plugins/Tempo/ExampleClients/Python/CitySampleAV/requirements.txt
```

## Run

Start the game, or let the client start it:

```bash
python Plugins/Tempo/ExampleClients/Python/CitySampleAV.py --launch Packaged/Windows/CitySample.exe
```

The game opens on foot in the big city. Within a few seconds the client picks a car out of the
traffic nearby, the game's camera jumps to it, and a Rerun viewer opens with a 3D view riding along
with the car (lidar points colored by reflectivity, 3D boxes of the vehicles and pedestrians around it)
beside a column per camera: its color image with its 2D boxes, its semantic labels and its depth.
The viewer keeps the latest 2 GB and drops older data (`--viewer-memory-limit`). The game's own
window goes black: the sensors render offscreen, and drawing the window too would cost them GPU time.

```bash
CitySampleAV.py --vehicle van01                  # a specific model (ids in config/vehicles.json)
CitySampleAV.py --vehicle truck                  # any vehicle of a category
CitySampleAV.py --vehicle random --seed 7        # reproducible random choice (the default is random)
CitySampleAV.py --record out/run1                # visualize and record
CitySampleAV.py --no-viz --record out/run1 --sim-duration-s 120   # 1200 frames per sensor, headless client
CitySampleAV.py --save-rrd run1.rrd              # keep the Rerun session too (raw images: ~1.5 GB/min)
CitySampleAV.py --label-type semantic            # semantic ids straight from the sim, no 2D boxes
CitySampleAV.py --lidar-color label              # or intensity, distance, instance, color (default reflectivity)
CitySampleAV.py --rig my_rig                     # config/rigs/my_rig.json for every vehicle
CitySampleAV.py --rate-hz 30                     # every sensor at 30 Hz instead of its JSON rate
CitySampleAV.py --viewport                       # keep rendering the game's own window (off by default)
CitySampleAV.py --instance-view --no-depth-view  # show each camera's instance ids instead of its depth
CitySampleAV.py --viewer-memory-limit 8GB        # keep more history in the viewer (default 2GB)
CitySampleAV.py --coverage                       # on exit, list meshes no label rule matched
```

If Mass releases the car (it can, at the end of a route), the client notices, adopts another, and
mounts the rig again. Streams pick up where they left off, under the same names.

## Configuration

Everything is in [`config/`](config):

| File | What it says |
|---|---|
| `sensors/*.json` | What each sensor *is*: rate, resolution, FOV, lens model (Pinhole, BrownConrady, Rational, KannalaBrandt, DoubleSphere) and coefficients; lidar beams, FOV, range, or a per-channel `elevations_deg` beam pattern. |
| `rigs/*.json` | Which sensors a vehicle carries and **where**: an `anchor` (`roof_center`, `front_top`, `front_bumper`, `rear_bumper`, `origin`) computed from the vehicle's own bounding box, plus an offset and orientation, in the vehicle frame (x forward, y left, z up, meters). One rig therefore fits a coupe and a van. A category can still have a layout of its own, through `rigs_by_category` in `vehicles.json`. |
| `vehicles.json` | The 12 CitySample cars, vans and trucks the client may adopt as the ego (never the bus), their category, and which rig each gets. |
| `labels.json` | The semantic classes, their colors, the rules that assign them, which are "stuff" (`"instanced": false`), and which get boxes (`box_classes`). |

The JSON uses this example's own units (meters, degrees). [`rig.py`](rig.py) converts and writes it
through typed reflected-API classes: `lidar.MaxDistance = cfg.range_m[1] * 100` against the
generated `TempoLidar`. Run Pyright over this directory and it checks every one of those writes
against the build you packaged. A property a future Tempo renames or retypes fails the check, not
the run:

```bash
pip install pyright && pyright Plugins/Tempo/ExampleClients/Python/CitySampleAV
```

Anything the schema doesn't cover can go in a sensor's `"unreal_properties"`, as raw Unreal
property names in Unreal units. That is the escape hatch, and it is unchecked.

## Labels

Tempo labels the world from a table of rows that match actors by class or tag, and components by
mesh asset. Here the table is JSON built from [`config/labels.json`](config/labels.json) and loaded
at startup with `LoadLabelTable`, so there is no binary asset and nothing changes in the project:

- **Vehicles and pedestrians** are labeled by their CitySample classes.
- **The city** (buildings, roads, sidewalks, markings, signs, lights, poles, props, sky) is
  labeled by **mesh-path regexes**, applied to whatever is loaded as World Partition streams it in.
  A table listing every mesh outright would make the sim load all of them to validate it.
- **The ego car** is tagged `TempoEgo` when it is adopted, which gives it its own class, so you can
  mask your own vehicle out of the training data. The tag goes on its components too, because the
  labeler resolves a component (by mesh) before its actor (by tag or class).

`--coverage` lists the loaded meshes no rule matched, most-instanced first. That is where to add
rules next.

**Instance ids, stuff and things.** Tempo's label mode is global, for cameras and lidar alike. The
client runs it in Instance mode: each labeled object gets an id of its own, and the 2D boxes come
from those. But the ids are 8-bit, only 1–251 for everything in view, and a city has thousands of
separately labeled road and building meshes. So the classes marked `"instanced": false` in
`labels.json` are *stuff*, as in panoptic segmentation: road, sidewalk, markings, building,
vegetation, lights, signs, poles, props and sky. Every object of a stuff class shares the class's
own id, which is reserved for it, and gets no 2D box. The *things* (vehicles, pedestrians, the
ego) have the rest of the ids to themselves.

The client turns instance ids back into classes with the labeler's instance-to-class map, which it
refreshes twice a second. So the semantic images and lidar labels are exact for stuff. A thing
labeled since the last refresh reads as unlabeled for up to half a second. `--label-type semantic`
streams class ids from the sim directly, with no 2D boxes.

**Boxes.** The classes in `box_classes` (car, van, truck, bus, vehicle_other, pedestrian) get both
kinds:

- **2D boxes are Tempo's.** Each is the extent of one instance's *visible* pixels, so a car half
  behind a truck gets a box around its visible half. They are drawn over each camera's color image.
- **3D boxes are the actors' ground truth**: local bounds placed by the actor's pose, for the
  moving actors within `--ground-truth-radius-m` (80 m) of the ego. They are drawn in the 3D view,
  colored by class.

**Known gaps**, as a starting point for iterating:

- **Far-off traffic and crowds get no boxes.** Mass draws distant cars and people with
  instanced-mesh components, one per model or part holding every copy, and a stencil can't tell
  the copies apart. So their pixels carry their class's own id, as stuff does: right in the
  semantic image, with no 2D box, and no 3D box either, since they are not actors. Vehicles and
  pedestrians near the ego are actors of their own and get both boxes, one per vehicle with its
  wheels and windows included.

- **Sky reads as `unlabeled` (0).** CitySample draws its sky with a sky atmosphere, which is not a
  mesh, so the `sky` rule (`SM_SkySphere`) has nothing to match. Treat 0 as sky/background.
- **Lane markings read as `road`.** The `SM_DECAL` meshes are decal materials, which don't write
  custom depth, so the road beneath them shows through.
- **Things can still run out of ids** in a dense scene, more than ~240 vehicles and people labeled
  at once. Ids are then shared, and boxes of two objects with one id merge.

## Recording format

`--record DIR` writes, with poses in Tempo's frame (meters, radians, right-handed, Z up):

```
meta.json                    vehicle, rig, label classes, box and stuff class ids, options,
                             frames written / dropped
calib/<sensor>.json          intrinsics, extrinsics in the vehicle frame
ego.jsonl                    the ego's pose, velocity and bounds per sample
actors.jsonl                 3D ground truth: nearby moving actors' poses, bounds and semantic_id
<sensor>/index.jsonl         per frame: frame number (NNNNNN), the sensor's own sequence id, sim
                             time, sensor pose in the world. Frame numbers run on across egos.
<camera>/color/NNNNNN.png
<camera>/label/NNNNNN.png    8-bit semantic class ids
<camera>/instance/NNNNNN.png 8-bit instance ids (a stuff class's is its class id)
<camera>/depth/NNNNNN.npy    float32 meters (--no-record-depth skips)
<camera>/bbox/NNNNNN.json    2D boxes of the box classes: inclusive pixel min/max, instance and class
<lidar>/NNNNNN.npz           xyz (sensor frame), distance, intensity, label (class), instance, ring,
                             azimuth, elevation
```

With `--label-type semantic` there is no `instance/` or `bbox/`, and `label` is from the sim.

The recorder shares the viewer's streams. If the disk can't keep up, it drops frames rather than
slowing the sim, and `meta.json` counts the drops.

**Time.** The client runs the sim in **Fixed Step**: 10 steps per second (`--step-hz`), with every
sensor at the same 10 Hz. So each step advances exactly 0.1 s of sim time and yields one frame from
every sensor, all captured in the same world state, with identical `time_s`. Match frames across
sensors by it. The game thread waits for each frame's sensor data, so the sim runs only as fast as
it can render. With this rig that is slower than real time, which costs only wall time. To collect a
fixed amount of data, stop on sim time: `--sim-duration-s 60` is 600 frames of each sensor.
`--wall-clock` runs in real time instead, with sensors capturing as frames allow, not in lockstep.

## Performance

The default rig runs at **about real time** on an i9-14900KF, RTX 4080 and 64 GB RAM. That's Fixed
Step at 10 steps/s, every sensor at 10 Hz, recording color, label, depth and lidar:

| Sensor | Settings |
|---|---|
| `RoofLidar` | 1024 × 64 beams, 360° × 45°, 0.5–120 m, participating media off. Renders 3 views (120° each). |
| `FrontCamera` | 960 × 540 pinhole, 90° horizontal FOV. Renders 1 view. |
| `FisheyeCamera` | 960 × 600 KannalaBrandt, 170° horizontal (about 106° vertical). Renders 2 views, stitched. |

Measured over 90 s of sim time with a moving car (522 m, two handovers), recording everything:
- a **real-time factor of 0.97 while streaming**, which is 9.5 synchronized frames/s from every
  sensor, every one on the same 0.1 s grid;
- nothing dropped;
- about 7 GB per sim-minute on disk.

Runs vary by about ±5% with the route.

**What it costs.** Sensors render in the game loop, and the cost is mostly *how many scene views*
each frame renders, not how many pixels. Measured the same way, with a moving car:

| Change from the default | Real-time factor |
|---|---|
| none | 0.97 |
| cameras at 1280 × 720 and 1280 × 800 | 0.89 |
| ... with the fisheye at 1024 × 640 | 0.87 |
| ... with a square 1024 × 1024 fisheye (4 views, not 2) and 1.5× upsampling | ~0.7 \* |
| not recording depth (`--no-record-depth`) | about +15–20% \* |
| writing to disk at all | no measurable cost \* |
| the Rerun viewer open | about −7%, since it shares the GPU |
| `--no-pipelined` (the game waits for each frame's readback) | about −10% \* |
| lidar participating media on | about −5% \* |
| lidar at 32 beams instead of 64; `screen_percentage` 70; scalability `high`/`medium` | no gain, or a loss \* |

\* From earlier, shorter runs, several with a stationary ego, which renders more cheaply than a moving
one. Take these as relative effects, not exact factors.

Hence the choices in the defaults:
- **A wide fisheye, not a square one.** A fisheye renders one view per 120° of each axis, so a
  square 170° image costs 4 full scene renders and a 16:10 one costs 2. Wide is also how
  automotive surround-view cameras are built.
- **The game's own window is off** while collecting; `--viewport` keeps it.
- **Pipelined rendering is on.** Each frame still carries the sim time and sequence id of the step
  it was captured on; data just arrives a frame or two later.
- **Participating media is off.** CitySample's streets have no smoke or fog for it to model.

On exit the client prints what each stream delivered per second of wall time and the real-time
factor while streaming. While the rig moves to a new car, nothing renders, so those seconds are
left out. Each index entry also records `received_wall_time`.

**Wall Clock** (`--wall-clock`) doesn't capture in lockstep. Each sensor captures only when a frame
happens to be due, so with all three sensors the lidar delivered under half the scans it gets in
Fixed Step.

**Moving cars.** CitySample's traffic often waits a minute or more at lights and in jams. That
happens in Wall Clock and Fixed Step alike, and a stationary ego records the same frame over and
over. So once the ego has stood still for `--max-stopped-s` (default 30) of sim time, the client
moves the rig to another car that is moving. Each handover leaves a gap of about a second in the
recording; match frames by `time_s`.

## How it works

- [`ego.py`](ego.py) adopts the car. Mass only keeps a car as a physics actor while it is near a
  viewer, and the viewer is the player's camera, so the client points the player controller's view
  at the ego (`SetViewTargetWithBlend`, through the reflected API). The car then stays a physics
  actor, and World Partition streams the city around it. No game code changes.
- [`rig.py`](rig.py) adds the sensor components (`world.add_component`) and configures them in one
  batch.
- [`labels.py`](labels.py) builds and loads the label table and keeps classifying streamed-in
  meshes.
- [`client.py`](client.py) runs the streams and restarts them on a new car when the ego is lost.
  The stream plumbing, Rerun loggers and layout come from
  [`../tempo_rerun`](../tempo_rerun), shared with RerunPlayground.

## Why no FastGeo

CitySample (5.6+) cooks most of its static city through Epic's FastGeoStreaming plugin, which
turns actors into lightweight primitives that are not components. Tempo's labeler sets each
component's custom-depth stencil, so it cannot label them. Worse, they don't draw into custom depth
at all, so the label of whatever is behind them shows through: the sky's label where a building
stands. `Setup.sh` (through `Scripts/ConfigureProject.sh`) therefore adds an override to the
project's `Config/DefaultEngine.ini` that turns FastGeo off for the cook, which is how CitySample
shipped before 5.6. It costs some streaming performance. The same block gives the levels Tempo's
world settings, which Fixed Step needs, and turns on physics substepping, so a 0.1 s step simulates
0.1 s of physics. `Scripts/ConfigureProject.sh -restore` removes it, and `Setup.sh -keep-fastgeo`
leaves FastGeo on.
