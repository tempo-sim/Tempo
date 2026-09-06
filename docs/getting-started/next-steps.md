# Next Steps

You have a simulation running and a client driving it. Here is where to go depending on what you
are building.

## Learn how Tempo thinks

Three ideas explain most of the API's surprises. Read them once and the rest of the
documentation reads more easily.

<div class="grid cards" markdown>

-   **[Time](../concepts/time.md)**

    ---

    Wall Clock vs. Fixed Step, why time is fixed-point, and what "step" actually advances.

-   **[Units and coordinates](../concepts/conventions.md)**

    ---

    The API speaks meters, radians and right-handed — with one deliberate exception you need to
    know about.

-   **[Naming](../concepts/naming.md)**

    ---

    How actors, components and classes are addressed over the wire, and why editor labels are not
    safe to hard-code.

</div>

## Build your simulator

| If you want to… | Go to |
|---|---|
| Add cameras or lidar and stream data out | [TempoSensors](../plugins/tempo-sensors.md) |
| Spawn, move, inspect or drive actors from a client | [TempoWorld](../plugins/tempo-world.md) |
| Drive vehicles, move pawns, or follow a trajectory | [TempoMovement](../plugins/tempo-movement.md) |
| Anchor the sim to a place and time on Earth | [TempoGeographic](../plugins/tempo-geographic.md) |
| Populate a scene procedurally | [TempoPCG](../plugins/tempo-pcg.md) |
| Simulate crowds and traffic at massive scale | [TempoAgents](../plugins/tempo-agents.md) |
| Publish to ROS 2 topics | [TempoROS](../plugins/tempo-ros.md) and [TempoROSBridge](../plugins/tempo-ros-bridge.md) |

## Write client code

- **[Client APIs](../clients/index.md)** — how the generated Python, Rust and C++ clients are
  built, and how to publish them for your users.
- **[Example clients](../clients/examples.md)** — the playgrounds shipped with Tempo, including
  a full [rerun](https://rerun.io) visualizer.
- **[gRPC API reference](../reference/api/index.md)** — every RPC, message and field.

## Add your own RPCs

Tempo's server is not a closed API. Any module in your project can define its own `.proto`
services and get generated Python, Rust and C++ clients for free.

[:octicons-arrow-right-24: Adding your own services](../guides/custom-services.md)

The [Greeter](https://github.com/tempo-sim/Greeter/) plugin — included in TempoSample — is a
bare-bones, end-to-end example you can read in five minutes.

## Ship it

- **[Packaging](../guides/packaging.md)** — building a standalone binary, and what changes about
  actor naming when you do.
- **[Continuous integration](../guides/continuous-integration.md)** — the reusable GitHub Actions
  workflows, and how to cut ~10–15 minutes per run with a pre-modded engine image.
- **[Testing](../guides/testing.md)** — the C++ automation tests and the packaged-build client
  API test suites.
