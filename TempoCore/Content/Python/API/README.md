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

Common helpers (`set_server`, `run_async`) are re-exported at the package root.
Prefer brevity? `import tempo_sim as tempo`.

## License

Licensed under the [Apache License, Version 2.0](LICENSE).
