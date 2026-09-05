# Hello World

With your project open in Unreal Editor, you'll start the simulation, add a sensor to the world,
stream images from it, change one of its properties while it streams, and step time — all from
outside the engine.

!!! note "No installation needed"

    You don't have to install any Python package or dependency. The build already generated the
    `tempo_sim` package and a virtual environment for you.

## 1. Activate the Tempo environment

From the root of your project:

=== "Linux / macOS"

    ```bash
    source ./TempoEnv/bin/activate
    python
    ```

=== "Windows"

    ```bash
    source ./TempoEnv/Scripts/activate
    winpty python
    ```

## 2. Start the simulation

```python
import tempo_sim.tempo_core_editor as tce

tce.play_in_editor()   # the simulation should begin
```

## 3. Spawn a sensor rig

```python
import tempo_sim.tempo_world as tw

tw.spawn_actor(actor_type="BP_SensorRig")
```

An actor with a tripod mesh appears. It carries a `TempoCamera` on top — which may not be
visible, since a camera component has no mesh of its own.

`BP_SensorRig` ships in `TempoSensors/Content/SensorRig/`, so it is available in any project
using Tempo. See [TempoWorld](../plugins/tempo-world.md) for what else `spawn_actor` can do —
deferred spawns, relative transforms, and spawning any C++ or Blueprint class in your project.

## 4. Stream images

From **another** terminal (leave the Python REPL open), run the included
[SensorPlayground](https://github.com/tempo-sim/Tempo/blob/main/ExampleClients/Python/SensorPlayground.py)
example client:

```bash
python ./Plugins/Tempo/ExampleClients/Python/SensorPlayground.py
```

Use it to list the available sensors and start streaming color images from the `TempoCamera`.

## 5. Change a property while it streams

Back in the Python REPL:

```python
tw.set_float_property(actor="BP_SensorRig", component="TempoCamera",
                      property="FOVAngle", value=60.0)
```

The field of view of your streaming images should narrow immediately. `FOVAngle` is not a
Tempo-specific API — it is an ordinary `UPROPERTY` reached through Unreal's reflection system,
which is how TempoWorld can get or set *any* property on *any* actor or component. See
[Getting and setting properties](../plugins/tempo-world.md#getting-and-setting-properties).

## 6. Control time

```python
import tempo_sim.tempo_core as tc
import tempo_sim.TempoCore.Time_pb2 as Time

tc.pause()                              # time stops
tc.play()                               # time resumes
tc.set_time_mode(Time.TM_FIXED_STEP)    # switch to Fixed Step; the sim runs faster than real time
tc.step()                               # advance to the next whole fixed step (0.1 s by default)
tc.step()                               # advance exactly one step — and get exactly one new image
```

That last pair is the point of Fixed Step mode: one step, one frame, one sensor measurement, every
time, regardless of how fast the machine renders. See [Time](../concepts/time.md) for what each
mode guarantees and why the first step after switching is a partial one.

## You're up and running

Congratulations. Keep experimenting: create new scenes, stream more sensor types, and vary the
many properties of your simulation at runtime.

[Next steps :octicons-arrow-right-24:](next-steps.md){ .md-button .md-button--primary }
