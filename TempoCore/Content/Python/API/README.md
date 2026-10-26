# tempo-sim

Python client API for the [Tempo](https://temposimulation.com) simulation server.

This package provides generated gRPC wrappers (built on
[protobuf](https://pypi.org/project/protobuf/) and
[grpcio](https://pypi.org/project/grpcio/)) for interacting with Tempo simulation
servers, exposing both synchronous and asynchronous surfaces.

## Installation

```bash
pip install tempo-sim
```

Visualization helpers and the example clients need extra dependencies:

```bash
pip install tempo-sim[examples]
```

## Quick start

```python
import tempo_sim
import tempo_sim.TempoSensors  # generated protobuf modules nest under the package

# Point the client at a running Tempo server.
tempo_sim.set_server("localhost", 10001)

# Or, for a server on this machine listening on a Unix domain socket (Linux/macOS):
tempo_sim.set_socket("sim-a.sock")

# Use the generated API modules, e.g. tempo_sensors, tempo_world.
from tempo_sim import tempo_sensors
```

The API is organized into per-service modules:

- `tempo_core` / `tempo_core_editor`
- `tempo_agents` / `tempo_agents_editor`
- `tempo_geographic`
- `tempo_movement`
- `tempo_sensors`
- `tempo_world`

Each wrapper takes its request's fields as keyword arguments, and works from synchronous and
asynchronous code alike:

```python
from tempo_sim import tempo_world

rig = tempo_world.spawn_actor(actor_type="BP_SensorRig")
tempo_world.destroy_actor(actor=rig.name)
```

Common helpers (`set_server`, `set_socket`, `run_async`) are re-exported at the package root.
Prefer brevity? `import tempo_sim as tempo`.

## The reflected API

The package also carries the runtime of Tempo's
[reflected API](https://tempo-sim.readthedocs.io/en/latest/clients/reflected-api/), which reaches a
build's properties and functions through generated, type-checked classes:

- `tempo_sim.unreal`: Unreal's value types (`FVector`, `FRotator`, `FTransform`, ...), in
  centimeters, degrees and left-handed, and the handle every generated class is built on.
- `tempo_sim.world`: finding, spawning and moving actors through those handles.
- `tempo_sim.conversion`: `ue_to_si` and `si_to_ue`, between Unreal's values and the API's meters,
  radians and right-handed frame.

The classes themselves are generated per project, from its packaged build, and ship in that
project's own package (`Packaged/API/Python/<project>/`), which depends on this one.

## License

Licensed under the [Apache License, Version 2.0](LICENSE).
