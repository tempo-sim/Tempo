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
   pedestrian, sky, the ego car itself, …) from a JSON rule file, no binary assets;
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
with the car (lidar points colored by class, actor boxes) beside each camera's color and label
images.

```bash
CitySampleAV.py --vehicle bus10                  # a specific model (ids in config/vehicles.json)
CitySampleAV.py --vehicle truck                  # any vehicle of a category
CitySampleAV.py --vehicle random --seed 7        # reproducible random choice (the default is random)
CitySampleAV.py --record out/run1                # visualize and record
CitySampleAV.py --no-viz --record out/run1 --sim-duration-s 120   # 1200 frames per sensor, headless client
CitySampleAV.py --save-rrd run1.rrd              # keep the Rerun session too (raw images: ~1.5 GB/min)
CitySampleAV.py --label-type instance            # instance ids + 2D bounding boxes
CitySampleAV.py --lidar-color intensity          # or distance, reflectivity, instance, color
CitySampleAV.py --rig bus                        # one rig for every vehicle
CitySampleAV.py --rate-hz 30                     # every sensor at 30 Hz instead of its JSON rate
CitySampleAV.py --viewport                       # keep rendering the game's own window (off by default)
CitySampleAV.py --coverage                       # on exit, list meshes no label rule matched
```

If Mass releases the car (it can, at the end of a route), the client notices, adopts another, and
mounts the rig again. Streams pick up where they left off, under the same names.

## Configuration

Everything is in [`config/`](config):

| File | What it says |
|---|---|
| `sensors/*.json` | What each sensor *is*: rate, resolution, FOV, lens model (Pinhole, BrownConrady, Rational, KannalaBrandt, DoubleSphere) and coefficients; lidar beams, FOV, range, or a per-channel `elevations_deg` beam pattern. |
| `rigs/*.json` | Which sensors a vehicle carries and **where**: an `anchor` (`roof_center`, `front_top`, `front_bumper`, `rear_bumper`, `origin`) computed from the vehicle's own bounding box, plus an offset and orientation, in the vehicle frame (x forward, y left, z up, meters). One rig therefore fits a coupe and a van; `bus.json` shows a category with its own layout. |
| `vehicles.json` | The 13 CitySample models traffic drives, their category, and which rig each gets. |
| `labels.json` | The semantic classes, their colors, and the rules that assign them. |

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

**Semantic or instance.** Tempo's label mode is global, for cameras and lidar alike. Semantic (the
default) gives class ids. Instance (`--label-type instance`) gives per-object ids and 2D bounding
boxes, but ids are only 1–253 and every labeled object in view needs one. In a city that includes
every building part, so ids are reused, and the semantic class recovered from an instance id is
approximate. Use instance mode for the boxes of dynamic objects.

**Known gaps**, as a starting point for iterating:

- **Sky reads as `unlabeled` (0).** CitySample draws its sky with a sky atmosphere, which is not a
  mesh, so the `sky` rule (`SM_SkySphere`) has nothing to match. Treat 0 as sky/background.
- **Lane markings read as `road`.** The `SM_DECAL` meshes are decal materials, which don't write
  custom depth, so the road beneath them shows through.
- **Instance ids are scarce in a city** (see above).

## Recording format

`--record DIR` writes, with poses in Tempo's frame (meters, radians, right-handed, Z up):

```
meta.json                 vehicle, rig, label classes, options, frames written / dropped
calib/<sensor>.json       intrinsics, extrinsics in the vehicle frame
ego.jsonl                 the ego's pose, velocity and bounds per sample
actors.jsonl              nearby actors' poses and bounds per sample
<sensor>/index.jsonl      per frame: sequence id, sim time, sensor pose in the world
<camera>/color/NNNNNN.png
<camera>/label/NNNNNN.png 8-bit label ids
<camera>/depth/NNNNNN.npy float32 meters (--record-depth)
<camera>/bbox/NNNNNN.json instance mode only
<lidar>/NNNNNN.npz        xyz (sensor frame), distance, intensity, label, ring, azimuth, elevation
```

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

What the client collects is what the sim renders: sensors capture in the game loop, so the slowest
frame decides. The default rig is:
- `RoofLidar`: 1024 × 64 beams, 360° × 45°, 0.5–120 m. Rendered as 3 tiles, so 3 segments per scan.
- `FrontCamera`: 1280 × 720 pinhole, 90° horizontal FOV.
- `FisheyeCamera`: 1024 × 1024 KannalaBrandt, 170°, 1.5× upsampling. Rendered as several
  perspective tiles, then stitched.

The game's own window is **not rendered** while collecting, since only the sensors need the GPU.
`--viewport` turns it back on. On exit the client prints what each stream delivered.

Measured on an i9-14900KF, RTX 4080 and 64 GB RAM, on Windows with the packaged game.

**Fixed Step (the default): 10 steps/s, every sensor at 10 Hz.** 60 s of sim time took 96 s of
wall time, a real-time factor of 0.63. That run delivered:
- **600 frames from each camera** (color and label), **599 lidar scans**: one per step for every
  sensor, all on the same 0.1 s grid. That's **6.2 synchronized frames per second of wall time, per
  sensor**.
- no frames dropped, and the recording complete;
- the ego covering 280 m: mean 4.7 m/s with stops at lights, peaking at 8.9 m/s.

**Wall Clock (`--wall-clock`)**, for comparison, with sensors requested at 30 Hz
(`--rate-hz 30`) and 45–60 s per run. "Game FPS" comes from the game's log:

| Rig | Game FPS | Delivered |
|---|---|---|
| lidar only | ~21 | 9.5 scans/s |
| pinhole only | ~28 | 21 color + 21 label frames/s |
| fisheye only | ~15 | 9 color + 9 label frames/s |
| **default (all three)** | **~10** | **~6 color + ~5.5 label frames/s per camera; 2.4 lidar scans/s** |
| default, at the JSON's 10 Hz | ~10 | ~4.5 frames/s per camera; 2.5 scans/s |
| default, game window rendering | ~10 | ~5.6 frames/s per camera; 2.3 scans/s |
| default, recording to disk | ~10 | same as without; no frames dropped |

In Wall Clock each sensor captures only when a frame happens to be due, so with all three the lidar
gets fewer than half as many scans as in Fixed Step, and the sensors aren't captured together.
The client dropped nothing in any run, and recording cost nothing measurable. The limit is GPU time
per game frame. The fisheye and the 360° lidar are the expensive sensors, because each renders
several views every capture. To collect faster:
- drop or shrink the fisheye (`upsampling_factor` 1.0 alone gains little);
- use fewer `horizontal_beams`, or a narrower `horizontal_fov_deg`, so the lidar renders fewer tiles;
- lower the engine scalability (`tempo_core.set_engine_scalability`).

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
