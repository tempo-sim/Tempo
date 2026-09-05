---
hide:
  - navigation
---

# Tempo

![The TempoSample street sweeper with live sensor overlays](https://github.com/user-attachments/assets/a1433caf-60fd-4db0-b6ab-ebdd0d3e2dc5){ loading=lazy }
/// caption
The [TempoSample](https://github.com/tempo-sim/TempoSample) project: a street sweeper driving the
Lower Sector environment, with camera and lidar output overlaid.
///

Tempo is a collection of simulation-focused plugins for Unreal Engine. It makes the power of
Unreal accessible to simulation and robotics developers, with plugins for client APIs, sensor
simulation, agent behaviors, and more.

Tempo is the foundation on which you build a simulator for *your* application — a set of
composable plugins, not a turnkey product.

<div class="grid cards" markdown>

-   :material-rocket-launch:{ .lg .middle } **Get running in an hour**

    ---

    Install the prerequisites, add Tempo to a project, build it, and drive a simulation from a
    Python REPL.

    [:octicons-arrow-right-24: Getting Started](getting-started/index.md)

-   :material-lightbulb-on:{ .lg .middle } **Understand the model**

    ---

    Deterministic time, the units and coordinate frame the API speaks, and how actors are named
    over the wire.

    [:octicons-arrow-right-24: Concepts](concepts/index.md)

-   :material-puzzle:{ .lg .middle } **Explore the plugins**

    ---

    Time and the gRPC server, world control, sensors, movement, agents, geography, procedural
    content.

    [:octicons-arrow-right-24: Plugins](plugins/index.md)

-   :material-api:{ .lg .middle } **Look up an RPC**

    ---

    Every service, RPC, message and field — generated from the `.proto` files on every build.

    [:octicons-arrow-right-24: gRPC API Reference](reference/api/index.md)

</div>

## Why Tempo

Tempo is for building your *own* robotics simulator on modern Unreal Engine and driving all of it
from code. Among great simulators (CARLA, AirSim/Colosseum, Gazebo, Webots, Isaac Sim, Genesis),
here is an honest look at where it fits.

### What Tempo is good at

| Key feature | What it means for your simulator |
|---|---|
| **Modern Unreal + ecosystem** | Runs on UE 5.7 / 5.8 — Lumen, Nanite, PCG, Niagara, MetaHuman, Chaos, and the huge library of real-time content the engine community ships, all for free. |
| **gRPC API, no ROS required** | The primary interface is [gRPC](https://grpc.io): language-agnostic, schema-first, with HTTP/2 streaming. Clients connect across machines and platforms (Rust on Linux → sim on a Mac) — a step up from the older msgpack-RPC interfaces some sims use, and not tied to an in-process language. |
| **Code-generated, reflection-based control** | One set of `.proto` files generates Python / Rust / C++ clients (sync + async). Spawn any actor and get/set *any* property over the wire — no engine code needed. |
| **High-fidelity sensors** | Cameras with multiple lens models (incl. wide-FOV fisheye), semantic + instance segmentation, 2D bounding boxes, and hardware H.264 streaming; lidar with per-beam calibration and material-derived reflectivity. |
| **Deterministic time** | Pause / play / step and wall-clock vs. fixed-step time, all over the API — built for reproducible runs and data generation. |
| **Native, optional ROS 2** | rclcpp runs in-process (no separate bridge process) and is entirely optional. |
| **Runs where you work** | Linux, Windows, and **macOS** (unusual among photorealistic engine-based sims). Develop locally on the hardware you already have. |
| **Extensible by design** | Tempo is built entirely as Unreal plugins (plus a few engine patches). If you can build it in Unreal, you can build it alongside Tempo. |

### Where another tool may fit better

| If you need… | Consider |
|---|---|
| Massive-scale parallel RL (thousands of GPU envs, Gym-style) | Isaac Lab, Genesis |
| Contact-rich / legged-robot physics fidelity | Isaac Sim (PhysX), Genesis (differentiable) |
| To drop in existing robot descriptions (URDF/SDF/USD) | Gazebo, Isaac Sim |
| A turnkey AV stack with prebuilt maps & scenarios | CARLA |

**In short:** Tempo is the strongest fit for a **photorealistic, deeply customizable simulator on
modern Unreal, controlled entirely from code, that runs on the hardware your team already
has** — especially if you're comfortable in Unreal. It's a younger project with a smaller
community and content library than the largest established sims, and it deliberately leaves the
last mile — your scenarios, robots, and application — to you. That trade-off is the point.

## A taste of the API

Everything below is a client talking to a running Unreal Editor or packaged binary over gRPC.

=== "Python"

    ```python
    import tempo_sim.tempo_core as tc
    import tempo_sim.tempo_world as tw
    import tempo_sim.TempoCore.Time_pb2 as Time

    tc.set_time_mode(Time.TM_FIXED_STEP)      # deterministic, reproducible time
    tc.set_sim_steps_per_second(10)

    rig = tw.spawn_actor(actor_type="BP_SensorRig")
    tw.set_float_property(actor=rig.name, component="TempoCamera",
                          property="FOVAngle", value=60.0)

    for _ in range(100):
        tc.step()                              # advance exactly 0.1 s
    ```

=== "Rust"

    ```rust
    use tempo_sim::{set_server, tempo_core, tempo_world};

    fn main() -> Result<(), tempo_sim::TempoError> {
        set_server("localhost", 10001);
        let rig = tempo_world::spawn_actor("BP_SensorRig".into(), /* … */)?;
        tempo_core::step()?;
        Ok(())
    }
    ```

=== "C++"

    ```cpp
    #include <tempo.h>

    int main() {
        tempo::set_server("localhost", 10001);
        auto rig = tempo::tempo_world::spawn_actor("BP_SensorRig");
        tempo::tempo_core::step();
    }
    ```

## Watch it work

<div class="grid cards" markdown>

-   :material-play-circle:{ .lg .middle } **What Tempo does**

    ---

    A tour of sensors, agents, and world control running in TempoSample.

    [:octicons-arrow-right-24: Overview video](https://github.com/user-attachments/assets/dfc7b28b-3b73-4603-a779-dd6e5b2acec9)

-   :material-play-circle:{ .lg .middle } **Setting it up**

    ---

    The setup, build, run and Hello World steps, narrated end to end. Sound on!

    [:octicons-arrow-right-24: Getting started video](https://github.com/user-attachments/assets/849cde96-a5a0-46e7-ab18-4fcbbc9fea8d)

</div>

## Getting help

Not sure where to start? Want guidance from the authors? Find us on
[:fontawesome-brands-discord: Discord](https://discord.gg/bKa2hnGYnw).

Something not working as expected, or a key feature missing? Send us an
[issue](https://github.com/tempo-sim/Tempo/issues). Want to contribute? We'll be happy to review
your PR.
